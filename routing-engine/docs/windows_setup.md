# EcoRoute Routing Engine — Windows Development & Build Guide

**Project:** EcoRoute — Traffic-Aware, Environmentally Conscious Route Planning  
**Component:** C++17 Routing Engine with Customizable Contraction Hierarchies (CCH)  
**Platform:** Windows 10/11 (64-bit)  
**Build System:** CMake + Ninja  
**Compiler:** GCC (MSYS2 UCRT64)  
**Routing Library:** RoutingKit (Git submodule)

---

## 1. Overview

This guide explains how to set up, compile, test, and work on the EcoRoute C++ routing engine on a Windows computer.

It is intended for developers cloning the EcoRoute GitHub repository for the first time.

The engine uses:

- **C++17:** Main implementation language.
- **RoutingKit:** CCH preprocessing, customization, and shortest-path queries.
- **CMake:** Configures the C++ project and its dependencies.
- **Ninja:** Executes the compilation and linking steps.
- **MSYS2 UCRT64 / GCC:** Provides the C++ compiler and required Windows-compatible development libraries.
- **CTest:** Runs the automated test suites.
- **Python:** Runs exporter and CLI integration tests and supports the Python integration layer.
- **Git submodules:** Retrieve the exact RoutingKit source version used by the project.

### Important

RoutingKit is already configured as a Git submodule.

**Do not download RoutingKit manually or compile it separately.**

The EcoRoute CMake configuration compiles RoutingKit automatically from the checked-out submodule.

The steps below use the same toolchain family and build configuration that successfully compiled and passed all 20 registered test suites during development.

This is a Windows UCRT64 setup guide. Linux, macOS, and MSVC builds have not been independently validated using these instructions.

---

## 2. Install Git

Git is required to clone the repository and retrieve the RoutingKit submodule.

### Installation

1. Visit https://git-scm.com/downloads/win
2. Download Git for Windows.
3. Run the installer.
4. The default options are generally sufficient.
5. Ensure Git is available from PowerShell and third-party applications.
6. Finish installation.

Open a new **PowerShell** terminal and run:

```powershell
git --version
```

The command should display an installed Git version.

If Git is not recognized, restart the terminal and check that Git for Windows was installed correctly.

### GitHub access

The EcoRoute repository may require GitHub authentication.

Ensure you have access to the project repository before cloning.

---

## 3. Install Visual Studio Code

VS Code is the recommended editor for developing the routing engine.

Download:

https://code.visualstudio.com/

Install it using the normal Windows installer.

### Recommended extensions

Open VS Code, press `Ctrl + Shift + X`, and install:

1. **C/C++** — Microsoft (`ms-vscode.cpptools`)
2. **CMake Tools** — Microsoft (`ms-vscode.cmake-tools`)

Optional extensions include GitLens and the GitHub Pull Requests extension.

These extensions provide:

- C++ code completion and IntelliSense.
- Navigation between declarations and definitions.
- CMake project configuration.
- Build and test integration.
- Debugging support.
- Compiler error diagnostics.

VS Code is only the editor. Installing it does not automatically install GCC, CMake, or Ninja.

---

## 4. Install MSYS2

MSYS2 supplies the Windows-compatible GCC toolchain and development libraries.

### Step 4.1 — Download MSYS2

Visit:

https://www.msys2.org/

Download the official Windows installer.

Run the installer and use the default installation location:

```text
C:\msys64
```

Using the default location is recommended because this guide uses that path.

### Step 4.2 — Open the correct terminal

From the Windows Start menu, open:

**MSYS2 UCRT64**

Do not confuse it with:

- MSYS2 MSYS
- MSYS2 MINGW64
- MSYS2 CLANG64

We use **UCRT64** consistently to avoid mixing compiler environments and incompatible libraries.

### Step 4.3 — Update MSYS2

Inside the **MSYS2 UCRT64 terminal**, run:

```bash
pacman -Syu
```

Accept the update when prompted.

If MSYS2 asks you to close its terminals during a core system update:

1. Allow the update to finish.
2. Close all MSYS2 terminal windows.
3. Reopen **MSYS2 UCRT64**.
4. Run the update command again:

