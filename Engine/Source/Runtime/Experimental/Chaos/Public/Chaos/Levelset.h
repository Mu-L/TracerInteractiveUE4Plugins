// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/ArrayND.h"
#include "Chaos/Box.h"
#include "Chaos/ImplicitObject.h"
#include "Chaos/ImplicitObjectScaled.h"
#include "Chaos/Particles.h"
#include "Chaos/UniformGrid.h"

namespace Chaos { class FErrorReporter; }

namespace Chaos
{

template<class T>
class TTriangleMesh;

template<class T, int D>
class TPlane;

template<typename T>
class TCapsule;

class FConvex;

template<class T, int d>
class CHAOS_API TLevelSet final : public FImplicitObject
{
  public:
	using FImplicitObject::SignedDistance;

	TLevelSet(FErrorReporter& ErrorReporter, const TUniformGrid<T, d>& InGrid, const TParticles<T, d>& InParticles, const TTriangleMesh<T>& Mesh, const int32 BandWidth = 0);
	TLevelSet(FErrorReporter& ErrorReporter, const TUniformGrid<T, d>& InGrid, const FImplicitObject& InObject, const int32 BandWidth = 0, const bool bUseObjectPhi = false);
	TLevelSet(std::istream& Stream);
	TLevelSet(const TLevelSet<T, d>& Other) = delete;
	TLevelSet(TLevelSet<T, d>&& Other);
	virtual ~TLevelSet();

	virtual TUniquePtr<FImplicitObject> DeepCopy() const;

	void Write(std::ostream& Stream) const;
	virtual T PhiWithNormal(const TVector<T, d>& x, TVector<T, d>& Normal) const override;
	T SignedDistance(const TVector<T, d>& x) const;

	virtual const TAABB<T, d> BoundingBox() const override { return MOriginalLocalBoundingBox; }

	// Returns a const ref to the underlying phi grid
	const TArrayND<T, d>& GetPhiArray() const { return MPhi; }

	// Returns a const ref to the underlying grid of normals
	const TArrayND<TVector<T, d>, d>& GetNormalsArray() const { return MNormals; }

	// Returns a const ref to the underlying grid structure
	const TUniformGrid<T, d>& GetGrid() const { return MGrid; }

	FORCEINLINE void Shrink(const T Value)
	{
		for (int32 i = 0; i < MGrid.Counts().Product(); ++i)
		{
			MPhi[i] += Value;
		}
	}

	FORCEINLINE static constexpr EImplicitObjectType StaticType()
	{
		return ImplicitObjectType::LevelSet;
	}

	FORCEINLINE void SerializeImp(FArchive& Ar)
	{
		FImplicitObject::SerializeImp(Ar);
		Ar << MGrid;
		Ar << MPhi;
		Ar << MNormals;
		TBox<FReal, 3>::SerializeAsAABB(Ar, MLocalBoundingBox);
		TBox<FReal, 3>::SerializeAsAABB(Ar, MOriginalLocalBoundingBox);
		Ar << MBandWidth;
	}

	virtual void Serialize(FChaosArchive& Ar) override
	{
		SerializeImp(Ar);
	}

	virtual void Serialize(FArchive& Ar) override
	{
		SerializeImp(Ar);
	}

	/** 
	 * Estimate the volume bounded by the zero'th isocontour of the level set.
	 * #BGTODO - We can generate a more accurate pre-calculated volume during generation as this method still under
	 * estimates the actual volume of the surface.
	 */
	T ApproximateNegativeMaterial() const
	{
		const TVector<T,d>& CellDim = MGrid.Dx();
		const float AvgRadius = (CellDim[0] + CellDim[1] + CellDim[2]) / (T)3;
		const T CellVolume = CellDim.Product();
		T Volume = 0.0;
		for (int32 Idx = 0; Idx < MPhi.Num(); ++Idx)
		{
			const T Phi = MPhi[Idx];
			if (Phi <= 0.0)
			{
				T CellRadius = AvgRadius - FMath::Abs(Phi);

				if(CellRadius > KINDA_SMALL_NUMBER)
				{
					const T Scale = FMath::Min((T)1, CellRadius / AvgRadius);
					Volume += CellVolume * Scale;
				}
				else
				{
					Volume += CellVolume;
				}
			}
		}
		return Volume;
	}

	bool ComputeMassProperties(T& OutVolume, TVector<T, d>& OutCOM, PMatrix<T,d,d>& OutInertia, TRotation<T, d>& OutRotationOfMass) const;

