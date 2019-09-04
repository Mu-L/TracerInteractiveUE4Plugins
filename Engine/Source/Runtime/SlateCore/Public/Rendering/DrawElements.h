// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/GCObject.h"
#include "Fonts/ShapedTextFwd.h"
#include "Stats/Stats.h"
#include "Misc/MemStack.h"
#include "Styling/WidgetStyle.h"
#include "Fonts/SlateFontInfo.h"
#include "Fonts/FontCache.h"
#include "Layout/SlateRect.h"
#include "Layout/Clipping.h"
#include "Types/PaintArgs.h"
#include "Layout/Geometry.h"
#include "Containers/StaticArray.h"
#include "Rendering/ShaderResourceManager.h"
#include "Rendering/RenderingCommon.h"
#include "Debugging/SlateDebugging.h"

class FSlateDrawLayerHandle;
class FSlateRenderBatch;
class FSlateRenderDataHandle;
class FSlateWindowElementList;
class ILayoutCache;
class SWidget;
class SWindow;

DECLARE_MEMORY_STAT_EXTERN(TEXT("Vertex/Index Buffer Pool Memory (CPU)"), STAT_SlateBufferPoolMemory, STATGROUP_SlateMemory, SLATECORE_API );


struct FSlateGradientStop
{
	FVector2D Position;
	FLinearColor Color;

	/**
	* Construct a Gradient Stop from a Position and a Color.
	* @param InPosition - The position in widget space for this stop. Both X and Y are used for a single-axis gradient.
						  A two stop gradient should go from (0,0), to (Width,Height).
	* @param InColor	- The color to lerp towards at this stop.
	*/
	FSlateGradientStop( const FVector2D& InPosition, const FLinearColor& InColor )
		: Position(InPosition)
		, Color(InColor)
	{

	}
};

template <> struct TIsPODType<FSlateGradientStop> { enum { Value = true }; };


class FSlateDrawLayerHandle;


class FSlateDataPayload
{
public:
	SLATECORE_API static FSlateShaderResourceManager* ResourceManager;

	FSlateDataPayload()
		: Tint(FLinearColor::White)
		, ResourceProxy(nullptr)
		, RenderTargetResource(nullptr)
		, BatchFlags(ESlateBatchDrawFlag::None)
		, bViewportTextureAlphaOnly(false)
		, bRequiresVSync(false)
		, CustomDrawer()
		, InstanceData(nullptr)
		, InstanceOffset(0)
		, NumInstances(0)
	{ }

	void SetGradientPayloadProperties( const TArray<FSlateGradientStop>& InGradientStops, EOrientation InGradientType )
	{
		GradientStops = InGradientStops;
		GradientType = InGradientType;
	}

	void SetCubicBezierPayloadProperties(const FVector2D& InP0, const FVector2D& InP1, const FVector2D& InP2, const FVector2D& InP3, float InThickness, const FLinearColor& InTint)
	{
		Tint = InTint;
		P0 = InP0;
		P1 = InP1;
		P2 = InP2;
		P3 = InP3;
		Thickness = InThickness;
	}
	
	void SetHermiteSplinePayloadProperties( const FVector2D& InStart, const FVector2D& InStartDir, const FVector2D& InEnd, const FVector2D& InEndDir, float InThickness, const FLinearColor& InTint )
	{
		Tint = InTint;
		P0 = InStart;
		P1 = InStart + InStartDir / 3.0f;
		P2 = InEnd - InEndDir / 3.0f;
		P3 = InEnd;
		Thickness = InThickness;
	}

	void SetGradientHermiteSplinePayloadProperties( const FVector2D& InStart, const FVector2D& InStartDir, const FVector2D& InEnd, const FVector2D& InEndDir, float InThickness, const TArray<FSlateGradientStop>& InGradientStops )
	{
		P0 = InStart;
		P1 = InStart + InStartDir / 3.0f;
		P2 = InEnd - InEndDir / 3.0f;
		P3 = InEnd;
		Thickness = InThickness;
		GradientStops = InGradientStops;
	}

	void SetViewportPayloadProperties( const TSharedPtr<const ISlateViewport>& InViewport, const FLinearColor& InTint )
	{
		Tint = InTint;
		RenderTargetResource = InViewport->GetViewportRenderTargetTexture();
		bAllowViewportScaling = InViewport->AllowScaling();
		bViewportTextureAlphaOnly = InViewport->IsViewportTextureAlphaOnly();
		bRequiresVSync = InViewport->RequiresVsync();
	}

	void SetCustomDrawerPayloadProperties( const TSharedPtr<ICustomSlateElement, ESPMode::ThreadSafe>& InCustomDrawer )
	{
		CustomDrawer = InCustomDrawer;
	}

	void SetCustomVertsPayloadProperties(const FSlateShaderResourceProxy* InRenderProxy, const TArray<FSlateVertex>& InVerts, const TArray<SlateIndex>& InIndexes, ISlateUpdatableInstanceBuffer* InInstanceData, uint32 InInstanceOffset, uint32 InNumInstances)
	{
		ResourceProxy = InRenderProxy;
		CustomVertsData = InVerts;
		CustomVertsIndexData = InIndexes;
		InstanceData = InInstanceData;
		InstanceOffset = InInstanceOffset;
		NumInstances = InNumInstances;
	}

	void SetLayerPayloadProperties(FSlateDrawLayerHandle* InLayerHandle)
	{
		LayerHandle = InLayerHandle;
		checkSlow(LayerHandle);
	}

	// Batch Flags
	FORCEINLINE const ESlateBatchDrawFlag GetBatchFlags() const { return BatchFlags; }

	// Brush
	void SetBrush(const FSlateBrush* InBrush)
	{
		check(InBrush);
		ensureMsgf(InBrush->GetDrawType() != ESlateBrushDrawType::NoDrawType, TEXT("This should have been filtered out earlier in the Make... call."));

		// The slate brush ptr can't be trusted after batch elements.
		BrushMargin = InBrush->GetMargin();
		BrushUVRegion = InBrush->GetUVRegion();
		BrushTiling = InBrush->GetTiling();
		BrushMirroring = InBrush->GetMirroring();
		BrushDrawType = InBrush->GetDrawType();

		const FSlateResourceHandle& ResourceHandle = InBrush->GetRenderingResource();
		ResourceProxy = ResourceHandle.GetResourceProxy();
	}

	FORCEINLINE const FMargin& GetBrushMargin() const { return BrushMargin; }
	FORCEINLINE const FBox2D& GetBrushUVRegion() const { return BrushUVRegion; }
	FORCEINLINE ESlateBrushTileType::Type GetBrushTiling() const { return BrushTiling; }
	FORCEINLINE ESlateBrushMirrorType::Type GetBrushMirroring() const { return BrushMirroring; }
	FORCEINLINE ESlateBrushDrawType::Type GetBrushDrawType() const { return BrushDrawType; }
	FORCEINLINE const FSlateShaderResourceProxy* GetResourceProxy() const { return ResourceProxy; }

	// Tinting
	FORCEINLINE void SetTint(FLinearColor InTint) { Tint = InTint; }
	FORCEINLINE FLinearColor GetTint() const { return Tint; }

	// Thickness
	FORCEINLINE void SetThickness(float InThickness) { Thickness = InThickness; }
	FORCEINLINE float GetThickness() const { return Thickness; }

	// Text
	void SetText(FSlateWindowElementList& ElementList, const FString& Text, const FSlateFontInfo& FontInfo, const int32 StartIndex = 0, const int32 EndIndex = MAX_int32);

	FORCEINLINE const FSlateFontInfo& GetFontInfo() const { return FontInfo; }
	FORCEINLINE const TCHAR* GetText() const { return ImmutableText; }
	FORCEINLINE int32 GetTextLength() const { return TextLength; }

	// Shaped Text
	void SetShapedText(FSlateWindowElementList& ElementList, const FShapedGlyphSequencePtr& InShapedGlyphSequence, FLinearColor InOutlineTint);

	FORCEINLINE FShapedGlyphSequencePtr GetShapedGlyphSequence() const { return ShapedGlyphSequence; }
	FORCEINLINE FLinearColor GetOutlineTint() const { return OutlineTint; }

	// Lines
	void SetLines(FSlateWindowElementList& ElementList, const TArray<FVector2D>& InPoints, bool bInAntialias, const TArray<FLinearColor>* InPointColors = nullptr);

	FORCEINLINE bool IsAntialiased() const { return bAntialias; }
	FORCEINLINE uint16 GetNumPoints() const { return NumPoints; }
	FORCEINLINE const FVector2D* GetPoints() const { return Points; }
	FORCEINLINE const FLinearColor* GetPointColors() const { return PointColors; }

	// Cached Buffers
	void SetCachedBuffer(FSlateRenderDataHandle* InRenderDataHandle, const FVector2D& Offset)
	{
		check(InRenderDataHandle);

		CachedRenderData = InRenderDataHandle;
		CachedRenderDataOffset = Offset;
	}

	FORCEINLINE class FSlateRenderDataHandle* GetRenderDataHandle() const { return CachedRenderData; }
	FORCEINLINE FVector2D GetRenderOffset() const { return CachedRenderDataOffset; }

protected:
	FLinearColor Tint;

