// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	ShaderBaseClasses.cpp: Shader base classes
=============================================================================*/

#include "ShaderBaseClasses.h"
#include "PostProcess/SceneRenderTargets.h"
#include "RendererModule.h"
#include "ScenePrivate.h"
#include "ParameterCollection.h"
#include "VT/VirtualTextureTest.h"
#include "VT/VirtualTextureSpace.h"
#include "VT/VirtualTextureSystem.h"

IMPLEMENT_TYPE_LAYOUT(FMaterialShader);
IMPLEMENT_TYPE_LAYOUT(FMeshMaterialShader);
IMPLEMENT_TYPE_LAYOUT(FBaseHS);
IMPLEMENT_TYPE_LAYOUT(FBaseDS);
IMPLEMENT_TYPE_LAYOUT(FDebugUniformExpressionSet);

/** If true, cached uniform expressions are allowed. */
int32 FMaterialShader::bAllowCachedUniformExpressions = true;

/** Console variable ref to toggle cached uniform expressions. */
FAutoConsoleVariableRef FMaterialShader::CVarAllowCachedUniformExpressions(
	TEXT("r.AllowCachedUniformExpressions"),
	bAllowCachedUniformExpressions,
	TEXT("Allow uniform expressions to be cached."),
	ECVF_RenderThreadSafe);

void FMeshMaterialShaderElementData::InitializeMeshMaterialData(const FSceneView* SceneView, const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy, const FMeshBatch& RESTRICT MeshBatch, int32 StaticMeshId, bool bAllowStencilDither)
{
	FadeUniformBuffer = GDistanceCullFadedInUniformBuffer.GetUniformBufferRHI();
	DitherUniformBuffer = GDitherFadedInUniformBuffer.GetUniformBufferRHI();

	if (SceneView && StaticMeshId >= 0)
	{
		checkSlow(SceneView->bIsViewInfo);
		const FViewInfo* ViewInfo = (FViewInfo*)SceneView;

		if (MeshBatch.bDitheredLODTransition && !(bAllowStencilDither && ViewInfo->bAllowStencilDither))
		{
			if (ViewInfo->StaticMeshFadeOutDitheredLODMap[StaticMeshId])
			{
				DitherUniformBuffer = ViewInfo->DitherFadeOutUniformBuffer;
			}
			else if (ViewInfo->StaticMeshFadeInDitheredLODMap[StaticMeshId])
			{
				DitherUniformBuffer = ViewInfo->DitherFadeInUniformBuffer;
			}
		}

		if (PrimitiveSceneProxy)
		{
			int32 const PrimitiveIndex = PrimitiveSceneProxy->GetPrimitiveSceneInfo()->GetIndex();

			if (ViewInfo->PrimitiveFadeUniformBufferMap[PrimitiveIndex])
			{
				FadeUniformBuffer = ViewInfo->PrimitiveFadeUniformBuffers[PrimitiveIndex];
			}
		}
	}
}

FName FMaterialShader::UniformBufferLayoutName(TEXT("Material"));

FMaterialShader::FMaterialShader(const FMaterialShaderType::CompiledShaderInitializerType& Initializer)
:	FShader(Initializer)
#if WITH_EDITORONLY_DATA
, DebugUniformExpressionSet(Initializer.UniformExpressionSet)
, DebugUniformExpressionUBLayout(FRHIUniformBufferLayout::Zero)
, DebugDescription(Initializer.DebugDescription)
#endif // WITH_EDITORONLY_DATA
{
#if WITH_EDITORONLY_DATA
	check(!DebugDescription.IsEmpty());
	DebugUniformExpressionUBLayout.CopyFrom(Initializer.UniformExpressionSet.GetUniformBufferLayout());
#endif // WITH_EDITORONLY_DATA

	// Bind the material uniform buffer parameter.
	MaterialUniformBuffer.Bind(Initializer.ParameterMap,TEXT("Material"));

	for (int32 CollectionIndex = 0; CollectionIndex < Initializer.UniformExpressionSet.ParameterCollections.Num(); CollectionIndex++)
	{
		FShaderUniformBufferParameter CollectionParameter;
		CollectionParameter.Bind(Initializer.ParameterMap,*FString::Printf(TEXT("MaterialCollection%u"), CollectionIndex));
		ParameterCollectionUniformBuffers.Add(CollectionParameter);
	}

	SceneTextureParameters.Bind(Initializer);
	
}

