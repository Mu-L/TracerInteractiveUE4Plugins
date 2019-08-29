// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#include "NiagaraRendererRibbons.h"
#include "ParticleResources.h"
#include "NiagaraRibbonVertexFactory.h"
#include "NiagaraDataSet.h"
#include "NiagaraStats.h"

DECLARE_CYCLE_STAT(TEXT("Generate Ribbon Vertex Data"), STAT_NiagaraGenRibbonVertexData, STATGROUP_Niagara);
DECLARE_CYCLE_STAT(TEXT("Render Ribbons"), STAT_NiagaraRenderRibbons, STATGROUP_Niagara);

DECLARE_CYCLE_STAT(TEXT("Genereate GPU Buffers"), STAT_NiagaraGenRibbonGpuBuffers, STATGROUP_Niagara);


struct FNiagaraDynamicDataRibbon : public FNiagaraDynamicDataBase
{
	TArray<FNiagaraRibbonVertex> VertexData;
	TArray<int16> IndexData;
	TArray<FNiagaraRibbonVertexDynamicParameter> MaterialParameterVertexData;

	const FNiagaraDataSet *DataSet;
	int32 PositionDataOffset;
	int32 WidthDataOffset;
	int32 TwistDataOffset;
	int32 ColorDataOffset;
};

class FNiagaraMeshCollectorResourcesRibbon : public FOneFrameResource
{
public:
	FNiagaraRibbonVertexFactory VertexFactory;
	FNiagaraRibbonUniformBufferRef UniformBuffer;

	virtual ~FNiagaraMeshCollectorResourcesRibbon()
	{
		VertexFactory.ReleaseResource();
	}
};


NiagaraRendererRibbons::NiagaraRendererRibbons(ERHIFeatureLevel::Type FeatureLevel, UNiagaraRendererProperties *InProps) :
	NiagaraRenderer()
{
	VertexFactory = new FNiagaraRibbonVertexFactory(NVFT_Ribbon, FeatureLevel);
	Properties = Cast<UNiagaraRibbonRendererProperties>(InProps);
}


void NiagaraRendererRibbons::ReleaseRenderThreadResources()
{
	VertexFactory->ReleaseResource();
	WorldSpacePrimitiveUniformBuffer.ReleaseResource();
}

// FPrimitiveSceneProxy interface.
void NiagaraRendererRibbons::CreateRenderThreadResources()
{
	VertexFactory->InitResource();
}




