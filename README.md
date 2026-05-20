# Captura

Captura is a structured, header-only C++17 logging library for the [CAPIO](https://github.com/High-Performance-IO/capio) ecosystem. It produces indented JSON output where each logged scope becomes a self-contained object with enter/exit timestamps and a nested array of events, making logs machine-readable without post-processing.

Captura is designed to work in two distinct environments:

- **STL safe processes**: uses `std::ofstream` for I/O (`StlLogger`)
- **NON STL safe processes**: uses raw syscalls whenever STL is not safe (`SyscallLogger`)

Both backends share the same JSON structure and produce identical output formats.


## Requirements

- C++17 or later
- CMake 3.16 or later
- Linux for `SyscallLogger` backend


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
    captura
    GIT_REPOSITORY https://github.com/High-Performance-IO/captura.git
    GIT_TAG        <commit-sha>   # pin to a specific commit for reproducible builds
    GIT_SHALLOW    TRUE
)

set(CAPTURA_LOG           ON  CACHE BOOL "" FORCE)
set(CAPTURA_BUILD_STL     ON  CACHE BOOL "" FORCE)
set(CAPTURA_BUILD_SYSCALL ON  CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(captura)
```

Then link each target to the appropriate backend:

```cmake
# Server binary
target_link_libraries(my_server PRIVATE captura::stl)

# POSIX syscall interceptor (.so)
target_link_libraries(my_interceptor PRIVATE captura::syscall)
```

## CMake options

| Option                  | Default | Description                                      |
|-------------------------|---------|--------------------------------------------------|
| `CAPTURA_LOG`           | `ON`    | Enable logging. When OFF all macros are no-ops.  |
| `CAPTURA_BUILD_STL`     | `ON`    | Include the STL (`std::ofstream`) backend.       |
| `CAPTURA_BUILD_SYSCALL` | `ON`    | Include the raw-syscall backend.                 |


## Usage

### Server / non-interceptor code

Include `StlLogger.h`. Logging is enabled by default on every thread.

```cpp
#include <captura/StlLogger.h>

void handle_request(int tid, const char *path) {
    START_LOG(tid, "call(path=%s)", path);

    LOG("processing path=%s", path);

    // ... do work ...

    LOG("done");
} // ts_exit written here when log goes out of scope
```

### POSIX syscall interceptor

Include `SyscallLogger.h`. Logging starts disabled to avoid re-entrancy during library setup. Call `ENABLE_LOGGER()` once setup is complete.

If the interceptor uses `syscall_no_intercept` from the [syscall_intercept](https://github.com/pmem/syscall_intercept) library, redirect Captura's syscalls before enabling:

```cpp
#include <captura/SyscallLogger.h>
#include <libsyscall_intercept_hook_point.h>

// Called once during interceptor initialisation.
void setup() {
    SET_CAPTURA_SYSCALL_HANDLER(syscall_no_intercept);
    ENABLE_LOGGER();
}

long hook(long syscall_number, ...) {
    START_LOG(capio_syscall(SYS_gettid), "call()");
    // ...
}
```

Use `DISABLE_LOGGER()` around internal operations that must not appear in the log (e.g. Captura's own file I/O):

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
| `SET_CAPTURA_SYSCALL_HANDLER` | Sets non intercepted syscall function handler. available only in SyscallLogger backend. |


## Environment variables

| Variable             | Description                                                   | Default          |
|----------------------|---------------------------------------------------------------|------------------|
| `CAPTURA_LOG_DIR`    | Root directory for all log output.                            | `./captura_logs` |
| `CAPTURA_LOG_PREFIX` | Filename prefix for per-thread log files.                     | Backend-specific |

Log files are written to `$CAPTURA_LOG_DIR/<backend>/<hostname>/<prefix><tid>.log`.
