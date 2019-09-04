// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "PaperRenderSceneProxy.h"
#include "Containers/ResourceArray.h"
#include "SceneManagement.h"
#include "Materials/Material.h"
#include "PhysicsEngine/BodySetup.h"
#include "EngineGlobals.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/Engine.h"
#include "DynamicMeshBuilder.h"

static TAutoConsoleVariable<int32> CVarDrawSpritesAsTwoSided(TEXT("r.Paper2D.DrawTwoSided"), 1, TEXT("Draw sprites as two sided."));

DECLARE_CYCLE_STAT(TEXT("Get Batch Mesh"), STAT_PaperRender_GetBatchMesh, STATGROUP_Paper2D);
DECLARE_CYCLE_STAT(TEXT("Get New Batch Meshes"), STAT_PaperRender_GetNewBatchMeshes, STATGROUP_Paper2D);
DECLARE_CYCLE_STAT(TEXT("Convert Batches"), STAT_PaperRender_ConvertBatches, STATGROUP_Paper2D);
DECLARE_CYCLE_STAT(TEXT("SpriteProxy GDME"), STAT_PaperRenderSceneProxy_GetDynamicMeshElements, STATGROUP_Paper2D);

FPackedNormal FPaperSpriteTangents::PackedNormalX(FVector(1.0f, 0.0f, 0.0f));
FPackedNormal FPaperSpriteTangents::PackedNormalZ(FVector(0.0f, -1.0f, 0.0f));

void FPaperSpriteTangents::SetTangentsFromPaperAxes()
{
	PackedNormalX = PaperAxisX;
	PackedNormalZ = -PaperAxisZ;
	// store determinant of basis in w component of normal vector
	PackedNormalZ.Vector.W = GetBasisDeterminantSignByte(PaperAxisX, PaperAxisY, PaperAxisZ);
}

//////////////////////////////////////////////////////////////////////////
// FSpriteTextureOverrideRenderProxy

/**
 * A material render proxy which overrides various named texture parameters.
 */
class FSpriteTextureOverrideRenderProxy : public FDynamicPrimitiveResource, public FMaterialRenderProxy
{
public:
	const FMaterialRenderProxy* const Parent;
	const UTexture* BaseTexture;
	FAdditionalSpriteTextureArray AdditionalTextures;
	UE_EXPAND_IF_WITH_EDITOR(const FPaperRenderSceneProxyTextureOverrideMap& TextureOverrideList);

public:
	/** Initialization constructor. */
	FSpriteTextureOverrideRenderProxy(const FMaterialRenderProxy* InParent, const UTexture* InBaseTexture, FAdditionalSpriteTextureArray InAdditionalTextures UE_EXPAND_IF_WITH_EDITOR(, const FPaperRenderSceneProxyTextureOverrideMap& InTextureOverrideList))
		: Parent(InParent)
		, BaseTexture(InBaseTexture)
		, AdditionalTextures(InAdditionalTextures)
		UE_EXPAND_IF_WITH_EDITOR(, TextureOverrideList(InTextureOverrideList))
	{}

	virtual ~FSpriteTextureOverrideRenderProxy()
	{
	}

	// FDynamicPrimitiveResource interface.
	virtual void InitPrimitiveResource() override
	{
	}
	virtual void ReleasePrimitiveResource() override
	{
		delete this;
	}

	// FMaterialRenderProxy interface.
	virtual const FMaterial& GetMaterialWithFallback(ERHIFeatureLevel::Type InFeatureLevel, const FMaterialRenderProxy*& OutFallbackMaterialRenderProxy) const override
	{
		return Parent->GetMaterialWithFallback(InFeatureLevel, OutFallbackMaterialRenderProxy);
	}

	virtual bool GetVectorValue(const FMaterialParameterInfo& ParameterInfo, FLinearColor* OutValue, const FMaterialRenderContext& Context) const override
	{
		return Parent->GetVectorValue(ParameterInfo, OutValue, Context);
	}

