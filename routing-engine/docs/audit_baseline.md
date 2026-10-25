# EcoRoute audit baseline — 2026-10-25

This checkout was inspected before modification. It contains no `.git` directory;
`git status` reports “not a git repository”. Committed versus uncommitted changes
cannot be determined. Original source files and their SHA-256 inventory are saved
under `build/audit-baseline/source/` and `build/audit-baseline/manifest.json`.
Generated build products are not source deliverables.

## Executed baseline

- Existing Debug build: **PASS**, all 13 CTest suites, 2.14 seconds.
- Separate clean Debug configuration: **PASS**, all 13 suites, 1.84 seconds.
- Compiler: MSYS2 UCRT64 GCC 16.2.0, CMake/Ninja, C++17.
- RoutingKit: existing local installation, `C:/SANDY/RoutingKit` supplied as a
  CMake parameter; ZLIB, OpenMP and Threads available.
- No compiler warnings were emitted by the clean baseline build.
- NYC input, PostGIS/FastAPI/React/Redis/ML implementations and credentials were
  not supplied in this repository. Live integration is **BLOCKED** by those inputs.

## Inventory and findings before implementation

| Component | Initial classification | Verified finding / priority |
|---|---|---|
| Result, shared types | Implemented / partial API | Result working; public route count defaults to obsolete five |
| RoadNetwork, RoutingGraph | Implemented, validation gap | Directed/parallel arcs and CSR mapping preserved; geometry endpoints not checked |
| Dijkstra | Implemented reference | Independent integer reference retained |
| CCH order | Implemented | RoutingKit inertial-flow nested dissection present |
| CCH adapter | Implemented but defective under concurrency | Shared metric mutates without synchronization; per-query allocation; overflow confused with no route |
| Cost model | Implemented, uncalibrated | Delay-only auxiliary charge and disjoint gravity credits present; EV efficiency above one accepted; segmentation caveat |
| Weight builder | Implemented, incomplete safety policy | Per-edge checks do not guarantee finite accumulated path cost |
| alternatives.* | Implemented, exactness defect | Epsilon merges near-endpoint preferences; route score has no objective association |
| Graph loader | Implemented v1 | Strict CSV and version parsing; no geometry/integrity manifest |
| Route evaluator | Empty placeholder | Physical accumulation duplicated inside alternatives |
| Traffic, snapper, snapshot manager, engine | Empty placeholders | Required for public end-to-end routing |
| CLI, preprocess, server, benchmark | Empty placeholders | No runnable application or benchmark targets |
| Runtime and service tests | Empty placeholders | Not registered in CTest |
| graph_io.* | Superseded empty placeholders | Compiled empty implementation; graph_loader is the real loader |
| service/*.proto, grpc_service.cpp | Empty placeholders | No functional gRPC contract or dependency |
| README, docs, configuration, fixtures | Empty or comment-only support assets | Must be replaced with executable examples and actual contracts |
| CMake | Working but incomplete | Duplicate alternatives.cpp entry; repeated test boilerplate; no presets |
| .gitignore / .clang-format | Defective / empty configuration | .gitignore contains pasted prose/UI markup; formatting settings absent |

`alternatives.*` and `test_alternatives.cpp` are the implemented modules, not
redundant duplicates. Their names are retained. Local `.vscode` configuration is
user-specific and is preserved.

## Implementation decisions

1. Preserve RoutingKit CCH and the independent Dijkstra reference.
2. Keep topology immutable; share it between bounded objective metrics. Publish
   complete traffic/physical-metric snapshots, and retain them throughout queries.
3. Use reusable synchronized CCH query state; never share mutable query buffers
   without a lock. Measure this conservative design before adding worker pools.
4. Preserve exact selected preferences and at most three unique essential routes.
5. Add graph-safe integer scaling with explicit quantization bounds; report
   representational limits distinctly from disconnected topology.
6. Implement indexed nearest-node snapping v1 with distance and direction-role
   checks. Full directed-edge projection and turn restrictions remain explicit
   limitations requiring richer input and routing state.
7. Extend snapshot interchange compatibly with geometry and integrity checks.
   Provide executable integration adapters without claiming unseen teammate
   implementations were validated.
8. Keep the physical model an explicitly uncalibrated additive estimate. Preserve
   its nonnegative routing invariant and document the limits of endpoint elevation.

The final file inventory, exact changes, evidence and unresolved issues are
maintained in `engineering_changes.md`, `backlog.md` and the validation report.
