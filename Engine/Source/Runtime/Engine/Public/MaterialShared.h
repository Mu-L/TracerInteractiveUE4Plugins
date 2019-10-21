// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	MaterialShared.h: Shared material definitions.
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "Containers/IndirectArray.h"
#include "Misc/Guid.h"
#include "Engine/EngineTypes.h"
#include "Templates/RefCounting.h"
#include "Templates/ScopedPointer.h"
#include "Templates/UniquePtr.h"
#include "Misc/SecureHash.h"
#include "RHI.h"
#include "RenderResource.h"
#include "RenderingThread.h"
#include "UniformBuffer.h"
#include "Shader.h"
#include "VertexFactory.h"
#include "SceneTypes.h"
#include "StaticParameterSet.h"
#include "Misc/Optional.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/ArchiveProxy.h"
#include "MaterialSceneTextureId.h"
#include "VirtualTexturing.h"

struct FExpressionInput;
class FMaterial;
class FMaterialCompiler;
class FMaterialRenderProxy;
class FMaterialShaderType;
class FMaterialUniformExpression;
struct FUniformExpressionCache;
class FUniformExpressionSet;
class FMeshMaterialShaderType;
class FSceneView;
class FShaderCommonCompileJob;
class FVirtualTexture2DResource;
class IAllocatedVirtualTexture;
class UMaterial;
class UMaterialExpression;
class UMaterialExpressionMaterialFunctionCall;
class UMaterialInstance;
class UMaterialInterface;
class URuntimeVirtualTexture;
class USubsurfaceProfile;
class UTexture;
class UTexture2D;

template <class ElementType> class TLinkedList;

#define ME_CAPTION_HEIGHT		18
#define ME_STD_VPADDING			16
#define ME_STD_HPADDING			32
#define ME_STD_BORDER			8
#define ME_STD_THUMBNAIL_SZ		96
#define ME_PREV_THUMBNAIL_SZ	256
#define ME_STD_LABEL_PAD		16
#define ME_STD_TAB_HEIGHT		21

#define HANDLE_CUSTOM_OUTPUTS_AS_MATERIAL_ATTRIBUTES 0

#define ALLOW_DITHERED_LOD_FOR_INSTANCED_STATIC_MESHES (1)

// disallow debug data in shipping or on non-desktop Test
#define ALLOW_SHADERMAP_DEBUG_DATA (!(UE_BUILD_SHIPPING || (UE_BUILD_TEST && !PLATFORM_DESKTOP)))

#define MATERIAL_OPACITYMASK_DOESNT_SUPPORT_VIRTUALTEXTURE 1

DECLARE_LOG_CATEGORY_EXTERN(LogMaterial,Log,Verbose);

/** Creates a string that represents the given quality level. */
extern void GetMaterialQualityLevelName(EMaterialQualityLevel::Type InMaterialQualityLevel, FString& OutName);
extern FName GetMaterialQualityLevelFName(EMaterialQualityLevel::Type InMaterialQualityLevel);

inline bool IsSubsurfaceShadingModel(FMaterialShadingModelField ShadingModel)
{
	return ShadingModel.HasShadingModel(MSM_Subsurface) || ShadingModel.HasShadingModel(MSM_PreintegratedSkin) ||
		ShadingModel.HasShadingModel(MSM_SubsurfaceProfile) || ShadingModel.HasShadingModel(MSM_TwoSidedFoliage) ||
		ShadingModel.HasShadingModel(MSM_Cloth) || ShadingModel.HasShadingModel(MSM_Eye);
}

inline bool UseSubsurfaceProfile(FMaterialShadingModelField ShadingModel)
{
	return ShadingModel.HasShadingModel(MSM_SubsurfaceProfile) || ShadingModel.HasShadingModel(MSM_Eye);
}

inline uint32 GetUseSubsurfaceProfileShadingModelMask()
{
	return (1 << MSM_SubsurfaceProfile) | (1 << MSM_Eye);
}

/** Whether to allow dithered LOD transitions for a specific feature level. */
ENGINE_API bool AllowDitheredLODTransition(ERHIFeatureLevel::Type FeatureLevel);

/**
 * The types which can be used by materials.
 */
enum EMaterialValueType
{
	/** 
	 * A scalar float type.  
	 * Note that MCT_Float1 will not auto promote to any other float types, 
	 * So use MCT_Float instead for scalar expression return types.
	 */
	MCT_Float1		= 1,
	MCT_Float2		= 2,
	MCT_Float3		= 4,
	MCT_Float4		= 8,

	/** 
	 * Any size float type by definition, but this is treated as a scalar which can auto convert (by replication) to any other size float vector.
	 * Use this as the type for any scalar expressions.
	 */
	MCT_Float                 = 8|4|2|1,
	MCT_Texture2D	          = 1 << 4,
	MCT_TextureCube	          = 1 << 5,
	MCT_VolumeTexture         = 1 << 6,
	MCT_StaticBool            = 1 << 7,
	MCT_Unknown               = 1 << 8,
	MCT_MaterialAttributes	  = 1 << 9,
	MCT_TextureExternal       = 1 << 10,
	MCT_TextureVirtual        = 1 << 11,
	MCT_Texture               = MCT_Texture2D | MCT_TextureCube | MCT_VolumeTexture | MCT_TextureExternal | MCT_TextureVirtual,

	/** Used internally when sampling from virtual textures */
	MCT_VTPageTableResult     = 1 << 13,
	
	MCT_ShadingModel = 1 << 14,
};

/**
 * The common bases of material
 */
enum EMaterialCommonBasis
{
	MCB_Tangent,
	MCB_Local,
	MCB_TranslatedWorld,
	MCB_World,
	MCB_View,
	MCB_Camera,
	MCB_MeshParticle,
	MCB_MAX,
};

//when setting deferred scene resources whether to throw warnings when we fall back to defaults.
enum struct EDeferredParamStrictness
{
	ELoose, // no warnings
	EStrict, // throw warnings
};

/** Defines the domain of a material. */
UENUM()
enum EMaterialDomain
{
	/** The material's attributes describe a 3d surface. */
	MD_Surface UMETA(DisplayName = "Surface"),
	/** The material's attributes describe a deferred decal, and will be mapped onto the decal's frustum. */
	MD_DeferredDecal UMETA(DisplayName = "Deferred Decal"),
	/** The material's attributes describe a light's distribution. */
	MD_LightFunction UMETA(DisplayName = "Light Function"),
	/** The material's attributes describe a 3d volume. */
	MD_Volume UMETA(DisplayName = "Volume"),
	/** The material will be used in a custom post process pass. */
	MD_PostProcess UMETA(DisplayName = "Post Process"),
	/** The material will be used for UMG or Slate UI */
	MD_UI UMETA(DisplayName = "User Interface"),
	/** The material will be used for runtime virtual texture */
	MD_RuntimeVirtualTexture UMETA(DisplayName = "Virtual Texture"),

	MD_MAX
};

ENGINE_API FString MaterialDomainString(EMaterialDomain MaterialDomain);

/**
 * The context of a material being rendered.
 */
struct ENGINE_API FMaterialRenderContext
{
	/** material instance used for the material shader */
	const FMaterialRenderProxy* MaterialRenderProxy;
	/** Material resource to use. */
	const FMaterial& Material;

	/** Whether or not selected objects should use their selection color. */
	bool bShowSelection;

	/** 
	* Constructor
	*/
	FMaterialRenderContext(
		const FMaterialRenderProxy* InMaterialRenderProxy,
		const FMaterial& InMaterial,
		const FSceneView* InView);
};

/**
 * Represents a subclass of FMaterialUniformExpression.
 */
class FMaterialUniformExpressionType
{
public:

	typedef class FMaterialUniformExpression* (*SerializationConstructorType)();

	/**
	 * @return The global uniform expression type list.  The list is used to temporarily store the types until
	 *			the name subsystem has been initialized.
	 */
	static TLinkedList<FMaterialUniformExpressionType*>*& GetTypeList();

	/**
	 * Should not be called until the name subsystem has been initialized.
	 * @return The global uniform expression type map.
	 */
	static TMap<FName,FMaterialUniformExpressionType*>& GetTypeMap();

	/**
	 * Minimal initialization constructor.
	 */
	FMaterialUniformExpressionType(const TCHAR* InName,SerializationConstructorType InSerializationConstructor);

	/**
	 * Serializer for references to uniform expressions.
	 */
	friend FArchive& operator<<(FArchive& Ar,class FMaterialUniformExpression*& Ref);
	friend FArchive& operator<<(FArchive& Ar,class FMaterialUniformExpressionTexture*& Ref);
	friend FArchive& operator<<(FArchive& Ar, class FMaterialUniformExpressionExternalTexture*& Ref);

	const TCHAR* GetName() const { return Name; }

private:

	const TCHAR* Name;
	SerializationConstructorType SerializationConstructor;
};

#define DECLARE_MATERIALUNIFORMEXPRESSION_TYPE(Name) \
	public: \
	static FMaterialUniformExpressionType StaticType; \
	static FMaterialUniformExpression* SerializationConstructor() { return new Name(); } \
	virtual FMaterialUniformExpressionType* GetType() const { return &StaticType; }