	virtual bool GetScalarValue(const FMaterialParameterInfo& ParameterInfo, float* OutValue, const FMaterialRenderContext& Context) const override
	{
		return Parent->GetScalarValue(ParameterInfo, OutValue, Context);
	}

	virtual bool GetTextureValue(const FMaterialParameterInfo& ParameterInfo, const UTexture** OutValue, const FMaterialRenderContext& Context) const override
	{
		if (ParameterInfo.Name == TextureParameterName)
		{
			*OutValue = ApplyEditorOverrides(BaseTexture);
			return true;
		}
		else if (ParameterInfo.Name.GetComparisonIndex() == AdditionalTextureParameterRootName.GetComparisonIndex())
		{
			const int32 AdditionalSlotIndex = ParameterInfo.Name.GetNumber() - 1;
			if (AdditionalTextures.IsValidIndex(AdditionalSlotIndex))
			{
				*OutValue = ApplyEditorOverrides(AdditionalTextures[AdditionalSlotIndex]);
				return true;
			}
		}
		
		return Parent->GetTextureValue(ParameterInfo, OutValue, Context);
	}

#if WITH_EDITOR
	inline const UTexture* ApplyEditorOverrides(const UTexture* InTexture) const
	{
		if (TextureOverrideList.Num() > 0)
		{
			if (const UTexture* const* OverridePtr = TextureOverrideList.Find(InTexture))
			{
				return *OverridePtr;
			}
		}

		return InTexture;
	}
#else
	FORCEINLINE const UTexture* ApplyEditorOverrides(const UTexture* InTexture) const
	{
		return InTexture;
	}
#endif

	static const FName TextureParameterName;
	static const FName AdditionalTextureParameterRootName;
};

const FName FSpriteTextureOverrideRenderProxy::TextureParameterName(TEXT("SpriteTexture"));
const FName FSpriteTextureOverrideRenderProxy::AdditionalTextureParameterRootName(TEXT("SpriteAdditionalTexture"));

//////////////////////////////////////////////////////////////////////////
// FPaperRenderSceneProxy

FPaperRenderSceneProxy::FPaperRenderSceneProxy(const UPrimitiveComponent* InComponent)
	: FPrimitiveSceneProxy(InComponent)
	, Material(nullptr)
	, Owner(InComponent->GetOwner())
	, MyBodySetup(const_cast<UPrimitiveComponent*>(InComponent)->GetBodySetup())
	, bCastShadow(InComponent->CastShadow)
	, CollisionResponse(InComponent->GetCollisionResponseToChannels())
{
	SetWireframeColor(FLinearColor::White);

	bDrawTwoSided = CVarDrawSpritesAsTwoSided.GetValueOnGameThread() != 0;
}

SIZE_T FPaperRenderSceneProxy::GetTypeHash() const
{
	static size_t UniquePointer;
	return reinterpret_cast<size_t>(&UniquePointer);
}

FPaperRenderSceneProxy::~FPaperRenderSceneProxy()
{
}

void FPaperRenderSceneProxy::DebugDrawBodySetup(const FSceneView* View, int32 ViewIndex, FMeshElementCollector& Collector, UBodySetup* BodySetup, const FMatrix& GeomTransformMatrix, const FLinearColor& CollisionColor, bool bDrawSolid) const
{
	if (FMath::Abs(GeomTransformMatrix.Determinant()) < SMALL_NUMBER)
	{
		// Catch this here or otherwise GeomTransform below will assert
		// This spams so commented out
		//UE_LOG(LogStaticMesh, Log, TEXT("Zero scaling not supported (%s)"), *StaticMesh->GetPathName());
	}
	else
	{
		FTransform GeomTransform(GeomTransformMatrix);

		if (bDrawSolid)
		{
			// Make a material for drawing solid collision stuff
			auto SolidMaterialInstance = new FColoredMaterialRenderProxy(
				GEngine->ShadedLevelColorationUnlitMaterial->GetRenderProxy(),
				GetWireframeColor()
				);

			Collector.RegisterOneFrameMaterialProxy(SolidMaterialInstance);

			BodySetup->AggGeom.GetAggGeom(GeomTransform, GetWireframeColor().ToFColor(true), SolidMaterialInstance, false, true, DrawsVelocity(), ViewIndex, Collector);
		}
		else
		{
			// wireframe
			BodySetup->AggGeom.GetAggGeom(GeomTransform, GetSelectionColor(CollisionColor, IsSelected(), IsHovered()).ToFColor(true), nullptr, ( Owner == nullptr ), false, DrawsVelocity(), ViewIndex, Collector);
		}
	}
}

