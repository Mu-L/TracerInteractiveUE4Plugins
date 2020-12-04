// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	RendererInterface.h: Renderer interface definition.
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "Templates/RefCounting.h"
#include "Misc/MemStack.h"
#include "Modules/ModuleInterface.h"
#include "RHI.h"
#include "RenderResource.h"
#include "RenderUtils.h"
#include "Misc/EnumClassFlags.h"
#include "UniformBuffer.h"
#include "VirtualTexturing.h"

class FCanvas;
class FMaterial;
class FSceneInterface;
class FSceneRenderTargets;
class FSceneView;
class FSceneViewFamily;
class FSceneTextureUniformParameters;
class FGlobalDistanceFieldParameterData;
struct FMeshBatch;
struct FSynthBenchmarkResults;
class FShader;
class FShaderMapPointerTable;
class FRDGBuilder;
template<typename ShaderType, typename PointerTableType> class TShaderRefBase;

// Shortcut for the allocator used by scene rendering.
typedef TMemStackAllocator<> SceneRenderingAllocator;

class SceneRenderingBitArrayAllocator
	: public TInlineAllocator<4,SceneRenderingAllocator>
{
};

class SceneRenderingSparseArrayAllocator
	: public TSparseArrayAllocator<SceneRenderingAllocator,SceneRenderingBitArrayAllocator>
{
};

class SceneRenderingSetAllocator
	: public TSetAllocator<SceneRenderingSparseArrayAllocator,TInlineAllocator<1,SceneRenderingAllocator> >
{
};

/** All necessary data to create a render target from the pooled render targets. */
struct FPooledRenderTargetDesc
{
public:

	/** Default constructor, use one of the factory functions below to make a valid description */
	FPooledRenderTargetDesc()
		: PackedBits(0)
	{
		AutoWritable = true;
		check(!IsValid());
	}

	/**
	 * Factory function to create 2D texture description
	 * @param InFlags bit mask combined from elements of ETextureCreateFlags e.g. TexCreate_UAV
	 */
	static FPooledRenderTargetDesc Create2DDesc(
		FIntPoint InExtent,
		EPixelFormat InFormat,
		const FClearValueBinding& InClearValue,
		ETextureCreateFlags InFlags,
		ETextureCreateFlags InTargetableFlags,
		bool bInForceSeparateTargetAndShaderResource,
		uint16 InNumMips = 1,
		bool InAutowritable = true,
		bool InCreateRTWriteMask = false,
		bool InCreateFmask = false)
	{
		check(InExtent.X);
		check(InExtent.Y);

		FPooledRenderTargetDesc NewDesc;
		NewDesc.ClearValue = InClearValue;
		NewDesc.Extent = InExtent;
		NewDesc.Depth = 0;
		NewDesc.ArraySize = 1;
		NewDesc.bIsArray = false;
		NewDesc.bIsCubemap = false;
		NewDesc.NumMips = InNumMips;
		NewDesc.NumSamples = 1;
		NewDesc.Format = InFormat;
		NewDesc.Flags = InFlags;
		NewDesc.TargetableFlags = InTargetableFlags;
		NewDesc.bForceSeparateTargetAndShaderResource = bInForceSeparateTargetAndShaderResource;
		NewDesc.DebugName = TEXT("UnknownTexture2D");
		NewDesc.AutoWritable = InAutowritable;
		NewDesc.bCreateRenderTargetWriteMask = InCreateRTWriteMask;
		NewDesc.bCreateRenderTargetFmask = InCreateFmask;
		check(NewDesc.Is2DTexture());
		return NewDesc;
	}