void NiagaraRendererRibbons::GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily, uint32 VisibilityMap, FMeshElementCollector& Collector, const FNiagaraSceneProxy *SceneProxy) const
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraRender);
	SCOPE_CYCLE_COUNTER(STAT_NiagaraRenderRibbons);

	SimpleTimer MeshElementsTimer;
	FNiagaraDynamicDataRibbon *DynamicDataRibbon = static_cast<FNiagaraDynamicDataRibbon*>(DynamicDataRender);
	if (!DynamicDataRibbon || DynamicDataRibbon->VertexData.Num() < 4)
	{
		return;
	}

	const bool bIsWireframe = ViewFamily.EngineShowFlags.Wireframe;
	FMaterialRenderProxy* MaterialRenderProxy = Material->GetRenderProxy(SceneProxy->IsSelected(), SceneProxy->IsHovered());

	int32 SizeInBytes = DynamicDataRibbon->VertexData.GetTypeSize() * DynamicDataRibbon->VertexData.Num();
	FGlobalDynamicVertexBuffer::FAllocation LocalDynamicVertexAllocation = FGlobalDynamicVertexBuffer::Get().Allocate(SizeInBytes);
	FGlobalDynamicVertexBuffer::FAllocation LocalDynamicVertexMaterialParamsAllocation;
	FGlobalDynamicIndexBuffer::FAllocation DynamicIndexAllocation = FGlobalDynamicIndexBuffer::Get().Allocate(DynamicDataRibbon->IndexData.Num(), sizeof(int16));

	if (DynamicDataRibbon->MaterialParameterVertexData.Num() > 0)
	{
		int32 MatParamSizeInBytes = DynamicDataRibbon->MaterialParameterVertexData.GetTypeSize() * DynamicDataRibbon->MaterialParameterVertexData.Num();
		LocalDynamicVertexMaterialParamsAllocation = FGlobalDynamicVertexBuffer::Get().Allocate(MatParamSizeInBytes);

		if (LocalDynamicVertexMaterialParamsAllocation.IsValid())
		{
			// Copy the extra material vertex data over.
			FMemory::Memcpy(LocalDynamicVertexMaterialParamsAllocation.Buffer, DynamicDataRibbon->MaterialParameterVertexData.GetData(), MatParamSizeInBytes);
		}
	}


	if (LocalDynamicVertexAllocation.IsValid())
	{
		// Update the primitive uniform buffer if needed.
		if (!WorldSpacePrimitiveUniformBuffer.IsInitialized())
		{
			FPrimitiveUniformShaderParameters PrimitiveUniformShaderParameters = GetPrimitiveUniformShaderParameters(
				FMatrix::Identity,
				SceneProxy->GetActorPosition(),
				SceneProxy->GetBounds(),
				SceneProxy->GetLocalBounds(),
				SceneProxy->ReceivesDecals(),
				false,
				false,
				SceneProxy->UseSingleSampleShadowFromStationaryLights(),
				SceneProxy->GetScene().HasPrecomputedVolumetricLightmap_RenderThread(),
				SceneProxy->UseEditorDepthTest(),
				SceneProxy->GetLightingChannelMask()
				);
			WorldSpacePrimitiveUniformBuffer.SetContents(PrimitiveUniformShaderParameters);
			WorldSpacePrimitiveUniformBuffer.InitResource();
		}

		// Copy the vertex data over.
		FMemory::Memcpy(LocalDynamicVertexAllocation.Buffer, DynamicDataRibbon->VertexData.GetData(), SizeInBytes);
		FMemory::Memcpy(DynamicIndexAllocation.Buffer, DynamicDataRibbon->IndexData.GetData(), DynamicDataRibbon->IndexData.Num() * sizeof(int16));

		// Compute the per-view uniform buffers.
		for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
		{
			if (VisibilityMap & (1 << ViewIndex))
			{
				const FSceneView* View = Views[ViewIndex];

				FNiagaraMeshCollectorResourcesRibbon& CollectorResources = Collector.AllocateOneFrameResource<FNiagaraMeshCollectorResourcesRibbon>();
				FNiagaraRibbonUniformParameters PerViewUniformParameters;// = UniformParameters;
				PerViewUniformParameters.CameraUp = View->GetViewUp(); // FVector4(0.0f, 0.0f, 1.0f, 0.0f);
				PerViewUniformParameters.CameraRight = View->GetViewRight();//	FVector4(1.0f, 0.0f, 0.0f, 0.0f);
				PerViewUniformParameters.ScreenAlignment = FVector4(0.0f, 0.0f, 0.0f, 0.0f);
				PerViewUniformParameters.UseCustomFacing = Properties->FacingMode == ENiagaraRibbonFacingMode::Custom;
				PerViewUniformParameters.PositionDataOffset = DynamicDataRibbon->PositionDataOffset;
				PerViewUniformParameters.ColorDataOffset = DynamicDataRibbon->ColorDataOffset;
				PerViewUniformParameters.WidthDataOffset = DynamicDataRibbon->WidthDataOffset;
				PerViewUniformParameters.TwistDataOffset = DynamicDataRibbon->TwistDataOffset;
				CollectorResources.VertexFactory.SetParticleData(DynamicDataRibbon->DataSet);

				// Collector.AllocateOneFrameResource uses default ctor, initialize the vertex factory
				CollectorResources.VertexFactory.SetParticleFactoryType(NVFT_Ribbon);

				CollectorResources.UniformBuffer = FNiagaraRibbonUniformBufferRef::CreateUniformBufferImmediate(PerViewUniformParameters, UniformBuffer_SingleFrame);

				CollectorResources.VertexFactory.InitResource();
				CollectorResources.VertexFactory.SetBeamTrailUniformBuffer(CollectorResources.UniformBuffer);
				CollectorResources.VertexFactory.SetVertexBuffer(LocalDynamicVertexAllocation.VertexBuffer, LocalDynamicVertexAllocation.VertexOffset, sizeof(FNiagaraRibbonVertex));
				if (DynamicDataRibbon->MaterialParameterVertexData.Num() > 0 && LocalDynamicVertexMaterialParamsAllocation.IsValid())
				{
					CollectorResources.VertexFactory.SetDynamicParameterBuffer(LocalDynamicVertexMaterialParamsAllocation.VertexBuffer, LocalDynamicVertexMaterialParamsAllocation.VertexOffset, sizeof(FNiagaraRibbonVertexDynamicParameter));
				}
				else
				{
					CollectorResources.VertexFactory.SetDynamicParameterBuffer(NULL, 0, 0);
				}

				FMeshBatch& MeshBatch = Collector.AllocateMesh();
				MeshBatch.VertexFactory = &CollectorResources.VertexFactory;
				MeshBatch.CastShadow = SceneProxy->CastsDynamicShadow();
				MeshBatch.bUseAsOccluder = false;
				MeshBatch.ReverseCulling = SceneProxy->IsLocalToWorldDeterminantNegative();
				MeshBatch.bDisableBackfaceCulling = true;
				MeshBatch.Type = PT_TriangleList;
				MeshBatch.DepthPriorityGroup = SceneProxy->GetDepthPriorityGroup(View);
				MeshBatch.bCanApplyViewModeOverrides = true;
				MeshBatch.bUseWireframeSelectionColoring = SceneProxy->IsSelected();

				if (bIsWireframe)
				{
					MeshBatch.MaterialRenderProxy = UMaterial::GetDefaultMaterial(MD_Surface)->GetRenderProxy(SceneProxy->IsSelected(), SceneProxy->IsHovered());
				}
				else
				{
					MeshBatch.MaterialRenderProxy = MaterialRenderProxy;
				}

				FMeshBatchElement& MeshElement = MeshBatch.Elements[0];
				MeshElement.IndexBuffer = DynamicIndexAllocation.IndexBuffer;
				MeshElement.FirstIndex = DynamicIndexAllocation.FirstIndex;
				MeshElement.NumPrimitives = DynamicDataRibbon->IndexData.Num() / 3;
				MeshElement.NumInstances = 1;
				MeshElement.MinVertexIndex = 0;
				MeshElement.MaxVertexIndex = DynamicDataRibbon->VertexData.Num() - 1;
				MeshElement.PrimitiveUniformBufferResource = &WorldSpacePrimitiveUniformBuffer;

				Collector.AddMesh(ViewIndex, MeshBatch);
			}
		}
	}

	CPUTimeMS += MeshElementsTimer.GetElapsedMilliseconds();
}

