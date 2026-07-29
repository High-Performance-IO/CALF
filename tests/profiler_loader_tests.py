import json
import sys

from calf.profiler.loader import TraceTab, discover_tabs, load_trace
from calf.protobuf import calf_trace_pb2


def _event(trace, kind, timestamp, name="", args="", track_uuid=1):
    packet = trace.packet.add(timestamp=timestamp * 1_000_000)
    event = packet.track_event
    event.type = kind
    event.track_uuid = track_uuid
    event.name = name
    event.source_location.function_name = name
    if args:
        annotation = event.debug_annotations.add(name="args")
        annotation.string_value = args


def test_loads_perfetto_trace_tree_and_truncated_tail(tmp_path):
    trace = calf_trace_pb2.Trace()
    _event(trace, calf_trace_pb2.TrackEvent.TYPE_SLICE_BEGIN, 10, "outer")
    _event(trace, calf_trace_pb2.TrackEvent.TYPE_SLICE_BEGIN, 11, "inner")
    _event(trace, calf_trace_pb2.TrackEvent.TYPE_INSTANT, 12, "inner", "event")
    _event(trace, calf_trace_pb2.TrackEvent.TYPE_SLICE_END, 13)
    _event(trace, calf_trace_pb2.TrackEvent.TYPE_SLICE_END, 14)

    path = tmp_path / "syscall_42.perfetto-trace"
    encoded = trace.SerializeToString()
    path.write_bytes(encoded[:-1])

    roots = load_trace(str(path))
    assert roots[0]["invoker"] == "outer"
    assert roots[0]["events"][0]["invoker"] == "inner"
    assert roots[0]["events"][0]["events"][0]["args"] == "event"
    assert roots[0]["events"][0]["ts_exit"] == 13
    assert roots[0]["ts_exit"] is None


def test_discovers_json_and_perfetto_traces(tmp_path):
    trace_dir = tmp_path / "syscall" / "host"
    trace_dir.mkdir(parents=True)
    (trace_dir / "syscall_1.log").write_text(json.dumps({"invoker": "json", "ts": 1}))
    perfetto_dir = tmp_path / "host"
    perfetto_dir.mkdir()
    (perfetto_dir / "calf_42.perfetto-trace").write_bytes(
        calf_trace_pb2.Trace().SerializeToString()
    )

    tabs = discover_tabs(str(tmp_path))
    assert [(tab.kind, tab.tid) for tab in tabs] == [
        ("syscall", "1"),
        ("calf", "process 42 (all threads)"),
    ]


def test_perfetto_loader_uses_bounded_reads(tmp_path, monkeypatch):
    trace = calf_trace_pb2.Trace()
    _event(trace, calf_trace_pb2.TrackEvent.TYPE_INSTANT, 10, "event")
    path = tmp_path / "trace.perfetto-trace"
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

    path = tmp_path / "trace.perfetto-trace"
    path.write_bytes(calf_trace_pb2.Trace().SerializeToString())
    tab = TraceTab(hostname="host", kind="syscall", path=str(path))

    server = create_web_server([tab], port=0)
    try:
        assert tab._roots is None
    finally:
        server.server_close()


def test_cli_does_not_load_tabs(tmp_path, monkeypatch):
    from calf.profiler import __main__ as profiler_main

    trace_dir = tmp_path / "host"
    trace_dir.mkdir(parents=True)
    (trace_dir / "calf_42.perfetto-trace").write_bytes(
        calf_trace_pb2.Trace().SerializeToString()
    )
    captured_tabs = []

    def fake_run_web(tabs, host, port):
        captured_tabs.extend(tabs)

    monkeypatch.setattr("calf.profiler.web.run_web", fake_run_web)
    monkeypatch.setattr(sys, "argv", ["calf", str(tmp_path)])

    profiler_main.main()
    assert captured_tabs
    assert all(tab._roots is None for tab in captured_tabs)