	/**
	 * Factory function to create 3D texture description
	 * @param InFlags bit mask combined from elements of ETextureCreateFlags e.g. TexCreate_UAV
	 */
	static FPooledRenderTargetDesc CreateVolumeDesc(
		uint32 InSizeX,
		uint32 InSizeY,
		uint32 InSizeZ,
		EPixelFormat InFormat,
		const FClearValueBinding& InClearValue,
		ETextureCreateFlags InFlags,
		ETextureCreateFlags InTargetableFlags,
		bool bInForceSeparateTargetAndShaderResource,
		uint16 InNumMips = 1,
		bool InAutowritable = true)
	{
		check(InSizeX);
		check(InSizeY);

		FPooledRenderTargetDesc NewDesc;
		NewDesc.ClearValue = InClearValue;
		NewDesc.Extent = FIntPoint(InSizeX,InSizeY);
		NewDesc.Depth = InSizeZ;
		NewDesc.ArraySize = 1;
		NewDesc.bIsArray = false;
		NewDesc.bIsCubemap = false;
		NewDesc.NumMips = InNumMips;
		NewDesc.NumSamples = 1;
		NewDesc.Format = InFormat;
		NewDesc.Flags = InFlags;
		NewDesc.TargetableFlags = InTargetableFlags;
		NewDesc.bForceSeparateTargetAndShaderResource = bInForceSeparateTargetAndShaderResource;
		NewDesc.DebugName = TEXT("UnknownTextureVolume");
		NewDesc.AutoWritable = InAutowritable;
		check(NewDesc.Is3DTexture());
		return NewDesc;
	}

	/**
	 * Factory function to create cube map texture description
	 * @param InFlags bit mask combined from elements of ETextureCreateFlags e.g. TexCreate_UAV
	 */
	static FPooledRenderTargetDesc CreateCubemapDesc(
		uint32 InExtent,
		EPixelFormat InFormat,
		const FClearValueBinding& InClearValue,
		ETextureCreateFlags InFlags,
		ETextureCreateFlags InTargetableFlags,
		bool bInForceSeparateTargetAndShaderResource,
		uint32 InArraySize = 1,
		uint16 InNumMips = 1,
		bool InAutowritable = true)
	{
		check(InExtent);

		FPooledRenderTargetDesc NewDesc;
		NewDesc.ClearValue = InClearValue;
		NewDesc.Extent = FIntPoint(InExtent, InExtent);
		NewDesc.Depth = 0;
		NewDesc.ArraySize = InArraySize;
		// Note: this doesn't allow an array of size 1
		NewDesc.bIsArray = InArraySize > 1;
		NewDesc.bIsCubemap = true;
		NewDesc.NumMips = InNumMips;
		NewDesc.NumSamples = 1;
		NewDesc.Format = InFormat;
		NewDesc.Flags = InFlags;
		NewDesc.TargetableFlags = InTargetableFlags;
		NewDesc.bForceSeparateTargetAndShaderResource = bInForceSeparateTargetAndShaderResource;
		NewDesc.DebugName = TEXT("UnknownTextureCube");
		NewDesc.AutoWritable = InAutowritable;
		check(NewDesc.IsCubemap());

		return NewDesc;
	}

	/**
	 * Factory function to create cube map array texture description
	 * @param InFlags bit mask combined from elements of ETextureCreateFlags e.g. TexCreate_UAV
	 */
	static FPooledRenderTargetDesc CreateCubemapArrayDesc(
		uint32 InExtent,
		EPixelFormat InFormat,
		const FClearValueBinding& InClearValue,
		ETextureCreateFlags InFlags,
		ETextureCreateFlags InTargetableFlags,
		bool bInForceSeparateTargetAndShaderResource,
		uint32 InArraySize,
		uint16 InNumMips = 1,
		bool InAutowritable = true)
	{
		check(InExtent);

		FPooledRenderTargetDesc NewDesc;
		NewDesc.ClearValue = InClearValue;
		NewDesc.Extent = FIntPoint(InExtent, InExtent);
		NewDesc.Depth = 0;
		NewDesc.ArraySize = InArraySize;
		NewDesc.bIsArray = true;
		NewDesc.bIsCubemap = true;
		NewDesc.NumMips = InNumMips;
		NewDesc.NumSamples = 1;
		NewDesc.Format = InFormat;
		NewDesc.Flags = InFlags;
		NewDesc.TargetableFlags = InTargetableFlags;
		NewDesc.bForceSeparateTargetAndShaderResource = bInForceSeparateTargetAndShaderResource;
		NewDesc.DebugName = TEXT("UnknownTextureCubeArray");
		NewDesc.AutoWritable = InAutowritable;
		check(NewDesc.IsCubemap());

		return NewDesc;
	}

