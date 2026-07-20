from __future__ import annotations

import re
from typing import Optional

from rich.text import Text
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
_DEPTH_STYLES = (
    "bright_magenta",
    "bright_blue",
    "bright_cyan",
    "bright_green",
    "bright_yellow",
)


def _format_ms(value: Optional[int]) -> str:
    if value is None:
        return "--"
    if value >= 1_000:
        return f"{value / 1_000:.2f}s"
    return f"{value}ms"


def _duration_style(value: Optional[int]) -> str:
    if value is None:
        return "dim"
    if value < 10:
        return "bold bright_green"
    if value < 100:
        return "bold green_yellow"
    if value < 1_000:
        return "bold orange3"
    return "bold bright_red"


def _node_label(node: TraceNode, indented: bool = False) -> Text:
    label = Text(_GLYPH_PAD if indented else "")
    if node.is_leaf:
        label.append(node.invoker, style="bold bright_cyan")
        if node.ts is not None:
            label.append(f"  @{_format_ms(node.ts)}", style="cyan")
    else:
        depth_style = _DEPTH_STYLES[node.depth % len(_DEPTH_STYLES)]
        label.append(node.invoker, style=f"bold {depth_style}")
        if node.duration_ms is not None:
            label.append(
                f"  {_format_ms(node.duration_ms)}",
                style=_duration_style(node.duration_ms),
            )
    if node.args:
        label.append("  |  ", style="grey50")
        label.append(node.args, style="italic grey70")
    return label

def _add_nodes(tree_node: TreeNode, trace_nodes: list[TraceNode],
               filter_pat: Optional[re.Pattern] = None,
               node_map: Optional[dict[int, TreeNode]] = None) -> int:
    if node_map is None:
        node_map = {}
    added = 0
    for tn in trace_nodes:
        if filter_pat:
            if not any(
                    filter_pat.search(n.invoker) or filter_pat.search(n.args)
                    for n in flatten([tn])
            ):
                continue

        if tn.is_leaf:
            node = tree_node.add_leaf(_node_label(tn, indented=True), data=tn)
            node_map[id(tn)] = node
            added += 1
        else:
            label = _node_label(tn)
            if tn.children:
                child = tree_node.add(label, data=tn, expand=(tn.depth < 2))
                node_map[id(tn)] = child
                added += _add_nodes(child, tn.children, filter_pat, node_map)
                if not list(child.children) and filter_pat:
                    child.remove()
                    child = tree_node.add_leaf(
                        _node_label(tn, indented=True), data=tn
                    )
                    node_map[id(tn)] = child
            else:
                # Scope node with no events — leaf, no triangle.
                node = tree_node.add_leaf(
                    _node_label(tn, indented=True), data=tn
                )
                node_map[id(tn)] = node
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
        with Vertical(id="stats-shell"):
            yield Static(id="stats-title")
            yield Static(id="stats-summary")
            yield DataTable(id="stats-dt", classes="stats-table")
            yield Static("Esc or q  Close", classes="modal-hint")

    def on_mount(self) -> None:
        self.query_one("#stats-title", Static).update(
            Text(f"Timing statistics  /  {self._tab.label}", style="bold")
        )
        dt = self.query_one("#stats-dt", DataTable)
        dt.cursor_type = "row"
        dt.zebra_stripes = True
        dt.add_columns(
            "Invoker", "Count", "Total ms",
            "Mean ms", "Median ms", "Max ms", "Min ms", "Std ms",
        )
        rows = compute_stats(self._tab.roots)
        total_calls = sum(r["count"] for r in rows)
        total_time = sum(r["total_ms"] for r in rows)
        self.query_one("#stats-summary", Static).update(
            f"{len(rows):,} invokers   {total_calls:,} timed calls   "
            f"{_format_ms(total_time)} cumulative"
        )
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