	// The slate brush ptr can't be trusted after batch elements, so we copy everything when slate brush is set.
	FMargin BrushMargin;
	FBox2D BrushUVRegion;
	TEnumAsByte<enum ESlateBrushTileType::Type> BrushTiling;
	TEnumAsByte<enum ESlateBrushMirrorType::Type> BrushMirroring;
	TEnumAsByte<enum ESlateBrushDrawType::Type> BrushDrawType;

	// The resource proxy that we actually render with
	const FSlateShaderResourceProxy* ResourceProxy;

public:
	//TODO add getter
	// Render target 'brush'
	FSlateShaderResource* RenderTargetResource;

protected:
	// The font to use when rendering
	FSlateFontInfo FontInfo;
	// The null-terminated string data
	TCHAR* ImmutableText;
	// The length of the text (excluding the null-terminator, matching strlen)
	int32 TextLength;

	// Spline/Line Data
	float Thickness;

	// Shaped text data
	FShapedGlyphSequencePtr ShapedGlyphSequence;
	// Element outline tint
	FLinearColor OutlineTint;

	// Line data - allocated with FSlateWindowElementList::Alloc
	uint16 NumPoints;

	FVector2D* Points;
	FLinearColor* PointColors;

	// Cached render data
	class FSlateRenderDataHandle* CachedRenderData;
	FVector2D CachedRenderDataOffset;

public:
	// Misc data
	ESlateBatchDrawFlag BatchFlags;

public:
	// Bezier Spline Data points. E.g.
	//
	//       P1 + - - - - + P2                P1 +
	//         /           \                    / \
	//     P0 *             * P3            P0 *   \   * P3
	//                                              \ /
	//                                               + P2	
	FVector2D P0;
	FVector2D P1;
	FVector2D P2;
	FVector2D P3;

	// Gradient data (fixme, this should be allocated with FSlateWindowElementList::Alloc)
	TArray<FSlateGradientStop> GradientStops;
	EOrientation GradientType;

	// Viewport data
	bool bAllowViewportScaling : 1;
	bool bViewportTextureAlphaOnly : 1;
	bool bRequiresVSync : 1;
	// Whether or not to anti-alias lines
	bool bAntialias : 1;

	// Custom drawer data
	TWeakPtr<ICustomSlateElement, ESPMode::ThreadSafe> CustomDrawer;

	// Custom verts data, TODO FSlateWindowElementList::Alloc)?
	TArray<FSlateVertex> CustomVertsData;
	TArray<SlateIndex> CustomVertsIndexData;

	// Instancing support
	ISlateUpdatableInstanceBuffer* InstanceData;
	uint32 InstanceOffset;
	uint32 NumInstances;

	// Layer handle
	FSlateDrawLayerHandle* LayerHandle;

	// Post Process Data
	FVector4 PostProcessData;
	int32 DownsampleAmount;
};


/**
 * FSlateDrawElement is the building block for Slate's rendering interface.
 * Slate describes its visual output as an ordered list of FSlateDrawElement s
 */
class FSlateDrawElement
{
public:
	enum EElementType
	{
		ET_Box,
		ET_DebugQuad,
		ET_Text,
		ET_ShapedText,
		ET_Spline,
		ET_Line,
		ET_Gradient,
		ET_Viewport,
		ET_Border,
		ET_Custom,
		ET_CustomVerts,
		/**
		* Used for Invalidation, these buffers represent a complete cached buffer of what we normally send to the GPU to be
		* drawn for a series of widgets.  They're used to reduce draw overhead in situations where the UI is largely static.
		*/
		ET_CachedBuffer,
		/**
		* These layers are different from "layerId", they're symbolic layers, used when building up cached geometry.  They allow
		* Slate to semantically differentiate between Layer A and Layer B, which may have completely different layerIds, which perhaps
		* overlap, but because they are in logically separate layers they won't intersect, the contents of Layer B would always
		* come after the contents of Layer A.
		*/
		ET_Layer,
		/**
		*
		*/
		ET_PostProcessPass,
		/** Total number of draw commands */
		ET_Count,
	};

	enum ERotationSpace
	{
		/** Relative to the element.  (0,0) is the upper left corner of the element */
		RelativeToElement,
		/** Relative to the alloted paint geometry.  (0,0) is the upper left corner of the paint geometry */
		RelativeToWorld,
	};

	/**
	 * Creates a wireframe quad for debug purposes
	 *
	 * @param ElementList			The list in which to add elements
	 * @param InLayer				The layer to draw the element on
	 * @param PaintGeometry         DrawSpace position and dimensions; see FPaintGeometry
	 */
	SLATECORE_API static void MakeDebugQuad( FSlateWindowElementList& ElementList, uint32 InLayer, const FPaintGeometry& PaintGeometry);

	/**
	 * Creates a box element based on the following diagram.  Allows for this element to be resized while maintain the border of the image
	 * If there are no margins the resulting box is simply a quad
	 *     ___LeftMargin    ___RightMargin
	 *    /                /
	 *  +--+-------------+--+
	 *  |  |c1           |c2| ___TopMargin
	 *  +--o-------------o--+
	 *  |  |             |  |
	 *  |  |c3           |c4|
	 *  +--o-------------o--+
	 *  |  |             |  | ___BottomMargin
	 *  +--+-------------+--+
	 *
	 * @param ElementList			The list in which to add elements
	 * @param InLayer               The layer to draw the element on
	 * @param PaintGeometry         DrawSpace position and dimensions; see FPaintGeometry
	 * @param InBrush               Brush to apply to this element
	 * @param InClippingRect        Parts of the element are clipped if it falls outside of this rectangle
	 * @param InDrawEffects         Optional draw effects to apply
	 * @param InTint                Color to tint the element
	 */
	SLATECORE_API static void MakeBox( 
		FSlateWindowElementList& ElementList,
		uint32 InLayer,
		const FPaintGeometry& PaintGeometry,
		const FSlateBrush* InBrush,
		ESlateDrawEffect InDrawEffects = ESlateDrawEffect::None,
		const FLinearColor& InTint = FLinearColor::White );


	UE_DEPRECATED(4.20, "Storing and passing in a FSlateResourceHandle to MakeBox is no longer necessary.")
	SLATECORE_API static void MakeBox(
		FSlateWindowElementList& ElementList,
		uint32 InLayer, 
		const FPaintGeometry& PaintGeometry, 
		const FSlateBrush* InBrush, 
		const FSlateResourceHandle& InRenderingHandle, 
		ESlateDrawEffect InDrawEffects = ESlateDrawEffect::None, 
		const FLinearColor& InTint = FLinearColor::White );

	SLATECORE_API static void MakeRotatedBox(
		FSlateWindowElementList& ElementList,
		uint32 InLayer, 
		const FPaintGeometry& PaintGeometry, 
		const FSlateBrush* InBrush, 
		ESlateDrawEffect InDrawEffects = ESlateDrawEffect::None, 
		float Angle = 0.0f,
		TOptional<FVector2D> InRotationPoint = TOptional<FVector2D>(),
		ERotationSpace RotationSpace = RelativeToElement,
		const FLinearColor& InTint = FLinearColor::White );

	/**
	 * Creates a text element which displays a string of a rendered in a certain font on the screen
	 *
	 * @param ElementList			The list in which to add elements
	 * @param InLayer               The layer to draw the element on
	 * @param PaintGeometry         DrawSpace position and dimensions; see FPaintGeometry
	 * @param InText                The string to draw
	 * @param StartIndex            Inclusive index to start rendering from on the specified text
	 * @param EndIndex				Exclusive index to stop rendering on the specified text
	 * @param InFontInfo            The font to draw the string with
	 * @param InClippingRect        Parts of the element are clipped if it falls outside of this rectangle
	 * @param InDrawEffects         Optional draw effects to apply
	 * @param InTint                Color to tint the element
	 */
	SLATECORE_API static void MakeText( FSlateWindowElementList& ElementList, uint32 InLayer, const FPaintGeometry& PaintGeometry, const FString& InText, const int32 StartIndex, const int32 EndIndex, const FSlateFontInfo& InFontInfo, ESlateDrawEffect InDrawEffects = ESlateDrawEffect::None, const FLinearColor& InTint = FLinearColor::White );
	
	SLATECORE_API static void MakeText( FSlateWindowElementList& ElementList, uint32 InLayer, const FPaintGeometry& PaintGeometry, const FString& InText, const FSlateFontInfo& InFontInfo, ESlateDrawEffect InDrawEffects = ESlateDrawEffect::None, const FLinearColor& InTint = FLinearColor::White );

	FORCEINLINE static void MakeText(FSlateWindowElementList& ElementList, uint32 InLayer, const FPaintGeometry& PaintGeometry, const FText& InText, const FSlateFontInfo& InFontInfo, ESlateDrawEffect InDrawEffects = ESlateDrawEffect::None, const FLinearColor& InTint = FLinearColor::White)
	{
		MakeText(ElementList, InLayer, PaintGeometry, InText.ToString(), InFontInfo, InDrawEffects, InTint);
	}