	/** Comparison operator to test if a render target can be reused */
	bool Compare(const FPooledRenderTargetDesc& rhs, bool bExact) const
	{
		auto LhsFlags = Flags;
		auto RhsFlags = rhs.Flags;

		if (!bExact || !FPlatformMemory::SupportsFastVRAMMemory())
		{
			LhsFlags &= (~TexCreate_FastVRAM);
			RhsFlags &= (~TexCreate_FastVRAM);
		}
		
		return ClearValue == rhs.ClearValue
			&& LhsFlags == RhsFlags
			&& TargetableFlags == rhs.TargetableFlags
			&& Format == rhs.Format
			&& Extent == rhs.Extent
			&& Depth == rhs.Depth
			&& ArraySize == rhs.ArraySize
			&& NumMips == rhs.NumMips
			&& NumSamples == rhs.NumSamples
			&& PackedBits == rhs.PackedBits;
	}

	bool IsCubemap() const
	{
		return bIsCubemap;
	}

	bool Is2DTexture() const
	{
		return Extent.X != 0 && Extent.Y != 0 && Depth == 0 && !bIsCubemap;
	}

	bool Is3DTexture() const
	{
		return Extent.X != 0 && Extent.Y != 0 && Depth != 0 && !bIsCubemap;
	}

	// @return true if this texture is a texture array
	bool IsArray() const
	{
		return bIsArray;
	}

	bool IsValid() const
	{
		if(NumSamples != 1)
		{
			if(NumSamples < 1 || NumSamples > 8)
			{
				// D3D11 limitations
				return false;
			}

			if(!Is2DTexture())
			{
				return false;
			}
		}

		return Extent.X != 0 && NumMips != 0 && NumSamples >=1 && NumSamples <=16 && Format != PF_Unknown
			&& ((TargetableFlags & TexCreate_UAV) == 0 || GMaxRHIFeatureLevel == ERHIFeatureLevel::SM5 || GMaxRHIFeatureLevel == ERHIFeatureLevel::ES3_1);
	}

	FIntVector GetSize() const
	{
		return FIntVector(Extent.X, Extent.Y, Depth);
	}

	/** 
	 * for debugging purpose
	 * @return e.g. (2D 128x64 PF_R8)
	 */
	FString GenerateInfoString() const
	{
		const TCHAR* FormatString = GetPixelFormatString(Format);

		FString FlagsString = TEXT("");

		ETextureCreateFlags LocalFlags = Flags | TargetableFlags;

		if(LocalFlags & TexCreate_RenderTargetable)
		{
			FlagsString += TEXT(" RT");
		}
		if(LocalFlags & TexCreate_SRGB)
		{
			FlagsString += TEXT(" sRGB");
		}
		if(NumSamples > 1)
		{
			FlagsString += FString::Printf(TEXT(" %dxMSAA"), NumSamples);
		}
		if(LocalFlags & TexCreate_UAV)
		{
			FlagsString += TEXT(" UAV");
		}

		if(LocalFlags & TexCreate_FastVRAM)
		{
			FlagsString += TEXT(" VRam");
		}

		if (LocalFlags & TexCreate_Transient)
		{
			FlagsString += TEXT(" Transient");
		}

		FString ArrayString;

		if(IsArray())
		{
			ArrayString = FString::Printf(TEXT("[%d]"), ArraySize);
		}

		if(Is2DTexture())
		{
			return FString::Printf(TEXT("(2D%s %dx%d %s%s)"), *ArrayString, Extent.X, Extent.Y, FormatString, *FlagsString);
		}
		else if(Is3DTexture())
		{
			return FString::Printf(TEXT("(3D%s %dx%dx%d %s%s)"), *ArrayString, Extent.X, Extent.Y, Depth, FormatString, *FlagsString);
		}
		else if(IsCubemap())
		{
			return FString::Printf(TEXT("(Cube%s %d %s%s)"), *ArrayString, Extent.X, FormatString, *FlagsString);
		}
		else
		{
			return TEXT("(INVALID)");
		}
	}

