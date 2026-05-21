from __future__ import annotations

import re
from typing import Optional

from textual.app import App, ComposeResult
from textual.binding import Binding
from textual.containers import Container, Horizontal, Vertical
from textual.screen import ModalScreen
from textual.widgets import (
    Button,
    DataTable,
    Footer,
    Header,
    Input,
    Label,
    Static,
    TabbedContent,
    TabPane,
    Tree,
)
from textual.widgets.tree import TreeNode

from .loader import TraceNode, TraceTab, compute_stats, flatten


_GLYPH_PAD = "  "

def _add_nodes(tree_node: TreeNode, trace_nodes: list[TraceNode],
               filter_pat: Optional[re.Pattern] = None) -> int:
    added = 0
    for tn in trace_nodes:
        if filter_pat:
            if not any(
                    filter_pat.search(n.invoker) or filter_pat.search(n.args)
                    for n in flatten([tn])
            ):
                continue

        if tn.is_leaf:
            ts_str = f"[{tn.ts}ms]" if tn.ts is not None else ""
            tree_node.add_leaf(
                f"{_GLYPH_PAD}{tn.invoker}  {ts_str}  {tn.args}", data=tn)
            added += 1
        else:
            dur   = f"  [{tn.duration_ms}ms]" if tn.duration_ms is not None else ""
            label = f"{tn.invoker}{dur}  {tn.args}"
            if tn.children:
                child = tree_node.add(label, data=tn, expand=(tn.depth < 2))
                added += _add_nodes(child, tn.children, filter_pat)
                if not list(child.children) and filter_pat:
                    child.remove()
                    tree_node.add_leaf(f"{_GLYPH_PAD}{label}", data=tn)
            else:
                # Scope node with no events — leaf, no triangle.
                tree_node.add_leaf(f"{_GLYPH_PAD}{label}", data=tn)
            added += 1
    return added

class FilterModal(ModalScreen):
    BINDINGS = [Binding("escape", "dismiss('')", "Cancel")]

    def __init__(self, prompt: str = "Filter invoker / args:",
                 current: str = "") -> None:
        super().__init__()
        self._prompt  = prompt
        self._current = current

    def compose(self) -> ComposeResult:
        with Container(classes="modal-box"):
            yield Label(self._prompt)
            yield Input(value=self._current, id="filter-input",
                        placeholder="regex pattern  (empty = clear)")
            with Horizontal(classes="modal-buttons"):
                yield Button("Apply",  variant="primary", id="apply")
                yield Button("Clear",  variant="default", id="clear")
                yield Button("Cancel", variant="error",   id="cancel")

    def on_mount(self) -> None:
        self.query_one("#filter-input", Input).focus()

    def on_button_pressed(self, event: Button.Pressed) -> None:
        if event.button.id == "apply":
            self.dismiss(self.query_one("#filter-input", Input).value)
        elif event.button.id == "clear":
            self.dismiss("")
        else:
            self.dismiss(None)

    def on_input_submitted(self, event: Input.Submitted) -> None:
        self.dismiss(event.value)


class StatsScreen(ModalScreen):
    BINDINGS = [Binding("escape,q", "dismiss", "Close")]

    def __init__(self, tab: TraceTab) -> None:
        super().__init__()
        self._tab = tab

    def compose(self) -> ComposeResult:
        yield Header(show_clock=False)
        yield DataTable(id="stats-dt", classes="stats-table")
        yield Footer()

    def on_mount(self) -> None:
        self.title     = f"Statistics — {self._tab.label}"
        self.sub_title = f"tid {self._tab.tid}"
        dt = self.query_one("#stats-dt", DataTable)
        dt.add_columns(
            "Invoker", "Count", "Total ms",
            "Mean ms", "Median ms", "Max ms", "Min ms", "Std ms",
        )
        rows = compute_stats(self._tab.roots)
        for r in rows:
            dt.add_row(
                r["invoker"],
                str(r["count"]),
                str(r["total_ms"]),
                f"{r['mean_ms']:.1f}",
                f"{r['median_ms']:.1f}",
                str(r["max_ms"]),
                str(r["min_ms"]),
                f"{r['std_ms']:.1f}",
            )
        dt.focus()
        self.sub_title += f"  |  {len(rows)} unique invokers"

