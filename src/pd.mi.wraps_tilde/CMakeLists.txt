cmake_minimum_required(VERSION 3.0)

include(${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/pre-target.cmake)

set(PRODUCT_NAME pd.mi.wraps~) 
set(PROJECT_NAME ${project_dir})


# Define the path to the Pure Data sources
set_pd_sources(${CMAKE_CURRENT_SOURCE_DIR}/../../pure-data/src)
# Set the output path for the externals  
set_pd_external_path(${CMAKE_CURRENT_SOURCE_DIR}/../../externals/)

# paths to our sources
set(MUTABLE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/../../eurorack)
set(MI_PATH ${MUTABLE_PATH}/warps)
set(STMLIB_PATH ${MUTABLE_PATH}/stmlib)

set(MI_SOURCES
	${MI_PATH}/dsp/filter_bank.cc
	${MI_PATH}/dsp/filter_bank.h
	${MI_PATH}/dsp/limiter.h
	${MI_PATH}/dsp/modulator.cc
	${MI_PATH}/dsp/modulator.h
	${MI_PATH}/dsp/oscillator.cc
	${MI_PATH}/dsp/oscillator.h
	${MI_PATH}/dsp/parameters.h
	${MI_PATH}/dsp/quadrature_oscillator.h
	${MI_PATH}/dsp/quadrature_transform.h
	${MI_PATH}/dsp/sample_rate_conversion_filters.h
	${MI_PATH}/dsp/sample_rate_converter.h
	${MI_PATH}/dsp/vocoder.cc
	${MI_PATH}/dsp/vocoder.h
	${MI_PATH}/resources.cc
	${MI_PATH}/resources.h
)

# set(MI_SOURCES 
# 	${WRPS_PATH}/dsp/modulator.cc
# 	${WRPS_PATH}/dsp/oscillator.cc
# 	${WRPS_PATH}/dsp/vocoder.cc
# 	${WRPS_PATH}/dsp/filter_bank.cc
# 	${WRPS_PATH}/resources.cc
# )


set(STMLIB_SOURCES 
	${STMLIB_PATH}/stmlib.h
	${STMLIB_PATH}/utils/random.cc
	${STMLIB_PATH}/utils/random.h
	${STMLIB_PATH}/dsp/units.cc
	${STMLIB_PATH}/dsp/units.h
)

set(BUILD_SOURCES 
	${PROJECT_NAME}.cpp
)

include_directories( 
	${MUTABLE_PATH}
	)

set(ALL_SOURCES 
	${STMLIB_SOURCES}
	${MI_SOURCES}
	${BUILD_SOURCES}
)

add_pd_external(${PROJECT_NAME} ${PRODUCT_NAME} "${ALL_SOURCES}")

include(${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/post-target.cmake)

# add preprocessor macro to avoid asm functions
target_compile_definitions(${PROJECT_NAME} PUBLIC TEST)