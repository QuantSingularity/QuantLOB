cmake_minimum_required(VERSION 3.20)
project(QuantLOB VERSION 1.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ---------------------------------------------------------------------------
# Build options
# ---------------------------------------------------------------------------
option(QUANTLOB_BUILD_TESTS      "Build unit tests"                   ON)
option(QUANTLOB_BUILD_BENCHMARKS "Build benchmarks"                   ON)
option(QUANTLOB_BUILD_MAIN       "Build main driver"                  ON)
option(QUANTLOB_BUILD_AI_MODELS         "Build ML module"                    ON)
option(QUANTLOB_ENABLE_ASAN      "Enable AddressSanitizer"            OFF)
option(QUANTLOB_ENABLE_TSAN      "Enable ThreadSanitizer"             OFF)
option(QUANTLOB_ENABLE_UBSAN     "Enable UndefinedBehaviorSanitizer"  OFF)
option(QUANTLOB_ENABLE_LTO       "Enable Link-Time Optimisation"      OFF)

# ---------------------------------------------------------------------------
# Dependencies
# ---------------------------------------------------------------------------
include(FetchContent)

FetchContent_Declare(
  Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG        v3.5.3
)

FetchContent_Declare(
  benchmark
  GIT_REPOSITORY https://github.com/google/benchmark.git
  GIT_TAG        v1.8.3
)

if(QUANTLOB_BUILD_TESTS)
  FetchContent_MakeAvailable(Catch2)
  list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
  include(Catch)
endif()

if(QUANTLOB_BUILD_BENCHMARKS)
  set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
  set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(benchmark)
endif()

# ---------------------------------------------------------------------------
# Compiler flags
# ---------------------------------------------------------------------------
set(QUANTLOB_WARNINGS
  $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic -Wno-unused-parameter>
  $<$<CXX_COMPILER_ID:MSVC>:/W4 /wd4100>
)

set(QUANTLOB_OPTFLAGS
  $<$<AND:$<CXX_COMPILER_ID:GNU,Clang>,$<CONFIG:Release>>:-O3 -march=native>
  $<$<AND:$<CXX_COMPILER_ID:MSVC>,$<CONFIG:Release>>:/O2>
)

# ---------------------------------------------------------------------------
# Helper macro: apply sanitizer flags
# ---------------------------------------------------------------------------
macro(quantlob_apply_sanitizers target)
  if(QUANTLOB_ENABLE_ASAN)
    target_compile_options(${target} PUBLIC -fsanitize=address -fno-omit-frame-pointer)
    target_link_options(${target}    PUBLIC -fsanitize=address)
  endif()
  if(QUANTLOB_ENABLE_TSAN)
    target_compile_options(${target} PUBLIC -fsanitize=thread)
    target_link_options(${target}    PUBLIC -fsanitize=thread)
  endif()
  if(QUANTLOB_ENABLE_UBSAN)
    target_compile_options(${target} PUBLIC -fsanitize=undefined -fsanitize=float-divide-by-zero)
    target_link_options(${target}    PUBLIC -fsanitize=undefined)
  endif()
endmacro()

# ---------------------------------------------------------------------------
# Core library
# ---------------------------------------------------------------------------
add_library(quantlob_core STATIC
  code/src/OrderBook.cpp
  code/src/MatchingEngine.cpp
  code/src/FeedHandler.cpp
)

target_include_directories(quantlob_core
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/code/include>
    $<INSTALL_INTERFACE:include>
)

target_compile_options(quantlob_core PRIVATE ${QUANTLOB_WARNINGS} ${QUANTLOB_OPTFLAGS})
quantlob_apply_sanitizers(quantlob_core)

if(QUANTLOB_ENABLE_LTO)
  set_property(TARGET quantlob_core PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()

# ---------------------------------------------------------------------------
# ML library
# ---------------------------------------------------------------------------
if(QUANTLOB_BUILD_AI_MODELS)
  add_library(quantlob_ml_core STATIC
    code/ai_models/src/FeatureExtractor.cpp
    code/ai_models/src/MidPricePredictor.cpp
    code/ai_models/src/OrderFlowPredictor.cpp
    code/ai_models/src/AnomalyDetector.cpp
    code/ai_models/src/MLPipeline.cpp
  )

  target_include_directories(quantlob_ml_core
    PUBLIC
      $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/code/ai_models/include>
      $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/code/include>
      $<INSTALL_INTERFACE:include>
  )

  target_link_libraries(quantlob_ml_core PUBLIC quantlob_core)
  target_compile_options(quantlob_ml_core PRIVATE ${QUANTLOB_WARNINGS} ${QUANTLOB_OPTFLAGS})
  quantlob_apply_sanitizers(quantlob_ml_core)

  if(QUANTLOB_ENABLE_LTO)
    set_property(TARGET quantlob_ml_core PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
  endif()
endif()

# ---------------------------------------------------------------------------
# Main executable
# ---------------------------------------------------------------------------
if(QUANTLOB_BUILD_MAIN)
  add_executable(quantlob_main code/src/main.cpp)
  target_link_libraries(quantlob_main PRIVATE quantlob_core)
  if(QUANTLOB_BUILD_AI_MODELS)
    target_link_libraries(quantlob_main PRIVATE quantlob_ml_core)
  endif()
  target_compile_options(quantlob_main PRIVATE ${QUANTLOB_WARNINGS} ${QUANTLOB_OPTFLAGS})
  set_target_properties(quantlob_main PROPERTIES OUTPUT_NAME quantlob)
  if(QUANTLOB_ENABLE_LTO)
    set_property(TARGET quantlob_main PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
  endif()
endif()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(QUANTLOB_BUILD_TESTS)
  enable_testing()

  set(TEST_SOURCES code/tests/test_orderbook.cpp)
  if(QUANTLOB_BUILD_AI_MODELS)
    list(APPEND TEST_SOURCES code/ai_models/tests/test_ml.cpp)
  endif()

  add_executable(quantlob_tests ${TEST_SOURCES})
  target_link_libraries(quantlob_tests PRIVATE quantlob_core Catch2::Catch2WithMain)
  if(QUANTLOB_BUILD_AI_MODELS)
    target_link_libraries(quantlob_tests PRIVATE quantlob_ml_core)
  endif()
  target_compile_options(quantlob_tests PRIVATE ${QUANTLOB_WARNINGS})
  catch_discover_tests(quantlob_tests)
endif()

# ---------------------------------------------------------------------------
# Benchmarks
# ---------------------------------------------------------------------------
if(QUANTLOB_BUILD_BENCHMARKS)
  add_executable(quantlob_bench code/benchmarks/bench_matching.cpp)
  target_link_libraries(quantlob_bench PRIVATE quantlob_core benchmark::benchmark)
  target_compile_options(quantlob_bench PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-O3 -march=native>
    $<$<CXX_COMPILER_ID:MSVC>:/O2>
  )
  if(QUANTLOB_ENABLE_LTO)
    set_property(TARGET quantlob_bench PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
  endif()
endif()

# ---------------------------------------------------------------------------
# Install
# ---------------------------------------------------------------------------
install(TARGETS quantlob_core
  EXPORT QuantLOBTargets
  ARCHIVE DESTINATION lib
)

if(QUANTLOB_BUILD_AI_MODELS)
  install(TARGETS quantlob_ml_core
    EXPORT QuantLOBTargets
    ARCHIVE DESTINATION lib
  )
  install(DIRECTORY code/ai_models/include/
    DESTINATION include
  )
endif()

install(DIRECTORY code/include/
  DESTINATION include
)

if(QUANTLOB_BUILD_MAIN)
  install(TARGETS quantlob_main RUNTIME DESTINATION bin)
endif()

install(EXPORT QuantLOBTargets
  FILE      QuantLOBTargets.cmake
  NAMESPACE QuantLOB::
  DESTINATION lib/cmake/QuantLOB
)
