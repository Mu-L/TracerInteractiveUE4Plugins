// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	MeshParticleVertexFactory.cpp: Mesh particle vertex factory implementation
=============================================================================*/

#include "MeshParticleVertexFactory.h"
#include "ParticleHelper.h"
#include "ShaderParameterUtils.h"
#include "MeshMaterialShader.h"

class FMeshParticleVertexFactoryShaderParameters : public FVertexFactoryShaderParameters
{
public:

	virtual void Bind(const FShaderParameterMap& ParameterMap) override
	{
		Transform1.Bind(ParameterMap,TEXT("Transform1"));
		Transform2.Bind(ParameterMap,TEXT("Transform2"));
		Transform3.Bind(ParameterMap,TEXT("Transform3"));
		SubUVParams.Bind(ParameterMap,TEXT("SubUVParams"));
		SubUVLerp.Bind(ParameterMap,TEXT("SubUVLerp"));
		ParticleDirection.Bind(ParameterMap, TEXT("ParticleDirection"));
		RelativeTime.Bind(ParameterMap, TEXT("RelativeTime"));
		DynamicParameter.Bind(ParameterMap, TEXT("DynamicParameter"));
		ParticleColor.Bind(ParameterMap, TEXT("ParticleColor"));
		PrevTransform0.Bind(ParameterMap, TEXT("PrevTransform0"));
		PrevTransform1.Bind(ParameterMap, TEXT("PrevTransform1"));
		PrevTransform2.Bind(ParameterMap, TEXT("PrevTransform2"));
		PrevTransformBuffer.Bind(ParameterMap, TEXT("PrevTransformBuffer"));
	}

	virtual void Serialize(FArchive& Ar) override
	{
		Ar << Transform1;
		Ar << Transform2;
		Ar << Transform3;
		Ar << SubUVParams;
		Ar << SubUVLerp;
		Ar << ParticleDirection;
		Ar << RelativeTime;
		Ar << DynamicParameter;
		Ar << ParticleColor;
		Ar << PrevTransform0;
		Ar << PrevTransform1;
		Ar << PrevTransform2;
		Ar << PrevTransformBuffer;
	}