FRHIUniformBuffer* FMaterialShader::GetParameterCollectionBuffer(const FGuid& Id, const FSceneInterface* SceneInterface) const
{
	const FScene* Scene = (const FScene*)SceneInterface;
	FRHIUniformBuffer* UniformBuffer = Scene ? Scene->GetParameterCollectionBuffer(Id) : nullptr;

	if (!UniformBuffer)
	{
		FMaterialParameterCollectionInstanceResource** CollectionResource = GDefaultMaterialParameterCollectionInstances.Find(Id);
		if (CollectionResource && *CollectionResource)
		{
			UniformBuffer = (*CollectionResource)->GetUniformBuffer();
		}
	}

	return UniformBuffer;
}

#if !(UE_BUILD_TEST || UE_BUILD_SHIPPING || !WITH_EDITOR)
void FMaterialShader::VerifyExpressionAndShaderMaps(const FMaterialRenderProxy* MaterialRenderProxy, const FMaterial& Material, const FUniformExpressionCache* UniformExpressionCache) const
{
	// Validate that the shader is being used for a material that matches the uniform expression set the shader was compiled for.
	FMaterialShaderMap* ShaderMap = Material.GetRenderingThreadShaderMap();
	const FUniformExpressionSet& MaterialUniformExpressionSet = ShaderMap->GetUniformExpressionSet();
	bool bUniformExpressionSetMismatch = !DebugUniformExpressionSet.Matches(MaterialUniformExpressionSet)
		|| UniformExpressionCache->CachedUniformExpressionShaderMap != ShaderMap;
	if (!bUniformExpressionSetMismatch)
	{
		auto DumpUB = [](const FRHIUniformBufferLayout& Layout)
		{
			UE_LOG(LogShaders, Warning, TEXT("Layout %s, Hash %08x"), *Layout.GetDebugName(), Layout.GetHash());
			FString ResourcesString;
			for (int32 Index = 0; Index < Layout.Resources.Num(); ++Index)
			{
				ResourcesString += FString::Printf(TEXT("%d "), (uint8)Layout.Resources[Index].MemberType);
			}
			UE_LOG(LogShaders, Warning, TEXT("Layout CB Size %d %d Resources: %s"), Layout.ConstantBufferSize, Layout.Resources.Num(), *ResourcesString);
		};
		if (UniformExpressionCache->LocalUniformBuffer.IsValid())
		{
			if (UniformExpressionCache->LocalUniformBuffer.BypassUniform)
			{
				if (DebugUniformExpressionUBLayout.GetHash() != UniformExpressionCache->LocalUniformBuffer.BypassUniform->GetLayout().GetHash())
				{
					UE_LOG(LogShaders, Warning, TEXT("Material Expression UB mismatch!"));
					DumpUB(DebugUniformExpressionUBLayout);
					DumpUB(UniformExpressionCache->LocalUniformBuffer.BypassUniform->GetLayout());
					bUniformExpressionSetMismatch = true;
				}
			}
			else
			{
				if (DebugUniformExpressionUBLayout.GetHash() != UniformExpressionCache->LocalUniformBuffer.WorkArea->Layout->GetHash())
				{
					UE_LOG(LogShaders, Warning, TEXT("Material Expression UB mismatch!"));
					DumpUB(DebugUniformExpressionUBLayout);
					DumpUB(*UniformExpressionCache->LocalUniformBuffer.WorkArea->Layout);
					bUniformExpressionSetMismatch = true;
				}
			}
		}
		else
		{
			if (DebugUniformExpressionUBLayout.GetHash() != UniformExpressionCache->UniformBuffer->GetLayout().GetHash())
			{
				UE_LOG(LogShaders, Warning, TEXT("Material Expression UB mismatch!"));
				DumpUB(DebugUniformExpressionUBLayout);
				DumpUB(UniformExpressionCache->UniformBuffer->GetLayout());
				bUniformExpressionSetMismatch = true;
			}
		}
	}
	if (bUniformExpressionSetMismatch)
	{
		const FShaderType* ShaderType = GetType(ShaderMap->GetPointerTable());
		const FString ProxyName = *MaterialRenderProxy->GetFriendlyName();
		const FString MaterialName = *Material.GetFriendlyName();
		const TCHAR* ShaderMapDesc = ShaderMap->GetDebugDescription();
		UE_LOG(
			LogShaders,
			Fatal,
			TEXT("%s shader uniform expression set mismatch for material %s/%s.\n")
			TEXT("Shader compilation info:                %s\n")
			TEXT("Material render proxy compilation info: %s\n")
			TEXT("Shader uniform expression set:   %u vectors, %u scalars, %u 2D textures, %u cube textures, %u array textures, %u 3D textures, %u virtual textures, shader map %p\n")
			TEXT("Material uniform expression set: %u vectors, %u scalars, %u 2D textures, %u cube textures, %u array textures, %u 3D textures, %u virtual textures, shader map %p\n"),
			ShaderType->GetName(),
			*ProxyName,
			*MaterialName,
			*DebugDescription,
			ShaderMapDesc,
			DebugUniformExpressionSet.NumVectorExpressions,
			DebugUniformExpressionSet.NumScalarExpressions,
			DebugUniformExpressionSet.NumTextureExpressions[(uint32)EMaterialTextureParameterType::Standard2D],
			DebugUniformExpressionSet.NumTextureExpressions[(uint32)EMaterialTextureParameterType::Cube],
			DebugUniformExpressionSet.NumTextureExpressions[(uint32)EMaterialTextureParameterType::Array2D],
			DebugUniformExpressionSet.NumTextureExpressions[(uint32)EMaterialTextureParameterType::Volume],
			DebugUniformExpressionSet.NumTextureExpressions[(uint32)EMaterialTextureParameterType::Virtual],
			UniformExpressionCache->CachedUniformExpressionShaderMap,
			MaterialUniformExpressionSet.UniformVectorPreshaders.Num(),
			MaterialUniformExpressionSet.UniformScalarPreshaders.Num(),
			MaterialUniformExpressionSet.UniformTextureParameters[(uint32)EMaterialTextureParameterType::Standard2D].Num(),
			MaterialUniformExpressionSet.UniformTextureParameters[(uint32)EMaterialTextureParameterType::Cube].Num(),
			MaterialUniformExpressionSet.UniformTextureParameters[(uint32)EMaterialTextureParameterType::Array2D].Num(),
			MaterialUniformExpressionSet.UniformTextureParameters[(uint32)EMaterialTextureParameterType::Volume].Num(),
			MaterialUniformExpressionSet.UniformTextureParameters[(uint32)EMaterialTextureParameterType::Virtual].Num(),
			ShaderMap
		);
	}
}
#endif

