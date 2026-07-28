import json
import sys

from calf.profiler.loader import TraceTab, discover_tabs, load_trace
from calf.protobuf import calf_trace_pb2


def _record(trace, kind, scope_id, timestamp, parent_scope_id=0, **fields):
    record = trace.records.add(
        kind=kind,
        scope_id=scope_id,
        parent_scope_id=parent_scope_id,
        timestamp_ms=timestamp,
    )
    for name, value in fields.items():
        setattr(record, name, value)


def test_loads_protobuf_trace_tree_and_truncated_tail(tmp_path):
    trace = calf_trace_pb2.TraceFile()
    _record(trace, calf_trace_pb2.TraceRecord.SCOPE_ENTER, 1, 10, invoker="outer")
    _record(trace, calf_trace_pb2.TraceRecord.SCOPE_ENTER, 2, 11, 1, invoker="inner")
    _record(trace, calf_trace_pb2.TraceRecord.EVENT, 2, 12, args="event")
    _record(trace, calf_trace_pb2.TraceRecord.SCOPE_EXIT, 2, 13, 1)
    _record(trace, calf_trace_pb2.TraceRecord.SCOPE_EXIT, 1, 14)

    path = tmp_path / "syscall_42.pb"
    encoded = trace.SerializeToString()
    path.write_bytes(encoded[:-1])

    roots = load_trace(str(path))
    assert roots[0]["invoker"] == "outer"
    assert roots[0]["events"][0]["invoker"] == "inner"
    assert roots[0]["events"][0]["events"][0]["args"] == "event"
    assert roots[0]["events"][0]["ts_exit"] == 13
    assert roots[0]["ts_exit"] is None


def test_discovers_json_and_protobuf_traces(tmp_path):
    trace_dir = tmp_path / "syscall" / "host"
    trace_dir.mkdir(parents=True)
    (trace_dir / "syscall_1.log").write_text(json.dumps({"invoker": "json", "ts": 1}))
    (trace_dir / "syscall_2.pb").write_bytes(calf_trace_pb2.TraceFile().SerializeToString())

    tabs = discover_tabs(str(tmp_path))
    assert [tab.tid for tab in tabs] == ["1", "2"]


def test_protobuf_loader_uses_bounded_reads(tmp_path, monkeypatch):
    trace = calf_trace_pb2.TraceFile()
    _record(trace, calf_trace_pb2.TraceRecord.EVENT, 1, 10, invoker="event")
    path = tmp_path / "trace.pb"
    path.write_bytes(trace.SerializeToString())

    real_open = open

    class BoundedReader:
        def __init__(self, wrapped):
            self.wrapped = wrapped

        def __enter__(self):
            return self

        def __exit__(self, *args):
            return self.wrapped.__exit__(*args)

        def read(self, size=-1):
            assert size >= 0
            return self.wrapped.read(size)

    def bounded_open(*args, **kwargs):
        return BoundedReader(real_open(*args, **kwargs))

    monkeypatch.setattr("builtins.open", bounded_open)
    assert load_trace(str(path))[0]["invoker"] == "event"


def test_web_server_creation_does_not_load_tabs(tmp_path):
    from calf.profiler.web import create_web_server

    path = tmp_path / "trace.pb"
    path.write_bytes(calf_trace_pb2.TraceFile().SerializeToString())
    tab = TraceTab(hostname="host", kind="syscall", path=str(path))

    server = create_web_server([tab], port=0)
    try:
        assert tab._roots is None
    finally:
        server.server_close()


def test_cli_does_not_load_tabs(tmp_path, monkeypatch):
    from calf.profiler import __main__ as profiler_main

    trace_dir = tmp_path / "syscall" / "host"
    trace_dir.mkdir(parents=True)
    (trace_dir / "trace.pb").write_bytes(
        calf_trace_pb2.TraceFile().SerializeToString()
    )
    captured_tabs = []

    def fake_run_web(tabs, host, port):
        captured_tabs.extend(tabs)

    monkeypatch.setattr("calf.profiler.web.run_web", fake_run_web)
    monkeypatch.setattr(sys, "argv", ["calf", str(tmp_path)])

    profiler_main.main()
    assert captured_tabs
    assert all(tab._roots is None for tab in captured_tabs)