	virtual void GetElementShaderBindings(
		const FSceneInterface* Scene,
		const FSceneView* View,
		const FMeshMaterialShader* Shader,
		const EVertexInputStreamType InputStreamType,
		ERHIFeatureLevel::Type FeatureLevel,
		const FVertexFactory* VertexFactory,
		const FMeshBatchElement& BatchElement,
		class FMeshDrawSingleShaderBindings& ShaderBindings,
		FVertexInputStreamArray& VertexStreams) const override
	{
		const bool bInstanced = GRHISupportsInstancing;
		FMeshParticleVertexFactory* MeshParticleVF = (FMeshParticleVertexFactory*)VertexFactory;
		ShaderBindings.Add(Shader->GetUniformBufferParameter<FMeshParticleUniformParameters>(), MeshParticleVF->GetUniformBuffer() );

		if (!bInstanced)
		{
			const FMeshParticleVertexFactory::FBatchParametersCPU* BatchParameters = (const FMeshParticleVertexFactory::FBatchParametersCPU*)BatchElement.UserData;
			const FMeshParticleInstanceVertex* Vertex = BatchParameters->InstanceBuffer + BatchElement.UserIndex;
			const FMeshParticleInstanceVertexDynamicParameter* DynamicVertex = BatchParameters->DynamicParameterBuffer + BatchElement.UserIndex;
			const FMeshParticleInstanceVertexPrevTransform* PrevTransformVertex = BatchParameters->PrevTransformBuffer + BatchElement.UserIndex;

			ShaderBindings.Add(Transform1, Vertex->Transform[0]);
			ShaderBindings.Add(Transform2, Vertex->Transform[1]);
			ShaderBindings.Add(Transform3, Vertex->Transform[2]);
			ShaderBindings.Add(SubUVParams, FVector4((float)Vertex->SubUVParams[0], (float)Vertex->SubUVParams[1], (float)Vertex->SubUVParams[2], (float)Vertex->SubUVParams[3]));
			ShaderBindings.Add(SubUVLerp, Vertex->SubUVLerp);
			ShaderBindings.Add(ParticleDirection, Vertex->Velocity);
			ShaderBindings.Add(RelativeTime, Vertex->RelativeTime);

			if (BatchParameters->DynamicParameterBuffer)
			{
				ShaderBindings.Add(DynamicParameter, FVector4(DynamicVertex->DynamicValue[0], DynamicVertex->DynamicValue[1], DynamicVertex->DynamicValue[2], DynamicVertex->DynamicValue[3]));
			}

			if (BatchParameters->PrevTransformBuffer && FeatureLevel >= ERHIFeatureLevel::SM4)
			{
				ShaderBindings.Add(PrevTransform0, PrevTransformVertex->PrevTransform0);
				ShaderBindings.Add(PrevTransform1, PrevTransformVertex->PrevTransform1);
				ShaderBindings.Add(PrevTransform2, PrevTransformVertex->PrevTransform2);
			}

			ShaderBindings.Add(ParticleColor, FVector4(Vertex->Color.Component(0), Vertex->Color.Component(1), Vertex->Color.Component(2), Vertex->Color.Component(3)));
		}
		else if (FeatureLevel >= ERHIFeatureLevel::SM4)
		{
			ShaderBindings.Add(PrevTransformBuffer, MeshParticleVF->GetPreviousTransformBufferSRV());
		}
	}

private:
	// Used only when instancing is off (ES2)
	FShaderParameter Transform1;
	FShaderParameter Transform2;
	FShaderParameter Transform3;
	FShaderParameter SubUVParams;
	FShaderParameter SubUVLerp;
	FShaderParameter ParticleDirection;
	FShaderParameter RelativeTime;
	FShaderParameter DynamicParameter;
	FShaderParameter ParticleColor;
	FShaderParameter PrevTransform0;
	FShaderParameter PrevTransform1;
	FShaderParameter PrevTransform2;
	FShaderResourceParameter PrevTransformBuffer;
};

class FDummyPrevTransformBuffer : public FRenderResource
{
public:
	virtual ~FDummyPrevTransformBuffer() {}

	virtual void InitRHI()
	{
		FRHIResourceCreateInfo CreateInfo;
		VB = RHICreateVertexBuffer(sizeof(FVector4) * 3, BUF_Static | BUF_ShaderResource, CreateInfo);
		SRV = RHICreateShaderResourceView(VB, sizeof(FVector4), PF_A32B32G32R32F);
	}

	virtual void ReleaseRHI()
	{
		VB.SafeRelease();
		SRV.SafeRelease();
	}

	virtual FString GetFriendlyName() const
	{
		return TEXT("FDummyPrevTransformBuffer");
	}

	inline FRHIVertexBuffer* GetVB() const
	{
		return VB;
	}

	inline FRHIShaderResourceView* GetSRV() const
	{
		return SRV;
	}

private:
	FVertexBufferRHIRef VB;
	FShaderResourceViewRHIRef SRV;
};

static TGlobalResource<FDummyPrevTransformBuffer> GDummyPrevTransformBuffer;