template<typename TRHIShader>
void FMaterialShader::SetParametersInner(
	FRHICommandList& RHICmdList,
	TRHIShader* ShaderRHI,
	const FMaterialRenderProxy* MaterialRenderProxy,
	const FMaterial& Material,
	const FSceneView& View)
{
	ERHIFeatureLevel::Type FeatureLevel = View.GetFeatureLevel();
	checkf(Material.GetRenderingThreadShaderMap(), TEXT("RenderingThreadShaderMap: %i"), Material.GetRenderingThreadShaderMap() ? 1 : 0);
	checkf(Material.GetRenderingThreadShaderMap()->IsValidForRendering(true) && Material.GetFeatureLevel() == FeatureLevel, TEXT("IsValid:%i, MaterialFeatureLevel:%i, FeatureLevel:%i"), Material.GetRenderingThreadShaderMap()->IsValidForRendering() ? 1 : 0, Material.GetFeatureLevel(), FeatureLevel);

	FUniformExpressionCache* UniformExpressionCache = &MaterialRenderProxy->UniformExpressionCache[FeatureLevel];
	bool bUniformExpressionCacheNeedsDelete = false;
	bool bForceExpressionEvaluation = false;

#if !(UE_BUILD_TEST || UE_BUILD_SHIPPING || !WITH_EDITOR)
	if (!(!bAllowCachedUniformExpressions || !UniformExpressionCache->bUpToDate))
	{
		// UE-46061 - Workaround for a rare crash with an outdated cached shader map
		if (UniformExpressionCache->CachedUniformExpressionShaderMap != Material.GetRenderingThreadShaderMap())
		{
			FMaterialShaderMap* ShaderMap = Material.GetRenderingThreadShaderMap();
			UMaterialInterface* MtlInterface = Material.GetMaterialInterface();
			UMaterialInterface* ProxyInterface = MaterialRenderProxy->GetMaterialInterface();

			const FShaderType* ShaderType = GetType(ShaderMap->GetPointerTable());
			ensureMsgf(false,
				TEXT("%s shader uniform expression set mismatched shader map for material %s/%s, forcing expression cache evaluation.\n")
				TEXT("Material:  %s\n")
				TEXT("Proxy:  %s\n"),
				ShaderType->GetName(),
				*MaterialRenderProxy->GetFriendlyName(), *Material.GetFriendlyName(),
				MtlInterface ? *MtlInterface->GetFullName() : TEXT("nullptr"),
				ProxyInterface ? *ProxyInterface->GetFullName() : TEXT("nullptr"));
			bForceExpressionEvaluation = true;
		}
	}
#endif

	if (!bAllowCachedUniformExpressions || !UniformExpressionCache->bUpToDate || bForceExpressionEvaluation)
	{
		FMaterialRenderContext MaterialRenderContext(MaterialRenderProxy, Material, &View);
		bUniformExpressionCacheNeedsDelete = true;
		UniformExpressionCache = new FUniformExpressionCache();
		MaterialRenderProxy->EvaluateUniformExpressions(*UniformExpressionCache, MaterialRenderContext, &RHICmdList);
		SetLocalUniformBufferParameter(RHICmdList, ShaderRHI, MaterialUniformBuffer, UniformExpressionCache->LocalUniformBuffer);
	}
	else
	{
		SetUniformBufferParameter(RHICmdList, ShaderRHI, MaterialUniformBuffer, UniformExpressionCache->UniformBuffer);
	}

#if !(UE_BUILD_TEST || UE_BUILD_SHIPPING || !WITH_EDITOR)
	VerifyExpressionAndShaderMaps(MaterialRenderProxy, Material, UniformExpressionCache);
#endif

	{
		const TArray<FGuid>& ParameterCollections = UniformExpressionCache->ParameterCollections;
		const int32 ParameterCollectionsNum = ParameterCollections.Num();

		// For shipping and test builds the assert above will be compiled out, but we're trying to verify that this condition is never hit.
		if (ParameterCollectionUniformBuffers.Num() < ParameterCollectionsNum)
		{
			UE_LOG(LogRenderer, Warning,
				TEXT("ParameterCollectionUniformBuffers.Num() [%u] < ParameterCollectionsNum [%u], this would crash below on SetUniformBufferParameter.\n")
				TEXT("RenderProxy=%s Material=%s"),
				ParameterCollectionUniformBuffers.Num(),
				ParameterCollectionsNum,
				*MaterialRenderProxy->GetFriendlyName(),
				*Material.GetFriendlyName()
				);
		}

		check(ParameterCollectionUniformBuffers.Num() >= ParameterCollectionsNum);

		

		int32 NumToSet = FMath::Min(ParameterCollectionUniformBuffers.Num(), ParameterCollections.Num());

		// Find each referenced parameter collection's uniform buffer in the scene and set the parameter
		for (int32 CollectionIndex = 0; CollectionIndex < NumToSet; CollectionIndex++)
		{			
			FRHIUniformBuffer* UniformBuffer = GetParameterCollectionBuffer(ParameterCollections[CollectionIndex], View.Family->Scene);

			if (!UniformBuffer)
			{
				// Dump the currently registered parameter collections and the ID we failed to find.
				// In a cooked project these numbers are persistent so we can track back to the original
				// parameter collection that was being referenced and no longer exists
				FString InstancesString;
				TMultiMap<FGuid, FMaterialParameterCollectionInstanceResource*>::TIterator Iter = GDefaultMaterialParameterCollectionInstances.CreateIterator();
				while (Iter)
				{
					FMaterialParameterCollectionInstanceResource* Instance = Iter.Value();
					InstancesString += FString::Printf(TEXT("\n0x%p: %s: %s"),
						Instance, Instance ? *Instance->GetOwnerName().ToString() : TEXT("None"), *Iter.Key().ToString());
					++Iter;
				}

				UE_LOG(LogRenderer, Fatal, TEXT("Failed to find parameter collection buffer with GUID '%s'.\n")
					TEXT("Currently %i listed default instances: %s"),
					*ParameterCollections[CollectionIndex].ToString(),
					GDefaultMaterialParameterCollectionInstances.Num(), *InstancesString);
			}

			SetUniformBufferParameter(RHICmdList, ShaderRHI, ParameterCollectionUniformBuffers[CollectionIndex], UniformBuffer);			
		}
	}

	if (bUniformExpressionCacheNeedsDelete)
	{
		delete UniformExpressionCache;
	}
}

