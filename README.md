<p align="center">
  <img src="./calf.svg?version=2" alt="CALF - CAPIO Logging Facility" width="720">
</p>

# CALF - CApio Logging Facility

![C++ 17](https://img.shields.io/badge/C%2B%2B-17-blue.svg)
![Python 3.10-3.14](https://img.shields.io/badge/Python-3.10--3.14-blue.svg)

[![C++ tests](https://github.com/High-Performance-IO/CALF/actions/workflows/cpp-tests.yml/badge.svg?branch=main)](https://github.com/High-Performance-IO/CALF/actions/workflows/cpp-tests.yml?query=branch%3Amain)
[![Python tests](https://github.com/High-Performance-IO/CALF/actions/workflows/python-tests.yml/badge.svg?branch=main)](https://github.com/High-Performance-IO/CALF/actions/workflows/python-tests.yml?query=branch%3Amain)

Calf is a structured, header-only C++17 logging library for the [CAPIO](https://github.com/High-Performance-IO/capio) ecosystem. It produces indented JSON output where each logged scope becomes a self-contained object with enter/exit timestamps and a nested array of events, making logs machine-readable without post-processing.

Calf is designed to work in two distinct environments:

- **STL safe processes**: uses `std::ofstream` for I/O (`StlLogger`)
- **CLI loggers (STL safe)**: logs to CLI (`StdOutLogger`)
- **NON STL safe processes**: uses raw syscalls whenever STL is not safe (`SyscallLogger`)
Both backends share the same JSON structure and produce identical output formats.

## CALF Inspector

CALF includes an interactive Inspector for exploring and analysing structured
trace logs. It provides a terminal interface and a responsive web interface
with call-tree navigation, lazy trace loading, search, timing details, and
aggregated statistics.

See the [CALF Inspector documentation](profiler/README.md) for installation,
usage, web-server options, and keyboard shortcuts.


## Requirements

- C++17 or later
- CMake 3.16 or later
- Linux for `SyscallLogger` backend
- Python 3.10 or later for the Python bindings and Inspector

## Testing

The GoogleTest C++ suites and Python binding tests run on Linux and macOS. The
raw syscall logger suite is built only on Linux because that backend is not
supported on macOS.

```bash
cmake -S . -B build -DCALF_TESTS=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

`CALF_TESTS=ON` enables `CALF_PYTHON_TESTS` by default and builds the Python
extension needed by those tests. To run only the Python binding test target:

```bash
cmake --build build --target calf_python_tests
```


## Output format

Each log scope opened by `START_LOG` becomes one JSON object in a root array. Inner `LOG` calls populate the `events` array. Scopes close with a `ts_exit` timestamp.

```json
[
  {
    "invoker": "capio_openat",
    "file": "posix/open.cpp",
    "line": 77,
    "ts_enter": 42,
    "args": "dirfd=4, pathname=/tmp/foo, flags=0",
    "events": [
      { "ts": 43, "invoker": "get_file_location", "file": "location.hpp", "line": 96, "args": "path=/tmp/foo" },
      { "ts": 44, "invoker": "get_file_location", "file": "location.hpp", "line": 96, "args": "File found on node host0" }
    ],
    "ts_exit": 45
  }
]
```

Each thread writes to its own log file under the log directory, named after the thread ID.


## Integration

### CMake FetchContent

```cmake
include(FetchContent)

FetchContent_Declare(
        calf
        GIT_REPOSITORY https://github.com/High-Performance-IO/calf.git
        GIT_TAG <commit-sha>   # pin to a specific commit for reproducible builds
        GIT_SHALLOW TRUE
)

set(CALF_LOG ON CACHE BOOL "" FORCE)
set(BUILD_PYTHON_BINDINGS OFF CACHE BOOL "" FORCE)
set(CALF_TESTS OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(calf)
```

Then link each target to the appropriate backend:

```cmake
# Server binary
target_link_libraries(my_server PRIVATE calf::stl)

# POSIX syscall interceptor (.so)
target_link_libraries(my_interceptor PRIVATE calf::syscall)
```

## CMake options

| Flag | Default | Description |
|------|---------|-------------|
| `CALF_LOG` | `ON` | Enable logging macros. When disabled, logging macros are no-ops. |
| `CALF_TESTS` | `OFF` | Build and register the GoogleTest suites with CTest. |
| `CALF_PYTHON_TESTS` | Value of `CALF_TESTS` | Build the Python extension and register its binding tests. |
| `BUILD_PYTHON_BINDINGS` | `OFF` | Build and install the `_py_calf` Python extension. |
| `CALF_DEFAULT_COMPONENT_NAME` | `calf` | Component directory and CLI header name used by configured targets. |
| `CALF_DEFAULT_LOG_DIR_NAME` | `./calf_logs` | Default log root when `CALF_LOG_DIR` is unset. |

Pass flags during configuration with `-D<flag>=<value>`. Common examples:

```bash
# Build all C++ and Python tests.
cmake -S . -B build -DCALF_TESTS=ON

# Build only the C++ tests.
cmake -S . -B build -DCALF_TESTS=ON -DCALF_PYTHON_TESTS=OFF

# Build the Python bindings without tests.
cmake -S . -B build -DBUILD_PYTHON_BINDINGS=ON

# Override compiled-in logging defaults.
cmake -S . -B build \
  -DCALF_DEFAULT_COMPONENT_NAME=my_component \
  -DCALF_DEFAULT_LOG_DIR_NAME=/var/log/calf
```


## Usage

### Python bindings

The Python package includes bindings for the STL file logger and stdout logger,
as well as the CALF Inspector.

#### Installation

Build and install the package from the repository root:

```bash
python -m pip install .
```

Install a published release from PyPI with:

```bash
python -m pip install CAPIO_CALF
```

For development, install it in editable mode:

```bash
python -m pip install -e .
```

The build uses CMake and pybind11 automatically. To build the extension directly
with CMake instead:

```bash
cmake -S . -B build -DBUILD_PYTHON_BINDINGS=ON
cmake --build build
```

#### STL logger

`calf.Logger` is an alias for the STL-backed `calf.StlLogger`. Use it as a
context manager so the logging scope is closed deterministically:

```python
import calf

with calf.Logger("processing request") as logger:
    logger.log("reading input")
    logger.log("request complete")
    print(logger.log_file_name)
```

CALF automatically uses the current Python function name as the invoker. At
module scope it uses the script filename, while interactive sessions use
`python`. Pass `invoker=` to override the detected name. The other optional
constructor arguments are `file`, `line`, and `tid`. If `tid` is omitted, CALF
uses the current thread ID. `close()` can be used instead of a context manager,
and `get_log_file_name()` returns the STL log path.

`CALF_LOG_DIR` and `CALF_LOG_PREFIX` configure file output in the same way as
the C++ STL logger.

#### Stdout logger

The stdout logger supports scoped logging and direct messages:

```python
import calf

options = calf.StdoutLogger.get_options()
options.workflow_name = "example"
options.color = calf.CLI_LEVEL_INFO
options.print_header = True
options.use_color = True
calf.StdoutLogger.set_options(options)

with calf.StdoutLogger("starting") as logger:
    logger.log("working")

calf.StdoutLogger.print("finished")
```

Available colors are `CLI_LEVEL_RESET`, `CLI_LEVEL_STATUS`, `CLI_LEVEL_INFO`,
`CLI_LEVEL_WARNING`, and `CLI_LEVEL_ERROR`.

#### Inspector

Installing the package also installs the bundled CALF Inspector:

```bash
calf calf_logs
calf calf_logs --web
```

It can also be launched with `python -m calf`. See the
[CALF Inspector documentation](profiler/README.md) for all options.

### Server / non-interceptor code

Include `StlLogger.h`. Logging is enabled by default on every thread.

```cpp
#include <calf/StlLogger.h>

void handle_request(int tid, const char *path) {
    START_LOG(tid, "call(path=%s)", path);

    LOG("processing path=%s", path);

    // ... do work ...

    LOG("done");
} // ts_exit written here when log goes out of scope
```

### POSIX syscall interceptor

Include `SyscallLogger.h`. Logging starts disabled to avoid re-entrancy during library setup. Call `ENABLE_LOGGER()` once setup is complete.

If the interceptor uses `syscall_no_intercept` from the [syscall_intercept](https://github.com/pmem/syscall_intercept) library, redirect Calf's syscalls before enabling:

```cpp
#include <calf/SyscallLogger.h>
#include <libsyscall_intercept_hook_point.h>

// Called once during interceptor initialisation.
void setup() {
    SET_CALF_SYSCALL_HANDLER(syscall_no_intercept);
    ENABLE_LOGGER();
}

long hook(long syscall_number, ...) {
    START_LOG(capio_syscall(SYS_gettid), "call()");
    // ...
}
```

Use `DISABLE_LOGGER()` around internal operations that must not appear in the log (e.g. Calf's own file I/O):

```cpp
void internal_op() {
    DISABLE_LOGGER();   // RAII: re-enables on scope exit
    // ... internal syscalls not logged ...
}
```

### Macros

| Macro                         | Description                                                                             |
|-------------------------------|-----------------------------------------------------------------------------------------|
| `START_LOG(tid, fmt, ...)`    | Opens a log scope. Creates a `Logger log` RAII object on the stack.                     |
| `LOG(fmt, ...)`               | Appends an event to the current scope's `events` array.                                 |
| `ERR_EXIT(fmt, ...)`          | Logs then terminates (or throws if `continue_on_error` is true).                        |
| `ENABLE_LOGGER()`             | Activates logging on the calling thread (POSIX build only).                             |
| `DISABLE_LOGGER()`            | Suspends logging for the current scope via RAII.                                        |
| `DBG(tid, lambda)`            | Wraps a lambda in a debug-only log scope. Compiled out in release builds.               |
| `SET_CALF_SYSCALL_HANDLER` | Sets non intercepted syscall function handler. available only in SyscallLogger backend. |


## Environment variables

| Variable             | Description                                                   | Default          |
|----------------------|---------------------------------------------------------------|------------------|
| `CALF_LOG_DIR`    | Root directory for all log output.                            | `./calf_logs` |
| `CALF_LOG_PREFIX` | Filename prefix for per-thread log files.                     | Backend-specific |

Log files are written to `$CALF_LOG_DIR/<backend>/<hostname>/<prefix><tid>.log`.
