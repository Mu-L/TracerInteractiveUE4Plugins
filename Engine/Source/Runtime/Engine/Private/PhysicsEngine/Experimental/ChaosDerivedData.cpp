// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosDerivedData.h"

#if WITH_CHAOS

#include "CoreMinimal.h"
#include "Chaos/Core.h"
#include "Chaos/CollisionConvexMesh.h"
#include "Chaos/ChaosArchive.h"
#include "ChaosDerivedDataUtil.h"
#include "Chaos/Particles.h"
#include "Chaos/TriangleMeshImplicitObject.h"
#include "Chaos/Convex.h"

#include "Serialization/Archive.h"
#include "PhysicsEngine/BodySetup.h"
#include "PhysicsEngine/PhysicsSettings.h"
#include "Serialization/MemoryWriter.h"

int32 EnableMeshClean = 1;
FAutoConsoleVariableRef CVarEnableMeshClean(TEXT("p.EnableMeshClean"), EnableMeshClean, TEXT("Enable/Disable mesh cleanup during cook."));

const TCHAR* FChaosDerivedDataCooker::GetPluginName() const
{
	return TEXT("ChaosGeometryData");
}

const TCHAR* FChaosDerivedDataCooker::GetVersionString() const
{
	// As changing our DDC version will most likely affect any external callers that rely on Chaos types
	// for their own DDC or serialized data - change Chaos::ChaosVersionString in Chaos/Core.h to bump our
	// Chaos data version. Callers can also rely on that version in their builders and avoid bad serialization
	// when basic Chaos data changes
	return *Chaos::ChaosVersionString;
}

FString FChaosDerivedDataCooker::GetPluginSpecificCacheKeySuffix() const
{
	FString SetupGeometryKey(TEXT("INVALID"));

	if(Setup)
	{
		Setup->GetGeometryDDCKey(SetupGeometryKey);
	}

	return FString::Printf(TEXT("%s_%s"),
		*RequestedFormat.ToString(),
		*SetupGeometryKey);
}

bool FChaosDerivedDataCooker::IsBuildThreadsafe() const
{
	// #BG Investigate Parallel Build
	return false;
}

bool FChaosDerivedDataCooker::Build(TArray<uint8>& OutData)
{
	bool bSucceeded = false;

	if(Setup)
	{
		FCookBodySetupInfo CookInfo;

		EPhysXMeshCookFlags TempFlags = static_cast<EPhysXMeshCookFlags>(0); // #BGTODO Remove need for PhysX specific flags
		Setup->GetCookInfo(CookInfo, TempFlags);

		FMemoryWriter MemWriterAr(OutData);
		Chaos::FChaosArchive Ar(MemWriterAr);

		int32 PrecisionSize = (int32)sizeof(BuildPrecision);

		Ar << PrecisionSize;
		BuildInternal(Ar, CookInfo);

		bSucceeded = true;
	}

	return bSucceeded;
}

void FChaosDerivedDataCooker::AddReferencedObjects(FReferenceCollector& Collector)
{
	if(Setup)
	{
		Collector.AddReferencedObject(Setup);
	}
}

FChaosDerivedDataCooker::FChaosDerivedDataCooker(UBodySetup* InSetup, FName InFormat)
	: Setup(InSetup)
	, RequestedFormat(InFormat)
{

}

