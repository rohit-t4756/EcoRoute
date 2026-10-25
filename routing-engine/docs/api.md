# Public API and integration status

Include ecoroute/engine.hpp and link ecoroute_core. Engine::create(options) loads
the graph, profiles and initial traffic; route(request) resolves coordinates;
routeNodes(source,target,request) accepts external uint64 node IDs.
updateTraffic(directory) atomically publishes a complete validated bundle.
status() returns initialization/version information; readiness does not mean
traffic is valid for every requested departure. Route/status/update calls may
overlap. Do not move or destroy an engine during calls.

Requests require vehicle_profile_id, departure_time_unix_seconds and an exact
urgency_preference in [0,1]. max_routes is 1..3, default 3. Detour ratio/seconds
are advisory; essential routes are retained and flagged. max_routes was changed
from the unused skeleton default 5 to match the product contract.

## Executable and Python bridge

CLI accepts --config, --source/--target or four coordinate options, --vehicle,
--preference, --departure, optional --max-routes and detour thresholds. Success
prints JSON with exit 0; failure prints {schema_version:1,error:{code,message}}
with exit 2. --health checks initialization. --help prints usage.

--serve retains one engine and accepts one whitespace-delimited command per
line, with C++ double-quoted string escaping. Responses are one JSON line. Input
is capped at 64 KiB; EOF or quit shuts down. Commands:

```text
health
nodes 1 4 "petrol_demo" 0.5 1800000000 3 0.25 600
coordinates 40 -74 40.01 -73.99 "petrol_demo" 0.5 1800000000 3 0.25 600
update "absolute/path/to/new/traffic"
quit
```

Use integration/engine_client.py rather than formatting commands in a backend.
It uses subprocess argument arrays without a shell, serializes requests, parses
JSON, and terminates timed-out workers before accepting another response.
Multiple workers can provide process parallelism at a memory cost. A worker
timeout restarts with the configured initial traffic; the backend must publish
its intended current snapshot again. No HTTP traffic-update endpoint is exposed.

Optional FastAPI example:
```powershell
$env:ECOROUTE_EXECUTABLE='absolute/path/to/ecoroute_cli.exe'
$env:ECOROUTE_CONFIG='absolute/path/to/engine.conf'
python -m uvicorn integration.fastapi_app:app --host 127.0.0.1 --port 8000
```

Requires FastAPI/Pydantic 2/Uvicorn; these packages were unavailable and the HTTP
example is NOT RUN. The standard-library Python client is tested against C++.
POST /api/v1/route body:
```json
{"source_node_id":"1","target_node_id":"4","vehicle_profile_id":"petrol_demo","preference":0.5,"departure_time_unix_seconds":1800000000}
```

Coordinates in this request are [latitude,longitude]. Returned GeoJSON geometry
and snapped positions are [longitude,latitude]. Segment IDs are decimal JSON
strings. Each route includes roles, matching_preferences, metrics, geometry,
approximation/detour flags and comparison deltas. A path can carry several
roles. Source=self returns a Point and zero metrics. recommended_route_id names
the exact selected route and is scoped to that response.

objective_scores associate integer_cost with its own preference/scale/error
bound. They are diagnostics. Display duration_seconds, distance_meters,
fuel_milliliters, electricity_wh and co2e_grams as the physical quantities.
The slider uses exact IEEE double values, not fuzzy bucket matches. Debounce
frontend requests or send on slider release.

Error categories distinguish InvalidArgument, UnsupportedProfile, SnapFailed,
NoRoute, InvalidGraph/Metric/TrafficSnapshot, VersionMismatch, FileError,
ResourceExhausted and Unavailable/InternalError. CCH range overflow is not
NoRoute. FastAPI maps user errors to 422, NoRoute to 404, worker deadlines to
504 and other engine availability failures to 503.

## Transport and cache decisions

gRPC was an empty placeholder, not an established functioning transport.
service/routing.proto is a proposed typed schema retained for teammate review;
no generated bindings/server/client or live gRPC claim is made. The local worker
is the implemented interim transport. Agree on deployment and deadline semantics
before introducing a gRPC dependency.

The core bounded caches are version-safe by immutable snapshot identity.
A future Redis route key must include graph and traffic versions, normalized
endpoint/snapping policy, exact preference encoding (e.g. float.hex), departure
context, vehicle/profile configuration version, model/objective versions and
route/detour limits. Use a canonical payload hash, not concatenated ambiguous
strings. TTL must not exceed traffic validity and producers must never reuse
versions. Keep weather/timezone context in the prediction version. Store route
responses only after confirming their returned versions. Redis is not connected
or tested here; independent expiring edge_weight:<id> keys cannot establish
snapshot consistency.