	T ComputeLevelSetError(const TParticles<T, d>& InParticles, const TArray<TVector<T, 3>>& Normals, const TTriangleMesh<T>& Mesh, T& AngleError, T& MaxDistError);

	// Output a mesh and level set as obj files
	void OutputDebugData(FErrorReporter& ErrorReporter, const TParticles<T, d>& InParticles, const TArray<TVector<T, 3>>& Normals, const TTriangleMesh<T>& Mesh, const FString FileName);

	bool CheckData(FErrorReporter& ErrorReporter, const TParticles<T, d>& InParticles, const TTriangleMesh<T>& Mesh, const TArray<TVector<T, 3>> &Normals);

	virtual uint32 GetTypeHash() const override
	{
		uint32 Result = 0;

		const int32 NumValues = MPhi.Num();

		for(int32 Index = 0; Index < NumValues; ++Index)
		{
			Result = HashCombine(Result, ::GetTypeHash(MPhi[Index]));
		}

		return Result;
	}


	bool SweepGeom(const TSphere<FReal, 3>& QueryGeom, const FRigidTransform3& StartTM, const FVec3& Dir, const FReal Length, FReal& OutTime, FVec3& OutPosition, FVec3& OutNormal, int32& OutFaceIndex, const FReal Thickness = 0, const bool bComputeMTD = false) const;
	bool SweepGeom(const TBox<FReal, 3>& QueryGeom, const FRigidTransform3& StartTM, const FVec3& Dir, const FReal Length, FReal& OutTime, FVec3& OutPosition, FVec3& OutNormal, int32& OutFaceIndex, const FReal Thickness = 0, const bool bComputeMTD = false) const;
	bool SweepGeom(const TCapsule<FReal>& QueryGeom, const FRigidTransform3& StartTM, const FVec3& Dir, const FReal Length, FReal& OutTime, FVec3& OutPosition, FVec3& OutNormal, int32& OutFaceIndex, const FReal Thickness = 0, const bool bComputeMTD = false) const;
	bool SweepGeom(const FConvex& QueryGeom, const FRigidTransform3& StartTM, const FVec3& Dir, const FReal Length, FReal& OutTime, FVec3& OutPosition, FVec3& OutNormal, int32& OutFaceIndex, const FReal Thickness = 0, const bool bComputeMTD = false) const;

	bool SweepGeom(const TImplicitObjectScaled<TSphere<FReal, 3>>& QueryGeom, const FRigidTransform3& StartTM, const FVec3& Dir, const FReal Length, FReal& OutTime, FVec3& OutPosition, FVec3& OutNormal, int32& OutFaceIndex, const FReal Thickness = 0, const bool bComputeMTD = false) const;
	bool SweepGeom(const TImplicitObjectScaled<TBox<FReal, 3>>& QueryGeom, const FRigidTransform3& StartTM, const FVec3& Dir, const FReal Length, FReal& OutTime, FVec3& OutPosition, FVec3& OutNormal, int32& OutFaceIndex, const FReal Thickness = 0, const bool bComputeMTD = false) const;
	bool SweepGeom(const TImplicitObjectScaled<TCapsule<FReal>>& QueryGeom, const FRigidTransform3& StartTM, const FVec3& Dir, const FReal Length, FReal& OutTime, FVec3& OutPosition, FVec3& OutNormal, int32& OutFaceIndex, const FReal Thickness = 0, const bool bComputeMTD = false) const;
	bool SweepGeom(const TImplicitObjectScaled<FConvex>& QueryGeom, const FRigidTransform3& StartTM, const FVec3& Dir, const FReal Length, FReal& OutTime, FVec3& OutPosition, FVec3& OutNormal, int32& OutFaceIndex, const FReal Thickness = 0, const bool bComputeMTD = false) const;

	template<typename QueryGeomType>
	bool SweepGeomImp(const QueryGeomType& QueryGeom, const FRigidTransform3& StartTM, const FVec3& Dir, const FReal Length, FReal& OutTime, FVec3& OutPosition, FVec3& OutNormal, int32& OutFaceIndex, const FReal Thickness, const bool bComputeMTD) const;

