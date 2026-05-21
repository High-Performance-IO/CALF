from __future__ import annotations

import json
import os
from dataclasses import dataclass, field
from typing import Optional

def repair_and_load(path: str) -> list:
    """
    Load a CALF JSON file, repairing truncation caused by an abrupt
    process exit mid-write.

    Strategy: walk the raw text tracking bracket depth (skipping string
    contents and escape sequences), then append the exact closing tokens
    that are missing.  Falls back to stripping after the last complete
    root-level entry if the first attempt still fails to parse.
    """
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        raw = f.read().rstrip()

    if not raw:
        return []

    depth_stack: list[str] = []
    in_string   = False
    escape_next = False

    for ch in raw:
        if escape_next:
            escape_next = False
            continue
        if ch == "\\" and in_string:
            escape_next = True
            continue
        if ch == '"':
            in_string = not in_string
            continue
        if in_string:
            continue
        if ch == "{":
            depth_stack.append("}")
        elif ch == "[":
            depth_stack.append("]")
        elif ch in ("}", "]"):
            if depth_stack and depth_stack[-1] == ch:
                depth_stack.pop()

    # If truncation happened inside a string, close it first.
    if in_string:
        depth_stack.append('"')

    repaired = raw + "".join(reversed(depth_stack))

    try:
        data = json.loads(repaired)
    except json.JSONDecodeError as exc:
        pos = repaired.rfind("\n  }")
        if pos != -1:
            repaired = repaired[: pos + 4] + "\n]"
            try:
                data = json.loads(repaired)
            except json.JSONDecodeError:
                raise RuntimeError(f"Cannot parse {path}: {exc}") from exc
        else:
            raise RuntimeError(f"Cannot parse {path}: {exc}") from exc

    if isinstance(data, dict):
        data = [data]
    return data

@dataclass
class TraceNode:
    """
    One node in the trace tree.

    Scope nodes  — have ts_enter / ts_exit and a children list.
    Leaf  nodes  — have ts only; they are LOG() events with no children.
    """
    invoker:  str
    args:     str
    file:     str
    line:     int
    ts_enter: Optional[int]
    ts_exit:  Optional[int]
    ts:       Optional[int]
    children: list[TraceNode]
    is_leaf:  bool
    depth:    int = 0
    parent:   Optional[TraceNode] = field(default=None, repr=False)

    @property
    def duration_ms(self) -> Optional[int]:
        if self.ts_enter is not None and self.ts_exit is not None:
            return self.ts_exit - self.ts_enter
        return None

    @property
    def timestamp(self) -> Optional[int]:
        return self.ts if self.is_leaf else self.ts_enter

    @property
    def short_file(self) -> str:
        """Last two path components, e.g. server/client_manager.cpp"""
        parts = self.file.replace("\\", "/").split("/")
        return "/".join(parts[-2:]) if len(parts) >= 2 else self.file


def build_tree(entries: list, depth: int = 0,
               parent: Optional[TraceNode] = None) -> list[TraceNode]:
    nodes: list[TraceNode] = []
    for entry in entries:
        is_scope      = "ts_enter" in entry
        children_raw  = entry.get("events", [])
        node = TraceNode(
            invoker  = entry.get("invoker", "?"),
            args     = entry.get("args", ""),
            file     = entry.get("file", ""),
            line     = entry.get("line", 0),
            ts_enter = entry.get("ts_enter"),
            ts_exit  = entry.get("ts_exit"),
            ts       = entry.get("ts"),
            children = [],
            is_leaf  = not is_scope,
            depth    = depth,
            parent   = parent,
        )
        node.children = build_tree(children_raw, depth + 1, node)
        nodes.append(node)
    return nodes


def flatten(nodes: list[TraceNode]) -> list[TraceNode]:
    """Return all nodes in the tree (depth-first), regardless of expansion."""
    result: list[TraceNode] = []
    for node in nodes:
        result.append(node)
        result.extend(flatten(node.children))
    return result

def compute_stats(roots: list[TraceNode]) -> list[dict]:
    """
    Walk the full tree and compute per-invoker timing statistics.
    Returns a list of dicts sorted by total_ms descending.
    """
    from collections import defaultdict
    import numpy as np

    buckets: dict[str, list[int]] = defaultdict(list)

    def walk(nodes: list[TraceNode]) -> None:
        for n in nodes:
            if not n.is_leaf and n.duration_ms is not None:
                buckets[n.invoker].append(n.duration_ms)
            walk(n.children)

    walk(roots)

    rows = []
    for invoker, times in buckets.items():
        arr = np.array(times, dtype=float)
        rows.append({
            "invoker":   invoker,
            "count":     len(times),
            "total_ms":  int(arr.sum()),
            "mean_ms":   float(arr.mean()),
            "median_ms": float(np.median(arr)),
            "max_ms":    int(arr.max()),
            "min_ms":    int(arr.min()),
            "std_ms":    float(arr.std()),
        })

    rows.sort(key=lambda r: r["total_ms"], reverse=True)
    return rows

@dataclass
class TraceTab:
    """
    One tab = one log file.

    Tabs are labeled "<kind> / <hostname> / <tid>" so each thread's trace
    is presented independently.
    """
    hostname: str
    kind:     str        # "syscall", "stl", or ...
    path:     str        # path to the single .log file this tab represents
    _roots:   Optional[list[TraceNode]] = field(default=None, repr=False)

    @property
    def tid(self) -> str:
        """Thread-id portion of the filename, e.g. '44623' from 'stl_44623.log'."""
        stem = os.path.splitext(os.path.basename(self.path))[0]
        # Strip the kind prefix if present (stl_44623 -> 44623)
        if "_" in stem:
            return stem.rsplit("_", 1)[-1]
        return stem

    @property
    def label(self) -> str:
        return f"{self.kind} / {self.hostname} / {self.tid}"

    @property
    def roots(self) -> list[TraceNode]:
        if self._roots is None:
            try:
                data = repair_and_load(self.path)
                self._roots = build_tree(data)
            except Exception:
                self._roots = []
        return self._roots

    @property
    def total_nodes(self) -> int:
        return len(flatten(self.roots))


def discover_tabs(log_dir: str) -> list[TraceTab]:
    """
    Walk log_dir and create one TraceTab per .log file.

    Expected CALF layout:
        <log_dir>/syscall/<hostname>/<tid>.log
        <log_dir>/stl/<hostname>/<tid>.log
    """
    if not os.path.isdir(log_dir):
        raise FileNotFoundError(f"Log directory not found: {log_dir!r}")

    tabs: list[TraceTab] = []

    for root, dirs, files in os.walk(log_dir):
        dirs.sort()
        for fname in sorted(files):
            if not fname.endswith(".log"):
                continue
            full  = os.path.join(root, fname)
            rel   = os.path.relpath(full, log_dir).replace("\\", "/")
            parts = rel.split("/")

            if len(parts) == 3:
                kind, hostname = parts[0], parts[1]
            else:
                hostname = os.path.basename(root) or "unknown"
                kind     = "unknown"

            tabs.append(TraceTab(hostname=hostname, kind=kind, path=full))

    if not tabs:
        raise RuntimeError(f"No .log files found under {log_dir!r}")

    order = {"syscall": 0, "stl": 1, "unknown": 2}
    tabs.sort(key=lambda t: (order.get(t.kind, 9), t.hostname, t.tid))
    return tabs