void FMeshParticleVertexFactory::InitRHI()
{
	FVertexDeclarationElementList Elements;

	const bool bInstanced = GRHISupportsInstancing;

	if (Data.bInitialized)
	{
		if(bInstanced)
		{
			// Stream 0 - Instance data
			{
				checkf(DynamicVertexStride != -1, TEXT("FMeshParticleVertexFactory does not have a valid DynamicVertexStride - likely an empty one was made, but SetStrides was not called"));
				FVertexStream VertexStream;
				VertexStream.VertexBuffer = NULL;
				VertexStream.Stride = 0;
				VertexStream.Offset = 0;
				Streams.Add(VertexStream);
	
				// @todo metal: this will need a valid stride when we get to instanced meshes!
				Elements.Add(FVertexElement(0, Data.TransformComponent[0].Offset, Data.TransformComponent[0].Type, 8, DynamicVertexStride, EnumHasAnyFlags(EVertexStreamUsage::Instancing, Data.TransformComponent[0].VertexStreamUsage)));
				Elements.Add(FVertexElement(0, Data.TransformComponent[1].Offset, Data.TransformComponent[1].Type, 9, DynamicVertexStride, EnumHasAnyFlags(EVertexStreamUsage::Instancing, Data.TransformComponent[1].VertexStreamUsage)));
				Elements.Add(FVertexElement(0, Data.TransformComponent[2].Offset, Data.TransformComponent[2].Type, 10, DynamicVertexStride, EnumHasAnyFlags(EVertexStreamUsage::Instancing, Data.TransformComponent[2].VertexStreamUsage)));
	
				Elements.Add(FVertexElement(0, Data.SubUVs.Offset, Data.SubUVs.Type, 11, DynamicVertexStride, EnumHasAnyFlags(EVertexStreamUsage::Instancing, Data.SubUVs.VertexStreamUsage)));
				Elements.Add(FVertexElement(0, Data.SubUVLerpAndRelTime.Offset, Data.SubUVLerpAndRelTime.Type, 12, DynamicVertexStride, EnumHasAnyFlags(EVertexStreamUsage::Instancing, Data.SubUVLerpAndRelTime.VertexStreamUsage)));
	
				Elements.Add(FVertexElement(0, Data.ParticleColorComponent.Offset, Data.ParticleColorComponent.Type, 14, DynamicVertexStride, EnumHasAnyFlags(EVertexStreamUsage::Instancing, Data.ParticleColorComponent.VertexStreamUsage)));
				Elements.Add(FVertexElement(0, Data.VelocityComponent.Offset, Data.VelocityComponent.Type, 15, DynamicVertexStride, EnumHasAnyFlags(EVertexStreamUsage::Instancing, Data.VelocityComponent.VertexStreamUsage)));
			}

			// Stream 1 - Dynamic parameter
			{
				checkf(DynamicParameterVertexStride != -1, TEXT("FMeshParticleVertexFactory does not have a valid DynamicParameterVertexStride - likely an empty one was made, but SetStrides was not called"));
				
				FVertexStream VertexStream;
				VertexStream.VertexBuffer = NULL;
				VertexStream.Stride = 0;
				VertexStream.Offset = 0;
				Streams.Add(VertexStream);
	
				Elements.Add(FVertexElement(1, 0, VET_Float4, 13, DynamicParameterVertexStride, true));
			}

			// Add a dummy resource to avoid crash due to missing resource
			if (GMaxRHIFeatureLevel >= ERHIFeatureLevel::SM4)
			{
				PrevTransformBuffer.NumBytes = 0;
				PrevTransformBuffer.Buffer = GDummyPrevTransformBuffer.GetVB();
				PrevTransformBuffer.SRV = GDummyPrevTransformBuffer.GetSRV();
			}
		}

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

		// Vertex color
		if(Data.ColorComponent.VertexBuffer != NULL)
		{
			Elements.Add(AccessStreamComponent(Data.ColorComponent,3));
		}
		else
		{
			//If the mesh has no color component, set the null color buffer on a new stream with a stride of 0.
			//This wastes 4 bytes of bandwidth per vertex, but prevents having to compile out twice the number of vertex factories.
			FVertexStreamComponent NullColorComponent(&GNullColorVertexBuffer, 0, 0, VET_Color, EVertexStreamUsage::ManualFetch);
			Elements.Add(AccessStreamComponent(NullColorComponent, 3));
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

			for(int32 CoordinateIndex = Data.TextureCoordinates.Num();CoordinateIndex < MAX_TEXCOORDS;CoordinateIndex++)
			{
				Elements.Add(AccessStreamComponent(
					Data.TextureCoordinates[Data.TextureCoordinates.Num() - 1],
					BaseTexCoordAttribute + CoordinateIndex
					));
			}
		}

		if(Streams.Num() > 0)
		{
			InitDeclaration(Elements);
			check(IsValidRef(GetDeclaration()));
		}
	}
}

