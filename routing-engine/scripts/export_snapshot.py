#!/usr/bin/env python3
"""Export a validated EcoRoute schema-v2 graph into a new immutable directory.

JSON mode uses only the standard library. Optional PostGIS mode requires psycopg
3 and caller-supplied SELECT files with the documented canonical column aliases.
No teammate table names, feature encodings, or units are guessed.
"""

from __future__ import annotations

import argparse
import csv
from decimal import Decimal
import json
import math
import os
from pathlib import Path
import re
import shutil
import sys
import tempfile
from typing import Iterable, Mapping
import zlib

NODE_FIELDS = ("node_id", "latitude", "longitude", "elevation_meters")
EDGE_FIELDS = ("edge_id", "from_node_id", "to_node_id", "length_meters",
               "speed_limit_kph", "road_class", "geometry_wkt")
MAX_ID = (1 << 64) - 1
MAX_COUNT = (1 << 32) - 1
ENDPOINT_TOLERANCE = 1e-6


def identifier(value: object, name: str) -> int:
    # String IDs prevent lossy JavaScript JSON serialization above 2**53.
    if isinstance(value, bool) or not isinstance(value, (str, int)):
        raise ValueError(f"{name} must be an unsigned integer or decimal string")
    if not re.fullmatch(r"[0-9]+", str(value)):
        raise ValueError(f"{name} must contain decimal digits only")
    result = int(value)
    if result > MAX_ID:
        raise ValueError(f"{name} exceeds uint64")
    return result


def number(value: object, name: str, *, positive: bool = False) -> float:
    if isinstance(value, bool) or not isinstance(value, (int, float, Decimal)):
        raise ValueError(f"{name} must be a JSON number")
    result = float(value)
    if not math.isfinite(result) or (positive and result <= 0):
        raise ValueError(f"{name} must be finite" + (" and positive" if positive else ""))
    return result


def coordinate(longitude: object, latitude: object) -> tuple[float, float]:
    lon, lat = number(longitude, "longitude"), number(latitude, "latitude")
    if not -180 <= lon <= 180 or not -90 <= lat <= 90:
        raise ValueError("Coordinates must be WGS84 longitude/latitude in degrees")
    return lon, lat


def text(value: object, name: str) -> str:
    if not isinstance(value, str) or any(ord(c) < 32 for c in value):
        raise ValueError(f"{name} must be a single-line string without control characters")
    return value.strip()


def matches(point: tuple[float, float], node: tuple[float, float]) -> bool:
    return (abs(math.remainder(point[0] - node[0], 360.0)) <= ENDPOINT_TOLERANCE
            and abs(point[1] - node[1]) <= ENDPOINT_TOLERANCE)


def fields(row: object, allowed: set[str], required: set[str], name: str) -> Mapping:
    if not isinstance(row, Mapping) or not required <= row.keys() or row.keys() - allowed:
        raise ValueError(f"{name} has missing or unknown fields; required={sorted(required)}")
    return row


def flush_file(stream) -> None:
    stream.flush()
    os.fsync(stream.fileno())


def checksum(path: Path) -> str:
    value = 0
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            value = zlib.crc32(block, value)
    return f"{value:08x}"