void NiagaraRendererRibbons::SetDynamicData_RenderThread(FNiagaraDynamicDataBase* NewDynamicData)
{
	check(IsInRenderingThread());

	if (DynamicDataRender)
	{
		delete static_cast<FNiagaraDynamicDataRibbon*>(DynamicDataRender);
		DynamicDataRender = NULL;
	}
	DynamicDataRender = NewDynamicData;
}

int NiagaraRendererRibbons::GetDynamicDataSize()
{
	uint32 Size = sizeof(FNiagaraDynamicDataRibbon);
	if (DynamicDataRender)
	{
		Size += (static_cast<FNiagaraDynamicDataRibbon*>(DynamicDataRender))->VertexData.GetAllocatedSize();
		Size += (static_cast<FNiagaraDynamicDataRibbon*>(DynamicDataRender))->IndexData.GetAllocatedSize();
		Size += (static_cast<FNiagaraDynamicDataRibbon*>(DynamicDataRender))->MaterialParameterVertexData.GetAllocatedSize();
	}

	return Size;
}

bool NiagaraRendererRibbons::HasDynamicData()
{
	return DynamicDataRender && (static_cast<FNiagaraDynamicDataRibbon*>(DynamicDataRender))->VertexData.Num() > 0;
}

#if WITH_EDITORONLY_DATA

const TArray<FNiagaraVariable>& NiagaraRendererRibbons::GetRequiredAttributes()
{
	return Properties->GetRequiredAttributes();
}

const TArray<FNiagaraVariable>& NiagaraRendererRibbons::GetOptionalAttributes()
{
	return Properties->GetOptionalAttributes();
}

#endif


bool NiagaraRendererRibbons::SetMaterialUsage()
{
	return Material && Material->CheckMaterialUsage_Concurrent(MATUSAGE_NiagaraRibbons);
}

