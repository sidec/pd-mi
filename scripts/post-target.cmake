if (WIN32)

	if (CMAKE_GENERATOR MATCHES "Visual Studio")
		# warning about constexpr not being const in c++14
		# set_target_properties(${PROJECT_NAME} PROPERTIES COMPILE_FLAGS "/wd4814")

		# do not generate ILK files
		# set_target_properties(${PROJECT_NAME} PROPERTIES LINK_FLAGS "/INCREMENTAL:NO")

		# allow parallel builds
		set_target_properties(${PROJECT_NAME} PROPERTIES COMPILE_FLAGS "/MP")
	endif ()

endif ()