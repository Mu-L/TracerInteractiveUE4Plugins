// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Array.h"
#include "ImplicitObject.h"
#include "Box.h"
#include "TriangleMeshImplicitObject.h"
#include "ChaosArchive.h"
#include "Serialization/MemoryWriter.h"
#include "Math/NumericLimits.h"
#include "Templates/UnrealTypeTraits.h"
#include "UniformGrid.h"
#include "Utilities.h"
#include "UObject/ExternalPhysicsCustomObjectVersion.h"

namespace Chaos
{
	class FHeightfieldRaycastVisitor;
	class FConvex;
	struct FMTDInfo;
}

namespace Chaos
{
	class CHAOS_API FHeightField final : public FImplicitObject
	{
	public:
		using FImplicitObject::GetTypeName;

		FHeightField(TArray<FReal>&& Height, TArray<uint8>&& InMaterialIndices, int32 InNumRows, int32 InNumCols, const FVec3& InScale);
		FHeightField(TArrayView<const uint16> InHeights, TArrayView<uint8> InMaterialIndices, int32 InNumRows, int32 InNumCols, const FVec3& InScale);
		FHeightField(const FHeightField& Other) = delete;
		
		// Not required as long as FImplicitObject also has deleted move constructor (adding this causes an error on Linux build)
		//FHeightField(FHeightField&& Other) = default;

		virtual ~FHeightField() {}

		/** Support for editing a subsection of the heightfield */
		void EditHeights(TArrayView<FReal> InHeights, int32 InBeginRow, int32 InBeginCol, int32 InNumRows, int32 InNumCols);
		void EditHeights(TArrayView<const uint16> InHeights, int32 InBeginRow, int32 InBeginCol, int32 InNumRows, int32 InNumCols);
		FReal GetHeight(int32 InIndex) const;
		FReal GetHeight(int32 InX, int32 InY) const;
		uint8 GetMaterialIndex(int32 InIndex) const;
		uint8 GetMaterialIndex(int32 InX, int32 InY) const;
		bool IsHole(int32 InIndex) const;
		bool IsHole(int32 InCellX, int32 InCellY) const;
		FVec3 GetNormalAt(const TVector<FReal, 2>& InGridLocationLocal) const;
		FReal GetHeightAt(const TVector<FReal, 2>& InGridLocationLocal) const;

		int32 GetNumRows() const { return GeomData.NumRows; }
		int32 GetNumCols() const { return GeomData.NumCols; }

		virtual FReal PhiWithNormal(const FVec3& x, FVec3& Normal) const;

		virtual bool Raycast(const FVec3& StartPoint, const FVec3& Dir, const FReal Length, const FReal Thickness, FReal& OutTime, FVec3& OutPosition, FVec3& OutNormal, int32& OutFaceIndex) const override;
		virtual bool Overlap(const FVec3& Point, const FReal Thickness) const override;
		
		bool OverlapGeom(const TSphere<FReal, 3>& QueryGeom, const FRigidTransform3& QueryTM, const FReal Thickness, FMTDInfo* OutMTD = nullptr) const;
		bool OverlapGeom(const TBox<FReal, 3>& QueryGeom, const FRigidTransform3& QueryTM, const FReal Thickness, FMTDInfo* OutMTD = nullptr) const;
		bool OverlapGeom(const TCapsule<FReal>& QueryGeom, const FRigidTransform3& QueryTM, const FReal Thickness, FMTDInfo* OutMTD = nullptr) const;
		bool OverlapGeom(const FConvex& QueryGeom, const FRigidTransform3& QueryTM, const FReal Thickness, FMTDInfo* OutMTD = nullptr) const;
		bool OverlapGeom(const TImplicitObjectScaled<TSphere<FReal, 3>>& QueryGeom, const FRigidTransform3& QueryTM, const FReal Thickness, FMTDInfo* OutMTD = nullptr) const;
		bool OverlapGeom(const TImplicitObjectScaled<TBox<FReal, 3>>& QueryGeom, const FRigidTransform3& QueryTM, const FReal Thickness, FMTDInfo* OutMTD = nullptr) const;
		bool OverlapGeom(const TImplicitObjectScaled<TCapsule<FReal>>& QueryGeom, const FRigidTransform3& QueryTM, const FReal Thickness, FMTDInfo* OutMTD = nullptr) const;
		bool OverlapGeom(const TImplicitObjectScaled<FConvex>& QueryGeom, const FRigidTransform3& QueryTM, const FReal Thickness, FMTDInfo* OutMTD = nullptr) const;

