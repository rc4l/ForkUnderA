project( ServerRegistry )

include( CheckFunctionExists )
include( CheckCXXCompilerFlag )

# Use the highest C++ standard available since VS2015 compiles with C++14
# but we only require C++11.  The recommended way to do this in CMake is to
# probably to use target_compile_features, but I don't feel like maintaining
# a list of features we use.
CHECK_CXX_COMPILER_FLAG( "-std=c++14" CAN_DO_CPP14 )
if ( CAN_DO_CPP14 )
	set ( CMAKE_CXX_FLAGS "-std=c++14 ${CMAKE_CXX_FLAGS}" )
else ()
	CHECK_CXX_COMPILER_FLAG( "-std=c++1y" CAN_DO_CPP1Y )
	if ( CAN_DO_CPP1Y )
		set ( CMAKE_CXX_FLAGS "-std=c++1y ${CMAKE_CXX_FLAGS}" )
	else ()
		CHECK_CXX_COMPILER_FLAG( "-std=c++11" CAN_DO_CPP11 )
		if ( CAN_DO_CPP11 )
			set ( CMAKE_CXX_FLAGS "-std=c++11 ${CMAKE_CXX_FLAGS}" )
		else ()
			CHECK_CXX_COMPILER_FLAG( "-std=c++0x" CAN_DO_CPP0X )
			if ( CAN_DO_CPP0X )
				set ( CMAKE_CXX_FLAGS "-std=c++0x ${CMAKE_CXX_FLAGS}" )
			endif ()
		endif ()
	endif ()
endif ()

set( ZAN_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../src )
include_directories( ${ZAN_DIR} )
include_directories( ${CMAKE_CURRENT_SOURCE_DIR} )

CHECK_FUNCTION_EXISTS( strnicmp STRNICMP_EXISTS )
if( NOT STRNICMP_EXISTS )
   add_definitions( -Dstrnicmp=strncasecmp )
endif( NOT STRNICMP_EXISTS )

add_executable( forkundera-server-registry
	main.cpp
	network.cpp
	${ZAN_DIR}/gitinfo.cpp
	# [rc4l] gitinfo.cpp's GetFuaVersionTag() calls zx::FuaVersionTag, which lives here. The engine
	# target picks it up through its own source list; this daemon compiles gitinfo.cpp on its own and
	# so has to link the definition explicitly or the build fails at link time, on Linux and macOS,
	# in a target a Windows-only `zdoom` build never touches.
	${ZAN_DIR}/features/fua-branding/computation/fua_version_compute.cpp
	# [rc4l] The punch broker's decision half, shared with the engine so both ends agree about
	# who may be introduced to whom.
	${ZAN_DIR}/features/server-hosting/computation/punchbroker_compute.cpp
	# [rc4l] Who may hold a reach cookie, and how many. Registry-only, since the registry is the one
	# handing them out.
	${ZAN_DIR}/features/federated-server-registry/computation/reachcookie_compute.cpp
	# [rc4l] The dual-stack grouping rule, which only the registry applies.
	${ZAN_DIR}/features/federated-server-registry/computation/servergroup_compute.cpp
	# [rc4l] Reading the optional tail of an announce, where "absent" means the old behaviour.
	${ZAN_DIR}/features/federated-server-registry/computation/announcefields_compute.cpp
	# [rc4l] Whether presenting a cookie destroys it, which differs by what it is being presented FOR.
	${ZAN_DIR}/features/federated-server-registry/computation/cookieclaim_compute.cpp
	# [rc4l] networkshared.cpp unmaps ::ffff:a.b.c.d through this, so the registry needs it too.
	${ZAN_DIR}/features/net/computation/v6mapped_compute.cpp
	# [rc4l] And matches v6 bans through this one. The registry reads the same ban files and answers
	# the same isIPInList question, so a v6 rule has to mean the same thing on both sides of the wire.
	${ZAN_DIR}/features/net/computation/v6prefix_compute.cpp
	${ZAN_DIR}/networkshared.cpp
	${ZAN_DIR}/platform.cpp
	${ZAN_DIR}/huffman/bitreader.cpp 
	${ZAN_DIR}/huffman/bitwriter.cpp 
	${ZAN_DIR}/huffman/huffcodec.cpp 
	${ZAN_DIR}/huffman/huffman.cpp
)

# [rc4l] revision_check lives in the parent (engine) project and regenerates src/gitinfo.h. When this
# directory is configured on its own -- which the container image does, to avoid pulling in SDL/GL/
# OpenAL just to build a UDP daemon -- that target does not exist and a standalone build provides
# gitinfo.h itself (see Dockerfile.registry).
#
# The test is "am I the top-level project", NOT if( TARGET revision_check ): the parent adds this
# directory BEFORE src/, so at configure time that target does not exist yet even in the engine
# build, and the guard would silently drop a dependency the engine needs. add_dependencies resolves
# at generate time, so the forward reference below is fine.
if( NOT CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR )
	add_dependencies( forkundera-server-registry revision_check )
endif()

if( WIN32 )
	target_link_libraries( forkundera-server-registry ws2_32 winmm )
endif( WIN32 )
