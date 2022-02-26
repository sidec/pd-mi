cmake_minimum_required(VERSION 3.19)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

project("pd-mi")

message(STATUS "Updating Git Submodules")
execute_process(
	COMMAND	git submodule update --init --recursive
	WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
)

include(${CMAKE_CURRENT_SOURCE_DIR}/pd.build/pd.cmake)
include(scripts/utilities.cmake)

set(TO_BUILD "pd.mi.wrps_tilde", "pd.mi.plts_tilde") 
SUBDIRLIST(PROJECT_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/src)
foreach (project_dir ${PROJECT_DIRS})
	list(FIND TO_BUILD ${project_dir} INCLUDED_IN_BUILD)
	if (NOT INCLUDED_IN_BUILD EQUAL -1)
		message("included in build: ${INCLUDED_IN_BUILD}")
		if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/${project_dir}/CMakeLists.txt")
			message("Generating: ${project_dir}")
			add_compile_definitions(TEST)
			add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/src/${project_dir})
		endif ()
	endif ()
	
endforeach ()