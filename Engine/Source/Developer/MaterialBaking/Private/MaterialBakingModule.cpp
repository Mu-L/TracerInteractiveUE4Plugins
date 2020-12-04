// Copyright Epic Games, Inc. All Rights Reserved.

#include "MaterialBakingModule.h"
#include "MaterialRenderItem.h"
#include "Engine/TextureRenderTarget2D.h"
#include "ExportMaterialProxy.h"
#include "Interfaces/IMainFrameModule.h"
#include "MaterialOptionsWindow.h"
#include "MaterialOptions.h"
#include "PropertyEditorModule.h"
#include "MaterialOptionsCustomization.h"
#include "UObject/UObjectGlobals.h"
#include "MaterialBakingStructures.h"
#include "Framework/Application/SlateApplication.h"
#include "MaterialBakingHelpers.h"
#include "Async/Async.h"
#include "Async/ParallelFor.h"
#include "Materials/MaterialInstance.h"
#include "Materials/MaterialInstanceConstant.h"
#include "MaterialEditor/MaterialEditorInstanceConstant.h"
#include "RenderingThread.h"
#include "RHISurfaceDataConversion.h"
#include "Misc/ScopedSlowTask.h"
#include "MeshDescription.h"
#if WITH_EDITOR
#include "Misc/FileHelper.h"
#endif

IMPLEMENT_MODULE(FMaterialBakingModule, MaterialBaking);

#define LOCTEXT_NAMESPACE "MaterialBakingModule"

/** Cvars for advanced features */
static TAutoConsoleVariable<int32> CVarUseMaterialProxyCaching(
	TEXT("MaterialBaking.UseMaterialProxyCaching"),
	1,
	TEXT("Determines whether or not Material Proxies should be cached to speed up material baking.\n")
	TEXT("0: Turned Off\n")
	TEXT("1: Turned On"),	
	ECVF_Default);

static TAutoConsoleVariable<int32> CVarSaveIntermediateTextures(
	TEXT("MaterialBaking.SaveIntermediateTextures"),
	0,
	TEXT("Determines whether or not to save out intermediate BMP images for each flattened material property.\n")
	TEXT("0: Turned Off\n")
	TEXT("1: Turned On"),
	ECVF_Default);

namespace FMaterialBakingModuleImpl
{
	// Custom dynamic mesh allocator specifically tailored for Material Baking.
	// This will always reuse the same couple buffers, so searching linearly is not a problem.
	class FMaterialBakingDynamicMeshBufferAllocator : public FDynamicMeshBufferAllocator
	{
		// This must be smaller than the large allocation blocks on Windows 10 which is currently ~508K.
		// Large allocations uses VirtualAlloc directly without any kind of buffering before
		// releasing pages to the kernel, so it causes lots of soft page fault when
		// memory is first initialized.
		const uint32 SmallestPooledBufferSize = 256*1024;

		TArray<FIndexBufferRHIRef>  IndexBuffers;
		TArray<FVertexBufferRHIRef> VertexBuffers;

		template <typename RefType>
		RefType GetSmallestFit(uint32 SizeInBytes, TArray<RefType>& Array)
		{
			uint32 SmallestFitIndex = UINT32_MAX;
			uint32 SmallestFitSize  = UINT32_MAX;
			for (int32 Index = 0; Index < Array.Num(); ++Index)
			{
				uint32 Size = Array[Index]->GetSize();
				if (Size >= SizeInBytes && (SmallestFitIndex == UINT32_MAX || Size < SmallestFitSize))
				{
					SmallestFitIndex = Index;
					SmallestFitSize  = Size;
				}
			}

			RefType Ref;
			// Do not reuse the smallest fit if it's a lot bigger than what we requested
			if (SmallestFitIndex != UINT32_MAX && SmallestFitSize < SizeInBytes*2)
			{
				Ref = Array[SmallestFitIndex];
				Array.RemoveAtSwap(SmallestFitIndex);
			}

			return Ref;
		}

		virtual FIndexBufferRHIRef AllocIndexBuffer(uint32 NumElements) override
		{
			uint32 BufferSize = GetIndexBufferSize(NumElements);
			if (BufferSize > SmallestPooledBufferSize)
			{
				FIndexBufferRHIRef Ref = GetSmallestFit(GetIndexBufferSize(NumElements), IndexBuffers);
				if (Ref.IsValid())
				{
					return Ref;
				}
			}

			return FDynamicMeshBufferAllocator::AllocIndexBuffer(NumElements);
		}

		virtual void ReleaseIndexBuffer(FIndexBufferRHIRef& IndexBufferRHI) override
		{
			if (IndexBufferRHI->GetSize() > SmallestPooledBufferSize)
			{
				IndexBuffers.Add(MoveTemp(IndexBufferRHI));
			}

			IndexBufferRHI = nullptr;
		}

		virtual FVertexBufferRHIRef AllocVertexBuffer(uint32 Stride, uint32 NumElements) override
		{
			uint32 BufferSize = GetVertexBufferSize(Stride, NumElements);
			if (BufferSize > SmallestPooledBufferSize)
			{
				FVertexBufferRHIRef Ref = GetSmallestFit(BufferSize, VertexBuffers);
				if (Ref.IsValid())
				{
					return Ref;
				}
			}

			return FDynamicMeshBufferAllocator::AllocVertexBuffer(Stride, NumElements);
		}