void FMeshParticleVertexFactory::SetInstanceBuffer(const FVertexBuffer* InstanceBuffer, uint32 StreamOffset, uint32 Stride)
{
	ensure(Stride == DynamicVertexStride);
	Streams[0].VertexBuffer = InstanceBuffer;
	Streams[0].Offset = StreamOffset;
	Streams[0].Stride = Stride;
}

void FMeshParticleVertexFactory::SetDynamicParameterBuffer(const FVertexBuffer* InDynamicParameterBuffer, uint32 StreamOffset, uint32 Stride)
{
	if (InDynamicParameterBuffer)
	{
		Streams[1].VertexBuffer = InDynamicParameterBuffer;
		ensure(Stride == DynamicParameterVertexStride);
		Streams[1].Stride = DynamicParameterVertexStride;
		Streams[1].Offset = StreamOffset;
	}
	else
	{
		Streams[1].VertexBuffer = &GNullDynamicParameterVertexBuffer;
		ensure(DynamicParameterVertexStride == 0);
		Streams[1].Stride = 0;
		Streams[1].Offset = 0;
	}
}

uint8* FMeshParticleVertexFactory::LockPreviousTransformBuffer(uint32 ParticleCount)
{
	const static uint32 ElementSize = sizeof(FVector4);
	const static uint32 ParticleSize = ElementSize * 3;
	const uint32 AllocationRequest = ParticleCount * ParticleSize;

	check(!PrevTransformBuffer.MappedBuffer);

	if (AllocationRequest > PrevTransformBuffer.NumBytes)
	{
		PrevTransformBuffer.Release();
		PrevTransformBuffer.Initialize(ElementSize, ParticleCount * 3, PF_A32B32G32R32F, BUF_Dynamic);
	}

	PrevTransformBuffer.Lock();

	return PrevTransformBuffer.MappedBuffer;
}

void FMeshParticleVertexFactory::UnlockPreviousTransformBuffer()
{
	check(PrevTransformBuffer.MappedBuffer);

	PrevTransformBuffer.Unlock();
}

FRHIShaderResourceView* FMeshParticleVertexFactory::GetPreviousTransformBufferSRV() const
{
	return PrevTransformBuffer.SRV;
}

bool FMeshParticleVertexFactory::ShouldCompilePermutation(EShaderPlatform Platform, const class FMaterial* Material, const class FShaderType* ShaderType)
{
	return (Material->IsUsedWithMeshParticles() || Material->IsSpecialEngineMaterial());
}

void FMeshParticleVertexFactory::SetData(const FDataType& InData)
{
	check(IsInRenderingThread());
	Data = InData;
	UpdateRHI();
}


FVertexFactoryShaderParameters* FMeshParticleVertexFactory::ConstructShaderParameters(EShaderFrequency ShaderFrequency)
{
	return ShaderFrequency == SF_Vertex ? new FMeshParticleVertexFactoryShaderParameters() : NULL;
}

IMPLEMENT_VERTEX_FACTORY_TYPE(FMeshParticleVertexFactory,"/Engine/Private/MeshParticleVertexFactory.ush",true,false,true,false,false);
IMPLEMENT_VERTEX_FACTORY_TYPE(FMeshParticleVertexFactoryEmulatedInstancing,"/Engine/Private/MeshParticleVertexFactory.ush",true,false,true,false,false);
IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT(FMeshParticleUniformParameters,"MeshParticleVF");