def write_snapshot(directory: Path, graph_version: str,
                   nodes: Iterable[Mapping], edges: Iterable[Mapping]) -> tuple[int, int]:
    """Validate while streaming rows; retain only IDs and node coordinates."""
    node_locations: dict[int, tuple[float, float]] = {}
    with (directory / "nodes.csv").open("w", encoding="utf-8", newline="") as stream:
        writer = csv.writer(stream, lineterminator="\n")
        writer.writerow(NODE_FIELDS)
        for source in nodes:
            row = fields(source, set(NODE_FIELDS), set(NODE_FIELDS[:3]), "node")
            node_id = identifier(row["node_id"], "node_id")
            if node_id in node_locations:
                raise ValueError(f"Duplicate node_id {node_id}")
            point = coordinate(row["longitude"], row["latitude"])
            elevation = row.get("elevation_meters")
            if elevation is not None:
                elevation = number(elevation, "elevation_meters")
            node_locations[node_id] = point
            if len(node_locations) > MAX_COUNT:
                raise ValueError("Node count exceeds CSR index range")
            writer.writerow((node_id, point[1], point[0], elevation))
        if not node_locations:
            raise ValueError("At least one node is required")
        flush_file(stream)

    edge_ids: set[int] = set()
    allowed_edges = set(EDGE_FIELDS[:-1]) | {"geometry", "speed_limit_mph"}
    with (directory / "edges.csv").open("w", encoding="utf-8", newline="") as stream:
        writer = csv.writer(stream, lineterminator="\n")
        writer.writerow(EDGE_FIELDS)
        for source in edges:
            row = fields(source, allowed_edges, set(EDGE_FIELDS[:4]), "edge")
            edge_id = identifier(row["edge_id"], "edge_id")
            source_id = identifier(row["from_node_id"], "from_node_id")
            target_id = identifier(row["to_node_id"], "to_node_id")
            if edge_id in edge_ids:
                raise ValueError(f"Duplicate edge_id {edge_id}")
            if source_id not in node_locations or target_id not in node_locations:
                raise ValueError(f"Unknown endpoint on edge {edge_id}")
            length = number(row["length_meters"], "length_meters", positive=True)
            speed = row.get("speed_limit_kph")
            mph = row.get("speed_limit_mph")
            if speed is not None and mph is not None:
                raise ValueError("Supply only one of speed_limit_kph and speed_limit_mph")
            if mph is not None:
                speed = number(mph, "speed_limit_mph", positive=True) * 1.609344
            if speed is not None:
                speed = number(speed, "speed_limit_kph", positive=True)
            road_class = row.get("road_class")
            if road_class is not None:
                road_class = text(road_class, "road_class")
            geometry = row.get("geometry")
            wkt = ""
            if geometry is not None and geometry != []:
                if not isinstance(geometry, (list, tuple)) or len(geometry) < 2:
                    raise ValueError("geometry must be a list of at least two [longitude,latitude] pairs")
                points = []
                for point in geometry:
                    if not isinstance(point, (list, tuple)) or len(point) != 2:
                        raise ValueError("geometry requires exactly two dimensions")
                    points.append(coordinate(*point))
                if not matches(points[0], node_locations[source_id]) or not matches(
                        points[-1], node_locations[target_id]):
                    raise ValueError(f"Geometry is inconsistent with directed endpoints on edge {edge_id}")
                wkt = "LINESTRING (" + ",".join(f"{lon:.17g} {lat:.17g}" for lon, lat in points) + ")"
            edge_ids.add(edge_id)
            if len(edge_ids) > MAX_COUNT:
                raise ValueError("Edge count exceeds CSR index range")
            writer.writerow((edge_id, source_id, target_id, length, speed, road_class, wkt))
        flush_file(stream)

    with (directory / "metadata.txt").open("w", encoding="utf-8", newline="\n") as stream:
        stream.write(f"schema_version=2\ngraph_version={graph_version}\n"
                     f"node_count={len(node_locations)}\nedge_count={len(edge_ids)}\n"
                     f"nodes_crc32={checksum(directory / 'nodes.csv')}\n"
                     f"edges_crc32={checksum(directory / 'edges.csv')}\n")
        flush_file(stream)
    return len(node_locations), len(edge_ids)


def export_snapshot(destination: Path, graph_version: str,
                    nodes: Iterable[Mapping], edges: Iterable[Mapping]) -> tuple[int, int]:
    graph_version = text(graph_version, "graph_version")
    if not graph_version:
        raise ValueError("graph_version cannot be empty")
    destination = destination.absolute()
    parent = destination.parent.resolve(strict=True)
    destination = parent / destination.name
    if not parent.is_dir() or destination.name in ("", ".", ".."):
        raise ValueError("Destination must be a new named directory in an existing parent")
    lock = parent / ("." + destination.name + ".publish.lock")
    # Cooperating publishers cannot race the non-overwriting publication check.
    # Readers only open the final directory after its atomic same-filesystem rename.
    descriptor = os.open(lock, os.O_CREAT | os.O_EXCL | os.O_WRONLY, 0o600)
    os.close(descriptor)
    staging: Path | None = None
    try:
        if destination.exists() or destination.is_symlink():
            raise FileExistsError(f"Snapshot destination already exists: {destination}")
        staging = Path(tempfile.mkdtemp(prefix=".ecoroute-export-", dir=parent)).resolve()
        result = write_snapshot(staging, graph_version, nodes, edges)
        if destination.exists() or destination.is_symlink():
            raise FileExistsError(f"Snapshot destination appeared during export: {destination}")
        os.rename(staging, destination)
        staging = None
        if os.name != "nt":
            descriptor = os.open(parent, os.O_RDONLY)
            try:
                os.fsync(descriptor)
            finally:
                os.close(descriptor)
        return result
    finally:
        if staging is not None:
            # Only remove the staging directory created in this explicit parent.
            if staging.parent != parent or not staging.name.startswith(".ecoroute-export-"):
                raise RuntimeError("Refusing cleanup outside the staging parent")
            shutil.rmtree(staging)
        lock.unlink()


