# QuantLOB Documentation

This directory contains the full reference documentation for QuantLOB.
Read the documents in numbered order for a complete understanding of the project,
or jump directly to the section you need using the index below.

## Document Index

| File                          | Title                     | Description                                                                |
| ----------------------------- | ------------------------- | -------------------------------------------------------------------------- |
| 01_architecture.md            | Architecture Reference    | Directory layout, component map, data flow, threading model, build targets |
| 02_api_reference.md           | API Reference             | Complete public API for every class and struct                             |
| 03_build_and_configuration.md | Build and Configuration   | Prerequisites, CMake options, sanitizer setup, Docker, install             |
| 04_matching_engine.md         | Matching Engine Internals | Price-time priority, crossing algorithm, invariants, complexity            |
| 05_ml_module.md               | Machine Learning Module   | Feature extractor, models, pipeline, Python training scripts               |
| 06_data_formats.md            | Data Formats Reference    | All CSV and text formats produced and consumed by QuantLOB                 |
| 07_performance_guide.md       | Performance Guide         | Throughput targets, latency budget, benchmarks, profiling workflow         |
| 08_testing_guide.md           | Testing Guide             | Test suite structure, all test cases, sanitizer runs, CI matrix            |

## Quick Links

### Getting started

1. Install prerequisites -- see 03_build_and_configuration.md
2. Run `cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build --parallel`
3. Run `./build/quantlob --help` to see all CLI options
4. Run `cd build && ctest --output-on-failure` to verify the build

### Common tasks

| Task                                 | Where to look                                 |
| ------------------------------------ | --------------------------------------------- |
| Understand order matching rules      | 04_matching_engine.md                         |
| Add a new order type                 | 04_matching_engine.md, MatchingEngine.hpp/cpp |
| Use the ML pipeline in your own code | 05_ml_module.md                               |
| Train models on LOBSTER data         | 05_ml_module.md, code/ai_models/python/       |
| Parse LOBSTER CSV files              | 06_data_formats.md, FeedHandler.cpp           |
| Export LOB snapshots to CSV          | 06_data_formats.md, Exporter.hpp              |
| Profile and benchmark                | 07_performance_guide.md                       |
| Write a new test                     | 08_testing_guide.md                           |

## Module Dependencies

```
Order.hpp  (no deps)
    |
    +-- OrderBook.hpp  -->  OrderBook.cpp
    |       |
    |       +-- MatchingEngine.hpp  -->  MatchingEngine.cpp
    |               |
    |               +-- FeedHandler.hpp  -->  FeedHandler.cpp
    |
    +-- Latency.hpp  (header-only)
    +-- Logger.hpp   (header-only)
    +-- MemoryPool.hpp (header-only)
    +-- RingBuffer.hpp (header-only)
    +-- Exporter.hpp   (header-only, depends on Latency + MatchingEngine)

ML module (code/ai_models/, depends on core headers):
    FeatureExtractor.hpp/cpp
    MidPricePredictor.hpp/cpp
    OrderFlowPredictor.hpp/cpp
    AnomalyDetector.hpp/cpp
    MLPipeline.hpp/cpp  (depends on all four above)
```
