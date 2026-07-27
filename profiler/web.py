from __future__ import annotations

import csv
import html
import io
import json
import mimetypes
import re
import threading
import webbrowser
from collections import Counter, defaultdict
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Optional
from urllib.parse import parse_qs, urlparse

from .loader import TraceNode, TraceTab, compute_stats, flatten


_ASSET_DIR = Path(__file__).with_name("web")
_ASSETS = {
    "/": "index.html",
    "/index.html": "index.html",
    "/app.js": "app.js",
    "/style.css": "style.css",
    "/calf.svg": "calf.svg",
}


def _time_range(nodes: list[TraceNode]) -> tuple[Optional[int], Optional[int]]:
    timestamps = [
        value
        for node in nodes
        for value in (node.ts, node.ts_enter, node.ts_exit)
        if value is not None
    ]
    if not timestamps:
        return None, None
    return min(timestamps), max(timestamps)


def _tab_summary(index: int, tab: TraceTab) -> dict:
    nodes = flatten(tab.roots)
    scopes = sum(not node.is_leaf for node in nodes)
    start, end = _time_range(nodes)
    return {
        "id": index,
        "hostname": tab.hostname,
        "kind": tab.kind,
        "tid": tab.tid,
        "file": Path(tab.path).name,
        "nodes": len(nodes),
        "scopes": scopes,
        "events": len(nodes) - scopes,
        "start_ms": start,
        "end_ms": end,
        "window_ms": end - start if start is not None and end is not None else None,
    }


def _serialize_node(node: TraceNode, node_id: str) -> dict:
    return {
        "id": node_id,
        "invoker": node.invoker,
        "args": node.args,
        "file": node.short_file,
        "line": node.line,
        "depth": node.depth,
        "is_event": node.is_leaf,
        "duration_ms": node.duration_ms,
        "timestamp_ms": node.timestamp,
        "enter_ms": node.ts_enter,
        "exit_ms": node.ts_exit,
        "child_count": len(node.children),
        "request": _request_identity(node.args),
        "linked_requests": _linked_requests(node, node_id),
    }


def _serialize_level(nodes: list[TraceNode], parent_id: str = "") -> list[dict]:
    return [
        _serialize_node(node, f"{parent_id}.{index}" if parent_id else str(index))
        for index, node in enumerate(nodes)
    ]


def _request_terms(args: str) -> tuple[str, ...]:
    values = re.findall(r"=([^,)]+)", args)
    terms: list[str] = []
    for value in values:
        for term in value.strip().split():
            normalized = term.strip("'\"")
            if re.fullmatch(r"\d{4}", normalized) or normalized in {"0", "1"}:
                continue
            if normalized and not normalized.startswith("0x"):
                terms.append(normalized.casefold())
    return tuple(terms)


def _request_identity(args: str) -> Optional[str]:
    match = re.search(r"(?:^|[\s(])req=([^)]*)", args)
    if not match:
        return None
    return " ".join(match.group(1).strip().casefold().split())


def _request_surface_id(node: TraceNode, node_id: str) -> str:
    chain: list[TraceNode] = []
    current: Optional[TraceNode] = node
    while current is not None:
        chain.append(current)
        current = current.parent
    parts = node_id.split(".")
    for candidate in reversed(chain):
        if len(_request_entries(candidate, "", limit=2)) == 1:
            if candidate.parent is None and node.depth == 1:
                continue
            return ".".join(parts[: candidate.depth + 1])
    return node_id


def _linked_requests(node: TraceNode, node_id: str) -> list[dict]:
    requests = _request_entries(node, node_id, limit=2)
    if len(requests) != 1:
        return []
    request = requests[0]
    current = node
    base_depth = len(node_id.split("."))
    for index in request["id"].split(".")[base_depth:]:
        current = current.children[int(index)]
    return requests if _request_surface_id(current, request["id"]) == node_id else []