template<typename TRHIShader>
void FMaterialShader::SetParameters(
	FRHICommandList& RHICmdList,
	TRHIShader* ShaderRHI,
	const FMaterialRenderProxy* MaterialRenderProxy,
	const FMaterial& Material,
	const FSceneView& View,
	const TUniformBufferRef<FViewUniformShaderParameters>& ViewUniformBuffer,
	ESceneTextureSetupMode SceneTextureSetupMode)
{
	SetViewParameters(RHICmdList, ShaderRHI, View, ViewUniformBuffer);
	FMaterialShader::SetParametersInner(RHICmdList, ShaderRHI, MaterialRenderProxy, Material, View);

	SceneTextureParameters.Set(RHICmdList, ShaderRHI, View.FeatureLevel, SceneTextureSetupMode);
}

template<typename TRHIShader>
void FMaterialShader::SetParameters(
	FRHICommandList& RHICmdList,
	TRHIShader* ShaderRHI,
	const FMaterialRenderProxy* MaterialRenderProxy,
	const FMaterial& Material,
	const FViewInfo& View,
	const TUniformBufferRef<FViewUniformShaderParameters>& ViewUniformBuffer,
	ESceneTextureSetupMode SceneTextureSetupMode)
{
	SetViewParameters(RHICmdList, ShaderRHI, View, ViewUniformBuffer);
	FMaterialShader::SetParametersInner(RHICmdList, ShaderRHI, MaterialRenderProxy, Material, View);

	if (SceneTextureParameters.IsBound())
	{
		if (FSceneInterface::GetShadingPath(View.FeatureLevel) == EShadingPath::Deferred)
		{
			FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);
			FSceneTexturesUniformParameters UniformParameters;
			SetupSceneTextureUniformParameters(SceneContext, View.FeatureLevel, SceneTextureSetupMode, UniformParameters);
			UniformParameters.EyeAdaptation = GetEyeAdaptation(View);
			TUniformBufferRef<FSceneTexturesUniformParameters> UniformBuffer = TUniformBufferRef<FSceneTexturesUniformParameters>::CreateUniformBufferImmediate(UniformParameters, UniformBuffer_SingleDraw);
			SetUniformBufferParameter(RHICmdList, ShaderRHI, SceneTextureParameters.GetUniformBufferParameter(), UniformBuffer);
		}
		else if (FSceneInterface::GetShadingPath(View.FeatureLevel) == EShadingPath::Mobile)
		{
			FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);
			FMobileSceneTextureUniformParameters UniformParameters;
			SetupMobileSceneTextureUniformParameters(SceneContext, View.FeatureLevel, true, SceneContext.bCustomDepthIsValid, UniformParameters);
			if (View.GetEyeAdaptationBuffer())
			{
				UniformParameters.EyeAdaptationBuffer = View.GetEyeAdaptationBuffer()->SRV;
			}
			TUniformBufferRef<FMobileSceneTextureUniformParameters> UniformBuffer = TUniformBufferRef<FMobileSceneTextureUniformParameters>::CreateUniformBufferImmediate(UniformParameters, UniformBuffer_SingleDraw);
			SetUniformBufferParameter(RHICmdList, ShaderRHI, SceneTextureParameters.GetUniformBufferParameter(), UniformBuffer);
		}
	}
}

