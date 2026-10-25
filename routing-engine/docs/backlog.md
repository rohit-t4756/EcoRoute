# Categorized engineering backlog — 2026-10-25

States: Resolved = implemented and verified; Limited = explicit justified v1
boundary; Blocked = external input unavailable; Deferred = optional improvement.
No unresolved caveat is treated as a hidden pass. M1/M2 acceptance covers the
documented v1 scope on the tested platform, not deployment or model calibration.

| ID | Category / priority | Issue / module | Gate | State | Dependency / acceptance / resolution |
|---|---|---|---|---|---|
| ER-01 | Contract / P1 | Road-class PostGIS/ML normalization | M3 | Blocked | Teammate vocabulary and feature encoder needed; preserve strings, do not fabricate consumption categories |
| ER-02 | Integrity / P1 | Stable directed IDs and graph version | M1/3 | Resolved locally | Version-bound external IDs/CSR mapping; GraphLoader/traffic/export roundtrip; real producer must honor contract |
| ER-03 | Integration / P1 | Geometry export and GeoJSON | M1/3 | Resolved locally | v2 WKT exporter/loader, directed endpoint checks and lon/lat response; exporter/CLI tests |
| ER-04 | Calibration / P1 | Elevation quality and ascent/descent | M2/3 | Limited | Endpoint-only elevations; no intermediate hills; requires surveyed profiles; model_review.md |
| ER-05 | Calibration / P1 | Congestion semantics | M1/3 | Resolved contract, blocked calibration | k>=1 rejection/no clipping, bounded configured maximum, UTC validity; real ML free-flow definition pending |
| ER-06 | Calibration / P1 | Vehicle/CO2e calibration | M2/3 | Blocked | Measured data and accounting boundary absent; examples synthetic; mathematical checks are not empirical accuracy |
| ER-07 | Model / P1 | Segment-splitting sensitivity | M2 | Limited | Uniform-grade invariance tested; arbitrary aggregation counterexample documented; energy-state/elevation-profile model outside v1 |
| ER-08 | Model / P1 | EV/hybrid regeneration | M2 | Limited | Nonnegative per-edge EV credit, disjoint fractions; no charge state; hybrid baseline only |
| ER-09 | Calibration / P1 | Speed-dependent consumption | M2/3 | Limited | Speed limit/fallback is time reference, no calibrated speed consumption curve; pending source data |
| ER-10 | Numerical / P1 | Weight normalization and scale | M2 | Resolved safety, limited meaning | Safe effective scale/error diagnostics implemented; slider calibration still needed |
| ER-11 | Correctness / P0 | Integer path overflow | M2 | Resolved | Conservative simple-path budget; long path tests; ResourceExhausted differs from NoRoute |
| ER-12 | Performance / P2 | Query workspace reuse | M2 | Resolved | Persistent mutex-protected query; benchmarks report measured modest raw-query improvement |
| ER-13 | Performance / P1 | Redundant customization | M2 | Resolved | Identical-weight skip and bounded exact objective cache; shared topology clones |
| ER-14 | Concurrency / P0 | Safe metric snapshots | M2 | Resolved | Immutable bundle publication, retained ownership, synchronized query; Runtime/CCH/Engine concurrency checks |
| ER-15 | Integrity / P0 | Route/cache snapshot consistency | M2 | Resolved locally | Snapshot identity keys + full request/config identity; updates clear caches; no external Redis claimed |
| ER-16 | Correctness / P0 | Exact selected preference | M1/2 | Resolved | Removed epsilon dedup; nextafter endpoint regression and explicit objective scores |
| ER-17 | Performance / P2 | Optional prefetch | M2 | Resolved decision | Default off; five-preference benchmark materially slower than three; lower-level opt-in remains |
| ER-18 | Routing / P1 | Pareto/detour selection | M2 | Limited | Preserve essential exact roles, filter extras only; finite samples are not complete Pareto frontier; quantization bound exposed |
| ER-19 | Data / P1 | Turn restrictions/legal access | M3 | Blocked / v1 non-goal | Source restrictions/access fields absent; requires expanded state graph and tests before legal navigation claim |
| ER-20 | Routing / P1 | Coordinate snapping | M1/2 | Resolved v1 / Limited | Bounded spherical k-d nearest node, direction roles/ties; no directed-edge projection/component rerouting |
| ER-21 | Integrity / P1 | Atomic export/integrity | M1/3 | Resolved locally | Staging rename, cooperative lock, v2 CRC/count validation; live PostGIS transaction untested |
| ER-22 | Build / P1 | RoutingKit portability/reproducibility | M2 | Limited | Revision/flags recorded, prefix-based CMake verified Windows; pristine dependency rebuild and Linux/MSVC CI NOT RUN |
| ER-23 | Performance / P1 | Large-network memory | M2 | Limited | Synthetic peak RSS measured at 22,500 nodes; bounded caches; NYC absent, no hard byte-level allocator limit |
| ER-24 | Performance / P1 | Reproducible phase benchmarks | M2 | Resolved synthetic | Two graph sizes, query/customization/order/throughput/memory; raw JSON retained; real NYC blocked |
| ER-25 | Build / P2 | CMake cleanup | M1/2 | Resolved | Duplicate/empty source removed, presets, real tools, optional tests/benchmarks, warning flags |
| ER-26 | Integration / P1 | Backend/ML/frontend alignment | M3 | Blocked | Python worker contract tested; optional FastAPI/proto provided; real repos and service choices absent |
| ER-27 | Documentation / P1 | Technical defense accuracy | M1/2 | Resolved | README/contracts/architecture/model review/validation/ledger distinguish mathematical from empirical evidence |
| ER-28 | Maintenance / P2 | Empty skeleton cleanup | M1/2 | Resolved | graph_io, empty server/gRPC sources and replaced empty YAML/CSV fixtures removed after reference check |
| ER-29 | Reliability / P1 | CLI/service lifecycle/errors | M1/2 | Resolved local | Persistent CLI, health/EOF/quit, structured errors; Python deadlines kill worker; actual HTTP/gRPC deployment pending |
| ER-30 | Correctness / P1 | Objective-specific score separation | M1/2 | Resolved | Per-preference scale/score/error association; display physical metrics |
| ER-31 | Correctness / P1 | Reversed/disconnected geometry accepted | M1 | Resolved | Endpoint tolerance validation; malformed geometry regression |
| ER-32 | Physical bounds / P1 | EV efficiency above one accepted | M2 | Resolved | Public profile validation enforces battery-to-wheel <=1; cost regression |
| ER-33 | Correctness / P1 | RoutingKit empty-vector address | M2 | Resolved | Stable sentinel pointer for zero-edge metrics; isolated nodes accepted/tested |
| ER-34 | Numerical / P1 | Inactive objective 0*overflow | M2 | Resolved | Skip inactive dimension, extended intermediates and finite bound checks |
| ER-35 | Data / P2 | Metadata BOM/locale/errors | M1 | Resolved | UTF-8 BOM and locale-independent numeric graph parsing, precise malformed input tests |
| ER-36 | Maintenance / P2 | Pasted prose in .gitignore | M1 | Resolved | Clean ignore rules; useful source/fixture files retained |
| ER-37 | Provenance / P1 | No Git metadata in checkout | All | Limited | SHA-256 baseline and source backup retained; file diff inventory/ledger used; no commit/push/history operations |
| ER-38 | Tooling / P2 | Windows sandbox temp ACL failure | Validation | Resolved execution | Exporter rerun and final regression outside sandbox PASS; no expected-value changes |
| ER-39 | Tooling / P2 | Sanitizer runtimes absent | M2 | Blocked tooling | CMake opt-in added; ASan/UBSan/TSan NOT RUN; execute on equipped Linux CI |
| ER-40 | Persistence / P2 | Full hierarchy serialization | M2 | Deferred | Versioned validated order cache implemented; full hierarchy format/ABI and benefit need measurements |
| ER-41 | Integration / P1 | Distributed Redis caching | M3 | Blocked | Version-safe key/TTL contract documented; service/credentials absent |
| ER-42 | Integrity / P1 | Retired traffic version reuse | M3 | Limited producer contract | Engine identity avoids cache collisions, current version conflicts rejected; producer must never reuse any retired ID |
| ER-43 | Reliability / P1 | In-process cancellation | M2/3 | Limited | CCH call not interruptible; Python deadline terminates isolated worker; restart current-traffic policy documented |
| ER-44 | Integrity / P1 | Traffic v1 file publication | M3 | Limited | No CRC manifest; immutable atomic directory publication required from producer; add manifest if real publisher cannot guarantee |
| ER-45 | Integrity / P1 | Same-time competing traffic updates | M2 | Resolved | Different version requires strictly newer validity start; idempotent current version accepted |