```bash
pacman -Syu
```

Repeat if necessary until the system reports that there is nothing left to update.

Do not proceed with a partially updated MSYS2 installation.

Official documentation:

https://www.msys2.org/docs/updating/

---

## 5. Install the C++ Compiler and Build Dependencies

**Run the following commands inside MSYS2 UCRT64, not PowerShell.**

### Step 5.1 — Install GCC

```bash
pacman -S --needed mingw-w64-ucrt-x86_64-gcc
```

This installs the GNU C/C++ compiler and its required runtime libraries.

### Step 5.2 — Install CMake

```bash
pacman -S --needed mingw-w64-ucrt-x86_64-cmake
```

CMake reads `CMakeLists.txt` and prepares the complete build.

The project requires **CMake 3.24 or newer**.

### Step 5.3 — Install Ninja

```bash
pacman -S --needed mingw-w64-ucrt-x86_64-ninja
```

Ninja executes the build instructions generated by CMake.

The project's `CMakePresets.json` selects Ninja as its generator.

### Step 5.4 — Install Zlib

```bash
pacman -S --needed mingw-w64-ucrt-x86_64-zlib
```

Zlib is required by the RoutingKit build configuration.

### Step 5.5 — Install GDB (recommended)

```bash
pacman -S --needed mingw-w64-ucrt-x86_64-gdb
```

GDB supports debugging C++ programs from VS Code.

It is recommended for development but not necessary simply to compile the engine.

### Alternative — Install everything together

Instead of running the preceding commands individually, a fresh setup can use:

```bash
pacman -S --needed \
    mingw-w64-ucrt-x86_64-gcc \
    mingw-w64-ucrt-x86_64-cmake \
    mingw-w64-ucrt-x86_64-ninja \
    mingw-w64-ucrt-x86_64-zlib \
    mingw-w64-ucrt-x86_64-gdb
```

Press Enter when asked to confirm package installation.

### Step 5.6 — Check the installation

Still inside MSYS2 UCRT64, run:

```bash
g++ --version
cmake --version
ninja --version
gdb --version
```

Each command should return version information.

The GCC installation used in the verified development build was GCC 16.2.0. Exact versions may change as MSYS2 packages are updated.

**OpenMP support:** The GCC UCRT64 toolchain provides the OpenMP compiler/runtime support needed by this project. No separate manual RoutingKit or OpenMP source installation is required.

---

## 6. Configure Windows PATH

This step allows **PowerShell and VS Code** to locate the MSYS2 compiler, CMake, Ninja, and runtime libraries.

Without the correct PATH configuration, compilation may fail even when the software is installed.

### Step 6.1 — Open Environment Variables

1. Press the Windows key.
2. Search for **Edit environment variables for your account**.
3. Open it.
4. Under **User variables**, select `Path`.
5. Click **Edit**.
6. Click **New**.
7. Add:

```text
C:\msys64\ucrt64\bin
```

8. Click OK to save all dialogs.

If you installed MSYS2 in another directory, adjust the path accordingly.

### Step 6.2 — Restart VS Code

Close all existing VS Code windows and PowerShell terminals.

Reopen VS Code or PowerShell so the updated PATH takes effect.

### Step 6.3 — Verify from PowerShell

Run:

```powershell
g++ --version
cmake --version
ninja --version
git --version
```

Then check the executable locations:

```powershell
where.exe g++
where.exe cmake
where.exe ninja
```

The intended paths should include:

```text
C:\msys64\ucrt64\bin\g++.exe
C:\msys64\ucrt64\bin\cmake.exe
C:\msys64\ucrt64\bin\ninja.exe
```

If several copies are installed, ensure the intended UCRT64 tools are selected.

For this project, avoid mixing `C:\msys64\ucrt64\bin` with another MSYS2 compiler environment such as `mingw64`, or accidentally configuring with Visual Studio's MSVC compiler.

### Temporary PATH fix

If PowerShell still cannot find GCC, run:

```powershell
$env:Path = "C:\msys64\ucrt64\bin;$env:Path"
```

This modifies PATH for the **current terminal only**.