void FPaperRenderSceneProxy::DebugDrawCollision(const FSceneView* View, int32 ViewIndex, FMeshElementCollector& Collector, bool bDrawSolid) const
{
	if (MyBodySetup != nullptr)
	{
		const FColor CollisionColor = FColor(157, 149, 223, 255);
		DebugDrawBodySetup(View, ViewIndex, Collector, MyBodySetup, GetLocalToWorld(), CollisionColor, bDrawSolid);
	}
}

void FPaperRenderSceneProxy::GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily, uint32 VisibilityMap, FMeshElementCollector& Collector) const
{
	SCOPE_CYCLE_COUNTER(STAT_PaperRenderSceneProxy_GetDynamicMeshElements);
	checkSlow(IsInRenderingThread());

	const FEngineShowFlags& EngineShowFlags = ViewFamily.EngineShowFlags;

	bool bDrawSimpleCollision = false;
	bool bDrawComplexCollision = false;
	const bool bInCollisionView = IsCollisionView(EngineShowFlags, bDrawSimpleCollision, bDrawComplexCollision);
	
	// Sprites don't distinguish between simple and complex collision; when viewing visibility we should still render simple collision
	bDrawSimpleCollision |= bDrawComplexCollision;

	// Draw simple collision as wireframe if 'show collision', collision is enabled
	const bool bDrawWireframeCollision = EngineShowFlags.Collision && IsCollisionEnabled();

	const bool bDrawSprite = !bInCollisionView;

	if (bDrawSprite)
	{
		for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
		{
			if (VisibilityMap & (1 << ViewIndex))
			{
				const FSceneView* View = Views[ViewIndex];

				GetDynamicMeshElementsForView(View, ViewIndex, Collector);
			}
		}
	}


	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
	{
		if (VisibilityMap & (1 << ViewIndex))
		{
			if ((bDrawSimpleCollision || bDrawWireframeCollision) && AllowDebugViewmodes())
			{
				const FSceneView* View = Views[ViewIndex];
				const bool bDrawSolid = !bDrawWireframeCollision;
				DebugDrawCollision(View, ViewIndex, Collector, bDrawSolid);
			}

			// Draw bounds
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
			if (EngineShowFlags.Paper2DSprites)
			{
				RenderBounds(Collector.GetPDI(ViewIndex), EngineShowFlags, GetBounds(), (Owner == nullptr) || IsSelected());
			}
#endif
		}
	}
}

void FPaperRenderSceneProxy::GetDynamicMeshElementsForView(const FSceneView* View, int32 ViewIndex, FMeshElementCollector& Collector) const
{
	if (Material != nullptr)
	{
		GetBatchMesh(View, Material, BatchedSprites, ViewIndex, Collector);
	}
	GetNewBatchMeshes(View, ViewIndex, Collector);
}