TUniquePtr<Chaos::FTriangleMeshImplicitObject> FChaosDerivedDataCooker::BuildSingleTrimesh(const FTriMeshCollisionData& Desc, TArray<int32>& OutFaceRemap)
{
	if(Desc.Vertices.Num() == 0)
	{
		return nullptr;
	}

	TArray<FVector> FinalVerts = Desc.Vertices;

	// Push indices into one flat array
	TArray<int32> FinalIndices;
	FinalIndices.Reserve(Desc.Indices.Num() * 3);
	for(const FTriIndices& Tri : Desc.Indices)
	{
		//question: It seems like unreal triangles are CW, but couldn't find confirmation for this
		FinalIndices.Add(Tri.v1);
		FinalIndices.Add(Tri.v0);
		FinalIndices.Add(Tri.v2);
	}

	if(EnableMeshClean)
	{
		Chaos::CleanTrimesh(FinalVerts, FinalIndices, &OutFaceRemap);
	}

	// Build particle list #BG Maybe allow TParticles to copy vectors?
	Chaos::TParticles<Chaos::FReal, 3> TriMeshParticles;
	TriMeshParticles.AddParticles(FinalVerts.Num());

	const int32 NumVerts = FinalVerts.Num();
	for(int32 VertIndex = 0; VertIndex < NumVerts; ++VertIndex)
	{
		TriMeshParticles.X(VertIndex) = FinalVerts[VertIndex];
	}

	// Build chaos triangle list. #BGTODO Just make the clean function take these types instead of double copying
	auto LambdaHelper = [&Desc, &FinalVerts, &FinalIndices, &TriMeshParticles, &OutFaceRemap](auto& Triangles) -> TUniquePtr<Chaos::FTriangleMeshImplicitObject>
	{
		const int32 NumTriangles = FinalIndices.Num() / 3;
		bool bHasMaterials = Desc.MaterialIndices.Num() > 0;
		TArray<uint16> MaterialIndices;

		if(bHasMaterials)
		{
			MaterialIndices.Reserve(NumTriangles);
		}

		Triangles.Reserve(NumTriangles);
		for(int32 TriangleIndex = 0; TriangleIndex < NumTriangles; ++TriangleIndex)
		{
			// Only add this triangle if it is valid
			const int32 BaseIndex = TriangleIndex * 3;
			const bool bIsValidTriangle = Chaos::FConvexBuilder::IsValidTriangle(
				FinalVerts[FinalIndices[BaseIndex]],
				FinalVerts[FinalIndices[BaseIndex + 1]],
				FinalVerts[FinalIndices[BaseIndex + 2]]);

			// TODO: Figure out a proper way to handle this. Could these edges get sewn together? Is this important?
			//if (ensureMsgf(bIsValidTriangle, TEXT("FChaosDerivedDataCooker::BuildTriangleMeshes(): Trimesh attempted cooked with invalid triangle!")));
			if(bIsValidTriangle)
			{
				Triangles.Add(Chaos::TVector<int32, 3>(FinalIndices[BaseIndex], FinalIndices[BaseIndex + 1], FinalIndices[BaseIndex + 2]));

				if(bHasMaterials)
				{
					if(EnableMeshClean)
					{
						if(!ensure(OutFaceRemap.IsValidIndex(TriangleIndex)))
						{
							MaterialIndices.Empty();
							bHasMaterials = false;
						}
						else
						{
							const int32 OriginalIndex = OutFaceRemap[TriangleIndex];

							if(ensure(Desc.MaterialIndices.IsValidIndex(OriginalIndex)))
							{
								MaterialIndices.Add(Desc.MaterialIndices[OriginalIndex]);
							}
							else
							{
								MaterialIndices.Empty();
								bHasMaterials = false;
							}
						}
					}
					else
					{
						if(ensure(Desc.MaterialIndices.IsValidIndex(TriangleIndex)))
						{
							MaterialIndices.Add(Desc.MaterialIndices[TriangleIndex]);
						}
						else
						{
							MaterialIndices.Empty();
							bHasMaterials = false;
						}
					}

				}
			}
		}

		TUniquePtr<TArray<int32>> OutFaceRemapPtr = MakeUnique<TArray<int32>>(OutFaceRemap);
		return MakeUnique<Chaos::FTriangleMeshImplicitObject>(MoveTemp(TriMeshParticles), MoveTemp(Triangles), MoveTemp(MaterialIndices), MoveTemp(OutFaceRemapPtr));
	};

	if(FinalVerts.Num() < TNumericLimits<uint16>::Max())
	{
		TArray<Chaos::TVector<uint16, 3>> TrianglesSmallIdx;
		return LambdaHelper(TrianglesSmallIdx);
	}
	else
	{
		TArray<Chaos::TVector<int32, 3>> TrianglesLargeIdx;
		return LambdaHelper(TrianglesLargeIdx);
	}

	return nullptr;
}