It does not replace the permanent environment-variable setup.

Verify again using:

```powershell
where.exe g++
```

---

## 7. Install Python

Python is used for the graph snapshot exporter, integration scripts, and Python-based test suites.

Download Python for Windows from:

https://www.python.org/downloads/windows/

The tested development environment used Python 3.14.7. Other Python versions should be checked against the repository's test and backend requirements.

During installation, make sure Python is available from the command line.

Verify in PowerShell:

```powershell
python --version
```

CMake will look for a Python interpreter when configuring the engine.

Python is not required for RoutingKit's C++ compilation itself, but it is necessary to run the full Python-backed test and integration workflow.

---

## 8. Clone the EcoRoute Repository

The routing engine lives inside the team's main repository.

Repository:

https://github.com/rohit-t4756/EcoRoute

The engine integration was developed on the branch:

```text
feat/routing-engine-integration
```

**Use this branch only after it has been pushed to GitHub.** It is separate from the team's existing `running-branch`.

### Step 8.1 — Choose a working directory

Open PowerShell.

For example:

```powershell
cd C:\
mkdir Projects
cd Projects
```

You may choose any directory where you have write access.

### Step 8.2 — Clone with submodules

Run:

```powershell
git clone --branch feat/routing-engine-integration --recurse-submodules https://github.com/rohit-t4756/EcoRoute.git
```

The `--recurse-submodules` option is important.

It clones:

1. The EcoRoute repository.
2. The C++ routing-engine source.
3. The RoutingKit Git submodule at the version recorded by the project.

Enter your GitHub credentials or complete authentication if prompted.

### Step 8.3 — Enter the repository

```powershell
cd EcoRoute
```

Verify the current branch:

```powershell
git branch --show-current
```

Expected:

```text
feat/routing-engine-integration
```

### If the repository was already cloned

There is no need to clone it again.

After checking that local changes are committed or safely saved, switch to the integration branch and update it:

```powershell
git fetch origin
git switch feat/routing-engine-integration
git pull --ff-only
git submodule update --init --recursive
```

If the branch does not yet exist locally, create a tracking branch with:

```powershell
git switch --track origin/feat/routing-engine-integration
```

Do not overwrite your own uncommitted work when switching branches.

---

## 9. Understanding the RoutingKit Submodule

RoutingKit is a third-party library used by EcoRoute for:

- Customizable Contraction Hierarchies.
- Nested-dissection node ordering.
- Hierarchy construction.
- Metric customization.
- Shortest-path queries.
- Route unpacking into original graph arcs.

### Directory structure

```text
EcoRoute/
├── app/
├── database/
├── scripts/
├── routing-engine/
│   ├── apps/
│   ├── benchmarks/
│   ├── config/
│   ├── docs/
│   ├── fixtures/
│   ├── include/
│   ├── integration/
│   ├── scripts/
│   ├── service/
│   ├── src/
│   ├── tests/
│   ├── third_party/
│   │   └── RoutingKit/
│   │       ├── include/
│   │       ├── src/
│   │       ├── Makefile
│   │       └── README.md
│   ├── CMakeLists.txt
│   ├── CMakePresets.json
│   └── README.md
├── .gitmodules
└── README.md
```

### Why use a Git submodule?

Instead of copying RoutingKit source files directly into our repository, Git records a reference to an exact RoutingKit commit.

This keeps dependency management separate and avoids duplicating upstream history.

The RoutingKit version used during development is:

```text
54d49bb0cdea56dde182357522e4e86a03c57852
```

### Verify the submodule

From the main EcoRoute repository directory:

```powershell
git submodule status
```

The output should identify RoutingKit at the pinned commit.

You can also run:

```powershell
git -C .\routing-engine\third_party\RoutingKit rev-parse HEAD
```

Expected:

```text
54d49bb0cdea56dde182357522e4e86a03c57852
```

### If RoutingKit is missing or empty

Run from the main repository directory:

```powershell
git submodule update --init --recursive
```

This retrieves the version referenced by the parent repository.

**Do not run `git submodule add` again.** RoutingKit is already registered in `.gitmodules`.

### Is detached HEAD normal?

Yes.