// Doxygen struggles to parse these explicit specializations. Just ignore them for now.
#if !UE_BUILD_DOCS

#define IMPLEMENT_MATERIAL_SHADER_SetParametersInner( TRHIShader ) \
	template RENDERER_API void FMaterialShader::SetParametersInner< TRHIShader >( \
		FRHICommandList& RHICmdList,					\
		TRHIShader* ShaderRHI,							\
		const FMaterialRenderProxy* MaterialRenderProxy,\
		const FMaterial& Material,						\
		const FSceneView& View							\
	);

IMPLEMENT_MATERIAL_SHADER_SetParametersInner(FRHIVertexShader);
IMPLEMENT_MATERIAL_SHADER_SetParametersInner(FRHIHullShader);
IMPLEMENT_MATERIAL_SHADER_SetParametersInner(FRHIDomainShader);
IMPLEMENT_MATERIAL_SHADER_SetParametersInner(FRHIGeometryShader);
IMPLEMENT_MATERIAL_SHADER_SetParametersInner(FRHIPixelShader);
IMPLEMENT_MATERIAL_SHADER_SetParametersInner(FRHIComputeShader);

#endif

// Doxygen struggles to parse these explicit specializations. Just ignore them for now.
#if !UE_BUILD_DOCS

#define IMPLEMENT_MATERIAL_SHADER_SetParameters( TRHIShader ) \
	template RENDERER_API void FMaterialShader::SetParameters< TRHIShader >( \
		FRHICommandList& RHICmdList,					\
		TRHIShader* ShaderRHI,							\
		const FMaterialRenderProxy* MaterialRenderProxy,\
		const FMaterial& Material,						\
		const FSceneView& View,							\
		const TUniformBufferRef<FViewUniformShaderParameters>& ViewUniformBuffer, \
		ESceneTextureSetupMode SceneTextureSetupMode		\
	); \
	template RENDERER_API void FMaterialShader::SetParameters< TRHIShader >( \
		FRHICommandList& RHICmdList,					\
		TRHIShader* ShaderRHI,							\
		const FMaterialRenderProxy* MaterialRenderProxy,\
		const FMaterial& Material,						\
		const FViewInfo& View,							\
		const TUniformBufferRef<FViewUniformShaderParameters>& ViewUniformBuffer, \
		ESceneTextureSetupMode SceneTextureSetupMode		\
	);