class FilePane(TabPane):
    """Innermost pane: trace tree for one log file (one thread)."""

    def __init__(self, trace_tab: TraceTab) -> None:
        super().__init__(trace_tab.tid, id=f"fp-{id(trace_tab):x}")
        self._trace_tab = trace_tab
        self._filter_pat: Optional[re.Pattern] = None
        self._trace_nodes = flatten(trace_tab.roots)
        self._node_map: dict[int, TreeNode] = {}
        self._search_pattern = ""
        self._search_matches: list[TreeNode] = []
        self._search_index = -1
        self._visible_count = 0

    def compose(self) -> ComposeResult:
        with Vertical(classes="pane-container"):
            yield Static("", id="trace-summary", classes="trace-summary")
            yield Static(id="tree-legend", classes="tree-legend")
            yield Tree("Traces", id="trace-tree", classes="tree-pane")
            with Vertical(classes="detail-panel"):
                yield Static("Select a trace to inspect it", id="detail-title")
                yield Static("", id="detail-meta")
                yield Static("", id="detail-args")

    def on_mount(self) -> None:
        legend = Text("COLOR", style="bold #4ecdc4")
        legend.append("   events", style="bold cyan")
        legend.append("   FAST <10ms", style="bold bright_green")
        legend.append("   WARM <1s", style="bold orange3")
        legend.append("   HOT >=1s", style="bold bright_red")
        legend.append("   SCOPE color = nesting depth", style="dim")
        self.query_one("#tree-legend", Static).update(legend)
        self._rebuild_tree()
        self.query_one("#trace-tree", Tree).focus()

    def _update_summary(self, visible_count: int) -> None:
        scopes = sum(not node.is_leaf for node in self._trace_nodes)
        events = len(self._trace_nodes) - scopes
        timestamps = [
            value
            for node in self._trace_nodes
            for value in (node.ts, node.ts_enter, node.ts_exit)
            if value is not None
        ]
        elapsed = max(timestamps) - min(timestamps) if len(timestamps) > 1 else None
        summary = Text()
        summary.append(f" THREAD {self._trace_tab.tid} ", style="bold black on cyan")
        summary.append(f"  {scopes:,} scopes", style="magenta")
        summary.append(f"   {events:,} events", style="cyan")
        summary.append(f"   {_format_ms(elapsed)} window", style="yellow")
        if self._filter_pat:
            summary.append(
                f"   FILTER  {self._filter_pat.pattern}  ({visible_count:,} shown)",
                style="bold black on yellow",
            )
        if self._search_matches:
            summary.append(
                f"   SEARCH  {self._search_pattern}  "
                f"({self._search_index + 1}/{len(self._search_matches)})",
                style="bold black on green",
            )
        self.query_one("#trace-summary", Static).update(summary)

    def _rebuild_tree(self) -> None:
        tree = self.query_one("#trace-tree", Tree)
        tree.clear()
        tree.root.expand()
        self._node_map.clear()
        count = _add_nodes(
            tree.root, self._trace_tab.roots, self._filter_pat, self._node_map
        )
        self._visible_count = count
        self._clear_search()
        tree.root.set_label(Text(f"TRACE CALLS  /  {count:,} visible", style="bold"))
        self._update_summary(count)

    def on_tree_node_highlighted(self, event: Tree.NodeHighlighted) -> None:
        tn: Optional[TraceNode] = event.node.data
        if tn is None:
            self.query_one("#detail-title", Static).update(
                "Select a trace to inspect it"
            )
            self.query_one("#detail-meta", Static).update("")
            self.query_one("#detail-args", Static).update("")
            return
        kind = "EVENT" if tn.is_leaf else "SCOPE"
        title = Text(f"{kind}  ", style="bold cyan" if tn.is_leaf else "bold magenta")
        title.append(tn.invoker, style="bold bright_white")
        self.query_one("#detail-title", Static).update(title)

        parts = [f"{tn.short_file}:{tn.line}", f"depth {tn.depth}"]
        if tn.is_leaf:
            parts.append(f"timestamp {_format_ms(tn.ts)}")
        else:
            parts.append(f"duration {_format_ms(tn.duration_ms)}")
            parts.append(f"enter {_format_ms(tn.ts_enter)}")
            parts.append(f"exit {_format_ms(tn.ts_exit)}")
        self.query_one("#detail-meta", Static).update("   |   ".join(parts))
        args = Text("ARGS  ", style="bold dim")
        args.append(tn.args or "(none)")
        self.query_one("#detail-args", Static).update(args)

    def apply_filter(self, pattern: str) -> None:
        if pattern:
            try:
                self._filter_pat = re.compile(pattern, re.IGNORECASE)
            except re.error:
                self._filter_pat = re.compile(re.escape(pattern), re.IGNORECASE)
        else:
            self._filter_pat = None
        self._rebuild_tree()

    def _clear_search(self) -> None:
        self._search_pattern = ""
        self._search_matches = []
        self._search_index = -1

    def search(self, pattern: str) -> None:
        self._clear_search()
        if not pattern:
            self._update_summary(self._visible_count)
            return
        try:
            search_pat = re.compile(pattern, re.IGNORECASE)
        except re.error:
            search_pat = re.compile(re.escape(pattern), re.IGNORECASE)

        self._search_pattern = pattern
        self._search_matches = [
            self._node_map[id(node)]
            for node in self._trace_nodes
            if id(node) in self._node_map
            and (
                search_pat.search(node.invoker)
                or search_pat.search(node.args)
                or search_pat.search(node.file)
            )
        ]
        if not self._search_matches:
            self.app.notify(f"No traces match {pattern!r}", severity="warning")
            self._clear_search()
            self._update_summary(self._visible_count)
            return
        self.search_next()

    def search_next(self, direction: int = 1) -> None:
        if not self._search_matches:
            self.app.notify("Press / to search traces", severity="information")
            return
        self._search_index = (
            self._search_index + direction
        ) % len(self._search_matches)
        target = self._search_matches[self._search_index]
        parent = target.parent
        while parent is not None:
            parent.expand()
            parent = parent.parent
        tree = self.query_one("#trace-tree", Tree)
        tree.select_node(target)
        tree.scroll_to_node(target)
        self._update_summary(self._visible_count)

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
        Binding("/", "search",         "Search",       show=True),
        Binding("n", "search_next",    "Next match",   show=True),
        Binding("shift+n", "search_previous", "Previous", show=False),
        Binding("f", "filter",       "Filter",       show=True),
        Binding("s", "stats",        "Statistics",   show=True),
        Binding("e", "expand_all",   "Expand all",   show=True),
        Binding("c", "collapse_all", "Collapse all", show=True),
        Binding("r", "clear_filter", "Clear filter", show=True),
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

    def action_search(self) -> None:
        pane = self._active_file_pane()
        if pane is None:
            return

        def _apply(result: Optional[str]) -> None:
            if result is not None:
                pane.search(result)

        self.push_screen(
            FilterModal(
                prompt="Search invoker, args, or source file:",
                current=pane._search_pattern,
            ),
            _apply,
        )

    def action_search_next(self) -> None:
        pane = self._active_file_pane()
        if pane:
            pane.search_next()

    def action_search_previous(self) -> None:
        pane = self._active_file_pane()
        if pane:
            pane.search_next(-1)

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

    def action_clear_filter(self) -> None:
        pane = self._active_file_pane()
        if pane:
            pane.apply_filter("")