		bool SweepGeom(const TSphere<FReal, 3>& QueryGeom, const FRigidTransform3& StartTM, const FVec3& Dir, const FReal Length, FReal& OutTime, FVec3& OutPosition, FVec3& OutNormal, int32& OutFaceIndex, const FReal Thickness = 0, bool bComputeMTD = false) const;
		bool SweepGeom(const TBox<FReal, 3>& QueryGeom, const FRigidTransform3& StartTM, const FVec3& Dir, const FReal Length, FReal& OutTime, FVec3& OutPosition, FVec3& OutNormal, int32& OutFaceIndex, const FReal Thickness = 0, bool bComputeMTD = false) const;
		bool SweepGeom(const TCapsule<FReal>& QueryGeom, const FRigidTransform3& StartTM, const FVec3& Dir, const FReal Length, FReal& OutTime, FVec3& OutPosition, FVec3& OutNormal, int32& OutFaceIndex, const FReal Thickness = 0, bool bComputeMTD = false) const;
		bool SweepGeom(const FConvex& QueryGeom, const FRigidTransform3& StartTM, const FVec3& Dir, const FReal Length, FReal& OutTime, FVec3& OutPosition, FVec3& OutNormal, int32& OutFaceIndex, const FReal Thickness = 0, bool bComputeMTD = false) const;
		bool SweepGeom(const TImplicitObjectScaled<TSphere<FReal, 3>>& QueryGeom, const FRigidTransform3& StartTM, const FVec3& Dir, const FReal Length, FReal& OutTime, FVec3& OutPosition, FVec3& OutNormal, int32& OutFaceIndex, const FReal Thickness = 0, bool bComputeMTD = false) const;
		bool SweepGeom(const TImplicitObjectScaled<TBox<FReal, 3>>& QueryGeom, const FRigidTransform3& StartTM, const FVec3& Dir, const FReal Length, FReal& OutTime, FVec3& OutPosition, FVec3& OutNormal, int32& OutFaceIndex, const FReal Thickness = 0, bool bComputeMTD = false) const;
		bool SweepGeom(const TImplicitObjectScaled<TCapsule<FReal>>& QueryGeom, const FRigidTransform3& StartTM, const FVec3& Dir, const FReal Length, FReal& OutTime, FVec3& OutPosition, FVec3& OutNormal, int32& OutFaceIndex, const FReal Thickness = 0, bool bComputeMTD = false) const;
		bool SweepGeom(const TImplicitObjectScaled<FConvex>& QueryGeom, const FRigidTransform3& StartTM, const FVec3& Dir, const FReal Length, FReal& OutTime, FVec3& OutPosition, FVec3& OutNormal, int32& OutFaceIndex, const FReal Thickness = 0, bool bComputeMTD = false) const;

