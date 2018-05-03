from unittest import TestCase
import os

import subprocess
import supervise_api
import pathlib
import signal
import tempfile
import fcntl

def collect_children():
    collected = False
    while True:
        try:
            os.waitid(os.P_ALL, 0, os.WEXITED)
            collected = True
        except ChildProcessError:
            return collected

def is_open_fd(fd):
    try:
        fcntl.fcntl(fd, fcntl.F_GETFD)
        return True
    except OSError:
        return False

def parse_fd_set(data):
    return set(int(name) for name in data.split(b'\n') if len(name) != 0)

def open_fd_set(up_to=1000):
    return set(fd for fd in range(up_to) if is_open_fd(fd))

class TestSupervise(TestCase):
    def setUp(self):
        signal.signal(signal.SIGCHLD, signal.SIG_IGN)
        self.open_fds = open_fd_set()

    def tearDown(self):
        collect_children()
        self.assertEqual(self.open_fds, open_fd_set())

    def just_run(self, args):
        r, w = os.pipe()
        try:
            proc = supervise_api.Process(args, fds={w:w})
        except:
            os.close(r)
            os.close(w)
            raise
        os.close(w)
        proc.kill()
        proc.wait()
        proc.close()
        # we should get eof because the process should be dead
        data = os.read(r, 4096)
        self.assertEqual(len(data), 0)
        os.close(r)

    def test_basics(self):
        self.just_run(["sh", "-c", "sleep inf"])

    def test_abspath(self):
        sh_abspath = supervise_api.which("sh")
        self.just_run([sh_abspath, "-c", "sleep inf"])

    def test_relpath(self):
        sh_relpath = os.path.relpath(supervise_api.which("sh"))
        self.just_run([sh_relpath, "-c", "sleep inf"])

    def test_pathlib_Path(self):
        sh_path = pathlib.Path(supervise_api.which("sh"))
        self.just_run([sh_path, "-c", "sleep inf"])

    def test_pathlib_sigchld_sigign(self):
        signal.signal(signal.SIGCHLD, signal.SIG_IGN)
        self.just_run(["sh", "-c", "sleep inf"])
        self.assertEqual(collect_children(), False)

    def test_pathlib_sigchld_sigdfl(self):
        signal.signal(signal.SIGCHLD, signal.SIG_DFL)
        self.just_run(["sh", "-c", "sleep inf"])
        self.assertEqual(collect_children(), True)

    def test_pathlib_sigchld_handler(self):
        flag = False
        def handler(signum, frame):
            nonlocal flag
            flag = True
        signal.signal(signal.SIGCHLD, handler)
        self.just_run(["sh", "-c", "sleep inf"])
        self.assertEqual(collect_children(), True)
        self.assertTrue(flag)

    def test_executable_not_found(self):
        with self.assertRaises(FileNotFoundError):
            supervise_api.Process(["supervise_api_nonexistent_executable_aosije"])

    def test_fds(self):
        with open("/dev/null") as devnull:
            proc = supervise_api.Process(["sh", "-c", "sleep inf"], fds={0: devnull})
        proc.kill()
        proc.wait()
        proc.close()

    def test_fds_closed(self):
        with open("/dev/null") as devnull:
            pass
        # devnull is closed now
        with self.assertRaises(ValueError):
            supervise_api.Process(["sh", "-c", "sleep inf"], fds={0: devnull})

    def test_fds_closed_fileno(self):
        with open("/dev/null") as devnull:
            fdnum = devnull.fileno()
        # devnull is closed now
        with self.assertRaises(ValueError):
            supervise_api.Process(["sh", "-c", "sleep inf"], fds={0: fdnum})

    def test_fds_swap(self):
        with open("/dev/null") as devnull:
            proc = supervise_api.Process(["sh", "-c", "sleep inf"], fds={devnull.fileno(): 0, 0: devnull})
        proc.kill()
        proc.wait()
        proc.close()

    def test_fds_unopened(self):
        with open("/dev/null") as devnull:
            proc = supervise_api.Process(["sh", "-c", "sleep inf"], fds={devnull.fileno(): 0, 42: devnull})
        proc.kill()
        proc.wait()
        proc.close()

    def test_fds_unopened_noleaks(self):
        with tempfile.TemporaryFile() as temp:
            with open("/dev/null") as devnull:
                proc = supervise_api.Process(["sh", "-c", "ls -1 /proc/$$/fd; true"], fds={devnull.fileno(): 0, 42: devnull, 1:temp})
                sent_fds = self.open_fds.union(set([devnull.fileno(), 42, 1]))
            proc.wait()
            proc.close()
            temp.seek(0)
            received_fds = parse_fd_set(temp.read())
            self.assertTrue(received_fds.issubset(sent_fds))

    def multifork(self, cmd):
        # using % here to avoid needing to escape {}
        cmd = "{ echo started; %s; }" % (cmd)
        started_length = len("started\n") * 5
        args = ["sh", "-c",
                "{ %s & %s & } & { %s & %s & } & { { { %s & } & } &} &" % (cmd, cmd, cmd, cmd, cmd)]
        started, stdout = os.pipe()
        try:
            exited, w = os.pipe()
        except:
            os.close(exited)
            os.close(w)
            raise
        try:
            proc = supervise_api.Process(args, fds={w:w, 1:stdout})
        except:
            os.close(started)
            os.close(stdout)
            os.close(exited)
            os.close(w)
            raise
        os.close(stdout)
        os.close(w)
        more_length = started_length
        while more_length > 0:
            more_length -= len(os.read(started, more_length))
        proc.kill()
        proc.wait()
        proc.close()
        # we should get eof because the process should be dead
        data = os.read(exited, 4096)
        self.assertEqual(len(data), 0)
        os.close(started)
        os.close(exited)

    def test_multifork(self):
        self.multifork("sleep inf")

    def test_nohup(self):
        self.multifork("nohup sleep inf 2>/dev/null")

    def test_setsid(self):
        self.multifork("setsid sleep inf")

    def test_setsid_and_nohup(self):
        self.multifork("nohup setsid sleep inf 2>/dev/null")

if __name__ == '__main__':
    import unittest
    unittest.main()
