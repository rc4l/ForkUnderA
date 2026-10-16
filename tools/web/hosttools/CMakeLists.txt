# [rc4l] The engine's build-time tools alone (lemon, re2c, zipdir, updaterevision), built for the host, so the browser build can import them without a whole native engine build.
cmake_minimum_required(VERSION 3.13)
project(fua_hosttools C CXX)

get_filename_component(ZA "${CMAKE_CURRENT_SOURCE_DIR}/../../../src/zandronum" ABSOLUTE)

include("${ZA}/ReleaseOnly.cmake")   # [rc4l] make_release_only, which the in-tree libraries call

# [rc4l] zipdir's compressors, the in-tree copies the engine itself builds.
add_subdirectory("${ZA}/zlib" zlib)
add_subdirectory("${ZA}/bzip2" bzip2)
add_subdirectory("${ZA}/lzma" lzma)
set(ZLIB_INCLUDE_DIR "${ZA}/zlib")
set(ZLIB_LIBRARIES z)
set(BZIP2_INCLUDE_DIR "${ZA}/bzip2")
set(BZIP2_LIBRARIES bz2)
set(LZMA_INCLUDE_DIR "${ZA}/lzma/C")

add_subdirectory("${ZA}/tools" tools)
export(TARGETS ${CROSS_EXPORTS} FILE "${CMAKE_BINARY_DIR}/ImportExecutables.cmake")