void FChaosDerivedDataCooker::BuildTriangleMeshes(TArray<TUniquePtr<Chaos::FTriangleMeshImplicitObject>>& OutTriangleMeshes, TArray<int32>& OutFaceRemap, const FCookBodySetupInfo& InParams)
{
	if(!InParams.bCookTriMesh)
	{
		return;
	}

	TArray<FVector> FinalVerts = InParams.TriangleMeshDesc.Vertices;

	// Push indices into one flat array
	TArray<int32> FinalIndices;
	FinalIndices.Reserve(InParams.TriangleMeshDesc.Indices.Num() * 3);
	for(const FTriIndices& Tri : InParams.TriangleMeshDesc.Indices)
	{
		//question: It seems like unreal triangles are CW, but couldn't find confirmation for this
		FinalIndices.Add(Tri.v1);
		FinalIndices.Add(Tri.v0);
		FinalIndices.Add(Tri.v2);
	}

	if (EnableMeshClean)
	{
		Chaos::CleanTrimesh(FinalVerts, FinalIndices, &OutFaceRemap);
	}

	// Build particle list #BG Maybe allow TParticles to copy vectors?
	Chaos::TParticles<Chaos::FReal, 3> TriMeshParticles;
	TriMeshParticles.AddParticles(FinalVerts.Num());

	const int32 NumVerts = FinalVerts.Num();
	for(int32 VertIndex = 0; VertIndex < NumVerts; ++VertIndex)
	{
		TriMeshParticles.X(VertIndex) = FinalVerts[VertIndex];
	}

	// Build chaos triangle list. #BGTODO Just make the clean function take these types instead of double copying
	const int32 NumTriangles = FinalIndices.Num() / 3;
	bool bHasMaterials = InParams.TriangleMeshDesc.MaterialIndices.Num() > 0;
	TArray<uint16> MaterialIndices;

	auto LambdaHelper = [&](auto& Triangles)
	{
		if(bHasMaterials)
		{
			MaterialIndices.Reserve(NumTriangles);
		}

		Triangles.Reserve(NumTriangles);
		for(int32 TriangleIndex = 0; TriangleIndex < NumTriangles; ++TriangleIndex)
		{
			// Only add this triangle if it is valid
			const int32 BaseIndex = TriangleIndex * 3;
			const bool bIsValidTriangle = Chaos::FConvexBuilder::IsValidTriangle(
				FinalVerts[FinalIndices[BaseIndex]],
				FinalVerts[FinalIndices[BaseIndex + 1]],
				FinalVerts[FinalIndices[BaseIndex + 2]]);

			// TODO: Figure out a proper way to handle this. Could these edges get sewn together? Is this important?
			//if (ensureMsgf(bIsValidTriangle, TEXT("FChaosDerivedDataCooker::BuildTriangleMeshes(): Trimesh attempted cooked with invalid triangle!")));
			if (bIsValidTriangle)
			{
				Triangles.Add(Chaos::TVector<int32, 3>(FinalIndices[BaseIndex], FinalIndices[BaseIndex + 1], FinalIndices[BaseIndex + 2]));

				if(bHasMaterials)
				{
					if (EnableMeshClean)
					{
						if(!ensure(OutFaceRemap.IsValidIndex(TriangleIndex)))
						{
							MaterialIndices.Empty();
							bHasMaterials = false;
						}
						else
						{
							const int32 OriginalIndex = OutFaceRemap[TriangleIndex];

							if (ensure(InParams.TriangleMeshDesc.MaterialIndices.IsValidIndex(OriginalIndex)))
							{
								MaterialIndices.Add(InParams.TriangleMeshDesc.MaterialIndices[OriginalIndex]);
							}
							else
							{
								MaterialIndices.Empty();
								bHasMaterials = false;
							}
						}
					}
					else
					{
						if (ensure(InParams.TriangleMeshDesc.MaterialIndices.IsValidIndex(TriangleIndex)))
						{
							MaterialIndices.Add(InParams.TriangleMeshDesc.MaterialIndices[TriangleIndex]);
						}
						else
						{
							MaterialIndices.Empty();
							bHasMaterials = false;
						}
					}

				}
			}
		}

		TUniquePtr<TArray<int32>> OutFaceRemapPtr = MakeUnique<TArray<int32>>(OutFaceRemap);
		OutTriangleMeshes.Emplace(new Chaos::FTriangleMeshImplicitObject(MoveTemp(TriMeshParticles), MoveTemp(Triangles), MoveTemp(MaterialIndices), MoveTemp(OutFaceRemapPtr)));
	};

	if(FinalVerts.Num() < TNumericLimits<uint16>::Max())
	{
		TArray<Chaos::TVector<uint16, 3>> TrianglesSmallIdx;
		LambdaHelper(TrianglesSmallIdx);
	}
	else
	{
		TArray<Chaos::TVector<int32, 3>> TrianglesLargeIdx;
		LambdaHelper(TrianglesLargeIdx);
	}	
}

