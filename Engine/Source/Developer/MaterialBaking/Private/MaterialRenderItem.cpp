// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#include "MaterialRenderItem.h"
#include "MaterialBakingStructures.h"

#include "EngineModule.h"
#include "RawMesh.h"
#include "DrawingPolicy.h"
#include "DynamicMeshBuilder.h"

#define SHOW_WIREFRAME_MESH 0

FMeshMaterialRenderItem::FMeshMaterialRenderItem(const FMaterialData* InMaterialSettings, const FMeshData* InMeshSettings, EMaterialProperty InMaterialProperty)
	: MeshSettings(InMeshSettings), MaterialSettings(InMaterialSettings), MaterialProperty(InMaterialProperty), MaterialRenderProxy(nullptr), ViewFamily(nullptr)
{
	GenerateRenderData();
	LCI = new FMeshRenderInfo(InMeshSettings->LightMap, nullptr, nullptr);
}

bool FMeshMaterialRenderItem::Render_RenderThread(FRHICommandListImmediate& RHICmdList, FDrawingPolicyRenderState& DrawRenderState, const FCanvas* Canvas)
{
	return false;
}

bool FMeshMaterialRenderItem::Render_GameThread(const FCanvas* Canvas)
{
	checkSlow(ViewFamily && MaterialSettings && MeshSettings && MaterialRenderProxy);
	// current render target set for the canvas
	const FRenderTarget* CanvasRenderTarget = Canvas->GetRenderTarget();
	const FIntRect ViewRect(FIntPoint(0, 0), CanvasRenderTarget->GetSizeXY());

	// make a temporary view
	FSceneViewInitOptions ViewInitOptions;
	ViewInitOptions.ViewFamily = ViewFamily;
	ViewInitOptions.SetViewRectangle(ViewRect);
	ViewInitOptions.ViewOrigin = FVector::ZeroVector;
	ViewInitOptions.ViewRotationMatrix = FMatrix::Identity;
	ViewInitOptions.ProjectionMatrix = Canvas->GetTransformStack().Top().GetMatrix();
	ViewInitOptions.BackgroundColor = FLinearColor::Black;
	ViewInitOptions.OverlayColor = FLinearColor::White;

	FSceneView* View = new FSceneView(ViewInitOptions);
	View->FinalPostProcessSettings.bOverride_IndirectLightingIntensity = 1;
	View->FinalPostProcessSettings.IndirectLightingIntensity = 0.0f;


	const bool bNeedsToSwitchVerticalAxis = RHINeedsToSwitchVerticalAxis(Canvas->GetShaderPlatform()) && !Canvas->GetAllowSwitchVerticalAxis();
	check(bNeedsToSwitchVerticalAxis == false);

	struct FDrawMaterialParameters
	{
		const FSceneView* View;
		FMeshMaterialRenderItem* RenderItem;
		uint32 AllowedCanvasModes;
	};

	const FDrawMaterialParameters Parameters
	{
		View,
		this,
		Canvas->GetAllowedModes()
	};

	if (Vertices.Num() && Indices.Num())
	{
		ENQUEUE_RENDER_COMMAND(DrawMaterialCommand)(
			[Parameters](FRHICommandListImmediate& RHICmdList)
		{
			FDrawingPolicyRenderState DrawRenderState(*Parameters.View);

			// disable depth test & writes
			DrawRenderState.SetBlendState(TStaticBlendState<CW_RGBA>::GetRHI());
			DrawRenderState.SetDepthStencilState(TStaticDepthStencilState<false, CF_Always>::GetRHI());

			Parameters.RenderItem->QueueMaterial(RHICmdList, DrawRenderState, Parameters.View);

			delete Parameters.View;
		});

	}
	
	return true;	
}

void FMeshMaterialRenderItem::GenerateRenderData()
{
	// Reset array without resizing
	Vertices.SetNum(0, false);
	Indices.SetNum(0, false);
	if (MeshSettings->RawMesh)
	{
		// Use supplied FRawMesh data to populate render data
		PopulateWithMeshData();
	}
	else
	{
		// Use simple rectangle
		PopulateWithQuadData();
	}
}