	// useful when compositing graph takes an input as output format
	void Reset()
	{
		// Usually we don't want to propagate MSAA samples.
		NumSamples = 1;

		bForceSeparateTargetAndShaderResource = false;
		bForceSharedTargetAndShaderResource = false;
		AutoWritable = true;

		// Remove UAV flag for rendertargets that don't need it (some formats are incompatible)
		TargetableFlags |= TexCreate_RenderTargetable;
		TargetableFlags &= (~TexCreate_UAV);
	}

	/** only set a pointer to memory that never gets released */
	const TCHAR* DebugName = TEXT("UnknownTexture");
	/** Value allowed for fast clears for this target. */
	FClearValueBinding ClearValue;
	/** The flags that must be set on both the shader-resource and the targetable texture. bit mask combined from elements of ETextureCreateFlags e.g. TexCreate_UAV */
	ETextureCreateFlags Flags = TexCreate_None;
	/** The flags that must be set on the targetable texture. bit mask combined from elements of ETextureCreateFlags e.g. TexCreate_UAV */
	ETextureCreateFlags TargetableFlags = TexCreate_None;
	/** Texture format e.g. PF_B8G8R8A8 */
	EPixelFormat Format = PF_Unknown;
	/** In pixels, (0,0) if not set, (x,0) for cube maps, todo: make 3d int vector for volume textures */
	FIntPoint Extent = FIntPoint::ZeroValue;
	/** 0, unless it's texture array or volume texture */
	uint16 Depth = 0;
	/** >1 if a texture array should be used (not supported on DX9) */
	uint16 ArraySize = 1;
	/** Number of mips */
	uint8 NumMips = 0;
	/** Number of MSAA samples, default: 1  */
	uint8 NumSamples = 1;

	union
	{
		struct
		{
			/** true if an array texture. Note that ArraySize still can be 1 */
			uint8 bIsArray : 1;
			/** true if a cubemap texture */
			uint8 bIsCubemap : 1;
			/** Whether the shader-resource and targetable texture must be separate textures. */
			uint8 bForceSeparateTargetAndShaderResource : 1;
			/** Whether the shader-resource and targetable texture must be the same resource. */
			uint8 bForceSharedTargetAndShaderResource : 1;
			/** automatically set to writable via barrier during */
			uint8 AutoWritable : 1;
			/** create render target write mask (supported only on specific platforms) */
			uint8 bCreateRenderTargetWriteMask : 1;
			/** create render target fmask (supported only on specific platforms) */
			uint8 bCreateRenderTargetFmask : 1;
			/** Unused flag. */
			uint8 bReserved0 : 1;
		};
		uint8 PackedBits;
	};
};

/** Enum to select between the two RHI textures on a pooled render target. */
enum class ERenderTargetTexture : uint8
{
	/** Maps to the targetable RHI texture on a pooled render target item. */
	Targetable,

	/** Maps to the shader resource RHI texture on a pooled render target item. */
	ShaderResource
};

/**
 * Single render target item consists of a render surface and its resolve texture, Render thread side
 */
struct FSceneRenderTargetItem
{
	/** default constructor */
	FSceneRenderTargetItem() {}

	/** constructor */
	FSceneRenderTargetItem(FRHITexture* InTargetableTexture, FRHITexture* InShaderResourceTexture, FUnorderedAccessViewRHIRef InUAV)
		:	TargetableTexture(InTargetableTexture)
		,	ShaderResourceTexture(InShaderResourceTexture)
		,	UAV(InUAV)
	{}

	void SafeRelease()
	{
		TargetableTexture.SafeRelease();
		ShaderResourceTexture.SafeRelease();
		UAV.SafeRelease();
		RTWriteMaskSRV.SafeRelease();
		FmaskSRV.SafeRelease();
	}

