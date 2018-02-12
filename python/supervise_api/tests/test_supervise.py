from unittest import TestCase
import os

import supervise_api
import pathlib
import signal

def collect_children():
    collected = False
    while True:
        try:
            os.waitid(os.P_ALL, 0, os.WEXITED)
            collected = True
        except ChildProcessError:
            return collected

class TestSupervise(TestCase):
    def setUp(self):
        signal.signal(signal.SIGCHLD, signal.SIG_IGN)

    def tearDown(self):
        collect_children()

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

if __name__ == '__main__':
    import unittest
    unittest.main()