	/**
	 * Creates a text element which displays a series of shaped glyphs on the screen
	 *
	 * @param ElementList			The list in which to add elements
	 * @param InLayer               The layer to draw the element on
	 * @param PaintGeometry         DrawSpace position and dimensions; see FPaintGeometry
	 * @param InShapedGlyphSequence The shaped glyph sequence to draw
	 * @param InClippingRect        Parts of the element are clipped if it falls outside of this rectangle
	 * @param InDrawEffects         Optional draw effects to apply
	 * @param InTint                Color to tint the element
	 */
	SLATECORE_API static void MakeShapedText( FSlateWindowElementList& ElementList, uint32 InLayer, const FPaintGeometry& PaintGeometry, const FShapedGlyphSequenceRef& InShapedGlyphSequence, ESlateDrawEffect InDrawEffects, const FLinearColor& BaseTint, const FLinearColor& OutlineTint);

	/**
	 * Creates a gradient element
	 *
	 * @param ElementList			   The list in which to add elements
	 * @param InLayer                  The layer to draw the element on
	 * @param PaintGeometry            DrawSpace position and dimensions; see FPaintGeometry
	 * @param InGradientStops          List of gradient stops which define the element
	 * @param InGradientType           The type of gradient (I.E Horizontal, vertical)
	 * @param InClippingRect           Parts of the element are clipped if it falls outside of this rectangle
	 * @param InDrawEffects            Optional draw effects to apply
	 */
	SLATECORE_API static void MakeGradient( FSlateWindowElementList& ElementList, uint32 InLayer, const FPaintGeometry& PaintGeometry, TArray<FSlateGradientStop> InGradientStops, EOrientation InGradientType, ESlateDrawEffect InDrawEffects = ESlateDrawEffect::None );

	/**
	 * Creates a Hermite Spline element
	 *
	 * @param ElementList			The list in which to add elements
	 * @param InLayer               The layer to draw the element on
	 * @param PaintGeometry         DrawSpace position and dimensions; see FPaintGeometry
	 * @param InStart               The start point of the spline (local space)
	 * @param InStartDir            The direction of the spline from the start point
	 * @param InEnd                 The end point of the spline (local space)
	 * @param InEndDir              The direction of the spline to the end point
	 * @param InClippingRect        Parts of the element are clipped if it falls outside of this rectangle
	 * @param InDrawEffects         Optional draw effects to apply
	 * @param InTint                Color to tint the element
	 */
	SLATECORE_API static void MakeSpline( FSlateWindowElementList& ElementList, uint32 InLayer, const FPaintGeometry& PaintGeometry, const FVector2D& InStart, const FVector2D& InStartDir, const FVector2D& InEnd, const FVector2D& InEndDir, float InThickness = 0.0f, ESlateDrawEffect InDrawEffects = ESlateDrawEffect::None, const FLinearColor& InTint=FLinearColor::White );

	/**
	 * Creates a Bezier Spline element
	 *
	 * @param ElementList			The list in which to add elements
	 * @param InLayer               The layer to draw the element on
	 * @param PaintGeometry         DrawSpace position and dimensions; see FPaintGeometry
	 * @param InStart               The start point of the spline (local space)
	 * @param InStartDir            The direction of the spline from the start point
	 * @param InEnd                 The end point of the spline (local space)
	 * @param InEndDir              The direction of the spline to the end point
	 * @param InClippingRect        Parts of the element are clipped if it falls outside of this rectangle
	 * @param InDrawEffects         Optional draw effects to apply
	 * @param InTint                Color to tint the element
	 */
	SLATECORE_API static void MakeCubicBezierSpline(FSlateWindowElementList& ElementList, uint32 InLayer, const FPaintGeometry& PaintGeometry, const FVector2D& P0, const FVector2D& P1, const FVector2D& P2, const FVector2D& P3, float InThickness = 0.0f, ESlateDrawEffect InDrawEffects = ESlateDrawEffect::None, const FLinearColor& InTint = FLinearColor::White);

	/** Just like MakeSpline but in draw-space coordinates. This is useful for connecting already-transformed widgets together. */
	SLATECORE_API static void MakeDrawSpaceSpline(FSlateWindowElementList& ElementList, uint32 InLayer, const FVector2D& InStart, const FVector2D& InStartDir, const FVector2D& InEnd, const FVector2D& InEndDir, float InThickness = 0.0f, ESlateDrawEffect InDrawEffects = ESlateDrawEffect::None, const FLinearColor& InTint=FLinearColor::White);

	/** Just like MakeSpline but in draw-space coordinates. This is useful for connecting already-transformed widgets together. */
	UE_DEPRECATED(4.20, "Splines with color gradients will not be supported in the future.")
	SLATECORE_API static void MakeDrawSpaceGradientSpline( FSlateWindowElementList& ElementList, uint32 InLayer, const FVector2D& InStart, const FVector2D& InStartDir, const FVector2D& InEnd, const FVector2D& InEndDir, const TArray<FSlateGradientStop>& InGradientStops, float InThickness = 0.0f, ESlateDrawEffect InDrawEffects = ESlateDrawEffect::None );

	UE_DEPRECATED(4.20, "Splines with color gradients will not be supported in the future.")
	static void MakeDrawSpaceGradientSpline(FSlateWindowElementList& ElementList, uint32 InLayer, const FVector2D& InStart, const FVector2D& InStartDir, const FVector2D& InEnd, const FVector2D& InEndDir, const FSlateRect InClippingRect, const TArray<FSlateGradientStop>& InGradientStops, float InThickness = 0.0f, ESlateDrawEffect InDrawEffects = ESlateDrawEffect::None);

	/**
	 * Creates a line defined by the provided points
	 *
	 * @param ElementList			   The list in which to add elements
	 * @param InLayer                  The layer to draw the element on
	 * @param PaintGeometry            DrawSpace position and dimensions; see FPaintGeometry
	 * @param Points                   Points that make up the lines.  The points are joined together. I.E if Points has A,B,C there the line is A-B-C.  To draw non-joining line segments call MakeLines multiple times
	 * @param InClippingRect           Parts of the element are clipped if it falls outside of this rectangle
	 * @param InDrawEffects            Optional draw effects to apply
	 * @param InTint                   Color to tint the element
	 * @param bAntialias               Should antialiasing be applied to the line?
	 * @param Thickness                The thickness of the line
	 */
	SLATECORE_API static void MakeLines( FSlateWindowElementList& ElementList, uint32 InLayer, const FPaintGeometry& PaintGeometry, const TArray<FVector2D>& Points, ESlateDrawEffect InDrawEffects = ESlateDrawEffect::None, const FLinearColor& InTint=FLinearColor::White, bool bAntialias = true, float Thickness = 1.0f );
	SLATECORE_API static void MakeLines( FSlateWindowElementList& ElementList, uint32 InLayer, const FPaintGeometry& PaintGeometry, const TArray<FVector2D>& Points, const TArray<FLinearColor>& PointColors, ESlateDrawEffect InDrawEffects = ESlateDrawEffect::None, const FLinearColor& InTint=FLinearColor::White, bool bAntialias = true, float Thickness = 1.0f );

	/**
	 * Creates a viewport element which is useful for rendering custom data in a texture into Slate
	 *
	 * @param ElementList		   The list in which to add elements
	 * @param InLayer                  The layer to draw the element on
	 * @param PaintGeometry            DrawSpace position and dimensions; see FPaintGeometry
	 * @param Viewport                 Interface for drawing the viewport
	 * @param InClippingRect           Parts of the element are clipped if it falls outside of this rectangle
	 * @param InScale                  Draw scale to apply to the entire element
	 * @param InDrawEffects            Optional draw effects to apply
	 * @param InTint                   Color to tint the element
	 */
	SLATECORE_API static void MakeViewport( FSlateWindowElementList& ElementList, uint32 InLayer, const FPaintGeometry& PaintGeometry, TSharedPtr<const ISlateViewport> Viewport, ESlateDrawEffect InDrawEffects = ESlateDrawEffect::None, const FLinearColor& InTint=FLinearColor::White );

	/**
	 * Creates a custom element which can be used to manually draw into the Slate render target with graphics API calls rather than Slate elements
	 *
	 * @param ElementList		   The list in which to add elements
	 * @param InLayer                  The layer to draw the element on
	 * @param PaintGeometry            DrawSpace position and dimensions; see FPaintGeometry
	 * @param CustomDrawer		   Interface to a drawer which will be called when Slate renders this element
	 */
	SLATECORE_API static void MakeCustom( FSlateWindowElementList& ElementList, uint32 InLayer, TSharedPtr<ICustomSlateElement, ESPMode::ThreadSafe> CustomDrawer );
	
	SLATECORE_API static void MakeCustomVerts(FSlateWindowElementList& ElementList, uint32 InLayer, const FSlateResourceHandle& InRenderResourceHandle, const TArray<FSlateVertex>& InVerts, const TArray<SlateIndex>& InIndexes, ISlateUpdatableInstanceBuffer* InInstanceData, uint32 InInstanceOffset, uint32 InNumInstances, ESlateDrawEffect InDrawEffects = ESlateDrawEffect::None);

	SLATECORE_API static void MakeCachedBuffer(FSlateWindowElementList& ElementList, uint32 InLayer, TSharedPtr<FSlateRenderDataHandle, ESPMode::ThreadSafe>& CachedRenderDataHandle, const FVector2D& Offset);

	SLATECORE_API static void MakeLayer(FSlateWindowElementList& ElementList, uint32 InLayer, TSharedPtr<FSlateDrawLayerHandle, ESPMode::ThreadSafe>& DrawLayerHandle);

