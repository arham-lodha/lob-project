cmake_minimum_required(VERSION 3.20)
project(lob_engine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug)
endif()

# Dependencies
include(FetchContent)

FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG        v1.14.0
)

FetchContent_Declare(
  rapidcheck
  GIT_REPOSITORY https://github.com/emil-e/rapidcheck.git
  GIT_TAG        ff6af6fc683159deb51c543b065eba14dfcf329b
)

FetchContent_Declare(
  benchmark
  GIT_REPOSITORY https://github.com/google/benchmark.git
  GIT_TAG        v1.8.4
)

set(RC_ENABLE_GTEST ON CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest rapidcheck benchmark)

# Engine library
add_library(lob_engine
  src/ref_book.cpp
  src/hierarchical_bitset.cpp
  src/fast_book.cpp
)
target_include_directories(lob_engine PUBLIC include/)
target_compile_features(lob_engine PUBLIC cxx_std_20)
target_compile_options(lob_engine PRIVATE -Wall -Wextra -Wpedantic -Werror)

# Tests
enable_testing()

add_executable(lob_tests
  tests/test_matching.cpp
  tests/test_invariants.cpp
  tests/test_hierarchical_bitset.cpp
  tests/test_fast_book.cpp
)

target_link_libraries(lob_tests PRIVATE
  lob_engine
  GTest::gtest_main
  rapidcheck
  rapidcheck_gtest
)
target_compile_options(lob_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)

include(GoogleTest)
gtest_discover_tests(lob_tests)

# Benchmarks
add_executable(lob_bench
  benchmarks/bench_ref_book.cpp
  benchmarks/bench_fast_book.cpp
)
target_link_libraries(lob_bench PRIVATE
  lob_engine
  benchmark::benchmark_main
)
target_compile_options(lob_bench PRIVATE -Wall -Wextra -Wpedantic -Werror -O3)
