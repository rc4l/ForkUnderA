# [SB] Removed for CMake 4.0 compatibility.
#cmake_minimum_required( VERSION 2.4 )

# [rc4l] Gated like UZDoom's wadsrc_bm: the content derives from commercial artwork, so a
# commercial standalone game builds with -DBUILD_NONFREE=OFF and ships without it.
if( BUILD_NONFREE )
	add_pk3(brightmaps.pk3 ${CMAKE_CURRENT_SOURCE_DIR}/static)
endif()
