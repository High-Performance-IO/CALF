"""Python interface for the CAPIO Logging Facility."""

from importlib.metadata import version

from ._py_calf import (
    CLI_LEVEL_ERROR,
    CLI_LEVEL_INFO,
    CLI_LEVEL_RESET,
    CLI_LEVEL_STATUS,
    CLI_LEVEL_WARNING,
    Logger,
    StdoutLogger,
    StdoutLoggerOptions,
    StlLogger,
)

__version__ = version("CALF")

__all__ = [
    "CLI_LEVEL_ERROR",
    "CLI_LEVEL_INFO",
    "CLI_LEVEL_RESET",
    "CLI_LEVEL_STATUS",
    "CLI_LEVEL_WARNING",
    "Logger",
    "StdoutLogger",
    "StdoutLoggerOptions",
    "StlLogger",
    "__version__",
]