void FMeshMaterialRenderItem::QueueMaterial(FRHICommandListImmediate& RHICmdList, FDrawingPolicyRenderState& DrawRenderState, const FSceneView* View)
{
	FDynamicMeshBuilder DynamicMeshBuilder(View->GetFeatureLevel(), MAX_STATIC_TEXCOORDS, MeshSettings->LightMapIndex);
	DynamicMeshBuilder.AddVertices(Vertices);
	DynamicMeshBuilder.AddTriangles(Indices);

	FMeshBatch MeshElement;
	FMeshBuilderOneFrameResources OneFrameResource;
	DynamicMeshBuilder.GetMeshElement(FMatrix::Identity, MaterialRenderProxy, SDPG_Foreground, true, false, 0, OneFrameResource, MeshElement);

	check(OneFrameResource.IsValidForRendering());

	LCI->SetPrecomputedLightingBuffer(LightMapHelpers::CreateDummyPrecomputedLightingUniformBuffer(UniformBuffer_SingleFrame, GMaxRHIFeatureLevel, LCI));
	MeshElement.LCI = LCI;

#if SHOW_WIREFRAME_MESH
	MeshElement.bWireframe = true;
#endif

	const int32 NumTris = FMath::TruncToInt(Indices.Num() / 3);
	if (NumTris == 0)
	{
		// there's nothing to do here
		return;
	}

	// Bake the material out to a tile
	GetRendererModule().DrawTileMesh(RHICmdList, DrawRenderState, *View, MeshElement, false /*bIsHitTesting*/, FHitProxyId());
}

void FMeshMaterialRenderItem::PopulateWithQuadData()
{
	Vertices.Empty(4);
	Indices.Empty(6);

	const float U = MeshSettings->TextureCoordinateBox.Min.X;
	const float V = MeshSettings->TextureCoordinateBox.Min.Y;
	const float SizeU = MeshSettings->TextureCoordinateBox.Max.X - MeshSettings->TextureCoordinateBox.Min.X;
	const float SizeV = MeshSettings->TextureCoordinateBox.Max.Y - MeshSettings->TextureCoordinateBox.Min.Y;
	const FIntPoint& PropertySize = MaterialSettings->PropertySizes[MaterialProperty];
	const float ScaleX = PropertySize.X;
	const float ScaleY = PropertySize.Y;

	// add vertices
	for (int32 VertIndex = 0; VertIndex < 4; VertIndex++)
	{
		FDynamicMeshVertex* Vert = new(Vertices)FDynamicMeshVertex();
		const int32 X = VertIndex & 1;
		const int32 Y = (VertIndex >> 1) & 1;
		Vert->Position.Set(ScaleX * X, ScaleY * Y, 0);
		Vert->SetTangents(FVector(1, 0, 0), FVector(0, 1, 0), FVector(0, 0, 1));
		FMemory::Memzero(&Vert->TextureCoordinate, sizeof(Vert->TextureCoordinate));
		for (int32 TexcoordIndex = 0; TexcoordIndex < MAX_STATIC_TEXCOORDS; TexcoordIndex++)
		{
			Vert->TextureCoordinate[TexcoordIndex].Set(U + SizeU * X, V + SizeV * Y);
		}		
		Vert->Color = FColor::White;
	}

	// add indices
	static const uint32 TriangleIndices[6] = { 0, 2, 1, 2, 3, 1 };
	Indices.Append(TriangleIndices, 6);
}