		bool GJKContactPoint(const TBox<FReal, 3>& QueryGeom, const FRigidTransform3& QueryTM, const FReal Thickness, FVec3& ContactLocation, FVec3& ContactNormal, FReal& ContactPhi) const;
		bool GJKContactPoint(const TSphere<FReal, 3>& QueryGeom, const FRigidTransform3& QueryTM, const FReal Thickness, FVec3& ContactLocation, FVec3& ContactNormal, FReal& ContactPhi) const;
		bool GJKContactPoint(const TCapsule<FReal>& QueryGeom, const FRigidTransform3& QueryTM, const FReal Thickness, FVec3& ContactLocation, FVec3& ContactNormal, FReal& ContactPhi) const;
		bool GJKContactPoint(const FConvex& QueryGeom, const FRigidTransform3& QueryTM, const FReal Thickness, FVec3& ContactLocation, FVec3& ContactNormal, FReal& ContactPhi) const;
		bool GJKContactPoint(const TImplicitObjectScaled<TBox<FReal, 3>>& QueryGeom, const FRigidTransform3& QueryTM, const FReal Thickness, FVec3& ContactLocation, FVec3& ContactNormal, FReal& ContactPhi) const;
		bool GJKContactPoint(const TImplicitObjectScaled<TSphere<FReal, 3>>& QueryGeom, const FRigidTransform3& QueryTM, const FReal Thickness, FVec3& ContactLocation, FVec3& ContactNormal, FReal& ContactPhi) const;
		bool GJKContactPoint(const TImplicitObjectScaled<TCapsule<FReal>>& QueryGeom, const FRigidTransform3& QueryTM, const FReal Thickness, FVec3& ContactLocation, FVec3& ContactNormal, FReal& ContactPhi) const;
		bool GJKContactPoint(const TImplicitObjectScaled<FConvex>& QueryGeom, const FRigidTransform3& QueryTM, const FReal Thickness, FVec3& ContactLocation, FVec3& ContactNormal, FReal& ContactPhi) const;


		virtual int32 FindMostOpposingFace(const FVec3& Position, const FVec3& UnitDir, int32 HintFaceIndex, FReal SearchDist) const override;
		virtual FVec3 FindGeometryOpposingNormal(const FVec3& DenormDir, int32 FaceIndex, const FVec3& OriginalNormal) const override;

		struct FClosestFaceData
		{
			int32 FaceIndex = INDEX_NONE;
			FReal DistanceToFaceSq = TNumericLimits<FReal>::Max();
			bool bWasSampleBehind = false;
		};

		FClosestFaceData FindClosestFace(const FVec3& Position, FReal SearchDist) const;
		
		virtual uint16 GetMaterialIndex(uint32 HintIndex) const override
		{
			ensure(GeomData.MaterialIndices.Num() > 0);

			// If we've only got a default
			if(GeomData.MaterialIndices.Num() == 1)
			{
				return GeomData.MaterialIndices[0];
			}
			else
			{
				// We store per cell for materials, so change to cell index
				int32 CellIndex = HintIndex / 2;
				if(GeomData.MaterialIndices.IsValidIndex(CellIndex))
				{
					return GeomData.MaterialIndices[CellIndex];
				}
			}
			
			// INDEX_NONE will be out of bounds but it is an expected value. If we reach this section of the code and the index isn't INDEX_NONE, we have an issue
			ensureMsgf(HintIndex == INDEX_NONE,TEXT("GetMaterialIndex called with an invalid MaterialIndex => %d"),HintIndex);
			
			return 0;
		}

		virtual const FAABB3 BoundingBox() const
		{
			CachedBounds = FAABB3(LocalBounds.Min() * GeomData.Scale, LocalBounds.Max() * GeomData.Scale);
			return CachedBounds;
		}

		virtual uint32 GetTypeHash() const override
		{
			TArray<uint8> Bytes;
			FMemoryWriter Writer(Bytes);
			FChaosArchive ChaosAr(Writer);

			// Saving to an archive is a const operation, but must be non-const
			// to support loading. Cast const away here to get bytes written
			const_cast<FHeightField*>(this)->Serialize(ChaosAr);

			return FCrc::MemCrc32(Bytes.GetData(), Bytes.GetAllocatedSize());
		}

		static constexpr EImplicitObjectType StaticType()
		{
			return ImplicitObjectType::HeightField;
		}

		virtual void Serialize(FChaosArchive& Ar) override
		{
			FChaosArchiveScopedMemory ScopedMemory(Ar, GetTypeName());
			FImplicitObject::SerializeImp(Ar);
			
			GeomData.Serialize(Ar);

			Ar.UsingCustomVersion(FExternalPhysicsCustomObjectVersion::GUID);
			if (Ar.CustomVer(FExternalPhysicsCustomObjectVersion::GUID) >= FExternalPhysicsCustomObjectVersion::HeightfieldData)
			{
				Ar << FlatGrid;
				Ar << FlattenedBounds.Min;
				Ar << FlattenedBounds.Max;
				TBox<FReal, 3>::SerializeAsAABB(Ar, LocalBounds);
			}
			else
			{
				CalcBounds();
			}
			

			if(Ar.IsLoading())
			{
				BuildQueryData();
				BoundingBox();	//temp hack to initialize cache
			}
		}

