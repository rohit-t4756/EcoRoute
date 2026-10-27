"""Executable contract checks for CLI, preprocessing, worker lifecycle and adapter."""
import json
import pathlib
import subprocess
import sys
import tempfile

exe, preprocessor, root = map(pathlib.Path, sys.argv[1:])
sys.path.insert(0, str(root / "integration"))
from engine_client import EngineProcess, EngineError

config = root / "config/engine.example.conf"
base = [str(exe), "--config", str(config)]
def run(args, expected=0):
    result = subprocess.run(args, text=True, capture_output=True, timeout=30)
    assert result.returncode == expected, result.stdout + result.stderr
    return json.loads(result.stdout)
assert run(base + ["--health"])["ready"]
request = ["--source", "1", "--target", "4", "--vehicle", "petrol_demo",
           "--preference", "0.5", "--departure", "1800000000"]
response = run(base + request)
assert len(response["routes"]) == 2
assert response["routes"][0]["geometry"]["coordinates"][0] == [-74, 40]
assert run(base + request + ["--unknown"], 2)["error"]["code"] == "InvalidArgument"
# Temp data under build tree avoids platform-dependent system temp ACLs.
with tempfile.TemporaryDirectory(dir=exe.parent) as directory:
    directory = pathlib.Path(directory)
    order = directory / "toy.order"
    assert run([str(preprocessor), str(root / "fixtures/toy_graph"), str(order)])["order_nodes"] == 5
    cached = directory / "engine.conf"
    cached.write_text(f"graph_file={(root / 'fixtures/toy_graph').as_posix()}\n"
        f"traffic_file={(root / 'fixtures/toy_traffic').as_posix()}\n"
        f"vehicle_profiles_file={(root / 'config/vehicle_profiles.example.csv').as_posix()}\n"
        f"cch_topology_file={order.as_posix()}\n")
    assert run([str(exe), "--config", str(cached)] + request)["routes"] == response["routes"]
    order.write_text(order.read_text().replace("toy-v1", "wrong-version"))
    assert run([str(exe), "--config", str(cached), "--health"], 2)["error"]["code"] == "VersionMismatch"
with EngineProcess(exe, config) as client:
    assert client.health()["ready"]
    kwargs = dict(source_node_id="1", target_node_id="4", vehicle_profile_id="petrol_demo",
                  preference=1.0, departure_time_unix_seconds=1800000000)
    before = client.route(**kwargs)
    assert before["routes"][0]["road_segment_ids"] == ["101", "102"]
    client.update_traffic(root / "fixtures/toy_traffic_updated")
    after = client.route(**kwargs)
    assert after["traffic_version"] == "toy-traffic-v2"
    assert after["routes"][0]["road_segment_ids"] == ["103", "104"]
    try:
        client.route(**{**kwargs, "source_node_id": "4", "target_node_id": "1"})
        raise AssertionError("expected NoRoute")
    except EngineError as error:
        assert error.code == "NoRoute"
    assert client.health()["ready"]
print("PASS: CLI, order cache, dynamic traffic, Python worker bridge, lifecycle")