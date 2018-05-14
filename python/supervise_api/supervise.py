"""
A low-level utility function "dfork" and high-level wrapper class
"Process", providing a better process API for Python, based on the
supervise utility.
"""
import fcntl
import os
import socket
import shutil
import select
import signal
import errno

# Python2 doesn't have os.O_CLOEXEC
try:
    O_CLOEXEC=os.O_CLOEXEC
except:
    O_CLOEXEC=0o2000000
# ...or os.set_inheritable
try:
    set_inheritable=os.set_inheritable
except:
    def set_inheritable(fd, inheritable):
        flags = fcntl.fcntl(fd, fcntl.F_GETFD)
        if inheritable:
            newflags = flags & ~fcntl.FD_CLOEXEC
        else:
            newflags = flags | fcntl.FD_CLOEXEC
        fcntl.fcntl(fd, fcntl.F_SETFD, newflags)
# ...or os.fspath
try:
    fspath = os.fspath
except:
    def fspath(path):
        if isinstance(path, (str, bytes)):
            return path
        raise TypeError("expected str or bytes, not " + type(path).__name__)
# ...or shutil.which
try:
    which = shutil.which
except:
    from whichcraft import which

supervise_utility_location = which("supervise")
if not supervise_utility_location:
    raise FileNotFoundError(errno.ENOENT, "Executable not found in PATH", "supervise")


def ignore_sigchld():
    """Mark SIGCHLD as SIG_IGN. Doing this explicitly prevents zombies."""
    signal.signal(signal.SIGCHLD, signal.SIG_IGN)

def fileno(fil):
    """Return the file descriptor representation of the file.

    If int is passed in, it is returned unchanged. Otherwise fileno()
    is called and its value is returned as long as it is an int
    object. In all other cases, TypeError is raised.

    """
    if isinstance(fil, int):
        return fil
    elif hasattr(fil, "fileno"):
        fileno = fil.fileno()
        if not isinstance(fileno, int):
            raise TypeError("expected fileno to return an int, not " + type(fileno).__name__)
        return fileno
    raise TypeError("expected int or an object with a fileno() method, not " + type(fil).__name__)

def is_valid_fd(fd):
    """Check whether the passed in fd is open"""
    try:
        fcntl.fcntl(fd, fcntl.F_GETFD)
        return True
    except:
        return False

def update_fds(fds):
    """Update current fds with mappings in parameter.

    This is only an update, as God and Dennis Ritchie intended. Any
    fds not mentioned in the parameter are inherited as normal. They
    are certainly not closed by brute force. If you want them to be
    closed, mark them CLOEXEC!

    This performs updates using dup2. Note that file descriptor flags
    such as CLOEXEC are not copied over by dup2, so the new/updated
    file descriptors will all be inherited.

    :param fds:
        A dictionary of updates to be performed, mapping targets to sources.
        These are all done "simultaneously", so to redirect two file
        descriptors to the same changed value, instead of doing:
           { 1: desired, 2: 1 }
        You should do:
           { 1: desired, 2: desired }
        If an fd is mapped to None, it will be closed.
    :type fds: ``{ int: int/None/object with fileno() }``

    """

    ## Bookkeeping
    # copy fds, turning everything to a raw integer fd
    orig_fds = fds
    fds = {}
    to_close = []
    for target in orig_fds:
        if orig_fds[target] is None:
            to_close.append(target)
        else:
            fds[target] = fileno(orig_fds[target])

    ## Actual work
    # If a target file descriptor does not refer to an actually open
    # file descriptor, make it a copy of /dev/null.
    def ensure_target_fds_open():
        if getattr(ensure_target_fds_open, "ensured", False): return
        for target in fds.keys():
            # if a target fd is not open, open it to /dev/null
            if not is_valid_fd(target):
                if not hasattr(ensure_target_fds_open, "devnull"):
                    ensure_target_fds_open.devnull = os.open("/dev/null", os.O_RDONLY)
                # no need to keep tracking of closing these;
                # they'll get closed when we later dup2 target again
                os.dup2(ensure_target_fds_open.devnull, target)
        ensure_target_fds_open.ensured = True

    # dup, with some extra magic to avoid conflicts. Used for copied_sources.
    def dup(fd):
        # If a target fd is not already an open file descriptor, and
        # we call os.dup, os.dup may return that target FD. That would
        # cause conflicts, so before calling os.dup we must first make
        # sure all the target fds are open. (We do this lazily, only
        # in this function, to avoid unnecessarily making new fds.)
        ensure_target_fds_open()
        return os.dup(fd)

    # If a file descriptor X is both a source and a target, we will
    # insert the key-value pair (X, dup(X)) into this map. Later, when
    # performing updates for which X is a source, the stored dup(X)
    # will be used instead of X. That way, if X, as a target, is
    # overwritten by some update, that won't affect the use of X as a
    # source.
    copied_sources = {}
    try:
        for source in set(fds.values()):
            # if a file descriptor is both a source and a target,
            # remove the conflict by using a duplicate as the source
            if source in fds.keys():
                copied_sources[source] = dup(source)
        # change target fds to duplicates of the source fds
        for target, source in fds.items():
            if source in copied_sources:
                source = copied_sources[source]
            os.dup2(source, target)
    finally:
        if hasattr(ensure_target_fds_open, "devnull"):
            os.close(ensure_target_fds_open.devnull)
        for copy in copied_sources.values():
            os.close(copy)

    for target in to_close:
        os.close(target)