void FMeshMaterialRenderItem::PopulateWithMeshData()
{
	const FRawMesh* RawMesh = MeshSettings->RawMesh;
	const int32 NumVerts = RawMesh->VertexPositions.Num();
	int32 TotalNumFaces = RawMesh->FaceMaterialIndices.Num();

	// reserve renderer data
	Vertices.Empty(NumVerts);
	Indices.Empty(NumVerts >> 1);

	const FIntPoint& PropertySize = MaterialSettings->PropertySizes[MaterialProperty];
	const float ScaleX = PropertySize.X;
	const float ScaleY = PropertySize.Y;

	const static int32 VertexPositionStoredUVChannel = 6;
	// count number of texture coordinates for this mesh
	const int32 NumTexcoords = [&]()
	{
		int32 Index = 1;
		for (; Index < VertexPositionStoredUVChannel; Index++)
		{
			if (RawMesh->WedgeTexCoords[Index].Num() == 0)
			{
				break;
			}
		}

		return Index;
	}();		

	// check if we should use NewUVs or original UV set
	const bool bUseNewUVs = MeshSettings->CustomTextureCoordinates.Num() > 0;
	if (bUseNewUVs)
	{
		check(MeshSettings->CustomTextureCoordinates.Num() == RawMesh->WedgeTexCoords[MeshSettings->TextureCoordinateIndex].Num());
	}

	// add vertices
	int32 VertIndex = 0;
	const bool bHasVertexColor = (RawMesh->WedgeColors.Num() > 0);
	for (int32 FaceIndex = 0; FaceIndex < TotalNumFaces; FaceIndex++)
	{
		if (MeshSettings->MaterialIndices.Contains(RawMesh->FaceMaterialIndices[FaceIndex]))
		{
			for (int32 Corner = 0; Corner < 3; Corner++)
			{
				const int32 SrcVertIndex = FaceIndex * 3 + Corner;
				// add vertex
				FDynamicMeshVertex* Vert = new(Vertices)FDynamicMeshVertex();
				if (!bUseNewUVs)
				{
					// compute vertex position from original UV
					const FVector2D& UV = RawMesh->WedgeTexCoords[MeshSettings->TextureCoordinateIndex][SrcVertIndex];
					Vert->Position.Set(UV.X * ScaleX, UV.Y * ScaleY, 0);
				}
				else
				{
					const FVector2D& UV = MeshSettings->CustomTextureCoordinates[SrcVertIndex];
					Vert->Position.Set(UV.X * ScaleX, UV.Y * ScaleY, 0);
				}
				Vert->SetTangents(RawMesh->WedgeTangentX[SrcVertIndex], RawMesh->WedgeTangentY[SrcVertIndex], RawMesh->WedgeTangentZ[SrcVertIndex]);
				for (int32 TexcoordIndex = 0; TexcoordIndex < NumTexcoords; TexcoordIndex++)
				{
					Vert->TextureCoordinate[TexcoordIndex] = RawMesh->WedgeTexCoords[TexcoordIndex][SrcVertIndex];
				}
				
				if (NumTexcoords < VertexPositionStoredUVChannel)
				{
					for (int32 TexcoordIndex = NumTexcoords; TexcoordIndex < VertexPositionStoredUVChannel; TexcoordIndex++)
					{
						Vert->TextureCoordinate[TexcoordIndex] = Vert->TextureCoordinate[FMath::Max(NumTexcoords - 1, 0)];
					}
				}
				// Store original vertex positions in texture coordinate data
				Vert->TextureCoordinate[6].X = RawMesh->VertexPositions[RawMesh->WedgeIndices[SrcVertIndex]].X;
				Vert->TextureCoordinate[6].Y = RawMesh->VertexPositions[RawMesh->WedgeIndices[SrcVertIndex]].Y;
				Vert->TextureCoordinate[7].X = RawMesh->VertexPositions[RawMesh->WedgeIndices[SrcVertIndex]].Z;

				Vert->Color = bHasVertexColor ? RawMesh->WedgeColors[SrcVertIndex] : FColor::White;
				// add index
				Indices.Add(VertIndex);
				VertIndex++;
			}
			// add the same triangle with opposite vertex order
			Indices.Add(VertIndex - 3);
			Indices.Add(VertIndex - 1);
			Indices.Add(VertIndex - 2);
		}
	}
}