void FPaperRenderSceneProxy::GetNewBatchMeshes(const FSceneView* View, int32 ViewIndex, FMeshElementCollector& Collector) const
{
	if (BatchedSections.Num() == 0)
	{
		return;
	}

	SCOPE_CYCLE_COUNTER(STAT_PaperRender_GetNewBatchMeshes);

	const uint8 DPG = GetDepthPriorityGroup(View);
	const bool bIsWireframeView = View->Family->EngineShowFlags.Wireframe;

	if (Vertices.Num())
	{
		for (const FSpriteRenderSection& Batch : BatchedSections)
		{
			FDynamicMeshBuilder DynamicMeshBuilder(View->GetFeatureLevel());

			for (int32 i = Batch.VertexOffset; i < Batch.VertexOffset + Batch.NumVertices; ++i)
			{
				DynamicMeshBuilder.AddVertex(Vertices[i]);
			}

			if (Batch.IsValid())
			{
				FMaterialRenderProxy* ParentMaterialProxy = Batch.Material->GetRenderProxy();

				FDynamicMeshBuilderSettings Settings;
				Settings.bCanApplyViewModeOverrides = true;
				Settings.bUseWireframeSelectionColoring = IsSelected();

				// Implementing our own wireframe coloring as the automatic one (controlled by Mesh.bCanApplyViewModeOverrides) only supports per-FPrimitiveSceneProxy WireframeColor
				if (bIsWireframeView)
				{
					const FLinearColor EffectiveWireframeColor = (Batch.Material->GetBlendMode() != BLEND_Opaque) ? GetWireframeColor() : FLinearColor::Green;

					auto WireframeMaterialInstance = new FColoredMaterialRenderProxy(
						GEngine->WireframeMaterial->GetRenderProxy(),
						GetSelectionColor(EffectiveWireframeColor, IsSelected(), IsHovered(), false)
					);

					Collector.RegisterOneFrameMaterialProxy(WireframeMaterialInstance);

					ParentMaterialProxy = WireframeMaterialInstance;

					Settings.bWireframe = true;
					// We are applying our own wireframe override
					Settings.bCanApplyViewModeOverrides = false;
				}

				// Create a texture override material proxy and register it as a dynamic resource so that it won't be deleted until the rendering thread has finished with it
				FSpriteTextureOverrideRenderProxy* TextureOverrideMaterialProxy = new FSpriteTextureOverrideRenderProxy(ParentMaterialProxy, Batch.BaseTexture, Batch.AdditionalTextures UE_EXPAND_IF_WITH_EDITOR(, TextureOverrideList));
				Collector.RegisterOneFrameMaterialProxy(TextureOverrideMaterialProxy);

				Settings.CastShadow = bCastShadow;
				Settings.bDisableBackfaceCulling = bDrawTwoSided;


				DynamicMeshBuilder.GetMesh(GetLocalToWorld(), TextureOverrideMaterialProxy, DPG, Settings, nullptr, ViewIndex, Collector);
			}
		}
	}
}

class FPaperVertexArray : public FOneFrameResource
{
public:
	TArray<FDynamicMeshVertex> Vertices;
};

