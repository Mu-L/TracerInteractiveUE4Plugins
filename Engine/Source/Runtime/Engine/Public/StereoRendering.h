// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	StereoRendering.h: Abstract stereoscopic rendering interface
=============================================================================*/

#pragma once

#include "CoreMinimal.h"

class FSceneView;
class IStereoLayers;
class IStereoRenderTargetManager;

/**
* Stereoscopic rendering passes.  FULL implies stereoscopic rendering isn't enabled for this pass
*/
enum EStereoscopicPass
{
	eSSP_FULL,
	eSSP_LEFT_EYE,
	eSSP_RIGHT_EYE,
	eSSP_LEFT_EYE_SIDE,
	eSSP_RIGHT_EYE_SIDE
};

class ENGINE_API IStereoRendering
{
public:
	virtual ~IStereoRendering() { }

	/** 
	 * Whether or not stereo rendering is on this frame.
	 */
	virtual bool IsStereoEnabled() const = 0;

	/** 
	 * Whether or not stereo rendering is on on next frame. Useful to determine if some preparation work
	 * should be done before stereo got enabled in next frame. 
	 */
	virtual bool IsStereoEnabledOnNextFrame() const { return IsStereoEnabled(); }

	/** 
	 * Switches stereo rendering on / off. Returns current state of stereo.
	 * @return Returns current state of stereo (true / false).
	 */
	virtual bool EnableStereo(bool stereo = true) = 0;

	/**
	* Returns the desired number of views, so that devices that require additional views can allocate them.
	* Default is two viewports if using stereo rendering.
	*/
	virtual int32 GetDesiredNumberOfViews(bool bStereoRequested) const { return (bStereoRequested) ? 2 : 1; }

	/**
	* For the specified view index in the view family, assign a stereoscopic pass type based on the extension's usage
	*/
	virtual EStereoscopicPass GetViewPassForIndex(bool bStereoRequested, uint32 ViewIndex) const
	{
		if (!bStereoRequested)
			return EStereoscopicPass::eSSP_FULL;
		else if (ViewIndex == 0)
			return EStereoscopicPass::eSSP_LEFT_EYE;
		else
			return EStereoscopicPass::eSSP_RIGHT_EYE;
	}

	/**
	* For the specified stereoscopic pass type, assign a view index based on the extension's sorting
	*/
	virtual uint32 GetViewIndexForPass(EStereoscopicPass StereoPassType) const
	{
		switch (StereoPassType)
		{
		case eSSP_LEFT_EYE:
		case eSSP_FULL:
			return 0;

		case eSSP_RIGHT_EYE:
			return 1;

		default:
			check(0);
			return -1;
		}
	}

	/**
	* Static helper. Passes request to the current stereo device
	*/
	static bool IsStereoEyePass(EStereoscopicPass Pass);

	/**
	* Return true if this pass is for a stereo eye view
	*/
	virtual bool DeviceIsStereoEyePass(EStereoscopicPass Pass)
	{
		return !(Pass == EStereoscopicPass::eSSP_FULL);
	}

	/**
	* Static helper. Passes request to the current stereo device
	*/
	static bool IsStereoEyeView(const FSceneView& View);

	/**
	* Return true if this is a stereoscopic view
	*/
	virtual bool DeviceIsStereoEyeView(const FSceneView& View);

	/**
	* Static helper. Passes request to the current stereo device
	*/
	static bool IsAPrimaryPass(EStereoscopicPass Pass);

	/**
	* Return true if this pass is for a view we do all the work for (ie this view can't borrow from another)
	*/
	virtual bool DeviceIsAPrimaryPass(EStereoscopicPass Pass)
	{
		return Pass == EStereoscopicPass::eSSP_FULL || Pass == EStereoscopicPass::eSSP_LEFT_EYE;
	}

	/**
	* Static helper. Passes request to the current stereo device
	*/
	static bool IsAPrimaryView(const FSceneView& View);

	/**
	* Return true if primary view
	*/
	virtual bool DeviceIsAPrimaryView(const FSceneView& View);

	/**
	* Static helper. Passes request to the current stereo device
	*/
	static bool IsASecondaryPass(EStereoscopicPass Pass);