void FChaosDerivedDataCooker::BuildConvexMeshes(TArray<TUniquePtr<Chaos::FImplicitObject>>& OutConvexMeshes, const FCookBodySetupInfo& InParams)
{
	using namespace Chaos;
	auto BuildConvexFromVerts = [](TArray<TUniquePtr<Chaos::FImplicitObject>>& OutConvexes, const TArray<TArray<FVector>>& InMeshVerts, const bool bMirrored)
	{
		for(const TArray<FVector>& HullVerts : InMeshVerts)
		{
			const int32 NumHullVerts = HullVerts.Num();
			if (NumHullVerts == 0)
			{
				continue;
			}

			// Calculate the margin to apply to the convex - it depends on overall dimensions
			FAABB3 Bounds = FAABB3::EmptyAABB();
			for (int32 VertIndex = 0; VertIndex < NumHullVerts; ++VertIndex)
			{
				const FVector& HullVert = HullVerts[VertIndex];
				Bounds.GrowToInclude(HullVert);
			}

			// @todo(chaos): dedupe - see ChaosInterfaceUtils.cpp CreateGeometry()
			const float CollisionMarginFraction = FMath::Max(0.0f, UPhysicsSettingsCore::Get()->SolverOptions.CollisionMarginFraction);
			const float CollisionMarginMax = FMath::Max(0.0f, UPhysicsSettingsCore::Get()->SolverOptions.CollisionMarginMax);
			const float CollisionMargin = FMath::Min(Bounds.Extents().GetAbsMax() * CollisionMarginFraction, CollisionMarginMax);

			// Create the surface particle for the convex
			TParticles<FReal, 3> ConvexParticles;
			ConvexParticles.AddParticles(NumHullVerts);

			for (int32 VertIndex = 0; VertIndex < NumHullVerts; ++VertIndex)
			{
				const FVector& HullVert = HullVerts[VertIndex];
				ConvexParticles.X(VertIndex) = FVector(bMirrored ? -HullVert.X : HullVert.X, HullVert.Y, HullVert.Z);
			}


			OutConvexes.Emplace(new Chaos::FConvex(ConvexParticles, CollisionMargin));
		}
	};

	if(InParams.bCookNonMirroredConvex)
	{
		BuildConvexFromVerts(OutConvexMeshes, InParams.NonMirroredConvexVertices, false);
	}

	if(InParams.bCookMirroredConvex)
	{
		BuildConvexFromVerts(OutConvexMeshes, InParams.MirroredConvexVertices, true);
	}

}

