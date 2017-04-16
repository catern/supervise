Summary
=======

This provides a superior API for process management and supervision on Linux,
in the form of a minimal and unprivileged utility "supervise".
supervise is designed to be used as a wrapper for child processes you execute.
Therefore supervise is designed to only wake up when there is an event to be handled,
and it otherwise consumes no CPU time.

There are three main benefits of using supervise to wrap child processes:

- The ability to get notified of child process exit or status change through a file descriptor interface
- Automatic termination of your child processes when you exit (by virtue of the fd interface)
- Guaranteed termination of all the transitive children of a child
  process; no possibility of orphans lingering on the system

It is a fully supported design goal for supervise to be usable in a nested way.
You can wrap a child process with `supervise` which in turn forks off more child processes and wraps them in `supervise`,
in arbitrary configurations and depths.

Behavior
========

supervise `fork`s and `execvp`s a single immediate child process.
supervise then monitors two file descriptors passed in at startup,
the `controlfd` and the `statusfd`
(in most use cases these will be the same fd),
as well as waiting for the immediate child process to change state.

supervise reads `signal` commands from `controlfd`,
which command supervise to send a given signal to the immediate child process.

If supervise receives a POLLHUP on the `controlfd`, it will exit.
So if you fork off a child and wrap it with supervise,
that child and all its transitive children will automatically be cleaned up when you exit.

When the immediate child process changes state, such as by exiting,
supervise writes the state change to `statusfd`.

When supervise exits, it terminates all its transitive children.
It is not possible for transitive children of supervise to escape supervise's notice,
so when supervise exits it fully cleans up all processes that were created by it or its transitive children.
This is achieved through the use of CHILD_SUBREAPER Linux API.

Invocation and use
==================

Supervise is invoked like this:

    supervise controlfd statusfd command [arg [arg...]]]

`controlfd` is the file descriptor number which supervise will read for control messages.

`statusfd` is the file descriptor number on which supervise will write status messages.

`command` and `arg`s are the command line which supervise will fork, execvp, and monitor.

supervise can be used from the shell, but it is especially useful when used from programming languages.
When used from programming languages, it should probably be wrapped in an interface looking something like this:

    spawnfd : string -> file_descriptor

which is implemented something like this:

	file_descriptor spawnfd(string command_line) {
        fdA, fdB = fdpair();
        command = "supervise ${fdA} ${fdA} ${command_line}";
        spawn(command);
	    close(fdA);
        return fdB;
	}

controlfd commands
------------------

Write the following commands to `controlfd` to have the corresponding effect.
Follow all commands with a newline.

- `signal [signum]`: Send signal number `signum` to the immediate child

Furthermore, if supervise reads an EOF/POLLHUP from `controlfd`,
indicating there are no more fds open which can write to `controlfd`,
supervise will SIGKILL all its child processes.
(supervise will exit shortly thereafter after writing the status to statusfd.)

A binary interface will be supported soon.

statusfd updates
----------------

Read the following messages from `statusfd` to learn about the corresponding status changes.
All messages are followed by a newline.

- `pid [pid number]`: We have forked off the immediate child, and this is its pid.
- `exited [status code]`: The immediate child has exited
- `killed [signal number]`: The immediate child has terminated due to a signal.
- `dumped [signal number]`: The immediate child has terminated due to a signal, and dumped core.
- `no_children`: There are no more children (and therefore no more children can start). (supervise will exit, sending `terminating`, promptly after this message.)
- `terminating`: We have just filicided and will now exit.

A binary interface will be supported soon.

When does supervise exit?
=========================

To assure you of the correctness of supervise, know that supervise will exit in these four cases:

- All our children are dead.
- A `controlfd` was passed in, and has now closed.
  This indirectly causes us to exit; first we kill all of our children, and then exit because of that.
- We received a non-SIGKILL fatal signal that was not blocked or ignored when we started.
  This also indirectly causes us to exit, killing all of our children and exiting because of that.
- Some believed-to-be-impossible syscall error happened.

In all of these cases, after we exit, all of our children will be dead.
(And our communication fds will be closed)

We will also exit in these two cases:

- We received SIGKILL
- A second believed-to-be-impossible syscall error happened during exiting

In these cases, some of our children may be able to leak.
Hopefully some day soon these holes can be removed by kernel support for an API like this.

Daemonizing
===========

For most applications,
automatic cleanup of child processes is very desirable.
In most cases, your child processes should not outlive the parent process that kicked them off.

However, sometimes you want to create a process which outlives your own.
Some example use cases:
- you are starting up a server which should live past your login session
- you are starting a new login session which shouldn't be killed just because the login server went down

Such processes are typically referred to as daemons.

Achieving this with supervise requires that the `controlfd` passed to supervise will not get an EOF/POLLHUP after its parent process dies.
If the `controlfd` is the read side of a pipe,
that means there must exist an open FD to the write side of that pipe,
even after the parent process (which presumably created the pipe and holds the write side FD) dies.