		void SetScale(const FVec3& InScale)
		{
			GeomData.Scale = InScale;
		}

		template<typename InStorageType, typename InRealType>
		struct FData
		{
			// For ease of access through typedefs
			using StorageType = InStorageType;
			using RealType = InRealType;

			// Only supporting unsigned int types for the height range - really no difference using
			// this or signed but this is a little nicer overall
			static_assert(TIsSame<StorageType, uint8>::Value || 
				TIsSame<StorageType, uint16>::Value || 
				TIsSame<StorageType, uint32>::Value || 
				TIsSame<StorageType, uint64>::Value,
				"Expected unsigned integer type for heightfield data storage");

			// Data sizes to validate during serialization
			static constexpr int32 RealSize = sizeof(RealType);
			static constexpr int32 StorageSize = sizeof(StorageType);

			// Range of the chosen type (unsigned so Min is always 0)
			static constexpr int32 StorageRange = TNumericLimits<StorageType>::Max();

			// Heights in the chosen format. final placement of the vertex will be at
			// MinValue + Heights[Index] * HeightPerUnit
			// With HeightPerUnit being the range of the min/max realtype values of
			// the heightfield divided by the range of StorageType
			TArray<StorageType> Heights;
			TArray<uint8> MaterialIndices;
			TVector<RealType, 3> Scale;
			RealType MinValue;
			RealType MaxValue;
			uint16 NumRows;
			uint16 NumCols;
			RealType Range;
			RealType HeightPerUnit;

			constexpr float GetCellWidth() const
			{
				return Scale[0];
			}

			constexpr float GetCellHeight() const
			{
				return Scale[1];
			}

			FORCEINLINE FVec3 GetPoint(int32 Index) const
			{
				const typename FDataType::RealType Height = MinValue + Heights[Index] * HeightPerUnit;

				const int32 X = Index % (NumCols);
				const int32 Y = Index / (NumCols);

				return {(typename FDataType::RealType)X, (typename FDataType::RealType)Y, Height};
			}

			FORCEINLINE FVec3 GetPointScaled(int32 Index) const
			{
				return GetPoint(Index) * Scale;
			}

			FORCEINLINE void GetPoints(int32 Index, FVec3 OutPts[4]) const
			{
				const typename FDataType::RealType H0 = MinValue + Heights[Index] * HeightPerUnit;
				const typename FDataType::RealType H1 = MinValue + Heights[Index + 1] * HeightPerUnit;
				const typename FDataType::RealType H2 = MinValue + Heights[Index + NumCols] * HeightPerUnit;
				const typename FDataType::RealType H3 = MinValue + Heights[Index + NumCols + 1] * HeightPerUnit;

				const int32 X = Index % (NumCols);
				const int32 Y = Index / (NumCols);

				OutPts[0] = {(typename FDataType::RealType)X, (typename FDataType::RealType)Y, H0};
				OutPts[1] = {(typename FDataType::RealType)X + 1, (typename FDataType::RealType)Y, H1};
				OutPts[2] = {(typename FDataType::RealType)X, (typename FDataType::RealType)Y + 1, H2};
				OutPts[3] = {(typename FDataType::RealType)X + 1, (typename FDataType::RealType)Y + 1, H3};
			}

			FORCEINLINE void GetPointsScaled(int32 Index, FVec3 OutPts[4]) const
			{
				GetPoints(Index, OutPts);

				OutPts[0] *= Scale;
				OutPts[1] *= Scale;
				OutPts[2] *= Scale;
				OutPts[3] *= Scale;
			}

			FORCEINLINE FReal GetMinHeight() const
			{
				return static_cast<typename FDataType::RealType>(MinValue);
			}

			FORCEINLINE FReal GetMaxHeight() const
			{
				return static_cast<typename FDataType::RealType>(MaxValue);
			}