template<typename Precision>
void BuildSimpleShapes(TArray<TUniquePtr<Chaos::FImplicitObject>>& OutImplicits, UBodySetup* InSetup)
{
	check(InSetup);

	FKAggregateGeom& AggGeom = InSetup->AggGeom;

	for(FKBoxElem& Box : AggGeom.BoxElems)
	{
		Chaos::TVector<Precision, 3> HalfBoxExtent = Chaos::TVector<Precision, 3>(Box.X, Box.Y, Box.Z) / 2.0f;
		TUniquePtr<Chaos::TBox<Precision, 3>> NonTransformed = MakeUnique<Chaos::TBox<Precision, 3>>(-HalfBoxExtent, HalfBoxExtent);
		Chaos::TRigidTransform<Precision, 3> ShapeLocalTransform(Box.Center, Box.Rotation.Quaternion());
		OutImplicits.Add(MakeUnique<Chaos::TImplicitObjectTransformed<Precision, 3>>(MakeSerializable(NonTransformed), MoveTemp(NonTransformed), ShapeLocalTransform));
	}

	for(FKSphereElem& Sphere : AggGeom.SphereElems)
	{
		OutImplicits.Add(MakeUnique<Chaos::TSphere<Precision, 3>>(Sphere.Center, Sphere.Radius));
	}

	for(FKSphylElem& Sphyl : AggGeom.SphylElems)
	{
		const float HalfLength = Sphyl.Length / 2.0f;
		const Chaos::TVector<Precision, 3> TopPoint(0.0f, 0.0f, HalfLength);
		const Chaos::TVector<Precision, 3> BottomPoint(0.0f, 0.0f, -HalfLength);
		TUniquePtr<Chaos::TCapsule<Precision>> NonTransformed = MakeUnique<Chaos::TCapsule<Precision>>(TopPoint, BottomPoint, Sphyl.Radius);
		Chaos::TRigidTransform<Precision, 3> ShapeLocalTransform(Sphyl.Center, Sphyl.Rotation.Quaternion());
		OutImplicits.Add(MakeUnique<Chaos::TImplicitObjectTransformed<Precision, 3>>(MakeSerializable(NonTransformed), MoveTemp(NonTransformed), ShapeLocalTransform));
	}

	const int32 NumTaperedCapsules = AggGeom.TaperedCapsuleElems.Num();
	UE_CLOG(NumTaperedCapsules > 0, LogChaos, Warning, TEXT("Ignoring %d tapered spheres when building collision data for body setup %s"), NumTaperedCapsules, *InSetup->GetName());
}

void FChaosDerivedDataCooker::BuildInternal(Chaos::FChaosArchive& Ar, FCookBodySetupInfo& InInfo)
{
	TArray<TUniquePtr<Chaos::FImplicitObject>> SimpleImplicits;
	TArray<TUniquePtr<Chaos::FTriangleMeshImplicitObject>> ComplexImplicits;

	TArray<int32> FaceRemap;
	//BuildSimpleShapes(SimpleImplicits, Setup);
	BuildConvexMeshes(SimpleImplicits, InInfo);
	BuildTriangleMeshes(ComplexImplicits, FaceRemap, InInfo);

	//TUniquePtr<Chaos::TImplicitObjectUnion<Precision, 3>> SimpleUnion = MakeUnique<Chaos::TImplicitObjectUnion<Precision, 3>>(MoveTemp(SimpleImplicits));
	//TUniquePtr<Chaos::TImplicitObjectUnion<Precision, 3>> ComplexUnion = MakeUnique<Chaos::TImplicitObjectUnion<Precision, 3>>(MoveTemp(ComplexImplicits));

	FBodySetupUVInfo UVInfo;
	if(InInfo.bSupportUVFromHitResults)
	{
		UVInfo.FillFromTriMesh(InInfo.TriangleMeshDesc);
	}
	if (!InInfo.bSupportFaceRemap)
	{
		FaceRemap.Empty();
	}

	Ar << SimpleImplicits << ComplexImplicits << UVInfo << FaceRemap;
}

#endif
 