		virtual void ReleaseVertexBuffer(FVertexBufferRHIRef& VertexBufferRHI) override
		{
			if (VertexBufferRHI->GetSize() > SmallestPooledBufferSize)
			{
				VertexBuffers.Add(MoveTemp(VertexBufferRHI));
			}

			VertexBufferRHI = nullptr;
		}
	};

	class FStagingBufferPool
	{
	public:
		FTexture2DRHIRef CreateStagingBuffer_RenderThread(FRHICommandListImmediate& RHICmdList, int32 Width, int32 Height, EPixelFormat Format)
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(CreateStagingBuffer_RenderThread)

			auto StagingBufferPredicate = 
				[Width, Height, Format](const FTexture2DRHIRef& Texture2DRHIRef)
				{
					return Texture2DRHIRef->GetSizeX() == Width && Texture2DRHIRef->GetSizeY() == Height && Texture2DRHIRef->GetFormat() == Format;
				};

			// Process any staging buffers available for unmapping
			{
				TArray<FTexture2DRHIRef> ToUnmapLocal;
				{
					FScopeLock Lock(&ToUnmapLock);
					ToUnmapLocal = MoveTemp(ToUnmap);
				}

				for (int32 Index = 0, Num = ToUnmapLocal.Num(); Index < Num; ++Index)
				{
					RHICmdList.UnmapStagingSurface(ToUnmapLocal[Index]);
					Pool.Add(MoveTemp(ToUnmapLocal[Index]));
				}
			}

			// Find any pooled staging buffer with suitable properties.
			int32 Index = Pool.IndexOfByPredicate(StagingBufferPredicate);

			if (Index != -1)
			{
				FTexture2DRHIRef StagingBuffer = MoveTemp(Pool[Index]);
				Pool.RemoveAtSwap(Index);
				return StagingBuffer;
			}

			TRACE_CPUPROFILER_EVENT_SCOPE(RHICreateTexture2D)
			FRHIResourceCreateInfo CreateInfo;
			return RHICreateTexture2D(Width, Height, Format, 1, 1, TexCreate_CPUReadback, CreateInfo);
		}

		void ReleaseStagingBufferForUnmap_AnyThread(FTexture2DRHIRef& Texture2DRHIRef)
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(ReleaseStagingBufferForUnmap_AnyThread)
			FScopeLock Lock(&ToUnmapLock);
			ToUnmap.Emplace(MoveTemp(Texture2DRHIRef));
		}

		void Clear_RenderThread(FRHICommandListImmediate& RHICmdList)
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(Clear_RenderThread)
			for (FTexture2DRHIRef& StagingSurface : ToUnmap)
			{
				RHICmdList.UnmapStagingSurface(StagingSurface);
			}

			ToUnmap.Empty();
			Pool.Empty();
		}

		~FStagingBufferPool()
		{
			check(Pool.Num() == 0);
		}

	private:
		TArray<FTexture2DRHIRef> Pool;

		// Not contented enough to warrant the use of lockless structures.
		FCriticalSection         ToUnmapLock;
		TArray<FTexture2DRHIRef> ToUnmap;
	};

	struct FRenderItemKey
	{
		const FMeshData* RenderData;
		const FIntPoint  RenderSize;

		FRenderItemKey(const FMeshData* InRenderData, const FIntPoint& InRenderSize)
			: RenderData(InRenderData)
			, RenderSize(InRenderSize)
		{
		}

		bool operator == (const FRenderItemKey& Other) const
		{
			return RenderData == Other.RenderData &&
				RenderSize == Other.RenderSize;
		}
	};

	uint32 GetTypeHash(const FRenderItemKey& Key)
	{
		return HashCombine(GetTypeHash(Key.RenderData), GetTypeHash(Key.RenderSize));
	}
}

void FMaterialBakingModule::StartupModule()
{
	// Set which properties should enforce gamma correction
	FMemory::Memset(PerPropertyGamma, (uint8)false);
	PerPropertyGamma[MP_Normal] = true;
	PerPropertyGamma[MP_Opacity] = true;
	PerPropertyGamma[MP_OpacityMask] = true;

	// Set which pixel format should be used for the possible baked out material properties
	FMemory::Memset(PerPropertyFormat, (uint8)PF_Unknown);
	PerPropertyFormat[MP_EmissiveColor] = PF_FloatRGBA;
	PerPropertyFormat[MP_Opacity] = PF_B8G8R8A8;
	PerPropertyFormat[MP_OpacityMask] = PF_B8G8R8A8;
	PerPropertyFormat[MP_BaseColor] = PF_B8G8R8A8;
	PerPropertyFormat[MP_Metallic] = PF_B8G8R8A8;
	PerPropertyFormat[MP_Specular] = PF_B8G8R8A8;
	PerPropertyFormat[MP_Roughness] = PF_B8G8R8A8;
	PerPropertyFormat[MP_Anisotropy] = PF_B8G8R8A8;
	PerPropertyFormat[MP_Normal] = PF_B8G8R8A8;
	PerPropertyFormat[MP_Tangent] = PF_B8G8R8A8;
	PerPropertyFormat[MP_AmbientOcclusion] = PF_B8G8R8A8;
	PerPropertyFormat[MP_SubsurfaceColor] = PF_B8G8R8A8;

	// Register property customization
	FPropertyEditorModule& Module = FModuleManager::Get().LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	Module.RegisterCustomPropertyTypeLayout(TEXT("PropertyEntry"), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FPropertyEntryCustomization::MakeInstance));
	
	// Register callback for modified objects
	FCoreUObjectDelegates::OnObjectModified.AddRaw(this, &FMaterialBakingModule::OnObjectModified);
}