			void Serialize(FChaosArchive& Ar)
			{
				int32 TempRealSize = RealSize;
				int32 TempStorageSize = StorageSize;

				Ar << TempRealSize;
				Ar << TempStorageSize;

				if(Ar.IsLoading())
				{
					checkf(TempRealSize == RealSize, TEXT("Heightfield was serialized with mismatched real type size (expected: %d, found: %d)"), RealSize, TempRealSize);
					checkf(TempStorageSize == StorageSize, TEXT("Heightfield was serialized with mismatched storage type size (expected: %d, found: %d)"), StorageSize, TempStorageSize);
				}
				
				Ar << Heights;
				Ar << Scale;
				Ar << MinValue;
				Ar << MaxValue;
				Ar << NumRows;
				Ar << NumCols;

				Ar.UsingCustomVersion(FExternalPhysicsCustomObjectVersion::GUID);
				if (Ar.CustomVer(FExternalPhysicsCustomObjectVersion::GUID) >= FExternalPhysicsCustomObjectVersion::HeightfieldData)
				{
					Ar << Range;
					Ar << HeightPerUnit;

					if (Ar.CustomVer(FExternalPhysicsCustomObjectVersion::GUID) < FExternalPhysicsCustomObjectVersion::HeightfieldImplicitBounds)
					{
						TArray<TBox<RealType, 3>> CellBounds;
						Ar << CellBounds;
					}
					else if(Ar.CustomVer(FExternalPhysicsCustomObjectVersion::GUID) < FExternalPhysicsCustomObjectVersion::HeightfieldUsesHeightsDirectly)
					{
						TArray<RealType> OldHeights;
						Ar << OldHeights;
					}
				}

				if(Ar.CustomVer(FExternalPhysicsCustomObjectVersion::GUID) >= FExternalPhysicsCustomObjectVersion::AddedMaterialManager)
				{
					Ar << MaterialIndices;
				}
			}
		};

		using FDataType = FData<uint16, float>;
		FDataType GeomData;

	private:

		// Struct for 2D bounds and associated operations
		struct FBounds2D
		{
			TVector<FReal, 2> Min;
			TVector<FReal, 2> Max;
			
			FBounds2D()
				: Min(0)
				, Max(0)
			{}

			explicit FBounds2D(const FAABB3& In3DBounds)
			{
				Set(In3DBounds);
			}

			void Set(const FAABB3& In3DBounds)
			{
				Min = {In3DBounds.Min()[0], In3DBounds.Min()[1]};
				Max = {In3DBounds.Max()[0], In3DBounds.Max()[1]};
			}

			TVector<FReal, 2> GetExtent() const
			{
				return Max - Min;
			}

			bool IsInside(const TVector<FReal, 2>& InPoint) const
			{
				return InPoint[0] >= Min[0] && InPoint[0] <= Max[0] && InPoint[1] >= Min[1] && InPoint[1] <= Max[1];
			}

			TVector<FReal, 2> Clamp(const TVector<FReal, 2>& InToClamp, FReal InNudge = SMALL_NUMBER) const
			{
				const TVector<FReal, 2> NudgeVec(InNudge, InNudge);
				const TVector<FReal, 2> TestMin = Min + NudgeVec;
				const TVector<FReal, 2> TestMax = Max - NudgeVec;

				TVector<FReal, 2> OutVec = InToClamp;

				OutVec[0] = FMath::Max(OutVec[0], TestMin[0]);
				OutVec[1] = FMath::Max(OutVec[1], TestMin[1]);

				OutVec[0] = FMath::Min(OutVec[0], TestMax[0]);
				OutVec[1] = FMath::Min(OutVec[1], TestMax[1]);

				return OutVec;
			}

			bool IntersectLine(const TVector<FReal, 2>& InStart, const TVector<FReal, 2>& InEnd)
			{
				if(IsInside(InStart) || IsInside(InEnd))
				{
					return true;
				}

				const TVector<FReal, 2> Extent = GetExtent();
				float TA, TB;

				if(Utilities::IntersectLineSegments2D(InStart, InEnd, Min, TVector<FReal, 2>(Min[0] + Extent[0], Min[1]), TA, TB)
					|| Utilities::IntersectLineSegments2D(InStart, InEnd, Min, TVector<FReal, 2>(Min[0], Min[1] + Extent[1]), TA, TB)
					|| Utilities::IntersectLineSegments2D(InStart, InEnd, Max, TVector<FReal, 2>(Max[0] - Extent[0], Max[1]), TA, TB)
					|| Utilities::IntersectLineSegments2D(InStart, InEnd, Max, TVector<FReal, 2>(Max[0], Max[1] - Extent[1]), TA, TB))
				{
					return true;
				}

				return false;
			}