	SLATECORE_API static void MakePostProcessPass(FSlateWindowElementList& ElementList, uint32 InLayer, const FPaintGeometry& PaintGeometry, const FVector4& Params, int32 DownsampleAmount);

	FORCEINLINE EElementType GetElementType() const { return ElementType; }
	FORCEINLINE const FSlateDataPayload& GetDataPayload() const { return DataPayload; }

	FORCEINLINE int16 GetLayer() const { return Layer; }
	FORCEINLINE const FSlateRenderTransform& GetRenderTransform() const { return RenderTransform; }
	FORCEINLINE void SetRenderTransform(const FSlateRenderTransform& InRenderTransform) { RenderTransform = InRenderTransform; }
	FORCEINLINE const FVector2D& GetPosition() const { return Position; }
	FORCEINLINE void SetPosition(const FVector2D& InPosition) { Position = InPosition; }
	FORCEINLINE const FVector2D& GetLocalSize() const { return LocalSize; }
	FORCEINLINE float GetScale() const { return Scale; }
	FORCEINLINE ESlateDrawEffect GetDrawEffects() const { return DrawEffects; }
	FORCEINLINE bool IsPixelSnapped() const { return !EnumHasAllFlags(DrawEffects, ESlateDrawEffect::NoPixelSnapping); }
	FORCEINLINE const int16 GetClippingIndex() const { return ClippingIndex; }
	FORCEINLINE void SetClippingIndex(const int32 InClippingIndex) { ClippingIndex = InClippingIndex; }
	FORCEINLINE const int8 GetSceneIndex() const { return SceneIndex; }

	FORCEINLINE FSlateLayoutTransform GetInverseLayoutTransform() const
	{
		return Inverse(FSlateLayoutTransform(Scale, Position));
	}

	/**
	* Update element cached position with an arbitrary offset
	*
	* @param Element		   Element to update
	* @param InOffset         Absolute translation delta
	*/
	void ApplyPositionOffset(const FVector2D& InOffset);

private:
	void Init(FSlateWindowElementList& ElementList, EElementType InElementType, uint32 InLayer, const FPaintGeometry& PaintGeometry, ESlateDrawEffect InDrawEffects);

	static bool ShouldCull(const FSlateWindowElementList& ElementList);

	FORCEINLINE static bool ShouldCull(const FSlateWindowElementList& ElementList, const FPaintGeometry& PaintGeometry)
	{
		const FVector2D& LocalSize = PaintGeometry.GetLocalSize();
		if (LocalSize.X == 0 || LocalSize.Y == 0)
		{
			return true;
		}

		return ShouldCull(ElementList);
	}

	static bool ShouldCull(const FSlateWindowElementList& ElementList, const FPaintGeometry& PaintGeometry, const FSlateBrush* InBrush);

	FORCEINLINE static bool ShouldCull(const FSlateWindowElementList& ElementList, const FPaintGeometry& PaintGeometry, const FLinearColor& InTint)
	{
		if (InTint.A == 0 || ShouldCull(ElementList, PaintGeometry))
		{
			return true;
		}

		return false;
	}

	FORCEINLINE static bool ShouldCull(const FSlateWindowElementList& ElementList, const FPaintGeometry& PaintGeometry, const FLinearColor& InTint, const FString& InText)
	{
		if (InTint.A == 0 || InText.Len() == 0 || ShouldCull(ElementList, PaintGeometry))
		{
			return true;
		}

		return false;
	}

	FORCEINLINE static bool ShouldCull(const FSlateWindowElementList& ElementList, const FPaintGeometry& PaintGeometry, const FSlateBrush* InBrush, const FLinearColor& InTint)
	{
		if (InTint.A == 0 || ShouldCull(ElementList, PaintGeometry, InBrush))
		{
			return true;
		}

		return false;
	}

	static FVector2D GetRotationPoint( const FPaintGeometry& PaintGeometry, const TOptional<FVector2D>& UserRotationPoint, ERotationSpace RotationSpace );

private:
	FSlateRenderTransform RenderTransform;

	FVector2D Position;
	FVector2D LocalSize;
	float Scale;

	int16 Layer;
	int16 ClippingIndex;
	int8 SceneIndex;
	ESlateDrawEffect DrawEffects;
	EElementType ElementType;

	FSlateDataPayload DataPayload;
};


/**
 * Shader parameters for slate
 */
struct FShaderParams
{
	/** Pixel shader parameters */
	FVector4 PixelParams;
	FVector4 PixelParams2;

	FShaderParams()
		: PixelParams( 0,0,0,0 )
		, PixelParams2( 0,0,0,0 ) 
	{}

	FShaderParams( const FVector4& InPixelParams, const FVector4& InPixelParams2 = FVector4(0) )
		: PixelParams( InPixelParams )
		, PixelParams2( InPixelParams2 )
	{}

	bool operator==( const FShaderParams& Other ) const
	{
		return PixelParams == Other.PixelParams && PixelParams2 == Other.PixelParams2;
	}

	static FShaderParams MakePixelShaderParams( const FVector4& PixelShaderParams, const FVector4& InPixelShaderParams2 = FVector4(0) )
	{
		return FShaderParams( PixelShaderParams, InPixelShaderParams2);
	}
};

class FSlateRenderer;
class FSlateRenderBatch;

class ISlateRenderDataManager
{
public:
	virtual void BeginReleasingRenderData(const FSlateRenderDataHandle* RenderHandle) = 0;
};

class SLATECORE_API FSlateRenderDataHandle : public TSharedFromThis < FSlateRenderDataHandle, ESPMode::ThreadSafe >
{
public:
	FSlateRenderDataHandle(const ILayoutCache* Cacher, ISlateRenderDataManager* InManager);

	virtual ~FSlateRenderDataHandle();
	
	void Disconnect();

	const ILayoutCache* GetCacher() const { return Cacher; }

	void SetRenderBatches(TArray<FSlateRenderBatch>* InRenderBatches) { RenderBatches = InRenderBatches; }
	TArray<FSlateRenderBatch>* GetRenderBatches() { return RenderBatches; }

	void BeginUsing() { FPlatformAtomics::InterlockedIncrement(&UsageCount); }
	void EndUsing() { FPlatformAtomics::InterlockedDecrement(&UsageCount); }

	bool IsInUse() const { return UsageCount > 0; }

private:
	const ILayoutCache* Cacher;
	ISlateRenderDataManager* Manager;
	TArray<FSlateRenderBatch>* RenderBatches;

	volatile int32 UsageCount;
};

/** 
 * Represents an element batch for rendering. 
 */
class FSlateElementBatch
{
public:
	FSlateElementBatch(const FSlateShaderResource* InShaderResource, const FShaderParams& InShaderParams, ESlateShader::Type ShaderType, ESlateDrawPrimitive::Type PrimitiveType, ESlateDrawEffect DrawEffects, ESlateBatchDrawFlag DrawFlags, const int32 InClippingIndex, const TArray<FSlateClippingState>& ClippingStates, int32 InstanceCount = 0, uint32 InstanceOffset = 0, ISlateUpdatableInstanceBuffer* InstanceData = nullptr, int32 SceneIndex = -1)
		: BatchKey(InShaderParams, ShaderType, PrimitiveType, DrawEffects, DrawFlags, InClippingIndex, InstanceCount, InstanceOffset, InstanceData, SceneIndex)
		, ShaderResource(InShaderResource)
		, NumElementsInBatch(0)
		, VertexArrayIndex(INDEX_NONE)
		, IndexArrayIndex(INDEX_NONE)
	{
		SaveClippingState(ClippingStates);
	}

	FSlateElementBatch( TWeakPtr<ICustomSlateElement, ESPMode::ThreadSafe> InCustomDrawer, const int32 InClippingIndex, const TArray<FSlateClippingState>& ClippingStates)
		: BatchKey( InCustomDrawer, InClippingIndex)
		, ShaderResource( nullptr )
		, NumElementsInBatch(0)
		, VertexArrayIndex(INDEX_NONE)
		, IndexArrayIndex(INDEX_NONE)
	{
		SaveClippingState(ClippingStates);
	}

	FSlateElementBatch( TSharedPtr<FSlateRenderDataHandle, ESPMode::ThreadSafe> InCachedRenderHandle, FVector2D InCachedRenderDataOffset, const int32 InClippingIndex, const TArray<FSlateClippingState>& ClippingStates)
		: BatchKey( InCachedRenderHandle, InCachedRenderDataOffset, InClippingIndex)
		, ShaderResource( nullptr )
		, NumElementsInBatch(0)
		, VertexArrayIndex(INDEX_NONE)
		, IndexArrayIndex(INDEX_NONE)
	{
		SaveClippingState(ClippingStates);
	}

	FSlateElementBatch( TSharedPtr<FSlateDrawLayerHandle, ESPMode::ThreadSafe> InLayerHandle, const int32 InClippingIndex, const TArray<FSlateClippingState>& ClippingStates)
		: BatchKey( InLayerHandle, InClippingIndex )
		, ShaderResource( nullptr )
		, NumElementsInBatch(0)
		, VertexArrayIndex(INDEX_NONE)
		, IndexArrayIndex(INDEX_NONE)
	{
		SaveClippingState(ClippingStates);
	}

	void SaveClippingState(const TArray<FSlateClippingState>& ClippingStates)
	{
		// Store the clipping state so we can use it later for rendering.
		if (ClippingStates.IsValidIndex(GetClippingIndex()))
		{
			ClippingState = ClippingStates[GetClippingIndex()];
		}
	}

