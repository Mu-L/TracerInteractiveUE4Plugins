// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	LocalVertexFactory.cpp: Local vertex factory implementation
=============================================================================*/

#include "LocalVertexFactory.h"
#include "SceneView.h"
#include "MeshBatch.h"
#include "SpeedTreeWind.h"
#include "ShaderParameterUtils.h"
#include "Rendering/ColorVertexBuffer.h"

class FSpeedTreeWindNullUniformBuffer : public TUniformBuffer<FSpeedTreeUniformParameters>
{
	typedef TUniformBuffer< FSpeedTreeUniformParameters > Super;
public:
	virtual void InitDynamicRHI() override;
};

void FSpeedTreeWindNullUniformBuffer::InitDynamicRHI()
{
	FSpeedTreeUniformParameters Parameters;
	FMemory::Memzero(Parameters);
	// Set valid vectors otherwise nothing gets rendered.
	Parameters.WindVector.X = 1.f;
	Parameters.PrevWindVector.X = 1.f;
	SetContentsNoUpdate(Parameters);
	
	Super::InitDynamicRHI();
}

static TGlobalResource< FSpeedTreeWindNullUniformBuffer > GSpeedTreeWindNullUniformBuffer;

void FLocalVertexFactoryShaderParameters::Bind(const FShaderParameterMap& ParameterMap)
{
	LODParameter.Bind(ParameterMap, TEXT("SpeedTreeLODInfo"));
	bAnySpeedTreeParamIsBound = LODParameter.IsBound() || ParameterMap.ContainsParameterAllocation(TEXT("SpeedTreeData"));
}

void FLocalVertexFactoryShaderParameters::Serialize(FArchive& Ar)
{
	Ar << bAnySpeedTreeParamIsBound;
	Ar << LODParameter;
}

IMPLEMENT_UNIFORM_BUFFER_STRUCT(FLocalVertexFactoryUniformShaderParameters, TEXT("LocalVF"));

TUniformBufferRef<FLocalVertexFactoryUniformShaderParameters> CreateLocalVFUniformBuffer(const FLocalVertexFactory* LocalVertexFactory, FColorVertexBuffer* OverrideColorVertexBuffer)
{
	FLocalVertexFactoryUniformShaderParameters UniformParameters;

	UniformParameters.VertexFetch_PackedTangentsBuffer = LocalVertexFactory->GetTangentsSRV();
	UniformParameters.VertexFetch_TexCoordBuffer = LocalVertexFactory->GetTextureCoordinatesSRV();

	int ColorIndexMask = 0;
	if (OverrideColorVertexBuffer)
	{
		UniformParameters.VertexFetch_ColorComponentsBuffer = OverrideColorVertexBuffer->GetColorComponentsSRV();
		ColorIndexMask = OverrideColorVertexBuffer->GetNumVertices() > 1 ? ~0 : 0;
	}
	else
	{
		UniformParameters.VertexFetch_ColorComponentsBuffer = LocalVertexFactory->GetColorComponentsSRV();
		ColorIndexMask = (int32)LocalVertexFactory->GetColorIndexMask();
	}

	if (!UniformParameters.VertexFetch_ColorComponentsBuffer)
	{
		UniformParameters.VertexFetch_ColorComponentsBuffer = GNullColorVertexBuffer.VertexBufferSRV;
	}

	const int NumTexCoords = LocalVertexFactory->GetNumTexcoords();
	const int LightMapCoordinateIndex = LocalVertexFactory->GetLightMapCoordinateIndex();
	UniformParameters.VertexFetch_Parameters = {ColorIndexMask, NumTexCoords, LightMapCoordinateIndex};

	return TUniformBufferRef<FLocalVertexFactoryUniformShaderParameters>::CreateUniformBufferImmediate(UniformParameters, UniformBuffer_MultiFrame);
}