	bool IsValid() const
	{
		return TargetableTexture != 0
			|| ShaderResourceTexture != 0
			|| UAV != 0;
	}

	FRHITexture* GetRHI(ERenderTargetTexture Texture) const
	{
		return Texture == ERenderTargetTexture::Targetable ? TargetableTexture : ShaderResourceTexture;
	}

	/** The 2D or cubemap texture that may be used as a render or depth-stencil target. */
	FTextureRHIRef TargetableTexture;

	/** The 2D or cubemap shader-resource 2D texture that the targetable textures may be resolved to. */
	FTextureRHIRef ShaderResourceTexture;
	
	/** only created if requested through the flag. */
	FUnorderedAccessViewRHIRef UAV;
	
	FShaderResourceViewRHIRef RTWriteMaskSRV;
	FShaderResourceViewRHIRef FmaskSRV;
};

/**
 * Render thread side, use TRefCountPtr<IPooledRenderTarget>, allows sharing and VisualizeTexture
 */
struct IPooledRenderTarget
{
	virtual ~IPooledRenderTarget() {}

	/** Checks if the reference count indicated that the rendertarget is unused and can be reused. */
	virtual bool IsFree() const = 0;
	/** Get all the data that is needed to create the render target. */
	virtual const FPooledRenderTargetDesc& GetDesc() const = 0;
	/** @param InName must not be 0 */
	virtual void SetDebugName(const TCHAR *InName) = 0;
	/**
	 * Only for debugging purpose
	 * @return in bytes
	 **/
	virtual uint32 ComputeMemorySize() const = 0;
	/** Get the low level internals (texture/surface) */
	inline FSceneRenderTargetItem& GetRenderTargetItem() { return RenderTargetItem; }
	/** Get the low level internals (texture/surface) */
	inline const FSceneRenderTargetItem& GetRenderTargetItem() const { return RenderTargetItem; }
	/** Returns if the render target is tracked by a pool. */
	virtual bool IsTracked() const = 0;
	/** Returns true if the render target is compatible with RDG. */
	virtual bool IsCompatibleWithRDG() const { return false; }

	// Refcounting
	virtual uint32 AddRef() const = 0;
	virtual uint32 Release() = 0;
	virtual uint32 GetRefCount() const = 0;

	FORCEINLINE FRHITexture* GetTargetableRHI() const
	{
		return RenderTargetItem.TargetableTexture.GetReference();
	}

	FORCEINLINE FRHITexture* GetShaderResourceRHI() const
	{
		return RenderTargetItem.ShaderResourceTexture.GetReference();
	}

	FORCEINLINE FRHITexture* GetRHI(ERenderTargetTexture Texture) const
	{
		return Texture == ERenderTargetTexture::Targetable ? GetTargetableRHI() : GetShaderResourceRHI();
	}

protected:

	/** The internal references to the created render target */
	FSceneRenderTargetItem RenderTargetItem;
};

/** Creates an untracked pooled render target from an RHI texture. */
extern RENDERCORE_API TRefCountPtr<IPooledRenderTarget> CreateRenderTarget(FRHITexture* Texture, const TCHAR* Name);

/** Creates an untracked pooled render target from the RHI texture, but only if the pooled render target is
 *  empty or doesn't match the input texture. If the pointer already exists and points at the input texture,
 *  the function just returns. Useful to cache a pooled render target for an RHI texture. Returns true if the
 *  render target was created, or false if it was reused.
 */
extern RENDERCORE_API bool CacheRenderTarget(FRHITexture* Texture, const TCHAR* Name, TRefCountPtr<IPooledRenderTarget>& OutPooledRenderTarget);


// use r.DrawDenormalizedQuadMode to override the function call setting (quick way to see if an artifact is caused by this optimization)
enum EDrawRectangleFlags
{
	// Rectangle is created by 2 triangles (diagonal can cause some slightly less efficient shader execution), this is the default as it has no artifacts
	EDRF_Default,
	//
	EDRF_UseTriangleOptimization,
	//
	EDRF_UseTesselatedIndexBuffer
};