void FPaperRenderSceneProxy::GetBatchMesh(const FSceneView* View, class UMaterialInterface* BatchMaterial, const TArray<FSpriteDrawCallRecord>& Batch, int32 ViewIndex, FMeshElementCollector& Collector) const
{
	SCOPE_CYCLE_COUNTER(STAT_PaperRender_GetBatchMesh);

	const uint8 DPG = GetDepthPriorityGroup(View);

	const bool bIsWireframeView = View->Family->EngineShowFlags.Wireframe;

	FMatrix PreviousLocalToWorld;

	if (!GetScene().GetPreviousLocalToWorld(GetPrimitiveSceneInfo(), PreviousLocalToWorld))
	{
		PreviousLocalToWorld = GetLocalToWorld();
	}

	for (const FSpriteDrawCallRecord& Record : Batch)
	{
		if (Record.IsValid())
		{
			const FColor SpriteColor(Record.Color);
			const FVector EffectiveOrigin = Record.Destination;

			FPaperVertexArray& VertexArray = Collector.AllocateOneFrameResource<FPaperVertexArray>();
			VertexArray.Vertices.Empty(Record.RenderVerts.Num());

			for (int32 SVI = 0; SVI < Record.RenderVerts.Num(); ++SVI)
			{
				const FVector4& SourceVert = Record.RenderVerts[SVI];
				const FVector Pos((PaperAxisX * SourceVert.X) + (PaperAxisY * SourceVert.Y) + EffectiveOrigin);
				const FVector2D UV(SourceVert.Z, SourceVert.W);

				new (VertexArray.Vertices) FDynamicMeshVertex(Pos, FPaperSpriteTangents::PackedNormalX.ToFVector(), FPaperSpriteTangents::PackedNormalZ.ToFVector(), UV, SpriteColor);
			}

			FDynamicMeshBuilder DynamicMeshBuilder(View->GetFeatureLevel());
			DynamicMeshBuilder.AddVertices(VertexArray.Vertices);

			FDynamicMeshBuilderSettings Settings;
			Settings.bCanApplyViewModeOverrides = true;
			Settings.bUseWireframeSelectionColoring = IsSelected();

			FMaterialRenderProxy* ParentMaterialProxy = BatchMaterial->GetRenderProxy();

			// Implementing our own wireframe coloring as the automatic one (controlled by Mesh.bCanApplyViewModeOverrides) only supports per-FPrimitiveSceneProxy WireframeColor
			if (bIsWireframeView)
			{
				const FLinearColor EffectiveWireframeColor = (BatchMaterial->GetBlendMode() != BLEND_Opaque) ? GetWireframeColor() : FLinearColor::Green;

				auto WireframeMaterialInstance = new FColoredMaterialRenderProxy(
					GEngine->WireframeMaterial->GetRenderProxy(),
					GetSelectionColor(EffectiveWireframeColor, IsSelected(), IsHovered(), false)
					);

				Collector.RegisterOneFrameMaterialProxy(WireframeMaterialInstance);

				ParentMaterialProxy = WireframeMaterialInstance;

				Settings.bWireframe = true;
				// We are applying our own wireframe override
				Settings.bCanApplyViewModeOverrides = false;
			}

			// Create a texture override material proxy and register it as a dynamic resource so that it won't be deleted until the rendering thread has finished with it
			FSpriteTextureOverrideRenderProxy* TextureOverrideMaterialProxy = new FSpriteTextureOverrideRenderProxy(ParentMaterialProxy, Record.BaseTexture, Record.AdditionalTextures UE_EXPAND_IF_WITH_EDITOR(, TextureOverrideList));
			Collector.RegisterOneFrameMaterialProxy(TextureOverrideMaterialProxy);
		
			Settings.bDisableBackfaceCulling = bDrawTwoSided;
			Settings.bReceivesDecals = true;
			Settings.bUseSelectionOutline = true;
			Settings.CastShadow = bCastShadow;
			DynamicMeshBuilder.GetMesh(GetLocalToWorld(), PreviousLocalToWorld, TextureOverrideMaterialProxy, DPG, Settings, nullptr, ViewIndex, Collector);
		}
	}
}

FPrimitiveViewRelevance FPaperRenderSceneProxy::GetViewRelevance(const FSceneView* View) const
{
	const FEngineShowFlags& EngineShowFlags = View->Family->EngineShowFlags;

	checkSlow(IsInParallelRenderingThread());

	FPrimitiveViewRelevance Result;
	Result.bDrawRelevance = IsShown(View) && EngineShowFlags.Paper2DSprites;
	Result.bRenderCustomDepth = ShouldRenderCustomDepth();
	Result.bRenderInMainPass = ShouldRenderInMainPass();
	Result.bUsesLightingChannels = GetLightingChannelMask() != GetDefaultLightingChannelMask();
	Result.bShadowRelevance = IsShadowCast(View);
	Result.bTranslucentSelfShadow = bCastVolumetricTranslucentShadow;

	MaterialRelevance.SetPrimitiveViewRelevance(Result);

#undef SUPPORT_EXTRA_RENDERING
#define SUPPORT_EXTRA_RENDERING !(UE_BUILD_SHIPPING || UE_BUILD_TEST) || WITH_EDITOR
	

#if SUPPORT_EXTRA_RENDERING
	bool bDrawSimpleCollision = false;
	bool bDrawComplexCollision = false;
	const bool bInCollisionView = IsCollisionView(EngineShowFlags, bDrawSimpleCollision, bDrawComplexCollision);
#endif

	Result.bDynamicRelevance = true;

	if (!EngineShowFlags.Materials
#if SUPPORT_EXTRA_RENDERING
		|| bInCollisionView
#endif
		)
	{
		Result.bOpaqueRelevance = true;
	}

	Result.bVelocityRelevance = IsMovable() && Result.bOpaqueRelevance && Result.bRenderInMainPass;

	return Result;
}

