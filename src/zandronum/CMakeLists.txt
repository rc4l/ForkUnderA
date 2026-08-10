# [SB] Set the minimum version to 3.5, since it's the lowest CMake 4.0 will accept.
cmake_minimum_required( VERSION 3.5 )
project(Zandronum)

if( COMMAND cmake_policy )
	cmake_policy( SET CMP0011 NEW )
	cmake_policy( SET CMP0054 NEW )
endif( COMMAND cmake_policy )

list( APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR} )
include( FindPackageHandleStandardArgs )

# Generator expression are available some time in CMake 2.8. Due to
# cmake_minimum_required, we can assume a minor version of > 7 implies major >= 2
if(${CMAKE_MAJOR_VERSION} GREATER 2 OR ${CMAKE_MINOR_VERSION} GREATER 7)
	option( NO_GENERATOR_EXPRESSIONS "Disable generator expressions (for building pk3s with IDEs)." OFF )
else(${CMAKE_MAJOR_VERSION} GREATER 2 OR ${CMAKE_MINOR_VERSION} GREATER 7)
	set( NO_GENERATOR_EXPRESSIONS ON )
endif(${CMAKE_MAJOR_VERSION} GREATER 2 OR ${CMAKE_MINOR_VERSION} GREATER 7)

# Support cross compiling
option( FORCE_CROSSCOMPILE "Turn on cross compiling." NO )
if( FORCE_CROSSCOMPILE )
	set( CMAKE_CROSSCOMPILING TRUE )
endif( FORCE_CROSSCOMPILE )

if(CMAKE_CROSSCOMPILING)
	set(IMPORT_EXECUTABLES "IMPORTFILE-NOTFOUND" CACHE FILEPATH "Export file from native build.")
	include(${IMPORT_EXECUTABLES})
endif(CMAKE_CROSSCOMPILING)

# [rc4l] The engine's own data pk3 carries a build key in its name: fua_core_<key>.pk3.
#
# A fixed zandronum.pk3 loads happily when it is stale, and the engine then misbehaves somewhere far
# from the cause. Keyed, a mismatched pair simply is not found and the engine says so at startup.
# That is the point of this: turning a silent wrong-data bug into a loud one.
#
# COMPUTED HERE AND NOWHERE ELSE. The exe is told the same string through FUA_CORE_PK3_NAME below,
# so the name it looks for and the name zipdir writes cannot drift apart within a build tree. A glob
# would reintroduce exactly the ambiguity this removes once a player has several cores in one folder.
# FUA_VERSION is the fork's own release tag, the same string GetFuaVersionTag() reports. Asking git
# is right even during a release build, where the tag does not exist yet: _build.yml already runs
# `git tag -f "<version>" HEAD` on all three platforms before compiling, so describe answers with the
# release being cut. The override is for builds with no git at all.
if( NOT FUA_VERSION )
	find_package( Git QUIET )
	if( GIT_FOUND )
		execute_process( COMMAND ${GIT_EXECUTABLE} describe --tags --abbrev=0
			WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
			OUTPUT_VARIABLE FUA_VERSION
			OUTPUT_STRIP_TRAILING_WHITESPACE
			ERROR_QUIET )
	endif()
endif()
if( NOT FUA_VERSION )
	set( FUA_VERSION "dev" )
endif()

# Anything a filename cannot carry becomes a dash rather than being dropped, so two different
# versions can never collapse onto one name.
string( REGEX REPLACE "[^A-Za-z0-9._-]" "-" FUA_CORE_KEY "${FUA_VERSION}" )

set( FUA_CORE_PK3_NAME "fua_core_${FUA_CORE_KEY}.pk3" )
message( STATUS "Engine data pk3: ${FUA_CORE_PK3_NAME}" )

# The exe is told the same string, so BASEWAD and the file zipdir writes come from one place.
add_definitions( -DFUA_CORE_PK3_NAME="${FUA_CORE_PK3_NAME}" )

# [rc4l] GAMEVER_STRING is the version clients and servers hand each other and refuse to mix across
# (sv_main.cpp compares it with stricmp), so it has to be OUR release rather than the Zandronum
# release we forked from. Zandronum sets it to its own release for exactly this reason; leaving it
# at 3.2.1 said every build of ours was interchangeable with every other, which was never true.
string( REGEX REPLACE "^v" "" FUA_GAMEVER "${FUA_VERSION}" )
add_definitions( -DFUA_GAMEVER_STRING="${FUA_GAMEVER}" )

