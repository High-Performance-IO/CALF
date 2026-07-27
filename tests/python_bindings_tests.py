from importlib.metadata import version
import json
import os
from pathlib import Path
import subprocess
import sys

import calf
import pytest


@pytest.fixture(scope="session", autouse=True)
def configure_log_directory(tmp_path_factory):
    log_directory = tmp_path_factory.mktemp("calf-python-tests")
    os.environ["CALF_LOG_DIR"] = str(log_directory)
    os.environ["CALF_LOG_PREFIX"] = "python_"


def test_stl_logger_writes_messages_and_exposes_path():
    logger = calf.StlLogger(
        "scope message", invoker="python_test", file="test.py", line=12
    )
    logger.log("event message")
    path = Path(logger.log_file_name)
    logger.close()

    calf.StlLogger("flush", invoker="python_test").close()

    assert path.is_file()
    assert path.name.startswith("python_")
    output = path.read_text()
    assert '"invoker": "python_test"' in output
    assert '"args": "scope message"' in output
    assert '"args": "event message"' in output
    assert '"ts_exit":' in output
    document = json.loads(output)
    assert len(document) >= 2
    assert document[-2]["invoker"] == "python_test"
    assert document[-1]["invoker"] == "python_test"


def test_logger_alias_and_context_manager():
    assert calf.Logger is calf.StlLogger
    with calf.Logger("context scope") as logger:
        logger.log("context event")
    with pytest.raises(RuntimeError, match="logger is closed"):
        logger.log("too late")


def test_stdout_logger_options_and_output(capfd):
    options = calf.StdoutLoggerOptions()
    options.print_header = False
    options.use_color = False
    options.workflow_name = "python-tests"
    options.color = calf.CLI_LEVEL_WARNING
    calf.StdoutLogger.set_options(options)

    configured = calf.StdoutLogger.get_options()
    assert not configured.print_header
    assert not configured.use_color
    assert configured.workflow_name == "python-tests"

    calf.StdoutLogger.print("direct output", invoker="python_test")
    with calf.StdoutLogger("scope output", invoker="python_test") as logger:
        logger.log("event output")

    captured = capfd.readouterr()
    assert captured.out == "direct output\nscope output\nevent output\n"


def test_color_constants_are_exported():
    assert calf.CLI_LEVEL_RESET == "\x1b[0m"
    assert calf.CLI_LEVEL_STATUS == "\x1b[1;34m"
    assert calf.CLI_LEVEL_INFO == "\x1b[1;32m"
    assert calf.CLI_LEVEL_WARNING == "\x1b[1;33m"
    assert calf.CLI_LEVEL_ERROR == "\x1b[1;31m"


def test_installed_distribution_metadata_and_cli():
    assert calf.__version__ == version("capio-calf")
    result = subprocess.run(
        [sys.executable, "-m", "calf", "--help"],
        check=False,
        capture_output=True,
        text=True,
    )
    assert result.returncode == 0, result.stderr
    assert "Web inspector and profiler" in result.stdout