def _request_entries(
    node: TraceNode, node_id: str, limit: int = 2
) -> list[dict]:
    requests: list[dict] = []

    identity = _request_identity(node.args)
    if identity:
        requests.append({"id": node_id, "request": identity})

    def walk(children: list[TraceNode], parent_id: str) -> None:
        for index, child in enumerate(children):
            if len(requests) >= limit:
                return
            child_id = f"{parent_id}.{index}"
            child_identity = _request_identity(child.args)
            if child_identity:
                requests.append({"id": child_id, "request": child_identity})
            walk(child.children, child_id)

    walk(node.children, node_id)
    return requests


def _is_request_node(node: TraceNode) -> bool:
    return (
        "req=" in node.args
        or "request" in node.invoker.casefold()
        or node.file.replace("\\", "/").endswith("/requests.hpp")
    )


def _request_anchor(node: TraceNode) -> TraceNode:
    current = node
    while current is not None:
        if _is_request_node(current):
            return current
        current = current.parent
    return node


def _request_score(
    terms: tuple[str, ...], candidate: TraceNode, identity: Optional[str] = None
) -> float:
    if not terms or not _is_request_node(candidate):
        return 0
    candidate_identity = _request_identity(candidate.args)
    if identity and candidate_identity:
        return 1 if identity == candidate_identity else 0
    source = set(terms)
    target = set(_request_terms(candidate.args))
    if not target:
        return 0
    score = len(source & target) / len(source | target)
    return score if not identity else score * 0.9


def _node_at(roots: list[TraceNode], node_id: str) -> Optional[TraceNode]:
    try:
        indexes = [int(part) for part in node_id.split(".")]
        if not indexes or any(index < 0 for index in indexes):
            return None
        node = roots[indexes[0]]
        for index in indexes[1:]:
            node = node.children[index]
        return node
    except (ValueError, IndexError):
        return None


def _search_nodes(roots: list[TraceNode], query: str, limit: int = 500) -> list[dict]:
    matches: list[dict] = []
    needle = query.casefold()
    request_terms = _request_terms(query) if "req=" in needle else ()
    request_identity = _request_identity(query)

    def walk(nodes: list[TraceNode], parent_id: str = "") -> None:
        for index, node in enumerate(nodes):
            if len(matches) >= limit:
                return
            node_id = f"{parent_id}.{index}" if parent_id else str(index)
            haystack = f"{node.invoker} {node.args} {node.file}".casefold()
            request_match = (
                request_terms
                and _request_score(request_terms, node, request_identity) >= 0.6
            )
            if needle in haystack or request_match:
                matches.append(_serialize_node(node, node_id))
            walk(node.children, node_id)

    walk(roots)
    return matches


def _event_nodes(roots: list[TraceNode], query: str = "") -> list[dict]:
    events: list[dict] = []
    needle = query.casefold()

    def walk(nodes: list[TraceNode], parent_id: str = "") -> None:
        for index, node in enumerate(nodes):
            node_id = f"{parent_id}.{index}" if parent_id else str(index)
            if node.is_leaf:
                haystack = f"{node.invoker} {node.args} {node.file}".casefold()
                if not needle or needle in haystack:
                    event = _serialize_node(node, node_id)
                    event["context"] = node.parent.invoker if node.parent else "Root"
                    events.append(event)
            walk(node.children, node_id)

    walk(roots)
    events.sort(
        key=lambda event: (
            event["timestamp_ms"] if event["timestamp_ms"] is not None else -1
        )
    )
    return events


def _indexed_nodes(
    roots: list[TraceNode],
) -> list[tuple[TraceNode, str, tuple[str, ...]]]:
    indexed: list[tuple[TraceNode, str, tuple[str, ...]]] = []

    def walk(
        nodes: list[TraceNode], parent_id: str = "", path: tuple[str, ...] = ()
    ) -> None:
        for index, node in enumerate(nodes):
            node_id = f"{parent_id}.{index}" if parent_id else str(index)
            node_path = (*path, node.invoker)
            indexed.append((node, node_id, node_path))
            walk(node.children, node_id, node_path)

    walk(roots)
    return indexed


