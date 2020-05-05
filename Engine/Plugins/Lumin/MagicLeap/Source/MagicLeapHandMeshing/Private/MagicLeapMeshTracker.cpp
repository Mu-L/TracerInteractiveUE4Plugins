// Copyright Epic Games, Inc. All Rights Reserved.

#include "MagicLeapMeshTracker.h"
#include "HeadMountedDisplayFunctionLibrary.h"
#include "MagicLeapHandMeshingTypes.h"
#include "MagicLeapHandMeshingModule.h"
#include "MagicLeapMath.h"

FMagicLeapMeshTracker::FMagicLeapMeshTracker()
: AppEventHandler() // HandMesh priv is autogranted and non-reality, so doesnt need runtime request.
#if WITH_MLSDK
, MeshTracker(ML_INVALID_HANDLE)
, CurrentMeshRequest(ML_INVALID_HANDLE)
#endif //WITH_MLSDK
, bCreating(false)
, bUseWeightedNormals(false)
, MeshBrickIndex(0)
, MRMesh(nullptr)
{
};

void FMagicLeapMeshTracker::OnClear()
{
	MeshBrickIndex = 0;
	MeshBrickCache.Empty();
	PendingMeshBricks.Empty();
}

void FMagicLeapMeshTracker::FreeMeshDataCache(FMagicLeapMeshTracker::FMLCachedMeshData::SharedPtr& DataCache)
{
	FScopeLock ScopeLock(&FreeCachedMeshDatasMutex);
	FreeCachedMeshDatas.Add(DataCache);
}

bool FMagicLeapMeshTracker::Create()
{
#if WITH_MLSDK
	if (MLHandleIsValid(MeshTracker))
	{
		UE_LOG(LogMagicLeapHandMeshing, Warning, TEXT("FMagicLeapMeshTracker has already been created!"));
		return false;
	}

	bCreating = true;
#endif //WITH_MLSDK
	return true;
}

bool FMagicLeapMeshTracker::Destroy()
{
	bCreating = false;
#if WITH_MLSDK
	if (!MLHandleIsValid(MeshTracker))
	{
		UE_LOG(LogMagicLeapHandMeshing, Warning, TEXT("FMagicLeapMeshTracker has already been destroyed!"));
		return false;
	}
	else
	{
		DisconnectMRMesh(MRMesh);
		MLResult Result = MLHandMeshingDestroyClient(&MeshTracker);
		if (Result != MLResult_Ok)
		{
			UE_LOG(LogMagicLeapHandMeshing, Error, TEXT("MLHandMeshingDestroyClient failed with error '%s'"), UTF8_TO_TCHAR(MLGetResultString(Result)));
			return false;
		}

		MeshTracker = ML_INVALID_HANDLE;
	}
#endif //WITH_MLSDK
	return true;
}

bool FMagicLeapMeshTracker::ConnectMRMesh(UMRMeshComponent* InMRMeshPtr)
{
	if (!InMRMeshPtr)
	{
		UE_LOG(LogMagicLeapHandMeshing, Warning,
			TEXT("MRMesh given is not valid. Ignoring this connect."));
		return false;
	}
	else if (MRMesh)
	{
		UE_LOG(LogMagicLeapHandMeshing, Warning,
			TEXT("FMagicLeapMeshTracker already has a MRMesh connected.  Ignoring this connect."));
		return false;
	}
	else if (InMRMeshPtr->IsConnected())
	{
		UE_LOG(LogMagicLeapHandMeshing, Warning,
			TEXT("MRMesh is already connected to a FMagicLeapMeshTracker. Ignoring this connect."));
		return false;
	}
	else
	{
		InMRMeshPtr->SetConnected(true);
		MRMesh = InMRMeshPtr;
	}

	return true;
}