void FMaterialBakingModule::ShutdownModule()
{
	// Unregister customization and callback
	FPropertyEditorModule* PropertyEditorModule = FModuleManager::GetModulePtr<FPropertyEditorModule>("PropertyEditor");

	if (PropertyEditorModule)
	{
		PropertyEditorModule->UnregisterCustomPropertyTypeLayout(TEXT("PropertyEntry"));
	}
	FCoreUObjectDelegates::OnObjectModified.RemoveAll(this);
}

void FMaterialBakingModule::BakeMaterials(const TArray<FMaterialData*>& MaterialSettings, const TArray<FMeshData*>& MeshSettings, TArray<FBakeOutput>& Output)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FMaterialBakingModule::BakeMaterials)

	checkf(MaterialSettings.Num() == MeshSettings.Num(), TEXT("Number of material settings does not match that of MeshSettings"));
	const int32 NumMaterials = MaterialSettings.Num();
	const bool bSaveIntermediateTextures = CVarSaveIntermediateTextures.GetValueOnAnyThread() == 1;

	using namespace FMaterialBakingModuleImpl;
	FMaterialBakingDynamicMeshBufferAllocator MaterialBakingDynamicMeshBufferAllocator;

	FScopedSlowTask Progress(NumMaterials, LOCTEXT("BakeMaterials", "Baking Materials..."), true );
	Progress.MakeDialog(true);

	TArray<uint32> ProcessingOrder;
	ProcessingOrder.Reserve(MeshSettings.Num());
	for (int32 Index = 0; Index < MeshSettings.Num(); ++Index)
	{
		ProcessingOrder.Add(Index);
	}

	// Start with the biggest mesh first so we can always reuse the same vertex/index buffers.
	// This will decrease the number of allocations backed by newly allocated memory from the OS,
	// which will reduce soft page faults while copying into that memory.
	// Soft page faults are now incredibly expensive on Windows 10.
	Algo::SortBy(
		ProcessingOrder,
		[&MeshSettings](const uint32 Index){ return MeshSettings[Index]->RawMeshDescription ? MeshSettings[Index]->RawMeshDescription->Vertices().Num() : 0; },
		TGreater<>()
	);

	Output.SetNum(NumMaterials);

	struct FPipelineContext
	{
		typedef TFunction<void (FRHICommandListImmediate& RHICmdList)> FReadCommand;
		FReadCommand ReadCommand;
	};

	// Distance between the command sent to rendering and the GPU read-back of the result
	// to minimize sync time waiting on GPU.
	const int32 PipelineDepth = 16;
	int32 PipelineIndex = 0;
	FPipelineContext PipelineContext[PipelineDepth];

	// This will create and prepare FMeshMaterialRenderItem for each property sizes we're going to need
	auto PrepareRenderItems_AnyThread =
		[&](int32 MaterialIndex)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PrepareRenderItems);
		
		TMap<FMaterialBakingModuleImpl::FRenderItemKey, FMeshMaterialRenderItem*>* RenderItems = new TMap<FRenderItemKey, FMeshMaterialRenderItem *>();
		const FMaterialData* CurrentMaterialSettings = MaterialSettings[MaterialIndex];
		const FMeshData* CurrentMeshSettings = MeshSettings[MaterialIndex];

		for (TMap<EMaterialProperty, FIntPoint>::TConstIterator PropertySizeIterator = CurrentMaterialSettings->PropertySizes.CreateConstIterator(); PropertySizeIterator; ++PropertySizeIterator)
		{
			FRenderItemKey RenderItemKey(CurrentMeshSettings, PropertySizeIterator.Value());
			if (RenderItems->Find(RenderItemKey) == nullptr)
			{
				RenderItems->Add(RenderItemKey, new FMeshMaterialRenderItem(CurrentMaterialSettings, CurrentMeshSettings, PropertySizeIterator.Key(), &MaterialBakingDynamicMeshBufferAllocator));
			}
		}

		return RenderItems;
	};

	// We reuse the pipeline depth to prepare render items in advance to avoid stalling the game thread
	int NextRenderItem = 0;
	TFuture<TMap<FRenderItemKey, FMeshMaterialRenderItem*>*> PreparedRenderItems[PipelineDepth];
	for (; NextRenderItem < NumMaterials && NextRenderItem < PipelineDepth; ++NextRenderItem)
	{
		PreparedRenderItems[NextRenderItem] = 
			Async(
				EAsyncExecution::ThreadPool,
				[&PrepareRenderItems_AnyThread, &ProcessingOrder, NextRenderItem]()
				{
					return PrepareRenderItems_AnyThread(ProcessingOrder[NextRenderItem]);
				}
			);
	}

	// Create all material proxies right away to start compiling shaders asynchronously and avoid stalling the baking process as much as possible
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(CreateMaterialProxies)

		for (int32 Index = 0; Index < NumMaterials; ++Index)
		{
			int32 MaterialIndex = ProcessingOrder[Index];
			const FMaterialData* CurrentMaterialSettings = MaterialSettings[MaterialIndex];

			TArray<UTexture*> MaterialTextures;
			CurrentMaterialSettings->Material->GetUsedTextures(MaterialTextures, EMaterialQualityLevel::Num, true, GMaxRHIFeatureLevel, true);

			// Force load materials used by the current material
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(LoadTexturesForMaterial)

				for (UTexture* Texture : MaterialTextures)
				{
					if (Texture != NULL)
					{
						UTexture2D* Texture2D = Cast<UTexture2D>(Texture);
						if (Texture2D)
						{
							Texture2D->SetForceMipLevelsToBeResident(30.0f);
							Texture2D->WaitForStreaming();
						}
					}
				}
			}

			for (TMap<EMaterialProperty, FIntPoint>::TConstIterator PropertySizeIterator = CurrentMaterialSettings->PropertySizes.CreateConstIterator(); PropertySizeIterator; ++PropertySizeIterator)
			{
				// They will be stored in the pool and compiled asynchronously
				CreateMaterialProxy(CurrentMaterialSettings->Material, PropertySizeIterator.Key());
			}
		}
	}

	TAtomic<uint32>    NumTasks(0);
	FStagingBufferPool StagingBufferPool;

	for (int32 Index = 0; Index < NumMaterials; ++Index)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(BakeOneMaterial)

		Progress.EnterProgressFrame(1.0f, FText::Format(LOCTEXT("BakingMaterial", "Baking Material {0}/{1}"), Index, NumMaterials));

		int32 MaterialIndex = ProcessingOrder[Index];
		TMap<FRenderItemKey, FMeshMaterialRenderItem*>* RenderItems;

		{
			TRACE_CPUPROFILER_EVENT_SCOPE(WaitOnPreparedRenderItems)
			RenderItems = PreparedRenderItems[Index % PipelineDepth].Get();
		}

		// Prepare the next render item in advance
		if (NextRenderItem < NumMaterials)
		{
			check((NextRenderItem % PipelineDepth) == (Index % PipelineDepth));
			PreparedRenderItems[NextRenderItem % PipelineDepth] = 
				Async(
					EAsyncExecution::ThreadPool,
					[&PrepareRenderItems_AnyThread, NextMaterialIndex = ProcessingOrder[NextRenderItem]]()
					{
						return PrepareRenderItems_AnyThread(NextMaterialIndex);
					}
				);
			NextRenderItem++;
		}

		const FMaterialData* CurrentMaterialSettings = MaterialSettings[MaterialIndex];
		const FMeshData* CurrentMeshSettings = MeshSettings[MaterialIndex];
		FBakeOutput& CurrentOutput = Output[MaterialIndex];

		TArray<EMaterialProperty> MaterialPropertiesToBakeOut;
		CurrentMaterialSettings->PropertySizes.GenerateKeyArray(MaterialPropertiesToBakeOut);

		const int32 NumPropertiesToRender = MaterialPropertiesToBakeOut.Num();
		if (NumPropertiesToRender > 0)
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(RenderOneMaterial)

			// Ensure data in memory will not change place passed this point to avoid race conditions
			CurrentOutput.PropertySizes = CurrentMaterialSettings->PropertySizes;
			for (int32 PropertyIndex = 0; PropertyIndex < NumPropertiesToRender; ++PropertyIndex)
			{
				const EMaterialProperty Property = MaterialPropertiesToBakeOut[PropertyIndex];
				CurrentOutput.PropertyData.Add(Property);
			}

			for (int32 PropertyIndex = 0; PropertyIndex < NumPropertiesToRender; ++PropertyIndex)
			{
				const EMaterialProperty Property = MaterialPropertiesToBakeOut[PropertyIndex];
				FExportMaterialProxy* ExportMaterialProxy = CreateMaterialProxy(CurrentMaterialSettings->Material, Property);

				if (!ExportMaterialProxy->IsCompilationFinished())
				{
					TRACE_CPUPROFILER_EVENT_SCOPE(WaitForMaterialProxyCompilation)
					ExportMaterialProxy->FinishCompilation();
				}

				// It is safe to reuse the same render target for each draw pass since they all execute sequentially on the GPU and are copied to staging buffers before
				// being reused.
				UTextureRenderTarget2D* RenderTarget = CreateRenderTarget(PerPropertyGamma[Property], PerPropertyFormat[Property], CurrentOutput.PropertySizes[Property]);
				if (RenderTarget != nullptr)
				{
					// Perform everything left of the operation directly on the render thread since we need to modify some RenderItem's properties
					// for each render pass and we can't do that without costly synchronization (flush) between the game thread and render thread.
					// Everything slow to execute has already been prepared on the game thread anyway.
					ENQUEUE_RENDER_COMMAND(RenderOneMaterial)(
						[this, RenderItems, RenderTarget, Property, ExportMaterialProxy, &PipelineContext, PipelineIndex, &StagingBufferPool, &NumTasks, bSaveIntermediateTextures, &MaterialSettings, &MeshSettings, MaterialIndex, &Output](FRHICommandListImmediate& RHICmdList)
						{
							const FMaterialData* CurrentMaterialSettings = MaterialSettings[MaterialIndex];
							const FMeshData* CurrentMeshSettings = MeshSettings[MaterialIndex];

							FMeshMaterialRenderItem& RenderItem = *RenderItems->FindChecked(FRenderItemKey(CurrentMeshSettings, FIntPoint(RenderTarget->GetSurfaceWidth(), RenderTarget->GetSurfaceHeight())));

							FSceneViewFamily ViewFamily(FSceneViewFamily::ConstructionValues(RenderTarget->GetRenderTargetResource(), nullptr,
								FEngineShowFlags(ESFIM_Game))
								.SetWorldTimes(0.0f, 0.0f, 0.0f)
								.SetGammaCorrection(RenderTarget->GetRenderTargetResource()->GetDisplayGamma()));

							RenderItem.MaterialProperty = Property;
							RenderItem.MaterialRenderProxy = ExportMaterialProxy;
							RenderItem.ViewFamily = &ViewFamily;

							FTextureRenderTargetResource* RenderTargetResource = RenderTarget->GetRenderTargetResource();
							FCanvas Canvas(RenderTargetResource, nullptr, FApp::GetCurrentTime() - GStartTime, FApp::GetDeltaTime(), FApp::GetCurrentTime() - GStartTime, GMaxRHIFeatureLevel);
							Canvas.SetAllowedModes(FCanvas::Allow_Flush);
							Canvas.SetRenderTargetRect(FIntRect(0, 0, RenderTarget->GetSurfaceWidth(), RenderTarget->GetSurfaceHeight()));
							Canvas.SetBaseTransform(Canvas.CalcBaseTransform2D(RenderTarget->GetSurfaceWidth(), RenderTarget->GetSurfaceHeight()));

							// Do rendering
							Canvas.Clear(RenderTarget->ClearColor);
							FCanvas::FCanvasSortElement& SortElement = Canvas.GetSortElement(Canvas.TopDepthSortKey());
							SortElement.RenderBatchArray.Add(&RenderItem);
							Canvas.Flush_RenderThread(RHICmdList);
							SortElement.RenderBatchArray.Empty();

							FTexture2DRHIRef StagingBufferRef = StagingBufferPool.CreateStagingBuffer_RenderThread(RHICmdList, RenderTargetResource->GetSizeX(), RenderTargetResource->GetSizeY(), PerPropertyFormat[Property]);
							FGPUFenceRHIRef GPUFence = RHICreateGPUFence(TEXT("MaterialBackingFence"));

							FResolveRect Rect(0, 0, RenderTargetResource->GetSizeX(), RenderTargetResource->GetSizeY());
							RHICmdList.CopyToResolveTarget(RenderTargetResource->GetRenderTargetTexture(), StagingBufferRef, FResolveParams(Rect));	
							RHICmdList.WriteGPUFence(GPUFence);

							// Prepare a lambda for final processing that will be executed asynchronously
							NumTasks++;
							auto FinalProcessing_AnyThread =
								[&NumTasks, bSaveIntermediateTextures, CurrentMaterialSettings, &StagingBufferPool, &Output, Property, MaterialIndex](FTexture2DRHIRef& StagingBuffer, void * Data, int32 DataWidth, int32 DataHeight)
								{
									TRACE_CPUPROFILER_EVENT_SCOPE(FinalProcessing)

									FBakeOutput& CurrentOutput  = Output[MaterialIndex];
									TArray<FColor>& OutputColor = CurrentOutput.PropertyData[Property];
									FIntPoint& OutputSize       = CurrentOutput.PropertySizes[Property];

									OutputColor.SetNum(OutputSize.X * OutputSize.Y);

									if (Property == MP_EmissiveColor)
									{
										// Only one thread will write to CurrentOutput.EmissiveScale since there can be only one emissive channel property per FBakeOutput
										FMaterialBakingModule::ProcessEmissiveOutput((const FFloat16Color*)Data, DataWidth, OutputSize, OutputColor, CurrentOutput.EmissiveScale);
									}
									else
									{
										TRACE_CPUPROFILER_EVENT_SCOPE(ConvertRawB8G8R8A8DataToFColor)
										
										check(StagingBuffer->GetFormat() == PF_B8G8R8A8);
										ConvertRawB8G8R8A8DataToFColor(OutputSize.X, OutputSize.Y, (uint8*)Data, DataWidth * sizeof(FColor), OutputColor.GetData());
									}

									// We can't unmap ourself since we're not on the render thread
									StagingBufferPool.ReleaseStagingBufferForUnmap_AnyThread(StagingBuffer);

									if (CurrentMaterialSettings->bPerformBorderSmear)
									{
										// This will resize the output to a single pixel if the result is monochrome.
										FMaterialBakingHelpers::PerformUVBorderSmearAndShrink(OutputColor, OutputSize.X, OutputSize.Y);
									}
#if WITH_EDITOR
									// If saving intermediates is turned on
									if (bSaveIntermediateTextures)
									{
										TRACE_CPUPROFILER_EVENT_SCOPE(SaveIntermediateTextures)
										const UEnum* PropertyEnum = StaticEnum<EMaterialProperty>();
										FName PropertyName = PropertyEnum->GetNameByValue(Property);
										FString TrimmedPropertyName = PropertyName.ToString();
										TrimmedPropertyName.RemoveFromStart(TEXT("MP_"));

										const FString DirectoryPath = FPaths::ConvertRelativePathToFull(FPaths::ProjectIntermediateDir() + TEXT("MaterialBaking/"));
										FString FilenameString = FString::Printf(TEXT("%s%s-%d-%s.bmp"), *DirectoryPath, *CurrentMaterialSettings->Material->GetName(), MaterialIndex, *TrimmedPropertyName);
										FFileHelper::CreateBitmap(*FilenameString, CurrentOutput.PropertySizes[Property].X, CurrentOutput.PropertySizes[Property].Y, CurrentOutput.PropertyData[Property].GetData());
									}
#endif // WITH_EDITOR
									NumTasks--;
								};

							// Run previous command if we're going to overwrite it meaning pipeline depth has been reached
							if (PipelineContext[PipelineIndex].ReadCommand)
							{
								PipelineContext[PipelineIndex].ReadCommand(RHICmdList);
							}

							// Generate a texture reading command that will be executed once it reaches the end of the pipeline
							PipelineContext[PipelineIndex].ReadCommand =
								[FinalProcessing_AnyThread, StagingBufferRef = MoveTemp(StagingBufferRef), GPUFence = MoveTemp(GPUFence)](FRHICommandListImmediate& RHICmdList) mutable
								{
									TRACE_CPUPROFILER_EVENT_SCOPE(MapAndEnqueue)

									void * Data = nullptr;
									int32 Width; int32 Height;
									RHICmdList.MapStagingSurface(StagingBufferRef, GPUFence.GetReference(), Data, Width, Height);

									// Schedule the copy and processing on another thread to free up the render thread as much as possible
									Async(
										EAsyncExecution::ThreadPool,
										[FinalProcessing_AnyThread, Data, Width, Height, StagingBufferRef = MoveTemp(StagingBufferRef)]() mutable
										{
											FinalProcessing_AnyThread(StagingBufferRef, Data, Width, Height);
										}
									);
								};
						}
					);

					PipelineIndex = (PipelineIndex + 1) % PipelineDepth;
				}
			}

		}

		// Destroying Render Items must happen on the render thread to ensure
		// they are not used anymore.
		ENQUEUE_RENDER_COMMAND(DestroyRenderItems)(
			[RenderItems](FRHICommandListImmediate& RHICmdList)
			{
				for (auto RenderItem : (*RenderItems))
				{
					delete RenderItem.Value;
				}

				delete RenderItems;
			}
		);
	}

	ENQUEUE_RENDER_COMMAND(ProcessRemainingReads)(
		[&PipelineContext, PipelineDepth, PipelineIndex](FRHICommandListImmediate& RHICmdList)
		{
			// Enqueue remaining reads
			for (int32 Index = 0; Index < PipelineDepth; Index++)
			{
				int32 LocalPipelineIndex = (PipelineIndex + Index) % PipelineDepth;

				if (PipelineContext[LocalPipelineIndex].ReadCommand)
				{
					PipelineContext[LocalPipelineIndex].ReadCommand(RHICmdList);
				}
			}
		}
	);

	// Wait until every tasks have been queued so that NumTasks is only decreasing
	FlushRenderingCommands();

	// Wait for any remaining final processing tasks
	while (NumTasks.Load(EMemoryOrder::Relaxed) > 0)
	{
		FPlatformProcess::Sleep(0.1f);
	}

	// Wait for all tasks to have been processed before clearing the staging buffers
	FlushRenderingCommands();

	ENQUEUE_RENDER_COMMAND(ClearStagingBufferPool)(
		[&StagingBufferPool](FRHICommandListImmediate& RHICmdList)
		{
			StagingBufferPool.Clear_RenderThread(RHICmdList);
		}
	);

	// Wait for StagingBufferPool clear to have executed before exiting the function
	FlushRenderingCommands();

	if (!CVarUseMaterialProxyCaching.GetValueOnAnyThread())
	{
		CleanupMaterialProxies();
	}
}

