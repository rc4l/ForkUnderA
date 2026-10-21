# [rc4l] fuactl's stack dump helper: built on first use by `fuactl stacks` (src/native.mjs), Windows only.
cmake_minimum_required(VERSION 3.16)
project(stackdump CXX)
add_executable(stackdump stackdump.cpp)
target_link_libraries(stackdump PRIVATE dbghelp)