	bool operator==( const FSlateElementBatch& Other ) const
	{	
		return BatchKey == Other.BatchKey && ShaderResource == Other.ShaderResource;
	}

	const FSlateShaderResource* GetShaderResource() const { return ShaderResource; }
	const FShaderParams& GetShaderParams() const { return BatchKey.ShaderParams; }
	ESlateBatchDrawFlag GetDrawFlags() const { return BatchKey.DrawFlags; }
	ESlateDrawPrimitive::Type GetPrimitiveType() const { return BatchKey.DrawPrimitiveType; }
	ESlateShader::Type GetShaderType() const { return BatchKey.ShaderType; } 
	ESlateDrawEffect GetDrawEffects() const { return BatchKey.DrawEffects; }
	const int32 GetClippingIndex() const { return BatchKey.ClippingIndex; }
	const TWeakPtr<ICustomSlateElement, ESPMode::ThreadSafe> GetCustomDrawer() const { return BatchKey.CustomDrawer; }
	const TSharedPtr<FSlateRenderDataHandle, ESPMode::ThreadSafe> GetCachedRenderHandle() const { return BatchKey.CachedRenderHandle; }
	FVector2D GetCachedRenderDataOffset() const { return BatchKey.CachedRenderDataOffset; }
	const TSharedPtr<FSlateDrawLayerHandle, ESPMode::ThreadSafe> GetLayerHandle() const { return BatchKey.LayerHandle; }
	int32 GetInstanceCount() const{ return BatchKey.InstanceCount; }
	uint32 GetInstanceOffset() const{ return BatchKey.InstanceOffset; }
	const ISlateUpdatableInstanceBuffer* GetInstanceData() const { return BatchKey.InstanceData; }
	int32 GetSceneIndex() const { return BatchKey.SceneIndex; }
private:
	struct FBatchKey
	{
		const TWeakPtr<ICustomSlateElement, ESPMode::ThreadSafe> CustomDrawer;
		const TSharedPtr<FSlateRenderDataHandle, ESPMode::ThreadSafe> CachedRenderHandle;
		const FVector2D CachedRenderDataOffset;
		const TSharedPtr<FSlateDrawLayerHandle, ESPMode::ThreadSafe> LayerHandle;
		const FShaderParams ShaderParams;
		const ESlateBatchDrawFlag DrawFlags;	
		const ESlateShader::Type ShaderType;
		const ESlateDrawPrimitive::Type DrawPrimitiveType;
		const ESlateDrawEffect DrawEffects;
		const int32 ClippingIndex;
		const int32 InstanceCount;
		const uint32 InstanceOffset;
		const ISlateUpdatableInstanceBuffer* InstanceData;
		const int32 SceneIndex;

		FBatchKey(const FShaderParams& InShaderParams, ESlateShader::Type InShaderType, ESlateDrawPrimitive::Type InDrawPrimitiveType, ESlateDrawEffect InDrawEffects, ESlateBatchDrawFlag InDrawFlags, const int32 InClippingIndex, int32 InInstanceCount, uint32 InInstanceOffset, ISlateUpdatableInstanceBuffer* InInstanceBuffer, int32 InSceneIndex)
			: ShaderParams(InShaderParams)
			, DrawFlags(InDrawFlags)
			, ShaderType(InShaderType)
			, DrawPrimitiveType(InDrawPrimitiveType)
			, DrawEffects(InDrawEffects)
			, ClippingIndex(InClippingIndex)
			, InstanceCount(InInstanceCount)
			, InstanceOffset(InInstanceOffset)
			, InstanceData(InInstanceBuffer)
			, SceneIndex(InSceneIndex)
		{
		}

		FBatchKey( const FShaderParams& InShaderParams, ESlateShader::Type InShaderType, ESlateDrawPrimitive::Type InDrawPrimitiveType, ESlateDrawEffect InDrawEffects, ESlateBatchDrawFlag InDrawFlags, const int32 InClippingIndex, int32 InInstanceCount, uint32 InInstanceOffset, ISlateUpdatableInstanceBuffer* InInstanceBuffer)
			: ShaderParams( InShaderParams )
			, DrawFlags( InDrawFlags )
			, ShaderType( InShaderType )
			, DrawPrimitiveType( InDrawPrimitiveType )
			, DrawEffects( InDrawEffects )
			, ClippingIndex(InClippingIndex)
			, InstanceCount( InInstanceCount )
			, InstanceOffset( InInstanceOffset )
			, InstanceData(InInstanceBuffer)
			, SceneIndex(-1)
		{
		}

		FBatchKey( TWeakPtr<ICustomSlateElement, ESPMode::ThreadSafe> InCustomDrawer, const int32 InClippingIndex)
			: CustomDrawer( InCustomDrawer )
			, ShaderParams()
			, DrawFlags( ESlateBatchDrawFlag::None )
			, ShaderType( ESlateShader::Default )
			, DrawPrimitiveType( ESlateDrawPrimitive::TriangleList )
			, DrawEffects( ESlateDrawEffect::None )
			, ClippingIndex(InClippingIndex)
			, InstanceCount(0)
			, InstanceOffset(0)
			, SceneIndex(-1)
		{}

		FBatchKey( TSharedPtr<FSlateRenderDataHandle, ESPMode::ThreadSafe> InCachedRenderHandle, FVector2D InCachedRenderDataOffset, const int32 InClippingIndex)
			: CachedRenderHandle(InCachedRenderHandle)
			, CachedRenderDataOffset(InCachedRenderDataOffset)
			, ShaderParams()
			, DrawFlags(ESlateBatchDrawFlag::None)
			, ShaderType(ESlateShader::Default)
			, DrawPrimitiveType(ESlateDrawPrimitive::TriangleList)
			, DrawEffects(ESlateDrawEffect::None)
			, ClippingIndex(InClippingIndex)
			, InstanceCount(0)
			, InstanceOffset(0)
			, SceneIndex(-1)
		{}

		FBatchKey(TSharedPtr<FSlateDrawLayerHandle, ESPMode::ThreadSafe> InLayerHandle, const int32 InClippingIndex)
			: LayerHandle(InLayerHandle)
			, ShaderParams()
			, DrawFlags(ESlateBatchDrawFlag::None)
			, ShaderType(ESlateShader::Default)
			, DrawPrimitiveType(ESlateDrawPrimitive::TriangleList)
			, DrawEffects(ESlateDrawEffect::None)
			, ClippingIndex(InClippingIndex)
			, InstanceCount(0)
			, InstanceOffset(0)
			, SceneIndex(-1)
		{}

		bool operator==( const FBatchKey& Other ) const
		{
			return DrawFlags == Other.DrawFlags
				&& ShaderType == Other.ShaderType
				&& DrawPrimitiveType == Other.DrawPrimitiveType
				&& DrawEffects == Other.DrawEffects
				&& ShaderParams == Other.ShaderParams
				&& ClippingIndex == Other.ClippingIndex
				&& CustomDrawer == Other.CustomDrawer
				&& CachedRenderHandle == Other.CachedRenderHandle
				&& LayerHandle == Other.LayerHandle
				&& InstanceCount == Other.InstanceCount
				&& InstanceOffset == Other.InstanceOffset
				&& InstanceData == Other.InstanceData
				&& SceneIndex == Other.SceneIndex;
		}
	};

	/** A secondary key which represents elements needed to make a batch unique */
	FBatchKey BatchKey;

	/** Shader resource to use with this batch.  Used as a primary key.  No batch can have multiple textures */
	const FSlateShaderResource* ShaderResource;

public:
	/** Number of elements in the batch */
	uint32 NumElementsInBatch;
	/** Index into an array of vertex arrays where this batches vertices are found (before submitting to the vertex buffer)*/
	int32 VertexArrayIndex;
	/** Index into an array of index arrays where this batches indices are found (before submitting to the index buffer) */
	int32 IndexArrayIndex;

	/** The Stored clipping state for the corresponding clipping state index.  The indexes are not directly comparable later, so we need to expand it to the full state to be compared. */
	TOptional<FSlateClippingState> ClippingState;
};

class FSlateRenderBatch
{
public:
	FSlateRenderBatch(uint32 InLayer, const FSlateElementBatch& InBatch, TSharedPtr<FSlateRenderDataHandle, ESPMode::ThreadSafe> InRenderHandle, int32 InNumVertices, int32 InNumIndices, int32 InVertexOffset, int32 InIndexOffset)
		: Layer( InLayer )
		, ShaderParams( InBatch.GetShaderParams() )
		, Texture( InBatch.GetShaderResource() )
		, InstanceData( InBatch.GetInstanceData() )
		, InstanceCount( InBatch.GetInstanceCount() )
		, InstanceOffset(InBatch.GetInstanceOffset() )
		, CustomDrawer( InBatch.GetCustomDrawer() )
		, LayerHandle( InBatch.GetLayerHandle() )
		, CachedRenderHandle( InRenderHandle )
		, DrawFlags( InBatch.GetDrawFlags() )
		, ShaderType( InBatch.GetShaderType() )
		, DrawPrimitiveType( InBatch.GetPrimitiveType() )
		, DrawEffects( InBatch.GetDrawEffects() )
		, ClippingIndex( InBatch.GetClippingIndex() )
		, ClippingState( InBatch.ClippingState )
		, VertexArrayIndex( InBatch.VertexArrayIndex )
		, IndexArrayIndex( InBatch.IndexArrayIndex )
		, VertexOffset( InVertexOffset )
		, IndexOffset( InIndexOffset )
		, NumVertices( InNumVertices )
		, NumIndices( InNumIndices )
		, SceneIndex(InBatch.GetSceneIndex())
	{}

public:
	/** The layer we need to sort by when  */
	const uint32 Layer;

