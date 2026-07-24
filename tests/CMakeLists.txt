# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 rc4l

# [rc4l] Standalone GoogleTest project, kept separate from the heavy vendored engine build.
# [rc4l] 3.19 is FuzzTest's floor; the plain test build would still be happy at 3.14.
cmake_minimum_required(VERSION 3.19)
project(zandrox_tests CXX)

# [rc4l] Tests build at C++17 with RTTI on so GoogleMock works, independent of the engine's C++11/-fno-rtti flags.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# [rc4l] Emit compile_commands.json so clang-tidy can find each translation unit's flags.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(ZANDROX_TESTS_SANITIZE "Build tests with AddressSanitizer + UBSan" OFF)
option(ZANDROX_TESTS_COVERAGE "Build tests with llvm-cov instrumentation" OFF)
# [rc4l] Off by default: FuzzTest drags in abseil/re2/protobuf, which roughly triples configure+build
# time. The ASan and coverage jobs never need it, so only the fuzz job pays that cost.
option(ZANDROX_TESTS_FUZZ "Build the FuzzTest property/fuzz harnesses (*_fuzz.cpp)" OFF)

# [rc4l] Pull GoogleTest/GoogleMock via FetchContent, the canonical modern wiring.
include(FetchContent)
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG        v1.15.2
  GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(googletest)

enable_testing()
include(GoogleTest)

# [rc4l] Root of the vendored engine sources that tests compile against. Our own feature
# modules live under features/ INSIDE this tree (src/zandronum/src/features/), so one
# recursive glob covers both vendored-unit tests and feature tests.
set(ENGINE_SRC "${CMAKE_CURRENT_SOURCE_DIR}/../src/zandronum/src")

# [rc4l] Tests colocate beside the code they cover (vendored units and feature modules alike).
# The engine build ignores every *_test.cpp (it compiles an explicit source list, never a glob
# — verified in src/zandronum/src/CMakeLists.txt), so they never leak into the shipped binary.
# CONFIGURE_DEPENDS re-globs at build time, so a new *_test.cpp needs no CMake edit.
file(GLOB_RECURSE TEST_SOURCES CONFIGURE_DEPENDS "${ENGINE_SRC}/*_test.cpp")
if(NOT TEST_SOURCES)
  message(FATAL_ERROR "No *_test.cpp files found under ${ENGINE_SRC}")
endif()

# [rc4l] Pure Compute* implementation TUs live in computation/ folders as *_compute.cpp
# (the actual .cpp we test, not just headers). They have no engine deps, so we compile
# them straight into the test binary — the coverage gate then measures the real .cpp.
file(GLOB_RECURSE COMPUTE_SOURCES CONFIGURE_DEPENDS "${ENGINE_SRC}/*_compute.cpp")

add_executable(zandrox_tests ${TEST_SOURCES} ${COMPUTE_SOURCES})
target_include_directories(zandrox_tests PRIVATE "${ENGINE_SRC}")
target_link_libraries(zandrox_tests PRIVATE GTest::gtest_main GTest::gmock)
target_compile_options(zandrox_tests PRIVATE -Wall -Wextra)

if(ZANDROX_TESTS_SANITIZE)
  # [rc4l] AddressSanitizer + UBSan catch memory errors and undefined behavior at runtime.
  message(STATUS "zandrox_tests: AddressSanitizer + UBSan enabled")
  target_compile_options(zandrox_tests PRIVATE
    -fsanitize=address,undefined -fno-omit-frame-pointer -g)
  target_link_options(zandrox_tests PRIVATE -fsanitize=address,undefined)
endif()

if(ZANDROX_TESTS_COVERAGE)
  # [rc4l] llvm-cov instrumentation records per-line and per-branch execution.
  message(STATUS "zandrox_tests: llvm-cov instrumentation enabled")
  target_compile_options(zandrox_tests PRIVATE
    -fprofile-instr-generate -fcoverage-mapping -g -O0)
  target_link_options(zandrox_tests PRIVATE
    -fprofile-instr-generate -fcoverage-mapping)
endif()

gtest_discover_tests(zandrox_tests)

# [rc4l] ---- Fuzz / property harnesses -------------------------------------------------
# Colocated *_fuzz.cpp files build into their own binary rather than into zandrox_tests, so
# the ASan and coverage jobs never link FuzzTest's dependency tree. Declared after
# zandrox_tests because fuzztest_setup_fuzzing_flags() edits directory-level CMAKE_CXX_FLAGS,
# which a target only picks up at creation time.
if(ZANDROX_TESTS_FUZZ)
  # [rc4l] Same Compute* units the tests cover, driven by a coverage-guided search over the
  # input domain instead of a hand-written value list. See tests/fuzz.sh.
  FetchContent_Declare(
    fuzztest
    GIT_REPOSITORY https://github.com/google/fuzztest.git
    # [rc4l] Pinned to a commit verified to build on both ubuntu/clang and macOS arm64/AppleClang.
    # FuzzTest cuts no releases, so a SHA is the only reproducible pin available.
    GIT_TAG        073bf2392bf11e22d456036a8f7d286bc9a6d346
  )
  FetchContent_MakeAvailable(fuzztest)

  # [rc4l] Adds the coverage instrumentation the engine needs to steer mutation. No-op unless
  # configured with -DFUZZTEST_FUZZING_MODE=on, so the default build still runs each FUZZ_TEST
  # as a bounded randomized unit test.
  fuzztest_setup_fuzzing_flags()

  file(GLOB_RECURSE FUZZ_SOURCES CONFIGURE_DEPENDS "${ENGINE_SRC}/*_fuzz.cpp")
  if(NOT FUZZ_SOURCES)
    message(FATAL_ERROR "ZANDROX_TESTS_FUZZ=ON but no *_fuzz.cpp files found under ${ENGINE_SRC}")
  endif()

  add_executable(zandrox_fuzz ${FUZZ_SOURCES} ${COMPUTE_SOURCES})
  target_include_directories(zandrox_fuzz PRIVATE "${ENGINE_SRC}")
  target_compile_options(zandrox_fuzz PRIVATE -Wall -Wextra)
  link_fuzztest(zandrox_fuzz)
  gtest_discover_tests(zandrox_fuzz)
endif()