void FLocalVertexFactoryShaderParameters::SetMesh(FRHICommandList& RHICmdList, FShader* Shader, const FVertexFactory* VertexFactory, const FSceneView& View, const FMeshBatchElement& BatchElement, uint32 DataFlags) const
{
	const auto* LocalVertexFactory = static_cast<const FLocalVertexFactory*>(VertexFactory);
	
	FVertexShaderRHIParamRef VS = Shader->GetVertexShader();
	if (LocalVertexFactory->SupportsManualVertexFetch(View.GetFeatureLevel()))
	{
		FUniformBufferRHIParamRef VertexFactoryUniformBuffer = static_cast<FUniformBufferRHIParamRef>(BatchElement.VertexFactoryUserData);

		if (!VertexFactoryUniformBuffer)
		{
			// No batch element override
			VertexFactoryUniformBuffer = LocalVertexFactory->GetUniformBuffer();
		}

		SetUniformBufferParameter(RHICmdList, VS, Shader->GetUniformBufferParameter<FLocalVertexFactoryUniformShaderParameters>(), VertexFactoryUniformBuffer);
	}

	if (BatchElement.bUserDataIsColorVertexBuffer)
	{
		FColorVertexBuffer* OverrideColorVertexBuffer = (FColorVertexBuffer*)BatchElement.UserData;
		check(OverrideColorVertexBuffer);

		if (!LocalVertexFactory->SupportsManualVertexFetch(View.GetFeatureLevel()))
		{
			LocalVertexFactory->SetColorOverrideStream(RHICmdList, OverrideColorVertexBuffer);
		}	
	}

	if (bAnySpeedTreeParamIsBound && View.Family != NULL && View.Family->Scene != NULL)
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_FLocalVertexFactoryShaderParameters_SetMesh_SpeedTree);
		FUniformBufferRHIParamRef SpeedTreeUniformBuffer = View.Family->Scene->GetSpeedTreeUniformBuffer(VertexFactory);
		if (SpeedTreeUniformBuffer == NULL)
		{
			SpeedTreeUniformBuffer = GSpeedTreeWindNullUniformBuffer.GetUniformBufferRHI();
		}
		check(SpeedTreeUniformBuffer != NULL);

		SetUniformBufferParameter(RHICmdList, VS, Shader->GetUniformBufferParameter<FSpeedTreeUniformParameters>(), SpeedTreeUniformBuffer);

		if (LODParameter.IsBound())
		{
			FVector LODData(BatchElement.MinScreenSize, BatchElement.MaxScreenSize, BatchElement.MaxScreenSize - BatchElement.MinScreenSize);
			SetShaderValue(RHICmdList, VS, LODParameter, LODData);
		}
	}
}

/**
 * Should we cache the material's shadertype on this platform with this vertex factory? 
 */
bool FLocalVertexFactory::ShouldCompilePermutation(EShaderPlatform Platform, const class FMaterial* Material, const class FShaderType* ShaderType)
{
	return true; 
}

void FLocalVertexFactory::SetData(const FDataType& InData)
{
	check(IsInRenderingThread());

	{
		//const int NumTexCoords = InData.NumTexCoords;
		//const int LightMapCoordinateIndex = InData.LightMapCoordinateIndex;
		//check(NumTexCoords > 0);
		//check(LightMapCoordinateIndex < NumTexCoords && LightMapCoordinateIndex >= 0);
		//check(InData.PositionComponentSRV);
		//check(InData.TangentsSRV);
		//check(InData.TextureCoordinatesSRV);
		//check(InData.ColorComponentsSRV);
	}

	// The shader code makes assumptions that the color component is a FColor, performing swizzles on ES2 and Metal platforms as necessary
	// If the color is sent down as anything other than VET_Color then you'll get an undesired swizzle on those platforms
	check((InData.ColorComponent.Type == VET_None) || (InData.ColorComponent.Type == VET_Color));

	Data = InData;
	UpdateRHI();
}

/**
* Copy the data from another vertex factory
* @param Other - factory to copy from
*/
void FLocalVertexFactory::Copy(const FLocalVertexFactory& Other)
{
	FLocalVertexFactory* VertexFactory = this;
	const FDataType* DataCopy = &Other.Data;
	ENQUEUE_RENDER_COMMAND(FLocalVertexFactoryCopyData)(
		[VertexFactory, DataCopy](FRHICommandListImmediate& RHICmdList)
		{
			VertexFactory->Data = *DataCopy;
		});
	BeginUpdateResourceRHI(this);
}

