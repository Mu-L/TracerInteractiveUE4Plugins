// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"

class USceneComponent;
class FSceneInterface;
class FPrimitiveSceneInfo;

/**
* Utility class for applying an offset to a hierarchy of components in the renderer thread.
*/
class HEADMOUNTEDDISPLAY_API FLateUpdateManager
{
public:
	FLateUpdateManager();
	virtual ~FLateUpdateManager() {}

	/** Setup state for applying the render thread late update */
	void Setup(const FTransform& ParentToWorld, USceneComponent* Component, bool bSkipLateUpdate);

	/** Returns true if the LateUpdateSetup data is stale. */
	bool GetSkipLateUpdate_RenderThread() const;

	/** Apply the late update delta to the cached components */
	void Apply_RenderThread(FSceneInterface* Scene, const FTransform& OldRelativeTransform, const FTransform& NewRelativeTransform);

	/** Increments the double buffered read index, etc. - in prep for the next render frame (read: MUST be called for each frame Setup() was called on). */
	void PostRender_RenderThread();

private:

	/*
	 *  Late update primitive info for accessing valid scene proxy info. From the time the info is gathered
	 *  to the time it is later accessed the render proxy can be deleted. To ensure we only access a proxy that is
	 *  still valid we cache the primitive's scene info AND a pointer to its own cached index. If the primitive
	 *  is deleted or removed from the scene then attempting to access it via its index will result in a different
	 *  scene info than the cached scene info.
	 */
	struct LateUpdatePrimitiveInfo
	{
		const int32*			IndexAddress;
		FPrimitiveSceneInfo*	SceneInfo;
	};

	/** A utility method that calls CacheSceneInfo on ParentComponent and all of its descendants */
	void GatherLateUpdatePrimitives(USceneComponent* ParentComponent);
	/** Generates a LateUpdatePrimitiveInfo for the given component if it has a SceneProxy and appends it to the current LateUpdatePrimitives array */
	void CacheSceneInfo(USceneComponent* Component);

	/** Parent world transform used to reconstruct new world transforms for late update scene proxies */
	FTransform LateUpdateParentToWorld[2];
	/** Primitives that need late update before rendering */
	TArray<LateUpdatePrimitiveInfo> LateUpdatePrimitives[2];
	/** Late Update Info Stale, if this is found true do not late update */
	bool SkipLateUpdate[2];

	int32 LateUpdateGameWriteIndex;
	int32 LateUpdateRenderReadIndex;

};

