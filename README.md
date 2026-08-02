<p align="center">
  <img src="./calf.svg?version=2" alt="CALF - CAPIO Logging Facility" width="720">
</p>

# CALF - CApio Logging Facility

![C++ 17](https://img.shields.io/badge/C%2B%2B-17-blue.svg)
![Python 3.10-3.14](https://img.shields.io/badge/Python-3.10--3.14-blue.svg)

[![C++ tests](https://github.com/High-Performance-IO/CALF/actions/workflows/cpp-tests.yml/badge.svg?branch=main)](https://github.com/High-Performance-IO/CALF/actions/workflows/cpp-tests.yml?query=branch%3Amain)
[![Python tests](https://github.com/High-Performance-IO/CALF/actions/workflows/python-tests.yml/badge.svg?branch=main)](https://github.com/High-Performance-IO/CALF/actions/workflows/python-tests.yml?query=branch%3Amain)

CALF is a structured, header-only C++17 logging library for the
[CAPIO](https://github.com/High-Performance-IO/capio) ecosystem. It records
RAII scopes and events in one of two file formats:

- **Perfetto** is the preferred format. It produces compact, streaming
  `.perfetto-trace` files that open directly in the
  [Perfetto UI](https://ui.perfetto.dev/).
- **JSON** is the dependency-free fallback. It produces readable `.log` files
  containing valid, indented JSON.

CALF provides three logging backends:

- `StlLogger` uses `std::fstream` and is intended for regular C++ processes.
- `SyscallLogger` uses raw Linux syscalls and is intended for syscall
  interceptors or other environments where the STL is unsafe.
- `StdOutLogger` writes human-readable messages to standard output.

`StlLogger` and `SyscallLogger` support both Perfetto and JSON with the same
logging macros. The selected file format is fixed when a target is built.

## Requirements

- C++17 or later
- CMake 3.16 or later
- Linux when using `SyscallLogger`
- Python 3.10 or later when using the Python bindings

## Quick start

Add CALF with `FetchContent`:

```cmake
include(FetchContent)

FetchContent_Declare(
  calf
  GIT_REPOSITORY https://github.com/High-Performance-IO/calf.git
  GIT_TAG <commit-sha> # Pin a commit for reproducible builds.
  GIT_SHALLOW TRUE
)

set(CALF_TESTS OFF CACHE BOOL "" FORCE)
set(CALF_BUILD_PYTHON_BINDINGS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(calf)

# Enables logging and selects Perfetto when CALF_PROTOBUF=ON.
calf_enable_log(my_server ON)
```

Use the logging macros in C++:

```cpp
#include <calf/StlLogger.h>

void handle_request(int tid, const char *path) {
    START_LOG(tid, "path=%s", path);
    LOG("processing");
} // The scope closes and its end timestamp is recorded here.
```

By default, this writes a Perfetto trace to:

```text
./calf_logs/<hostname>/calf_<pid>.perfetto-trace
```

Open the file in <https://ui.perfetto.dev/>.

## Log formats

### Perfetto format

Perfetto is selected when a target links to `calf::protobuf`. The
`calf_enable_log` helper does this automatically when `CALF_PROTOBUF=ON`:

```cmake
# Automatic: Perfetto if available, otherwise JSON.
calf_enable_log(my_server ON)

# Explicit: require Perfetto. Configuration fails if CALF_PROTOBUF=OFF.
calf_enable_log(my_server ON PROTOBUF)
```

You can also select it by linking the target directly:

```cmake
target_link_libraries(my_server PRIVATE calf::protobuf)
```

CALF writes a standard Perfetto TrackEvent protobuf stream. Its logging model
maps to Perfetto as follows:

| CALF operation | Perfetto representation |
|---|---|
| `START_LOG(...)` | Slice begin on the calling thread's track |
| Scope destruction | Slice end on the same track |
| `LOG(...)` | Instant event on the same track |
| C++ function name | Event name |
| Formatted message | `args` debug annotation |
| Source file and line | Perfetto source location |
| Nested CALF scopes | Nested Perfetto slices |

All threads in a process write to one trace:

```text
$CALF_LOG_DIR/<hostname>/<component>_<pid>.perfetto-trace
```

Each thread has its own Perfetto track. Timestamps use the monotonic clock in
nanoseconds. Writes are synchronized so packets from different threads cannot
interleave. Separate processes always use separate files.

The trace is streamed as logging occurs; CALF does not build the complete trace
in memory. `StlLogger` uses generated protobuf support for the schema, while
`SyscallLogger` writes compatible protobuf bytes through raw syscalls.

CMake uses any installed Google Protobuf package that provides `protoc` and
generates the schema with it. Otherwise, it downloads Protobuf through
`FetchContent`.

### JSON fallback

JSON is selected automatically by `calf_enable_log(target ON)` when
`CALF_PROTOBUF=OFF`. It can also be requested explicitly:

```cmake
# Disable protobuf support for the whole CALF build.
set(CALF_PROTOBUF OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(calf)
calf_enable_log(my_server ON)

# Or keep protobuf available but use JSON for this target.
calf_enable_log(my_tool ON JSON)
```

Directly linking `calf::stl` or `calf::syscall` also uses JSON unless the target
additionally receives the Perfetto configuration through `calf::protobuf`:

```cmake
target_link_libraries(my_server PRIVATE calf::stl)
target_link_libraries(my_interceptor PRIVATE calf::syscall)
```

This is a build-time fallback. CALF does not switch formats at runtime or
convert an existing Perfetto trace to JSON.

JSON output uses one file per thread:

```text
$CALF_LOG_DIR/<component>/<hostname>/<prefix><tid>.log
```

The default prefix is `stl_` for `StlLogger` and `syscall_` for
`SyscallLogger`. `CALF_LOG_PREFIX` overrides it.

Each file contains one root array. Every `START_LOG` scope is an object, and
each `LOG` call is an event in that scope's `events` array:

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
      { "ts": 44, "invoker": "get_file_location", "file": "location.hpp", "line": 96, "args": "file found on node host0" }
    ],
    "ts_exit": 45
  }
]
```

Scope fields:

| Field | Meaning |
|---|---|
| `invoker` | Function that opened the scope |
| `file`, `line` | Source location of `START_LOG` |
| `ts_enter` | Scope start time in elapsed milliseconds |
| `args` | Expanded `START_LOG` format string |
| `events` | Ordered `LOG` events and any nested scopes |
| `ts_exit` | Scope end time in elapsed milliseconds |

Event fields:

| Field | Meaning |
|---|---|
| `ts` | Event time in elapsed milliseconds |
| `invoker` | Function associated with the enclosing logger |
| `file`, `line` | Source location associated with the logger |
| `args` | Expanded `LOG` format string |

Strings are JSON-escaped, and the file remains valid JSON after each completed
outer scope. Multiple outer scopes are appended to the same root array.

## C++ usage

### Regular processes

Use `StlLogger` in code where the C++ standard library is safe. Logging starts
enabled on every thread:

```cpp
#include <calf/StlLogger.h>

void handle_request(int tid, const char *path) {
    START_LOG(tid, "call(path=%s)", path);
    LOG("processing path=%s", path);
    LOG("done");
}
```

### Syscall interceptors

Use `SyscallLogger` where logging must avoid intercepted STL I/O. This backend
is Linux-only and starts disabled to prevent re-entrancy during setup. Enable it
after initialization:

```cpp
#include <calf/SyscallLogger.h>
#include <libsyscall_intercept_hook_point.h>

void setup() {
    SET_CALF_SYSCALL_HANDLER(syscall_no_intercept);
    ENABLE_LOGGER();
}

long hook(long syscall_number, ...) {
    START_LOG(capio_syscall(SYS_gettid), "call()");
    // Handle the syscall.
}
```

`SET_CALF_SYSCALL_HANDLER` redirects CALF's internal syscalls through
`syscall_no_intercept`. Temporarily suspend logging around operations that must
not be captured:

```cpp
void internal_operation() {
    DISABLE_LOGGER(); // Logging is restored when this scope exits.
    // Perform internal syscalls.
}
```

### Macros

| Macro | Description |
|---|---|
| `START_LOG(tid, fmt, ...)` | Creates a scoped `Logger log` RAII object. |
| `LOG(fmt, ...)` | Adds an event to the current scope. |
| `ERR_EXIT(fmt, ...)` | Logs and terminates, or throws when configured to continue on error. |
| `ENABLE_LOGGER()` | Enables logging on the calling thread. |
| `DISABLE_LOGGER()` | Suspends logging until the current C++ scope exits. |
| `DBG(tid, lambda)` | Wraps a lambda in a debug-only scope; compiled out in release builds. |
| `SET_CALF_SYSCALL_HANDLER(handler)` | Sets the non-intercepted syscall handler for `SyscallLogger`. |

## Configuration

### CMake options

| Option | Default | Description |
|---|---:|---|
| `CALF_LOG` | `ON` | Enables logging macros globally; when disabled, they are no-ops. |
| `CALF_PROTOBUF` | `ON` | Builds Perfetto support and the `calf::protobuf` target. |
| `CALF_PROTOBUF_FETCH` | `ON` | Downloads Protobuf when an installation with `protoc` is unavailable. |
| `CALF_PROTOBUF_FORCE_FETCH` | `OFF` | Downloads and uses Protobuf even when it is installed. |
| `CALF_PROTOBUF_REGENERATE` | `OFF` | Adds the `calf_regenerate_protobuf` source regeneration target. |
| `CALF_TESTS` | `OFF` | Builds and registers the C++ test suites. |
| `CALF_PYTHON_TESTS` | Value of `CALF_TESTS` | Builds the Python extension and registers binding tests. |
| `CALF_BUILD_PYTHON_BINDINGS` | `OFF` | Builds and installs the private `calf._py_calf` extension. |
| `CALF_DEFAULT_COMPONENT_NAME` | `calf` | Sets the default component used in paths and CLI headers. |
| `CALF_DEFAULT_LOG_DIR_NAME` | `./calf_logs` | Sets the log root used when `CALF_LOG_DIR` is unset. |

Pass options during configuration with `-D<option>=<value>`:

```bash
# Build C++ and Python tests.
cmake -S . -B build -DCALF_TESTS=ON

# Build only C++ tests.
cmake -S . -B build -DCALF_TESTS=ON -DCALF_PYTHON_TESTS=OFF

# Build without protobuf and use JSON as the automatic format.
cmake -S . -B build -DCALF_PROTOBUF=OFF

# Override compiled-in output defaults.
cmake -S . -B build \
  -DCALF_DEFAULT_COMPONENT_NAME=my_component \
  -DCALF_DEFAULT_LOG_DIR_NAME=/var/log/calf
```

For individual targets, `calf_set_component(target name)` and
`calf_set_default_log_dir(target path)` override the compiled-in defaults.

### Environment variables

| Variable | Description | Default |
|---|---|---|
| `CALF_LOG_DIR` | Root directory for Perfetto and JSON output. | `./calf_logs` |
| `CALF_LOG_PREFIX` | Prefix for per-thread JSON filenames; ignored by Perfetto. | `stl_` or `syscall_` |

Environment variables are read by the process at runtime and override the
compiled-in output directory and JSON prefix.

## Python bindings

The Python package exposes the JSON-backed `StlLogger` and the human-readable
`StdoutLogger`.

Install a published release:

```bash
python -m pip install capio-calf
```

Install from the repository for development:

```bash
python -m pip install -e .
```

Use `calf.Logger`, an alias for `calf.StlLogger`, as a context manager so its
scope closes deterministically:

```python
import calf

with calf.Logger("processing request") as logger:
    logger.log("reading input")
    logger.log("request complete")
    print(logger.log_file_name)
```

CALF detects the current Python function as the invoker. At module scope it
uses the script name, and in an interactive session it uses `python`. The
constructor accepts `invoker`, `file`, `line`, and `tid` overrides. If `tid` is
omitted, CALF uses the current thread ID. `close()` can be used without a
context manager, and `get_log_file_name()` returns the output path.

Configure stdout logging through `StdoutLoggerOptions`:

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

## Testing

The C++ tests use GoogleTest. Python binding tests use pytest. Both run on Linux
and macOS, except the Linux-only raw syscall suites.

```bash
cmake -S . -B build -DCALF_TESTS=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Run only the Python binding test target with:

```bash
cmake --build build --target calf_python_tests
```
