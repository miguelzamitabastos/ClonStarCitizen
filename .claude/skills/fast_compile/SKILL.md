---
name: fast_compile
description: >-
  Runs accelerated CMake builds with cmake --build build -j$(nproc). Use when
  verifying compile, building ClonStarCitizen, or after C++/CMake/shader changes.
---

# Fast Compile

## Instructions

1. Ensure the build directory exists and is configured:

```bash
test -d build || cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
# If CMakeLists or FetchContent deps changed, reconfigure:
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
```

2. Build with all CPU cores (required verification command):

```bash
cmake --build build -j$(nproc)
```

Or run the helper:

```bash
.cursor/skills/fast_compile/scripts/fast_compile.sh
```

3. Fix compile errors and repeat step 2 until success. Do not claim build success without running this command.

## Notes

- Working directory: repository root (`ClonStarCitizen`).
- Network may be required on first configure for FetchContent (Flecs, GLM).
- Prefer this over single-threaded `cmake --build build`.
