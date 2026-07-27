import json

from calf.profiler.loader import discover_tabs, load_trace
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