bool FMagicLeapMeshTracker::DisconnectMRMesh(class UMRMeshComponent* InMRMeshPtr)
{
	if (!MRMesh)
	{
		UE_LOG(LogMagicLeapHandMeshing, Warning,
			TEXT("FMagicLeapMeshTracker MRMesh is already disconnected. Ignoring this disconnect."));
		return false;
	}
	else if (InMRMeshPtr != MRMesh)
	{
		UE_LOG(LogMagicLeapHandMeshing, Warning,
			TEXT("FMagicLeapMeshTracker MRMesh given is not the MRMesh connected. Ignoring this disconnect."));
		return false;
	}
	else
	{
		check(MRMesh->IsConnected());
		MRMesh->SetConnected(false);
	}
	MRMesh = nullptr;
	return true;
}

void FMagicLeapMeshTracker::SetUseWeightedNormals(const bool bInUseWeightedNormals)
{
	bUseWeightedNormals = bInUseWeightedNormals;
}

bool FMagicLeapMeshTracker::Update()
{
	if (bCreating)
	{
		if (!MRMesh)
		{
			UE_LOG(LogMagicLeapHandMeshing, Error, TEXT("FMagicLeapMeshTracker has no MRMesh!"));
			return false;
		}

#if WITH_MLSDK
		MLResult Result = MLHandMeshingCreateClient(&MeshTracker);
		if (Result != MLResult_Ok && Result != MLResult_NotImplemented)
		{
			UE_LOG(LogMagicLeapHandMeshing, Error, TEXT("MLHandMeshingCreateClient failed with error '%s'"), UTF8_TO_TCHAR(MLGetResultString(Result)));
			return false;
		}
#endif // WITH_MLSDK

		MeshBrickCache.Empty();
		MeshBrickIndex = 0;
		bCreating = false;
	}

#if WITH_MLSDK
	if (!MLHandleIsValid(MeshTracker))
	{
		return false;
	}

	if (!MLHandleIsValid(CurrentMeshRequest))
	{
		if (!RequestMesh())
		{
			return false;
		}
	}
#endif // WITH_MLSDK

	return GetMeshResult();
}

bool FMagicLeapMeshTracker::RequestMesh()
{
#if WITH_MLSDK
	MLResult Result = MLHandMeshingRequestMesh(MeshTracker, &CurrentMeshRequest);
	if (Result != MLResult_Ok)
	{
		UE_LOG(LogMagicLeapHandMeshing, Error, TEXT("MLHandMeshingRequestMesh failed with error %s"), UTF8_TO_TCHAR(MLGetResultString(Result)));
		return false;
	}

	return true;
#else
	return false;
#endif // WITH_MLSDK
}