# Simplify pk3 building, add_pk3(filename srcdirectory)
function( add_pk3 PK3_NAME PK3_DIR )
	# Generate target name. Just use "pk3" for main pk3 target.
	string( REPLACE "." "_" PK3_TARGET ${PK3_NAME} )
	# [BB] Replaced 'zdoom' with 'zandronum'
	if( ${PK3_TARGET} STREQUAL "zandronum_pk3" )
		set( PK3_TARGET "pk3" )
	endif( ${PK3_TARGET} STREQUAL "zandronum_pk3" )
	# [rc4l] The core pk3's FILENAME now carries a build key, so deriving the target name from it
	# would rename the target on every rebuild and break everything that depends on "pk3".
	if( ${PK3_NAME} STREQUAL ${FUA_CORE_PK3_NAME} )
		set( PK3_TARGET "pk3" )
	endif()

	if( NOT NO_GENERATOR_EXPRESSIONS AND NOT ZDOOM_OUTPUT_OLDSTYLE )
		add_custom_command( OUTPUT ${ZDOOM_OUTPUT_DIR}/${PK3_NAME}
			COMMAND zipdir -udf ${ZDOOM_OUTPUT_DIR}/${PK3_NAME} ${PK3_DIR}
			COMMAND ${CMAKE_COMMAND} -E copy_if_different ${ZDOOM_OUTPUT_DIR}/${PK3_NAME} $<TARGET_FILE_DIR:zdoom>/${PK3_NAME}
			DEPENDS zipdir )
	else( NOT NO_GENERATOR_EXPRESSIONS AND NOT ZDOOM_OUTPUT_OLDSTYLE )
		add_custom_command( OUTPUT ${ZDOOM_OUTPUT_DIR}/${PK3_NAME}
			COMMAND zipdir -udf ${ZDOOM_OUTPUT_DIR}/${PK3_NAME} ${PK3_DIR}
			DEPENDS zipdir )
	endif( NOT NO_GENERATOR_EXPRESSIONS AND NOT ZDOOM_OUTPUT_OLDSTYLE )

	if( NOT NO_GENERATOR_EXPRESSIONS )
		# Touch the zipdir executable here so that the pk3s are forced to
		# rebuild each time since their dependecy has "changed."
		add_custom_target( ${PK3_TARGET} ALL
			COMMAND ${CMAKE_COMMAND} -E touch $<TARGET_FILE:zipdir>
			DEPENDS ${ZDOOM_OUTPUT_DIR}/${PK3_NAME} )
	endif( NOT NO_GENERATOR_EXPRESSIONS )
endfunction( add_pk3 )

# Macro for building libraries without debugging information
macro( make_release_only )
	set( CMAKE_C_FLAGS_MINSIZEREL ${CMAKE_C_FLAGS_RELEASE} )
	set( CMAKE_C_FLAGS_RELWITHDEBINFO ${CMAKE_C_FLAGS_RELEASE} )
	string( REPLACE "/MT " "/MTd " CMAKE_C_FLAGS_DEBUG ${CMAKE_C_FLAGS_RELEASE} )
	set( CMAKE_CXX_FLAGS_MINSIZEREL ${CMAKE_CXX_FLAGS_RELEASE} )
	set( CMAKE_CXX_FLAGS_RELWITHDEBINFO ${CMAKE_CXX_FLAGS_RELEASE} )
	string( REPLACE "/MT " "/MTd " CMAKE_CXX_FLAGS_DEBUG ${CMAKE_CXX_FLAGS_RELEASE} )
endmacro( make_release_only )

IF( NOT CMAKE_BUILD_TYPE )
	SET( CMAKE_BUILD_TYPE Debug CACHE STRING
		"Choose the type of build, options are: Debug Release RelWithDebInfo MinSizeRel."
		FORCE )
ENDIF( NOT CMAKE_BUILD_TYPE )

set( ZDOOM_OUTPUT_DIR ${CMAKE_BINARY_DIR} CACHE PATH "Directory where zdoom.pk3 and the executable will be created." )
set( ZDOOM_EXE_NAME "zandronum" CACHE FILEPATH "Name of the executable to create" )
if( MSVC )
	# Allow the user to use ZDOOM_OUTPUT_DIR as a single release point.
	# Use zdoom, zdoomd, zdoom64, and zdoomd64 for the binary names
	option( ZDOOM_OUTPUT_OLDSTYLE "Don't use Release/Debug directories." OFF )