A Git submodule normally checks out the exact referenced commit instead of following a changing development branch.

A detached HEAD inside RoutingKit is expected and is not an error.

### Do developers need to compile RoutingKit manually?

**No.**

Our `routing-engine/CMakeLists.txt` creates the `ecoroute_routingkit` library target and compiles the required RoutingKit source files directly.

CMake also handles linking it with the EcoRoute engine.

Do not run RoutingKit's standalone `make all` as part of the normal EcoRoute build. That would compile many additional RoutingKit utilities and tests we do not require.

### Windows compatibility

The engine's CMake configuration includes RoutingKit compatibility definitions for MinGW:

```cmake
if(MINGW)
    target_compile_definitions(ecoroute_routingkit PRIVATE
        ROUTING_KIT_NO_ALIGNED_ALLOC
        ROUTING_KIT_NO_POSIX
        ROUTING_KIT_ASSUME_LITTLE_ENDIAN
    )
endif()
```

These were added because the original pinned RoutingKit source encountered an `aligned_alloc` compilation error in the Windows UCRT64 environment.

**These settings are already part of the integration branch. Teammates should not need to add them manually.**

### Updating RoutingKit

Do not update RoutingKit independently without coordinating with the routing-engine maintainer.

Updating it changes the dependency version and could affect compilation or CCH behavior.

Routine Git operations should use:

```powershell
git submodule update --init --recursive
```

This restores the version recorded by EcoRoute rather than automatically upgrading to the latest upstream commit.

---

## 10. Build the Routing Engine

These are the main compilation commands.

**Run them from PowerShell or the VS Code PowerShell terminal.**

### Step 10.1 — Enter the routing engine directory

From the EcoRoute repository root:

```powershell
cd routing-engine
```

Check that the directory contains:

```text
CMakeLists.txt
CMakePresets.json
```

All remaining build and test commands in this guide assume you are inside `routing-engine/`.

### Step 10.2 — Configure the Debug build

Run:

```powershell
cmake --preset debug
```

CMake will:

1. Detect the C++ compiler.
2. Find Zlib.
3. Find OpenMP.
4. Find thread support.
5. Locate the bundled RoutingKit source.
6. Configure the routing-engine targets.
7. Configure the registered tests.
8. Generate Ninja build files.

The generated build directory will be:

```text
routing-engine/build/debug/
```

A successful configuration should end with messages similar to:

```text
-- Configuring done
-- Generating done
-- Build files have been written to: .../routing-engine/build/debug
```

The compiler should be the MSYS2 UCRT64 GCC compiler, rather than an unintended compiler from another installation.

### Step 10.3 — Compile

Run:

```powershell
cmake --build --preset debug --parallel 8
```

The command builds:

- RoutingKit static library.
- EcoRoute core library.
- Routing CLI.
- Preprocessing executable.
- Enabled benchmarks.
- C++ test executables.

`--parallel 8` allows up to eight compilation jobs.

On a lower-memory computer, use:

```powershell
cmake --build --preset debug --parallel 4
```

If memory is very limited, reduce the number further.

### Step 10.4 — Confirm successful compilation

A successful build finishes without `FAILED` or fatal compiler/linker errors.

Ninja might display messages such as:

```text
[80/80] Linking CXX executable ecoroute_test_engine.exe
```

The exact number of build steps can differ.

Some third-party compiler warnings may appear without causing build failure.

Do not ignore actual compilation errors.

---

## 11. Run the Automated Tests

The project includes 18 C++ test targets and two additional Python-backed integration test registrations.

The exact registered test count can depend on whether Python was detected during CMake configuration.

### Run all available tests

From `routing-engine/`:

```powershell
ctest --preset debug --output-on-failure
```

The tests cover important areas such as:

- Graph construction and loading.
- CSR routing graph.
- Dijkstra baseline routing.
- CCH hierarchy and customization.
- Route correctness and path reconstruction.
- Cost and weight calculations.
- Traffic snapshot validation.
- Alternative-route generation.
- Location snapping.
- Runtime snapshot management.
- Engine behavior.
- Service-contract consistency.
- Python exporter and CLI integration.

### Expected result