bool FMagicLeapMeshTracker::GetMeshResult()
{
#if WITH_MLSDK
	const float WorldToMetersScale = IMagicLeapPlugin::Get().GetWorldToMetersScale();

	// Get mesh result
	if (MLHandleIsValid(CurrentMeshRequest))
	{
		MLHandMesh Mesh = {};
		MLHandMeshInit(&Mesh);
		auto Result = MLHandMeshingGetResult(MeshTracker, CurrentMeshRequest, &Mesh);

		if (MLResult_Ok != Result)
		{
			// Just silently wait for pending result
			if (MLResult_Pending != Result)
			{
				UE_LOG(LogMagicLeapHandMeshing, Error, TEXT("MLMeshingGetMeshResult failed: %s."), UTF8_TO_TCHAR(MLGetResultString(Result)));
				// Mesh request failed, lets queue another one.
				CurrentMeshRequest = ML_INVALID_HANDLE;
				return true;
			}
			// Mesh request pending...
			return false;
		}
		else
		{
			// Create a bounding box based on the hmd position and rotation
			FRotator HMDRotation;
			FVector HMDPosition;
			UHeadMountedDisplayFunctionLibrary::GetOrientationAndPosition(HMDRotation, HMDPosition);
			// Put the center 1/2 meter in front of the face and set the radius to 1 meter
			const FTransform TrackingToWorld = UHeadMountedDisplayFunctionLibrary::GetTrackingToWorldTransform(nullptr);
			FVector Center = TrackingToWorld.TransformPosition(HMDPosition + ((WorldToMetersScale / 2.f) * HMDRotation.Vector()));
			FVector BoxExtent(WorldToMetersScale);
			FBox Bounds(Center - BoxExtent, Center + BoxExtent);

			FVector VertexOffset = UHeadMountedDisplayFunctionLibrary::GetTrackingToWorldTransform(MRMesh).Inverse().GetLocation();
			for (uint32_t MeshIndex = 0; MeshIndex < Mesh.data_count; ++MeshIndex)
			{
				const auto &MeshData = Mesh.data[MeshIndex];
				auto BlockID = FGuid();// MeshData.id.data[0], MeshData.id.data[0] >> 32, MeshData.id.data[1], MeshData.id.data[1] >> 32);
				// Acquire mesh data cache and mark its brick ID
				FMLCachedMeshData::SharedPtr CurrentMeshDataCache = AquireMeshDataCache();
				CurrentMeshDataCache->BlockID = BlockID;

				// Pull vertices
				CurrentMeshDataCache->OffsetVertices.Reserve(MeshData.vertex_count);
				CurrentMeshDataCache->WorldVertices.Reserve(MeshData.vertex_count);
				for (uint32_t v = 0; v < MeshData.vertex_count; ++v)
				{
					CurrentMeshDataCache->OffsetVertices.Add(MagicLeap::ToFVector(MeshData.vertex[v], WorldToMetersScale) - VertexOffset);
					CurrentMeshDataCache->WorldVertices.Add(MagicLeap::ToFVector(MeshData.vertex[v], WorldToMetersScale));
				}

				// Pull indices
				CurrentMeshDataCache->Triangles.Reserve(MeshData.index_count);
				for (uint16_t i = 0; i < MeshData.index_count-2; i += 3)
				{
					// Hand mesh indices are in clockwise winding order but Unreal needs them to be counter clockwise to display properly,
					// so reverse the winding when adding the indices to the Triangles array.
					CurrentMeshDataCache->Triangles.Add(static_cast<uint32>(MeshData.index[i]));
					CurrentMeshDataCache->Triangles.Add(static_cast<uint32>(MeshData.index[i+2]));
					CurrentMeshDataCache->Triangles.Add(static_cast<uint32>(MeshData.index[i+1]));
				}

				// Generate normals
				CurrentMeshDataCache->Normals.SetNumZeroed(MeshData.vertex_count);
				for (int32 iNormal = 0; iNormal < CurrentMeshDataCache->Triangles.Num()-2; iNormal += 3)
				{
					FVector A = CurrentMeshDataCache->WorldVertices[CurrentMeshDataCache->Triangles[iNormal]];
					FVector B = CurrentMeshDataCache->WorldVertices[CurrentMeshDataCache->Triangles[iNormal+1]];
					FVector C = CurrentMeshDataCache->WorldVertices[CurrentMeshDataCache->Triangles[iNormal+2]];
					// Get the normal for this triangle.
					FVector AToB = B - A;
					FVector AToC = C - A;
					FVector Normal = FVector::CrossProduct(AToC, AToB);
					// Weight it based on the area of the triangle, if requested.  Otherwise, just normalize it.
					if (bUseWeightedNormals)
					{
						float TriangleSize = 0.5f * Normal.Size();
						Normal.Normalize();
						Normal *= TriangleSize;
					}
					else
					{
						Normal.Normalize();
					}
					// Add to the normals of each vertex of the triangle.  The final normals will be Normalized while iterating to get tangents, below.
					CurrentMeshDataCache->Normals[CurrentMeshDataCache->Triangles[iNormal]] += Normal;
					CurrentMeshDataCache->Normals[CurrentMeshDataCache->Triangles[iNormal+1]] += Normal;
					CurrentMeshDataCache->Normals[CurrentMeshDataCache->Triangles[iNormal+2]] += Normal;
				}
				
				CurrentMeshDataCache->Tangents.Reserve(MeshData.vertex_count * 2);
				for (uint32_t t = 0; t < MeshData.vertex_count; ++t)
				{
					// Normals aren't normalized above due to the iterative nature of their generation.  Normalize here before getting their tangents.
					CurrentMeshDataCache->Normals[t].Normalize();
					const FVector& Norm = CurrentMeshDataCache->Normals[t];

					// Calculate tangent
					auto Perp = Norm.X < Norm.Z ?
						FVector(1.0f, 0.0f, 0.0f) : FVector(0.0f, 1.0f, 0.0f);
					auto Tang = FVector::CrossProduct(Norm, Perp);
					Tang.Normalize();

					CurrentMeshDataCache->Tangents.Add(Tang);
					CurrentMeshDataCache->Tangents.Add(Norm);
				}

				// To work in all rendering paths we always set a vertex color
				if (CurrentMeshDataCache->VertexColors.Num() == 0)
				{
					for (uint32 v = 0; v < MeshData.vertex_count; ++v)
					{
						CurrentMeshDataCache->VertexColors.Add(FColor::White);
					}
				}

				// Write UVs
				CurrentMeshDataCache->UV0.Reserve(MeshData.vertex_count);
				for (uint32 v = 0; v < MeshData.vertex_count; ++v)
				{
					const float FakeCoord = static_cast<float>(v) / static_cast<float>(MeshData.vertex_count);
					CurrentMeshDataCache->UV0.Add(FVector2D(FakeCoord, FakeCoord));
				}

#ifdef DEBUG_MESH_BRICK_EVENTS
				UE_LOG(LogMagicLeapHandMeshing,
					Log,
					TEXT("UMeshTrackerComponent: ADDING/UPDATING brick %s"),
					TCHAR_TO_UTF8(*(BlockID.ToString())));
#endif //DEBUG_MESH_BRICK_EVENTS

				// Get/create brick ID for this mesh GUID
				auto MeshBrickInfo = GetBrickInfo(BlockID, true);
				// Create/update brick
				static_cast<IMRMesh*>(MRMesh)->SendBrickData(
					IMRMesh::FSendBrickDataArgs
					{
						TSharedPtr<IMRMesh::FBrickDataReceipt, ESPMode::ThreadSafe>
						(new FMeshTrackerBrickDataReceipt(CurrentMeshDataCache)),
						*MeshBrickInfo,
						CurrentMeshDataCache->WorldVertices,
						CurrentMeshDataCache->UV0,
						CurrentMeshDataCache->Tangents,
						CurrentMeshDataCache->VertexColors,
						CurrentMeshDataCache->Triangles,
						Bounds
					}
				);
			}

			// All meshes pulled and/or updated; free the ML resource
			MLHandMeshingFreeResource(MeshTracker, &CurrentMeshRequest);
			CurrentMeshRequest = ML_INVALID_HANDLE;
			return true;
		}
	}
#endif // WITH_MLSDK
	return true;
}

uint64* FMagicLeapMeshTracker::GetBrickInfo(const FGuid& meshGuid, bool addIfNotFound)
{
	auto meshBrickInfo = MeshBrickCache.Find(meshGuid);
	if (meshBrickInfo == nullptr && addIfNotFound)
	{
		MeshBrickCache.Add(meshGuid, MeshBrickIndex++);
		meshBrickInfo = MeshBrickCache.Find(meshGuid);
	}
	return meshBrickInfo;
}

bool FMagicLeapMeshTracker::HasMRMesh() const
{
	return MRMesh != nullptr;
}

bool FMagicLeapMeshTracker::HasClient() const
{
#if WITH_MLSDK
	return MLHandleIsValid(MeshTracker);
#else
	return false;
#endif // WITH_MLSDK
}