bool FMaterialBakingModule::SetupMaterialBakeSettings(TArray<TWeakObjectPtr<UObject>>& OptionObjects, int32 NumLODs)
{
	TSharedRef<SWindow> Window = SNew(SWindow)
		.Title(LOCTEXT("WindowTitle", "Material Baking Options"))
		.SizingRule(ESizingRule::Autosized);

	TSharedPtr<SMaterialOptions> Options;

	Window->SetContent
	(
		SAssignNew(Options, SMaterialOptions)
		.WidgetWindow(Window)
		.NumLODs(NumLODs)
		.SettingsObjects(OptionObjects)
	);

	TSharedPtr<SWindow> ParentWindow;
	if (FModuleManager::Get().IsModuleLoaded("MainFrame"))
	{
		IMainFrameModule& MainFrame = FModuleManager::LoadModuleChecked<IMainFrameModule>("MainFrame");
		ParentWindow = MainFrame.GetParentWindow();
		FSlateApplication::Get().AddModalWindow(Window, ParentWindow, false);
		return !Options->WasUserCancelled();
	}

	return false;
}

void FMaterialBakingModule::CleanupMaterialProxies()
{
	for (auto Iterator : MaterialProxyPool)
	{
		delete Iterator.Value.Value;
	}
	MaterialProxyPool.Reset();
}