class FPostOpaqueRenderParameters
{
public:
	FIntRect ViewportRect;
	FMatrix ViewMatrix;
	FMatrix ProjMatrix;
	FRHITexture2D* DepthTexture = nullptr;
	FRHITexture2D* NormalTexture = nullptr;
	FRHITexture2D* VelocityTexture = nullptr;
	FRHITexture2D* SmallDepthTexture = nullptr;
	FRHICommandListImmediate* RHICmdList = nullptr;
	FRHIUniformBuffer* ViewUniformBuffer = nullptr;
	TUniformBufferRef<FSceneTextureUniformParameters> SceneTexturesUniformParams;
	const FGlobalDistanceFieldParameterData* GlobalDistanceFieldParams = nullptr;
	void* Uid = nullptr; // A unique identifier for the view.
};
DECLARE_MULTICAST_DELEGATE_OneParam(FOnPostOpaqueRender, class FPostOpaqueRenderParameters&);
typedef FOnPostOpaqueRender::FDelegate FPostOpaqueRenderDelegate;

class ICustomVisibilityQuery: public IRefCountedObject
{
public:
	/** prepares the query for visibility tests */
	virtual bool Prepare() = 0;

	/** test primitive visiblity */
	virtual bool IsVisible(int32 VisibilityId, const FBoxSphereBounds& Bounds) = 0;

	/** return true if we can call IsVisible from a ParallelFor */
	virtual bool IsThreadsafe()
	{
		return false;
	}
};

class ICustomCulling
{
public:
	virtual ICustomVisibilityQuery* CreateQuery (const FSceneView& View) = 0;
};

/**
 * Class use to add FScene pixel inspect request
 */
class FPixelInspectorRequest
{
public:
	FPixelInspectorRequest()
	{
		SourceViewportUV = FVector2D(-1, -1);
		BufferIndex = -1;
		RenderingCommandSend = false;
		RequestComplete = true;
		ViewId = -1;
		GBufferPrecision = 1;
		AllowStaticLighting = true;
		FrameCountAfterRenderingCommandSend = 0;
		RequestTickSinceCreation = 0;
		PreExposure = 1;
	}

	void SetRequestData(FVector2D SrcViewportUV, int32 TargetBufferIndex, int32 ViewUniqueId, int32 GBufferFormat, bool StaticLightingEnable, float InPreExposure)
	{
		SourceViewportUV = SrcViewportUV;
		BufferIndex = TargetBufferIndex;
		RenderingCommandSend = false;
		RequestComplete = false;
		ViewId = ViewUniqueId;
		GBufferPrecision = GBufferFormat;
		AllowStaticLighting = StaticLightingEnable;
		PreExposure = InPreExposure;
		FrameCountAfterRenderingCommandSend = 0;
		RequestTickSinceCreation = 0;
	}

	void MarkSendToRendering() { RenderingCommandSend = true; }

	~FPixelInspectorRequest()
	{
	}

	bool RenderingCommandSend;
	int32 FrameCountAfterRenderingCommandSend;
	int32 RequestTickSinceCreation;
	bool RequestComplete;
	FVector2D SourceViewportUV;
	int32 BufferIndex;
	int32 ViewId;

	//GPU state at capture time
	int32 GBufferPrecision;
	bool AllowStaticLighting;
	float PreExposure;
};

class IPersistentViewUniformBufferExtension
{
public:
	virtual void BeginFrame() {}
	virtual void PrepareView(const FSceneView* View) {}
	virtual void BeginRenderView(const FSceneView* View, bool bShouldWaitForJobs = true) {}
	virtual void EndFrame() {}
};

/**
 * The public interface of the renderer module.
 */
class IRendererModule : public IModuleInterface
{
public:

	/** Call from the game thread to send a message to the rendering thread to being rendering this view family. */
	virtual void BeginRenderingViewFamily(FCanvas* Canvas, FSceneViewFamily* ViewFamily) = 0;
	