else( MSVC )
	set( ZDOOM_OUTPUT_OLDSTYLE OFF )
endif( MSVC )

if( "${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU" OR "${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang" )
	set( PROFILE 0 CACHE BOOL "Enable profiling with gprof for Debug and RelWithDebInfo build types." )
endif( "${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU" OR "${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang" )

set(CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}")

# [EP/BB]
if( CMAKE_BUILD_TYPE MATCHES Release AND ( CMAKE_COMPILER_IS_GNUCXX OR MSVC ) AND NOT APPLE )
	option( RELEASE_WITH_DEBUG_FILE "Create the debug symbols file for the release build." ON )
endif( CMAKE_BUILD_TYPE MATCHES Release AND ( CMAKE_COMPILER_IS_GNUCXX OR MSVC ) AND NOT APPLE )

find_package( BZip2 )
find_package( JPEG )
find_package( ZLIB )
# GME
find_path( GME_INCLUDE_DIR gme/gme.h )
find_library( GME_LIBRARIES gme )
mark_as_advanced( GME_INCLUDE_DIR GME_LIBRARIES )
FIND_PACKAGE_HANDLE_STANDARD_ARGS( GME
	REQUIRED_VARS GME_LIBRARIES GME_INCLUDE_DIR
)

if( MSVC )
	# Eliminate unreferenced functions and data
	# Perform identical COMDAT folding
	set( REL_LINKER_FLAGS "/opt:ref /opt:icf /nodefaultlib:msvcrt /TSAWARE" )

	# String pooling
	# Function-level linking
	# Disable run-time type information
	# Set floating point model to fast or the GL render will suffer for it.
	set( ALL_C_FLAGS "/GF /Gy /GR- /fp:fast" )
	
	if( CMAKE_SIZEOF_VOID_P MATCHES "4")
		# SSE2 option (mostly to switch it off in VC2012 and later where it's the default
		option (ZDOOM_USE_SSE2	"Use SSE2 instruction set")
		if (ZDOOM_USE_SSE2)
			set( ALL_C_FLAGS "${ALL_C_FLAGS} /arch:SSE2")
		else (ZDOOM_USE_SSE2)
			if (MSVC_VERSION GREATER 1699)
				# On Visual C++ 2012 and later SSE2 is the default, so we need to switch it off explicitly
				set( ALL_C_FLAGS "${ALL_C_FLAGS} /arch:IA32")
			endif (MSVC_VERSION GREATER 1699)
		endif (ZDOOM_USE_SSE2)
	endif( CMAKE_SIZEOF_VOID_P MATCHES "4")
	
	# Avoid CRT DLL dependancies in release builds, optionally generate assembly output for checking crash locations.
	option( ZDOOM_GENERATE_ASM "Generate assembly output." OFF )
	if( ZDOOM_GENERATE_ASM )
		set( REL_C_FLAGS "/MT /Oy /Oi /FAcs" )
	else( ZDOOM_GENERATE_ASM )
		set( REL_C_FLAGS "/MT /Oy /Oi" )
	endif( ZDOOM_GENERATE_ASM )

	
	# Debug allocations in debug builds
	set( DEB_C_FLAGS "/D _CRTDBG_MAP_ALLOC /MTd" )

	# Disable warnings for unsecure CRT functions from VC8+
	if( MSVC_VERSION GREATER 1399 )
		set( ALL_C_FLAGS "${ALL_C_FLAGS} /wd4996" )
	endif( MSVC_VERSION GREATER 1399 )
	
	# The CMake configurations set /GR and /MD by default, which conflict with our settings.
	string(REPLACE "/MD " " " CMAKE_CXX_FLAGS_RELEASE ${CMAKE_CXX_FLAGS_RELEASE} )
	string(REPLACE "/MD " " " CMAKE_CXX_FLAGS_MINSIZEREL ${CMAKE_CXX_FLAGS_MINSIZEREL} )
	string(REPLACE "/MD " " " CMAKE_CXX_FLAGS_RELWITHDEBINFO ${CMAKE_CXX_FLAGS_RELWITHDEBINFO} )
	string(REPLACE "/MDd " " " CMAKE_CXX_FLAGS_DEBUG ${CMAKE_CXX_FLAGS_DEBUG} )
	string(REPLACE "/MD " " " CMAKE_C_FLAGS_RELEASE ${CMAKE_C_FLAGS_RELEASE} )
	string(REPLACE "/MD " " " CMAKE_C_FLAGS_MINSIZEREL ${CMAKE_C_FLAGS_MINSIZEREL} )
	string(REPLACE "/MD " " " CMAKE_C_FLAGS_RELWITHDEBINFO ${CMAKE_C_FLAGS_RELWITHDEBINFO} )
	string(REPLACE "/MDd " " " CMAKE_C_FLAGS_DEBUG ${CMAKE_C_FLAGS_DEBUG} )
	string(REPLACE " /GR" " " CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS} )