UTextureRenderTarget2D* FMaterialBakingModule::CreateRenderTarget(bool bInForceLinearGamma, EPixelFormat InPixelFormat, const FIntPoint& InTargetSize)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FMaterialBakingModule::CreateRenderTarget)

	UTextureRenderTarget2D* RenderTarget = nullptr;
	const int32 MaxTextureSize = 1 << (MAX_TEXTURE_MIP_COUNT - 1); // Don't use GetMax2DTextureDimension() as this is for the RHI only.
	const FIntPoint ClampedTargetSize(FMath::Clamp(InTargetSize.X, 1, MaxTextureSize), FMath::Clamp(InTargetSize.Y, 1, MaxTextureSize));
	auto RenderTargetComparison = [bInForceLinearGamma, InPixelFormat, ClampedTargetSize](const UTextureRenderTarget2D* CompareRenderTarget) -> bool
	{
		return (CompareRenderTarget->SizeX == ClampedTargetSize.X && CompareRenderTarget->SizeY == ClampedTargetSize.Y && CompareRenderTarget->OverrideFormat == InPixelFormat && CompareRenderTarget->bForceLinearGamma == bInForceLinearGamma);
	};

	// Find any pooled render target with suitable properties.
	UTextureRenderTarget2D** FindResult = RenderTargetPool.FindByPredicate(RenderTargetComparison);
	
	if (FindResult)
	{
		RenderTarget = *FindResult;
	}
	else
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(CreateNewRenderTarget)

		// Not found - create a new one.
		RenderTarget = NewObject<UTextureRenderTarget2D>();
		check(RenderTarget);
		RenderTarget->AddToRoot();
		RenderTarget->ClearColor = FLinearColor(1.0f, 0.0f, 1.0f);
		RenderTarget->ClearColor.A = 1.0f;
		RenderTarget->TargetGamma = 0.0f;
		RenderTarget->InitCustomFormat(ClampedTargetSize.X, ClampedTargetSize.Y, InPixelFormat, bInForceLinearGamma);

		RenderTargetPool.Add(RenderTarget);
	}

	checkf(RenderTarget != nullptr, TEXT("Unable to create or find valid render target"));
	return RenderTarget;
}