In the verified development environment, all 20 registered tests passed.

A successful equivalent environment should report something similar to:

```text
100% tests passed, 0 tests failed out of 20
```

If your machine reports fewer tests, inspect the CMake configuration and whether Python was detected.

### List the registered tests

```powershell
ctest --preset debug -N
```

### Verbose test output

```powershell
ctest --preset debug --verbose
```

If tests fail, retain the complete output and report the failing test name.

**Do not consider the environment fully verified until both compilation and testing succeed.**

Passing the tests verifies the covered development scenarios; it does not yet prove real city-scale performance or complete integration with the live ML, Redis, and PostGIS services.

---

## 12. Build in Release Mode

Debug mode is intended for development and debugging.

For performance measurements and benchmarks, use Release mode.

### Configure Release

```powershell
cmake --preset release
```

### Compile Release

```powershell
cmake --build --preset release --parallel 8
```

### Test Release

```powershell
ctest --preset release --output-on-failure
```

The output is placed in:

```text
routing-engine/build/release/
```

Debug and Release outputs use separate directories.

### Why does Release matter?

Debug compilation generally prioritizes debugging information and does not enable the same level of compiler optimization.

Release compilation is more appropriate for:

- CCH query latency measurements.
- Traffic customization benchmarks.
- Startup and preprocessing performance.
- City-scale routing performance tests.

Use Release when reporting performance figures.

Do not compare Debug timings directly with optimized Release timings.

---

## 13. Configure VS Code for C++ Development

After successfully building from PowerShell, configure VS Code to recognize the same compiler and include directories.

### Step 13.1 — Open the engine folder

In VS Code:

**File → Open Folder**

Select:

```text
EcoRoute/routing-engine
```

This opens the folder containing `CMakeLists.txt` and `CMakePresets.json` as the workspace root.

Git can still recognize the parent EcoRoute repository.

### Step 13.2 — Verify extensions

Ensure these are installed:

- Microsoft C/C++
- Microsoft CMake Tools

### Step 13.3 — Enable CMake Presets

Press:

`Ctrl + Shift + P`

Search for:

```text
CMake: Select Configure Preset
```

Choose:

```text
debug
```

Then run:

```text
CMake: Configure
```

This uses the project's existing CMake configuration.

### Step 13.4 — Build using VS Code

From the Command Palette, select:

```text
CMake: Build
```

Alternatively, use the PowerShell terminal:

```powershell
cmake --build --preset debug --parallel 8
```

Both methods should refer to the same CMake project and preset.

### Step 13.5 — Configure IntelliSense

The C/C++ extension should use the project compilation information provided by CMake Tools.

If IntelliSense fails to detect the right environment, open **User Settings (JSON)** in VS Code and add:

```json
{
    "cmake.useCMakePresets": "always",
    "C_Cpp.default.configurationProvider": "ms-vscode.cmake-tools",
    "C_Cpp.default.compilerPath": "C:/msys64/ucrt64/bin/g++.exe"
}
```

If your existing `settings.json` contains other settings, merge these entries into the existing JSON object rather than replacing unrelated preferences.

**Do not commit personal compiler paths into the shared repository.**

The path shown assumes the default MSYS2 installation.

### Step 13.6 — Verify CMake's selected compiler

In PowerShell, from `routing-engine/`:

```powershell
Select-String `
    -Path .\build\debug\CMakeCache.txt `
    -Pattern 'CMAKE_CXX_COMPILER:FILEPATH'
```

The result should identify the intended UCRT64 GCC compiler, for example:

```text
C:/msys64/ucrt64/bin/c++.exe
```

The `c++.exe` executable is GCC's C++ compiler driver and is valid.

### Step 13.7 — If red IntelliSense errors remain

Try the following, in order:

1. Save the files.
2. Run `CMake: Configure`.
3. Run `C/C++: Reset IntelliSense Database` from the Command Palette.
4. Restart VS Code.
5. Confirm the C/C++ extension uses the CMake Tools configuration provider and the correct compiler.

Red underlines caused by a missing IntelliSense configuration are not necessarily real compiler errors.

**The actual CMake build is the authority on whether the C++ project compiles.**

