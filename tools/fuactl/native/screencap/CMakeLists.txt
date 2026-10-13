# [rc4l] fuactl's screen capture helper: built on first use by `fuactl screen` (src/screen.mjs), Windows only.
cmake_minimum_required(VERSION 3.16)
project(screencap CXX)
add_executable(screencap screencap.cpp)
target_link_libraries(screencap PRIVATE d3d11 dxgi user32)