	/** Dynamically modified offset that occurs when we have relative position stored render batches. */
	FVector2D DynamicOffset;

	const FShaderParams ShaderParams;

	/** Texture to use with this batch.  */
	const FSlateShaderResource* Texture;

	const ISlateUpdatableInstanceBuffer* InstanceData;

	const int32 InstanceCount;

	const uint32 InstanceOffset;

	const TWeakPtr<ICustomSlateElement, ESPMode::ThreadSafe> CustomDrawer;

	const TWeakPtr<FSlateDrawLayerHandle, ESPMode::ThreadSafe> LayerHandle;

	const TSharedPtr<FSlateRenderDataHandle, ESPMode::ThreadSafe> CachedRenderHandle;

	const ESlateBatchDrawFlag DrawFlags;	

	const ESlateShader::Type ShaderType;

	const ESlateDrawPrimitive::Type DrawPrimitiveType;

	const ESlateDrawEffect DrawEffects;

	const int32 ClippingIndex;

	/** The Stored clipping state for the corresponding clipping state index.  The indexes are not directly comparable later, so we need to expand it to the full state to be compared. */
	TOptional<FSlateClippingState> ClippingState;
	
	/** Index into the vertex array pool */
	const int32 VertexArrayIndex;
	/** Index into the index array pool */
	const int32 IndexArrayIndex;
	/** How far into the vertex buffer is this batch*/
	const uint32 VertexOffset;
	/** How far into the index buffer this batch is*/
	const uint32 IndexOffset;
	/** Number of vertices in the batch */
	const uint32 NumVertices;
	/** Number of indices in the batch */
	const uint32 NumIndices;

	const int32 SceneIndex;
};


typedef TArray<FSlateElementBatch, TInlineAllocator<2>> FElementBatchArray;

class FElementBatchMap
{
public:
	FElementBatchMap()
		: ResourceVersion(0)
	{
		Reset();
	}

	FORCEINLINE_DEBUGGABLE int32 Num() const { return Layers.Num() + OverflowLayers.Num(); }
	
	FORCEINLINE_DEBUGGABLE TUniqueObj<FElementBatchArray>* Find(uint32 Layer)
	{
		if ( Layer < (uint32)Layers.Num() )
		{
			if ( ActiveLayers[Layer] )
			{
				return &Layers[Layer];
			}

			return nullptr;
		}
		else
		{
			return OverflowLayers.Find(Layer);
		}
	}

	FORCEINLINE_DEBUGGABLE TUniqueObj<FElementBatchArray>& Add(uint32 Layer)
	{
		if ( Layer < (uint32)Layers.Num() )
		{
			MinLayer = FMath::Min(Layer, MinLayer);
			MaxLayer = FMath::Max(Layer, MaxLayer);
			ActiveLayers[Layer] = true;
			return Layers[Layer];
		}
		else
		{
			return OverflowLayers.Add(Layer);
		}
	}

	FORCEINLINE_DEBUGGABLE void Sort()
	{
		OverflowLayers.KeySort(TLess<uint32>());
	}

	template <typename TFunc>
	FORCEINLINE_DEBUGGABLE void ForEachLayer(const TFunc& Process)
	{
		if ( MinLayer < (uint32)Layers.Num() )
		{
			for ( TBitArray<>::FConstIterator BitIt(ActiveLayers, MinLayer); BitIt; ++BitIt )
			{
				if ( BitIt.GetValue() == 0 )
				{
					continue;
				}

				const int32 BitIndex = BitIt.GetIndex();
				FElementBatchArray& ElementBatches = *Layers[BitIndex];

				if ( ElementBatches.Num() > 0 )
				{
					Process(BitIndex, ElementBatches);
				}

				if ( ((uint32)BitIndex) >= MaxLayer )
				{
					break;
				}
			}
		}

		for ( TMap<uint32, TUniqueObj<FElementBatchArray>>::TIterator It(OverflowLayers); It; ++It )
		{
			uint32 Layer = It.Key();
			FElementBatchArray& ElementBatches = *It.Value();

			if ( ElementBatches.Num() > 0 )
			{
				Process(Layer, ElementBatches);
			}
		}
	}

	FORCEINLINE_DEBUGGABLE void UpdateResourceVersion(uint32 NewResourceVersion)
	{
		if (ResourceVersion != NewResourceVersion)
		{
			OverflowLayers.Empty();

			// Since the resource version changed, clean out all cached data in the element batch arrays
			for (int32 LayerIdx = 0; LayerIdx < Layers.Num(); ++LayerIdx)
			{
				Layers[LayerIdx]->Empty();
			}

			MinLayer = UINT_MAX;
			MaxLayer = 0;
			ActiveLayers.Init(false, Layers.Num());

			ResourceVersion = NewResourceVersion;
		}
	}

	FORCEINLINE_DEBUGGABLE void Reset()
	{
		MinLayer = UINT_MAX;
		MaxLayer = 0;
		ActiveLayers.Init(false, Layers.Num());
		OverflowLayers.Reset();
	}

private:
	TBitArray<> ActiveLayers;
	TStaticArray<TUniqueObj<FElementBatchArray>, 256> Layers;
	TMap<uint32, TUniqueObj<FElementBatchArray>> OverflowLayers;
	uint32 MinLayer;
	uint32 MaxLayer;
	uint32 ResourceVersion;
};

#if STATS

class FSlateStatTrackingMemoryAllocator : public FDefaultAllocator
{
public:
	typedef FDefaultAllocator Super;

	class ForAnyElementType : public FDefaultAllocator::ForAnyElementType
	{
	public:
		typedef FDefaultAllocator::ForAnyElementType Super;

		ForAnyElementType()
			: AllocatedSize(0)
		{

		}

		/**
		 * Moves the state of another allocator into this one.
		 * Assumes that the allocator is currently empty, i.e. memory may be allocated but any existing elements have already been destructed (if necessary).
		 * @param Other - The allocator to move the state from.  This allocator should be left in a valid empty state.
		 */
		FORCEINLINE void MoveToEmpty(ForAnyElementType& Other)
		{
			Super::MoveToEmpty(Other);

			AllocatedSize = Other.AllocatedSize;
			Other.AllocatedSize = 0;
		}

		/** Destructor. */
		~ForAnyElementType()
		{
			if(AllocatedSize)
			{
				DEC_DWORD_STAT_BY(STAT_SlateBufferPoolMemory, AllocatedSize);
			}
		}

		void ResizeAllocation(int32 PreviousNumElements, int32 NumElements, int32 NumBytesPerElement)
		{
			const int32 NewSize = NumElements * NumBytesPerElement;
			INC_DWORD_STAT_BY(STAT_SlateBufferPoolMemory, NewSize - AllocatedSize);
			AllocatedSize = NewSize;

			Super::ResizeAllocation(PreviousNumElements, NumElements, NumBytesPerElement);
		}

	private:
		ForAnyElementType(const ForAnyElementType&);
		ForAnyElementType& operator=(const ForAnyElementType&);
	private:
		int32 AllocatedSize;
	};
};

template <>
struct TAllocatorTraits<FSlateStatTrackingMemoryAllocator> : TAllocatorTraitsBase<FSlateStatTrackingMemoryAllocator>
{
	enum { SupportsMove    = TAllocatorTraits<FDefaultAllocator>::SupportsMove    };
	enum { IsZeroConstruct = TAllocatorTraits<FDefaultAllocator>::IsZeroConstruct };
};

typedef TArray<FSlateVertex, FSlateStatTrackingMemoryAllocator> FSlateVertexArray;
typedef TArray<SlateIndex, FSlateStatTrackingMemoryAllocator> FSlateIndexArray;

#else

typedef TArray<FSlateVertex> FSlateVertexArray;
typedef TArray<SlateIndex> FSlateIndexArray;

#endif


class FSlateBatchData
{
public:
	FSlateBatchData()
		: NumBatchedVertices(0)
		, NumBatchedIndices(0)
		, NumLayers(0)
		, bIsStencilBufferRequired(false)
	{}

	void Reset();

	/**
	 * Returns a list of element batches for this window
	 */
	const TArray<FSlateRenderBatch>& GetRenderBatches() const { return RenderBatches; }

	/**
	 * 
	 */
	SLATECORE_API bool IsStencilClippingRequired() const;

	void DetermineIsStencilClippingRequired(const TArray<FSlateClippingState>& ClippingStates);

	/**
	 * Assigns a vertex array from the pool which is appropriate for the batch.  Creates a new array if needed
	 */
	void AssignVertexArrayToBatch( FSlateElementBatch& Batch );

	/**
	 * Assigns an index array from the pool which is appropriate for the batch.  Creates a new array if needed
	 */
	void AssignIndexArrayToBatch( FSlateElementBatch& Batch );

	/** @return the list of vertices for a batch */
	FSlateVertexArray& GetBatchVertexList( FSlateElementBatch& Batch ) { return BatchVertexArrays[Batch.VertexArrayIndex]; }