uint32 FPaperRenderSceneProxy::GetMemoryFootprint() const
{
	return sizeof(*this) + GetAllocatedSize();
}

bool FPaperRenderSceneProxy::CanBeOccluded() const
{
	return !MaterialRelevance.bDisableDepthTest;
}

bool FPaperRenderSceneProxy::IsUsingDistanceCullFade() const
{
	return MaterialRelevance.bUsesDistanceCullFade;
}

void FPaperRenderSceneProxy::SetDrawCall_RenderThread(const FSpriteDrawCallRecord& NewDynamicData)
{
	SCOPE_CYCLE_COUNTER(STAT_PaperRender_SetSpriteRT);

	BatchedSprites.Empty();

	FSpriteDrawCallRecord& Record = *new (BatchedSprites) FSpriteDrawCallRecord;
	Record = NewDynamicData;
}

void FPaperRenderSceneProxy::SetBodySetup_RenderThread(UBodySetup* NewSetup)
{
	MyBodySetup = NewSetup;
}

bool FPaperRenderSceneProxy::IsCollisionView(const FEngineShowFlags& EngineShowFlags, bool& bDrawSimpleCollision, bool& bDrawComplexCollision) const
{
	bDrawSimpleCollision = false;
	bDrawComplexCollision = false;

	// If in a 'collision view' and collision is enabled
	const bool bInCollisionView = EngineShowFlags.CollisionVisibility || EngineShowFlags.CollisionPawn;
	if (bInCollisionView && IsCollisionEnabled())
	{
		// See if we have a response to the interested channel
		bool bHasResponse = EngineShowFlags.CollisionPawn && (CollisionResponse.GetResponse(ECC_Pawn) != ECR_Ignore);
		bHasResponse |= EngineShowFlags.CollisionVisibility && (CollisionResponse.GetResponse(ECC_Visibility) != ECR_Ignore);

		if (bHasResponse)
		{
			bDrawComplexCollision = EngineShowFlags.CollisionVisibility;
			bDrawSimpleCollision = EngineShowFlags.CollisionPawn;
		}
	}

	return bInCollisionView;
}

#if WITH_EDITOR
void FPaperRenderSceneProxy::SetTransientTextureOverride_RenderThread(const UTexture* InTextureToModifyOverrideFor, UTexture* InOverrideTexture)
{
	if (InOverrideTexture != nullptr)
	{
		TextureOverrideList.FindOrAdd(InTextureToModifyOverrideFor) = InOverrideTexture;
	}
	else
	{
		TextureOverrideList.Remove(InTextureToModifyOverrideFor);
	}
}
#endif

void FPaperRenderSceneProxy::ConvertBatchesToNewStyle(TArray<FSpriteDrawCallRecord>& SourceBatches)
{
	SCOPE_CYCLE_COUNTER(STAT_PaperRender_ConvertBatches);

	Vertices.Reset();
	BatchedSections.Reset();

	for (const FSpriteDrawCallRecord& SourceBatch : SourceBatches)
	{
		if (SourceBatch.IsValid())
		{
			FSpriteRenderSection& DestBatch = *new (BatchedSections) FSpriteRenderSection();

			DestBatch.BaseTexture = SourceBatch.BaseTexture;
			DestBatch.AdditionalTextures = SourceBatch.AdditionalTextures;
			DestBatch.Material = Material;

			DestBatch.AddTriangles(SourceBatch, Vertices);
		}
	}
}