			bool ClipLine(const FVec3& InStart, const FVec3& InEnd, TVector<FReal, 2>& OutClippedStart, TVector<FReal, 2>& OutClippedEnd) const
			{
				TVector<FReal, 2> TempStart(InStart[0], InStart[1]);
				TVector<FReal, 2> TempEnd(InEnd[0], InEnd[1]);

				bool bLineIntersects = ClipLine(TempStart, TempEnd);

				OutClippedStart = TempStart;
				OutClippedEnd = TempEnd;

				return bLineIntersects;
			}

			bool ClipLine(TVector<FReal, 2>& InOutStart, TVector<FReal, 2>& InOutEnd) const
			{
				
				// Test we don't need to clip at all, quite likely with a heightfield so optimize for it.
				const bool bStartInside = IsInside(InOutStart);
				const bool bEndInside = IsInside(InOutEnd);
				if(bStartInside && bEndInside)
				{
					return true;
				}

				const TVector<FReal, 2> Dir = InOutEnd - InOutStart;

				// Tiny ray not inside so must be outside
				if(Dir.SizeSquared() < 1e-4)
				{
					return false;
				}

				bool bPerpendicular[2];
				TVector<FReal, 2> InvDir;
				for(int Axis = 0; Axis < 2; ++Axis)
				{
					bPerpendicular[Axis] = Dir[Axis] == 0;
					InvDir[Axis] = bPerpendicular[Axis] ? 0 : 1 / Dir[Axis];
				}

				

				if(bStartInside)
				{
					const FReal TimeToExit = ComputeTimeToExit(InOutStart,InvDir);
					InOutEnd = InOutStart + Dir * TimeToExit;
					return true;
				}

				if(bEndInside)
				{
					const FReal TimeToExit = ComputeTimeToExit(InOutEnd,-InvDir);
					InOutStart = InOutEnd - Dir * TimeToExit;
					return true;
				}

				//start and end outside, need to see if we even intersect
				FReal TimesToEnter[2] = {TNumericLimits<FReal>::Max(),TNumericLimits<FReal>::Max()};
				FReal TimesToExit[2] = {TNumericLimits<FReal>::Max(),TNumericLimits<FReal>::Max()};
				
				for(int Axis = 0; Axis < 2; ++Axis)
				{
					if(bPerpendicular[Axis])
					{
						if(InOutStart[Axis] >= Min[Axis] && InOutStart[Axis] <= Max[Axis])
						{
							TimesToEnter[Axis] = 0;
						}
					}
					else
					{
						if(Dir[Axis] > 0)
						{
							if(InOutStart[Axis] <= Max[Axis])
							{
								TimesToEnter[Axis] = FMath::Max<FReal>(Min[Axis] - InOutStart[Axis], 0) * InvDir[Axis];
								TimesToExit[Axis] = (Max[Axis] - InOutStart[Axis])  * InvDir[Axis];
							}
						}
						else if(Dir[Axis] < 0)
						{
							if(InOutStart[Axis] >= Min[Axis])
							{
								TimesToEnter[Axis] = FMath::Max<FReal>(InOutStart[Axis] - Max[Axis],0) * InvDir[Axis];
								TimesToExit[Axis] = (InOutStart[Axis] - Min[Axis]) * InvDir[Axis];
							}
						}
					}
				}

				const FReal TimeToEnter = FMath::Max(FMath::Abs(TimesToEnter[0]),FMath::Abs(TimesToEnter[1]));
				const FReal TimeToExit = FMath::Min(FMath::Abs(TimesToExit[0]),FMath::Abs(TimesToExit[1]));

				if(TimeToExit < TimeToEnter)
				{
					//no intersection
					return false;
				}

				InOutEnd = InOutStart + Dir * TimeToExit;
				InOutStart = InOutStart + Dir * TimeToEnter;
				return true;
			}

