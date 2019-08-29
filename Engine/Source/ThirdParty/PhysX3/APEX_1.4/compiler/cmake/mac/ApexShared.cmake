#
# Build ApexShared
#

SET(GW_DEPS_ROOT $ENV{GW_DEPS_ROOT})
FIND_PACKAGE(PxShared REQUIRED)

SET(APEX_MODULE_DIR ${PROJECT_SOURCE_DIR}/../../../module)

SET(SHARED_SOURCE_DIR ${PROJECT_SOURCE_DIR}/../../../shared)

#SET(AM_SOURCE_DIR ${APEX_MODULE_DIR}/{{TARGET_MODULE_DIR}})

SET(APEXSHARED_PLATFORM_INCLUDES
	${PROJECT_SOURCE_DIR}/../../../common/include/mac	
)

SET(APEX_SHARED_LIBTYPE STATIC)

# Use generator expressions to set config specific preprocessor definitions
SET(APEXSHARED_COMPILE_DEFS 

	# Common to all configurations
	${APEX_MAC_COMPILE_DEFS};_LIB;PX_PHYSX_STATIC_LIB;

	$<$<CONFIG:debug>:${APEX_MAC_DEBUG_COMPILE_DEFS};>
	$<$<CONFIG:checked>:${APEX_MAC_CHECKED_COMPILE_DEFS};>
	$<$<CONFIG:profile>:${APEX_MAC_PROFILE_COMPILE_DEFS};>
	$<$<CONFIG:release>:${APEX_MAC_RELEASE_COMPILE_DEFS};>
)

# include common ApexShared.cmake
INCLUDE(../common/ApexShared.cmake)