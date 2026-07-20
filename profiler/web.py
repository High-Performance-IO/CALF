from __future__ import annotations

import json
import mimetypes
import threading
import webbrowser
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
        "timestamp_ms": node.ts,
        "enter_ms": node.ts_enter,
        "exit_ms": node.ts_exit,
        "child_count": len(node.children),
    }


def _serialize_level(nodes: list[TraceNode], parent_id: str = "") -> list[dict]:
    return [
        _serialize_node(node, f"{parent_id}.{index}" if parent_id else str(index))
        for index, node in enumerate(nodes)
    ]


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

    def walk(nodes: list[TraceNode], parent_id: str = "") -> None:
        for index, node in enumerate(nodes):
            if len(matches) >= limit:
                return
            node_id = f"{parent_id}.{index}" if parent_id else str(index)
            haystack = f"{node.invoker} {node.args} {node.file}".casefold()
            if needle in haystack:
                matches.append(_serialize_node(node, node_id))
            walk(node.children, node_id)

    walk(roots)
    return matches


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
            asset = _ASSETS.get(parsed.path)
            if asset is not None:
                self._send_asset(asset)
                return
            self.send_error(404)

        def log_message(self, format: str, *args: object) -> None:
            return

    return ThreadingHTTPServer((host, port), TraceHandler)


def run_web(
    tabs: list[TraceTab], host: str = "127.0.0.1", port: int = 8765,
    open_browser: bool = True,
) -> None:
    server = create_web_server(tabs, host, port)
    bound_host, bound_port = server.server_address[:2]
    browser_host = "127.0.0.1" if bound_host in ("0.0.0.0", "::") else bound_host
    url = f"http://{browser_host}:{bound_port}"
    print(f"CALF web explorer: {url}")
    print("Press Ctrl-C to stop.")
    if open_browser:
        threading.Timer(0.3, webbrowser.open, args=(url,)).start()
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