else( MSVC )
	set( REL_LINKER_FLAGS "" )
	set( ALL_C_FLAGS "" )
	set( REL_C_FLAGS "" )
	set( DEB_C_FLAGS "" )
endif( MSVC )

set( CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} ${REL_LINKER_FLAGS}" )
set( CMAKE_EXE_LINKER_FLAGS_MINSIZEREL "${CMAKE_EXE_LINKER_FLAGS_MINSIZEREL} ${REL_LINKER_FLAGS}" )
set( CMAKE_EXE_LINKER_FLAGS_RELWITHDEBINFO "${CMAKE_EXE_LINKER_FLAGS_RELWITHDEBINFO} ${REL_LINKER_FLAGS}" )

set( CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${ALL_C_FLAGS}" )
set( CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} ${REL_C_FLAGS}" )
set( CMAKE_C_FLAGS_MINSIZEREL "${CMAKE_C_FLAGS_MINSIZEREL} ${REL_C_FLAGS}" )
set( CMAKE_C_FLAGS_RELWITHDEBINFO "${CMAKE_C_FLAGS_RELWITHDEBINFO} ${REL_C_FLAGS}" )
set( CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} ${DEB_C_FLAGS} -D_DEBUG" )

set( CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${ALL_C_FLAGS}" )
set( CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} ${REL_C_FLAGS}" )
set( CMAKE_CXX_FLAGS_MINSIZEREL "${CMAKE_CXX_FLAGS_MINSIZEREL} ${REL_C_FLAGS}" )
set( CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO} ${REL_C_FLAGS}" )
set( CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} ${DEB_C_FLAGS} -D_DEBUG" )

option(FORCE_INTERNAL_ZLIB "Use internal zlib")
option(FORCE_INTERNAL_JPEG "Use internal jpeg")
option(FORCE_INTERNAL_BZIP2 "Use internal bzip2")
option(FORCE_INTERNAL_GME "Use internal gme" ON)

# [rc4l] Matches UZDoom's option of the same name. brightmaps.pk3 is derived from
# commercial id/Raven/Rogue artwork, so a commercial standalone game must build with
# -DBUILD_NONFREE=OFF; see wadsrc_bm/static/license.md.
option( BUILD_NONFREE "Build non-free assets used by commercial games" ON )

if( ZLIB_FOUND AND NOT FORCE_INTERNAL_ZLIB )
	message( STATUS "Using system zlib" )
else( ZLIB_FOUND AND NOT FORCE_INTERNAL_ZLIB )
	message( STATUS "Using internal zlib" )
	add_subdirectory( zlib )
	set( ZLIB_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/zlib )
	set( ZLIB_LIBRARIES z )
	set( ZLIB_LIBRARY z )
endif( ZLIB_FOUND AND NOT FORCE_INTERNAL_ZLIB )

if( JPEG_FOUND AND NOT FORCE_INTERNAL_JPEG )
	message( STATUS "Using system jpeg library" )
else( JPEG_FOUND AND NOT FORCE_INTERNAL_JPEG )
	message( STATUS "Using internal jpeg library" )
	add_subdirectory( jpeg-6b )
	set( JPEG_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/jpeg-6b )
	set( JPEG_LIBRARIES jpeg )
	set( JPEG_LIBRARY jpeg )
endif( JPEG_FOUND AND NOT FORCE_INTERNAL_JPEG )

if( BZIP2_FOUND AND NOT FORCE_INTERNAL_BZIP2 )
	message( STATUS "Using system bzip2 library" )
else( BZIP2_FOUND AND NOT FORCE_INTERNAL_BZIP2 )
	message( STATUS "Using internal bzip2 library" )
	add_subdirectory( bzip2 )
	set( BZIP2_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/bzip2" )
	set( BZIP2_LIBRARIES bz2 )
	set( BZIP2_LIBRARY bz2 )
endif( BZIP2_FOUND AND NOT FORCE_INTERNAL_BZIP2 )

if( GME_FOUND AND NOT FORCE_INTERNAL_GME )
	message( STATUS "Using system gme library ${GME_INCLUDE_DIR}" )
else( GME_FOUND AND NOT FORCE_INTERNAL_GME )
	message( STATUS "Using internal gme library" )
	add_subdirectory( game-music-emu )
	set( GME_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/game-music-emu" )
	set( GME_LIBRARIES gme )
endif( GME_FOUND AND NOT FORCE_INTERNAL_GME )

set( LZMA_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/lzma/C" )

# [EP/BB]
if( RELEASE_WITH_DEBUG_FILE )
	if ( MSVC )
		set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} /Zi")
		set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /Zi")
		set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} /DEBUG /OPT:REF /OPT:ICF")
	else ( MSVC )
		set( CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -g3" )
		set( CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -g3" )
	endif ( MSVC )
endif( RELEASE_WITH_DEBUG_FILE )

# [BB] Library that allows to map IP addresses to country names.
add_subdirectory( GeoIP )
# [BB]
add_subdirectory( server-registry )
# [BB] Library for the database backend.
add_subdirectory( sqlite )
add_subdirectory( lzma )
add_subdirectory( tools )
add_subdirectory( dumb )
add_subdirectory( gdtoa )

# [rc4l] sentry-native crash reporting -> our self-hosted GlitchTip. Built with the inproc
# backend (no external handler binary). Empty ZX_SENTRY_DSN disables it at compile time.
set( ZX_SENTRY_DSN "https://6e61a7f0-0c4b-47d7-9444-ecbef0957bfc@crash.cantstopscrolling.net/1"
	CACHE STRING "Sentry/GlitchTip DSN for crash reporting (empty string disables it)" )
option( ZX_ENABLE_SENTRY "Build with sentry-native crash reporting" ON )
if( ZX_ENABLE_SENTRY AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/sentry-native/CMakeLists.txt" )
	set( SENTRY_BACKEND inproc CACHE STRING "" FORCE )
	set( SENTRY_BUILD_TESTS OFF CACHE BOOL "" FORCE )
	set( SENTRY_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE )
	set( SENTRY_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE )
	add_subdirectory( sentry-native )
else()
	set( ZX_ENABLE_SENTRY OFF )
endif()

# [rc4l] Guard against silently shipping crash reporting that can't deliver: if sentry is enabled it
# MUST have a DSN, or every report goes nowhere (a whole class of "works locally, dead in release"
# bugs). Emit a status line CI asserts on (see .github/workflows/_build.yml).
if( ZX_ENABLE_SENTRY )
	if( ZX_SENTRY_DSN STREQUAL "" )
		message( FATAL_ERROR "ZX_ENABLE_SENTRY is ON but ZX_SENTRY_DSN is empty -- crash reports would "
			"go nowhere. Set -DZX_SENTRY_DSN=... or build with -DZX_ENABLE_SENTRY=OFF." )
	endif()
	message( STATUS "ZX crash reporting: sentry ENABLED (DSN baked in)" )
else()
	message( STATUS "ZX crash reporting: sentry DISABLED" )
endif()

add_subdirectory( wadsrc )
add_subdirectory( wadsrc_bm )
# [BB] Zandronum includes the light definitions in zandronum.pk3.
#add_subdirectory( wadsrc_lights )
# [BB] Compatibility wad to support Skulltag mods.
add_subdirectory( wadsrc_st )
add_subdirectory( src )

# [AK] Library for noise suppression for VoIP.
if ( NOT NO_SOUND )
	add_subdirectory( rnnoise )
endif ( NOT NO_SOUND )

if( NOT CMAKE_CROSSCOMPILING )
	export(TARGETS ${CROSS_EXPORTS} FILE "${CMAKE_BINARY_DIR}/ImportExecutables.cmake" )
endif( NOT CMAKE_CROSSCOMPILING )
