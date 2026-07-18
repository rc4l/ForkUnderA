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

# [rc4l] Root of the vendored engine sources that tests compile against.
set(ENGINE_SRC "${CMAKE_CURRENT_SOURCE_DIR}/../src/zandronum/src")

# [rc4l] Colocated tests: each *_test.cpp lives next to the code it covers, inside the
# engine tree. The engine build ignores them (it compiles an explicit source list, never
# a glob — verified in src/zandronum/src/CMakeLists.txt), so they never leak into the
# shipped binary. CONFIGURE_DEPENDS re-globs at build time, so adding a new *_test.cpp
# needs no CMake edit — drop the file next to its source and rebuild.
file(GLOB_RECURSE TEST_SOURCES CONFIGURE_DEPENDS "${ENGINE_SRC}/*_test.cpp")
if(NOT TEST_SOURCES)
  message(FATAL_ERROR "No *_test.cpp files found under ${ENGINE_SRC}")
endif()

add_executable(zandrox_tests ${TEST_SOURCES})
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
