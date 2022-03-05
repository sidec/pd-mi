cmake_minimum_required(VERSION 3.0)

set(PRODUCT_NAME pd.mi.tds~) 
set(PROJECT_NAME ${project_dir})

# include(${CMAKE_CURRENT_SOURCE_DIR}/../../pd.build/pd.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/pre-target.cmake)

# Define the path to the Pure Data sources
set_pd_sources(${CMAKE_CURRENT_SOURCE_DIR}/../../pure-data/src)
# Set the output path for the externals  
set_pd_external_path(${CMAKE_CURRENT_SOURCE_DIR}/../../externals/)

# paths to our sources
set(MUTABLE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/../../vb.mi-dev/source/mutableSources32) # eurorack
set(STMLIB_PATH ${MUTABLE_PATH}/stmlib)
set(MI_PATH ${MUTABLE_PATH}/tides2)

set(STMLIB_SOURCES 
	${STMLIB_PATH}/stmlib.h
    ${STMLIB_PATH}/utils/gate_flags.h
	${STMLIB_PATH}/dsp/dsp.h
	${STMLIB_PATH}/dsp/hysteresis_quantizer.h
    ${STMLIB_PATH}/dsp/polyblep.h
)

set(MI_SOURCES
	${MI_PATH}/ramp_generator.h
	${MI_PATH}/ramp_shaper.h
	${MI_PATH}/ratio.h
	${MI_PATH}/resources.cc
	${MI_PATH}/resources.h
	${MI_PATH}/poly_slope_generator.cc
	${MI_PATH}/poly_slope_generator.h
	${MI_PATH}/ramp_extractor.cc
	${MI_PATH}/ramp_extractor.h
)

set(BUILD_SOURCES 
	${PROJECT_NAME}.cpp
)

include_directories( ${MUTABLE_PATH})

set(ALL_SOURCES 
	${STMLIB_SOURCES}
	${MI_SOURCES}
	${BUILD_SOURCES}
)

add_pd_external(${PROJECT_NAME} ${PRODUCT_NAME} "${ALL_SOURCES}")

include(${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/post-target.cmake)
# target_compile_definitions(${PROJECT_NAME} PUBLIC M_PI=3.14159265358979323846)
# add preprocessor macro to avoid asm functions
target_compile_definitions(${PROJECT_NAME} PUBLIC TEST)