This requirement seems strange; what does it mean?
If you're curious, read the explanation in the next section.
If you just want things to work, please feel free to skip over the explanation to the section below it.

Long-winded philosophizing about approach
-----------------------------------------

This behavior is fundamentally a matter of reference counting, which is a concept fundamental to Unix and many other systems.
We want our resource to stick around
as long as there is at least one reference to that resource.
A resource, once created, should be destructed if and only if both of these properties are true:
- there are no more references
- no more references can be created.

Since typically references can only be created from existing references,
and an existing reference can always be used to create more references,
those two properties are equivalent in most systems.

In effect, supervise implements reference counting for processes,
since there is no existing way on Linux to get an (unforgeable) reference to a process.
We treat the `controlfd` passed in as a proxy for the underlying process started by supervise;
that is, we treat the reference count on the `controlfd` as a proxy for the reference count on the process.

Typically the `controlfd` will be the read end of a pipe.
For a pipe created with pipe(3),
we know that a poll(3) on the read side of the file descriptor,
will return POLLHUP if and only if there are no more references to the write side of the pipe.
So supervise knows when it gets POLLHUP that there are no more references to the write side of the pipe,
and it interprets this as meaning that there are no more references to the process.
So the process should be destructed; that is, killed.

Now we can restate what we want to do:
we want to store a reference to the write side of the pipe,
in a place outside our own process,
so that that reference is not automatically deleted when our process exits.

An immediately obvious way to do this,
is to have a long-lived server which will store file descriptors passed to it and permit retrieving them later.
In our case, however, there is no need for this.
We can just use the filesystem!

We can use a fifo created by mkfifo(3), also known as a named pipe, instead of a regular pipe created by pipe(3).
A fifo is a file visible in the filesystem,
and is a reference to both the write-side and the read-side of a pipe.
As long as the fifo exists in the filesystem,
new write-side file descriptors can be created.
Note that a file may exist in the filesystem multiple times,
and may no longer exist in its original location while still existing somewhere, through rename(3), link(3) or unlink(3).
So as long as the fifo still exists somewhere in the filesystem, possibly under a different name,
poll(3) should not return POLLHUP when polling on the read-side of a fifo.

Well, due to (what I view as) a design flaw in Unix, that's not true;
poll(3) will return POLLHUP whenever there are no write-side file descriptors currently open,
even if more could be created in the future.

So I wrote the unlinkwait utility to watch an inode,
the underlying reference-counted object behind a file or fifo,
and exit when that inode is completely removed from the filesystem.
That is, unlinkwait exits when the inode has a link count of 0;
link count is the reference count for inodes.
Once the link count hits 0, it cannot be increased,
and the only way to create new references to the file is to duplicate existing file descriptors.

We can pass a write-side file descriptor in to the unlinkwait process,
tell unlinkwait to monitor the inode at /proc/self/fd/n,
and when the fifo is completely gone from the filesystem,
the unlinkwait process will exit and the write-side file descriptor it holds will be closed.
So by using unlinkwait in conjunction with a fifo, we can have the reference counting behavior we desire.

As an interesting side note,
if an "flink" system call was added this logic would break down.
"flink" would take a path, and a file descriptor which currently has a link count of zero,
and creates a new link to that file descriptor at that path.
In fact iI think it would become fundamentally impossible in the presence of flink
to do this kind of reference counting across the filesystem and file descriptors:
link count and POLLHUP are two pieces of information read from separate interfaces,
and you would want to destruct your resource when link count is 0 and you are receiving POLLHUP,
but there's no (existing) way to check both of those atomically.
If anyone has thoughts about how to support this in the presence of flink,
feel free to send me mail.

How to run a daemon with supervise
----------------------------------

You want to run a process that outlasts any individual process,
while still getting the control and cleanup properties of supervise.

Use the unlinkwait utility in this repository and a fifo as follows,
adjusting the statusfd if you wish:

    mkfifo fifo
	unlinkwait /proc/self/fd/3 3>fifo &
	supervise 3 3<fifo 4 4>status.log [command] &

You may need to prefix `nohup` to these commands or follow up with a `disown` if you are actually running these commands from an interactive shell.

This way supervise will exit (from getting a POLLHUP on the `controlfd`) if and only if both of these are true:

- there are no open writable fds to the fifo
- the fifo (and all hardlinks to it) is removed from the filesystem.
  This is provided by unlinkwait, which is holding a writable fd to the fifo, and exits when the fifo is removed from the filesystem.

Renaming and moving the fifo is fine,
though it should not be moved between filesystems as mv(1) implements that by creating a new copy and deleting the old copy.

In other words, the lifecycle of the daemon is tied to the fifo inode,
which is something that exists in the filesystem and outlasts any individual process.

Also, conveniently, you can send commands to supervise with a simple `echo command > fifo`.

TODO
====

- Support a binary interface in addition to the text interface
- Fix the text interface to cope with partial reads and multiple lines per read
- Optimization: support reading `/proc/[pid]/task/[tid]/children`
