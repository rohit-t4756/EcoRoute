# Versioned interchange contracts

## Graph directories

Treat a published snapshot directory as immutable. Never rewrite a file while
an Engine is reading it. A graph_version names the topology, directed-edge IDs,
attributes and geometry together. New imports may renumber IDs only with a new
version and matching traffic. IDs are unsigned 64-bit integers internally;
PostGIS BIGINT sources are restricted by their own signed range. JSON IDs should
be decimal strings to preserve values above JavaScript's exact integer range.

Both formats use UTF-8 metadata.txt with schema_version and graph_version.
nodes.csv has exactly:
`node_id,latitude,longitude,elevation_meters`.

v1 edges.csv has exactly:
`edge_id,from_node_id,to_node_id,length_meters,speed_limit_kph,road_class`.

v2 appends `geometry_wkt`; values are blank or quoted CSV containing 2D WGS84
`LINESTRING (longitude latitude, ...)`. No SRID prefix, Z/M dimension or other
geometry type is accepted. Coordinates and optional elevation must be finite;
lat/lon ranges are checked. Geometry needs at least two points, with endpoints
matching the directed nodes within 1e-6 degrees per coordinate. Reversed
geometry is rejected. Empty geometry is valid and is flagged approximate in
responses. Length/speed must be positive. Parallel edges and self-loops remain
directed; no reverse edges are manufactured. A nonempty isolated-node graph is
valid; an empty node set is rejected.

v2 metadata additionally requires node_count, edge_count, nodes_crc32 and
edges_crc32. CRCs are eight hex digits over exact file bytes, including line
endings/BOM. The loader hashes the bytes actually parsed. Counts/CRCs detect
corruption and mixed exports, not malicious replacement. v1 remains supported
but has no integrity manifest.

The exporter writes staging files, flushes them, writes metadata last and
renames a sibling directory into a new destination. A cooperative publication
lock prevents competing exporters. Existing destinations are never replaced.
An interrupted process can leave a staging directory/lock requiring operator
cleanup after confirming no exporter is active. Atomic visibility is supported;
power-loss durability still depends on the filesystem/platform.

## Exporter

```text
python scripts/export_snapshot.py --input canonical.json --output NEW_DIRECTORY
python scripts/export_snapshot.py --postgis --graph-version VERSION --nodes-sql nodes.sql --edges-sql edges.sql --output NEW_DIRECTORY
```

Canonical JSON: graph_version, nodes array and edges array. Node fields match the
CSV aliases. Edge fields match the v1 aliases plus optional geometry as
`[[lon,lat],...]`. Use either speed_limit_kph or speed_limit_mph; mph is converted
using 1.609344. Both together are rejected. Unknown fields, duplicate IDs, invalid
numbers and malformed geometry are rejected. Blank speed/class/elevation remain
missing, not silently invented.

PostGIS mode requires optional psycopg 3 and an environment variable
ECOROUTE_POSTGIS_DSN. Explicit user-supplied SQL must return canonical aliases,
including a JSON array of coordinates for geometry. It runs in one read-only
repeatable-read transaction with server-side cursors. Actual teammate tables,
road-category encodings and SQL have not been supplied or verified. Road-class
strings are preserved; consumption adjustments by road class are intentionally
absent until the team agrees on a vocabulary.

## Traffic directory

metadata.txt:
```text
schema_version=1
graph_version=toy-v1
traffic_version=toy-traffic-v1
valid_from_unix_seconds=1700000000
valid_until_unix_seconds=2000000000
```

factors.csv: `edge_id,travel_time_multiplier` with unquoted numeric values.
External directed IDs are mapped explicitly to CSR order. Duplicate/unknown IDs,
graph mismatch, nonfinite multipliers, values below 1 and values above the
configured maximum fail. The default operational maximum is 100; it is a
rejection limit, not a calibrated ML clipping policy. Missing values fail by
default. allow_missing_traffic=true explicitly fills missing factors with 1 and
adds a response warning. An omitted traffic path requires allow_free_flow=true.

A snapshot applies at UTC departure in [valid_from,valid_until). Stale or future
snapshots fail; there is no silent fallback. Different published versions need
strictly increasing valid_from, so concurrent older preparations cannot replace
newer state. The entire route uses departure-time factors; the engine does not
simulate changes during traversal. Prediction timezone, rainfall, feature/model
versions and free-flow reference definitions remain producer responsibilities.
The ML factor must be predicted travel time / the same reference travel time
used by the C++ model. Predictions below 1 need upstream agreement, not clamping.

Publish traffic directories atomically and immutably too. Unlike graph v2,
traffic v1 has no checksum manifest; directory publication is a producer
requirement. Never read unrelated Redis edge keys as if they were one snapshot.

## Model configuration

engine.example.conf documents every supported setting. Cache caps are limited
to 64 objectives and 4096 responses; zero disables them. Version tokens and
profile IDs use letters, digits, underscore, hyphen and dot. Profile CSV has
nine exact columns, no quoted strings, and permits blank mass and wheel-energy
values only as a pair. Empty profile sets and duplicate IDs fail startup.

All shipped demo consumption and CO2e factors are synthetic arithmetic inputs.
They are not validated specifications. Change cost_model_version whenever
vehicle parameters, emissions accounting or cost options change; change
objective_version when normalization/scaling policy changes. Configuration
and vehicle definitions are fixed for an Engine lifetime.