def dfork(args, env={}, fds={}, cwd=None, flags=O_CLOEXEC):
    """Create an fd-managed process, and return the fd.

    See the documentation of the "supervise" utility for usage of the
    returned fd. The returned fd is both the read end of the statusfd
    and the write end of the controlfd.

    Note that just because this returns, doesn't mean the process started successfully.
    This is a "low-level" function.
    The returned fd may immediately return POLLHUP, without ever returning a pid.
    The Process() class provides more guarantees.

    :param args:
        Arguments to execute. The first should point to an executable
        locatable by execvp, possible with the updated PATH and cwd
        specified by the env and cwd parameters.
    :type args: ``[PathLike or str or bytes]``

    :param env:
        A dictionary of updates to be performed to the
        environment. This is only updates; clearing the environment is
        not supported.
    :type env: ``{ str: str }``

    :param fds:
        A dictionary of updates to be performed to the fds by update_fds.
        If an fd is mapped to None, it will be closed.
    :type fds: ``{ int: int/None/object with fileno() }``

    :param cwd:
        The working directory to change to.
    :type cwd: ``PathLike or str or bytes``

    :param flags:
        Additional flags to set on the fd. Linux supports O_CLOEXEC, O_NONBLOCK.
    :type cwd: ``str``

    :returns: int -- the new file descriptor for tracking the process

    """

    # validate arguments so we don't spuriously call fork
    args = [fspath(arg) for arg in args]
    if cwd:
        cwd = fspath(cwd)
    for var in env:
        if not isinstance(var, str):
            raise TypeError("env key is not a string: {}".format(var))
        if not isinstance(env[var], str):
            raise TypeError("env value is not a string: {}".format(env[var]))
    for fd in fds:
        if not isinstance(fd, int):
            raise TypeError("fds key is not an int: {}".format(fd))
        source_fd = fds[fd]
        if source_fd is None:
            continue
        fd_fileno = fileno(source_fd)
        # test that all file descriptors are open
        if not is_valid_fd(fd_fileno):
            raise ValueError("fds[{}] file is closed: {}".format(fd, source_fd))
    executable = which(args[0], path=env.get("PATH", os.environ["PATH"]))
    if not executable:
        raise OSError(errno.ENOENT, "Executable not found in PATH", args[0])
    args[0] = executable

    parent_side, child_side = socket.socketpair(socket.AF_UNIX, socket.SOCK_SEQPACKET|flags, 0)
    # counteract Python's behavior of setting O_CLOEXEC by default
    if not (flags & O_CLOEXEC):
        set_inheritable(parent_side.fileno(), True)
    commfd = str(child_side.fileno())
    realargs = [supervise_utility_location, commfd, commfd] + args
    try:
        ret = os.fork()
    except:
        parent_side.close()
        child_side.close()
        raise
    # TOOD investigate why "ret == 0" makes supervise totally insane (it's an easy mistake)
    if ret != 0:
        # we don't care about the pid we just forked off
        child_side.close()
        return parent_side
    # we are now in the child
    parent_side.close()
    set_inheritable(child_side.fileno(), True)
    if cwd: os.chdir(cwd)
    os.environ.update(env)
    update_fds(fds)
    # stop Ctrl-C on controlling terminal from killing subprocess prematurely
    os.setsid()
    os.execvp(realargs[0], realargs)