def _percentile(values: list[int], percentile: int) -> Optional[float]:
    if not values:
        return None
    ordered = sorted(values)
    position = (len(ordered) - 1) * percentile / 100
    lower = int(position)
    upper = min(lower + 1, len(ordered) - 1)
    fraction = position - lower
    return round(ordered[lower] + (ordered[upper] - ordered[lower]) * fraction, 1)


def _trace_analysis(roots: list[TraceNode]) -> dict:
    indexed = _indexed_nodes(roots)
    scopes = [(node, node_id, path) for node, node_id, path in indexed if not node.is_leaf]
    durations = [node.duration_ms for node, _, _ in scopes if node.duration_ms is not None]
    slowest = sorted(
        ((node, node_id, path) for node, node_id, path in scopes if node.duration_ms is not None),
        key=lambda item: item[0].duration_ms or 0,
        reverse=True,
    )[:10]

    counts = Counter(
        (node.invoker, "event" if node.is_leaf else "scope")
        for node, _, _ in indexed
    )
    frequent = [
        {
            "invoker": invoker,
            "count": count,
            "kind": kind,
        }
        for (invoker, kind), count in counts.most_common(10)
    ]

    paths: dict[str, dict[str, int]] = defaultdict(lambda: {"count": 0, "total_ms": 0})
    for node, _, path in scopes:
        if node.duration_ms is None:
            continue
        label = " > ".join(path)
        paths[label]["count"] += 1
        paths[label]["total_ms"] += node.duration_ms
    call_paths = [
        {"path": path, **values}
        for path, values in sorted(
            paths.items(), key=lambda item: item[1]["total_ms"], reverse=True
        )[:10]
    ]

    return {
        "percentiles": {
            f"p{percentile}": _percentile(durations, percentile)
            for percentile in (50, 90, 95, 99)
        },
        "slowest": [
            {**_serialize_node(node, node_id), "path": " > ".join(path)}
            for node, node_id, path in slowest
        ],
        "frequent": frequent,
        "call_paths": call_paths,
    }


def _export_events(tab: TraceTab, summary: dict, query: str, format_name: str) -> tuple[bytes, str, str]:
    events = _event_nodes(tab.roots, query)
    stem = f"{summary['kind']}-{summary['hostname']}-{summary['tid']}-events"
    if format_name == "json":
        body = json.dumps({"summary": summary, "query": query, "events": events}, indent=2)
        return body.encode("utf-8"), "application/json", f"{stem}.json"
    if format_name == "csv":
        output = io.StringIO()
        writer = csv.writer(output)
        writer.writerow(["timestamp_ms", "event", "details", "context", "source", "line"])
        for event in events:
            writer.writerow([
                event["timestamp_ms"], event["invoker"], event["args"],
                event["context"], event["file"], event["line"],
            ])
        return output.getvalue().encode("utf-8"), "text/csv", f"{stem}.csv"
    if format_name == "html":
        rows = "".join(
            "<tr>"
            f"<td>{html.escape(str(event['timestamp_ms']))}</td>"
            f"<td>{html.escape(event['invoker'])}</td>"
            f"<td>{html.escape(event['args'])}</td>"
            f"<td>{html.escape(event['context'])}</td>"
            f"<td>{html.escape(event['file'])}:{event['line']}</td>"
            "</tr>"
            for event in events
        )
        title = html.escape(f"{summary['kind']} / {summary['hostname']} / {summary['tid']}")
        document = f"""<!doctype html><html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>{title}</title>
<style>body{{font:14px system-ui;margin:32px;color:#26313d}}table{{width:100%;border-collapse:collapse}}th,td{{padding:8px;border-bottom:1px solid #ddd;text-align:left}}th{{position:sticky;top:0;background:#f5f6f6}}code{{color:#197980}}</style></head>
<body><h1>{title}</h1><p>{len(events)} events{f' matching {html.escape(query)}' if query else ''}</p>
<table><thead><tr><th>Time (ms)</th><th>Event</th><th>Details</th><th>Context</th><th>Source</th></tr></thead><tbody>{rows}</tbody></table></body></html>"""
        return document.encode("utf-8"), "text/html", f"{stem}.html"
    raise ValueError("Unknown export format")