	/** @return the list of indices for a batch */
	FSlateIndexArray& GetBatchIndexList( FSlateElementBatch& Batch ) { return BatchIndexArrays[Batch.IndexArrayIndex]; }

	/** @return The total number of batched vertices */
	int32 GetNumBatchedVertices() const { return NumBatchedVertices; }

	/** @return The total number of batched indices */
	int32 GetNumBatchedIndices() const { return NumBatchedIndices; }

	/** @return Total number of batched layers */
	int32 GetNumLayers() const { return NumLayers; }

	/** Set the associated vertex/index buffer handle. */
	void SetRenderDataHandle(TSharedPtr<FSlateRenderDataHandle, ESPMode::ThreadSafe> InRenderDataHandle) { RenderDataHandle = InRenderDataHandle; }

	/** 
	 * Fills batch data into the actual vertex and index buffer
	 *
	 * @param VertexBuffer	Pointer to the actual memory for the vertex buffer 
	 * @param IndexBuffer	Pointer to the actual memory for an index buffer
	 * @param bAbsoluteIndices	Whether to write absolute indices (simplifies draw call setup on RHIs that do not support BaseVertex)
	 */
	SLATECORE_API void FillVertexAndIndexBuffer(uint8* VertexBuffer, uint8* IndexBuffer, bool bAbsoluteIndices);

	/** 
	 * Creates rendering data from batched elements
	 */
	SLATECORE_API void CreateRenderBatches(FElementBatchMap& LayerToElementBatches);

private:
	/**  */
	void Merge(FElementBatchMap& InLayerToElementBatches, uint32& VertexOffset, uint32& IndexOffset);

	/** */
	void AddRenderBatch(uint32 InLayer, const FSlateElementBatch& InElementBatch, int32 InNumVertices, int32 InNumIndices, int32 InVertexOffset, int32 InIndexOffset);

	/**
	 * Resets an array from the pool of vertex arrays
	 * This will empty the array and give it a reasonable starting memory amount for when it is reused
	 */
	void ResetVertexArray(FSlateVertexArray& InOutVertexArray);

	/**
	* Resets an array from the pool of index arrays
	* This will empty the array and give it a reasonable starting memory amount for when it is reused
	*/
	void ResetIndexArray(FSlateIndexArray& InOutIndexArray);

private:

	// The associated render data handle if these render batches are not in the default vertex/index buffer
	TSharedPtr<FSlateRenderDataHandle, ESPMode::ThreadSafe> RenderDataHandle;

	// Array of vertex lists that are currently free (have no elements in them).
	TArray<uint32> VertexArrayFreeList;

	// Array of index lists that are currently free (have no elements in them).
	TArray<uint32> IndexArrayFreeList;

	// Array of vertex lists for batching vertices. We use this method for quickly resetting the arrays without deleting memory.
	TArray<FSlateVertexArray> BatchVertexArrays;

	// Array of vertex lists for batching indices. We use this method for quickly resetting the arrays without deleting memory.
	TArray<FSlateIndexArray> BatchIndexArrays;

	/** List of element batches sorted by later for use in rendering (for threaded renderers, can only be accessed from the render thread)*/
	TArray<FSlateRenderBatch> RenderBatches;

	/**  */
	int32 NumBatchedVertices;

	/**  */
	int32 NumBatchedIndices;

	/** */
	int32 NumLayers;

	/** */
	bool bIsStencilBufferRequired;
};

class FSlateRenderer;

/**
 * The draw layer represents a logical draw layer.  Since some drawn areas mights be from cached
 * buffers we can't depend on layer id to sort widget draw buffers from different frames being combined
 * together.
 */
class FSlateDrawLayer
{
public:
	FElementBatchMap& GetElementBatchMap() { return LayerToElementBatches; }

	void ResetLayer()
	{
		DrawElements.Reset();
	}

	FORCEINLINE int32 GetElementCount() const
	{
		return DrawElements.Num();
	}

	/** Apply Function to each draw element */ 
	void ForEachElement(const TFunction<void(FSlateDrawElement&)>& InFunction)
	{
		for (FSlateDrawElement& Element : DrawElements)
		{
			InFunction(Element);
		}
	}

public:
	// Element batch maps sorted by layer.
	FElementBatchMap LayerToElementBatches;

	/** The elements drawn on this layer */
	TArray<FSlateDrawElement> DrawElements;
};

/**
 * 
 */
class FSlateDrawLayerHandle : public TSharedFromThis < FSlateDrawLayerHandle, ESPMode::ThreadSafe >
{
public:
	FSlateDrawLayerHandle()
		: BatchMap(nullptr)
	{
	}

	FElementBatchMap* BatchMap;
};


/**
 * Represents a top level window and its draw elements.
 */
class FSlateWindowElementList : public FNoncopyable
{
	friend class FSlateElementBatcher;
	friend class FSlateDrawElement;
	friend class FSlateDataPayload;

public:
	/** 
	 * Construct a new list of elements with which to paint a window.
	 *
	 * @param InPaintWindow		The window that owns the widgets being painted.  This is almost most always the same window that is being rendered to
	 * @param InRenderWindow	The window that we will be rendering to.
	 */
	SLATECORE_API explicit FSlateWindowElementList(const TSharedPtr<SWindow>& InPaintWindow = TSharedPtr<SWindow>());

	SLATECORE_API ~FSlateWindowElementList();

	/** @return Get the window that we will be painting */
	UE_DEPRECATED(4.21, "FSlateWindowElementList::GetWindow is not thread safe but window element lists are accessed on multiple threads.  Please call GetPaintWindow instead")
	FORCEINLINE TSharedPtr<SWindow> GetWindow() const
	{
		// check that we are in game thread or are in slate/movie loading thread
		check(IsInGameThread() || IsInSlateThread());
		return WeakPaintWindow.Pin();
	}

	/** @return Get the window that we will be painting */
	FORCEINLINE SWindow* GetPaintWindow() const
	{
		check(IsInGameThread() || IsInSlateThread());
		return WeakPaintWindow.IsValid() ? RawPaintWindow : nullptr;
	}

	/** @return Get the window that we will be rendering to.  Unless you are a slate renderer you probably want to use GetPaintWindow() */
	FORCEINLINE SWindow* GetRenderWindow() const
	{
		check(IsInGameThread() || IsInSlateThread());
		// Note: This assumes that the PaintWindow is safe to pin and is not accessed by another thread
		return RenderTargetWindow != nullptr ? RenderTargetWindow : GetPaintWindow();
	}

	/** @return Get the draw elements that we want to render into this window */
	FORCEINLINE const TArray<FSlateDrawElement>& GetDrawElements() const
	{
		return RootDrawLayer.DrawElements;
	}

	/** @return Get the draw elements that we want to render into this window */
	FORCEINLINE TArray<FSlateDrawElement>& GetDrawElements()
	{
		return RootDrawLayer.DrawElements;
	}

	/** Apply Function to each draw element */ 
	FORCEINLINE void ForEachElement(const TFunction<void(FSlateDrawElement&)>& InFunction)
	{
		RootDrawLayer.ForEachElement(InFunction);
	}


	/** @return the total number of elements that have been registered to be drawn. */
	SLATECORE_API int32 GetElementCount() const;

	/**
	 * Add a draw element to the list
	 *
	 * @param InDrawElement  The draw element to add
	 */
	FORCEINLINE void AddItem(const FSlateDrawElement& InDrawElement)
	{
		TArray<FSlateDrawElement>& ActiveDrawElements = DrawStack.Last()->DrawElements;
		ActiveDrawElements.Add(InDrawElement);
	}
	
	void AppendItems(FSlateWindowElementList* Other);


	/** @return Get the window size that we will be painting */
	FORCEINLINE FVector2D GetWindowSize() const
	{
		return WindowSize;
	}

	/**
	 * Creates an uninitialized draw element
	 */
	FORCEINLINE FSlateDrawElement& AddUninitialized()
	{
		TArray<FSlateDrawElement>& Elements = DrawStack.Last()->DrawElements;
		const int32 InsertIdx = Elements.AddDefaulted();

#if WITH_SLATE_DEBUGGING
		FSlateDebugging::ElementAdded.Broadcast(*this, InsertIdx);
#endif

		return Elements[InsertIdx];
	}

	SLATECORE_API void MergeElementList(FSlateWindowElementList* ElementList, FVector2D AbsoluteOffset);

	SLATECORE_API void MergeResources(const TArray<UObject*>& AssociatedResources);

	//--------------------------------------------------------------------------
	// CLIPPING
	//--------------------------------------------------------------------------

	SLATECORE_API void PushClip(const FSlateClippingZone& InClipZone);
	SLATECORE_API int32 GetClippingIndex() const;
	SLATECORE_API TOptional<FSlateClippingState> GetClippingState() const;
	SLATECORE_API void PopClip();

	FSlateClippingManager& GetClippingManager() { return ClippingManager; }
	const FSlateClippingManager& GetClippingManager() const { return ClippingManager; }

	//--------------------------------------------------------------------------
	// DEFERRED PAINTING
	//--------------------------------------------------------------------------