FExportMaterialProxy* FMaterialBakingModule::CreateMaterialProxy(UMaterialInterface* Material, const EMaterialProperty Property)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FMaterialBakingModule::CreateMaterialProxy)

	FExportMaterialProxy* Proxy = nullptr;

	// Find all pooled material proxy matching this material
	TArray<FMaterialPoolValue> Entries;
	MaterialProxyPool.MultiFind(Material, Entries);

	// Look for the matching property
	for (FMaterialPoolValue& Entry : Entries)
	{
		if (Entry.Key == Property)
		{
			Proxy = Entry.Value;
			break;
		}
	}

	// Not found, create a new entry
	if (Proxy == nullptr)
	{
		Proxy = new FExportMaterialProxy(Material, Property, false /* bInSynchronousCompilation */);
		MaterialProxyPool.Add(Material, FMaterialPoolValue(Property, Proxy));
	}

	return Proxy;
}

void FMaterialBakingModule::ProcessEmissiveOutput(const FFloat16Color* Color16, int32 Color16Pitch, const FIntPoint& OutputSize, TArray<FColor>& OutputColor, float& EmissiveScale)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FMaterialBakingModule::ProcessEmissiveOutput)

	const int32 NumThreads = [&]()
	{
		return FPlatformProcess::SupportsMultithreading() ? FPlatformMisc::NumberOfCores() : 1;
	}();

	float* MaxValue = new float[NumThreads];
	FMemory::Memset(MaxValue, 0, NumThreads * sizeof(MaxValue[0]));
	const int32 LinesPerThread = FMath::CeilToInt((float)OutputSize.Y / (float)NumThreads);

	// Find maximum float value across texture
	ParallelFor(NumThreads, [&Color16, LinesPerThread, MaxValue, OutputSize, Color16Pitch](int32 Index)
	{
		const int32 EndY = FMath::Min((Index + 1) * LinesPerThread, OutputSize.Y);			
		float& CurrentMaxValue = MaxValue[Index];
		const FFloat16Color MagentaFloat16 = FFloat16Color(FLinearColor(1.0f, 0.0f, 1.0f));
		for (int32 PixelY = Index * LinesPerThread; PixelY < EndY; ++PixelY)
		{
			const int32 SrcYOffset = PixelY * Color16Pitch;
			for (int32 PixelX = 0; PixelX < OutputSize.X; PixelX++)
			{
				const FFloat16Color& Pixel16 = Color16[PixelX + SrcYOffset];
				// Find maximum channel value across texture
				if (!(Pixel16 == MagentaFloat16))
				{
					CurrentMaxValue = FMath::Max(CurrentMaxValue, FMath::Max3(Pixel16.R.GetFloat(), Pixel16.G.GetFloat(), Pixel16.B.GetFloat()));
				}
			}
		}
	});

	const float GlobalMaxValue = [&MaxValue, NumThreads]
	{
		float TempValue = 0.0f;
		for (int32 ThreadIndex = 0; ThreadIndex < NumThreads; ++ThreadIndex)
		{
			TempValue = FMath::Max(TempValue, MaxValue[ThreadIndex]);
		}

		return TempValue;
	}();
		
	if (GlobalMaxValue <= 0.01f)
	{
		// Black emissive, drop it			
	}

	// Now convert Float16 to Color using the scale
	OutputColor.SetNumUninitialized(OutputSize.X * OutputSize.Y);
	const float Scale = 255.0f / GlobalMaxValue;
	ParallelFor(NumThreads, [&Color16, LinesPerThread, &OutputColor, OutputSize, Color16Pitch, Scale](int32 Index)
	{
		const FFloat16Color MagentaFloat16 = FFloat16Color(FLinearColor(1.0f, 0.0f, 1.0f));

		const int32 EndY = FMath::Min((Index + 1) * LinesPerThread, OutputSize.Y);
		for (int32 PixelY = Index * LinesPerThread; PixelY < EndY; ++PixelY)
		{
			const int32 SrcYOffset = PixelY * Color16Pitch;
			const int32 DstYOffset = PixelY * OutputSize.X;

			for (int32 PixelX = 0; PixelX < OutputSize.X; PixelX++)
			{
				const FFloat16Color& Pixel16 = Color16[PixelX + SrcYOffset];
				FColor& Pixel8 = OutputColor[PixelX + DstYOffset];

				if (Pixel16 == MagentaFloat16)
				{
					Pixel8.R = 255;
					Pixel8.G = 0;
					Pixel8.B = 255;
				}
				else
				{
					Pixel8.R = (uint8)FMath::RoundToInt(Pixel16.R.GetFloat() * Scale);
					Pixel8.G = (uint8)FMath::RoundToInt(Pixel16.G.GetFloat() * Scale);
					Pixel8.B = (uint8)FMath::RoundToInt(Pixel16.B.GetFloat() * Scale);
				}
					
				Pixel8.A = 255;
			}
		}
	});

	// This scale will be used in the proxy material to get the original range of emissive values outside of 0-1
	EmissiveScale = GlobalMaxValue;
}