class Process(object):
    """Run a new process and track it.

    This API is mostly compatible with Popen, excluding the constructor, but better:
    - Children will be automatically terminated on Python process exit or GC of this object.
    - All transitive children will be terminated as part of that termination.
    - File descriptor based, so one can use select/poll to be notified of changes.

    This class has a fileno() method corresponding to the underlying
    process management file descriptor, so you may simply select/poll
    for readability on this class to get notification of changes; then
    call Process.poll() or other methods to read off events.

    Like most classes, the methods on this class are not thread-safe,
    so you'll have to do your own locking if for some reason you want
    to use threads in Python.
    """
    # pid - None if not yet received
    pid = None
    # return code - positive if normal exit, negative if signalled, None if running
    returncode = None
    # true if we are certain there are no more children left (only
    # false while running and on some unclean shutdowns)
    childfree = False
    # true if we got a hangup, i.e., an unclean shutdown
    hangup = False
    def __init__(self, *args, **kwargs):
        """Follows the same argument conventions as dfork

        Throws if it can't start up the process.
        """
        self.fd = dfork(*args, **kwargs)
        self.fd.setblocking(0)

        # wait for the pid to be available
        while self.pid is None:
            if self.closed():
                raise Exception("starting process failed, couldn't even get pid")
            _ = select.select([self], [], [])
            self.flush_events()

    def closed(self):
        """Returns true if supervise communication fd is closed."""
        return self.fd.fileno() == -1

    def fileno(self):
        """Return supervise communication fd, or -1 if closed."""
        return self.fd.fileno()

    def close(self):
        """Close the supervise communication fd, killing the process and all descendents."""
        if self.returncode is None:
            self.returncode = -signal.SIGKILL
        return self.fd.close()

    def __read_event(self):
        """Read a single event from the fd.

        Returns None on EAGAIN, and an empty buffer on hangup.
        """
        if self.closed(): return None
        try:
            buf = self.fd.recv(4096)
            return buf
        except OSError as e:
            if e.errno == errno.EAGAIN:
                return None
            else:
                raise
        except socket.error as e:
            if e.errno == errno.EAGAIN:
                return None
            else:
                raise

    def __parse_event(self, buf):
        """Parse a single event"""
        data = buf.rstrip().split(b" ")
        msg = data[0]
        code = int(data[1]) if len(data) > 1 else None
        return (msg, code)

    def __handle_event(self, msg, code):
        """Handle a single event"""
        # starting up
        if msg == b"pid":
            self.pid = code
        # main child process exiting normally
        elif msg == b"exited":
            self.returncode = code
        # main child process was signalled
        elif msg == b"killed":
            self.returncode = -code
        elif msg == b"dumped":
            self.returncode = -code
        # notification about no children
        elif msg == b"no_children":
            self.childfree = True
        # supervise exiting, in one of two ways
        elif msg == b"terminating":
            # normal termination
            self.childfree = True
            self.close()
        elif msg == b"":
            # hangup! This can only happen if supervise was SIGKILL'd (or worse)
            self.hangup = True
            self.close()

    def get_event(self):
        """Return new event (oldest first), or None if no new events"""
        buf = self.__read_event()
        if buf is None:
            return None
        msg, code = self.__parse_event(buf)
        self.__handle_event(msg, code)
        return (msg, code)

    def new_events(self):
        """Return iterator over unprocessed events."""
        return iter(self.get_event, None)

    def flush_events(self):
        """Check for events, handle them, and throw them away."""
        while self.get_event() is not None:
            pass

    ## Compatibility with Popen
    def poll(self):
        """Check if process has exited."""
        self.flush_events()
        return self.returncode

    def wait(self):
        """Wait for process to exit."""
        while self.returncode is None and not self.closed():
            _ = select.select([self], [], [])
            self.flush_events()
        return self.returncode

    def send_signal(self, signum):
        """Send this signal to the main child process."""
        if self.closed():
            raise Exception("Communication fd is already closed")
        if not isinstance(signum, int):
            raise TypeError("signum must be an integer: {}".format(signum))
        self.fd.send("signal {}".format(int(signum)).encode())

    def terminate(self):
        """Terminate the main child process with SIGTERM.

        Note that this does not kill all descendent processes.
        For that, call close().
        """
        self.send_signal(signal.SIGTERM)

    def kill(self):
        """Kill the main child process with SIGKILL.

        Note that this does not kill all descendent processes.
        For that, call close().
        """
        self.send_signal(signal.SIGKILL)

    def communicate(self, _):
        """Wait for process to exit.

        Unlike Popen.communicate, this does not support actually
        sending or reading data out. Because that doesn't make sense.
        """
        self.wait()
        return (None, None)

    def __enter__(self):
        """Context manager protocol method; does nothing, returns the class"""
        return self

    def __exit__(self, exc_type, exc_value, traceback):
        """Context manager protocol method; destructs the class, killing the process"""
        self.fd.close()