FNiagaraDynamicDataBase *NiagaraRendererRibbons::GenerateVertexData(const FNiagaraSceneProxy* Proxy, FNiagaraDataSet &Data, const ENiagaraSimTarget Target)
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraGenRibbonVertexData);

	SimpleTimer VertexDataTimer;
	if (Data.GetNumInstances() < 2 || !bEnabled)
	{
		return nullptr;
	}
	FNiagaraDynamicDataRibbon *DynamicData = new FNiagaraDynamicDataRibbon;
	TArray<FNiagaraRibbonVertex>& RenderData = DynamicData->VertexData;
	TArray<int16>& IndexData = DynamicData->IndexData;

	//I'm not a great fan of pulling scalar components out to a structured vert buffer like this.
	//TODO: Experiment with a new VF that reads the data directly from the scalar layout.
	FNiagaraDataSetIterator<FVector> PosItr(Data, FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Position")));
	FNiagaraDataSetIterator<FVector> VelItr(Data, FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Velocity")));
	FNiagaraDataSetIterator<FLinearColor> ColItr(Data, FNiagaraVariable(FNiagaraTypeDefinition::GetColorDef(), TEXT("Color")));
	FNiagaraDataSetIterator<float> NormAgeItr(Data, FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("NormalizedAge")));
	FNiagaraDataSetIterator<float> AgeItr(Data, FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("NormalizedAge")));
	FNiagaraDataSetIterator<int32> RibbonIdItr(Data, FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("RibbonId")));
	FNiagaraDataSetIterator<float> SizeItr(Data, FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("RibbonWidth")));
	FNiagaraDataSetIterator<float> TwistItr(Data, FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("RibbonTwist")));
	FNiagaraDataSetIterator<FVector> AlignItr(Data, FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("RibbonFacing")));
	FNiagaraDataSetIterator<FVector4> MaterialParamItr(Data, FNiagaraVariable(FNiagaraTypeDefinition::GetVec4Def(), TEXT("DynamicMaterialParameter")));

	//Bail if we don't have the required attributes to render this emitter.
	if (!PosItr.IsValid() || !ColItr.IsValid() || !NormAgeItr.IsValid() || !AgeItr.IsValid() )
	{
		return DynamicData;
	}

	const FNiagaraVariableLayoutInfo* PositionLayout = Data.GetVariableLayout(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Position")));
	const FNiagaraVariableLayoutInfo* TwistLayout = Data.GetVariableLayout(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("RibbonTwist")));
	const FNiagaraVariableLayoutInfo* WidthLayout = Data.GetVariableLayout(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("RibbonWidth")));
	const FNiagaraVariableLayoutInfo* ColorLayout = Data.GetVariableLayout(FNiagaraVariable(FNiagaraTypeDefinition::GetColorDef(), TEXT("Color")));

	// required attributes
	DynamicData->PositionDataOffset = PositionLayout->FloatComponentStart;
	DynamicData->ColorDataOffset = ColorLayout->FloatComponentStart;

	// optional attributes
	int32 IntDummy;
	Data.GetVariableComponentOffsets(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("RibbonWidth")), DynamicData->WidthDataOffset, IntDummy);
	Data.GetVariableComponentOffsets(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("RibbonTwist")), DynamicData->TwistDataOffset, IntDummy);

	DynamicData->DataSet = &Data;
	
	////////

	bool bMultiRibbons = RibbonIdItr.IsValid();

	// build a sorted list by age, so we always get particles in order regardless of them being moved around due to dieing and spawning
	// TODO: need to pull the data out because we currently can't index into the data set; terrible, but works
	TArray<int32> SortedIndices;
	TArray<int32> RibbonIDs;
	TArray<int32> UniqueRibbonIDs;
	TArray<FVector> Positions;
	TArray<FVector> FacingVectors;
	TArray<FVector4> DynamicParams;
	TArray<float> Sizes;
	TArray<FLinearColor> Colors;
	TArray<float> Rotations;
	TArray<float> Ages;
	uint32 CurIdx = 0;
	for (uint32 Idx = 0; Idx < Data.GetNumInstances(); Idx++)
	{
		float Age = NormAgeItr.GetAdvance();
		if (Age >= 0.0f && Age < 1.0f)
		{
			SortedIndices.Add(CurIdx++);
			Positions.Add(PosItr.GetAdvance());
			Sizes.Add(SizeItr.GetAdvanceWithDefault(1.0));
			Rotations.Add(TwistItr.GetAdvanceWithDefault(0.0f));
			FacingVectors.Add(AlignItr.GetAdvanceWithDefault(FVector(0.0f, 0.0f, 1.0f)));
			DynamicParams.Add(MaterialParamItr.GetAdvanceWithDefault(FVector4(0.0f, 0.0f, 0.0f, 0.0f)));

			Ages.Add(Age);
			Colors.Add(ColItr.GetAdvance());
			if (bMultiRibbons)
			{
				int32 RibbonID = RibbonIdItr.GetAdvance();
				RibbonIDs.Add(RibbonID);
				UniqueRibbonIDs.AddUnique(RibbonID);
			}
		}
	}

	if (!bMultiRibbons)
	{
		SortedIndices.Sort(
			[&Ages](const int32& A, const int32& B) {
			return (Ages[A] < Ages[B]);
		}
		);
		UniqueRibbonIDs.Add(0);
	}
	else
	{
		SortedIndices.Sort(
			[&RibbonIDs](const int32& A, const int32& B) {
			return (RibbonIDs[A] < RibbonIDs[B]);
		}
		);
	}


	if (SortedIndices.Num() == 0)
	{
		return DynamicData;
	}

	RenderData.Empty();
	IndexData.Empty();

	// TODO : deal with the dynamic vertex material parameter should the user have specified it as an output...

	FVector2D UVs[4] = { FVector2D(1.0f, 0.0f), FVector2D(1.0f, 1.0f) };
	int32 StartIndex = 0;
	int32 NumIndices = SortedIndices.Num();
	int32 *IndexPtr = &SortedIndices[0];
	int32 NumTotalVerts = 0;

	for (int32 RibbonIdx = 0; RibbonIdx < UniqueRibbonIDs.Num(); RibbonIdx++)
	{
		TArray<int32> SortedPartialIndices;
		if (bMultiRibbons)
		{
			int32 CurID = RibbonIDs[SortedIndices[StartIndex]];
			NumIndices = 0;
			while ((StartIndex + NumIndices)<RibbonIDs.Num() && RibbonIDs[SortedIndices[StartIndex + NumIndices]] == CurID)
			{
				SortedPartialIndices.Add(SortedIndices[StartIndex + NumIndices]);
				NumIndices++;
			}
			SortedPartialIndices.Sort(
				[&Ages](const int32& A, const int32& B) {
				return (Ages[A] < Ages[B]);
			}
			);
			IndexPtr = &SortedPartialIndices[0];
		}

		if (NumIndices > 1)
		{
			FVector PrevPos, PrevPos2, PrevDir(0.0f, 0.0f, 0.1f);
			float TotalDistance = 0.0f;
			FVector2D AgeOffset(Ages.Last(), 0.0f);

			for (int32 i = 0; i < NumIndices; i++)
			{
				uint32 Index1 = IndexPtr[i];
				uint32 Index2 = IndexPtr[i + 1];

				const FVector ParticlePos = Positions[Index1];
				FVector ParticleDir;
				if (i < NumIndices - 1)
				{
					ParticleDir = Positions[Index2] - ParticlePos;
				}
				else
				{
					Index2 = IndexPtr[i - 1];
					ParticleDir = ParticlePos - Positions[Index2];
				}


				// if two ribbon particles were spawned too close together, we skip one
				// but never skip the last, because that will result in invalid indices from the prev loop
				if (ParticleDir.SizeSquared() > 0.002 )
				{
					FVector NormDir = ParticleDir.GetSafeNormal();
					PrevDir = NormDir;
					FVector2D UV0Mult((float)i / NumIndices, 1.0f);
					FVector2D UV1Mult((float)i / NumIndices, 1.0f);

					if (Properties->UV0TilingDistance)
					{
						UV0Mult = FVector2D(TotalDistance / Properties->UV0TilingDistance, 1.0f);
					}
					if (Properties->UV1TilingDistance)
					{
						UV1Mult = FVector2D(TotalDistance / Properties->UV1TilingDistance, 1.0f);
					}

					TotalDistance += ParticleDir.Size();
					AddRibbonVert(RenderData, ParticlePos, UVs[0] * UV0Mult + AgeOffset, UVs[0] * UV1Mult + AgeOffset, Colors[Index1], Ages[Index1], Rotations[Index1], Sizes[Index1], NormDir, FacingVectors[Index1]);
					AddRibbonVert(RenderData, ParticlePos, UVs[1] * UV0Mult + AgeOffset, UVs[1] * UV1Mult + AgeOffset, Colors[Index1], Ages[Index1], Rotations[Index1], Sizes[Index1], NormDir, FacingVectors[Index1]);
					if (DynamicParams.Num())
					{
						AddDynamicParam(DynamicData->MaterialParameterVertexData, DynamicParams[Index1]);
						AddDynamicParam(DynamicData->MaterialParameterVertexData, DynamicParams[Index1]);
					}

					if (i < NumIndices - 1)
					{
						IndexData.Add(NumTotalVerts);
						IndexData.Add(NumTotalVerts + 1);
						IndexData.Add(NumTotalVerts + 2);
						IndexData.Add(NumTotalVerts + 1);
						IndexData.Add(NumTotalVerts + 3);
						IndexData.Add(NumTotalVerts + 2);
					}
					NumTotalVerts += 2;
				}
			}
		}
		StartIndex += NumIndices;
	}

	CPUTimeMS = VertexDataTimer.GetElapsedMilliseconds();

	return DynamicData;
}


