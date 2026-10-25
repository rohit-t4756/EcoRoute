# Dependency and portability evidence

| Component | Required for | Observed status |
|---|---|---|
| C++17 compiler, CMake >=3.24, Ninja | Core/tools | PASS: GCC 16.2.0, CMake 4.4.4, Ninja 1.13.2 on Windows UCRT64 |
| RoutingKit CCH | Mandatory routing technology | PASS: local revision 54d49bb0cdea56dde182357522e4e86a03c57852 |
| ZLIB / OpenMP / Threads | Installed RoutingKit and graph CRC | Found and linked; ZLIB 1.3.2, OpenMP 5.2 |
| Python 3.10+ | Exporter/client/contracts | PASS with Python 3.14.7 |
| psycopg 3 + PostGIS | Database export | BLOCKED: package/database/schema absent |
| FastAPI / Pydantic 2 / Uvicorn | Optional HTTP example | NOT RUN: packages absent |
| Redis | External distributed caching | BLOCKED: package/service absent |
| Protobuf/gRPC | Proposed future transport | NOT RUN; no dependency or executable server claimed |
| ONNX model + feature mapping | ML predictions | BLOCKED: model/pipeline absent |
| React/Mapbox backend consumer | End-to-end UI | BLOCKED: repository absent |
| ASan/UBSan/TSan | Instrumented validation | NOT RUN: installed GCC runtime libraries unavailable |

The external RoutingKit tree was not changed by this work. Its Git status shows
an existing modified Makefile. The observed CFLAGS include C++17, O3, fPIC,
-march=native, -ffast-math, and ROUTING_KIT_ASSUME_LITTLE_ENDIAN,
ROUTING_KIT_NO_ALIGNED_ALLOC, ROUTING_KIT_NO_POSIX. EcoRoute itself does not enable
fast-math. Rebuild dependencies for each target architecture; do not distribute
the host-native archive to arbitrary machines.

Obtain the dependency from [RoutingKit](https://github.com/RoutingKit/RoutingKit),
pin the recorded revision, and build its static library with a compatible
toolchain. Supply its prefix via ECOROUTE_ROUTINGKIT_ROOT or ROUTINGKIT_ROOT.
CMake discovers headers and library without a hardcoded developer path.
MSVC is not verified against upstream RoutingKit; use the validated MinGW/UCRT64
toolchain on Windows. Linux dependency build/CI remains to be executed by the
team. A clean EcoRoute configure/build was executed against the available
prebuilt dependency; rebuilding that external dependency from pristine upstream
sources was not executed.

The optional ECOROUTE_SANITIZERS CMake string accepts compiler-supported names
such as address,undefined on a platform with those runtimes. Instrument the
dependency too for full coverage. No sanitizer success is inferred from normal
regression success.