		private:
			//This helper assumes Start is inside the min/max box and uses InvDir to compute how long it takes to exit
			FReal ComputeTimeToExit(const TVector<FReal, 2>& Start,const TVector<FReal, 2>& InvDir) const
			{
				FReal Times[2] ={TNumericLimits<FReal>::Max(),TNumericLimits<FReal>::Max()};
				for(int Axis = 0; Axis < 2; ++Axis)
				{
					if(InvDir[Axis] > 0)
					{
						Times[Axis] = (Max[Axis] - Start[Axis]) * InvDir[Axis];
					}
					else if(InvDir[Axis] < 0)
					{
						Times[Axis] = (Start[Axis] - Min[Axis]) * InvDir[Axis];
					}
				}

				const FReal MinTime = FMath::Min(FMath::Abs(Times[0]),FMath::Abs(Times[1]));
				return MinTime;
			}
		};

		// Helpers for accessing bounds
		bool GetCellBounds2D(const TVector<int32, 2> InCoord, FBounds2D& OutBounds, const TVector<FReal, 2>& InInflate = {0}) const;
		bool GetCellBounds3D(const TVector<int32, 2> InCoord, FVec3& OutMin, FVec3& OutMax, const FVec3& InInflate = FVec3(0)) const;
		bool GetCellBounds2DScaled(const TVector<int32, 2> InCoord, FBounds2D& OutBounds, const TVector<FReal, 2>& InInflate = {0}) const;
		bool GetCellBounds3DScaled(const TVector<int32, 2> InCoord, FVec3& OutMin, FVec3& OutMax, const FVec3& InInflate = FVec3(0)) const;
		bool CalcCellBounds3D(const TVector<int32, 2> InCoord, FVec3& OutMin, FVec3& OutMax, const FVec3& InInflate = FVec3(0)) const;

		// Query functions - sweep, ray, overlap
		template<typename SQVisitor>
		bool GridSweep(const FVec3& StartPoint, const FVec3& Dir, const FReal Length, const TVector<FReal, 2> InHalfExtents, SQVisitor& Visitor) const;
		bool GridCast(const FVec3& StartPoint, const FVec3& Dir, const FReal Length, FHeightfieldRaycastVisitor& Visitor) const;
		bool GetGridIntersections(FBounds2D InFlatBounds, TArray<TVector<int32, 2>>& OutInterssctions) const;
		
		FBounds2D GetFlatBounds() const;

		// Grid for queries, faster than bounding volumes for heightfields
		TUniformGrid<FReal, 2> FlatGrid;
		// Bounds in 2D of the whole heightfield, to clip queries against
		FBounds2D FlattenedBounds;
		// 3D bounds for the heightfield, for insertion to the scene structure
		FAABB3 LocalBounds;
		// Cached when bounds are requested. Mutable to allow GetBounds to be logical const
		mutable FAABB3 CachedBounds;

		void CalcBounds();
		void BuildQueryData();

		// Needed for serialization
		FHeightField() : FImplicitObject(EImplicitObject::HasBoundingBox, ImplicitObjectType::HeightField) {}
		friend FImplicitObject;

		template <typename QueryGeomType>
		bool OverlapGeomImp(const QueryGeomType& QueryGeom, const FRigidTransform3& QueryTM, const FReal Thickness, FMTDInfo* OutMTD = nullptr) const;

		template <typename QueryGeomType>
		bool SweepGeomImp(const QueryGeomType& QueryGeom, const FRigidTransform3& StartTM, const FVec3& Dir, const FReal Length, FReal& OutTime, FVec3& OutPosition, FVec3& OutNormal, int32& OutFaceIndex, const FReal Thickness, bool bComputeMTD) const;

		template <typename GeomType>
		bool GJKContactPointImp(const GeomType& QueryGeom, const FRigidTransform3& QueryTM, const FReal Thickness, FVec3& ContactLocation, FVec3& ContactNormal, FReal& ContactPhi) const;

	};
}
