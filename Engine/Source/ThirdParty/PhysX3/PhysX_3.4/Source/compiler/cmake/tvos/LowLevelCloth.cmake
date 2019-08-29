#
# Build LowLevelCloth
#

SET(PHYSX_SOURCE_DIR ${PROJECT_SOURCE_DIR}/../../../)

SET(LL_SOURCE_DIR ${PHYSX_SOURCE_DIR}/LowLevelCloth/src)

SET(LOWLEVELCLOTH_PLATFORM_INCLUDES
	${PHYSX_SOURCE_DIR}/LowLevelAABB/tvos/include
)

SET(LOWLEVELCLOTH_COMPILE_DEFS

	# Common to all configurations
	${PHYSX_TVOS_COMPILE_DEFS};PX_PHYSX_STATIC_LIB

	$<$<CONFIG:debug>:${PHYSX_TVOS_DEBUG_COMPILE_DEFS};PX_PHYSX_DLL_NAME_POSTFIX=DEBUG;>
	$<$<CONFIG:checked>:${PHYSX_TVOS_CHECKED_COMPILE_DEFS};PX_PHYSX_DLL_NAME_POSTFIX=CHECKED;>
	$<$<CONFIG:profile>:${PHYSX_TVOS_PROFILE_COMPILE_DEFS};PX_PHYSX_DLL_NAME_POSTFIX=PROFILE;>
	$<$<CONFIG:release>:${PHYSX_TVOS_RELEASE_COMPILE_DEFS};>
)

# include common low level cloth settings
INCLUDE(../common/LowLevelCloth.cmake)


# enable -fPIC so we can link static libs with the editor
SET_TARGET_PROPERTIES(LowLevelCloth PROPERTIES POSITION_INDEPENDENT_CODE TRUE)
