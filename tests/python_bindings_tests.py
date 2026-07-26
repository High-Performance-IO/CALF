import contextlib
import importlib
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest


@contextlib.contextmanager
def capture_native_stdout():
    read_fd, write_fd = os.pipe()
    saved_stdout = os.dup(sys.stdout.fileno())
    try:
        os.dup2(write_fd, sys.stdout.fileno())
        os.close(write_fd)
        yield
    finally:
        os.dup2(saved_stdout, sys.stdout.fileno())
        os.close(saved_stdout)

    with os.fdopen(read_fd) as output:
        capture_native_stdout.output = output.read()


class PythonBindingsTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary_directory = tempfile.TemporaryDirectory(prefix="calf-python-tests-")
        os.environ["CALF_LOG_DIR"] = cls.temporary_directory.name
        os.environ["CALF_LOG_PREFIX"] = "python_"
        cls.calf = importlib.import_module("_py_calf")

    @classmethod
    def tearDownClass(cls):
        cls.temporary_directory.cleanup()

    def test_stl_logger_writes_messages_and_exposes_path(self):
        logger = self.calf.StlLogger(
            "scope message", invoker="python_test", file="test.py", line=12
        )
        logger.log("event message")
        path = Path(logger.log_file_name)
        logger.close()

        # Opening a subsequent scope flushes the preceding buffered JSON object.
        self.calf.StlLogger("flush", invoker="python_test").close()

        self.assertTrue(path.is_file())
        self.assertTrue(path.name.startswith("python_"))
        output = path.read_text()
        self.assertIn('"invoker": "python_test"', output)
        self.assertIn('"args": "scope message"', output)
        self.assertIn('"args": "event message"', output)
        self.assertIn('"ts_exit":', output)
        document = json.loads(output)
        self.assertGreaterEqual(len(document), 2)
        self.assertEqual(document[-2]["invoker"], "python_test")
        self.assertEqual(document[-1]["invoker"], "python_test")

    def test_logger_alias_and_context_manager(self):
        self.assertIs(self.calf.Logger, self.calf.StlLogger)
        with self.calf.Logger("context scope") as logger:
            logger.log("context event")
        with self.assertRaisesRegex(RuntimeError, "logger is closed"):
            logger.log("too late")

    def test_stdout_logger_options_and_output(self):
        options = self.calf.StdoutLoggerOptions()
        options.print_header = False
        options.use_color = False
        options.workflow_name = "python-tests"
        options.color = self.calf.CLI_LEVEL_WARNING
        self.calf.StdoutLogger.set_options(options)

        configured = self.calf.StdoutLogger.get_options()
        self.assertFalse(configured.print_header)
        self.assertFalse(configured.use_color)
        self.assertEqual(configured.workflow_name, "python-tests")

        with capture_native_stdout():
            self.calf.StdoutLogger.print("direct output", invoker="python_test")
            with self.calf.StdoutLogger("scope output", invoker="python_test") as logger:
                logger.log("event output")

        self.assertEqual(
            capture_native_stdout.output,
            "direct output\nscope output\nevent output\n",
        )

    def test_color_constants_are_exported(self):
        self.assertEqual(self.calf.CLI_LEVEL_RESET, "\x1b[0m")
        self.assertEqual(self.calf.CLI_LEVEL_STATUS, "\x1b[1;34m")
        self.assertEqual(self.calf.CLI_LEVEL_INFO, "\x1b[1;32m")
        self.assertEqual(self.calf.CLI_LEVEL_WARNING, "\x1b[1;33m")
        self.assertEqual(self.calf.CLI_LEVEL_ERROR, "\x1b[1;31m")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit("usage: python_bindings_tests.py <extension-directory>")
    sys.path.insert(0, sys.argv.pop())
    unittest.main()