---

## 14. Understanding CMake, Ninja, and GCC

These tools have different responsibilities.

### CMake

CMake reads:

```text
CMakeLists.txt
CMakePresets.json
```

It decides which source files, libraries, compiler options, and targets are needed.

CMake generates the build instructions.

### Ninja

Ninja executes those build instructions.

It knows which source files require recompilation after a change and supports parallel builds.

### GCC

GCC is the actual C++ compiler.

It compiles `.cpp` files into object files and participates in linking the final executables.

### Simplified process

```text
CMakeLists.txt + CMakePresets.json
                 |
                 v
               CMake
                 |
                 v
         Ninja build files
                 |
                 v
               Ninja
                 |
                 v
         GCC compiler/linker
                 |
                 v
      RoutingKit + EcoRoute
                 |
                 v
       Executables and tests
```

CMake and Ninja are build-time tools.

They do not participate in individual CCH shortest-path queries after the engine has been compiled.

---

## 15. Where Are the Compiled Executables?

For Debug builds, look inside:

```text
routing-engine/build/debug/
```

For Release builds:

```text
routing-engine/build/release/
```

Important targets include:

```text
ecoroute_cli.exe
ecoroute_preprocess.exe
ecoroute_bench.exe
```

Test executables are also generated in the build directory.

To list them from PowerShell after a Debug build:

```powershell
Get-ChildItem .\build\debug\ecoroute*.exe |
    Select-Object Name
```

These executables are build outputs and are not committed to Git.

Their runtime usage, graph snapshot inputs, and CLI request format are documented separately in the engine's README and API documentation.

The repository's Python integration adapter and the team backend must still be configured to use the correct executable path when launching or communicating with the engine.

---

## 16. Daily Development Workflow

Once the initial setup is complete, developers do not need to reinstall dependencies or clone RoutingKit repeatedly.

For ordinary changes to existing C++ code:

```powershell
cd path\to\EcoRoute\routing-engine
```

Build:

```powershell
cmake --build --preset debug --parallel 8
```

Test:

```powershell
ctest --preset debug --output-on-failure
```

### When should CMake be run again?

Reconfigure when:

- `CMakeLists.txt` changes.
- Compiler settings change.
- Build dependencies change.
- A new C++ target or source file is added to the build configuration.
- The build cache is removed.
- RoutingKit integration settings are changed.

Use:

```powershell
cmake --preset debug
```

Then build again.

Ninja normally avoids recompiling unchanged files.

### When should the submodule be updated?

After pulling repository changes that may update the recorded RoutingKit commit, run:

```powershell
git submodule update --init --recursive
```

There is no reason to update the submodule for ordinary changes to EcoRoute's C++ source files.

---

## 17. Troubleshooting

### Problem A — `g++` is not recognized

**Likely cause:** The UCRT64 compiler directory is missing from PATH.

Check:

```powershell
where.exe g++
```

Expected path:

```text
C:\msys64\ucrt64\bin\g++.exe
```

Add the UCRT64 `bin` directory to Windows PATH and restart VS Code.

---

### Problem B — `cmake` or `ninja` is not recognized

Install the required packages from MSYS2 UCRT64:

```bash
pacman -S --needed mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja
```

Check PATH afterward.

---

### Problem C — CMake cannot find Zlib

Example error:

```text
Could NOT find ZLIB
```

Install the correct UCRT64 package:

```bash
pacman -S --needed mingw-w64-ucrt-x86_64-zlib
```

Run this inside MSYS2 UCRT64.

Reconfigure the engine after installation:

```powershell
cmake --preset debug
```

---

### Problem D — CMake cannot find OpenMP

Check whether the correct GCC compiler is being selected.

Run:

```powershell
where.exe g++
g++ --version
```

Ensure CMake is not mixing MSVC, Clang, or a different MinGW environment with the UCRT64 libraries.

Install or repair the UCRT64 GCC package if necessary:

```bash
pacman -S --needed mingw-w64-ucrt-x86_64-gcc
```

---

### Problem E — RoutingKit header not found

Example:

```text
RoutingKit missing.
Run: git submodule update --init --recursive
```

