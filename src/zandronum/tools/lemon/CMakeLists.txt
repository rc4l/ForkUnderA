if( NOT CMAKE_CROSSCOMPILING )
	set( CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -D_DEBUG" )

	add_executable( lemon lemon.c )
	set( CROSS_EXPORTS ${CROSS_EXPORTS} lemon PARENT_SCOPE )

	# Lemon wants lempar.c in its directory
	# [rc4l] Only where lemon is built: a cross build imports the native one, beside which its own build already put lempar.c.
	if( NOT NO_GENERATOR_EXPRESSIONS )
	add_custom_command( TARGET lemon
		POST_BUILD
		COMMAND echo $<TARGET_FILE_DIR:lemon>
		COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_CURRENT_SOURCE_DIR}/lempar.c $<TARGET_FILE_DIR:lemon> )
	else( NOT NO_GENERATOR_EXPRESSIONS )
	add_custom_command( TARGET lemon
		POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_CURRENT_SOURCE_DIR}/lempar.c ${CMAKE_CURRENT_BINARY_DIR} )
	endif( NOT NO_GENERATOR_EXPRESSIONS )
endif( NOT CMAKE_CROSSCOMPILING )
