from __future__ import annotations

import argparse
import sys

from .loader import build_tree, discover_tabs, load_trace


def main() -> None:
    parser = argparse.ArgumentParser(
        prog="calf",
        description=(
            "Web inspector and profiler for CALF JSON and protobuf traces.\n\n"
            "Traces are read from a log directory laid out as:\n"
            "  <log_dir>/syscall/<hostname>/<tid>.log\n"
            "  <log_dir>/stl/<hostname>/<tid>.log\n"
            "  <log_dir>/syscall/<hostname>/<tid>.pb\n\n"
            "Each trace file becomes a tab in the web inspector."
        ),
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument(
        "log_dir",
        nargs="?",
        default="calf_logs",
        help="Root log directory (default: calf_logs)",
    )
    parser.add_argument(
        "--host", default="127.0.0.1",
        help="Web server bind address (default: 127.0.0.1)",
    )
    parser.add_argument(
        "--port", type=int, default=8765,
        help="Web server port (default: 8765)",
    )

    args = parser.parse_args()

    # Discover tabs (fast — no file parsing yet)
    try:
        tabs = discover_tabs(args.log_dir)
    except (FileNotFoundError, RuntimeError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        sys.exit(1)

    total_files = len(tabs)
    print(f"Found {len(tabs)} tab(s) across {total_files} file(s):")

    for tab in tabs:
        try:
            data = load_trace(tab.path)
            tab._roots = build_tree(data)
            print(f"  [LOADED: {tab.kind:8}]  {tab.hostname}  tid={tab.tid}")
        except Exception as exc:
            print(f"  [SKIP:   {tab.kind:8}]  {tab.hostname}  tid={tab.tid}: {exc}")
            tab._roots = []

    total_nodes = sum(t.total_nodes for t in tabs)
    print(f"Loaded {total_nodes:,} trace nodes.")
    from .web import run_web
    run_web(tabs, args.host, args.port)


if __name__ == "__main__":
    main()
