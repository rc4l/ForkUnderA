# [rc4l] Gated with the brightmaps for the same reason: the artwork in here has no recorded
# license, so a commercial standalone game builds with -DBUILD_NONFREE=OFF and ships without
# it. See static/license.md.
if( BUILD_NONFREE )
	add_pk3(skulltag_actors.pk3 ${CMAKE_CURRENT_SOURCE_DIR}/static)
endif()
