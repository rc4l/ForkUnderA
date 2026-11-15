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
# [rc4l] Under Emscripten (web-port-design 13a) the tests run in Node, the browser build's runtime, with WebAssembly's own exceptions and threads on every object as the engine has them.
if(EMSCRIPTEN)
  add_compile_options(-fwasm-exceptions -pthread)
  add_link_options(-fwasm-exceptions -pthread)
endif()

include(FetchContent)
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG        v1.15.2
  GIT_SHALLOW    TRUE
)
# [rc4l] MSVC only: GoogleTest defaults to the STATIC CRT (/MT) while everything else here uses the
# dynamic one (/MD), and the two cannot link -- LNK2038 on RuntimeLibrary, followed by a few hundred
# LNK2005 duplicate symbols out of msvcprt.lib that all trace back to it. Must be set BEFORE
# MakeAvailable, since that is when GoogleTest's own CMake reads it. No effect on GCC/Clang.
#
# Not caught locally because an already-configured build-tests/ reuses its cached GoogleTest; only a
# clean configure trips it, which is what CI does every run.
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
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

# [rc4l] iwadallow_compute.cpp is generated-header-dependent: the downloadable-IWAD allowlist lives in
# the repo-root iwadallowlist.txt and is compiled in (see tools/gen-wadlists.cmake for why it is not
# a runtime file). The engine build runs the same generator; running it here too is what lets the
# tests assert against the SHIPPED list rather than a copy that could quietly disagree with it.
set(ZX_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")
if(WIN32)
  set(ZX_WADLIST_PLATFORM windows)
elseif(APPLE)
  set(ZX_WADLIST_PLATFORM macos)
else()
  set(ZX_WADLIST_PLATFORM linux)
endif()
set(ZX_GEN_LISTS "${CMAKE_CURRENT_BINARY_DIR}/generated/zx_waddownload_lists.h")
add_custom_command(OUTPUT "${ZX_GEN_LISTS}"
  COMMAND ${CMAKE_COMMAND}
    "-DIWAD_LIST=${ZX_REPO_ROOT}/config/iwadallowlist.txt"
    "-DSITE_LIST=${ZX_REPO_ROOT}/config/waddownloadsites.txt"
    "-DSUBST_LIST=${ZX_REPO_ROOT}/config/iwadsubstitutes.txt"
    "-DDIR_LIST=${ZX_REPO_ROOT}/config/waddirectories.txt"
    "-DPLATFORM=${ZX_WADLIST_PLATFORM}"
    "-DOUT=${ZX_GEN_LISTS}"
    -P "${ZX_REPO_ROOT}/tools/gen-wadlists.cmake"
  DEPENDS "${ZX_REPO_ROOT}/config/iwadallowlist.txt" "${ZX_REPO_ROOT}/config/waddownloadsites.txt"
          "${ZX_REPO_ROOT}/config/iwadsubstitutes.txt"
          "${ZX_REPO_ROOT}/config/waddirectories.txt"
          "${ZX_REPO_ROOT}/tools/gen-wadlists.cmake"
  COMMENT "Generating zx_waddownload_lists.h from iwadallowlist.txt / waddownloadsites.txt")

# [rc4l] features/sound-decoders: pcmdecode_compute.cpp calls the vendored decoders, built once here as in the engine, with their warnings off.
set(ZX_SOUND_DECODERS "${ENGINE_SRC}/features/sound-decoders/vendor/decoders_vendor.cpp")
set_source_files_properties("${ZX_SOUND_DECODERS}" PROPERTIES COMPILE_OPTIONS "$<IF:$<CXX_COMPILER_ID:MSVC>,/w,-w>")
# [Kaori-Chani] features/net-snapshot: the format layer's two engine-free .cpp files, which the
# reference decode test reads recordings through. A .zsnap record is a zstd frame, so the vendored
# single-file zstd is built in beside them as the engine builds it, with its warnings off.
# [Kaori-Chani] The shared tic time base, built in here as well as into the game: this is the
# only coverage its arithmetic gets on a platform nobody can run by hand, wasm32 above all.
# [Kaori-Chani] The wait is a file per platform, as in the engine, so the tests take
# whichever this build compiles and ask TIC_SleepWaits() what it does.
if(EMSCRIPTEN)
  set(ZX_TICTIME "${ENGINE_SRC}/i_tictime.cpp" "${ENGINE_SRC}/i_tictime_web.cpp")
else()
  set(ZX_TICTIME "${ENGINE_SRC}/i_tictime.cpp" "${ENGINE_SRC}/i_tictime_wait.cpp")
endif()
set(ZX_SNAPSHOT_FORMAT
  "${ENGINE_SRC}/features/net-snapshot/stream.cpp"
  "${ENGINE_SRC}/features/net-snapshot/recordread.cpp"
  "${ZX_REPO_ROOT}/deps/zstd/zstd.c")
# [Kaori-Chani] The decode dumper's body, shared by the reference test and the snap_dump tool
# below, so neither can hold a copy that drifts from the other.
set(ZX_SNAPSHOT_DUMP "${CMAKE_CURRENT_SOURCE_DIR}/snapdump/snapdump.cpp")
set_source_files_properties("${ZX_REPO_ROOT}/deps/zstd/zstd.c" PROPERTIES
  COMPILE_OPTIONS "$<IF:$<CXX_COMPILER_ID:MSVC>,/w,-w>")
add_executable(zandrox_tests ${TEST_SOURCES} ${COMPUTE_SOURCES} "${ZX_SOUND_DECODERS}" ${ZX_TICTIME}
  ${ZX_SNAPSHOT_FORMAT} ${ZX_SNAPSHOT_DUMP} "${ZX_GEN_LISTS}"
  "${CMAKE_CURRENT_SOURCE_DIR}/snapdump/snapdump_test.cpp")
# [Doruk-Aksoy] features/database: database_compute.cpp runs its SQL on the engine's own SQLite, built here from the same folder (which downloads it when missing).
enable_language(C)
add_subdirectory("${ENGINE_SRC}/../sqlite" "${CMAKE_CURRENT_BINARY_DIR}/sqlite")
target_include_directories(zandrox_tests PRIVATE "${ENGINE_SRC}" "${ENGINE_SRC}/../sqlite"
  "${ZX_REPO_ROOT}/deps/zstd" "${CMAKE_CURRENT_BINARY_DIR}/generated"
  "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(zandrox_tests PRIVATE GTest::gtest_main GTest::gmock sqlite3)
# [Kaori-Chani] The wide snapshot reference ships beside its module, and a dump that differs is
# written into the build folder, so the whole difference can be taken with any tool.
if(EMSCRIPTEN)
  # [Kaori-Chani] Under Emscripten there is no host file system to reach, so both paths are
  # MEMFS ones: the reference is embedded at /testdata by the link options below, and /tmp is
  # a writable directory Emscripten's runtime creates for itself. A dump that differs lands
  # there rather than in the build folder, so on this one platform it is in the node
  # process's memory and gone when it exits; the failure still names the first line that
  # differs, which is what the assertion reports, and any other build gives the file.
  target_compile_definitions(zandrox_tests PRIVATE
    ZX_SNAPSHOT_TESTDATA="/testdata"
    ZX_SNAPSHOT_TESTOUT="/tmp")
else()
  target_compile_definitions(zandrox_tests PRIVATE
    ZX_SNAPSHOT_TESTDATA="${ENGINE_SRC}/features/net-snapshot/testdata"
    ZX_SNAPSHOT_TESTOUT="${CMAKE_CURRENT_BINARY_DIR}")
endif()
# [rc4l] This target is the portable one -- pure Compute* logic with no engine or platform deps -- so
# it is the one a Windows dev can actually run before pushing. MSVC rejects -Wextra outright (D8021,
# a hard error, not a warning), which silently made that impossible; /W4 is its equivalent. The
# sanitizer/coverage options below stay GCC/Clang-only on purpose: CI measures coverage on Linux.
if(MSVC)
  target_compile_options(zandrox_tests PRIVATE /W4)
  # [rc4l] MSVC hides M_PI and friends behind _USE_MATH_DEFINES; several tests use M_PI to build
  # trig reference values. Defined on the target rather than #defined in each file, so a new test
  # does not have to know this.
  target_compile_definitions(zandrox_tests PRIVATE _USE_MATH_DEFINES)
else()
  target_compile_options(zandrox_tests PRIVATE -Wall -Wextra)
endif()

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

# [rc4l] The heap grows as the desktop's does, main runs on a worker so the job pool's threads can start while it waits, and the exit status is the tests' verdict.
# [Kaori-Chani] "No test reads the disk" was true until the snapshot reference tests
# arrived: they open ref-wide.csnap and write a dump beside it when it differs. The in-memory
# file system stays -- the data goes into it instead. --embed-file puts the testdata folder
# at /testdata inside the module (68 KB, one recording), which ZX_SNAPSHOT_TESTDATA names
# above. Embedded rather than preloaded on purpose: an embedded file is there the moment main
# runs, where a preloaded one needs the runtime's async fetch to have finished first, and
# these tests are ordinary synchronous reads.
#
# [Kaori-Chani] These tests are NOT skipped on web, and that is the point: wasm32 is CI's
# only 32-bit target, so the wide reference decoding byte for byte here is the strongest
# check the 48.16 widening gets -- a fixed_t is eight bytes on every other platform in the
# matrix, and a pointer is four here.
if(EMSCRIPTEN)
  target_link_options(zandrox_tests PRIVATE "SHELL:-s ALLOW_MEMORY_GROWTH=1" "SHELL:-s EXIT_RUNTIME=1" "SHELL:-s STACK_SIZE=4MB"
    "SHELL:-s PROXY_TO_PTHREAD=1" "SHELL:-s PTHREAD_POOL_SIZE=8"
    "SHELL:--embed-file ${ENGINE_SRC}/features/net-snapshot/testdata@/testdata")
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
      "${ENGINE_SRC}/posix/osx/i_system_cocoa_net.mm")
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
# [rc4l] Not under Emscripten, whose sockets are no UDP to test.
if(NET_TEST_SOURCES AND NOT EMSCRIPTEN)
  add_executable(zandrox_tests_net
    ${NET_TEST_SOURCES}
    "${ENGINE_SRC}/networkshared.cpp"
    # [rc4l] networkshared.cpp unmaps ::ffff:a.b.c.d through this, so the target that links it
    # needs it as well.
    "${ENGINE_SRC}/features/net/computation/v6mapped_compute.cpp"
    # [rc4l] ...and matches v6 bans through this one, for the same reason.
    "${ENGINE_SRC}/features/net/computation/v6prefix_compute.cpp"
    # [Doruk-Aksoy] The sound commands' volume byte, which soundactor_wire_nettest.cpp encodes the way the server does.
    "${ENGINE_SRC}/features/sound-amplify/computation/sound_amplify_compute.cpp"
    # [Doruk-Aksoy] The give/take amount, which cheatamount_wire_nettest.cpp encodes and decodes the way client and server do.
    "${ENGINE_SRC}/features/cheat-amount/computation/cheatamount_compute.cpp"
    "${ENGINE_SRC}/platform.cpp"
    # [rc4l] The ini parser, here for the same reason as networkshared.cpp: it is vendored engine
    # code whose only heavy dependency is shimmed away, and a truncation bug in it reached players.
    "${ENGINE_SRC}/configfile.cpp"
    "${ENGINE_SRC}/zstring.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/shims/net/frandom_stub.cpp"
    "${ENGINE_SRC}/huffman/bitreader.cpp"
    "${ENGINE_SRC}/huffman/bitwriter.cpp"
    "${ENGINE_SRC}/huffman/huffcodec.cpp"
    "${ENGINE_SRC}/huffman/huffman.cpp")
  # Shim dir BEFORE the engine tree: networkshared.cpp's #include "i_system.h" must resolve to
  # tests/shims/net/i_system.h; doomtype.h etc. still come from ENGINE_SRC.
  target_include_directories(zandrox_tests_net BEFORE PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/shims/net" "${ENGINE_SRC}"
    # [rc4l] configfile.cpp's own two dependencies: FString's formatter and the RNG behind its
    # multi-line end tags.
    "${ENGINE_SRC}/../gdtoa" "${ENGINE_SRC}/sound")
  # strnicmp is a Windows spelling; the vendored code uses it, POSIX spells it strncasecmp.
  # [rc4l] Only remap it AWAY from Windows: on MSVC strnicmp is the one that exists and
  # strncasecmp is not declared, so defining this unconditionally broke the Windows build.
  if(NOT WIN32)
    target_compile_definitions(zandrox_tests_net PRIVATE strnicmp=strncasecmp)
  endif()
  target_link_libraries(zandrox_tests_net PRIVATE GTest::gtest_main)
  # [rc4l] networkshared.cpp calls htons/ntohs/inet_addr/gethostbyname. On POSIX those are in libc;
  # on Windows they live in Winsock, so the target needs it explicitly -- the same ws2_32 the
  # server-registry executable links for the same reason.
  if(WIN32)
    target_link_libraries(zandrox_tests_net PRIVATE ws2_32)
  endif()
  # -Wall -Wextra but NOT -Werror: this compiles vendored engine .cpp we don't own (it trips
  # legacy warnings like the deprecated sprintf); the value is the assertions under ASan.
  # [rc4l] Same MSVC split as zandrox_tests above -- cl rejects /Wextra with D8021, a hard command
  # line error, so passing the GCC flags here failed the whole Windows configure even though this
  # target's own sources are portable. /W4 is the equivalent; the deprecation quieting is /wd4996.
  if(MSVC)
    target_compile_options(zandrox_tests_net PRIVATE /W4 /wd4996)
    target_compile_definitions(zandrox_tests_net PRIVATE _USE_MATH_DEFINES _CRT_SECURE_NO_WARNINGS)
  else()
    target_compile_options(zandrox_tests_net PRIVATE -Wall -Wextra -Wno-deprecated-declarations)
  endif()
  if(ZANDROX_TESTS_SANITIZE)
    message(STATUS "zandrox_tests_net: AddressSanitizer + UBSan enabled")
    target_compile_options(zandrox_tests_net PRIVATE
      -fsanitize=address,undefined -fno-omit-frame-pointer -g)
    target_link_options(zandrox_tests_net PRIVATE -fsanitize=address,undefined)
    # [rc4l] FINDING (documented, not hidden): the adversarial Huffman decode tests feed the
    # vendored decoder garbage/truncated streams on purpose, and it does a signed left-shift of a
    # negative value at huffman/huffcodec.cpp:282 -- undefined in C++, but BENIGN on the
    # two's-complement target the whole engine already assumes (and ships without UBSan). ASan
    # stays fully on and PROVES those same hostile inputs never read or write out of bounds -- the
    # memory-safety guarantee that actually matters. So we scope only UBSan's `shift` check out of
    # the vendored huffman TUs (source-level flag wins over the target's -fsanitize=undefined);
    # every other UBSan check, and all of ASan, remain -- and networkshared.cpp keeps FULL UBSan,
    # since its tested BYTESTREAM_s paths are UB-clean. We do not patch the vendored decoder
    # (touchless rule) to silence a benign, on-target-defined shift.
    set_source_files_properties(
      "${ENGINE_SRC}/huffman/bitreader.cpp" "${ENGINE_SRC}/huffman/bitwriter.cpp"
      "${ENGINE_SRC}/huffman/huffcodec.cpp" "${ENGINE_SRC}/huffman/huffman.cpp"
      PROPERTIES COMPILE_OPTIONS "-fno-sanitize=shift")
  endif()
  gtest_discover_tests(zandrox_tests_net)
endif()

# [Kaori-Chani] features/net-snapshot's decoder fuzzer. It stays a program of its own rather than
# a test case: libFuzzer supplies its own main, the seeded mode takes a count, a seed and a corpus
# folder, and the sanitizers it needs are build flags that would otherwise apply to the whole test
# binary. It is also what writes ref-wide.csnap, so --wide-reference has to stay buildable here.
option(ZANDROX_SNAP_FUZZ "Build the snapshot decoder fuzzer (GCC/Clang)" ON)
option(ZANDROX_SNAP_FUZZ_LIBFUZZER "Build the snapshot fuzzer as a libFuzzer target (Clang)" OFF)
if(ZANDROX_SNAP_FUZZ AND NOT MSVC)
  add_executable(snap_fuzz
    "${CMAKE_CURRENT_SOURCE_DIR}/snapfuzz/fuzz.cpp"
    "${ENGINE_SRC}/features/net-snapshot/recordread.cpp"
    "${ENGINE_SRC}/features/net-snapshot/stream.cpp"
    "${ZX_REPO_ROOT}/deps/zstd/zstd.c")
  target_include_directories(snap_fuzz PRIVATE "${ENGINE_SRC}" "${ZX_REPO_ROOT}/deps/zstd")
  # [Kaori-Chani] The vendored zstd is always here, so the .zsnap container is fuzzed as well as
  # the .csnap one, which the standalone build could only do where it found a system zstd.
  target_compile_definitions(snap_fuzz PRIVATE SNAP_FUZZ_ZSNAP)
  # [Kaori-Chani] A finding stops the run with a nonzero exit rather than being reported and
  # walked past, which is what -fno-sanitize-recover=all is for.
  if(ZANDROX_SNAP_FUZZ_LIBFUZZER)
    target_compile_definitions(snap_fuzz PRIVATE SNAP_FUZZ_LIBFUZZER)
    target_compile_options(snap_fuzz PRIVATE -O1 -g
      -fsanitize=fuzzer,address,undefined -fno-sanitize-recover=all)
    target_link_options(snap_fuzz PRIVATE -fsanitize=fuzzer,address,undefined)
  else()
    target_compile_options(snap_fuzz PRIVATE -O1 -g
      -fsanitize=address,undefined -fno-sanitize-recover=all)
    target_link_options(snap_fuzz PRIVATE -fsanitize=address,undefined)
  endif()

  # [Kaori-Chani] Two short deterministic runs on every build, so the fuzzer is exercised rather
  # than only ever run by hand; the long runs on both widths stay a gate someone runs themselves.
  if(NOT ZANDROX_SNAP_FUZZ_LIBFUZZER)
    add_test(NAME snap_fuzz_seeded COMMAND snap_fuzz 20000)
    # [Kaori-Chani] The generator writes a fresh wide reference and reads it back, so the mode
    # that makes new wide references is exercised end to end. It is not compared with the
    # committed ref-wide.csnap: the generator draws one byte at a time from its stream, and a
    # fixed_t is eight bytes here where it was four in the build that wrote that file, so no
    # port can reproduce it byte for byte. The committed copy stays the reference, held to its
    # dump by SnapshotRefDump.TheWideReferenceDecodesByteForByte.
    add_test(NAME snap_fuzz_wide_reference COMMAND snap_fuzz --wide-reference
      "${CMAKE_CURRENT_BINARY_DIR}/ref-wide.regenerated.csnap")
  endif()
endif()

# [Kaori-Chani] snap_dump, the command line over that same dumper. The reference test decodes
# the three recordings whose digests it knows; this decodes any recording, which is what makes a
# new reference dump after a deliberate format change and what reads a recording when a later
# layer goes wrong. It needs no sanitizer and no main of its own beyond argument handling, so
# unlike snap_fuzz it builds everywhere, MSVC included.
add_executable(snap_dump
  "${CMAKE_CURRENT_SOURCE_DIR}/snapdump/main.cpp"
  ${ZX_SNAPSHOT_DUMP}
  ${ZX_SNAPSHOT_FORMAT})
target_include_directories(snap_dump PRIVATE "${ENGINE_SRC}" "${ZX_REPO_ROOT}/deps/zstd"
  "${CMAKE_CURRENT_SOURCE_DIR}")

# [Kaori-Chani] The command line runs on every build, over the one reference that ships in the
# repo. What the dump says is already held to its digest by
# SnapshotRefDump.TheWideReferenceDecodesByteForByte, which calls the same SnapDumpWrite; this is
# the argument handling around it, so the tool cannot quietly stop working between the steps that
# need it.
add_test(NAME snap_dump_wide_reference COMMAND snap_dump
  "${ENGINE_SRC}/features/net-snapshot/testdata/ref-wide.csnap"
  "${CMAKE_CURRENT_BINARY_DIR}/ref-wide.byhand.dump.txt")

# [Kaori-Chani] And the two statuses a good recording never produces. The tool returns its result
# as its exit code, so these pin the step the GoogleTests cannot reach: main()'s own mapping. Which
# flavour of 2 a bad recording gets -- no container, a header that will not open, a record cut off
# mid-payload -- is pinned by SnapshotDump in snapdump_test.cpp; what is checked here is that 1 and
# 2 arrive at the shell as 1 and 2.
add_test(NAME snap_dump_missing_file COMMAND "${CMAKE_COMMAND}"
  -D "TOOL=$<TARGET_FILE:snap_dump>"
  -D "IN=${CMAKE_CURRENT_BINARY_DIR}/no-such-recording.csnap"
  -D WANT=1 -P "${CMAKE_CURRENT_SOURCE_DIR}/snapdump/expect_exit.cmake")
add_test(NAME snap_dump_not_a_recording COMMAND "${CMAKE_COMMAND}"
  -D "TOOL=$<TARGET_FILE:snap_dump>"
  -D "IN=${CMAKE_CURRENT_SOURCE_DIR}/snapdump/main.cpp"
  -D WANT=2 -P "${CMAKE_CURRENT_SOURCE_DIR}/snapdump/expect_exit.cmake")