void FLocalVertexFactory::InitRHI()
{
	// If the vertex buffer containing position is not the same vertex buffer containing the rest of the data,
	// then initialize PositionStream and PositionDeclaration.
	if(Data.PositionComponent.VertexBuffer != Data.TangentBasisComponents[0].VertexBuffer)
	{
		FVertexDeclarationElementList PositionOnlyStreamElements;
		PositionOnlyStreamElements.Add(AccessPositionStreamComponent(Data.PositionComponent,0));
		InitPositionDeclaration(PositionOnlyStreamElements);
	}

	FVertexDeclarationElementList Elements;
	if(Data.PositionComponent.VertexBuffer != NULL)
	{
		Elements.Add(AccessStreamComponent(Data.PositionComponent,0));
	}

	// only tangent,normal are used by the stream. the binormal is derived in the shader
	uint8 TangentBasisAttributes[2] = { 1, 2 };
	for(int32 AxisIndex = 0;AxisIndex < 2;AxisIndex++)
	{
		if(Data.TangentBasisComponents[AxisIndex].VertexBuffer != NULL)
		{
			Elements.Add(AccessStreamComponent(Data.TangentBasisComponents[AxisIndex],TangentBasisAttributes[AxisIndex]));
		}
	}

	if (Data.ColorComponentsSRV == nullptr)
	{
		Data.ColorComponentsSRV = GNullColorVertexBuffer.VertexBufferSRV;
		Data.ColorIndexMask = 0;
	}

	ColorStreamIndex = -1;
	if(Data.ColorComponent.VertexBuffer)
	{
		Elements.Add(AccessStreamComponent(Data.ColorComponent,3));
		ColorStreamIndex = Elements.Last().StreamIndex;
	}
	else
	{
		//If the mesh has no color component, set the null color buffer on a new stream with a stride of 0.
		//This wastes 4 bytes of bandwidth per vertex, but prevents having to compile out twice the number of vertex factories.
		FVertexStreamComponent NullColorComponent(&GNullColorVertexBuffer, 0, 0, VET_Color, EVertexStreamUsage::ManualFetch);
		Elements.Add(AccessStreamComponent(NullColorComponent, 3));
		ColorStreamIndex = Elements.Last().StreamIndex;
	}

	if(Data.TextureCoordinates.Num())
	{
		const int32 BaseTexCoordAttribute = 4;
		for(int32 CoordinateIndex = 0;CoordinateIndex < Data.TextureCoordinates.Num();CoordinateIndex++)
		{
			Elements.Add(AccessStreamComponent(
				Data.TextureCoordinates[CoordinateIndex],
				BaseTexCoordAttribute + CoordinateIndex
				));
		}

		for (int32 CoordinateIndex = Data.TextureCoordinates.Num(); CoordinateIndex < MAX_STATIC_TEXCOORDS / 2; CoordinateIndex++)
		{
			Elements.Add(AccessStreamComponent(
				Data.TextureCoordinates[Data.TextureCoordinates.Num() - 1],
				BaseTexCoordAttribute + CoordinateIndex
				));
		}
	}

	if(Data.LightMapCoordinateComponent.VertexBuffer)
	{
		Elements.Add(AccessStreamComponent(Data.LightMapCoordinateComponent,15));
	}
	else if(Data.TextureCoordinates.Num())
	{
		Elements.Add(AccessStreamComponent(Data.TextureCoordinates[0],15));
	}

	check(Streams.Num() > 0);

	InitDeclaration(Elements);
	check(IsValidRef(GetDeclaration()));

	if (RHISupportsManualVertexFetch(GMaxRHIShaderPlatform))
	{
		UniformBuffer = CreateLocalVFUniformBuffer(this, nullptr);
	}
}

FVertexFactoryShaderParameters* FLocalVertexFactory::ConstructShaderParameters(EShaderFrequency ShaderFrequency)
{
	if (ShaderFrequency == SF_Vertex)
	{
		return new FLocalVertexFactoryShaderParameters();
	}

	return NULL;
}

IMPLEMENT_VERTEX_FACTORY_TYPE(FLocalVertexFactory,"/Engine/Private/LocalVertexFactory.ush",true,true,true,true,true);