IMPLEMENT_MATERIAL_SHADER_SetParameters(FRHIVertexShader);
IMPLEMENT_MATERIAL_SHADER_SetParameters(FRHIHullShader);
IMPLEMENT_MATERIAL_SHADER_SetParameters(FRHIDomainShader);
IMPLEMENT_MATERIAL_SHADER_SetParameters(FRHIGeometryShader);
IMPLEMENT_MATERIAL_SHADER_SetParameters(FRHIPixelShader);
IMPLEMENT_MATERIAL_SHADER_SetParameters(FRHIComputeShader);

#endif

void FMaterialShader::GetShaderBindings(
	const FScene* Scene,
	const FStaticFeatureLevel FeatureLevel,
	const FMaterialRenderProxy& MaterialRenderProxy,
	const FMaterial& Material,
	FMeshDrawSingleShaderBindings& ShaderBindings) const
{
	check(Material.GetRenderingThreadShaderMap() && Material.GetRenderingThreadShaderMap()->IsValidForRendering() && Material.GetFeatureLevel() == FeatureLevel);

	const FUniformExpressionCache& UniformExpressionCache = MaterialRenderProxy.UniformExpressionCache[FeatureLevel];

	checkf(UniformExpressionCache.bUpToDate, TEXT("UniformExpressionCache should be up to date, RenderProxy=%s Material=%s FeatureLevel=%d"), *MaterialRenderProxy.GetFriendlyName(), *Material.GetFriendlyName(), FeatureLevel);
	checkf(UniformExpressionCache.UniformBuffer, TEXT("NULL UniformBuffer, RenderProxy=%s Material=%s FeatureLevel=%d"), *MaterialRenderProxy.GetFriendlyName(), *Material.GetFriendlyName(), FeatureLevel);

#if !(UE_BUILD_TEST || UE_BUILD_SHIPPING || !WITH_EDITOR)
	VerifyExpressionAndShaderMaps(&MaterialRenderProxy, Material, &UniformExpressionCache);
#endif

	ShaderBindings.Add(MaterialUniformBuffer, UniformExpressionCache.UniformBuffer);

	{
		const TArray<FGuid>& ParameterCollections = UniformExpressionCache.ParameterCollections;
		const int32 ParameterCollectionsNum = ParameterCollections.Num();

		// For shipping and test builds the assert above will be compiled out, but we're trying to verify that this condition is never hit.
		if (ParameterCollectionUniformBuffers.Num() < ParameterCollectionsNum)
		{
			UE_LOG(LogRenderer, Warning,
				TEXT("ParameterCollectionUniformBuffers.Num() [%u] < ParameterCollectionsNum [%u], this would crash below on SetUniformBufferParameter.\n")
				TEXT("RenderProxy=%s Material=%s"),
				ParameterCollectionUniformBuffers.Num(),
				ParameterCollectionsNum,
				*MaterialRenderProxy.GetFriendlyName(),
				*Material.GetFriendlyName()
				);
		}

		check(ParameterCollectionUniformBuffers.Num() >= ParameterCollectionsNum);

		const int32 NumToSet = FMath::Min(ParameterCollectionUniformBuffers.Num(), ParameterCollections.Num());

		// Find each referenced parameter collection's uniform buffer in the scene and set the parameter
		for (int32 CollectionIndex = 0; CollectionIndex < NumToSet; CollectionIndex++)
		{			
			FRHIUniformBuffer* UniformBuffer = GetParameterCollectionBuffer(ParameterCollections[CollectionIndex], Scene);

			if (!UniformBuffer)
			{
				// Dump the currently registered parameter collections and the ID we failed to find.
				// In a cooked project these numbers are persistent so we can track back to the original
				// parameter collection that was being referenced and no longer exists
				FString InstancesString;
				TMultiMap<FGuid, FMaterialParameterCollectionInstanceResource*>::TIterator Iter = GDefaultMaterialParameterCollectionInstances.CreateIterator();
				while (Iter)
				{
					FMaterialParameterCollectionInstanceResource* Instance = Iter.Value();
					InstancesString += FString::Printf(TEXT("\n0x%p: %s: %s"),
						Instance, Instance ? *Instance->GetOwnerName().ToString() : TEXT("None"), *Iter.Key().ToString());
					++Iter;
				}

				UE_LOG(LogRenderer, Fatal, TEXT("Failed to find parameter collection buffer with GUID '%s'.\n")
					TEXT("Currently %i listed default instances: %s"),
					*ParameterCollections[CollectionIndex].ToString(),
					GDefaultMaterialParameterCollectionInstances.Num(), *InstancesString);
			}

			ShaderBindings.Add(ParameterCollectionUniformBuffers[CollectionIndex], UniformBuffer);		
		}
	}
}