#define IMPLEMENT_MATERIALUNIFORMEXPRESSION_TYPE(Name) \
	FMaterialUniformExpressionType Name::StaticType(TEXT(#Name),&Name::SerializationConstructor);

/**
 * Represents an expression which only varies with uniform inputs.
 */
class FMaterialUniformExpression : public FRefCountedObject
{
public:

	virtual ~FMaterialUniformExpression() {}

	virtual FMaterialUniformExpressionType* GetType() const = 0;
	virtual void Serialize(FArchive& Ar) = 0;
	virtual void GetNumberValue(const struct FMaterialRenderContext& Context,FLinearColor& OutValue) const {}
	virtual class FMaterialUniformExpressionTexture* GetTextureUniformExpression() { return nullptr; }
	virtual class FMaterialUniformExpressionExternalTexture* GetExternalTextureUniformExpression() { return nullptr; }
	virtual bool IsConstant() const { return false; }
	virtual bool IsIdentical(const FMaterialUniformExpression* OtherExpression) const { return false; }

	friend FArchive& operator<<(FArchive& Ar,class FMaterialUniformExpression*& Ref);
};

/**
 * A texture expression.
 */
class FMaterialUniformExpressionTexture: public FMaterialUniformExpression
{
	DECLARE_MATERIALUNIFORMEXPRESSION_TYPE(FMaterialUniformExpressionTexture);

public:
	FMaterialUniformExpressionTexture();
	FMaterialUniformExpressionTexture(int32 InTextureIndex, EMaterialSamplerType InSamplerType, ESamplerSourceMode InSamplerSource, bool InVirtualTexture);
	FMaterialUniformExpressionTexture(int32 InTextureIndex, int32 InLayerIndex, EMaterialSamplerType InSamplerType);

	/** Sets an override texture. */
	void SetTransientOverrideTextureValue(UTexture* InOverrideTexture);

	//~ Begin FMaterialUniformExpression Interface.
	virtual void Serialize(FArchive& Ar);
	virtual class FMaterialUniformExpressionTexture* GetTextureUniformExpression() { return this; }
	virtual class FMaterialUniformExpressionTextureParameter* GetTextureParameterUniformExpression() { return nullptr; }
	virtual bool IsIdentical(const FMaterialUniformExpression* OtherExpression) const;
	//~ End FMaterialUniformExpression Interface.

	friend FArchive& operator<<(FArchive& Ar, class FMaterialUniformExpressionTexture*& Ref);

	/** Gets texture index which is the index in the full set of referenced textures for this material. */
	int32 GetTextureIndex() const { return TextureIndex; }
	/** Gets the layer index in the virtual texture stack if this is fixed. If we don't have a fixed layer then we will allocate during compilation (and not store here). */
	int32 GetLayerIndex() const { return LayerIndex; }

#if WITH_EDITORONLY_DATA
	/** Get the sampling/decoding logic to compile in the shader for this texture. */
	EMaterialSamplerType GetSamplerType() const { return SamplerType; }
#endif

	/** Get the sampler state object to use (globally shared or from texture asset).  */
	ESamplerSourceMode GetSamplerSource() const { return SamplerSource; }

	/** Get the UTexture referenced by this expression (can be a nullptr). */
	virtual void GetTextureValue(const FMaterialRenderContext& Context, const FMaterial& Material, const UTexture*& OutValue) const;
	/** Get the URuntimeVirtualTexture referenced by this expression (can be a nullptr). */
	virtual void GetTextureValue(const FMaterial& Material, const URuntimeVirtualTexture*& OutValue) const;
	/** Get the UTexture referenced by this expression including any transient override logic. */
	virtual void GetGameThreadTextureValue(const UMaterialInterface* MaterialInterface, const FMaterial& Material, UTexture*& OutValue, bool bAllowOverride = true) const;

protected:
	/** Index into FMaterial::GetReferencedTextures */
	int32 TextureIndex;
	/** Fixed layer in virtual texture stack if preallocated */
	int32 LayerIndex;
#if WITH_EDITORONLY_DATA
	/** Sampler logic for this expression */
	EMaterialSamplerType SamplerType;
#endif
	/** Sampler state object source for this expression */
	ESamplerSourceMode SamplerSource;
	/** Virtual texture flag used only for unique serialization */
	bool bVirtualTexture;
	/** Texture that may be used in the editor for overriding the texture but never saved to disk, accessible only by the game thread! */
	UTexture* TransientOverrideValue_GameThread;
	/** Texture that may be used in the editor for overriding the texture but never saved to disk, accessible only by the rendering thread! */
	UTexture* TransientOverrideValue_RenderThread;
};

class FMaterialUniformExpressionExternalTextureBase : public FMaterialUniformExpression
{
	DECLARE_MATERIALUNIFORMEXPRESSION_TYPE(FMaterialUniformExpressionExternalTextureBase);
public:

	FMaterialUniformExpressionExternalTextureBase(int32 InSourceTextureIndex = INDEX_NONE);
	FMaterialUniformExpressionExternalTextureBase(const FGuid& InExternalTextureGuid);

	virtual void Serialize(FArchive& Ar) override;
	virtual bool IsConstant() const override { return false; }
	virtual bool IsIdentical(const FMaterialUniformExpression* OtherExpression) const override;

	int32 GetSourceTextureIndex() const { return SourceTextureIndex; }

protected:

	/** Resolve the guid that relates to texture information inside FExternalTexture */
	FGuid ResolveExternalTextureGUID(const FMaterialRenderContext& Context, TOptional<FName> ParameterName = TOptional<FName>()) const;

	/** Index of the texture in the material that should be used to retrieve the external texture GUID at runtime (or INDEX_NONE) */
	int32 SourceTextureIndex;
	/** Optional external texture GUID defined at compile time */
	FGuid ExternalTextureGuid;
};

/**
* An external texture expression.
*/
class FMaterialUniformExpressionExternalTexture : public FMaterialUniformExpressionExternalTextureBase
{
	DECLARE_MATERIALUNIFORMEXPRESSION_TYPE(FMaterialUniformExpressionExternalTexture);
public:

	FMaterialUniformExpressionExternalTexture(int32 InSourceTextureIndex = INDEX_NONE) : FMaterialUniformExpressionExternalTextureBase(InSourceTextureIndex) {}
	FMaterialUniformExpressionExternalTexture(const FGuid& InGuid) : FMaterialUniformExpressionExternalTextureBase(InGuid) {}

	// FMaterialUniformExpression interface.
	virtual FMaterialUniformExpressionExternalTexture* GetExternalTextureUniformExpression() override { return this; }
	virtual class FMaterialUniformExpressionExternalTextureParameter* GetExternalTextureParameterUniformExpression() { return nullptr; }

	// Lookup the external texture if it is set
	virtual bool GetExternalTexture(const FMaterialRenderContext& Context, FTextureRHIRef& OutTextureRHI, FSamplerStateRHIRef& OutSamplerStateRHI) const;

	friend FArchive& operator<<(FArchive& Ar, FMaterialUniformExpressionExternalTexture*& Ref)
	{
		Ar << (FMaterialUniformExpression*&)Ref;
		return Ar;
	}
};

class FMaterialVirtualTextureStack
{
public:
	FMaterialVirtualTextureStack();
	/** Construct with a texture index when this references a preallocated VT stack (for example when we are using a URuntimeVirtualTexture). */
	FMaterialVirtualTextureStack(int32 InPreallocatedStackTextureIndex);

	/** Add space for a layer in the stack. Returns an index that can be used for SetLayer(). */
	uint32 AddLayer();
	/** Set an expression index at a layer in the stack. */
	uint32 SetLayer(int32 LayerIndex, int32 UniformExpressionIndex);
	/** Get the number of layers allocated in the stack. */
	inline uint32 GetNumLayers() const { return NumLayers; }
	/** Returns true if we have allocated the maximum number of layers for this stack. */
	inline bool AreLayersFull() const { return NumLayers == VIRTUALTEXTURE_SPACE_MAXLAYERS; }
	/** Find the layer in the stack that was set with this expression index. */
	int32 FindLayer(int32 UniformExpressionIndex) const;

	/** Returns true if this is a stack that with a preallocated layout of layers (for example when we are using a URuntimeVirtualTexture). */
	inline bool IsPreallocatedStack() const { return PreallocatedStackTextureIndex != INDEX_NONE; }
	/** Get the array of UTexture2D objects for the expressions that in the layers of this stack. Can return nullptr objects for layers that don't hold UTexture2D references. */
	void GetTextureValues(const FMaterialRenderContext& Context, const FUniformExpressionSet& UniformExpressionSet, UTexture2D const** OutValues) const;
	/** Get the URuntimeVirtualTexture object if one was used to initialize this stack. */
	void GetTextureValue(const FMaterialRenderContext& Context, const FUniformExpressionSet& UniformExpressionSet, const URuntimeVirtualTexture*& OutValue) const;

	void Serialize(FArchive& Ar);

	friend FArchive& operator<<(FArchive& Ar, FMaterialVirtualTextureStack& Stack)
	{
		Stack.Serialize(Ar);
		return Ar;
	}

	friend bool operator==(const FMaterialVirtualTextureStack& Lhs, const FMaterialVirtualTextureStack& Rhs)
	{
		if (Lhs.PreallocatedStackTextureIndex != Rhs.PreallocatedStackTextureIndex || Lhs.NumLayers != Rhs.NumLayers)
		{
			return false;
		}
		for (uint32 i = 0u; i < Lhs.NumLayers; ++i)
		{
			if (Lhs.LayerUniformExpressionIndices[i] != Rhs.LayerUniformExpressionIndices[i])
			{
				return false;
			}
		}
		return true;
	}

private:
	/** Number of layers that have been allocated in this stack. */
	uint32 NumLayers;
	/** Indices of the expressions that were set to layers in this stack. */
	int32 LayerUniformExpressionIndices[VIRTUALTEXTURE_SPACE_MAXLAYERS];
	/** Index of a texture reference if we create a stack from a single known texture that has it's own layer stack. */
	int32 PreallocatedStackTextureIndex;
};

inline bool operator!=(const FMaterialVirtualTextureStack& Lhs, const FMaterialVirtualTextureStack& Rhs)
{
	return !operator==(Lhs, Rhs);
}

/** Stores all uniform expressions for a material generated from a material translation. */
class FUniformExpressionSet : public FRefCountedObject
{
public:
	FUniformExpressionSet() {}

	ENGINE_API void Serialize(FArchive& Ar);
	bool IsEmpty() const;
	bool operator==(const FUniformExpressionSet& ReferenceSet) const;
	FString GetSummaryString() const;

	void SetParameterCollections(const TArray<class UMaterialParameterCollection*>& Collections);
	void CreateBufferStruct();
	ENGINE_API const FShaderParametersMetadata& GetUniformBufferStruct() const;

	ENGINE_API void FillUniformBuffer(const FMaterialRenderContext& MaterialRenderContext, const FUniformExpressionCache& UniformExpressionCache, uint8* TempBuffer, int TempBufferSize) const;

	// Get a combined hash of all referenced Texture2D's underlying RHI textures, going through TextureReferences. Can be used to tell if any texture has gone through texture streaming mip changes recently.
	ENGINE_API uint32 GetReferencedTexture2DRHIHash(const FMaterialRenderContext& MaterialRenderContext) const;

	inline bool HasExternalTextureExpressions() const
	{
		return UniformExternalTextureExpressions.Num() > 0;
	}

	uint32 GetAllocatedSize() const
	{
		return UniformVectorExpressions.GetAllocatedSize()
			+ UniformScalarExpressions.GetAllocatedSize()
			+ Uniform2DTextureExpressions.GetAllocatedSize()
			+ UniformCubeTextureExpressions.GetAllocatedSize()
			+ UniformVolumeTextureExpressions.GetAllocatedSize()
			+ UniformVirtualTextureExpressions.GetAllocatedSize()
			+ UniformExternalTextureExpressions.GetAllocatedSize()
			+ VTStacks.GetAllocatedSize()
			+ ParameterCollections.GetAllocatedSize()
			+ (UniformBufferStruct ? (sizeof(FShaderParametersMetadata) + UniformBufferStruct->GetMembers().GetAllocatedSize()) : 0);
	}

protected:
	union FVTPackedStackAndLayerIndex
	{
		inline FVTPackedStackAndLayerIndex(uint16 InStackIndex, uint16 InLayerIndex) : StackIndex(InStackIndex), LayerIndex(InLayerIndex) {}

		uint32 PackedValue;
		struct
		{
			uint16 StackIndex;
			uint16 LayerIndex;
		};
	};

	FVTPackedStackAndLayerIndex GetVTStackAndLayerIndex(int32 UniformExpressionIndex) const;
	
	TArray<TRefCountPtr<FMaterialUniformExpression> > UniformVectorExpressions;
	TArray<TRefCountPtr<FMaterialUniformExpression> > UniformScalarExpressions;
	TArray<TRefCountPtr<FMaterialUniformExpressionTexture> > Uniform2DTextureExpressions;
	TArray<TRefCountPtr<FMaterialUniformExpressionTexture> > UniformCubeTextureExpressions;
	TArray<TRefCountPtr<FMaterialUniformExpressionTexture> > UniformVolumeTextureExpressions;
	TArray<TRefCountPtr<FMaterialUniformExpressionTexture> > UniformVirtualTextureExpressions;
	TArray<TRefCountPtr<FMaterialUniformExpressionExternalTexture> > UniformExternalTextureExpressions;

	/** Virtual texture stacks found during compilation */
	TArray<FMaterialVirtualTextureStack> VTStacks;

	/** Ids of parameter collections referenced by the material that was translated. */
	TArray<FGuid> ParameterCollections;

	/** The structure of a uniform buffer containing values for these uniform expressions. */
	TOptional<FShaderParametersMetadata> UniformBufferStruct;

	friend class FMaterial;
	friend class FHLSLMaterialTranslator;
	friend class FMaterialShaderMap;
	friend class FMaterialShader;
	friend class FMaterialRenderProxy;
	friend class FMaterialVirtualTextureStack;
	friend class FDebugUniformExpressionSet;
};

/** Stores outputs from the material compile that need to be saved. */
class FMaterialCompilationOutput
{
public:
	FMaterialCompilationOutput() :
		UsedSceneTextures(0),
#if WITH_EDITOR
		EstimatedNumTextureSamplesVS(0),
		EstimatedNumTextureSamplesPS(0),
		EstimatedNumVirtualTextureLookups(0),
		NumUsedUVScalars(0),
		NumUsedCustomInterpolatorScalars(0),
#endif
		bNeedsSceneTextures(false),
		bUsesEyeAdaptation(false),
		bModifiesMeshPosition(false),
		bUsesWorldPositionOffset(false),
		bUsesGlobalDistanceField(false),
		bUsesPixelDepthOffset(false),
		bUsesDistanceCullFade(false),
		bHasRuntimeVirtualTextureOutput(false)
	{}

	ENGINE_API void Serialize(FArchive& Ar);

	ENGINE_API bool IsSceneTextureUsed(ESceneTextureId TexId) const { return UsedSceneTextures & (1 << TexId); }
	ENGINE_API void SetIsSceneTextureUsed(ESceneTextureId TexId) { UsedSceneTextures |= (1 << TexId); }

	FUniformExpressionSet UniformExpressionSet;

	/** Bitfield of the ESceneTextures used */
	uint32 UsedSceneTextures;

#if WITH_EDITOR
	/** Number of times SampleTexture is called, excludes custom nodes. */
	uint16 EstimatedNumTextureSamplesVS;
	uint16 EstimatedNumTextureSamplesPS;

        /** Number of virtual texture lookups performed, excludes direct invocation in shaders (for example VT lightmaps) */
	uint16 EstimatedNumVirtualTextureLookups;

	/** Number of used custom UV scalars. */
	uint8 NumUsedUVScalars;

	/** Number of used custom vertex interpolation scalars. */
	uint8 NumUsedCustomInterpolatorScalars;
#endif // WITH_EDITOR

	/** true if the material needs the scenetexture lookups. */
	uint8 bNeedsSceneTextures : 1;

	/** true if the material uses the EyeAdaptationLookup */
	uint8 bUsesEyeAdaptation : 1;

	/** true if the material modifies the the mesh position. */
	uint8 bModifiesMeshPosition : 1;

	/** Whether the material uses world position offset. */
	uint8 bUsesWorldPositionOffset : 1;

	/** true if material uses the global distance field */
	uint8 bUsesGlobalDistanceField : 1;

	/** true if the material writes a pixel depth offset */
	uint8 bUsesPixelDepthOffset : 1;

	/** true if the material uses distance cull fade */
	uint8 bUsesDistanceCullFade : 1;

	/** true if the material supports virtual texture output */
	uint8 bHasRuntimeVirtualTextureOutput : 1;

	/** Indicates whether the material uses scene color. */
	ENGINE_API bool RequiresSceneColorCopy() const { return IsSceneTextureUsed(PPI_SceneColor); }

	/** true if the material uses any GBuffer textures */
	ENGINE_API bool NeedsGBuffer() const
	{
		return
			IsSceneTextureUsed(PPI_DiffuseColor) ||
			IsSceneTextureUsed(PPI_SpecularColor) ||
			IsSceneTextureUsed(PPI_SubsurfaceColor) ||
			IsSceneTextureUsed(PPI_BaseColor) ||
			IsSceneTextureUsed(PPI_Specular) ||
			IsSceneTextureUsed(PPI_Metallic) ||
			IsSceneTextureUsed(PPI_WorldNormal) ||
			IsSceneTextureUsed(PPI_Opacity) ||
			IsSceneTextureUsed(PPI_Roughness) ||
			IsSceneTextureUsed(PPI_MaterialAO) ||
			IsSceneTextureUsed(PPI_DecalMask) ||
			IsSceneTextureUsed(PPI_ShadingModelColor) ||
			IsSceneTextureUsed(PPI_ShadingModelID) ||
			IsSceneTextureUsed(PPI_StoredBaseColor) ||
			IsSceneTextureUsed(PPI_StoredSpecular) ||
			IsSceneTextureUsed(PPI_Velocity);
	}

	/** true if the material uses the SceneDepth lookup */
	ENGINE_API bool UsesSceneDepthLookup() const { return IsSceneTextureUsed(PPI_SceneColor); }

	/** true if the material uses the Velocity SceneTexture lookup */
	ENGINE_API bool UsesVelocitySceneTexture() const { return IsSceneTextureUsed(PPI_Velocity); }
};

/** 
 * Usage options for a shader map.
 * The purpose of EMaterialShaderMapUsage is to allow creating a unique yet deterministic (no appCreateGuid) Id,
 * For a shader map corresponding to any UMaterial or UMaterialInstance, for different use cases.
 * As an example, when exporting a material to Lightmass we want to compile a shader map with FLightmassMaterialProxy,
 * And generate a FMaterialShaderMapId for it that allows reuse later, so it must be deterministic.
 */
namespace EMaterialShaderMapUsage
{
	enum Type
	{
		Default,
		LightmassExportEmissive,
		LightmassExportDiffuse,
		LightmassExportOpacity,
		LightmassExportNormal,
		MaterialExportBaseColor,
		MaterialExportSpecular,
		MaterialExportNormal,
		MaterialExportMetallic,
		MaterialExportRoughness,
		MaterialExportAO,
		MaterialExportEmissive,
		MaterialExportOpacity,
		MaterialExportOpacityMask,
		MaterialExportSubSurfaceColor,
		DebugViewMode,
	};
}

/** Contains all the information needed to uniquely identify a FMaterialShaderMap. */
class FMaterialShaderMapId
{
public:
	FSHAHash CookedShaderMapIdHash;

#if WITH_EDITOR
	/** 
	 * The base material's StateId.  
	 * This guid represents all the state of a UMaterial that is not covered by the other members of FMaterialShaderMapId.
	 * Any change to the UMaterial that modifies that state (for example, adding an expression) must modify this guid.
	 */
	FGuid BaseMaterialId;
#endif

	/** 
	 * Quality level that this shader map is going to be compiled at.  
	 * Can be a value of EMaterialQualityLevel::Num if quality level doesn't matter to the compiled result.
	 */
	EMaterialQualityLevel::Type QualityLevel;

	/** Feature level that the shader map is going to be compiled for. */
	ERHIFeatureLevel::Type FeatureLevel;

#if WITH_EDITOR
	/** 
	 * Indicates what use case this shader map will be for.
	 * This allows the same UMaterial / UMaterialInstance to be compiled with multiple FMaterial derived classes,
	 * While still creating an Id that is deterministic between runs (no appCreateGuid used).
	 */
	EMaterialShaderMapUsage::Type Usage;

private:
	/** Was the shadermap Id loaded in from a cooked resource. */
	bool bIsCookedId;

	/** Static parameters and base Id. */
	FStaticParameterSet ParameterSet;
	FString ParameterSetLayerParametersKey;
public:
	/** Guids of any functions the material was dependent on. */
	TArray<FGuid> ReferencedFunctions;

	/** Guids of any Parameter Collections the material was dependent on. */
	TArray<FGuid> ReferencedParameterCollections;

	/** Shader types of shaders that are inlined in this shader map in the DDC. */
	TArray<FShaderTypeDependency> ShaderTypeDependencies;

	/** Shader pipeline types of shader pipelines that are inlined in this shader map in the DDC. */
	TArray<FShaderPipelineTypeDependency> ShaderPipelineTypeDependencies;

	/** Vertex factory types of shaders that are inlined in this shader map in the DDC. */
	TArray<FVertexFactoryTypeDependency> VertexFactoryTypeDependencies;

	/** 
	 * Hash of the textures referenced by the uniform expressions in the shader map.
	 * This is stored in the shader map Id to gracefully handle situations where code changes
	 * that generates the array of textures that the uniform expressions use to link up after being loaded from the DDC.
	 */
	FSHAHash TextureReferencesHash;
	
	/** A hash of the base property overrides for this material instance. */
	FSHAHash BasePropertyOverridesHash;
#endif // WITH_EDITOR
	
	FMaterialShaderMapId()
		: QualityLevel(EMaterialQualityLevel::High)
		, FeatureLevel(ERHIFeatureLevel::SM4)
#if WITH_EDITOR
		, Usage(EMaterialShaderMapUsage::Default)
		, bIsCookedId(false)
#endif
	{ }

	~FMaterialShaderMapId()
	{ }

#if WITH_EDITOR
	ENGINE_API void SetShaderDependencies(const TArray<FShaderType*>& ShaderTypes, const TArray<const FShaderPipelineType*>& ShaderPipelineTypes, const TArray<FVertexFactoryType*>& VFTypes, EShaderPlatform ShaderPlatform);
#endif

	void Serialize(FArchive& Ar, bool bLoadedByCookedMaterial);

	bool IsCookedId() const
	{
#if WITH_EDITOR
		return bIsCookedId;
#else
		return true;
#endif
	}

	bool IsValid() const
	{
#if WITH_EDITOR
		return !IsCookedId() ? BaseMaterialId.IsValid() : (CookedShaderMapIdHash != FSHAHash());
#else
		return (CookedShaderMapIdHash != FSHAHash());
#endif
	}

	friend uint32 GetTypeHash(const FMaterialShaderMapId& Ref)
	{
#if WITH_EDITOR
		return !Ref.IsCookedId() ? Ref.BaseMaterialId.A : (*(uint32*)&Ref.CookedShaderMapIdHash.Hash[0]);
#else
		// Using the hash value directly instead of FSHAHash CRC as fairly uniform distribution
		return *(uint32*)&Ref.CookedShaderMapIdHash.Hash[0];
#endif
	}

	SIZE_T GetSizeBytes() const
	{
		return sizeof(*this)
#if WITH_EDITOR
			+ ReferencedFunctions.GetAllocatedSize()
			+ ReferencedParameterCollections.GetAllocatedSize()
			+ ShaderTypeDependencies.GetAllocatedSize()
			+ ShaderPipelineTypeDependencies.GetAllocatedSize()
			+ VertexFactoryTypeDependencies.GetAllocatedSize()
#endif
			;
	}

#if WITH_EDITOR
	/** Hashes the material-specific part of this shader map Id. */
	void GetMaterialHash(FSHAHash& OutHash) const;
#endif

	/** 
	* Tests this set against another for equality, disregarding override settings.
	* 
	* @param ReferenceSet	The set to compare against
	* @return				true if the sets are equal
	*/
	bool operator==(const FMaterialShaderMapId& ReferenceSet) const;

	bool operator!=(const FMaterialShaderMapId& ReferenceSet) const
	{
		return !(*this == ReferenceSet);
	}

#if WITH_EDITOR
	/** Updates the Id's static parameter set */	
	void UpdateParameterSet(const FStaticParameterSet& StaticParameters);
	
	const FStaticParameterSet& GetParameterSet() const
	{
		return ParameterSet;
	}

	const FString& GetParameterSetLayerParametersKey() const
	{
		return ParameterSetLayerParametersKey;
	}

	/** Appends string representations of this Id to a key string. */
	void AppendKeyString(FString& KeyString) const;

	/** Returns true if the requested shader type is a dependency of this shader map Id. */
	bool ContainsShaderType(const FShaderType* ShaderType, int32 PermutationId) const;

	/** Returns true if the requested shader type is a dependency of this shader map Id. */
	bool ContainsShaderPipelineType(const FShaderPipelineType* ShaderPipelineType) const;

	/** Returns true if the requested vertex factory type is a dependency of this shader map Id. */
	bool ContainsVertexFactoryType(const FVertexFactoryType* VFType) const;
#endif // WITH_EDITOR
};

/**
 * The shaders which the render the material on a mesh generated by a particular vertex factory type.
 */
class FMeshMaterialShaderMap : public TShaderMap<FMeshMaterialShaderType>
{
public:

	FMeshMaterialShaderMap(EShaderPlatform InPlatform, FVertexFactoryType* InVFType) 
		: TShaderMap<FMeshMaterialShaderType>(InPlatform)
		, VertexFactoryType(InVFType)
	{}

	/**
	 * Enqueues compilation for all shaders for a material and vertex factory type.
	 * @param Material - The material to compile shaders for.
	 * @param VertexFactoryType - The vertex factory type to compile shaders for.
	 * @param Platform - The platform to compile for.
	 */
	uint32 BeginCompile(
		uint32 ShaderMapId,
		const FMaterialShaderMapId& InShaderMapId, 
		const FMaterial* Material,
		FShaderCompilerEnvironment* MaterialEnvironment,
		EShaderPlatform Platform,
		TArray<FShaderCommonCompileJob*>& NewJobs,
		FString DebugDescription,
		FString DebugExtension
		);

	/**
	 * Checks whether a material shader map is missing any shader types necessary for the given material.
	 * May be called with a NULL FMeshMaterialShaderMap, which is equivalent to a FMeshMaterialShaderMap with no shaders cached.
	 * @param MeshShaderMap - The FMeshMaterialShaderMap to check for the necessary shaders.
	 * @param Material - The material which is checked.
	 * @return True if the shader map has all of the shader types necessary.
	 */
	static bool IsComplete(
		const FMeshMaterialShaderMap* MeshShaderMap,
		EShaderPlatform Platform,
		const FMaterial* Material,
		FVertexFactoryType* InVertexFactoryType,
		bool bSilent);

#if WITH_EDITOR
	void LoadMissingShadersFromMemory(
		const FSHAHash& MaterialShaderMapHash, 
		const FMaterial* Material, 
		EShaderPlatform Platform);
#endif

	/**
	 * Removes all entries in the cache with exceptions based on a shader type
	 * @param ShaderType - The shader type to flush
	 */
	void FlushShadersByShaderType(FShaderType* ShaderType);

		/**
	 * Removes all entries in the cache with exceptions based on a shader type
	 * @param ShaderType - The shader type to flush
	 */
	void FlushShadersByShaderPipelineType(const FShaderPipelineType* ShaderPipelineType);

	// Accessors.
	inline FVertexFactoryType* GetVertexFactoryType() const { return VertexFactoryType; }

private:
	/** The vertex factory type these shaders are for. */
	FVertexFactoryType* VertexFactoryType;

	static bool IsMeshShaderComplete(
		const FMeshMaterialShaderMap* MeshShaderMap,
		EShaderPlatform Platform,
		const FMaterial* Material,
		const FMeshMaterialShaderType* ShaderType,
		const FShaderPipelineType* Pipeline,
		FVertexFactoryType* InVertexFactoryType,
		int32 PermutationId,
		bool bSilent);
};

/**
 * The set of material shaders for a single material.
 */
class FMaterialShaderMap : public TShaderMap<FMaterialShaderType>, public FDeferredCleanupInterface
{
public:

	/**
	 * Finds the shader map for a material.
	 * @param StaticParameterSet - The static parameter set identifying the shader map
	 * @param Platform - The platform to lookup for
	 * @return NULL if no cached shader map was found.
	 */
	static TRefCountPtr<FMaterialShaderMap> FindId(const FMaterialShaderMapId& ShaderMapId, EShaderPlatform Platform);

#if ALLOW_SHADERMAP_DEBUG_DATA
	/** Flushes the given shader types from any loaded FMaterialShaderMap's. */
	static void FlushShaderTypes(TArray<FShaderType*>& ShaderTypesToFlush, TArray<const FShaderPipelineType*>& ShaderPipelineTypesToFlush, TArray<const FVertexFactoryType*>& VFTypesToFlush);
#endif

	static void FixupShaderTypes(EShaderPlatform Platform, 
		const TMap<FShaderType*, FString>& ShaderTypeNames,
		const TMap<const FShaderPipelineType*, FString>& ShaderPipelineTypeNames,
		const TMap<FVertexFactoryType*, FString>& VertexFactoryTypeNames);

#if WITH_EDITOR
	/** 
	 * Attempts to load the shader map for the given material from the Derived Data Cache.
	 * If InOutShaderMap is valid, attempts to load the individual missing shaders instead.
	 */
	static void LoadFromDerivedDataCache(const FMaterial* Material, const FMaterialShaderMapId& ShaderMapId, EShaderPlatform Platform, TRefCountPtr<FMaterialShaderMap>& InOutShaderMap);
#endif

	inline FMaterialShaderMap() : FMaterialShaderMap(EShaderPlatform::SP_NumPlatforms) {}
	FMaterialShaderMap(EShaderPlatform InPlatform);

	// Destructor.
	~FMaterialShaderMap();

	/**
	 * Compiles the shaders for a material and caches them in this shader map.
	 * @param Material - The material to compile shaders for.
	 * @param ShaderMapId - the set of static parameters to compile for
	 * @param Platform - The platform to compile to
	 */
	void Compile(
		FMaterial* Material,
		const FMaterialShaderMapId& ShaderMapId, 
		TRefCountPtr<FShaderCompilerEnvironment> MaterialEnvironment,
		const FMaterialCompilationOutput& InMaterialCompilationOutput,
		EShaderPlatform Platform,
		bool bSynchronousCompile
		);

#if WITH_EDITOR
	/** Sorts the incoming compiled jobs into the appropriate mesh shader maps, and finalizes this shader map so that it can be used for rendering. */
	bool ProcessCompilationResults(const TArray<FShaderCommonCompileJob*>& InCompilationResults, int32& ResultIndex, float& TimeBudget, TMap<const FVertexFactoryType*, TArray<const FShaderPipelineType*> >& SharedPipelines);
#endif

	/**
	 * Checks whether the material shader map is missing any shader types necessary for the given material.
	 * @param Material - The material which is checked.
	 * @return True if the shader map has all of the shader types necessary.
	 */
	bool IsComplete(const FMaterial* Material, bool bSilent);

#if WITH_EDITOR
	/** Attempts to load missing shaders from memory. */
	void LoadMissingShadersFromMemory(const FMaterial* Material);
#endif

	/**
	 * Checks to see if the shader map is already being compiled for another material, and if so
	 * adds the specified material to the list to be applied to once the compile finishes.
	 * @param Material - The material we also wish to apply the compiled shader map to.
	 * @return True if the shader map was being compiled and we added Material to the list to be applied.
	 */
	bool TryToAddToExistingCompilationTask(FMaterial* Material);

#if WITH_EDITOR
	ENGINE_API const FString *GetShaderSource(const FName ShaderTypeName) const;
#endif

	/** Builds a list of the shaders in a shader map. */
	ENGINE_API void GetShaderList(TMap<FShaderId, FShader*>& OutShaders) const;

	/** Builds a list of the shaders in a shader map. Key is FShaderType::TypeName */
	ENGINE_API void GetShaderList(TMap<FName, FShader*>& OutShaders) const;

	/** Builds a list of the shader pipelines in a shader map. */
	ENGINE_API void GetShaderPipelineList(TArray<FShaderPipeline*>& OutShaderPipelines) const;

	/** Registers a material shader map in the global map so it can be used by materials. */
	void Register(EShaderPlatform InShaderPlatform);

	// Reference counting.
	ENGINE_API void AddRef();
	ENGINE_API void Release();

	/**
	 * Removes all entries in the cache with exceptions based on a shader type
	 * @param ShaderType - The shader type to flush
	 */
	void FlushShadersByShaderType(FShaderType* ShaderType);

	/**
	 * Removes all entries in the cache with exceptions based on a shader pipeline type
	 * @param ShaderPipelineType - The shader pipeline type to flush
	 */
	void FlushShadersByShaderPipelineType(const FShaderPipelineType* ShaderPipelineType);

	/**
	 * Removes all entries in the cache with exceptions based on a vertex factory type
	 * @param ShaderType - The shader type to flush
	 */
	void FlushShadersByVertexFactoryType(const FVertexFactoryType* VertexFactoryType);
	
	/** Removes a material from ShaderMapsBeingCompiled. */
	static void RemovePendingMaterial(FMaterial* Material);

	/** Finds a shader map currently being compiled that was enqueued for the given material. */
	static const FMaterialShaderMap* GetShaderMapBeingCompiled(const FMaterial* Material);

	/** Serializes the shader map. */
	void Serialize(FArchive& Ar, bool bInlineShaderResources=true, bool bLoadedByCookedMaterial=false);

#if WITH_EDITOR
	/** Saves this shader map to the derived data cache. */
	void SaveToDerivedDataCache();
#endif

	/** Registers all shaders that have been loaded in Serialize */
	virtual void RegisterSerializedShaders(bool bLoadedByCookedMaterial) override;
	virtual void DiscardSerializedShaders() override;

	/** Backs up any FShaders in this shader map to memory through serialization and clears FShader references. */
	TArray<uint8>* BackupShadersToMemory();
	/** Recreates FShaders from the passed in memory, handling shader key changes. */
	void RestoreShadersFromMemory(const TArray<uint8>& ShaderData);

	/** Serializes a shader map to an archive (used with recompiling shaders for a remote console) */
	ENGINE_API static void SaveForRemoteRecompile(FArchive& Ar, const TMap<FString, TArray<TRefCountPtr<FMaterialShaderMap> > >& CompiledShaderMaps, const TArray<FShaderResourceId>& ClientResourceIds);
	ENGINE_API static void LoadForRemoteRecompile(FArchive& Ar, EShaderPlatform ShaderPlatform, const TArray<FString>& MaterialsForShaderMaps);

	/** Computes the memory used by this shader map without counting the shaders themselves. */
	uint32 GetSizeBytes() const
	{
		return sizeof(*this)
			+ MeshShaderMaps.GetAllocatedSize()
			+ OrderedMeshShaderMaps.GetAllocatedSize()
			+ MaterialCompilationOutput.UniformExpressionSet.GetAllocatedSize()
#if ALLOW_SHADERMAP_DEBUG_DATA
			+ FriendlyName.GetAllocatedSize()
			+ DebugDescription.GetAllocatedSize()
#endif
		;
	}

#if WITH_EDITOR
	/** Returns the maximum number of texture samplers used by any shader in this shader map. */
	uint32 GetMaxTextureSamplers() const;
#endif

	// Accessors.
	ENGINE_API const FMeshMaterialShaderMap* GetMeshShaderMap(FVertexFactoryType* VertexFactoryType) const;
	const FMaterialShaderMapId& GetShaderMapId() const { return ShaderMapId; }
	uint32 GetCompilingId() const { return CompilingId; }
	bool IsCompilationFinalized() const { return bCompilationFinalized; }
	bool CompiledSuccessfully() const { return bCompiledSuccessfully; }
#if ALLOW_SHADERMAP_DEBUG_DATA
	const FString& GetFriendlyName() const { return FriendlyName; }
	const FString& GetDebugDescription() const { return DebugDescription; }
#else
	const FString& GetFriendlyName() const { static FString T; return T; }
	const FString& GetDebugDescription() const { static FString T; return T; }
#endif
	bool RequiresSceneColorCopy() const { return MaterialCompilationOutput.RequiresSceneColorCopy(); }
	bool NeedsSceneTextures() const { return MaterialCompilationOutput.bNeedsSceneTextures; }
	bool UsesGlobalDistanceField() const { return MaterialCompilationOutput.bUsesGlobalDistanceField; }
	bool UsesWorldPositionOffset() const { return MaterialCompilationOutput.bUsesWorldPositionOffset; }
	bool NeedsGBuffer() const { return MaterialCompilationOutput.NeedsGBuffer(); }
	bool UsesEyeAdaptation() const { return MaterialCompilationOutput.bUsesEyeAdaptation; }
	bool ModifiesMeshPosition() const { return MaterialCompilationOutput.bModifiesMeshPosition; }
	bool UsesPixelDepthOffset() const { return MaterialCompilationOutput.bUsesPixelDepthOffset; }
	bool UsesSceneDepthLookup() const { return MaterialCompilationOutput.UsesSceneDepthLookup(); }
	bool UsesVelocitySceneTexture() const { return MaterialCompilationOutput.UsesVelocitySceneTexture(); }
	bool UsesDistanceCullFade() const { return MaterialCompilationOutput.bUsesDistanceCullFade; }
	bool HasRuntimeVirtualTextureOutput() const { return MaterialCompilationOutput.bHasRuntimeVirtualTextureOutput; }
#if WITH_EDITOR
	uint32 GetNumUsedUVScalars() const { return MaterialCompilationOutput.NumUsedUVScalars; }
	uint32 GetNumUsedCustomInterpolatorScalars() const { return MaterialCompilationOutput.NumUsedCustomInterpolatorScalars; }
	void GetEstimatedNumTextureSamples(uint32& VSSamples, uint32& PSSamples) const { VSSamples = MaterialCompilationOutput.EstimatedNumTextureSamplesVS; PSSamples = MaterialCompilationOutput.EstimatedNumTextureSamplesPS; }
	uint32 GetEstimatedNumVirtualTextureLookups() const { return MaterialCompilationOutput.EstimatedNumVirtualTextureLookups; }
#endif
	uint32 GetNumVirtualTextureStacks() const { return MaterialCompilationOutput.UniformExpressionSet.VTStacks.Num(); }
	bool UsesSceneTexture(uint32 TexId) const { return MaterialCompilationOutput.UsedSceneTextures & (1ull << TexId); }

	bool IsValidForRendering(bool bFailOnInvalid = false) const
	{
		const bool bValid = bCompilationFinalized && bCompiledSuccessfully;// && !bDeletedThroughDeferredCleanup; //deferred actually deletion will prevent the material to go away before we finish rendering
		checkf(bValid || !bFailOnInvalid, TEXT("FMaterialShaderMap %s invalid for rendering: bCompilationFinalized: %i, bCompiledSuccessfully: %i, bDeletedThroughDeferredCleanup: %i"), *GetFriendlyName(), bCompilationFinalized, bCompiledSuccessfully, bDeletedThroughDeferredCleanup ? 1 : 0);
		return bValid;
	}

	const FUniformExpressionSet& GetUniformExpressionSet() const { return MaterialCompilationOutput.UniformExpressionSet; }

	int32 GetNumRefs() const { return NumRefs; }

	void CountNumShaders(int32& NumShaders, int32& NumPipelines) const
	{
		NumShaders = GetNumShaders();
		NumPipelines = GetNumShaderPipelines();

		for (FMeshMaterialShaderMap* MeshShaderMap : OrderedMeshShaderMaps)
		{
			if (MeshShaderMap)
			{
				NumShaders += MeshShaderMap->GetNumShaders();
				NumPipelines += MeshShaderMap->GetNumShaderPipelines();
			}
		}
	}
	void DumpDebugInfo();
	void SaveShaderStableKeys(EShaderPlatform TargetShaderPlatform, const struct FStableShaderKeyAndValue& SaveKeyVal);

private:

	/** 
	 * A global map from a material's static parameter set to any shader map cached for that material. 
	 * Note: this does not necessarily contain all material shader maps in memory.  Shader maps with the same key can evict each other.
	 * No ref counting needed as these are removed on destruction of the shader map.
	 */
	static TMap<FMaterialShaderMapId,FMaterialShaderMap*> GIdToMaterialShaderMap[SP_NumPlatforms];
	static FCriticalSection GIdToMaterialShaderMapCS;

#if ALLOW_SHADERMAP_DEBUG_DATA
	/** 
	 * All material shader maps in memory. 
	 * No ref counting needed as these are removed on destruction of the shader map.
	 */
	static TArray<FMaterialShaderMap*> AllMaterialShaderMaps;
#endif

	/** The material's cached shaders for vertex factory type dependent shaders. */
	TIndirectArray<FMeshMaterialShaderMap> MeshShaderMaps;

	/** The material's mesh shader maps, indexed by VFType->GetId(), for fast lookup at runtime. */
	TArray<FMeshMaterialShaderMap*> OrderedMeshShaderMaps;

#if ALLOW_SHADERMAP_DEBUG_DATA
	/** The material's user friendly name, typically the object name. */
	FString FriendlyName;
#endif

	/** The static parameter set that this shader map was compiled with */
	FMaterialShaderMapId ShaderMapId;

	/** Uniform expressions generated from the material compile. */
	FMaterialCompilationOutput MaterialCompilationOutput;

	/** Next value for CompilingId. */
	static uint32 NextCompilingId;

	/** Tracks material resources and their shader maps that need to be compiled but whose compilation is being deferred. */
	static TMap<TRefCountPtr<FMaterialShaderMap>, TArray<FMaterial*> > ShaderMapsBeingCompiled;

	/** Uniquely identifies this shader map during compilation, needed for deferred compilation where shaders from multiple shader maps are compiled together. */
	uint32 CompilingId;

	mutable int32 NumRefs;

	/** Used to catch errors where the shader map is deleted directly. */
	bool bDeletedThroughDeferredCleanup;

	/** Indicates whether this shader map has been registered in GIdToMaterialShaderMap */
	uint32 bRegistered : 1;

	/** 
	 * Indicates whether this shader map has had ProcessCompilationResults called after Compile.
	 * The shader map must not be used on the rendering thread unless bCompilationFinalized is true.
	 */
	uint32 bCompilationFinalized : 1;

	uint32 bCompiledSuccessfully : 1;

	/** Indicates whether the shader map should be stored in the shader cache. */
	uint32 bIsPersistent : 1;

#if ALLOW_SHADERMAP_DEBUG_DATA
	/** Debug information about how the material shader map was compiled. */
	FString DebugDescription;

#if WITH_EDITOR
	/** Map that stores source code used to compile a certain shader present in this FMaterialShaderMap instance. Key is ShaderType::TypeName. */
	TMap<FName, FString> ShaderProcessedSource;
#endif
#endif

	FShader* ProcessCompilationResultsForSingleJob(class FShaderCompileJob* SingleJob, const FShaderPipelineType* ShaderPipeline, const FSHAHash& MaterialShaderMapHash);

	bool IsMaterialShaderComplete(
		const FMaterial* Material,
		const FMaterialShaderType* ShaderType,
		const FShaderPipelineType* Pipeline,
		int32 PermutationId,
		bool bSilent) const;

	/** Initializes OrderedMeshShaderMaps from the contents of MeshShaderMaps. */
	void InitOrderedMeshShaderMaps();

	friend ENGINE_API void DumpMaterialStats( EShaderPlatform Platform );
	friend class FShaderCompilingManager;
};



/** 
 * Enum that contains entries for the ways that material properties need to be compiled.
 * This 'inherits' from EMaterialProperty in the sense that all of its values start after the values in EMaterialProperty.
 * Each material property is compiled once for its usual shader frequency, determined by GetShaderFrequency(),
 * And then this enum contains entries for extra compiles of a material property with a different shader frequency.
 * This is necessary for material properties which need to be evaluated in multiple shader frequencies.
 */
enum ECompiledMaterialProperty
{
	CompiledMP_EmissiveColorCS = MP_MAX,
	CompiledMP_PrevWorldPositionOffset,
	CompiledMP_MAX
};

/**
 * Uniquely identifies a material expression output. 
 * Used by the material compiler to keep track of which output it is compiling.
 */
class FMaterialExpressionKey
{
public:
	UMaterialExpression* Expression;
	int32 OutputIndex;
	/** Attribute currently being compiled through a MatterialAttributes connection. */
	FGuid MaterialAttributeID;
	// Expressions are different (e.g. View.PrevWorldViewOrigin) when using previous frame's values, value if from FHLSLMaterialTranslator::bCompilingPreviousFrame
	bool bCompilingPreviousFrameKey;

	FMaterialExpressionKey(UMaterialExpression* InExpression, int32 InOutputIndex) :
		Expression(InExpression),
		OutputIndex(InOutputIndex),
		MaterialAttributeID(FGuid(0,0,0,0)),
		bCompilingPreviousFrameKey(false)
	{}

	FMaterialExpressionKey(UMaterialExpression* InExpression, int32 InOutputIndex, const FGuid& InMaterialAttributeID, bool bInCompilingPreviousFrameKey) :
		Expression(InExpression),
		OutputIndex(InOutputIndex),
		MaterialAttributeID(InMaterialAttributeID),
		bCompilingPreviousFrameKey(bInCompilingPreviousFrameKey)
	{}


	friend bool operator==(const FMaterialExpressionKey& X, const FMaterialExpressionKey& Y)
	{
		return X.Expression == Y.Expression && X.OutputIndex == Y.OutputIndex && X.MaterialAttributeID == Y.MaterialAttributeID && X.bCompilingPreviousFrameKey == Y.bCompilingPreviousFrameKey;
	}

	friend uint32 GetTypeHash(const FMaterialExpressionKey& ExpressionKey)
	{
		return PointerHash(ExpressionKey.Expression);
	}
};

/** Function specific compiler state. */
class FMaterialFunctionCompileState
{
public:
	explicit FMaterialFunctionCompileState(UMaterialExpressionMaterialFunctionCall* InFunctionCall)
		: FunctionCall(InFunctionCall)
	{}

	~FMaterialFunctionCompileState()
	{
		ClearSharedFunctionStates();
	}

	FMaterialFunctionCompileState* FindOrAddSharedFunctionState(FMaterialExpressionKey& ExpressionKey, class UMaterialExpressionMaterialFunctionCall* SharedFunctionCall)
	{
		if (FMaterialFunctionCompileState** ExistingState = SharedFunctionStates.Find(ExpressionKey))
		{
			return *ExistingState;
		}
		return SharedFunctionStates.Add(ExpressionKey, new FMaterialFunctionCompileState(SharedFunctionCall));
	}

	void ClearSharedFunctionStates()
	{
		for (auto SavedStateIt = SharedFunctionStates.CreateIterator(); SavedStateIt; ++SavedStateIt)
		{
			FMaterialFunctionCompileState* SavedState = SavedStateIt.Value();
			SavedState->ClearSharedFunctionStates();
			delete SavedState;
		}
		SharedFunctionStates.Empty();
	}

	void Reset()
	{
		ExpressionStack.Empty();
		ExpressionCodeMap.Empty();
		ClearSharedFunctionStates();
	}

	class UMaterialExpressionMaterialFunctionCall* FunctionCall;

	// Stack used to avoid re-entry within this function
	TArray<FMaterialExpressionKey> ExpressionStack;

	/** A map from material expression to the index into CodeChunks of the code for the material expression. */
	TMap<FMaterialExpressionKey,int32> ExpressionCodeMap;

private:
	/** Cache of MaterialFunctionOutput CodeChunks.  Allows for further reuse than just the ExpressionCodeMap */
	TMap<FMaterialExpressionKey, FMaterialFunctionCompileState*> SharedFunctionStates;
};

/** Returns whether the given expression class is allowed. */
extern ENGINE_API bool IsAllowedExpressionType(UClass* Class, bool bMaterialFunction);

/** Parses a string into multiple lines, for use with tooltips. */
extern ENGINE_API void ConvertToMultilineToolTip(const FString& InToolTip, int32 TargetLineLength, TArray<FString>& OutToolTip);

/** Given a combination of EMaterialValueType flags, get text descriptions of all types */
extern ENGINE_API void GetMaterialValueTypeDescriptions(uint32 MaterialValueType, TArray<FText>& OutDescriptions);

/** Check whether a combination of EMaterialValueType flags can be connected */
extern ENGINE_API bool CanConnectMaterialValueTypes(uint32 InputType, uint32 OutputType);

/**
 * FMaterial serves 3 intertwined purposes:
 *   Represents a material to the material compilation process, and provides hooks for extensibility (CompileProperty, etc)
 *   Represents a material to the renderer, with functions to access material properties
 *   Stores a cached shader map, and other transient output from a compile, which is necessary with async shader compiling
 *      (when a material finishes async compilation, the shader map and compile errors need to be stored somewhere)
 */
class ENGINE_VTABLE FMaterial
{
public:

	/**
	 * Minimal initialization constructor.
	 */
	FMaterial():
		RenderingThreadShaderMap(NULL),
		QualityLevel(EMaterialQualityLevel::High),
		bHasQualityLevelUsage(false),
		FeatureLevel(ERHIFeatureLevel::SM4),
		bContainsInlineShaders(false),
		bLoadedCookedShaderMapId(false)
	{}

	/**
	 * Destructor
	 */
	ENGINE_API virtual ~FMaterial();

	/**
	 * Caches the material shaders for this material with no static parameters on the given platform.
	 * This is used by material resources of UMaterials.
	 */
	ENGINE_API bool CacheShaders(EShaderPlatform Platform, const ITargetPlatform* TargetPlatform = nullptr);

	/**
	 * Caches the material shaders for the given static parameter set and platform.
	 * This is used by material resources of UMaterialInstances.
	 */
	ENGINE_API bool CacheShaders(const FMaterialShaderMapId& ShaderMapId, EShaderPlatform Platform, const ITargetPlatform* TargetPlatform = nullptr);

	/**
	 * Should the shader for this material with the given platform, shader type and vertex 
	 * factory type combination be compiled
	 *
	 * @param Platform		The platform currently being compiled for
	 * @param ShaderType	Which shader is being compiled
	 * @param VertexFactory	Which vertex factory is being compiled (can be NULL)
	 *
	 * @return true if the shader should be compiled
	 */
	ENGINE_API virtual bool ShouldCache(EShaderPlatform Platform, const FShaderType* ShaderType, const FVertexFactoryType* VertexFactoryType) const;

	/** Serializes the material. */
	ENGINE_API virtual void LegacySerialize(FArchive& Ar);

	/** Serializes the shader map inline in this material, including any shader dependencies. */
	void SerializeInlineShaderMap(FArchive& Ar);

	/** Serializes the shader map inline in this material, including any shader dependencies. */
	void RegisterInlineShaderMap(bool bLoadedByCookedMaterial);

	/** Releases this material's shader map.  Must only be called on materials not exposed to the rendering thread! */
	void ReleaseShaderMap();

	/** Discards loaded shader maps if the application can't render */
	void DiscardShaderMap();

	// Material properties.
	ENGINE_API virtual void GetShaderMapId(EShaderPlatform Platform, FMaterialShaderMapId& OutId) const;
	virtual EMaterialDomain GetMaterialDomain() const = 0; // See EMaterialDomain.
	virtual bool IsTwoSided() const = 0;
	virtual bool IsDitheredLODTransition() const = 0;
	virtual bool IsTranslucencyWritingCustomDepth() const { return false; }
	virtual bool IsTranslucencyWritingVelocity() const { return false; }
	virtual bool IsTangentSpaceNormal() const { return false; }
	virtual bool ShouldInjectEmissiveIntoLPV() const { return false; }
	virtual bool ShouldBlockGI() const { return false; }
	virtual bool ShouldGenerateSphericalParticleNormals() const { return false; }
	virtual	bool ShouldDisableDepthTest() const { return false; }
	virtual	bool ShouldWriteOnlyAlpha() const { return false; }
	virtual	bool ShouldEnableResponsiveAA() const { return false; }
	virtual bool ShouldDoSSR() const { return false; }
	virtual bool ShouldDoContactShadows() const { return false; }
	virtual bool IsLightFunction() const = 0;
	virtual bool IsUsedWithEditorCompositing() const { return false; }
	virtual bool IsDeferredDecal() const = 0;
	virtual bool IsVolumetricPrimitive() const = 0;
	virtual bool IsWireframe() const = 0;
	virtual bool IsUIMaterial() const { return false; }
	virtual bool IsSpecialEngineMaterial() const = 0;
	virtual bool IsUsedWithSkeletalMesh() const { return false; }
	virtual bool IsUsedWithLandscape() const { return false; }
	virtual bool IsUsedWithParticleSystem() const { return false; }
	virtual bool IsUsedWithParticleSprites() const { return false; }
	virtual bool IsUsedWithBeamTrails() const { return false; }
	virtual bool IsUsedWithMeshParticles() const { return false; }
	virtual bool IsUsedWithNiagaraSprites() const { return false; }
	virtual bool IsUsedWithNiagaraRibbons() const { return false; }
	virtual bool IsUsedWithNiagaraMeshParticles() const { return false; }
	virtual bool IsUsedWithStaticLighting() const { return false; }
	virtual	bool IsUsedWithMorphTargets() const { return false; }
	virtual bool IsUsedWithSplineMeshes() const { return false; }
	virtual bool IsUsedWithInstancedStaticMeshes() const { return false; }
	virtual bool IsUsedWithGeometryCollections() const { return false; }
	virtual bool IsUsedWithAPEXCloth() const { return false; }
	virtual bool IsUsedWithUI() const { return false; }
	virtual bool IsUsedWithGeometryCache() const { return false; }
	ENGINE_API virtual enum EMaterialTessellationMode GetTessellationMode() const;
	virtual bool IsCrackFreeDisplacementEnabled() const { return false; }
	virtual bool IsAdaptiveTessellationEnabled() const { return false; }
	virtual bool IsFullyRough() const { return false; }
	virtual bool UseNormalCurvatureToRoughness() const { return false; }
	virtual bool IsUsingFullPrecision() const { return false; }
	virtual bool IsUsingHQForwardReflections() const { return false; }
	virtual bool IsUsingPlanarForwardReflections() const { return false; }
	virtual bool OutputsVelocityOnBasePass() const { return true; }
	virtual bool IsNonmetal() const { return false; }
	virtual bool UseLmDirectionality() const { return true; }
	virtual bool IsMasked() const = 0;
	virtual bool IsDitherMasked() const { return false; }
	virtual bool AllowNegativeEmissiveColor() const { return false; }
	virtual enum EBlendMode GetBlendMode() const = 0;
	ENGINE_API virtual enum ERefractionMode GetRefractionMode() const;
	virtual FMaterialShadingModelField GetShadingModels() const = 0;
	virtual bool IsShadingModelFromMaterialExpression() const = 0;
	virtual enum ETranslucencyLightingMode GetTranslucencyLightingMode() const { return TLM_VolumetricNonDirectional; };
	virtual float GetOpacityMaskClipValue() const = 0;
	virtual bool GetCastDynamicShadowAsMasked() const = 0;
	virtual bool IsDistorted() const { return false; };
	virtual float GetTranslucencyDirectionalLightingIntensity() const { return 1.0f; }
	virtual float GetTranslucentShadowDensityScale() const { return 1.0f; }
	virtual float GetTranslucentSelfShadowDensityScale() const { return 1.0f; }
	virtual float GetTranslucentSelfShadowSecondDensityScale() const { return 1.0f; }
	virtual float GetTranslucentSelfShadowSecondOpacity() const { return 1.0f; }
	virtual float GetTranslucentBackscatteringExponent() const { return 1.0f; }
	virtual bool IsTranslucencyAfterDOFEnabled() const { return false; }
	virtual bool IsMobileSeparateTranslucencyEnabled() const { return false; }
	virtual FLinearColor GetTranslucentMultipleScatteringExtinction() const { return FLinearColor::White; }
	virtual float GetTranslucentShadowStartOffset() const { return 0.0f; }
	virtual float GetRefractionDepthBiasValue() const { return 0.0f; }
	virtual float GetMaxDisplacement() const { return 0.0f; }
	virtual bool ShouldApplyFogging() const { return false; }
	virtual bool ComputeFogPerPixel() const { return false; }
	virtual FString GetFriendlyName() const = 0;
	virtual bool HasVertexPositionOffsetConnected() const { return false; }
	virtual bool HasPixelDepthOffsetConnected() const { return false; }
	virtual bool HasMaterialAttributesConnected() const { return false; }
	virtual uint32 GetDecalBlendMode() const { return 0; }
	virtual uint32 GetMaterialDecalResponse() const { return 0; }
	virtual bool HasNormalConnected() const { return false; }
	virtual bool HasEmissiveColorConnected() const { return false; }
	virtual bool RequiresSynchronousCompilation() const { return false; };
	virtual bool IsDefaultMaterial() const { return false; };
	virtual int32 GetNumCustomizedUVs() const { return 0; }
	virtual int32 GetBlendableLocation() const { return 0; }
	virtual bool GetBlendableOutputAlpha() const { return false; }
	virtual bool IsStencilTestEnabled() const { return false; }
	virtual uint32 GetStencilRefValue() const { return 0; }
	virtual uint32 GetStencilCompare() const { return 0; }
	virtual bool HasRuntimeVirtualTextureOutput() const { return false; }
	virtual bool CastsRayTracedShadows() const { return true; }

	/**
	 * Should shaders compiled for this material be saved to disk?
	 */
	virtual bool IsPersistent() const = 0;
	virtual UMaterialInterface* GetMaterialInterface() const { return NULL; }

#if WITH_EDITOR
	/**
	* Called when compilation of an FMaterial finishes, after the GameThreadShaderMap is set and the render command to set the RenderThreadShaderMap is queued
	*/
	virtual void NotifyCompilationFinished() { }

	/**
	* Cancels all outstanding compilation jobs for this material.
	*/
	ENGINE_API void CancelCompilation();

	/** 
	 * Blocks until compilation has completed. Returns immediately if a compilation is not outstanding.
	 */
	ENGINE_API void FinishCompilation();

	/**
	 * Checks if the compilation for this shader is finished
	 * 
	 * @return returns true if compilation is complete false otherwise
	 */
	ENGINE_API bool IsCompilationFinished() const;
#endif // WITH_EDITOR

	/**
	* Checks if there is a valid GameThreadShaderMap, that is, the material can be rendered as intended.
	*
	* @return returns true if there is a GameThreadShaderMap.
	*/
	ENGINE_API bool HasValidGameThreadShaderMap() const;

	/** Returns whether this material should be considered for casting dynamic shadows. */
	inline bool ShouldCastDynamicShadows() const
	{
		return GetShadingModels().IsLit() &&
			(GetBlendMode() == BLEND_Opaque ||
			 GetBlendMode() == BLEND_Masked ||
			(GetBlendMode() == BLEND_Translucent && GetCastDynamicShadowAsMasked()));
	}


	EMaterialQualityLevel::Type GetQualityLevel() const 
	{
		return QualityLevel;
	}

	// Accessors.
	ENGINE_API const TArray<TRefCountPtr<FMaterialUniformExpressionTexture> >& GetUniform2DTextureExpressions() const;
	ENGINE_API const TArray<TRefCountPtr<FMaterialUniformExpressionTexture> >& GetUniformCubeTextureExpressions() const;
	ENGINE_API const TArray<TRefCountPtr<FMaterialUniformExpressionTexture> >& GetUniformVolumeTextureExpressions() const;
	ENGINE_API const TArray<TRefCountPtr<FMaterialUniformExpressionTexture> >& GetUniformVirtualTextureExpressions() const;
	ENGINE_API const TArray<TRefCountPtr<FMaterialUniformExpression> >& GetUniformVectorParameterExpressions() const;
	ENGINE_API const TArray<TRefCountPtr<FMaterialUniformExpression> >& GetUniformScalarParameterExpressions() const;

#if WITH_EDITOR
	const TArray<FString>& GetCompileErrors() const { return CompileErrors; }
	void SetCompileErrors(const TArray<FString>& InCompileErrors) { CompileErrors = InCompileErrors; }
	const TArray<UMaterialExpression*>& GetErrorExpressions() const { return ErrorExpressions; }
	const FGuid& GetLegacyId() const { return Id_DEPRECATED; }
#endif // WITH_EDITOR

	ERHIFeatureLevel::Type GetFeatureLevel() const { return FeatureLevel; }
	bool GetUsesDynamicParameter() const 
	{ 
		//@todo - remove non-dynamic parameter particle VF and always support dynamic parameter
		return true; 
	}
	ENGINE_API bool RequiresSceneColorCopy_GameThread() const;
	ENGINE_API bool RequiresSceneColorCopy_RenderThread() const;
	ENGINE_API bool NeedsSceneTextures() const;
	ENGINE_API bool NeedsGBuffer() const;
	ENGINE_API bool UsesEyeAdaptation() const;	
	ENGINE_API bool UsesGlobalDistanceField_GameThread() const;
	ENGINE_API bool UsesWorldPositionOffset_GameThread() const;

	/** Does the material modify the mesh position. */
	ENGINE_API bool MaterialModifiesMeshPosition_RenderThread() const;
	ENGINE_API bool MaterialModifiesMeshPosition_GameThread() const;

	/** Does the material use a pixel depth offset. */
	ENGINE_API bool MaterialUsesPixelDepthOffset() const;

	/** Does the material use a distance cull fade. */
	ENGINE_API bool MaterialUsesDistanceCullFade_GameThread() const;

	/** Does the material use a SceneDepth lookup. */
	ENGINE_API bool MaterialUsesSceneDepthLookup_RenderThread() const;
	ENGINE_API bool MaterialUsesSceneDepthLookup_GameThread() const;

	/** Does the material have a runtime virtual texture output node. */
	ENGINE_API bool HasRuntimeVirtualTextureOutput_RenderThread() const;

	/** Note: This function is only intended for use in deciding whether or not shader permutations are required before material translation occurs. */
	ENGINE_API bool MaterialMayModifyMeshPosition() const;

	class FMaterialShaderMap* GetGameThreadShaderMap() const 
	{ 
		checkSlow(IsInGameThread() || IsInAsyncLoadingThread());
		return GameThreadShaderMap; 
	}

	void SetGameThreadShaderMap(FMaterialShaderMap* InMaterialShaderMap)
	{
		checkSlow(IsInGameThread() || IsInAsyncLoadingThread());
		GameThreadShaderMap = InMaterialShaderMap;

		FMaterial* Material = this;
		ENQUEUE_RENDER_COMMAND(SetGameThreadShaderMap)([Material](FRHICommandListImmediate& RHICmdList)
		{
			Material->RenderingThreadShaderMap = Material->GameThreadShaderMap;
		});
	}

	void SetInlineShaderMap(FMaterialShaderMap* InMaterialShaderMap)
	{
		checkSlow(IsInGameThread() || IsInAsyncLoadingThread());
		GameThreadShaderMap = InMaterialShaderMap;
		bContainsInlineShaders = true;
		bLoadedCookedShaderMapId = true;

		FMaterial* Material = this;
		ENQUEUE_RENDER_COMMAND(SetInlineShaderMap)([Material](FRHICommandListImmediate& RHICmdList)
		{
			Material->RenderingThreadShaderMap = Material->GameThreadShaderMap;
		});
	}

	ENGINE_API class FMaterialShaderMap* GetRenderingThreadShaderMap() const;

	/** Note: SetGameThreadShaderMap must also be called with the same value, but from the game thread. */
	ENGINE_API void SetRenderingThreadShaderMap(FMaterialShaderMap* InMaterialShaderMap);

#if WITH_EDITOR
	void RemoveOutstandingCompileId(const int32 OldOutstandingCompileShaderMapId )
	{
		OutstandingCompileShaderMapIds.Remove( OldOutstandingCompileShaderMapId );
	}
#endif // WITH_EDITOR

	ENGINE_API virtual void AddReferencedObjects(FReferenceCollector& Collector);

	virtual const TArray<UObject*>& GetReferencedTextures() const = 0;

	/**
	 * Finds the shader matching the template type and the passed in vertex factory, asserts if not found.
	 * Note - Only implemented for FMeshMaterialShaderTypes
	 */
	template<typename ShaderType>
	ShaderType* GetShader(FVertexFactoryType* VertexFactoryType, const typename ShaderType::FPermutationDomain& PermutationVector, bool bFatalIfMissing = true) const
	{
		return GetShader<ShaderType>(VertexFactoryType, PermutationVector.ToDimensionValueId(), bFatalIfMissing);
	}

	template <typename ShaderType>
	ShaderType* GetShader(FVertexFactoryType* VertexFactoryType, int32 PermutationId = 0, bool bFatalIfMissing = true) const
	{
		return (ShaderType*)GetShader(&ShaderType::StaticType, VertexFactoryType, PermutationId, bFatalIfMissing);
	}

	ENGINE_API FShaderPipeline* GetShaderPipeline(class FShaderPipelineType* ShaderPipelineType, FVertexFactoryType* VertexFactoryType, bool bFatalIfNotFound = true) const;

	/** Returns a string that describes the material's usage for debugging purposes. */
	virtual FString GetMaterialUsageDescription() const = 0;

	/** Returns true if this material is allowed to make development shaders via the global CVar CompileShadersForDevelopment. */
	virtual bool GetAllowDevelopmentShaderCompile()const{ return true; }

	/** Returns which shadermap this material is bound to. */
	virtual EMaterialShaderMapUsage::Type GetMaterialShaderMapUsage() const { return EMaterialShaderMapUsage::Default; }

	/**
	* Get user source code for the material, with a list of code snippets to highlight representing the code for each MaterialExpression
	* @param OutSource - generated source code
	* @param OutHighlightMap - source code highlight list
	* @return - true on Success
	*/
	ENGINE_API bool GetMaterialExpressionSource(FString& OutSource);

	/* Helper function to look at both IsMasked and IsDitheredLODTransition to determine if it writes every pixel */
	ENGINE_API bool WritesEveryPixel(bool bShadowPass = false) const;

	/** call during shader compilation jobs setup to fill additional settings that may be required by classes who inherit from this */
	virtual void SetupExtaCompilationSettings(const EShaderPlatform Platform, FExtraShaderCompilerSettings& Settings) const
	{}

	void DumpDebugInfo();
	void SaveShaderStableKeys(EShaderPlatform TargetShaderPlatform, struct FStableShaderKeyAndValue& SaveKeyVal); // arg is non-const, we modify it as we go

#if WITH_EDITOR
	/** 
	 * Adds an FMaterial to the global list.
	 * Any FMaterials that don't belong to a UMaterialInterface need to be registered in this way to work correctly with runtime recompiling of outdated shaders.
	 */
	static void AddEditorLoadedMaterialResource(FMaterial* Material)
	{
		EditorLoadedMaterialResources.Add(Material);
	}

	/** Recompiles any materials in the EditorLoadedMaterialResources list if they are not complete. */
	static void UpdateEditorLoadedMaterialResources(EShaderPlatform InShaderPlatform);

	/** Backs up any FShaders in editor loaded materials to memory through serialization and clears FShader references. */
	static void BackupEditorLoadedMaterialShadersToMemory(TMap<FMaterialShaderMap*, TUniquePtr<TArray<uint8> > >& ShaderMapToSerializedShaderData);
	/** Recreates FShaders in editor loaded materials from the passed in memory, handling shader key changes. */
	static void RestoreEditorLoadedMaterialShadersFromMemory(const TMap<FMaterialShaderMap*, TUniquePtr<TArray<uint8> > >& ShaderMapToSerializedShaderData);
#endif // WITH_EDITOR

protected:
	
	// shared code needed for GetUniformScalarParameterExpressions, GetUniformVectorParameterExpressions, GetUniformCubeTextureExpressions..
	// @return can be 0
	const FMaterialShaderMap* GetShaderMapToUse() const;

#if WITH_EDITOR
	/**
	* Fills the passed array with IDs of shader maps unfinished compilation jobs.
	*/
	void GetShaderMapIDsWithUnfinishedCompilation(TArray<int32>& ShaderMapIds);
#endif // WITH_EDITOR

	/**
	 * Entry point for compiling a specific material property.  This must call SetMaterialProperty. 
	 * @param OverrideShaderFrequency SF_NumFrequencies to not override
	 * @return cases to the proper type e.g. Compiler->ForceCast(Ret, GetValueType(Property));
	 */
	virtual int32 CompilePropertyAndSetMaterialProperty(EMaterialProperty Property, class FMaterialCompiler* Compiler, EShaderFrequency OverrideShaderFrequency = SF_NumFrequencies, bool bUsePreviousFrameTime = false) const = 0;
	
#if HANDLE_CUSTOM_OUTPUTS_AS_MATERIAL_ATTRIBUTES
	/** Used to translate code for custom output attributes such as ClearCoatBottomNormal  */
	virtual int32 CompileCustomAttribute(const FGuid& AttributeID, class FMaterialCompiler* Compiler) const {return INDEX_NONE;}
#endif

	/* Gather any UMaterialExpressionCustomOutput expressions they can be compiled in turn */
	virtual void GatherCustomOutputExpressions(TArray<class UMaterialExpressionCustomOutput*>& OutCustomOutputs) const {}

	/* Gather any UMaterialExpressionCustomOutput expressions in the material and referenced function calls */
	virtual void GatherExpressionsForCustomInterpolators(TArray<class UMaterialExpression*>& OutExpressions) const {}

	/** Returns the index to the Expression in the Expressions array, or -1 if not found. */
	int32 FindExpression(const TArray<TRefCountPtr<FMaterialUniformExpressionTexture> >&Expressions, const FMaterialUniformExpressionTexture &Expression);

	/** Useful for debugging. */
	virtual FString GetBaseMaterialPathName() const { return TEXT(""); }
	virtual FString GetDebugName() const { return GetBaseMaterialPathName(); }

	void SetQualityLevelProperties(EMaterialQualityLevel::Type InQualityLevel, bool bInHasQualityLevelUsage, ERHIFeatureLevel::Type InFeatureLevel)
	{
		QualityLevel = InQualityLevel;
		bHasQualityLevelUsage = bInHasQualityLevelUsage;
		FeatureLevel = InFeatureLevel;
	}

	/** 
	 * Gets the shader map usage of the material, which will be included in the DDC key.
	 * This mechanism allows derived material classes to create different DDC keys with the same base material.
	 * For example lightmass exports diffuse and emissive, each of which requires a material resource with the same base material.
	 */
	virtual EMaterialShaderMapUsage::Type GetShaderMapUsage() const { return EMaterialShaderMapUsage::Default; }

	/** Gets the Guid that represents this material. */
	virtual FGuid GetMaterialId() const = 0;
	
	/** Produces arrays of any shader and vertex factory type that this material is dependent on. */
	ENGINE_API void GetDependentShaderAndVFTypes(EShaderPlatform Platform, TArray<FShaderType*>& OutShaderTypes, TArray<const FShaderPipelineType*>& OutShaderPipelineTypes, TArray<FVertexFactoryType*>& OutVFTypes) const;

	bool GetLoadedCookedShaderMapId() const { return bLoadedCookedShaderMapId; }

private:

#if WITH_EDITOR
	/** 
	 * Tracks FMaterials without a corresponding UMaterialInterface in the editor, for example FExpressionPreviews.
	 * Used to handle the 'recompileshaders changed' command in the editor.
	 * This doesn't have to use a reference counted pointer because materials are removed on destruction.
	 */
	ENGINE_API static TSet<FMaterial*> EditorLoadedMaterialResources;

	TArray<FString> CompileErrors;

	/** List of material expressions which generated a compiler error during the last compile. */
	TArray<UMaterialExpression*> ErrorExpressions;
#endif // WITH_EDITOR

	/** 
	 * Game thread tracked shader map, which is ref counted and manages shader map lifetime. 
	 * The shader map uses deferred deletion so that the rendering thread has a chance to process a release command when the shader map is no longer referenced.
	 * Code that sets this is responsible for updating RenderingThreadShaderMap in a thread safe way.
	 * During an async compile, this will be NULL and will not contain the actual shader map until compilation is complete.
	 */
	TRefCountPtr<FMaterialShaderMap> GameThreadShaderMap;

	/** 
	 * Shader map for this material resource which is accessible by the rendering thread. 
	 * This must be updated along with GameThreadShaderMap, but on the rendering thread.
	 */
	FMaterialShaderMap* RenderingThreadShaderMap;

#if WITH_EDITOR
	/** 
	 * Legacy unique identifier of this material resource.
	 * This functionality is now provided by UMaterial::StateId.
	 */
	FGuid Id_DEPRECATED;

	/** 
	 * Contains the compiling id of this shader map when it is being compiled asynchronously. 
	 * This can be used to access the shader map during async compiling, since GameThreadShaderMap will not have been set yet.
	 */
	TArray<int32, TInlineAllocator<1> > OutstandingCompileShaderMapIds;
#endif // WITH_EDITOR

	/** Quality level that this material is representing. */
	EMaterialQualityLevel::Type QualityLevel;

	/** Whether this material has quality level specific nodes. */
	bool bHasQualityLevelUsage;

	/** Feature level that this material is representing. */
	ERHIFeatureLevel::Type FeatureLevel;

	/** 
	 * Whether this material was loaded with shaders inlined. 
	 * If true, GameThreadShaderMap will contain a reference to the inlined shader map between Serialize and PostLoad.
	 */
	uint32 bContainsInlineShaders : 1;
	uint32 bLoadedCookedShaderMapId : 1;

	/**
	* Compiles this material for Platform, storing the result in OutShaderMap if the compile was synchronous
	*/
	bool BeginCompileShaderMap(
		const FMaterialShaderMapId& ShaderMapId,
		EShaderPlatform Platform, 
		TRefCountPtr<class FMaterialShaderMap>& OutShaderMap, 
		const ITargetPlatform* TargetPlatform = nullptr);

	/** Populates OutEnvironment with defines needed to compile shaders for this material. */
	void SetupMaterialEnvironment(
		EShaderPlatform Platform,
		const FUniformExpressionSet& InUniformExpressionSet,
		FShaderCompilerEnvironment& OutEnvironment
		) const;

	/**
	 * Finds the shader matching the template type and the passed in vertex factory, asserts if not found.
	 */
	ENGINE_API FShader* GetShader(class FMeshMaterialShaderType* ShaderType, FVertexFactoryType* VertexFactoryType, int32 PermutationId, bool bFatalIfMissing = true) const;

	void GetReferencedTexturesHash(EShaderPlatform Platform, FSHAHash& OutHash) const;

	EMaterialQualityLevel::Type GetQualityLevelForShaderMapId() const 
	{
		return bHasQualityLevelUsage ? QualityLevel : EMaterialQualityLevel::Num;
	}

	friend class FMaterialShaderMap;
	friend class FShaderCompilingManager;
	friend class FHLSLMaterialTranslator;
};

/**
 * Cached uniform expression values.
 */
struct FUniformExpressionCache
{
	/** Material uniform buffer. */
	FUniformBufferRHIRef UniformBuffer;
	/** Material uniform buffer. */
	FLocalUniformBuffer LocalUniformBuffer;
	/** Allocated virtual textures, one for each entry in FUniformExpressionSet::VTStacks */
	TArray<IAllocatedVirtualTexture*> AllocatedVTs;
	/** Allocated virtual textures that will need destroying during a call to ResetAllocatedVTs() */
	TArray<IAllocatedVirtualTexture*> OwnedAllocatedVTs;
	/** Ids of parameter collections needed for rendering. */
	TArray<FGuid> ParameterCollections;
	/** True if the cache is up to date. */
	bool bUpToDate;

	/** Shader map that was used to cache uniform expressions on this material.  This is used for debugging and verifying correct behavior. */
	const FMaterialShaderMap* CachedUniformExpressionShaderMap;

	FUniformExpressionCache() :
		bUpToDate(false),
		CachedUniformExpressionShaderMap(NULL)
	{}

	/** Destructor. */
	ENGINE_API ~FUniformExpressionCache();

	void ResetAllocatedVTs();
};

class USubsurfaceProfile;

/**
 * A material render proxy used by the renderer.
 */
class ENGINE_VTABLE FMaterialRenderProxy : public FRenderResource
{
public:

	/** Cached uniform expressions. */
	mutable FUniformExpressionCache UniformExpressionCache[ERHIFeatureLevel::Num];

	/** Cached external texture immutable samplers */
	mutable FImmutableSamplerState ImmutableSamplerState;

	/** Default constructor. */
	ENGINE_API FMaterialRenderProxy();

	/** Destructor. */
	ENGINE_API virtual ~FMaterialRenderProxy();

	/**
	 * Evaluates uniform expressions and stores them in OutUniformExpressionCache.
	 * @param OutUniformExpressionCache - The uniform expression cache to build.
	 * @param MaterialRenderContext - The context for which to cache expressions.
	 */
	void ENGINE_API EvaluateUniformExpressions(FUniformExpressionCache& OutUniformExpressionCache, const FMaterialRenderContext& Context, class FRHICommandList* CommandListIfLocalMode = nullptr) const;

	/**
	 * Caches uniform expressions for efficient runtime evaluation.
	 */
	void ENGINE_API CacheUniformExpressions(bool bRecreateUniformBuffer);

	/**
	 * Enqueues a rendering command to cache uniform expressions for efficient runtime evaluation.
	 * bRecreateUniformBuffer - whether to recreate the material uniform buffer.  
	 *		This is required if the FMaterial is being recompiled (the uniform buffer layout will change).
	 *		This should only be done if the calling code is using FMaterialUpdateContext to recreate the rendering state of primitives using this material, since cached mesh commands also cache uniform buffer pointers.
	 */
	void ENGINE_API CacheUniformExpressions_GameThread(bool bRecreateUniformBuffer);

	/**
	 * Invalidates the uniform expression cache.
	 */
	void ENGINE_API InvalidateUniformExpressionCache(bool bRecreateUniformBuffer);

	void ENGINE_API UpdateUniformExpressionCacheIfNeeded(ERHIFeatureLevel::Type InFeatureLevel) const;

	// These functions should only be called by the rendering thread.
	/** Returns the effective FMaterial, which can be a fallback if this material's shader map is invalid.  Always returns a valid material pointer. */
	const class FMaterial* GetMaterial(ERHIFeatureLevel::Type InFeatureLevel) const
	{
		const FMaterialRenderProxy* Unused = nullptr;
		return &GetMaterialWithFallback(InFeatureLevel, Unused);
	}
	
	/** 
	 * Finds the FMaterial to use for rendering this FMaterialRenderProxy.  Will fall back to a default material if needed due to a content error, or async compilation.
	 * OutFallbackMaterialRenderProxy - if valid, the default material had to be used and OutFallbackMaterialRenderProxy should be used for rendering.
	 */
	virtual const FMaterial& GetMaterialWithFallback(ERHIFeatureLevel::Type InFeatureLevel, const FMaterialRenderProxy*& OutFallbackMaterialRenderProxy) const = 0;
	/** Returns the FMaterial, without using a fallback if the FMaterial doesn't have a valid shader map. Can return NULL. */
	virtual FMaterial* GetMaterialNoFallback(ERHIFeatureLevel::Type InFeatureLevel) const { return NULL; }
	virtual UMaterialInterface* GetMaterialInterface() const { return NULL; }
	virtual bool GetVectorValue(const FMaterialParameterInfo& ParameterInfo, FLinearColor* OutValue, const FMaterialRenderContext& Context) const = 0;
	virtual bool GetScalarValue(const FMaterialParameterInfo& ParameterInfo, float* OutValue, const FMaterialRenderContext& Context) const = 0;
	virtual bool GetTextureValue(const FMaterialParameterInfo& ParameterInfo,const UTexture** OutValue, const FMaterialRenderContext& Context) const = 0;
	bool IsDeleted() const
	{
		return DeletedFlag != 0;
	}

	void MarkForGarbageCollection()
	{
		MarkedForGarbageCollection = 1;
	}

	bool IsMarkedForGarbageCollection() const
	{
		return MarkedForGarbageCollection != 0;
	}

	// FRenderResource interface.
	ENGINE_API virtual void InitDynamicRHI() override;
	ENGINE_API virtual void ReleaseDynamicRHI() override;
	ENGINE_API virtual void ReleaseResource() override;

	ENGINE_API static const TSet<FMaterialRenderProxy*>& GetMaterialRenderProxyMap() 
	{
		check(!FPlatformProperties::RequiresCookedData());
		return MaterialRenderProxyMap;
	}

	void SetSubsurfaceProfileRT(const USubsurfaceProfile* Ptr) { SubsurfaceProfileRT = Ptr; }
	const USubsurfaceProfile* GetSubsurfaceProfileRT() const { return SubsurfaceProfileRT; }

	ENGINE_API static void UpdateDeferredCachedUniformExpressions();

	static inline bool HasDeferredUniformExpressionCacheRequests() 
	{
		return DeferredUniformExpressionCacheRequests.Num() > 0;
	}

private:
	IAllocatedVirtualTexture* GetPreallocatedVTStack(const FMaterialRenderContext& Context, const FUniformExpressionSet& UniformExpressionSet, const FMaterialVirtualTextureStack& VTStack) const;
	IAllocatedVirtualTexture* AllocateVTStack(const FMaterialRenderContext& Context, const FUniformExpressionSet& UniformExpressionSet, const FMaterialVirtualTextureStack& VTStack) const;

	/** 0 if not set, game thread pointer, do not dereference, only for comparison */
	const USubsurfaceProfile* SubsurfaceProfileRT;

	/** For tracking down a bug accessing a deleted proxy. */
	mutable int8 MarkedForGarbageCollection : 1;
	mutable int8 DeletedFlag : 1;
	mutable int8 ReleaseResourceFlag : 1;
	/** If any VT producer destroyed callbacks have been registered */
	mutable int8 HasVirtualTextureCallbacks : 1;

	/** 
	 * Tracks all material render proxies in all scenes, can only be accessed on the rendering thread.
	 * This is used to propagate new shader maps to materials being used for rendering.
	 */
	ENGINE_API static TSet<FMaterialRenderProxy*> MaterialRenderProxyMap;

	ENGINE_API static TSet<FMaterialRenderProxy*> DeferredUniformExpressionCacheRequests;
};

/**
 * An material render proxy which overrides the material's Color vector parameter.
 */
class ENGINE_VTABLE FColoredMaterialRenderProxy : public FMaterialRenderProxy
{
public:

	const FMaterialRenderProxy* const Parent;
	const FLinearColor Color;
	FName ColorParamName;

	/** Initialization constructor. */
	FColoredMaterialRenderProxy(const FMaterialRenderProxy* InParent,const FLinearColor& InColor, FName InColorParamName = NAME_Color):
		Parent(InParent),
		Color(InColor),
		ColorParamName(InColorParamName)
	{}

	// FMaterialRenderProxy interface.
	ENGINE_API virtual const FMaterial& GetMaterialWithFallback(ERHIFeatureLevel::Type InFeatureLevel, const FMaterialRenderProxy*& OutFallbackMaterialRenderProxy) const;
	ENGINE_API virtual bool GetVectorValue(const FMaterialParameterInfo& ParameterInfo, FLinearColor* OutValue, const FMaterialRenderContext& Context) const;
	ENGINE_API virtual bool GetScalarValue(const FMaterialParameterInfo& ParameterInfo, float* OutValue, const FMaterialRenderContext& Context) const;
	ENGINE_API virtual bool GetTextureValue(const FMaterialParameterInfo& ParameterInfo,const UTexture** OutValue, const FMaterialRenderContext& Context) const;
};


/**
 * An material render proxy which overrides the material's Color vector and Texture parameter (mixed together).
 */
class ENGINE_VTABLE FColoredTexturedMaterialRenderProxy : public FColoredMaterialRenderProxy
{
public:

	const UTexture* Texture;
	FName TextureParamName;

	/** Initialization constructor. */
	FColoredTexturedMaterialRenderProxy(const FMaterialRenderProxy* InParent, const FLinearColor& InColor, FName InColorParamName, const UTexture* InTexture, FName InTextureParamName) :
		FColoredMaterialRenderProxy(InParent, InColor, InColorParamName),
		Texture(InTexture),
		TextureParamName(InTextureParamName)
	{}

	ENGINE_API virtual bool GetTextureValue(const FMaterialParameterInfo& ParameterInfo, const UTexture** OutValue, const FMaterialRenderContext& Context) const;
};


/**
 * A material render proxy which overrides the selection color
 */
class FOverrideSelectionColorMaterialRenderProxy : public FMaterialRenderProxy
{
public:

	const FMaterialRenderProxy* const Parent;
	const FLinearColor SelectionColor;

	/** Initialization constructor. */
	FOverrideSelectionColorMaterialRenderProxy(const FMaterialRenderProxy* InParent, const FLinearColor& InSelectionColor) :
		Parent(InParent),
		SelectionColor(InSelectionColor)
	{
	}

	// FMaterialRenderProxy interface.
	ENGINE_API virtual const FMaterial& GetMaterialWithFallback(ERHIFeatureLevel::Type InFeatureLevel, const FMaterialRenderProxy*& OutFallbackMaterialRenderProxy) const;
	ENGINE_API virtual bool GetVectorValue(const FMaterialParameterInfo& ParameterInfo, FLinearColor* OutValue, const FMaterialRenderContext& Context) const;
	ENGINE_API virtual bool GetScalarValue(const FMaterialParameterInfo& ParameterInfo, float* OutValue, const FMaterialRenderContext& Context) const;
	ENGINE_API virtual bool GetTextureValue(const FMaterialParameterInfo& ParameterInfo, const UTexture** OutValue, const FMaterialRenderContext& Context) const;
};


/**
 * An material render proxy which overrides the material's Color and Lightmap resolution vector parameter.
 */
class FLightingDensityMaterialRenderProxy : public FColoredMaterialRenderProxy
{
public:
	const FVector2D LightmapResolution;

	/** Initialization constructor. */
	FLightingDensityMaterialRenderProxy(const FMaterialRenderProxy* InParent,const FLinearColor& InColor, const FVector2D& InLightmapResolution) :
		FColoredMaterialRenderProxy(InParent, InColor), 
		LightmapResolution(InLightmapResolution)
	{}

	// FMaterialRenderProxy interface.
	virtual bool GetVectorValue(const FMaterialParameterInfo& ParameterInfo, FLinearColor* OutValue, const FMaterialRenderContext& Context) const;
};

/**
 * @return True if BlendMode is translucent (should be part of the translucent rendering).
 */
inline bool IsTranslucentBlendMode(enum EBlendMode BlendMode)
{
	return BlendMode != BLEND_Opaque && BlendMode != BLEND_Masked;
}

/**
 * Implementation of the FMaterial interface for a UMaterial or UMaterialInstance.
 */
class FMaterialResource : public FMaterial
{
public:

	ENGINE_API FMaterialResource();
	virtual ~FMaterialResource() {}

	void SetMaterial(UMaterial* InMaterial, EMaterialQualityLevel::Type InQualityLevel, bool bInQualityLevelHasDifferentNodes, ERHIFeatureLevel::Type InFeatureLevel, UMaterialInstance* InInstance = NULL)
	{
		Material = InMaterial;
		MaterialInstance = InInstance;
		SetQualityLevelProperties(InQualityLevel, bInQualityLevelHasDifferentNodes, InFeatureLevel);
	}

#if WITH_EDITOR
	/** Returns the number of samplers used in this material, or -1 if the material does not have a valid shader map (compile error or still compiling). */
	ENGINE_API int32 GetSamplerUsage() const;
#endif

#if WITH_EDITOR
	ENGINE_API void GetUserInterpolatorUsage(uint32& NumUsedUVScalars, uint32& NumUsedCustomInterpolatorScalars) const;
	ENGINE_API void GetEstimatedNumTextureSamples(uint32& VSSamples, uint32& PSSamples) const;
	ENGINE_API uint32 GetEstimatedNumVirtualTextureLookups() const;
#endif
	ENGINE_API uint32 GetNumVirtualTextureStacks() const;

	ENGINE_API virtual FString GetMaterialUsageDescription() const override;

	// FMaterial interface.
	ENGINE_API virtual void GetShaderMapId(EShaderPlatform Platform, FMaterialShaderMapId& OutId) const override;
	ENGINE_API virtual EMaterialDomain GetMaterialDomain() const override;
	ENGINE_API virtual bool IsTwoSided() const override;
	ENGINE_API virtual bool IsDitheredLODTransition() const override;
	ENGINE_API virtual bool IsTranslucencyWritingCustomDepth() const override;
	ENGINE_API virtual bool IsTranslucencyWritingVelocity() const override;
	ENGINE_API virtual bool IsTangentSpaceNormal() const override;
	ENGINE_API virtual bool ShouldInjectEmissiveIntoLPV() const override;
	ENGINE_API virtual bool ShouldBlockGI() const override;
	ENGINE_API virtual bool ShouldGenerateSphericalParticleNormals() const override;
	ENGINE_API virtual bool ShouldDisableDepthTest() const override;
	ENGINE_API virtual bool ShouldWriteOnlyAlpha() const override;
	ENGINE_API virtual bool ShouldEnableResponsiveAA() const override;
	ENGINE_API virtual bool ShouldDoSSR() const override;
	ENGINE_API virtual bool ShouldDoContactShadows() const override;
	ENGINE_API virtual bool IsLightFunction() const override;
	ENGINE_API virtual bool IsUsedWithEditorCompositing() const override;
	ENGINE_API virtual bool IsDeferredDecal() const override;
	ENGINE_API virtual bool IsVolumetricPrimitive() const override;
	ENGINE_API virtual bool IsWireframe() const override;
	ENGINE_API virtual bool IsUIMaterial() const override;
	ENGINE_API virtual bool IsSpecialEngineMaterial() const override;
	ENGINE_API virtual bool IsUsedWithSkeletalMesh() const override;
	ENGINE_API virtual bool IsUsedWithLandscape() const override;
	ENGINE_API virtual bool IsUsedWithParticleSystem() const override;
	ENGINE_API virtual bool IsUsedWithParticleSprites() const override;
	ENGINE_API virtual bool IsUsedWithBeamTrails() const override;
	ENGINE_API virtual bool IsUsedWithMeshParticles() const override;
	ENGINE_API virtual bool IsUsedWithNiagaraSprites() const override;
	ENGINE_API virtual bool IsUsedWithNiagaraRibbons() const override;
	ENGINE_API virtual bool IsUsedWithNiagaraMeshParticles() const override;
	ENGINE_API virtual bool IsUsedWithStaticLighting() const override;
	ENGINE_API virtual bool IsUsedWithMorphTargets() const override;
	ENGINE_API virtual bool IsUsedWithSplineMeshes() const override;
	ENGINE_API virtual bool IsUsedWithInstancedStaticMeshes() const override;
	ENGINE_API virtual bool IsUsedWithGeometryCollections() const override;
	ENGINE_API virtual bool IsUsedWithAPEXCloth() const override;
	ENGINE_API virtual bool IsUsedWithGeometryCache() const override;
	ENGINE_API virtual enum EMaterialTessellationMode GetTessellationMode() const override;
	ENGINE_API virtual bool IsCrackFreeDisplacementEnabled() const override;
	ENGINE_API virtual bool IsAdaptiveTessellationEnabled() const override;
	ENGINE_API virtual bool IsFullyRough() const override;
	ENGINE_API virtual bool UseNormalCurvatureToRoughness() const override;
	ENGINE_API virtual bool IsUsingFullPrecision() const override;
	ENGINE_API virtual bool IsUsingHQForwardReflections() const override;
	ENGINE_API virtual bool IsUsingPlanarForwardReflections() const override;
	ENGINE_API virtual bool OutputsVelocityOnBasePass() const override;
	ENGINE_API virtual bool IsNonmetal() const override;
	ENGINE_API virtual bool UseLmDirectionality() const override;
	ENGINE_API virtual enum EBlendMode GetBlendMode() const override;
	ENGINE_API virtual enum ERefractionMode GetRefractionMode() const override;
	ENGINE_API virtual uint32 GetDecalBlendMode() const override;
	ENGINE_API virtual uint32 GetMaterialDecalResponse() const override;
	ENGINE_API virtual bool HasNormalConnected() const override;
	ENGINE_API virtual bool HasEmissiveColorConnected() const override;
	ENGINE_API virtual FMaterialShadingModelField GetShadingModels() const override;
	ENGINE_API virtual bool IsShadingModelFromMaterialExpression() const override;
	ENGINE_API virtual enum ETranslucencyLightingMode GetTranslucencyLightingMode() const override;
	ENGINE_API virtual float GetOpacityMaskClipValue() const override;
	ENGINE_API virtual bool GetCastDynamicShadowAsMasked() const override;
	ENGINE_API virtual bool IsDistorted() const override;
	ENGINE_API virtual float GetTranslucencyDirectionalLightingIntensity() const override;
	ENGINE_API virtual float GetTranslucentShadowDensityScale() const override;
	ENGINE_API virtual float GetTranslucentSelfShadowDensityScale() const override;
	ENGINE_API virtual float GetTranslucentSelfShadowSecondDensityScale() const override;
	ENGINE_API virtual float GetTranslucentSelfShadowSecondOpacity() const override;
	ENGINE_API virtual float GetTranslucentBackscatteringExponent() const override;
	ENGINE_API virtual bool IsTranslucencyAfterDOFEnabled() const override;
	ENGINE_API virtual bool IsMobileSeparateTranslucencyEnabled() const override;
	ENGINE_API virtual FLinearColor GetTranslucentMultipleScatteringExtinction() const override;
	ENGINE_API virtual float GetTranslucentShadowStartOffset() const override;
	ENGINE_API virtual bool IsMasked() const override;
	ENGINE_API virtual bool IsDitherMasked() const override;
	ENGINE_API virtual bool AllowNegativeEmissiveColor() const override;
	ENGINE_API virtual FString GetFriendlyName() const override;
	ENGINE_API virtual bool RequiresSynchronousCompilation() const override;
	ENGINE_API virtual bool IsDefaultMaterial() const override;
	ENGINE_API virtual int32 GetNumCustomizedUVs() const override;
	ENGINE_API virtual int32 GetBlendableLocation() const override;
	ENGINE_API virtual bool GetBlendableOutputAlpha() const override;
	ENGINE_API virtual bool IsStencilTestEnabled() const override;
	ENGINE_API virtual uint32 GetStencilRefValue() const override;
	ENGINE_API virtual uint32 GetStencilCompare() const override;
	ENGINE_API virtual float GetRefractionDepthBiasValue() const override;
	ENGINE_API virtual float GetMaxDisplacement() const override;
	ENGINE_API virtual bool ShouldApplyFogging() const override;
	ENGINE_API virtual bool ComputeFogPerPixel() const override;
	ENGINE_API virtual bool HasRuntimeVirtualTextureOutput() const override;
	ENGINE_API virtual bool CastsRayTracedShadows() const override;
	ENGINE_API virtual UMaterialInterface* GetMaterialInterface() const override;
	/**
	 * Should shaders compiled for this material be saved to disk?
	 */
	ENGINE_API virtual bool IsPersistent() const override;
	ENGINE_API virtual FGuid GetMaterialId() const override;

#if WITH_EDITOR
	ENGINE_API virtual void NotifyCompilationFinished() override;
#endif // WITH_EDITOR

	ENGINE_API void GetResourceSizeEx(FResourceSizeEx& CumulativeResourceSize);

	ENGINE_API virtual void LegacySerialize(FArchive& Ar) override;

	ENGINE_API virtual const TArray<UObject*>& GetReferencedTextures() const override;

	ENGINE_API virtual void AddReferencedObjects(FReferenceCollector& Collector) override;

	ENGINE_API virtual bool GetAllowDevelopmentShaderCompile() const override;

protected:
	UMaterial* Material;
	UMaterialInstance* MaterialInstance;

	/** Entry point for compiling a specific material property.  This must call SetMaterialProperty. */
	ENGINE_API virtual int32 CompilePropertyAndSetMaterialProperty(EMaterialProperty Property, class FMaterialCompiler* Compiler, EShaderFrequency OverrideShaderFrequency, bool bUsePreviousFrameTime) const override;
#if HANDLE_CUSTOM_OUTPUTS_AS_MATERIAL_ATTRIBUTES
	/** Used to translate code for custom output attributes such as ClearCoatBottomNormal  */
	ENGINE_API virtual int32 CompileCustomAttribute(const FGuid& AttributeID, FMaterialCompiler* Compiler) const override;
#endif

	/* Gives the material a chance to compile any custom output nodes it has added */
	ENGINE_API virtual void GatherCustomOutputExpressions(TArray<class UMaterialExpressionCustomOutput*>& OutCustomOutputs) const override;
	ENGINE_API virtual void GatherExpressionsForCustomInterpolators(TArray<class UMaterialExpression*>& OutExpressions) const override;
	ENGINE_API virtual bool HasVertexPositionOffsetConnected() const override;
	ENGINE_API virtual bool HasPixelDepthOffsetConnected() const override;
	ENGINE_API virtual bool HasMaterialAttributesConnected() const override;
	/** Useful for debugging. */
	ENGINE_API virtual FString GetBaseMaterialPathName() const override;
	ENGINE_API virtual FString GetDebugName() const override;

	friend class FDebugViewModeMaterialProxy; // Needed to redirect compilation

};

/**
 * This class takes care of all of the details you need to worry about when modifying a UMaterial
 * on the main thread. This class should *always* be used when doing so!
 */
class FMaterialUpdateContext
{
	/** UMaterial parents of any UMaterialInterfaces updated within this context. */
	TSet<UMaterial*> UpdatedMaterials;
	/** Materials updated within this context. */
	TSet<UMaterialInterface*> UpdatedMaterialInterfaces;
	/** Active global component reregister context, if any. */
	TUniquePtr<class FGlobalComponentReregisterContext> ComponentReregisterContext;
	/** Active global component render state recreation context, if any. */
	TUniquePtr<class FGlobalComponentRecreateRenderStateContext> ComponentRecreateRenderStateContext;
	/** The shader platform that was being processed - can control if we need to update components */
	EShaderPlatform ShaderPlatform;
	/** True if the SyncWithRenderingThread option was specified. */
	bool bSyncWithRenderingThread;

public:

	/** Options controlling what is done before/after the material is updated. */
	struct EOptions
	{
		enum Type
		{
			/** Reregister all components while updating the material. */
			ReregisterComponents = 0x1,
			/**
			 * Sync with the rendering thread. This is necessary when modifying a
			 * material exposed to the rendering thread. You may omit this flag if
			 * you have already flushed rendering commands.
			 */
			SyncWithRenderingThread = 0x2,
			/* Recreates only the render state for all components (mutually exclusive with ReregisterComponents) */
			RecreateRenderStates = 0x4,
			/** Default options: Recreate render state, sync with rendering thread. */
			Default = RecreateRenderStates | SyncWithRenderingThread,
		};
	};

	/** Initialization constructor. */
	explicit ENGINE_API FMaterialUpdateContext(uint32 Options = EOptions::Default, EShaderPlatform InShaderPlatform = GMaxRHIShaderPlatform);

	/** Destructor. */
	ENGINE_API ~FMaterialUpdateContext();

	/** Add a material that has been updated to the context. */
	ENGINE_API void AddMaterial(UMaterial* Material);

	/** Adds a material instance that has been updated to the context. */
	ENGINE_API void AddMaterialInstance(UMaterialInstance* Instance);

	/** Adds a material interface that has been updated to the context. */
	ENGINE_API void AddMaterialInterface(UMaterialInterface* Instance);
};

/**
 * Check whether the specified texture is needed to render the material instance.
 * @param Texture	The texture to check.
 * @return bool - true if the material uses the specified texture.
 */
ENGINE_API bool DoesMaterialUseTexture(const UMaterialInterface* Material,const UTexture* CheckTexture);

#if WITH_EDITORONLY_DATA
/** TODO - This can be removed whenever VER_UE4_MATERIAL_ATTRIBUTES_REORDERING is no longer relevant. */
ENGINE_API void DoMaterialAttributeReorder(FExpressionInput* Input, int32 UE4Ver);
#endif // WITH_EDITORONLY_DATA

/**
 * Custom attribute blend functions
 */
typedef int32 (*MaterialAttributeBlendFunction)(FMaterialCompiler* Compiler, int32 A, int32 B, int32 Alpha);

/**
 * Attribute data describing a material property
 */
class FMaterialAttributeDefintion
{
public:
	FMaterialAttributeDefintion(const FGuid& InGUID, const FString& InDisplayName, EMaterialProperty InProperty,
		EMaterialValueType InValueType, const FVector4& InDefaultValue, EShaderFrequency InShaderFrequency,
		int32 InTexCoordIndex = INDEX_NONE, bool bInIsHidden = false, MaterialAttributeBlendFunction InBlendFunction = nullptr);

	int32 CompileDefaultValue(FMaterialCompiler* Compiler);

	bool operator==(const FMaterialAttributeDefintion& Other) const
	{
		return (AttributeID == Other.AttributeID);
	}

	FGuid				AttributeID;
	FString				DisplayName;
	EMaterialProperty	Property;	
	EMaterialValueType	ValueType;
	FVector4			DefaultValue;
	EShaderFrequency	ShaderFrequency;
	int32				TexCoordIndex;

	// Optional function pointer for custom blend behavior
	MaterialAttributeBlendFunction BlendFunction;

	// Hidden from auto-generated lists but valid for manual material creation
	bool				bIsHidden;
};

/**
 * Attribute data describing a material property used for a custom output
 */
class FMaterialCustomOutputAttributeDefintion : public FMaterialAttributeDefintion
{
public:
	FMaterialCustomOutputAttributeDefintion(const FGuid& InGUID, const FString& InDisplayName, const FString& InFunctionName, EMaterialProperty InProperty,
		EMaterialValueType InValueType, const FVector4& InDefaultValue, EShaderFrequency InShaderFrequency, MaterialAttributeBlendFunction InBlendFunction = nullptr);

	bool operator==(const FMaterialCustomOutputAttributeDefintion& Other) const
	{
		return (AttributeID == Other.AttributeID);
	}

	// Name of function used to access attribute in shader code
	FString							FunctionName;
};

/**
 * Material property to attribute data mappings
 */
class ENGINE_VTABLE FMaterialAttributeDefinitionMap
{
public:
	FMaterialAttributeDefinitionMap()
	: AttributeDDCString(TEXT(""))
	, bIsInitialized(false)
	{
		AttributeMap.Empty(MP_MAX);
		InitializeAttributeMap();
	}

	/** Compiles the default expression for a material attribute */
	ENGINE_API static int32 CompileDefaultExpression(FMaterialCompiler* Compiler, EMaterialProperty Property)
	{
		FMaterialAttributeDefintion* Attribute = GMaterialPropertyAttributesMap.Find(Property);
		return Attribute->CompileDefaultValue(Compiler);
	}

	/** Compiles the default expression for a material attribute */
	ENGINE_API static int32 CompileDefaultExpression(FMaterialCompiler* Compiler, const FGuid& AttributeID)
	{
		FMaterialAttributeDefintion* Attribute = GMaterialPropertyAttributesMap.Find(AttributeID);
		return Attribute->CompileDefaultValue(Compiler);
	}

	/** Returns the display name of a material attribute */
	ENGINE_API static FString GetDisplayName(EMaterialProperty Property)
	{
		FMaterialAttributeDefintion* Attribute = GMaterialPropertyAttributesMap.Find(Property);
		return Attribute->DisplayName;
	}

	/** Returns the display name of a material attribute */
	ENGINE_API static FString GetDisplayName(const FGuid& AttributeID)
	{
		FMaterialAttributeDefintion* Attribute = GMaterialPropertyAttributesMap.Find(AttributeID);
		return Attribute->DisplayName;
	}

	/** Returns the value type of a material attribute */
	ENGINE_API static EMaterialValueType GetValueType(EMaterialProperty Property)
	{
		FMaterialAttributeDefintion* Attribute = GMaterialPropertyAttributesMap.Find(Property);
		return Attribute->ValueType;
	}

	/** Returns the value type of a material attribute */
	ENGINE_API static EMaterialValueType GetValueType(const FGuid& AttributeID)
	{
		FMaterialAttributeDefintion* Attribute = GMaterialPropertyAttributesMap.Find(AttributeID);
		return Attribute->ValueType;
	}
	
	/** Returns the shader frequency of a material attribute */
	ENGINE_API static EShaderFrequency GetShaderFrequency(EMaterialProperty Property)
	{
		FMaterialAttributeDefintion* Attribute = GMaterialPropertyAttributesMap.Find(Property);
		return Attribute->ShaderFrequency;
	}

	/** Returns the shader frequency of a material attribute */
	ENGINE_API static EShaderFrequency GetShaderFrequency(const FGuid& AttributeID)
	{
		FMaterialAttributeDefintion* Attribute = GMaterialPropertyAttributesMap.Find(AttributeID);
		return Attribute->ShaderFrequency;
	}

	/** Returns the attribute ID for a matching material property */
	ENGINE_API static FGuid GetID(EMaterialProperty Property)
	{
		FMaterialAttributeDefintion* Attribute = GMaterialPropertyAttributesMap.Find(Property);
		return Attribute->AttributeID;
	}

	/** Returns a the material property matching the specified attribute AttributeID */
	ENGINE_API static EMaterialProperty GetProperty(const FGuid& AttributeID)
	{
		if (FMaterialAttributeDefintion* Attribute = GMaterialPropertyAttributesMap.Find(AttributeID))
		{
			return Attribute->Property;
		}
		return MP_MAX;
	}

	/** Returns the custom blend function of a material attribute */
	ENGINE_API static MaterialAttributeBlendFunction GetBlendFunction(const FGuid& AttributeID)
	{
		FMaterialAttributeDefintion* Attribute = GMaterialPropertyAttributesMap.Find(AttributeID);
		return Attribute->BlendFunction;
	}

	/** Returns a default attribute AttributeID */
	ENGINE_API static FGuid GetDefaultID()
	{
		return GMaterialPropertyAttributesMap.Find(MP_MAX)->AttributeID;
	}

	/** Appends a hash of the property map intended for use with the DDC key */
	ENGINE_API static void AppendDDCKeyString(FString& String);

	/** Appends a new attribute definition to the custom output list */
	ENGINE_API static void AddCustomAttribute(const FGuid& AttributeID, const FString& DisplayName, const FString& FunctionName, EMaterialValueType ValueType, const FVector4& DefaultValue, MaterialAttributeBlendFunction BlendFunction = nullptr);

	/** Returns a list of registered custom attributes */
	ENGINE_API static void GetCustomAttributeList(TArray<FMaterialCustomOutputAttributeDefintion>& CustomAttributeList);

	ENGINE_API static const TArray<FGuid>& GetOrderedVisibleAttributeList()
	{
		return GMaterialPropertyAttributesMap.OrderedVisibleAttributeList;
	}

private:
	// Customization class for displaying data in the material editor
	friend class FMaterialAttributePropertyDetails;

	/** Returns a list of display names and their associated GUIDs for material properties */
	ENGINE_API static void GetDisplayNameToIDList(TArray<TPair<FString, FGuid>>& NameToIDList);

	// Internal map management
	void InitializeAttributeMap();

	void Add(const FGuid& AttributeID, const FString& DisplayName, EMaterialProperty Property,
		EMaterialValueType ValueType, const FVector4& DefaultValue, EShaderFrequency ShaderFrequency,
		int32 TexCoordIndex = INDEX_NONE, bool bIsHidden = false, MaterialAttributeBlendFunction BlendFunction = nullptr);

	ENGINE_API FMaterialAttributeDefintion* Find(const FGuid& AttributeID);
	ENGINE_API FMaterialAttributeDefintion* Find(EMaterialProperty Property);

	ENGINE_API static FMaterialAttributeDefinitionMap GMaterialPropertyAttributesMap;

	TMap<EMaterialProperty, FMaterialAttributeDefintion>	AttributeMap; // Fixed map of compile-time definitions
	TArray<FMaterialCustomOutputAttributeDefintion>			CustomAttributes; // Array of custom output definitions
	TArray<FGuid>											OrderedVisibleAttributeList; // List used for consistency with e.g. combobox filling

	FString													AttributeDDCString;
	bool bIsInitialized;
};

struct FMaterialResourceLocOnDisk
{
	// Relative offset to package (uasset/umap + uexp) beginning
	uint32 Offset;
	// ERHIFeatureLevel::Type
	uint8 FeatureLevel;
	// EMaterialQualityLevel::Type
	uint8 QualityLevel;
};

inline FArchive& operator<<(FArchive& Ar, FMaterialResourceLocOnDisk& Loc)
{
	Ar << Loc.Offset;
	Ar << Loc.FeatureLevel;
	Ar << Loc.QualityLevel;
	return Ar;
}

class FMaterialResourceMemoryWriter final : public FMemoryWriter
{
public:
	FMaterialResourceMemoryWriter(FArchive& Ar);

	virtual ~FMaterialResourceMemoryWriter();

	FMaterialResourceMemoryWriter(const FMaterialResourceMemoryWriter&) = delete;
	FMaterialResourceMemoryWriter(FMaterialResourceMemoryWriter&&) = delete;
	FMaterialResourceMemoryWriter& operator=(const FMaterialResourceMemoryWriter&) = delete;
	FMaterialResourceMemoryWriter& operator=(FMaterialResourceMemoryWriter&&) = delete;

	virtual FArchive& operator<<(class FName& Name) override;

	virtual const FCustomVersionContainer& GetCustomVersions() const override { return ParentAr->GetCustomVersions(); }

	virtual FString GetArchiveName() const override { return TEXT("FMaterialResourceMemoryWriter"); }

	inline void BeginSerializingMaterialResource()
	{
		Locs.AddUninitialized();
		int64 ResourceOffset = this->Tell();
		Locs.Last().Offset = ResourceOffset;
	}

	inline void EndSerializingMaterialResource(const FMaterialResource& Resource)
	{
		static_assert(ERHIFeatureLevel::Num <= 256, "ERHIFeatureLevel doesn't fit into a byte");
		static_assert(EMaterialQualityLevel::Num <= 256, "EMaterialQualityLevel doesn't fit into a byte");
		check(Resource.GetMaterialInterface());
		Locs.Last().FeatureLevel = uint8(Resource.GetFeatureLevel());
		Locs.Last().QualityLevel = uint8(Resource.GetQualityLevel());
	}

private:
	TArray<uint8> Bytes;
	TArray<FMaterialResourceLocOnDisk> Locs;
	TMap<FNameEntryId, int32> Name2Indices;
	FArchive* ParentAr;

	void SerializeToParentArchive();
};

class FMaterialResourceWriteScope final
{
public:
	FMaterialResourceWriteScope(
		FMaterialResourceMemoryWriter* InAr,
		const FMaterialResource& InResource) :
		Ar(InAr),
		Resource(InResource)
	{
		check(Ar);
		Ar->BeginSerializingMaterialResource();
	}

	~FMaterialResourceWriteScope()
	{
		Ar->EndSerializingMaterialResource(Resource);
	}

private:
	FMaterialResourceMemoryWriter* Ar;
	const FMaterialResource& Resource;
};

class FMaterialResourceProxyReader final : public FArchiveProxy
{
public:
	FMaterialResourceProxyReader(
		FArchive& Ar,
		ERHIFeatureLevel::Type FeatureLevel = ERHIFeatureLevel::Num,
		EMaterialQualityLevel::Type QualityLevel = EMaterialQualityLevel::Num);

	FMaterialResourceProxyReader(
		const TCHAR* Filename,
		uint32 NameMapOffset,
		ERHIFeatureLevel::Type FeatureLevel = ERHIFeatureLevel::Num,
		EMaterialQualityLevel::Type QualityLevel = EMaterialQualityLevel::Num);

	virtual ~FMaterialResourceProxyReader();

	FMaterialResourceProxyReader(const FMaterialResourceProxyReader&) = delete;
	FMaterialResourceProxyReader(FMaterialResourceProxyReader&&) = delete;
	FMaterialResourceProxyReader& operator=(const FMaterialResourceProxyReader&) = delete;
	FMaterialResourceProxyReader& operator=(FMaterialResourceProxyReader&&) = delete;

	virtual int64 Tell() override
	{
		return InnerArchive.Tell() - OffsetToFirstResource;
	}

	virtual void Seek(int64 InPos) override
	{
		InnerArchive.Seek(OffsetToFirstResource + InPos);
	}

	virtual FArchive& operator<<(class FName& Name) override;

	virtual FString GetArchiveName() const override { return TEXT("FMaterialResourceProxyReader"); }

private:
	TArray<FName> Names;
	int64 OffsetToFirstResource;
	int64 OffsetToEnd;
	bool bReleaseInnerArchive;

	void Initialize(
		ERHIFeatureLevel::Type FeatureLevel,
		EMaterialQualityLevel::Type QualityLevel,
		bool bSeekToEnd = false);
};

/** Sets shader maps on the specified materials without blocking. */
extern ENGINE_API void SetShaderMapsOnMaterialResources(const TMap<FMaterial*, FMaterialShaderMap*>& MaterialsToUpdate);

ENGINE_API uint8 GetRayTracingMaskFromMaterial(const EBlendMode BlendMode);

#if STORE_ONLY_ACTIVE_SHADERMAPS
bool HasMaterialResource(
	UMaterial* Material,
	ERHIFeatureLevel::Type FeatureLevel,
	EMaterialQualityLevel::Type QualityLevel);

const FMaterialResourceLocOnDisk* FindMaterialResourceLocOnDisk(
	const TArray<FMaterialResourceLocOnDisk>& DiskLocations,
	ERHIFeatureLevel::Type FeatureLevel,
	EMaterialQualityLevel::Type QualityLevel);

bool ReloadMaterialResource(
	FMaterialResource* InOutMaterialResource,
	const FString& PackageName,
	uint32 OffsetToFirstResource,
	ERHIFeatureLevel::Type FeatureLevel,
	EMaterialQualityLevel::Type QualityLevel);
#endif
