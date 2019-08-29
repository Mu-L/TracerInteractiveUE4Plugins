// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	ShaderBaseClasses.cpp: Shader base classes
=============================================================================*/

#include "ShaderBaseClasses.h"
#include "PostProcess/SceneRenderTargets.h"
#include "RendererModule.h"
#include "ScenePrivate.h"
#include "ParameterCollection.h"

/** If true, cached uniform expressions are allowed. */
int32 FMaterialShader::bAllowCachedUniformExpressions = true;

/** Console variable ref to toggle cached uniform expressions. */
FAutoConsoleVariableRef FMaterialShader::CVarAllowCachedUniformExpressions(
	TEXT("r.AllowCachedUniformExpressions"),
	bAllowCachedUniformExpressions,
	TEXT("Allow uniform expressions to be cached."),
	ECVF_RenderThreadSafe);

FName FMaterialShader::UniformBufferLayoutName(TEXT("Material"));

FMaterialShader::FMaterialShader(const FMaterialShaderType::CompiledShaderInitializerType& Initializer)
:	FShader(Initializer)
,	DebugUniformExpressionSet(Initializer.UniformExpressionSet)
,	DebugUniformExpressionUBLayout(FRHIUniformBufferLayout::Zero)
,	DebugDescription(Initializer.DebugDescription)
{
	check(!DebugDescription.IsEmpty());
	DebugUniformExpressionUBLayout.CopyFrom(Initializer.UniformExpressionSet.GetUniformBufferStruct().GetLayout());

	// Bind the material uniform buffer parameter.
	MaterialUniformBuffer.Bind(Initializer.ParameterMap,TEXT("Material"));

	for (int32 CollectionIndex = 0; CollectionIndex < Initializer.UniformExpressionSet.ParameterCollections.Num(); CollectionIndex++)
	{
		FShaderUniformBufferParameter CollectionParameter;
		CollectionParameter.Bind(Initializer.ParameterMap,*FString::Printf(TEXT("MaterialCollection%u"), CollectionIndex));
		ParameterCollectionUniformBuffers.Add(CollectionParameter);
	}

	for (int32 Index = 0; Index < Initializer.UniformExpressionSet.PerFrameUniformScalarExpressions.Num(); Index++)
	{
		FShaderParameter Parameter;
		Parameter.Bind(Initializer.ParameterMap, *FString::Printf(TEXT("UE_Material_PerFrameScalarExpression%u"), Index));
		PerFrameScalarExpressions.Add(Parameter);
	}

	for (int32 Index = 0; Index < Initializer.UniformExpressionSet.PerFrameUniformVectorExpressions.Num(); Index++)
	{
		FShaderParameter Parameter;
		Parameter.Bind(Initializer.ParameterMap, *FString::Printf(TEXT("UE_Material_PerFrameVectorExpression%u"), Index));
		PerFrameVectorExpressions.Add(Parameter);
	}

	for (int32 Index = 0; Index < Initializer.UniformExpressionSet.PerFramePrevUniformScalarExpressions.Num(); Index++)
	{
		FShaderParameter Parameter;
		Parameter.Bind(Initializer.ParameterMap, *FString::Printf(TEXT("UE_Material_PerFramePrevScalarExpression%u"), Index));
		PerFramePrevScalarExpressions.Add(Parameter);
	}

	for (int32 Index = 0; Index < Initializer.UniformExpressionSet.PerFramePrevUniformVectorExpressions.Num(); Index++)
	{
		FShaderParameter Parameter;
		Parameter.Bind(Initializer.ParameterMap, *FString::Printf(TEXT("UE_Material_PerFramePrevVectorExpression%u"), Index));
		PerFramePrevVectorExpressions.Add(Parameter);
	}

	DeferredParameters.Bind(Initializer.ParameterMap);
	SceneColorCopyTexture.Bind(Initializer.ParameterMap, TEXT("SceneColorCopyTexture"));
	SceneColorCopyTextureSampler.Bind(Initializer.ParameterMap, TEXT("SceneColorCopyTextureSampler"));
	EyeAdaptation.Bind(Initializer.ParameterMap, TEXT("EyeAdaptation"));

	InstanceCount.Bind(Initializer.ParameterMap, TEXT("InstanceCount"));
	InstanceOffset.Bind(Initializer.ParameterMap, TEXT("InstanceOffset"));
	VertexOffset.Bind(Initializer.ParameterMap, TEXT("VertexOffset"));
}