	/** Call from the render thread to create and initalize a new FViewInfo with the specified options, and add it to the specified view family. */
	virtual void CreateAndInitSingleView(FRHICommandListImmediate& RHICmdList, class FSceneViewFamily* ViewFamily, const struct FSceneViewInitOptions* ViewInitOptions) = 0;

	/**
	 * Allocates a new instance of the private FScene implementation for the given world.
	 * @param World - An optional world to associate with the scene.
	 * @param bInRequiresHitProxies - Indicates that hit proxies should be rendered in the scene.
	 */
	virtual FSceneInterface* AllocateScene(UWorld* World, bool bInRequiresHitProxies, bool bCreateFXSystem, ERHIFeatureLevel::Type InFeatureLevel) = 0;
	
	virtual void RemoveScene(FSceneInterface* Scene) = 0;

	/**
	* Updates all static draw lists for each allocated scene.
	*/
	virtual void UpdateStaticDrawLists() = 0;

	/**
	 * Updates static draw lists for the given set of materials for each allocated scene.
	 */
	virtual void UpdateStaticDrawListsForMaterials(const TArray<const FMaterial*>& Materials) = 0;

	/** Allocates a new instance of the private scene manager implementation of FSceneViewStateInterface */
	virtual class FSceneViewStateInterface* AllocateViewState() = 0;

	/** @return The number of lights that affect a primitive. */
	virtual uint32 GetNumDynamicLightsAffectingPrimitive(const class FPrimitiveSceneInfo* PrimitiveSceneInfo,const class FLightCacheInterface* LCI) = 0;

	/** Forces reallocation of scene render targets. */
	virtual void ReallocateSceneRenderTargets() = 0;

	virtual void OnWorldCleanup(UWorld* World, bool bSessionEnded, bool bCleanupResources, bool bWorldChanged) = 0;

	/** Sets the buffer size of the render targets. */
	virtual void SceneRenderTargetsSetBufferSize(uint32 SizeX, uint32 SizeY) = 0;

	virtual void InitializeSystemTextures(FRHICommandListImmediate& RHICmdList) = 0;

	/** Draws a tile mesh element with the specified view. */
	virtual void DrawTileMesh(FRHICommandListImmediate& RHICmdList, struct FMeshPassProcessorRenderState& DrawRenderState, const FSceneView& View, FMeshBatch& Mesh, bool bIsHitTesting, const class FHitProxyId& HitProxyId, bool bUse128bitRT = false) = 0;

	virtual const TSet<FSceneInterface*>& GetAllocatedScenes() = 0;

	/** Renderer gets a chance to log some useful crash data */
	virtual void DebugLogOnCrash() = 0;

	// @param WorkScale >0, 10 for normal precision and runtime of less than a second
	virtual void GPUBenchmark(FSynthBenchmarkResults& InOut, float WorkScale = 10.0f) = 0;

	virtual void ExecVisualizeTextureCmd(const FString& Cmd) = 0;

	virtual void UpdateMapNeedsLightingFullyRebuiltState(UWorld* World) = 0;

	/**
	 * Draws a quad with the given vertex positions and UVs in denormalized pixel/texel coordinates.
	 * The platform-dependent mapping from pixels to texels is done automatically.
	 * Note that the positions are affected by the current viewport.
	 * NOTE: DrawRectangle should be used in the vertex shader to calculate the correct position and uv for vertices.
	 *
	 * X, Y							Position in screen pixels of the top left corner of the quad
	 * SizeX, SizeY					Size in screen pixels of the quad
	 * U, V							Position in texels of the top left corner of the quad's UV's
	 * SizeU, SizeV					Size in texels of the quad's UV's
	 * TargetSizeX, TargetSizeY		Size in screen pixels of the target surface
	 * TextureSize                  Size in texels of the source texture
	 * VertexShader					The vertex shader used for rendering
	 * Flags						see EDrawRectangleFlags
	 */
	virtual void DrawRectangle(
		FRHICommandList& RHICmdList,
		float X,
		float Y,
		float SizeX,
		float SizeY,
		float U,
		float V,
		float SizeU,
		float SizeV,
		FIntPoint TargetSize,
		FIntPoint TextureSize,
		const TShaderRefBase<FShader, FShaderMapPointerTable>& VertexShader,
		EDrawRectangleFlags Flags = EDRF_Default
		) = 0;

