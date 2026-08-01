# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 rc4l

# [rc4l] Standalone GoogleTest project, kept separate from the heavy vendored engine build.
cmake_minimum_required(VERSION 3.14)
project(zandrox_tests CXX)

# [rc4l] Tests build at C++17 with RTTI on so GoogleMock works, independent of the engine's C++11/-fno-rtti flags.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# [rc4l] Emit compile_commands.json so clang-tidy can find each translation unit's flags.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(ZANDROX_TESTS_SANITIZE "Build tests with AddressSanitizer + UBSan" OFF)
option(ZANDROX_TESTS_COVERAGE "Build tests with llvm-cov instrumentation" OFF)

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
# [rc4l] *_nettest.cpp are colocated beside the vendored net code they cover but belong to the
# standalone zandrox_tests_net target (they need the i_system shim and networkshared.cpp linked
# in), so keep them out of this monolithic binary. The *_test.cpp glob above does not actually
# match the _nettest.cpp suffix, but the explicit filter documents the routing and is a guard
# against a future rename that would make it match.
list(FILTER TEST_SOURCES EXCLUDE REGEX "_nettest\\.cpp$")
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

# [rc4l] macOS-only Objective-C++ test target for the Cocoa updater networking seam. Kept SEPARATE
# from zandrox_tests because it needs Foundation and MUST compile without ARC, matching the shipping
# i_system_cocoa_net.mm's manual retain/release -- if the memory model differed, the use-after-free
# regression test would be meaningless. The .mm sources are matched by their own *_test.mm glob, so the
# portable zandrox_tests target above (globbing *_test.cpp / *_compute.cpp) never sees them, and no
# Linux/Windows build gains an Objective-C or Foundation dependency. These .mm units sit outside the
# 100%-line coverage gate (that gate measures *_compute.cpp); their guard is ASan + the assertions.
if(APPLE)
  file(GLOB_RECURSE OBJC_TEST_SOURCES CONFIGURE_DEPENDS "${ENGINE_SRC}/*_test.mm")
  if(OBJC_TEST_SOURCES)
    add_executable(zandrox_tests_objc
      ${OBJC_TEST_SOURCES}
      "${ENGINE_SRC}/sdl/i_system_cocoa_net.mm")
    target_include_directories(zandrox_tests_objc PRIVATE "${ENGINE_SRC}")
    target_link_libraries(zandrox_tests_objc PRIVATE GTest::gtest_main)
    target_link_options(zandrox_tests_objc PRIVATE -framework Foundation)
    # Manual reference counting (NOT ARC) to match the engine build of these .mm files.
    target_compile_options(zandrox_tests_objc PRIVATE -Wall -Wextra -fno-objc-arc)
    if(ZANDROX_TESTS_SANITIZE)
      message(STATUS "zandrox_tests_objc: AddressSanitizer + UBSan enabled")
      target_compile_options(zandrox_tests_objc PRIVATE
        -fsanitize=address,undefined -fno-omit-frame-pointer -g)
      target_link_options(zandrox_tests_objc PRIVATE -fsanitize=address,undefined)
    endif()
    gtest_discover_tests(zandrox_tests_objc)
  endif()
endif()

# [rc4l] Wire-format test target for the net serialization core (BYTESTREAM_s). Kept SEPARATE
# from zandrox_tests because its unit-under-test is vendored engine code (networkshared.cpp +
# platform.cpp + huffman/*) that reaches the engine only through Printf/atterm -- so it compiles
# with the tests/shims/net/ i_system.h shim placed FIRST on the include path, shadowing the
# engine's heavy one (the exact recipe the masterserver subproject uses). Its *_nettest.cpp
# tests COLOCATE beside the code they cover (src/zandronum/src/), same as every other test; the
# _nettest.cpp suffix (not a bare _test.cpp) is what routes them here instead of into the
# monolithic zandrox_tests binary, which has no shim and would fail to link networkshared.cpp.
# The shim header cannot colocate -- it would collide with the engine's real i_system.h in that
# directory -- so it lives under tests/shims/. Portable C/C++: runs on Linux and macOS.
file(GLOB_RECURSE NET_TEST_SOURCES CONFIGURE_DEPENDS "${ENGINE_SRC}/*_nettest.cpp")
if(NET_TEST_SOURCES)
  add_executable(zandrox_tests_net
    ${NET_TEST_SOURCES}
    "${ENGINE_SRC}/networkshared.cpp"
    "${ENGINE_SRC}/platform.cpp"
    "${ENGINE_SRC}/huffman/bitreader.cpp"
    "${ENGINE_SRC}/huffman/bitwriter.cpp"
    "${ENGINE_SRC}/huffman/huffcodec.cpp"
    "${ENGINE_SRC}/huffman/huffman.cpp")
  # Shim dir BEFORE the engine tree: networkshared.cpp's #include "i_system.h" must resolve to
  # tests/shims/net/i_system.h; doomtype.h etc. still come from ENGINE_SRC.
  target_include_directories(zandrox_tests_net BEFORE PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/shims/net" "${ENGINE_SRC}")
  # strnicmp is a Windows spelling; the vendored code uses it, POSIX spells it strncasecmp.
  target_compile_definitions(zandrox_tests_net PRIVATE strnicmp=strncasecmp)
  target_link_libraries(zandrox_tests_net PRIVATE GTest::gtest_main)
  # -Wall -Wextra but NOT -Werror: this compiles vendored engine .cpp we don't own (it trips
  # legacy warnings like the deprecated sprintf); the value is the assertions under ASan.
  target_compile_options(zandrox_tests_net PRIVATE -Wall -Wextra -Wno-deprecated-declarations)
  if(ZANDROX_TESTS_SANITIZE)
    message(STATUS "zandrox_tests_net: AddressSanitizer + UBSan enabled")
    target_compile_options(zandrox_tests_net PRIVATE
      -fsanitize=address,undefined -fno-omit-frame-pointer -g)
    target_link_options(zandrox_tests_net PRIVATE -fsanitize=address,undefined)
  endif()
  gtest_discover_tests(zandrox_tests_net)
endif()