From the EcoRoute repository root:

```powershell
git submodule update --init --recursive
```

Verify that the following file exists:

```text
routing-engine/third_party/RoutingKit/include/routingkit/customizable_contraction_hierarchy.h
```

Do not create a new submodule or copy a compiled RoutingKit library from another computer.

---

### Problem F — RoutingKit `aligned_alloc` compilation error

The pinned RoutingKit revision previously produced this error under MinGW:

```text
error: 'aligned_alloc' was not declared in this scope
```

The integration branch contains a CMake compatibility fix using:

```text
ROUTING_KIT_NO_ALIGNED_ALLOC
ROUTING_KIT_NO_POSIX
ROUTING_KIT_ASSUME_LITTLE_ENDIAN
```

If this error appears:

1. Confirm you are using the correct integration branch.
2. Confirm your `CMakeLists.txt` contains the compatibility block.
3. Reconfigure CMake.
4. Rebuild.

Do not modify third-party RoutingKit source files as the first solution.

---

### Problem G — AVX-512 warning

The compiler may produce a warning similar to:

```text
warning: AVX512F vector return without AVX512F enabled changes the ABI
```

This warning appeared in the successfully compiled and tested development environment.

It does not automatically mean compilation has failed.

However, warnings about CPU instructions and ABI compatibility should be investigated if they lead to test failures or problems on a different processor.

Do not suppress such warnings blindly.

---

### Problem H — CMake picks the wrong compiler

A machine may have multiple C++ compilers installed.

Check:

```powershell
where.exe g++
where.exe cmake
```

To force the correct C++ compiler during fresh configuration:

```powershell
cmake --fresh --preset debug -DCMAKE_CXX_COMPILER="C:/msys64/ucrt64/bin/g++.exe"
```

This refreshes the generated CMake configuration, not the source code.

Then compile:

```powershell
cmake --build --preset debug --parallel 8
```

Avoid using a cached build created with a different compiler or incompatible toolchain.

---

### Problem I — VS Code shows missing C++ headers

Examples:

```text
cannot open source file
#include errors detected
```

Check that:

1. The engine folder was opened as the CMake workspace.
2. CMake configuration completed successfully.
3. The compiler path points to UCRT64 GCC.
4. The CMake Tools configuration provider is enabled.
5. RoutingKit's submodule has been initialized.

Reset IntelliSense if needed.

Do not modify source-code include statements until the compiler configuration has been checked.

---

### Problem J — Build fails with insufficient memory

Reduce parallel compilation:

```powershell
cmake --build --preset debug --parallel 2
```

A lower parallelism setting can reduce peak build-time memory usage.

---

### Problem K — PowerShell cannot run an executable