#if 0
bool FMaterialShader::Serialize(FArchive& Ar)
{
	const bool bShaderHasOutdatedParameters = FShader::Serialize(Ar);
	Ar << SceneTextureParameters;
	Ar << MaterialUniformBuffer;
	Ar << ParameterCollectionUniformBuffers;

#if !ALLOW_SHADERMAP_DEBUG_DATA
	FDebugUniformExpressionSet	DebugUniformExpressionSet;
	static FName DebugUniformExpressionUB(TEXT("DebugUniformExpressionUB"));
	FRHIUniformBufferLayout		DebugUniformExpressionUBLayout(DebugUniformExpressionUB);
	FString						DebugDescription;
#endif

	Ar << DebugUniformExpressionSet;
	if (Ar.IsLoading())
	{
		FName LayoutName;
		Ar << LayoutName;
		DebugUniformExpressionUBLayout = FRHIUniformBufferLayout(LayoutName);
		Ar << DebugUniformExpressionUBLayout.ConstantBufferSize;

		TArray<uint16> ResourceOffsets;
		TArray<uint8> ResourceTypes;
		Ar << ResourceOffsets;
		Ar << ResourceTypes;

#if ALLOW_SHADERMAP_DEBUG_DATA
		DebugUniformExpressionUBLayout.Resources.Reserve(ResourceOffsets.Num());
		for (int32 i = 0; i < ResourceOffsets.Num(); i++)
		{
			DebugUniformExpressionUBLayout.Resources.Emplace(FRHIUniformBufferLayout::FResourceParameter{ ResourceOffsets[i], EUniformBufferBaseType(ResourceTypes[i]) });
		}
		DebugUniformExpressionUBLayout.ComputeHash();
#endif
	}
	else
	{
		FName LayoutName = DebugUniformExpressionUBLayout.GetDebugName();
		Ar << LayoutName;
		Ar << DebugUniformExpressionUBLayout.ConstantBufferSize;

		TArray<uint16> ResourceOffsets;
		TArray<uint8> ResourceTypes;

		ResourceOffsets.Reserve(DebugUniformExpressionUBLayout.Resources.Num());
		ResourceTypes.Reserve(DebugUniformExpressionUBLayout.Resources.Num());
		for (int32 i = 0; i < DebugUniformExpressionUBLayout.Resources.Num(); i++)
		{
			ResourceOffsets.Emplace(DebugUniformExpressionUBLayout.Resources[i].MemberOffset);
			ResourceTypes.Emplace(uint8(DebugUniformExpressionUBLayout.Resources[i].MemberType));
		}

		Ar << ResourceOffsets;
		Ar << ResourceTypes;
	}
	Ar << DebugDescription;

	return bShaderHasOutdatedParameters;
}
#endif

FMeshMaterialShader::FMeshMaterialShader(const FMeshMaterialShaderType::CompiledShaderInitializerType& Initializer)
	: FMaterialShader(Initializer)
{
	VertexFactoryParameters = Initializer.VertexFactoryType->CreateShaderParameters(Initializer.Target.GetFrequency(), Initializer.ParameterMap);
}

void FMeshMaterialShader::GetShaderBindings(
	const FScene* Scene,
	ERHIFeatureLevel::Type FeatureLevel,
	const FPrimitiveSceneProxy* PrimitiveSceneProxy,
	const FMaterialRenderProxy& MaterialRenderProxy,
	const FMaterial& Material,
	const FMeshPassProcessorRenderState& DrawRenderState,
	const FMeshMaterialShaderElementData& ShaderElementData,
	FMeshDrawSingleShaderBindings& ShaderBindings) const
{
	FMaterialShader::GetShaderBindings(Scene, FeatureLevel, MaterialRenderProxy, Material, ShaderBindings);
	ShaderBindings.Add(PassUniformBuffer, DrawRenderState.GetPassUniformBuffer());
	ShaderBindings.Add(GetUniformBufferParameter<FViewUniformShaderParameters>(), DrawRenderState.GetViewUniformBuffer());
	ShaderBindings.Add(GetUniformBufferParameter<FDistanceCullFadeUniformShaderParameters>(), ShaderElementData.FadeUniformBuffer);
	ShaderBindings.Add(GetUniformBufferParameter<FDitherUniformShaderParameters>(), ShaderElementData.DitherUniformBuffer);
	ShaderBindings.Add(GetUniformBufferParameter<FInstancedViewUniformShaderParameters>(), DrawRenderState.GetInstancedViewUniformBuffer());
}