	/**
	* Return true if this pass is for a view for which we share some work done for eSSP_LEFT_EYE (ie borrow some intermediate state from that eye)
	*/
	virtual bool DeviceIsASecondaryPass(EStereoscopicPass Pass)
	{
		return !DeviceIsAPrimaryPass(Pass);
	}

	/**
	* Static helper. Passes request to the current stereo device
	*/
	static bool IsASecondaryView(const FSceneView& View);

	/**
	* Return true if secondary view
	*/
	virtual bool DeviceIsASecondaryView(const FSceneView& View);

	/**
	* Static helper. Passes request to the current stereo device
	*/
	static bool IsAnAdditionalPass(EStereoscopicPass Pass);

	/**
	* Return true for additional eyes past the first two (a plugin could implement additional 'eyes').
	*/
	virtual bool DeviceIsAnAdditionalPass(EStereoscopicPass Pass)
	{
		return Pass > EStereoscopicPass::eSSP_RIGHT_EYE;
	}

	/**
	* Static helper. Passes request to the current stereo device
	*/
	static bool IsAnAdditionalView(const FSceneView& View);

	/**
	* Return true if additional view
	*/
	virtual bool DeviceIsAnAdditionalView(const FSceneView& View);

	/**
	 * Static helper. Passes request to the current stereo device
	 */
	static uint32 GetLODViewIndex();

	/**
	 * Return the index of the view that is used for selecting LODs
	 */
	virtual uint32 DeviceGetLODViewIndex() const;

	/**
	 * Adjusts the viewport rectangle for stereo, based on which eye pass is being rendered.
	 */
	virtual void AdjustViewRect(enum EStereoscopicPass StereoPass, int32& X, int32& Y, uint32& SizeX, uint32& SizeY) const = 0;

	/**
	* Provides the final view rect that the renderer will render into.
	*/
	virtual void SetFinalViewRect(const enum EStereoscopicPass StereoPass, const FIntRect& FinalViewRect) {}

	/**
	 * Gets the percentage bounds of the safe region to draw in.  This allows things like stat rendering to appear within the readable portion of the stereo view.
	 * @return	The centered percentage of the view that is safe to draw readable text in
	 */
	virtual FVector2D GetTextSafeRegionBounds() const { return FVector2D(0.75f, 0.75f); }

	/**
	 * Calculates the offset for the camera position, given the specified position, rotation, and world scale
	 */
	virtual void CalculateStereoViewOffset(const enum EStereoscopicPass StereoPassType, FRotator& ViewRotation, const float WorldToMeters, FVector& ViewLocation) = 0;

	/**
	 * Gets a projection matrix for the device, given the specified eye setup
	 */
	virtual FMatrix GetStereoProjectionMatrix(const enum EStereoscopicPass StereoPassType) const = 0;

	/**
	 * Sets view-specific params (such as view projection matrix) for the canvas.
	 */
	virtual void InitCanvasFromView(class FSceneView* InView, class UCanvas* Canvas) = 0;

	// Renders texture into a backbuffer. Could be empty if no rendertarget texture is used, or if direct-rendering 
	// through RHI bridge is implemented. 
	virtual void RenderTexture_RenderThread(class FRHICommandListImmediate& RHICmdList, class FRHITexture2D* BackBuffer, class FRHITexture2D* SrcTexture, FVector2D WindowSize) const {}

	/**
	 * Returns currently active render target manager.
	 */
	virtual IStereoRenderTargetManager* GetRenderTargetManager() { return nullptr; }

	/**
	 * Returns an IStereoLayers implementation, if one is present
	 */
	virtual IStereoLayers* GetStereoLayers () { return nullptr; }

	
	virtual void StartFinalPostprocessSettings(struct FPostProcessSettings* StartPostProcessingSettings, const enum EStereoscopicPass StereoPassType) {}
	virtual bool OverrideFinalPostprocessSettings(struct FPostProcessSettings* OverridePostProcessingSettings, const enum EStereoscopicPass StereoPassType, float& BlendWeight) { return false; }
	virtual void EndFinalPostprocessSettings(struct FPostProcessSettings* FinalPostProcessingSettings, const enum EStereoscopicPass StereoPassType) {}

};