class FilePane(TabPane):
    """Innermost pane: trace tree for one log file (one thread)."""

    def __init__(self, trace_tab: TraceTab) -> None:
        super().__init__(trace_tab.tid, id=f"fp-{id(trace_tab):x}")
        self._trace_tab = trace_tab
        self._filter_pat: Optional[re.Pattern] = None

    def compose(self) -> ComposeResult:
        with Vertical(classes="pane-container"):
            yield Tree("Traces", id="trace-tree", classes="tree-pane")
            yield Static("", id="detail-bar", classes="detail-bar")

    def on_mount(self) -> None:
        self._rebuild_tree()

    def _rebuild_tree(self) -> None:
        tree = self.query_one("#trace-tree", Tree)
        tree.clear()
        tree.root.expand()
        count  = _add_nodes(tree.root, self._trace_tab.roots, self._filter_pat)
        suffix = (f", filter={self._filter_pat.pattern!r}"
                  if self._filter_pat else "")
        tree.root.set_label(f"Traces  ({count} nodes{suffix})")

    def on_tree_node_highlighted(self, event: Tree.NodeHighlighted) -> None:
        tn: Optional[TraceNode] = event.node.data
        bar = self.query_one("#detail-bar", Static)
        if tn is None:
            bar.update("")
            return
        parts = [f"{tn.short_file}:{tn.line}"]
        if not tn.is_leaf and tn.duration_ms is not None:
            parts.append(f"duration={tn.duration_ms}ms")
        if tn.ts_enter is not None:
            parts.append(f"ts_enter={tn.ts_enter}ms")
        if tn.ts_exit is not None:
            parts.append(f"ts_exit={tn.ts_exit}ms")
        bar.update("  |  ".join(parts))

    def apply_filter(self, pattern: str) -> None:
        if pattern:
            try:
                self._filter_pat = re.compile(pattern, re.IGNORECASE)
            except re.error:
                self._filter_pat = re.compile(re.escape(pattern), re.IGNORECASE)
        else:
            self._filter_pat = None
        self._rebuild_tree()

    def expand_all(self) -> None:
        for node in self.query_one("#trace-tree", Tree).root.children:
            node.expand_all()

    def collapse_all(self) -> None:
        for node in self.query_one("#trace-tree", Tree).root.children:
            node.collapse_all()

class KindPane(TabPane):
    """Middle pane: one FilePane per log file."""

    def __init__(self, kind: str,
                 file_tabs: list[TraceTab],
                 pane_id: str) -> None:
        super().__init__(kind, id=pane_id)
        self._file_tabs = file_tabs

    def compose(self) -> ComposeResult:
        with TabbedContent(classes="file-tabs"):
            for tab in self._file_tabs:
                yield FilePane(tab)

class HostPane(TabPane):
    """Outermost pane: one KindPane per backend."""

    def __init__(self, hostname: str,
                 kind_map: dict[str, list[TraceTab]],
                 pane_id: str) -> None:
        super().__init__(hostname, id=pane_id)
        self._kind_map = kind_map

    def compose(self) -> ComposeResult:
        order = {"syscall": 0, "stl": 1, "unknown": 2}
        kinds = sorted(self._kind_map, key=lambda k: (order.get(k, 9), k))
        with TabbedContent(classes="kind-tabs"):
            for i, kind in enumerate(kinds):
                yield KindPane(kind, self._kind_map[kind],
                               pane_id=f"kp-{id(self)}-{i}")

class CALFApp(App):
    """CALF — CAPIO Logging Facility trace viewer."""

    TITLE = "CALF — CAPIO Logging Facility"

    # CSS is loaded from style.css next to this file so it can be edited
    # without reinstalling the package.
    CSS_PATH = "style.css"

    BINDINGS = [
        Binding("f", "filter",       "Filter",       show=True),
        Binding("s", "stats",        "Statistics",   show=True),
        Binding("e", "expand_all",   "Expand all",   show=True),
        Binding("c", "collapse_all", "Collapse all", show=True),
        Binding("q", "quit",         "Quit",         show=True),
    ]

    def __init__(self, tabs: list[TraceTab]) -> None:
        super().__init__()
        self._tabs = tabs
        self._tree: dict[str, dict[str, list[TraceTab]]] = {}
        for tab in tabs:
            self._tree.setdefault(tab.hostname, {}).setdefault(
                tab.kind, []
            ).append(tab)

    def compose(self) -> ComposeResult:
        yield Header(show_clock=True)
        hostnames = sorted(self._tree)
        with TabbedContent(classes="host-tabs"):
            for i, hostname in enumerate(hostnames):
                yield HostPane(hostname, self._tree[hostname], pane_id=f"hp-{i}")
        yield Footer()

    def on_mount(self) -> None:
        n_hosts = len(self._tree)
        n_kinds = sum(len(v) for v in self._tree.values())
        self.sub_title = (
            f"{n_hosts} host(s)  |  "
            f"{n_kinds} backend(s)  |  "
            f"{len(self._tabs)} file(s)"
        )

    def _active_file_pane(self) -> Optional[FilePane]:
        try:
            host_tc   = self.query_one(".host-tabs", TabbedContent)
            host_pane = host_tc.active_pane
            if not isinstance(host_pane, HostPane):
                return None
            kind_tc   = host_pane.query_one(".kind-tabs", TabbedContent)
            kind_pane = kind_tc.active_pane
            if not isinstance(kind_pane, KindPane):
                return None
            file_tc   = kind_pane.query_one(".file-tabs", TabbedContent)
            file_pane = file_tc.active_pane
            return file_pane if isinstance(file_pane, FilePane) else None
        except Exception:
            return None

    def action_filter(self) -> None:
        pane = self._active_file_pane()
        if pane is None:
            return
        current = pane._filter_pat.pattern if pane._filter_pat else ""

        def _apply(result: Optional[str]) -> None:
            if result is not None and pane:
                pane.apply_filter(result)

        self.push_screen(FilterModal(current=current), _apply)

    def action_stats(self) -> None:
        pane = self._active_file_pane()
        if pane is None:
            return
        self.push_screen(StatsScreen(pane._trace_tab))

    def action_expand_all(self) -> None:
        pane = self._active_file_pane()
        if pane:
            pane.expand_all()

    def action_collapse_all(self) -> None:
        pane = self._active_file_pane()
        if pane:
            pane.collapse_all()