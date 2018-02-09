from unittest import TestCase
import os

import supervise_api
import pathlib

class TestSupervise(TestCase):
    def test_basics(self):
        proc = supervise_api.Process(["sh", "-c", "true"])
        proc.kill()
        proc.wait()
        proc.close()

    def test_abspath(self):
        sh_abspath = supervise_api.which("sh")
        proc = supervise_api.Process([sh_abspath, "-c", "true"])
        proc.kill()
        proc.wait()
        proc.close()

    def test_relpath(self):
        sh_relpath = os.path.relpath(supervise_api.which("sh"))
        proc = supervise_api.Process([sh_relpath, "-c", "true"])
        proc.kill()
        proc.wait()
        proc.close()

    def test_pathlib_Path(self):
        sh_path = pathlib.Path(supervise_api.which("sh"))
        proc = supervise_api.Process([sh_path, "-c", "true"])
        proc.kill()
        proc.wait()
        proc.close()

    def test_executable_not_found(self):
        with self.assertRaises(FileNotFoundError):
            supervise_api.Process(["supervise_api_nonexistent_executable_aosije"])

    def test_fds(self):
        with open("/dev/null") as devnull:
            proc = supervise_api.Process(["sh", "-c", "true"], fds={0: devnull})
        proc.kill()
        proc.wait()
        proc.close()

    def test_fds_closed(self):
        with open("/dev/null") as devnull:
            pass
        # devnull is closed now
        with self.assertRaises(ValueError):
            supervise_api.Process(["sh", "-c", "true"], fds={0: devnull})

    def test_fds_closed_fileno(self):
        with open("/dev/null") as devnull:
            fdnum = devnull.fileno()
        # devnull is closed now
        with self.assertRaises(ValueError):
            supervise_api.Process(["sh", "-c", "true"], fds={0: fdnum})

if __name__ == '__main__':
    import unittest
    unittest.main()