void FMaterialBakingModule::OnObjectModified(UObject* Object)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FMaterialBakingModule::OnObjectModified)

	if (CVarUseMaterialProxyCaching.GetValueOnAnyThread())
	{
		UMaterialInterface* MaterialToInvalidate = Cast<UMaterialInterface>(Object);
		if (!MaterialToInvalidate)
		{
			// Check to see if the object is a material editor instance constant and if so, retrieve its source instance
			UMaterialEditorInstanceConstant* EditorInstance = Cast<UMaterialEditorInstanceConstant>(Object);
			if (EditorInstance && EditorInstance->SourceInstance)
			{
				MaterialToInvalidate = EditorInstance->SourceInstance;
			}
		}

		if (MaterialToInvalidate)
		{
			// Search our proxy pool for materials or material instances that refer to MaterialToInvalidate
			for (auto It = MaterialProxyPool.CreateIterator(); It; ++It)
			{
				TWeakObjectPtr<UMaterialInterface> PoolMaterialPtr = It.Key();

				// Remove stale entries from the pool
				bool bMustDelete = PoolMaterialPtr.IsValid();
				if (!bMustDelete)
				{
					bMustDelete = PoolMaterialPtr == MaterialToInvalidate;
				}

				// No match - Test the MaterialInstance hierarchy
				if (!bMustDelete)
				{
					UMaterialInstance* MaterialInstance = Cast<UMaterialInstance>(PoolMaterialPtr);
					while (!bMustDelete && MaterialInstance && MaterialInstance->Parent != nullptr)
					{
						bMustDelete = MaterialInstance->Parent == MaterialToInvalidate;
						MaterialInstance = Cast<UMaterialInstance>(MaterialInstance->Parent);
					}
				}

				// We have a match, remove the entry from our pool
				if (bMustDelete)
				{
					FExportMaterialProxy* Proxy = It.Value().Value;

					ENQUEUE_RENDER_COMMAND(DeleteCachedMaterialProxy)(
						[Proxy](FRHICommandListImmediate& RHICmdList)
						{
							delete Proxy;
						});

					It.RemoveCurrent();
				}
			}
		}
	}
}

#undef LOCTEXT_NAMESPACE //"MaterialBakingModule"