def create_web_server(
    tabs: list[TraceTab], host: str = "127.0.0.1", port: int = 8765
) -> ThreadingHTTPServer:
    summaries = [_tab_summary(index, tab) for index, tab in enumerate(tabs)]

    class TraceHandler(BaseHTTPRequestHandler):
        def _send_json(self, payload: object, status: int = 200) -> None:
            body = json.dumps(payload, separators=(",", ":")).encode("utf-8")
            self.send_response(status)
            self.send_header("Content-Type", "application/json; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(body)

        def _send_asset(self, name: str) -> None:
            path = _ASSET_DIR / name
            try:
                body = path.read_bytes()
            except OSError:
                self.send_error(404)
                return
            content_type = mimetypes.guess_type(path.name)[0] or "application/octet-stream"
            self.send_response(200)
            self.send_header("Content-Type", f"{content_type}; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(body)

        def _send_download(self, body: bytes, content_type: str, filename: str) -> None:
            self.send_response(200)
            self.send_header("Content-Type", f"{content_type}; charset=utf-8")
            self.send_header("Content-Disposition", f'attachment; filename="{filename}"')
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def do_GET(self) -> None:
            parsed = urlparse(self.path)
            if parsed.path == "/api/overview":
                self._send_json({"tabs": summaries})
                return
            if parsed.path == "/api/trace":
                try:
                    index = int(parse_qs(parsed.query).get("id", [""])[0])
                    tab = tabs[index]
                    if index < 0:
                        raise IndexError
                except (ValueError, IndexError):
                    self._send_json({"error": "Unknown trace"}, status=404)
                    return
                self._send_json({
                    "summary": summaries[index],
                    "roots": _serialize_level(tab.roots),
                    "stats": compute_stats(tab.roots),
                    "analysis": _trace_analysis(tab.roots),
                })
                return
            if parsed.path == "/api/children":
                query = parse_qs(parsed.query)
                try:
                    index = int(query.get("trace", [""])[0])
                    tab = tabs[index]
                    if index < 0:
                        raise IndexError
                except (ValueError, IndexError):
                    self._send_json({"error": "Unknown trace"}, status=404)
                    return
                node_id = query.get("node", [""])[0]
                node = _node_at(tab.roots, node_id)
                if node is None:
                    self._send_json({"error": "Unknown node"}, status=404)
                    return
                self._send_json({"nodes": _serialize_level(node.children, node_id)})
                return
            if parsed.path == "/api/search":
                query = parse_qs(parsed.query)
                try:
                    index = int(query.get("trace", [""])[0])
                    tab = tabs[index]
                    if index < 0:
                        raise IndexError
                except (ValueError, IndexError):
                    self._send_json({"error": "Unknown trace"}, status=404)
                    return
                term = query.get("q", [""])[0].strip()
                matches = _search_nodes(tab.roots, term) if term else []
                self._send_json({"matches": matches, "limited": len(matches) >= 500})
                return
            if parsed.path == "/api/events":
                query = parse_qs(parsed.query)
                try:
                    index = int(query.get("trace", [""])[0])
                    offset = max(0, int(query.get("offset", ["0"])[0]))
                    limit = min(1000, max(1, int(query.get("limit", ["250"])[0])))
                    tab = tabs[index]
                    if index < 0:
                        raise IndexError
                except (ValueError, IndexError):
                    self._send_json({"error": "Invalid event query"}, status=404)
                    return
                events = _event_nodes(tab.roots, query.get("q", [""])[0].strip())
                self._send_json({
                    "events": events[offset:offset + limit],
                    "total": len(events),
                    "offset": offset,
                })
                return
            if parsed.path == "/api/correlate":
                query = parse_qs(parsed.query)
                try:
                    source_index = int(query.get("source", [""])[0])
                    indexes = [
                        int(value) for value in query.get("traces", [""])[0].split(",")
                        if value
                    ]
                    if (
                        source_index < 0
                        or source_index >= len(tabs)
                        or not indexes
                        or any(index < 0 or index >= len(tabs) for index in indexes)
                    ):
                        raise ValueError
                except ValueError:
                    self._send_json({"error": "Invalid correlation query"}, status=400)
                    return
                source_node = _node_at(
                    tabs[source_index].roots, query.get("node", [""])[0]
                )
                if source_node is None:
                    self._send_json({"error": "Unknown source node"}, status=404)
                    return
                source_node = _request_anchor(source_node)
                terms = _request_terms(source_node.args)
                identity = _request_identity(source_node.args)
                request_mode = _is_request_node(source_node) and bool(terms)

                source_equivalents = [
                    node
                    for node, _, _ in _indexed_nodes(tabs[source_index].roots)
                    if _is_request_node(node)
                    and (
                        _request_identity(node.args) == identity
                        if identity
                        else set(_request_terms(node.args)) == set(terms)
                    )
                ]
                source_ordinal = next(
                    (
                        index
                        for index, node in enumerate(source_equivalents)
                        if node is source_node
                    ),
                    0,
                )
                matches = []
                for index in indexes:
                    if index == source_index:
                        continue
                    indexed = _indexed_nodes(tabs[index].roots)
                    if request_mode:
                        scored = [
                            (_request_score(terms, node, identity), node, node_id)
                            for node, node_id, _ in indexed
                            if _is_request_node(node)
                        ]
                        best_score = max((item[0] for item in scored), default=0)
                        candidates = [
                            (node, node_id)
                            for score, node, node_id in scored
                            if score == best_score and score >= 0.6
                        ]
                    else:
                        best_score = 1
                        candidates = [
                            (node, node_id)
                            for node, node_id, _ in indexed
                            if node.invoker == source_node.invoker
                            and node.timestamp is not None
                        ]
                    if not candidates:
                        continue
                    if request_mode:
                        node, node_id = candidates[min(source_ordinal, len(candidates) - 1)]
                    else:
                        node, node_id = min(
                            candidates,
                            key=lambda item: abs(
                                (item[0].timestamp or 0) - (source_node.timestamp or 0)
                            ),
                        )
                    matches.append({
                        "trace": index,
                        "delta_ms": (
                            None
                            if request_mode
                            else (node.timestamp or 0) - (source_node.timestamp or 0)
                        ),
                        "score": round(best_score, 2),
                        "node": _serialize_node(node, node_id),
                    })
                self._send_json({
                    "mode": "request" if request_mode else "invoker",
                    "request": identity,
                    "terms": list(dict.fromkeys(terms)),
                    "matches": matches,
                })
                return
            if parsed.path == "/api/export":
                query = parse_qs(parsed.query)
                try:
                    index = int(query.get("trace", [""])[0])
                    tab = tabs[index]
                    if index < 0:
                        raise IndexError
                    body, content_type, filename = _export_events(
                        tab,
                        summaries[index],
                        query.get("q", [""])[0].strip(),
                        query.get("format", ["json"])[0],
                    )
                except (ValueError, IndexError):
                    self._send_json({"error": "Invalid export query"}, status=400)
                    return
                self._send_download(body, content_type, filename)
                return
            asset = _ASSETS.get(parsed.path)
            if asset is not None:
                self._send_asset(asset)
                return
            self.send_error(404)

        def log_message(self, format: str, *args: object) -> None:
            return

    return ThreadingHTTPServer((host, port), TraceHandler)


def run_web(
    tabs: list[TraceTab], host: str = "127.0.0.1", port: int = 8765
) -> None:
    server = create_web_server(tabs, host, port)
    bound_host, bound_port = server.server_address[:2]
    browser_host = "127.0.0.1" if bound_host in ("0.0.0.0", "::") else bound_host
    print(f"CALF web explorer: http://{browser_host}:{bound_port}")
    print("Press Ctrl-C to stop.")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
