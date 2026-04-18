# QuantLOB Build and Configuration Guide

## Table of Contents

1. [Prerequisites](#prerequisites)
2. [Quick Start](#quick-start)
3. [CMake Options](#cmake-options)
4. [Build Configurations](#build-configurations)
5. [Sanitizers](#sanitizers)
6. [Docker Build](#docker-build)
7. [Install and Package](#install-and-package)
8. [Compiler Compatibility](#compiler-compatibility)

---

## Prerequisites

| Requirement     | Minimum version                 | Notes                                                                |
| --------------- | ------------------------------- | -------------------------------------------------------------------- |
| CMake           | 3.20                            | FetchContent and generator expressions require this                  |
| C++ compiler    | GCC 12, Clang 14, or MSVC 19.30 | C++20 required (`std::ranges`, `std::optional`, structured bindings) |
| Ninja           | Any recent                      | Strongly recommended; make also works                                |
| Python          | 3.9 (optional)                  | Only for visualisation scripts                                       |
| Docker          | 24 (optional)                   | Only for containerised builds                                        |
| Internet access | At configure time               | FetchContent downloads Catch2 v3.5.3 and Google Benchmark v1.8.3     |

---

## Quick Start

```bash
# Clone or unzip the project, then:
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel

# Run the synthetic simulator
./build/quantlob --events 500000

# Run tests
cd build && ctest --output-on-failure

# Run benchmarks
./build/quantlob_bench
```

A convenience wrapper is provided:

```bash
./scripts/shell/build.sh Release    # Release build
./scripts/shell/build.sh Debug      # Debug build
```

---

## CMake Options

| Option                      | Type   | Default | Description                                |
| --------------------------- | ------ | ------- | ------------------------------------------ |
| `QUANTLOB_BUILD_TESTS`      | BOOL   | ON      | Build `quantlob_tests` (Catch2)            |
| `QUANTLOB_BUILD_BENCHMARKS` | BOOL   | ON      | Build `quantlob_bench` (Google Benchmark)  |
| `QUANTLOB_BUILD_MAIN`       | BOOL   | ON      | Build `quantlob` CLI driver                |
| `QUANTLOB_ENABLE_ASAN`      | BOOL   | OFF     | AddressSanitizer                           |
| `QUANTLOB_ENABLE_TSAN`      | BOOL   | OFF     | ThreadSanitizer                            |
| `QUANTLOB_ENABLE_UBSAN`     | BOOL   | OFF     | UndefinedBehaviorSanitizer                 |
| `QUANTLOB_ENABLE_LTO`       | BOOL   | OFF     | Link-time optimisation (IPO)               |
| `CMAKE_BUILD_TYPE`          | STRING | (none)  | Debug, Release, RelWithDebInfo, MinSizeRel |

Example combining multiple options:

```bash
cmake -B build -G Ninja \
      -DCMAKE_BUILD_TYPE=Release \
      -DQUANTLOB_ENABLE_LTO=ON \
      -DQUANTLOB_BUILD_BENCHMARKS=ON
```

---

## Build Configurations

| Configuration    | Optimisation      | Debug info | Assertions | Use case                    |
| ---------------- | ----------------- | ---------- | ---------- | --------------------------- |
| `Debug`          | -O0               | Full DWARF | Enabled    | Development, sanitizers     |
| `Release`        | -O3 -march=native | None       | Disabled   | Performance measurement     |
| `RelWithDebInfo` | -O2               | Full DWARF | Disabled   | Profiling with symbols      |
| `MinSizeRel`     | -Os               | None       | Disabled   | Embedded / size-constrained |

For benchmark numbers always use `Release`. For sanitizer runs use `Debug`.

---

## Sanitizers

Only one sanitizer may be active at a time (ASAN and TSAN are mutually exclusive).

### AddressSanitizer (ASAN)

Detects heap/stack buffer overflows, use-after-free, and memory leaks.

```bash
cmake -B build_asan -G Ninja \
      -DCMAKE_BUILD_TYPE=Debug \
      -DQUANTLOB_ENABLE_ASAN=ON
cmake --build build_asan --parallel
cd build_asan && ctest --output-on-failure
```

### ThreadSanitizer (TSAN)

Detects data races between threads.

```bash
cmake -B build_tsan -G Ninja \
      -DCMAKE_BUILD_TYPE=Debug \
      -DQUANTLOB_ENABLE_TSAN=ON
cmake --build build_tsan --parallel
cd build_tsan && ctest --output-on-failure
```

### UndefinedBehaviorSanitizer (UBSAN)

Detects signed integer overflow, misaligned accesses, and similar UB.

```bash
cmake -B build_ubsan -G Ninja \
      -DCMAKE_BUILD_TYPE=Debug \
      -DQUANTLOB_ENABLE_UBSAN=ON
cmake --build build_ubsan --parallel
cd build_ubsan && ctest --output-on-failure
```

### Sanitizer interaction table

| ASAN | TSAN | UBSAN | Result                                    |
| ---- | ---- | ----- | ----------------------------------------- |
| ON   | OFF  | OFF   | Memory safety                             |
| OFF  | ON   | OFF   | Data race detection                       |
| OFF  | OFF  | ON    | Undefined behaviour detection             |
| ON   | OFF  | ON    | Combined memory + UB (supported by Clang) |
| ON   | ON   | any   | Not supported; will fail to link          |

---

## Docker Build

```bash
# Build and run the container (runs ./quantlob by default)
docker-compose -f infrastructure/docker/docker-compose.yml up --build

# Or build manually
docker build -f infrastructure/docker/Dockerfile -t quantlob:latest .
docker run --rm quantlob:latest --events 100000 --log-level WARN
```

---

## Install and Package

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_INSTALL_PREFIX=/usr/local
cmake --build build --parallel
cmake --install build
```

This installs:

| Artifact                | Destination                   |
| ----------------------- | ----------------------------- |
| `libquantlob_core.a`    | `$prefix/lib/`                |
| `quantlob` binary       | `$prefix/bin/`                |
| Public headers          | `$prefix/include/lob/`        |
| `QuantLOBTargets.cmake` | `$prefix/lib/cmake/QuantLOB/` |

Downstream CMake projects can then use:

```cmake
find_package(QuantLOB REQUIRED)
target_link_libraries(my_target PRIVATE QuantLOB::quantlob_core)
```

---

## Compiler Compatibility

| Compiler    | Minimum version | C++20 features used                                                                 | Notes                                         |
| ----------- | --------------- | ----------------------------------------------------------------------------------- | --------------------------------------------- |
| GCC         | 12              | Structured bindings, `std::optional`, `[[nodiscard]]`, concepts via `static_assert` | Full support                                  |
| Clang       | 14              | Same as GCC                                                                         | Full support; preferred for sanitizers        |
| MSVC        | 19.30 (VS 2022) | Same subset                                                                         | Windows-only path for `localtime_s` in Logger |
| Apple Clang | 14 (Xcode 14)   | Same                                                                                | Should work; not CI-tested                    |
