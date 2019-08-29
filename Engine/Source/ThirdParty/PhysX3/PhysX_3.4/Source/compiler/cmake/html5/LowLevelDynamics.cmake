#
# Build LowLevelDynamics
#

SET(PHYSX_SOURCE_DIR ${PROJECT_SOURCE_DIR}/../../../)

SET(LL_SOURCE_DIR ${PHYSX_SOURCE_DIR}/LowLevelDynamics/src)

SET(LOWLEVELDYNAMICS_PLATFORM_INCLUDES
	$ENV{EMSCRIPTEN}/system/include
)

SET(LOWLEVELDYNAMICS_COMPILE_DEFS

	# Common to all configurations
	${PHYSX_HTML5_COMPILE_DEFS};

	$<$<CONFIG:debug>:${PHYSX_HTML5_DEBUG_COMPILE_DEFS};>
	$<$<CONFIG:checked>:${PHYSX_HTML5_CHECKED_COMPILE_DEFS};>
	$<$<CONFIG:profile>:${PHYSX_HTML5_PROFILE_COMPILE_DEFS};>
	$<$<CONFIG:release>:${PHYSX_HTML5_RELEASE_COMPILE_DEFS};>
)

# include common low level dynamics settings
INCLUDE(../common/LowLevelDynamics.cmake)
