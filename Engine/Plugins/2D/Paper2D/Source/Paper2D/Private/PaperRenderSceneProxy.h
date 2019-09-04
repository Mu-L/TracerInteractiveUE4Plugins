// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "RenderResource.h"
#include "SpriteDrawCall.h"
#include "Materials/MaterialInterface.h"
#include "PackedNormal.h"
#include "PrimitiveViewRelevance.h"
#include "PrimitiveSceneProxy.h"
#include "LocalVertexFactory.h"
#include "Paper2DModule.h"
#include "DynamicMeshBuilder.h"

class FMeshElementCollector;
class UBodySetup;
class UPrimitiveComponent;

#if WITH_EDITOR
typedef TMap<const UTexture*, const UTexture*> FPaperRenderSceneProxyTextureOverrideMap;

#define UE_EXPAND_IF_WITH_EDITOR(...) __VA_ARGS__
#else
#define UE_EXPAND_IF_WITH_EDITOR(...)
#endif

/** A Paper2D sprite vertex. */
struct PAPER2D_API FPaperSpriteTangents
{
	static void SetTangentsFromPaperAxes();

	static FPackedNormal PackedNormalX;
	static FPackedNormal PackedNormalZ;
};

//////////////////////////////////////////////////////////////////////////
// FSpriteRenderSection

struct PAPER2D_API FSpriteRenderSection
{
	UMaterialInterface* Material;
	UTexture* BaseTexture;
	FAdditionalSpriteTextureArray AdditionalTextures;

	int32 VertexOffset;
	int32 NumVertices;

	FSpriteRenderSection()
		: Material(nullptr)
		, BaseTexture(nullptr)
		, VertexOffset(INDEX_NONE)
		, NumVertices(0)
	{
	}

	class FTexture* GetBaseTextureResource() const
	{
		return (BaseTexture != nullptr) ? BaseTexture->Resource : nullptr;
	}

	bool IsValid() const
	{
		return (Material != nullptr) && (NumVertices > 0) && (GetBaseTextureResource() != nullptr);
	}

	template <typename SourceArrayType>
	void AddTriangles(const FSpriteDrawCallRecord& Record, SourceArrayType& Vertices)
	{
		if (NumVertices == 0)
		{
			VertexOffset = Vertices.Num();
			BaseTexture = Record.BaseTexture;
			AdditionalTextures = Record.AdditionalTextures;
		}
		else
		{
			checkSlow((VertexOffset + NumVertices) == Vertices.Num());
			checkSlow(BaseTexture == Record.BaseTexture);
			// Note: Not checking AdditionalTextures for now, since checking BaseTexture should catch most bugs
		}

		const int32 NumNewVerts = Record.RenderVerts.Num();
		NumVertices += NumNewVerts;
		Vertices.Reserve(Vertices.Num() + NumNewVerts);

		const FColor VertColor(Record.Color);
		for (const FVector4& SourceVert : Record.RenderVerts)
		{
			const FVector Pos((PaperAxisX * SourceVert.X) + (PaperAxisY * SourceVert.Y) + Record.Destination);
			const FVector2D UV(SourceVert.Z, SourceVert.W);

			new (Vertices) FDynamicMeshVertex(Pos, UV, VertColor);
		}
	}

	template <typename SourceArrayType>
	inline void AddVertex(float X, float Y, float U, float V, const FVector& Origin, const FColor& Color, SourceArrayType& Vertices)
	{
		const FVector Pos((PaperAxisX * X) + (PaperAxisY * Y) + Origin);

		new (Vertices) FDynamicMeshVertex(Pos, FVector2D(U, V), Color);
		++NumVertices;
	}

	template <typename SourceArrayType>
	inline void AddVertex(float X, float Y, float U, float V, const FVector& Origin, const FColor& Color, const FPackedNormal& TangentX, const FPackedNormal& TangentZ, SourceArrayType& Vertices)
	{
		const FVector Pos((PaperAxisX * X) + (PaperAxisY * Y) + Origin);

		new (Vertices) FDynamicMeshVertex(Pos, TangentX.ToFVector(), TangentZ.ToFVector(), FVector2D(U, V), Color);
		++NumVertices;
	}
};

//////////////////////////////////////////////////////////////////////////
// FPaperRenderSceneProxy

class PAPER2D_API FPaperRenderSceneProxy : public FPrimitiveSceneProxy
{
public:
	SIZE_T GetTypeHash() const override;

	FPaperRenderSceneProxy(const UPrimitiveComponent* InComponent);
	virtual ~FPaperRenderSceneProxy();

	// FPrimitiveSceneProxy interface.
	virtual void GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily, uint32 VisibilityMap, FMeshElementCollector& Collector) const override;
	virtual FPrimitiveViewRelevance GetViewRelevance(const FSceneView* View) const override;
	virtual uint32 GetMemoryFootprint() const override;
	virtual bool CanBeOccluded() const override;
	virtual bool IsUsingDistanceCullFade() const override;
	// End of FPrimitiveSceneProxy interface.

	void SetDrawCall_RenderThread(const FSpriteDrawCallRecord& NewDynamicData);
	void SetBodySetup_RenderThread(UBodySetup* NewSetup);

#if WITH_EDITOR
	void SetTransientTextureOverride_RenderThread(const UTexture* InTextureToModifyOverrideFor, UTexture* InOverrideTexture);
#endif

protected:
	virtual void GetDynamicMeshElementsForView(const FSceneView* View, int32 ViewIndex, FMeshElementCollector& Collector) const;

	void GetBatchMesh(const FSceneView* View, UMaterialInterface* BatchMaterial, const TArray<FSpriteDrawCallRecord>& Batch, int32 ViewIndex, FMeshElementCollector& Collector) const;
	void GetNewBatchMeshes(const FSceneView* View, int32 ViewIndex, FMeshElementCollector& Collector) const;

	bool IsCollisionView(const FEngineShowFlags& EngineShowFlags, bool& bDrawSimpleCollision, bool& bDrawComplexCollision) const;

	void ConvertBatchesToNewStyle(TArray<FSpriteDrawCallRecord>& SourceBatches);

	virtual void DebugDrawCollision(const FSceneView* View, int32 ViewIndex, FMeshElementCollector& Collector, bool bDrawSolid) const;
	virtual void DebugDrawBodySetup(const FSceneView* View, int32 ViewIndex, FMeshElementCollector& Collector, UBodySetup* BodySetup, const FMatrix& GeomTransform, const FLinearColor& CollisionColor, bool bDrawSolid) const;
protected:
	TArray<FSpriteRenderSection> BatchedSections;
	TArray<FDynamicMeshVertex> Vertices;

	// Old style
	TArray<FSpriteDrawCallRecord> BatchedSprites;
	class UMaterialInterface* Material;

	//
	AActor* Owner;
	UBodySetup* MyBodySetup;

	bool bDrawTwoSided;
	bool bCastShadow;

	// The view relevance for the associated material
	FMaterialRelevance MaterialRelevance;

	// The Collision Response of the component being proxied
	FCollisionResponseContainer CollisionResponse;

	// The texture override list
	UE_EXPAND_IF_WITH_EDITOR(FPaperRenderSceneProxyTextureOverrideMap TextureOverrideList);
};