void FMeshMaterialShader::GetElementShaderBindings(
	const FShaderMapPointerTable& PointerTable,
	const FScene* Scene, 
	const FSceneView* ViewIfDynamicMeshCommand, 
	const FVertexFactory* VertexFactory,
	const EVertexInputStreamType InputStreamType,
	const FStaticFeatureLevel FeatureLevel,
	const FPrimitiveSceneProxy* PrimitiveSceneProxy,
	const FMeshBatch& MeshBatch,
	const FMeshBatchElement& BatchElement, 
	const FMeshMaterialShaderElementData& ShaderElementData,
	FMeshDrawSingleShaderBindings& ShaderBindings,
	FVertexInputStreamArray& VertexStreams) const
{
	const FVertexFactoryType* VertexFactoryType = GetVertexFactoryType(PointerTable);
	if (VertexFactoryType)
	{
		const FVertexFactoryShaderParameters* VFParameters = VertexFactoryParameters.Get();
		if (VFParameters)
		{
			VertexFactoryType->GetShaderParameterElementShaderBindings(GetFrequency(), VFParameters, Scene, ViewIfDynamicMeshCommand, this, InputStreamType, FeatureLevel, VertexFactory, BatchElement, ShaderBindings, VertexStreams);
		}
	}
		
	if (UseGPUScene(GMaxRHIShaderPlatform, FeatureLevel) && VertexFactory->GetPrimitiveIdStreamIndex(InputStreamType) >= 0)
	{
		const FShaderType* ShaderType = GetType(PointerTable);
		ensureMsgf(!GetUniformBufferParameter<FPrimitiveUniformShaderParameters>().IsBound(), TEXT("Shader %s attempted to bind the Primitive uniform buffer even though Vertex Factory computes a PrimitiveId per-instance.  This will break auto-instancing.  Shaders should use GetPrimitiveData(PrimitiveId).Member instead of Primitive.Member."), ShaderType->GetName());
		ensureMsgf(!BatchElement.PrimitiveUniformBuffer, TEXT("FMeshBatchElement was assigned a PrimitiveUniformBuffer even though Vertex Factory %s fetches primitive shader data through a Scene buffer.  The assigned PrimitiveUniformBuffer cannot be respected.  Use PrimitiveUniformBufferResource instead for dynamic primitive data."), ShaderType->GetName());
	}
	else
	{
		if (BatchElement.PrimitiveUniformBuffer)
		{
			ShaderBindings.Add(GetUniformBufferParameter<FPrimitiveUniformShaderParameters>(), BatchElement.PrimitiveUniformBuffer);
		}
		else
		{
			const FShaderType* ShaderType = GetType(PointerTable);
			checkf(BatchElement.PrimitiveUniformBufferResource, TEXT("%s expected a primitive uniform buffer but none was set on BatchElement.PrimitiveUniformBuffer or BatchElement.PrimitiveUniformBufferResource"), ShaderType->GetName());
			ShaderBindings.Add(GetUniformBufferParameter<FPrimitiveUniformShaderParameters>(), BatchElement.PrimitiveUniformBufferResource->GetUniformBufferRHI());
		}
	}
}

/*bool FMeshMaterialShader::Serialize(FArchive& Ar)
{
	bool bShaderHasOutdatedParameters = FMaterialShader::Serialize(Ar);
	Ar << PassUniformBuffer;
	bShaderHasOutdatedParameters |= Ar << VertexFactoryParameters;
	return bShaderHasOutdatedParameters;
}*/

void FMeshMaterialShader::WriteFrozenVertexFactoryParameters(FMemoryImageWriter& Writer, const TMemoryImagePtr<FVertexFactoryShaderParameters>& InVertexFactoryParameters) const
{
	const FVertexFactoryType* VertexFactoryType = GetVertexFactoryType(Writer.TryGetPrevPointerTable());
	InVertexFactoryParameters.WriteMemoryImageWithDerivedType(Writer, VertexFactoryType->GetShaderParameterLayout(GetFrequency()));
}