def database_rows(connection, query: str, cursor_name: str):
    # A server-side cursor avoids materializing the complete database result.
    with connection.cursor(name=cursor_name) as cursor:
        cursor.execute(query)
        while batch := cursor.fetchmany(4096):
            yield from batch


def reject_constant(value: str):
    raise ValueError(f"Nonfinite JSON number is not permitted: {value}")


def unique_object(pairs: list[tuple[str, object]]) -> dict:
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f"Duplicate JSON field: {key}")
        result[key] = value
    return result


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument("--input", type=Path, help="Canonical graph JSON")
    source.add_argument("--postgis", action="store_true", help="Read using psycopg 3 in one read-only repeatable-read transaction")
    parser.add_argument("--output", type=Path, required=True, help="New immutable snapshot directory")
    parser.add_argument("--graph-version", help="Required in PostGIS mode; optional JSON value consistency check")
    parser.add_argument("--dsn-env", default="ECOROUTE_POSTGIS_DSN", help="Environment variable holding PostgreSQL connection parameters")
    parser.add_argument("--nodes-sql", type=Path, help="Read-only SELECT with canonical node aliases")
    parser.add_argument("--edges-sql", type=Path, help="Read-only SELECT with canonical edge aliases; geometry is a JSON array of lon/lat pairs")
    args = parser.parse_args(argv)
    try:
        if args.input:
            if args.nodes_sql or args.edges_sql:
                raise ValueError("SQL arguments require --postgis")
            with args.input.open(encoding="utf-8-sig") as stream:
                document = json.load(stream, parse_constant=reject_constant,
                                     object_pairs_hook=unique_object)
            document = fields(document, {"graph_version", "nodes", "edges"},
                              {"graph_version", "nodes", "edges"}, "graph document")
            if not isinstance(document["nodes"], list) or not isinstance(document["edges"], list):
                raise ValueError("nodes and edges must be JSON arrays")
            if args.graph_version and args.graph_version != document["graph_version"]:
                raise ValueError("--graph-version does not match the JSON graph_version")
            count = export_snapshot(args.output, document["graph_version"],
                                    document["nodes"], document["edges"])
        else:
            if not args.graph_version or not args.nodes_sql or not args.edges_sql:
                raise ValueError("PostGIS mode requires --graph-version, --nodes-sql and --edges-sql")
            dsn = os.environ.get(args.dsn_env)
            if not dsn:
                raise ValueError(f"Connection environment variable is unset: {args.dsn_env}")
            try:
                import psycopg
                from psycopg.rows import dict_row
            except ImportError as error:
                raise ValueError("PostGIS mode requires optional dependency psycopg 3") from error
            # Credentials never appear in CLI arguments or successful output.
            with psycopg.connect(dsn, row_factory=dict_row) as connection:
                connection.execute("SET TRANSACTION ISOLATION LEVEL REPEATABLE READ READ ONLY")
                count = export_snapshot(args.output, args.graph_version,
                    database_rows(connection, args.nodes_sql.read_text(encoding="utf-8"), "ecoroute_nodes"),
                    database_rows(connection, args.edges_sql.read_text(encoding="utf-8"), "ecoroute_edges"))
        print(json.dumps({"schema_version": 2, "output": str(args.output),
                          "node_count": count[0], "edge_count": count[1]}))
        return 0
    except (ValueError, OSError, KeyError, TypeError, OverflowError) as error:
        print(f"Export failed: {error}", file=sys.stderr)
        return 2
    except Exception as error:
        # Database driver errors can embed connection details; report only type.
        if args.postgis:
            print(f"PostGIS export failed ({type(error).__name__}); no credentials logged.", file=sys.stderr)
            return 2
        raise


if __name__ == "__main__":
    raise SystemExit(main())