Run local executables using `.\`.

For example:

```powershell
.\build\debug\ecoroute_cli.exe
```

The CLI expects its documented input protocol; launching it alone is not the same as running a full end-to-end route request.

---

### Problem L — Some tests are missing

Check whether Python was found:

```powershell
python --version
```

Reconfigure:

```powershell
cmake --preset debug
```

List registered tests:

```powershell
ctest --preset debug -N
```

Python-dependent tests are registered only when the appropriate Python interpreter is detected by CMake.

---

### Problem M — Runtime reports missing DLLs

A compiled Windows executable may depend on DLLs supplied by MSYS2.

Ensure:

```text
C:\msys64\ucrt64\bin
```

is available in the runtime PATH.

A working development build does not necessarily produce a self-contained executable that can be copied to an arbitrary Windows PC without its runtime dependencies.

Packaging or containerization is a separate deployment task.

---

## 18. Git and Submodule Best Practices

Follow these rules when contributing to EcoRoute:

1. Create changes on an appropriate feature branch.
2. Do not commit generated `build/` directories.
3. Do not commit machine-specific `.vscode` settings or compiler paths.
4. Do not commit local secrets, passwords, API tokens, or database credentials.
5. Do not manually copy RoutingKit's compiled binaries into the repository.
6. Do not change the pinned RoutingKit commit without coordinating with the team.
7. Run the build and test suites before submitting changes.
8. Use the project's existing CMake presets instead of creating conflicting shared build configurations.

### Verify Git state

From the main EcoRoute repository directory:

```powershell
git status -sb
git submodule status
```

### Submodule changes in a commit

Git records a submodule as a reference to a specific commit, rather than including all its source files directly.

A submodule entry appearing with Git file mode `160000` is normal.

---

## 19. Verification Checklist

Before beginning routing-engine development, confirm:

- [ ] Git is installed and GitHub access works.
- [ ] VS Code and the recommended extensions are installed.
- [ ] MSYS2 UCRT64 is installed and updated.
- [ ] GCC, CMake, Ninja, and Zlib are available.
- [ ] UCRT64 is correctly configured in Windows PATH.
- [ ] Python is installed for integration tests.
- [ ] The correct EcoRoute integration branch is checked out.
- [ ] RoutingKit has been initialized at the pinned commit.
- [ ] `cmake --preset debug` succeeds.
- [ ] `cmake --build --preset debug --parallel 8` succeeds.
- [ ] `ctest --preset debug --output-on-failure` passes all registered tests.
- [ ] VS Code IntelliSense uses the correct compiler and CMake configuration.
- [ ] Release configuration and compilation succeed before performance testing.

A fully checked list indicates that the local development environment is ready for the tested routing-engine functionality.

---

## 20. Quick Start — For Developers Who Already Have the Tools

Once GCC, CMake, Ninja, Zlib, Python, Git, and PATH are configured, the complete checkout/build/test process is:

```powershell
# Clone the integrated branch with RoutingKit
git clone --branch feat/routing-engine-integration --recurse-submodules https://github.com/rohit-t4756/EcoRoute.git

# Enter routing engine
cd EcoRoute\routing-engine

# Configure
cmake --preset debug

# Compile
cmake --build --preset debug --parallel 8

# Run all registered tests
ctest --preset debug --output-on-failure
```

For an existing checkout, initialize the dependency from the repository root:

```powershell
git submodule update --init --recursive
```

For optimized builds:

```powershell
cd routing-engine

cmake --preset release
cmake --build --preset release --parallel 8
ctest --preset release --output-on-failure
```

Run `cd routing-engine` only when you are currently at the main EcoRoute repository root.

---

## 21. Verified Development Baseline and Limitations

The following configuration was successfully used during the integration work:

| Component | Verified configuration |
|---|---|
| Operating system | Windows 11 |
| Compiler | MSYS2 UCRT64 GCC 16.2.0 |
| Language | C++17 |
| Build system | CMake + Ninja |
| RoutingKit revision | `54d49bb0cdea56dde182357522e4e86a03c57852` |
| Zlib | 1.3.2 detected during configuration |
| OpenMP | 5.2 detected during configuration |
| Python | 3.14.7 |
| Debug compilation | Successful |
| Registered automated tests | 20/20 passed |

**Remaining validation:** The project must still be checked on other developers' machines and against realistic city-scale data. Successful local compilation does not imply that the overall EcoRoute application is already integrated with the teammates' live PostGIS, ML, Redis, or frontend components.

The purpose of this guide is to reproduce the verified C++ build and test environment, not to claim that the entire application has completed production validation.

---

## 22. Official References

- [MSYS2 installation](https://www.msys2.org/)
- [MSYS2 updating guide](https://www.msys2.org/docs/updating/)
- [MSYS2 CMake guide](https://www.msys2.org/docs/cmake/)
- [MinGW-w64 Windows/MSYS2 instructions](https://www.mingw-w64.org/getting-started/msys2/)
- [Git for Windows](https://git-scm.com/downloads/win)
- [Visual Studio Code](https://code.visualstudio.com/)
- [Microsoft CMake Tools — Presets](https://github.com/microsoft/vscode-cmake-tools/blob/main/docs/cmake-presets.md)
- [VS Code C++ IntelliSense](https://code.visualstudio.com/docs/cpp/configure-intellisense)
- [RoutingKit repository](https://github.com/RoutingKit/RoutingKit)

---

**EcoRoute — C++ Routing Engine**

*Build the dependency, compile the engine, run the tests, and verify the environment before starting integration work.*