	/**
	 * Some widgets may want to paint their children after after another, loosely-related widget finished painting.
	 * Or they may want to paint "after everyone".
	 */
	struct SLATECORE_API FDeferredPaint
	{
	public:
		FDeferredPaint( const TSharedRef<const SWidget>& InWidgetToPaint, const FPaintArgs& InArgs, const FGeometry InAllottedGeometry, const FWidgetStyle& InWidgetStyle, bool InParentEnabled );

		int32 ExecutePaint( int32 LayerId, FSlateWindowElementList& OutDrawElements, const FSlateRect& MyCullingRect ) const;

		FDeferredPaint Copy(const FPaintArgs& InArgs);

	private:
		// Used for making copies.
		FDeferredPaint(const FDeferredPaint& Copy, const FPaintArgs& InArgs);

		const TWeakPtr<const SWidget> WidgetToPaintPtr;
		const FPaintArgs Args;
		const FGeometry AllottedGeometry;
		const FWidgetStyle WidgetStyle;
		const bool bParentEnabled;
	};

	SLATECORE_API void QueueDeferredPainting( const FDeferredPaint& InDeferredPaint );

	int32 PaintDeferred(int32 LayerId, const FSlateRect& MyCullingRect);

	bool ShouldResolveDeferred() const { return bNeedsDeferredResolve; }

	SLATECORE_API void BeginDeferredGroup();
	SLATECORE_API void EndDeferredGroup();

	TArray< TSharedPtr<FDeferredPaint> > GetDeferredPaintList() const { return DeferredPaintList; }

	//--------------------------------------------------------------------------
	// VOLATILE PAINTING
	//--------------------------------------------------------------------------

	struct FVolatilePaint
	{
	public:
		SLATECORE_API FVolatilePaint(const TSharedRef<const SWidget>& InWidgetToPaint, const FPaintArgs& InArgs, const FGeometry InAllottedGeometry, const FSlateRect InMyCullingRect, const TOptional<FSlateClippingState>& ClippingState, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool InParentEnabled);

		int32 ExecutePaint(FSlateWindowElementList& OutDrawElements, double InCurrentTime, float InDeltaTime, const FVector2D& InDynamicOffset) const;

		FORCEINLINE const SWidget* GetWidget() const { return WidgetToPaintPtr.Pin().Get(); }
		FORCEINLINE FGeometry GetGeometry() const { return AllottedGeometry; }
		FORCEINLINE int32 GetLayerId() const { return LayerId; }

	public:
		TSharedPtr< FSlateDrawLayerHandle, ESPMode::ThreadSafe > LayerHandle;
	 
	private:
		const TWeakPtr<const SWidget> WidgetToPaintPtr;
		const FPaintArgs Args;
		const FGeometry AllottedGeometry;
		const FSlateRect MyCullingRect;
		const TOptional<FSlateClippingState> ClippingState;
		const int32 LayerId;
		const FWidgetStyle WidgetStyle;
		const bool bParentEnabled;
	};

	SLATECORE_API void QueueVolatilePainting( const FVolatilePaint& InVolatilePaint );

	SLATECORE_API int32 PaintVolatile(FSlateWindowElementList& OutElementList, double InCurrentTime, float InDeltaTime, const FVector2D& InDynamicOffset);
	SLATECORE_API int32 PaintVolatileRootLayer(FSlateWindowElementList& OutElementList, double InCurrentTime, float InDeltaTime, const FVector2D& InDynamicOffset);

	SLATECORE_API void BeginLogicalLayer(const TSharedPtr<FSlateDrawLayerHandle, ESPMode::ThreadSafe>& LayerHandle);
	SLATECORE_API void EndLogicalLayer();

	SLATECORE_API const TArray< TSharedPtr<FVolatilePaint> >& GetVolatileElements() const { return VolatilePaintList; }

	//--------------------------------------------------------------------------
	// OTHER
	//--------------------------------------------------------------------------
	
	/**
	 * Remove all the elements from this draw list.
	 */
	SLATECORE_API void ResetElementBuffers();

	/** Allows manual hinting of reporting of references. */
	SLATECORE_API void SetShouldReportReferencesToGC(bool bInReportReferences);

	/** Will UObject references be reported to the GC? */
	SLATECORE_API bool ShouldReportUObjectReferences() const;

	/**
	 * Allocate memory that remains valid until ResetBuffers is called.
	 */
	FORCEINLINE_DEBUGGABLE void* Alloc(int32 AllocSize, int32 Alignment = MIN_ALIGNMENT)
	{
		return MemManager.Alloc(AllocSize, Alignment);
	}

	/**
	 * Allocate memory for a type that remains valid until ResetBuffers is called.
	 */
	template <typename T>
	FORCEINLINE_DEBUGGABLE void* Alloc()
	{
		return MemManager.Alloc(sizeof(T), alignof(T));
	}

	FSlateBatchData& GetBatchData() { return BatchData; }

	FSlateDrawLayer& GetRootDrawLayer() { return RootDrawLayer; }

	TMap < TSharedPtr<FSlateDrawLayerHandle, ESPMode::ThreadSafe>, TSharedPtr<FSlateDrawLayer> >& GetChildDrawLayers() { return DrawLayers; }

	/**
	 * Caches this element list on the renderer, generating all needed index and vertex buffers.
	 */
	SLATECORE_API TSharedRef<FSlateRenderDataHandle, ESPMode::ThreadSafe> CacheRenderData(const ILayoutCache* Cacher);

	SLATECORE_API TSharedPtr<FSlateRenderDataHandle, ESPMode::ThreadSafe> GetCachedRenderDataHandle() const
	{
		return CachedRenderDataHandle.Pin();
	}

	void BeginUsingCachedBuffer(TSharedPtr<FSlateRenderDataHandle, ESPMode::ThreadSafe>& InCachedRenderDataHandle)
	{
		InCachedRenderDataHandle->BeginUsing();
		CachedRenderHandlesInUse.Add(InCachedRenderDataHandle);
	}

	SLATECORE_API bool IsCachedRenderDataInUse() const
	{
		TSharedPtr<FSlateRenderDataHandle, ESPMode::ThreadSafe> SafeHandle = CachedRenderDataHandle.Pin();
		return SafeHandle.IsValid() && SafeHandle->IsInUse();
	}

	SLATECORE_API void PreDraw_ParallelThread();

	SLATECORE_API void PostDraw_ParallelThread();

	SLATECORE_API void PostDraw_NonParallelRenderer();

	SLATECORE_API void SetRenderTargetWindow(SWindow* InRenderTargetWindow);

	SLATECORE_API void AddReferencedObjects(FReferenceCollector& Collector);

private:
	/** Resources to report to the garbage collector */
	TArray<UObject*> ResourcesToReport;

	/**
	* Window which owns the widgets that are being painted but not necessarily rendered to
	* Widgets are always rendered to the RenderTargetWindow
	*/
	TWeakPtr<SWindow> WeakPaintWindow;
	SWindow* RawPaintWindow;

	/**
	 * The window to render to.  This may be different from the paint window if we are displaying the contents of a window (or virtual window) onto another window
	 * The primary use case of this is thread safe rendering of widgets during times when the main thread is blocked (e.g loading movies)
	 * If this is null, the paint window is used
 	 */
	SWindow* RenderTargetWindow;

	/** Batched data used for rendering */
	FSlateBatchData BatchData;

	/** The base draw layer/context. */
	FSlateDrawLayer RootDrawLayer;

	/**  */
	FSlateClippingManager ClippingManager;

	/** */
	TMap< TSharedPtr<FSlateDrawLayerHandle, ESPMode::ThreadSafe>, TSharedPtr<FSlateDrawLayer> > DrawLayers;

	/** */
	TArray< TSharedPtr<FSlateDrawLayer> > DrawLayerPool;

	/** */
	TArray< FSlateDrawLayer* > DrawStack;

	/** */
	TArray< TSharedPtr<FSlateRenderDataHandle, ESPMode::ThreadSafe> > CachedRenderHandlesInUse;

	/**
	 * Some widgets want their logical children to appear at a different "layer" in the physical hierarchy.
	 * We accomplish this by deferring their painting.
	 */
	TArray< TSharedPtr<FDeferredPaint> > DeferredPaintList;

	bool bNeedsDeferredResolve;
	TArray<int32> ResolveToDeferredIndex;

	/** The widgets be cached for a later paint pass when the invalidation host paints. */
	TArray< TSharedPtr<FVolatilePaint> > VolatilePaintList;

	/**
	 * Handle to the cached render data associated with this element list.  Will only exist if 
	 * this element list is being used for invalidation / caching.
	 */
	mutable TWeakPtr<FSlateRenderDataHandle, ESPMode::ThreadSafe> CachedRenderDataHandle;

	// Mem stack for temp allocations
	FMemStackBase MemManager; 

	/** Store the size of the window being used to paint */
	FVector2D WindowSize;

	/** Should this element list report any references it knows about.  Defaults to true, once drawn it turns off on the rendering thread. */
	bool bReportReferences;

private:

	class FWindowElementGCObject : public FGCObject
	{
	public:
		FWindowElementGCObject(FSlateWindowElementList* InOwner);

		void ClearOwner();

		virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
		virtual FString GetReferencerName() const override;

	private:
		FSlateWindowElementList* Owner;
	};

	/** This keeps drawn elements alive in case the UObject stops being referenced elsewhere that we need. */
	TUniquePtr<FWindowElementGCObject> ResourceGCRoot;
};