FUniformBufferRHIParamRef FMaterialShader::GetParameterCollectionBuffer(const FGuid& Id, const FSceneInterface* SceneInterface) const
{
	const FScene* Scene = (const FScene*)SceneInterface;
	FUniformBufferRHIParamRef UniformBuffer = Scene ? Scene->GetParameterCollectionBuffer(Id) : FUniformBufferRHIParamRef();

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
void FMaterialShader::VerifyExpressionAndShaderMaps(const FMaterialRenderProxy* MaterialRenderProxy, const FMaterial& Material, const FUniformExpressionCache* UniformExpressionCache)
{
	// Validate that the shader is being used for a material that matches the uniform expression set the shader was compiled for.
	const FUniformExpressionSet& MaterialUniformExpressionSet = Material.GetRenderingThreadShaderMap()->GetUniformExpressionSet();
	bool bUniformExpressionSetMismatch = !DebugUniformExpressionSet.Matches(MaterialUniformExpressionSet)
		|| UniformExpressionCache->CachedUniformExpressionShaderMap != Material.GetRenderingThreadShaderMap();
	if (!bUniformExpressionSetMismatch)
	{
		auto DumpUB = [](const FRHIUniformBufferLayout& Layout)
		{
			FString DebugName = Layout.GetDebugName().GetPlainNameString();
			UE_LOG(LogShaders, Warning, TEXT("Layout %s, Hash %08x"), *DebugName, Layout.GetHash());
			FString ResourcesString;
			for (int32 Index = 0; Index < Layout.Resources.Num(); ++Index)
			{
				ResourcesString += FString::Printf(TEXT("%d "), Layout.Resources[Index]);
			}
			UE_LOG(LogShaders, Warning, TEXT("Layout CB Size %d Res Offs %d; %d Resources: %s"), Layout.ConstantBufferSize, Layout.ResourceOffset, Layout.Resources.Num(), *ResourcesString);
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
		UE_LOG(
			LogShaders,
			Fatal,
			TEXT("%s shader uniform expression set mismatch for material %s/%s.\n")
			TEXT("Shader compilation info:                %s\n")
			TEXT("Material render proxy compilation info: %s\n")
			TEXT("Shader uniform expression set:   %u vectors, %u scalars, %u 2D textures, %u cube textures, %u scalars/frame, %u vectors/frame, shader map %p\n")
			TEXT("Material uniform expression set: %u vectors, %u scalars, %u 2D textures, %u cube textures, %u scalars/frame, %u vectors/frame, shader map %p\n"),
			GetType()->GetName(),
			*MaterialRenderProxy->GetFriendlyName(),
			*Material.GetFriendlyName(),
			*DebugDescription,
			*Material.GetRenderingThreadShaderMap()->GetDebugDescription(),
			DebugUniformExpressionSet.NumVectorExpressions,
			DebugUniformExpressionSet.NumScalarExpressions,
			DebugUniformExpressionSet.Num2DTextureExpressions,
			DebugUniformExpressionSet.NumCubeTextureExpressions,
			DebugUniformExpressionSet.NumPerFrameScalarExpressions,
			DebugUniformExpressionSet.NumPerFrameVectorExpressions,
			UniformExpressionCache->CachedUniformExpressionShaderMap,
			MaterialUniformExpressionSet.UniformVectorExpressions.Num(),
			MaterialUniformExpressionSet.UniformScalarExpressions.Num(),
			MaterialUniformExpressionSet.Uniform2DTextureExpressions.Num(),
			MaterialUniformExpressionSet.UniformCubeTextureExpressions.Num(),
			MaterialUniformExpressionSet.PerFrameUniformScalarExpressions.Num(),
			MaterialUniformExpressionSet.PerFrameUniformVectorExpressions.Num(),
			Material.GetRenderingThreadShaderMap()
		);
	}
}
#endif

template<typename ShaderRHIParamRef>
void FMaterialShader::SetParameters(
	FRHICommandList& RHICmdList,
	const ShaderRHIParamRef ShaderRHI,
	const FMaterialRenderProxy* MaterialRenderProxy,
	const FMaterial& Material,
	const FSceneView& View,
	const TUniformBufferRef<FViewUniformShaderParameters>& ViewUniformBuffer,
	bool bDeferredPass,
	ESceneRenderTargetsMode::Type TextureMode)
{
	SetViewParameters(RHICmdList, ShaderRHI, View, ViewUniformBuffer);

	// If the material has cached uniform expressions for selection or hover
	// and that is being overridden by show flags in the editor, recache
	// expressions for this draw call.
	const bool bOverrideSelection =
		GIsEditor &&
		!View.Family->EngineShowFlags.Selection &&
		(MaterialRenderProxy->IsSelected() || MaterialRenderProxy->IsHovered());

	ERHIFeatureLevel::Type FeatureLevel = View.GetFeatureLevel();
	check(Material.GetRenderingThreadShaderMap() && Material.GetRenderingThreadShaderMap()->IsValidForRendering() && Material.GetFeatureLevel() == FeatureLevel);

	FUniformExpressionCache* UniformExpressionCache = &MaterialRenderProxy->UniformExpressionCache[FeatureLevel];
	bool bUniformExpressionCacheNeedsDelete = false;
	bool bForceExpressionEvaluation = false;

#if !(UE_BUILD_TEST || UE_BUILD_SHIPPING || !WITH_EDITOR)
	if (!(!bAllowCachedUniformExpressions || !UniformExpressionCache->bUpToDate || bOverrideSelection))
	{
		// UE-46061 - Workaround for a rare crash with an outdated cached shader map
		if (UniformExpressionCache->CachedUniformExpressionShaderMap != Material.GetRenderingThreadShaderMap())
		{
			UMaterialInterface* MtlInterface = Material.GetMaterialInterface();
			UMaterialInterface* ProxyInterface = MaterialRenderProxy->GetMaterialInterface();

			ensureMsgf(false,
				TEXT("%s shader uniform expression set mismatched shader map for material %s/%s, forcing expression cache evaluation.\n")
				TEXT("Material:  %s\n")
				TEXT("Proxy:  %s\n"),
				GetType()->GetName(),
				*MaterialRenderProxy->GetFriendlyName(), *Material.GetFriendlyName(),
				MtlInterface ? *MtlInterface->GetFullName() : TEXT("nullptr"),
				ProxyInterface ? *ProxyInterface->GetFullName() : TEXT("nullptr"));
			bForceExpressionEvaluation = true;
		}
	}
#endif

	if (!bAllowCachedUniformExpressions || !UniformExpressionCache->bUpToDate || bOverrideSelection || bForceExpressionEvaluation)
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
			FUniformBufferRHIParamRef UniformBuffer = GetParameterCollectionBuffer(ParameterCollections[CollectionIndex], View.Family->Scene);

			if (!UniformBuffer)
			{
				// Dump the currently registered parameter collections and the ID we failed to find.
				// In a cooked project these numbers are persistent so we can track back to the original
				// parameter collection that was being referenced and no longer exists
				FString InstancesString;
				TMap<FGuid, FMaterialParameterCollectionInstanceResource*>::TIterator Iter = GDefaultMaterialParameterCollectionInstances.CreateIterator();
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

	{
		// Per frame material expressions
		const int32 NumScalarExpressions = PerFrameScalarExpressions.Num();
		const int32 NumVectorExpressions = PerFrameVectorExpressions.Num();

		if (NumScalarExpressions > 0 || NumVectorExpressions > 0)
		{

			const FUniformExpressionSet& MaterialUniformExpressionSet = Material.GetRenderingThreadShaderMap()->GetUniformExpressionSet();
			FMaterialRenderContext MaterialRenderContext(MaterialRenderProxy, Material, &View);
			MaterialRenderContext.Time = View.Family->CurrentWorldTime;
			MaterialRenderContext.RealTime = View.Family->CurrentRealTime;
			for (int32 Index = 0; Index < NumScalarExpressions; ++Index)
			{
				auto& Parameter = PerFrameScalarExpressions[Index];
				if (Parameter.IsBound())
				{
					FLinearColor TempValue;
					MaterialUniformExpressionSet.PerFrameUniformScalarExpressions[Index]->GetNumberValue(MaterialRenderContext, TempValue);
					SetShaderValue(RHICmdList, ShaderRHI, Parameter, TempValue.R);
				}
			}

			for (int32 Index = 0; Index < NumVectorExpressions; ++Index)
			{
				auto& Parameter = PerFrameVectorExpressions[Index];
				if (Parameter.IsBound())
				{
					FLinearColor TempValue;
					MaterialUniformExpressionSet.PerFrameUniformVectorExpressions[Index]->GetNumberValue(MaterialRenderContext, TempValue);
					SetShaderValue(RHICmdList, ShaderRHI, Parameter, TempValue);
				}
			}

			// Now previous frame's expressions
			const int32 NumPrevScalarExpressions = PerFramePrevScalarExpressions.Num();
			const int32 NumPrevVectorExpressions = PerFramePrevVectorExpressions.Num();
			if (NumPrevScalarExpressions > 0 || NumPrevVectorExpressions > 0)
			{
				MaterialRenderContext.Time = View.Family->CurrentWorldTime - View.Family->DeltaWorldTime;
				MaterialRenderContext.RealTime = View.Family->CurrentRealTime - View.Family->DeltaWorldTime;

				for (int32 Index = 0; Index < NumPrevScalarExpressions; ++Index)
				{
					auto& Parameter = PerFramePrevScalarExpressions[Index];
					if (Parameter.IsBound())
					{
						FLinearColor TempValue;
						MaterialUniformExpressionSet.PerFramePrevUniformScalarExpressions[Index]->GetNumberValue(MaterialRenderContext, TempValue);
						SetShaderValue(RHICmdList, ShaderRHI, Parameter, TempValue.R);
					}
				}

				for (int32 Index = 0; Index < NumPrevVectorExpressions; ++Index)
				{
					auto& Parameter = PerFramePrevVectorExpressions[Index];
					if (Parameter.IsBound())
					{
						FLinearColor TempValue;
						MaterialUniformExpressionSet.PerFramePrevUniformVectorExpressions[Index]->GetNumberValue(MaterialRenderContext, TempValue);
						SetShaderValue(RHICmdList, ShaderRHI, Parameter, TempValue);
					}
				}
			}
		}
	}

	// If there is no scene, then FSceneRenderTargets::CurrentShadingPath won't be set (see FSceneRenderTargets::Allocate()) and binding the scene textures will crash.
	DeferredParameters.Set(RHICmdList, ShaderRHI, View, Material.GetMaterialDomain(), View.Family->Scene ? TextureMode : ESceneRenderTargetsMode::InvalidScene);

	if (FeatureLevel >= ERHIFeatureLevel::SM4)
	{
		// for copied scene color
		if(SceneColorCopyTexture.IsBound())
		{
			SetTextureParameter(
				RHICmdList,
				ShaderRHI,
				SceneColorCopyTexture,
				SceneColorCopyTextureSampler,
				TStaticSamplerState<SF_Bilinear,AM_Clamp,AM_Clamp,AM_Clamp>::GetRHI(),
				FSceneRenderTargets::Get(RHICmdList).GetLightAttenuationTexture());
		}
	}

	//Use of the eye adaptation texture here is experimental and potentially dangerous as it can introduce a feedback loop. May be removed.
	if(EyeAdaptation.IsBound())
	{
		FTextureRHIRef& EyeAdaptationTex = GetEyeAdaptation(RHICmdList, View);
		SetTextureParameter(RHICmdList, ShaderRHI, EyeAdaptation, EyeAdaptationTex);
	}

	if (bUniformExpressionCacheNeedsDelete)
	{
		delete UniformExpressionCache;
	}
}

// Doxygen struggles to parse these explicit specializations. Just ignore them for now.
#if !UE_BUILD_DOCS

#define IMPLEMENT_MATERIAL_SHADER_SetParameters( ShaderRHIParamRef ) \
	template RENDERER_API void FMaterialShader::SetParameters< ShaderRHIParamRef >( \
		FRHICommandList& RHICmdList,					\
		const ShaderRHIParamRef ShaderRHI,				\
		const FMaterialRenderProxy* MaterialRenderProxy,\
		const FMaterial& Material,						\
		const FSceneView& View,							\
		const TUniformBufferRef<FViewUniformShaderParameters>& ViewUniformBuffer, \
		bool bDeferredPass,								\
		ESceneRenderTargetsMode::Type TextureMode		\
	);

IMPLEMENT_MATERIAL_SHADER_SetParameters( FVertexShaderRHIParamRef );
IMPLEMENT_MATERIAL_SHADER_SetParameters( FHullShaderRHIParamRef );
IMPLEMENT_MATERIAL_SHADER_SetParameters( FDomainShaderRHIParamRef );
IMPLEMENT_MATERIAL_SHADER_SetParameters( FGeometryShaderRHIParamRef );
IMPLEMENT_MATERIAL_SHADER_SetParameters( FPixelShaderRHIParamRef );
IMPLEMENT_MATERIAL_SHADER_SetParameters( FComputeShaderRHIParamRef );

#endif

FTextureRHIRef& GetEyeAdaptation(FRHICommandList& RHICmdList, const FSceneView& View)
{
	FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);
	if (View.bIsViewInfo)
	{
		const FViewInfo& ViewInfo = static_cast<const FViewInfo&>(View);
		if (ViewInfo.HasValidEyeAdaptation())
		{
			IPooledRenderTarget* EyeAdaptationRT = ViewInfo.GetEyeAdaptation(RHICmdList);
			if (EyeAdaptationRT)
			{
				return EyeAdaptationRT->GetRenderTargetItem().TargetableTexture;
			}
		}
	}
	return GWhiteTexture->TextureRHI;
}

bool FMaterialShader::Serialize(FArchive& Ar)
{
	const bool bShaderHasOutdatedParameters = FShader::Serialize(Ar);
	Ar << MaterialUniformBuffer;
	Ar << ParameterCollectionUniformBuffers;
	Ar << DeferredParameters;
	Ar << SceneColorCopyTexture;
	Ar << SceneColorCopyTextureSampler;
	Ar << DebugUniformExpressionSet;
	if (Ar.IsLoading())
	{
		FName LayoutName;
		Ar << LayoutName;
		FRHIUniformBufferLayout Layout(LayoutName);
		Ar << Layout.ConstantBufferSize;
		Ar << Layout.ResourceOffset;
		Ar << Layout.Resources;
		DebugUniformExpressionUBLayout.CopyFrom(Layout);
	}
	else
	{
		FName LayoutName = DebugUniformExpressionUBLayout.GetDebugName();
		Ar << LayoutName;
		Ar << DebugUniformExpressionUBLayout.ConstantBufferSize;
		Ar << DebugUniformExpressionUBLayout.ResourceOffset;
		Ar << DebugUniformExpressionUBLayout.Resources;
	}
	Ar << DebugDescription;
	Ar << EyeAdaptation;

	Ar << PerFrameScalarExpressions;
	Ar << PerFrameVectorExpressions;
	Ar << PerFramePrevScalarExpressions;
	Ar << PerFramePrevVectorExpressions;

	Ar << InstanceCount;
	Ar << InstanceOffset;
	Ar << VertexOffset;

	return bShaderHasOutdatedParameters;
}

uint32 FMaterialShader::GetAllocatedSize() const
{
	return FShader::GetAllocatedSize()
		+ ParameterCollectionUniformBuffers.GetAllocatedSize()
		+ DebugDescription.GetAllocatedSize();
}


template< typename ShaderRHIParamRef >
void FMeshMaterialShader::SetMesh(
	FRHICommandList& RHICmdList,
	const ShaderRHIParamRef ShaderRHI,
	const FVertexFactory* VertexFactory,
	const FSceneView& View,
	const FPrimitiveSceneProxy* Proxy,
	const FMeshBatchElement& BatchElement,
	const FDrawingPolicyRenderState& DrawRenderState,
	uint32 DataFlags )
{
	// Set the mesh for the vertex factory
	VertexFactoryParameters.SetMesh(RHICmdList, this,VertexFactory,View,BatchElement, DataFlags);
		
	if(IsValidRef(BatchElement.PrimitiveUniformBuffer))
	{
		SetUniformBufferParameter(RHICmdList, ShaderRHI,GetUniformBufferParameter<FPrimitiveUniformShaderParameters>(),BatchElement.PrimitiveUniformBuffer);
	}
	else
	{
		check(BatchElement.PrimitiveUniformBufferResource);
		SetUniformBufferParameter(RHICmdList, ShaderRHI,GetUniformBufferParameter<FPrimitiveUniformShaderParameters>(),*BatchElement.PrimitiveUniformBufferResource);
	}

	TShaderUniformBufferParameter<FDistanceCullFadeUniformShaderParameters> LODParameter = GetUniformBufferParameter<FDistanceCullFadeUniformShaderParameters>();
	if( LODParameter.IsBound() )
	{
		SetUniformBufferParameter(RHICmdList, ShaderRHI,LODParameter,GetPrimitiveFadeUniformBufferParameter(View, Proxy));
	}
	if (NonInstancedDitherLODFactorParameter.IsBound())
	{
		SetShaderValue(RHICmdList, ShaderRHI, NonInstancedDitherLODFactorParameter, DrawRenderState.GetDitheredLODTransitionAlpha());
	}
}

#define IMPLEMENT_MESH_MATERIAL_SHADER_SetMesh( ShaderRHIParamRef ) \
	template RENDERER_API void FMeshMaterialShader::SetMesh< ShaderRHIParamRef >( \
		FRHICommandList& RHICmdList,					 \
		const ShaderRHIParamRef ShaderRHI,				 \
		const FVertexFactory* VertexFactory,			 \
		const FSceneView& View,							 \
		const FPrimitiveSceneProxy* Proxy,				 \
		const FMeshBatchElement& BatchElement,			 \
		const FDrawingPolicyRenderState& DrawRenderState,\
		uint32 DataFlags								 \
	);

IMPLEMENT_MESH_MATERIAL_SHADER_SetMesh( FVertexShaderRHIParamRef );
IMPLEMENT_MESH_MATERIAL_SHADER_SetMesh( FHullShaderRHIParamRef );
IMPLEMENT_MESH_MATERIAL_SHADER_SetMesh( FDomainShaderRHIParamRef );
IMPLEMENT_MESH_MATERIAL_SHADER_SetMesh( FGeometryShaderRHIParamRef );
IMPLEMENT_MESH_MATERIAL_SHADER_SetMesh( FPixelShaderRHIParamRef );
IMPLEMENT_MESH_MATERIAL_SHADER_SetMesh( FComputeShaderRHIParamRef );

bool FMeshMaterialShader::Serialize(FArchive& Ar)
{
	bool bShaderHasOutdatedParameters = FMaterialShader::Serialize(Ar);
	bShaderHasOutdatedParameters |= Ar << VertexFactoryParameters;
	Ar << NonInstancedDitherLODFactorParameter;
	return bShaderHasOutdatedParameters;
}

uint32 FMeshMaterialShader::GetAllocatedSize() const
{
	return FMaterialShader::GetAllocatedSize()
		+ VertexFactoryParameters.GetAllocatedSize();
}

FUniformBufferRHIParamRef FMeshMaterialShader::GetPrimitiveFadeUniformBufferParameter(const FSceneView& View, const FPrimitiveSceneProxy* Proxy)
{
	FUniformBufferRHIParamRef FadeUniformBuffer = NULL;
	if( Proxy != NULL )
	{
		const FPrimitiveSceneInfo* PrimitiveSceneInfo = Proxy->GetPrimitiveSceneInfo();
		int32 PrimitiveIndex = PrimitiveSceneInfo->GetIndex();

		// This cast should always be safe. Check it :)
		checkSlow(View.bIsViewInfo);
		const FViewInfo& ViewInfo = (const FViewInfo&)View;
		FadeUniformBuffer = ViewInfo.PrimitiveFadeUniformBuffers[PrimitiveIndex];
	}
	if (FadeUniformBuffer == NULL)
	{
		FadeUniformBuffer = GDistanceCullFadedInUniformBuffer.GetUniformBufferRHI();
	}
	return FadeUniformBuffer;
}
