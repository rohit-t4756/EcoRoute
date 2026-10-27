"""Executable interchange check; needs no database or third-party Python packages."""

import argparse
import copy
import csv
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
import zlib

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("export_snapshot", ROOT / "scripts/export_snapshot.py")
EXPORTER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(EXPORTER)
ENGINE = None


def graph():
    return {
        "graph_version": "export-contract-v1",
        "nodes": [
            {"node_id": "9007199254740993", "latitude": 40.0, "longitude": -74.0},
            {"node_id": "9007199254740994", "latitude": 40.001, "longitude": -74.0,
             "elevation_meters": 20.0},
            {"node_id": "9007199254740995", "latitude": 40.002, "longitude": -74.0},
        ],
        # Deliberately not in CSR order; parallel edges remain distinct.
        "edges": [
            {"edge_id": "18446744073709551615", "from_node_id": "9007199254740994",
             "to_node_id": "9007199254740995", "length_meters": 130.0,
             "speed_limit_mph": 25.0, "road_class": "collector,urban",
             "geometry": [[-74.0, 40.001], [-74.0001, 40.0015], [-74.0, 40.002]]},
            {"edge_id": "9007199254740997", "from_node_id": "9007199254740993",
             "to_node_id": "9007199254740994", "length_meters": 120.0,
             "speed_limit_kph": 40.0,
             "geometry": [[-74.0, 40.0], [-74.0, 40.001]]},
            {"edge_id": "9007199254740998", "from_node_id": "9007199254740993",
             "to_node_id": "9007199254740994", "length_meters": 150.0,
             "speed_limit_kph": 30.0},
        ],
    }


class ExportContractTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="ecoroute-export-test-")
        self.root = Path(self.temporary.name)
        self.destination = self.root / "snapshot"
        self.document = graph()

    def tearDown(self):
        self.temporary.cleanup()

    def export(self):
        return EXPORTER.export_snapshot(self.destination, self.document["graph_version"],
                                        self.document["nodes"], self.document["edges"])

    def test_exact_ids_geometry_speed_and_integrity(self):
        self.assertEqual(self.export(), (3, 3))
        metadata = dict(line.split("=", 1) for line in
                        (self.destination / "metadata.txt").read_text().splitlines())
        self.assertEqual(metadata["schema_version"], "2")
        for name in ("nodes", "edges"):
            data = (self.destination / f"{name}.csv").read_bytes()
            self.assertEqual(metadata[f"{name}_crc32"], f"{zlib.crc32(data):08x}")
        with (self.destination / "edges.csv").open(newline="") as stream:
            rows = list(csv.DictReader(stream))
        self.assertEqual(rows[0]["edge_id"], "18446744073709551615")
        self.assertAlmostEqual(float(rows[0]["speed_limit_kph"]), 40.2336)
        self.assertEqual(rows[0]["road_class"], "collector,urban")
        self.assertTrue(rows[0]["geometry_wkt"].startswith("LINESTRING (-74 40.000"))
        original = (self.destination / "metadata.txt").read_bytes()
        with self.assertRaises(FileExistsError):
            self.export()
        self.assertEqual((self.destination / "metadata.txt").read_bytes(), original)

    def test_validation_failure_never_publishes(self):
        invalid_documents = []
        for mutation in (
            lambda d: d["nodes"].append(copy.deepcopy(d["nodes"][0])),
            lambda d: d["edges"][0].update(from_node_id="404"),
            lambda d: d["edges"][0].update(length_meters=0),
            lambda d: d["edges"][0].update(speed_limit_kph=20),
            lambda d: d["edges"][0].update(geometry=list(reversed(d["edges"][0]["geometry"]))),
            lambda d: d["nodes"][0].update(latitude=float("nan")),
            lambda d: d["edges"][0].update(edge_id=18446744073709551616),
            lambda d: d["edges"][0].update(road_class="bad\nrow"),
            lambda d: d["edges"][0].update(unknown_field=1),
        ):
            document = graph()
            mutation(document)
            invalid_documents.append(document)
        for document in invalid_documents:
            with self.subTest(document=document):
                self.document = document
                with self.assertRaises(ValueError):
                    self.export()
                self.assertFalse(self.destination.exists())
                self.assertEqual(list(self.root.iterdir()), [])

    def test_isolated_nodes_and_json_cli(self):
        self.document["edges"] = []
        source = self.root / "input.json"
        source.write_text(json.dumps(self.document), encoding="utf-8")
        result = subprocess.run([sys.executable, str(ROOT / "scripts/export_snapshot.py"),
                                 "--input", str(source), "--output", str(self.destination)],
                                capture_output=True, text=True, timeout=15)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(json.loads(result.stdout)["edge_count"], 0)

    def test_cpp_roundtrip(self):
        if ENGINE is None:
            self.skipTest("C++ CLI not supplied; pass --engine to run the integration contract")
        self.export()
        profiles = self.root / "profiles.csv"
        profiles.write_text(
            "id,type,consumption_per_100km,auxiliary_consumption_per_hour,co2e_grams_per_unit,"
            "mass_kg,wheel_energy_kwh_per_unit,downhill_propulsion_fraction,regenerative_recovery_fraction\n"
            "demo,petrol,7,0.8,2300,,,1,0\n", encoding="utf-8")
        config = self.root / "engine.conf"
        config.write_text(f"graph_file={self.destination.as_posix()}\n"
                          f"vehicle_profiles_file={profiles.as_posix()}\nallow_free_flow=true\n",
                          encoding="utf-8")
        base = [str(ENGINE), "--config", str(config), "--vehicle", "demo", "--preference", "0.5",
                "--departure", "1800000000"]
        result = subprocess.run(base + ["--source", "9007199254740993", "--target", "9007199254740995"],
                                capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stderr + result.stdout)
        response = json.loads(result.stdout)
        self.assertEqual(response["graph_version"], "export-contract-v1")
        self.assertEqual(len(response["routes"]), 1)
        route = response["routes"][0]
        self.assertEqual(route["road_segment_ids"], ["9007199254740997", "18446744073709551615"])
        self.assertEqual(route["geometry"]["type"], "LineString")
        self.assertEqual(route["geometry"]["coordinates"][0], [-74.0, 40.0])
        self.assertEqual(route["geometry"]["coordinates"][-1], [-74.0, 40.002])
        reverse = subprocess.run(base + ["--source", "9007199254740995", "--target", "9007199254740993"],
                                 capture_output=True, text=True, timeout=30)
        self.assertEqual(reverse.returncode, 2)
        # A byte-level change must fail before the graph becomes a runnable engine.
        with (self.destination / "edges.csv").open("ab") as stream:
            stream.write(b"\n")
        invalid = subprocess.run(base + ["--source", "9007199254740993", "--target", "9007199254740995"],
                                 capture_output=True, text=True, timeout=30)
        self.assertEqual(invalid.returncode, 2)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--engine", type=Path)
    args, remaining = parser.parse_known_args()
    ENGINE = args.engine
    unittest.main(argv=[sys.argv[0], *remaining])
