# [rc4l] Diligent's WebGPU backend built for the browser on its own: a page drawing the engine's GLSL triangle, and a Node tool converting dumped shaders to WGSL.
cmake_minimum_required(VERSION 3.20)
project(fua_webgpu_check C CXX)

if(NOT EMSCRIPTEN)
	message(FATAL_ERROR "Configure with emcmake: see tools/web/README.md")
endif()

get_filename_component(FUA_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)

set(DILIGENT_BUILD_TESTS         OFF CACHE BOOL "" FORCE)
set(DILIGENT_BUILD_SAMPLES       OFF CACHE BOOL "" FORCE)
set(DILIGENT_NO_OPENGL           ON  CACHE BOOL "" FORCE)
set(DILIGENT_NO_ARCHIVER         ON  CACHE BOOL "" FORCE)
set(DILIGENT_NO_SUPER_RESOLUTION ON  CACHE BOOL "" FORCE)
set(DILIGENT_NO_HLSL             ON  CACHE BOOL "" FORCE)
add_subdirectory("${FUA_ROOT}/deps/DiligentCore" "${CMAKE_BINARY_DIR}/DiligentCore")

add_executable(triangle triangle.cpp)
target_link_libraries(triangle PRIVATE Diligent-GraphicsEngineWebGPU-static Diligent-Common Diligent-BuildSettings)
set_target_properties(triangle PROPERTIES SUFFIX ".html" LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/shell.html")
target_link_options(triangle PRIVATE
	"SHELL: -s ALLOW_MEMORY_GROWTH=1"
	"SHELL: -s STACK_SIZE=5MB"
	"SHELL: -s EXPORTED_RUNTIME_METHODS=callMain"
	"SHELL: --shell-file ${CMAKE_CURRENT_SOURCE_DIR}/shell.html")

add_executable(shadercheck shadercheck.cpp)
target_link_libraries(shadercheck PRIVATE Diligent-ShaderTools Diligent-Common Diligent-BuildSettings)
target_link_options(shadercheck PRIVATE
	"SHELL: -s ALLOW_MEMORY_GROWTH=1"
	"SHELL: -s STACK_SIZE=8MB"
	"SHELL: -s NODERAWFS=1"
	"SHELL: -s ENVIRONMENT=node,worker"
	"SHELL: -s EXIT_RUNTIME=1"
	"-pthread")