	/** Register/unregister a custom occlusion culling implementation */
	virtual void RegisterCustomCullingImpl(ICustomCulling* impl) = 0;
	virtual void UnregisterCustomCullingImpl(ICustomCulling* impl) = 0;

	virtual FDelegateHandle RegisterPostOpaqueRenderDelegate(const FPostOpaqueRenderDelegate& PostOpaqueRenderDelegate) = 0;
	virtual void RemovePostOpaqueRenderDelegate(FDelegateHandle PostOpaqueRenderDelegate) = 0;
	virtual FDelegateHandle RegisterOverlayRenderDelegate(const FPostOpaqueRenderDelegate& OverlayRenderDelegate) = 0;
	virtual void RemoveOverlayRenderDelegate(FDelegateHandle OverlayRenderDelegate) = 0;

	/** Delegate that is called upon resolving scene color. */
	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnResolvedSceneColor, FRHICommandListImmediate& /*RHICmdList*/, class FSceneRenderTargets& /*SceneContext*/);

	/** Accessor for post scene color resolve delegates */
	virtual FOnResolvedSceneColor& GetResolvedSceneColorCallbacks() = 0;

	virtual void PostRenderAllViewports() = 0;

	/** Performs necessary per-frame cleanup. Only use when rendering through scene renderer (i.e. BeginRenderingViewFamily) is skipped */
	virtual void PerFrameCleanupIfSkipRenderer() = 0;

	virtual IAllocatedVirtualTexture* AllocateVirtualTexture(const FAllocatedVTDescription& Desc) = 0;
	virtual void DestroyVirtualTexture(IAllocatedVirtualTexture* AllocatedVT) = 0;

	virtual IAdaptiveVirtualTexture* AllocateAdaptiveVirtualTexture(const FAdaptiveVTDescription& AdaptiveVTDesc, const FAllocatedVTDescription& AllocatedVTDesc) = 0;
	virtual void DestroyAdaptiveVirtualTexture(IAdaptiveVirtualTexture* AdaptiveVT) = 0;

	virtual FVirtualTextureProducerHandle RegisterVirtualTextureProducer(const FVTProducerDescription& Desc, IVirtualTexture* Producer) = 0;
	virtual void ReleaseVirtualTextureProducer(const FVirtualTextureProducerHandle& Handle) = 0;
	virtual void AddVirtualTextureProducerDestroyedCallback(const FVirtualTextureProducerHandle& Handle, FVTProducerDestroyedFunction* Function, void* Baton) = 0;
	virtual uint32 RemoveAllVirtualTextureProducerDestroyedCallbacks(const void* Baton) = 0;
	virtual void ReleaseVirtualTexturePendingResources() = 0;

	/**	Provided a list of packed virtual texture tile ids, let the VT system request them. Note this should be called as long as the tiles are needed.*/
	virtual void RequestVirtualTextureTiles(const FVector2D& InScreenSpaceSize, int32 InMipLevel) = 0;
	virtual void RequestVirtualTextureTilesForRegion(IAllocatedVirtualTexture* AllocatedVT, const FVector2D& InScreenSpaceSize, const FIntRect& InTextureRegion, int32 InMipLevel) = 0;

	/** Ensure that any tiles requested by 'RequestVirtualTextureTilesForRegion' are loaded, must be called from render thread */
	virtual void LoadPendingVirtualTextureTiles(FRHICommandListImmediate& RHICmdList, ERHIFeatureLevel::Type FeatureLevel) = 0;

	/** Evict all data from virtual texture caches*/
	virtual void FlushVirtualTextureCache() = 0;

	virtual void RegisterPersistentViewUniformBufferExtension(IPersistentViewUniformBufferExtension* Extension) = 0;
};

