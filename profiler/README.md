# CALF

An interactive viewer and analyser for 
[CALF](https://github.com/High-Performance-IO/CALF) structured JSON traces.

## Install

```
pip install git+https://github.com/High-Performance-IO/calf.git
```

Or from a local clone:

```
pip install .
```

## Usage

```
calf [log_dir]
```

`log_dir` defaults to `calf_logs`. The expected layout is the one produced by CALF:

```
calf_logs/
    syscall/<hostname>/<tid>.log
    stl/<hostname>/<tid>.log
```

Each `(hostname, type)` pair becomes one tab in the UI.

## Keyboard shortcuts

| Key                 | Action                                 |
|---------------------|----------------------------------------|
| `Tab` / `Shift-Tab` | Switch between tabs                    |
| `Arrow keys`        | Navigate the tree                      |
| `Enter` / `Space`   | Expand / collapse node                 |
| `e`                 | Expand all nodes                       |
| `c`                 | Collapse all nodes                     |
| `f`                 | Filter tree by invoker or args (regex) |
| `s`                 | Open statistics table for current tab  |
| `q`                 | Quit                                   |

## Output format

CALF traces are structured JSON arrays where each entry is either a **scope** (has `ts_enter`, `ts_exit`, `events`)
or a **leaf event** (has `ts` only). Truncated files caused by process crashes are repaired automatically on load.