	bool OverlapGeom(const TSphere<FReal, 3>& QueryGeom, const FRigidTransform3& QueryTM, const FReal Thickness, FMTDInfo* OutMTD = nullptr) const;
	bool OverlapGeom(const TBox<FReal, 3>& QueryGeom, const FRigidTransform3& QueryTM, const FReal Thickness, FMTDInfo* OutMTD = nullptr) const;
	bool OverlapGeom(const TCapsule<FReal>& QueryGeom, const FRigidTransform3& QueryTM, const FReal Thickness, FMTDInfo* OutMTD = nullptr) const;
	bool OverlapGeom(const FConvex& QueryGeom, const FRigidTransform3& QueryTM, const FReal Thickness, FMTDInfo* OutMTD = nullptr) const;

	bool OverlapGeom(const TImplicitObjectScaled<TSphere<FReal, 3>>& QueryGeom, const FRigidTransform3& QueryTM, const FReal Thickness, FMTDInfo* OutMTD = nullptr) const;
	bool OverlapGeom(const TImplicitObjectScaled<TBox<FReal, 3>>& QueryGeom, const FRigidTransform3& QueryTM, const FReal Thickness, FMTDInfo* OutMTD = nullptr) const;
	bool OverlapGeom(const TImplicitObjectScaled<TCapsule<FReal>>& QueryGeom, const FRigidTransform3& QueryTM, const FReal Thickness, FMTDInfo* OutMTD = nullptr) const;
	bool OverlapGeom(const TImplicitObjectScaled<FConvex>& QueryGeom, const FRigidTransform3& QueryTM, const FReal Thickness, FMTDInfo* OutMTD = nullptr) const;

	template<typename QueryGeomType>
	bool OverlapGeomImp(const QueryGeomType& QueryGeom, const FRigidTransform3& QueryTM, const FReal Thickness, FMTDInfo* OutMTD = nullptr) const;

  private:
	bool ComputeDistancesNearZeroIsocontour(FErrorReporter& ErrorReporter, const TParticles<T, d>& InParticles, const TArray<TVector<T, 3>> &Normals, const TTriangleMesh<T>& Mesh, TArrayND<bool, d>& BlockedFaceX, TArrayND<bool, d>& BlockedFaceY, TArrayND<bool, d>& BlockedFaceZ, TArray<TVector<int32, d>>& InterfaceIndices);
	void ComputeDistancesNearZeroIsocontour(const FImplicitObject& Object, const TArrayND<T, d>& ObjectPhi, TArray<TVector<int32, d>>& InterfaceIndices);
	void CorrectSign(const TArrayND<bool, d>& BlockedFaceX, const TArrayND<bool, d>& BlockedFaceY, const TArrayND<bool, d>& BlockedFaceZ, TArray<TVector<int32, d>>& InterfaceIndices);
	T ComputePhi(const TArrayND<bool, d>& Done, const TVector<int32, d>& CellIndex);
	void FillWithFastMarchingMethod(const T StoppingDistance, const TArray<TVector<int32, d>>& InterfaceIndices);
	void FloodFill(const TArrayND<bool, d>& BlockedFaceX, const TArrayND<bool, d>& BlockedFaceY, const TArrayND<bool, d>& BlockedFaceZ, TArrayND<int32, d>& Color, int32& NextColor);
	void FloodFillFromCell(const TVector<int32, d> CellIndex, const int32 NextColor, const TArrayND<bool, d>& BlockedFaceX, const TArrayND<bool, d>& BlockedFaceY, const TArrayND<bool, d>& BlockedFaceZ, TArrayND<int32, d>& Color);
	bool IsIntersectingWithTriangle(const TParticles<T, d>& Particles, const TVector<int32, 3>& Elements, const TPlane<T, d>& TrianglePlane, const TVector<int32, d>& CellIndex, const TVector<int32, d>& PrevCellIndex);
	void ComputeNormals();
	void ComputeConvexity(const TArray<TVector<int32, d>>& InterfaceIndices);
	
	void ComputeNormals(const TParticles<T, d>& InParticles, const TTriangleMesh<T>& Mesh, const TArray<TVector<int32, d>>& InterfaceIndices);

	TUniformGrid<T, d> MGrid;
	TArrayND<T, d> MPhi;
	TArrayND<TVector<T, d>, d> MNormals;
	TAABB<T, d> MLocalBoundingBox;
	TAABB<T, d> MOriginalLocalBoundingBox;
	int32 MBandWidth;
private:
	TLevelSet() : FImplicitObject(EImplicitObject::HasBoundingBox, ImplicitObjectType::LevelSet) {}	//needed for serialization
	friend FImplicitObject;	//needed for serialization
};
}
