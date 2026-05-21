from __future__ import annotations

import argparse
import sys

from alive_progress import alive_bar

from .loader import discover_tabs


def main() -> None:
    parser = argparse.ArgumentParser(
        prog="calf",
        description=(
            "Interactive viewer and analyser for CALF structured JSON traces.\n\n"
            "Traces are read from a log directory laid out as:\n"
            "  <log_dir>/syscall/<hostname>/<tid>.log\n"
            "  <log_dir>/stl/<hostname>/<tid>.log\n\n"
            "Each (hostname, type) pair becomes one tab in the UI."
        ),
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument(
        "log_dir",
        nargs="?",
        default="calf_logs",
        help="Root log directory (default: calf_logs)",
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
    for t in tabs:
        print(f"  [{t.kind:8}]  {t.hostname}  tid={t.tid}")

    # Pre-load all tabs with a progress bar so the UI opens instantly
    print()
    with alive_bar(total_files, title="Loading traces", spinner="dots") as bar:
        for tab in tabs:
            from .loader import repair_and_load, build_tree
            try:
                data = repair_and_load(tab.path)
                tab._roots = build_tree(data)
            except Exception as exc:
                print(f"  warning: skipping {tab.path}: {exc}", file=sys.stderr)
                tab._roots = []
            bar()

    total_nodes = sum(t.total_nodes for t in tabs)
    print(f"Loaded {total_nodes:,} trace nodes.")
    print()

    # Launch the Textual app
    from .app import CALFApp
    app = CALFApp(tabs)
    app.run()


if __name__ == "__main__":
    main()