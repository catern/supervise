from unittest import TestCase
import os

import supervise_api

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

    def test_executable_not_found(self):
        with self.assertRaises(FileNotFoundError):
            supervise_api.Process(["supervise_api_nonexistent_executable_aosije"])

if __name__ == '__main__':
    import unittest
    unittest.main()
