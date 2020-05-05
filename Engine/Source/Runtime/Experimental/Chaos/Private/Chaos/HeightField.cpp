// Copyright Epic Games, Inc. All Rights Reserved.
#include "Chaos/HeightField.h"
#include "Chaos/Convex.h"
#include "Chaos/Box.h"
#include "Chaos/GJK.h"
#include "Chaos/ImplicitObjectScaled.h"
#include "Chaos/Capsule.h"
#include "Chaos/GeometryQueries.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "Chaos/Triangle.h"

namespace Chaos
{

	template<typename T>
	class THeightfieldRaycastVisitor
	{
	public:

		THeightfieldRaycastVisitor(const typename THeightField<T>::FDataType* InData, const TVector<T, 3>& InStart, const TVector<T, 3>& InDir, const T InThickness)
			: OutTime(TNumericLimits<T>::Max())
			, OutFaceIndex(INDEX_NONE)
			, GeomData(InData)
			, Start(InStart)
			, Dir(InDir)
			, Thickness(InThickness)
		{}

		enum class ERaycastType
		{
			Raycast,
			Sweep
		};

		template<ERaycastType SQType>
		bool Visit(int32 Payload, T& CurrentLength)
		{
			const int32 SubX = Payload % (GeomData->NumCols - 1);
			const int32 SubY = Payload / (GeomData->NumCols - 1);

			const int32 FullIndex = Payload + SubY;

			const T Radius = Thickness + SMALL_NUMBER;
			const T Radius2 = Radius * Radius;
			bool bIntersection = false;

			auto TestTriangle = [&](int32 FaceIndex, const TVector<T, 3>& A, const TVector<T, 3>& B, const TVector<T, 3>& C) -> bool
			{
				const TVector<T, 3> AB = B - A;
				const TVector<T, 3> AC = C - A;

				TVector<T, 3> Normal = TVector<T, 3>::CrossProduct(AB, AC);
				const T Len2 = Normal.SafeNormalize();

				if(!ensure(Len2 > SMALL_NUMBER))
				{
					// Bad triangle, co-linear points or very thin
					return true;
				}

				const TPlane<T, 3> TrianglePlane(A, Normal);

				TVector<T, 3> ResultPosition(0);
				TVector<T, 3> ResultNormal(0);
				T Time = TNumericLimits<T>::Max();
				int32 DummyFaceIndex = INDEX_NONE;

				if(TrianglePlane.Raycast(Start, Dir, CurrentLength, Thickness, Time, ResultPosition, ResultNormal, DummyFaceIndex))
				{
					if(Time == 0)
					{
						// Initial overlap
						const TVector<T, 3> ClosestPtOnTri = FindClosestPointOnTriangle(TrianglePlane, A, B, C, Start);
						const T DistToTriangle2 = (Start - ClosestPtOnTri).SizeSquared();
						if(DistToTriangle2 <= Radius2)
						{
							OutTime = 0;
							OutPosition = ClosestPtOnTri;
							OutNormal = Normal;
							OutFaceIndex = FaceIndex;
							return false;
						}
					}
					else
					{
						const TVector<T, 3> ClosestPtOnTri = FindClosestPointOnTriangle(ResultPosition, A, B, C, ResultPosition);
						const T DistToTriangle2 = (ResultPosition - ClosestPtOnTri).SizeSquared();
						bIntersection = DistToTriangle2 <= SMALL_NUMBER;
					}
				}

				if(SQType == ERaycastType::Sweep && !bIntersection)
				{
					//sphere is not immediately touching the triangle, but it could start intersecting the perimeter as it sweeps by
					TVector<T, 3> BorderPositions[3];
					TVector<T, 3> BorderNormals[3];
					T BorderTimes[3];
					bool bBorderIntersections[3];

					const TCapsule<T> ABCapsule(A, B, Thickness);
					bBorderIntersections[0] = ABCapsule.Raycast(Start, Dir, CurrentLength, 0, BorderTimes[0], BorderPositions[0], BorderNormals[0], DummyFaceIndex);

					const TCapsule<T> BCCapsule(B, C, Thickness);
					bBorderIntersections[1] = BCCapsule.Raycast(Start, Dir, CurrentLength, 0, BorderTimes[1], BorderPositions[1], BorderNormals[1], DummyFaceIndex);

					const TCapsule<T> ACCapsule(A, C, Thickness);
					bBorderIntersections[2] = ACCapsule.Raycast(Start, Dir, CurrentLength, 0, BorderTimes[2], BorderPositions[2], BorderNormals[2], DummyFaceIndex);

					int32 MinBorderIdx = INDEX_NONE;
					T MinBorderTime = 0;

					for(int32 BorderIdx = 0; BorderIdx < 3; ++BorderIdx)
					{
						if(bBorderIntersections[BorderIdx])
						{
							if(!bIntersection || BorderTimes[BorderIdx] < MinBorderTime)
							{
								MinBorderTime = BorderTimes[BorderIdx];
								MinBorderIdx = BorderIdx;
								bIntersection = true;
							}
						}
					}

					if(MinBorderIdx != INDEX_NONE)
					{
						ResultNormal = BorderNormals[MinBorderIdx];
						ResultPosition = BorderPositions[MinBorderIdx] - ResultNormal * Thickness;

						if(Time == 0)
						{
							//we were initially overlapping with triangle plane so no normal was given. Compute it now
							TVector<T, 3> TmpNormal;
							const T SignedDistance = TrianglePlane.PhiWithNormal(Start, TmpNormal);
							ResultNormal = SignedDistance >= 0 ? TmpNormal : -TmpNormal;
						}

						Time = MinBorderTime;
					}
				}

				if(bIntersection)
				{
					if(Time < OutTime)
					{
						bool bHole = false;

						const int32 CellIndex = FaceIndex / 2;
						if(GeomData->MaterialIndices.IsValidIndex(CellIndex))
						{
							bHole = GeomData->MaterialIndices[CellIndex] == TNumericLimits<uint8>::Max();
						}

						if(!bHole)
						{
							OutPosition = ResultPosition;
							OutNormal = ResultNormal;
							OutTime = Time;
							OutFaceIndex = FaceIndex;
							CurrentLength = Time;
						}
					}
				}

				return true;
			};

			TVector<T, 3> Points[4];
			GeomData->GetPointsScaled(FullIndex, Points);

			// Test both triangles that are in this cell, as we could hit both in any order
			TestTriangle(Payload * 2, Points[0], Points[1], Points[3]);
			TestTriangle(Payload * 2 + 1, Points[0], Points[3], Points[2]);

			return OutTime > 0;
		}

		bool VisitRaycast(int32 Payload, T& CurLength)
		{
			return Visit<ERaycastType::Raycast>(Payload, CurLength);
		}

		bool VisitSweep(int32 Payload, T& CurLength)
		{
			return Visit<ERaycastType::Sweep>(Payload, CurLength);
		}

		T OutTime;
		TVector<T, 3> OutPosition;
		TVector<T, 3> OutNormal;
		int32 OutFaceIndex;

	private:

		const typename THeightField<T>::FDataType* GeomData;

		TVector<T, 3> Start;
		TVector<T, 3> Dir;
		T Thickness;
	};

	template<typename GeomQueryType, typename T>
	class THeightfieldSweepVisitor
	{
	public:

		THeightfieldSweepVisitor(const typename THeightField<T>::FDataType* InData, const GeomQueryType& InQueryGeom, const TRigidTransform<T, 3>& InStartTM, const TVector<T, 3>& InDir, const T InThickness, bool InComputeMTD)
			: OutTime(TNumericLimits<T>::Max())
			, OutFaceIndex(INDEX_NONE)
			, HfData(InData)
			, StartTM(InStartTM)
			, OtherGeom(InQueryGeom)
			, Dir(InDir)
			, Thickness(InThickness)
			, bComputeMTD(InComputeMTD)
		{}

		bool VisitSweep(int32 Payload, T& CurrentLength)
		{
			const int32 SubX = Payload % (HfData->NumCols - 1);
			const int32 SubY = Payload / (HfData->NumCols - 1);

			const int32 FullIndex = Payload + SubY;

			auto TestTriangle = [&](int32 FaceIndex, const TVector<T, 3>& A, const TVector<T, 3>& B, const TVector<T, 3>& C) -> bool
			{
				if(OutTime == 0)
				{
					return false;
				}

				//Convert into local space of A to get better precision

				TTriangle<T> Triangle(TVec3<T>(0), B-A, C-A);

				T Time;
				TVector<T, 3> LocalHitPosition;
				TVector<T, 3> HitNormal;
				const TRigidTransform<T,3> LocalStartTM(StartTM.GetTranslation() - A,StartTM.GetRotation());
				if(GJKRaycast2<T>(Triangle, OtherGeom, LocalStartTM, Dir, CurrentLength, Time, LocalHitPosition, HitNormal, Thickness, bComputeMTD))
				{
					if(Time < OutTime)
					{
						bool bHole = false;

						const int32 CellIndex = FaceIndex / 2;
						if(HfData->MaterialIndices.IsValidIndex(CellIndex))
						{
							bHole = HfData->MaterialIndices[CellIndex] == TNumericLimits<uint8>::Max();
						}

						if(!bHole)
						{
							OutNormal = HitNormal;
							OutPosition = LocalHitPosition + A;
							OutTime = Time;
							OutFaceIndex = FaceIndex;

							if(Time <= 0) //initial overlap or MTD, so stop
							{
								// This is incorrect. To prevent objects pushing through the surface of the heightfield
								// we adopt the triangle normal but this leaves us with an incorrect MTD from the GJK call
								// above. #TODO possibly re-do GJK with a plane, or some geom vs.plane special case to solve
								// both triangles as planes 
								const TVector<T, 3> AB = B - A;
								const TVector<T, 3> AC = C - A;

								TVector<T, 3> TriNormal = TVector<T, 3>::CrossProduct(AB, AC);
								TriNormal.SafeNormalize();

								OutNormal = TriNormal;
								CurrentLength = 0;
								return false;
							}

							CurrentLength = Time;
						}
					}
				}

				return true;
			};

			TVector<T, 3> Points[4];
			HfData->GetPointsScaled(FullIndex, Points);

			bool bContinue = TestTriangle(Payload * 2, Points[0], Points[1], Points[3]);
			if (bContinue)
			{
				TestTriangle(Payload * 2 + 1, Points[0], Points[3], Points[2]);
			}

			return OutTime > 0;
		}

		T OutTime;
		TVector<T, 3> OutPosition;
		TVector<T, 3> OutNormal;
		int32 OutFaceIndex;

	private:

		const typename THeightField<T>::FDataType* HfData;
		const TRigidTransform<T, 3> StartTM;
		const GeomQueryType& OtherGeom;
		const TVector<T, 3>& Dir;
		const T Thickness;
		bool bComputeMTD;

	};

	template<typename HeightfieldT, typename BufferType>
	void BuildGeomData(TArrayView<BufferType> BufferView, TArrayView<uint8> MaterialIndexView, int32 NumRows, int32 NumCols, const TVector<HeightfieldT, 3>& InScale, TUniqueFunction<HeightfieldT(const BufferType)> ToRealFunc, typename THeightField<HeightfieldT>::FDataType& OutData, TAABB<HeightfieldT, 3>& OutBounds)
	{
		using FDataType = typename THeightField<HeightfieldT>::FDataType;

		using RealType = typename FDataType::RealType;

		const bool bHaveMaterials = MaterialIndexView.Num() > 0;
		const bool bOnlyDefaultMaterial = MaterialIndexView.Num() == 1;
		ensure(BufferView.Num() == NumRows * NumCols);
		ensure(NumRows > 1);
		ensure(NumCols > 1);

		// Populate data.
		const int32 NumHeights = BufferView.Num();
		OutData.Heights.SetNum(NumHeights);

		OutData.NumRows = NumRows;
		OutData.NumCols = NumCols;
		OutData.MinValue = ToRealFunc(BufferView[0]);
		OutData.MaxValue = ToRealFunc(BufferView[0]);
		OutData.Scale = InScale;

		for(int32 HeightIndex = 1; HeightIndex < NumHeights; ++HeightIndex)
		{
			const RealType CurrHeight = ToRealFunc(BufferView[HeightIndex]);

			if(CurrHeight > OutData.MaxValue)
			{
				OutData.MaxValue = CurrHeight;
			}
			else if(CurrHeight < OutData.MinValue)
			{
				OutData.MinValue = CurrHeight;
			}
		}

		OutData.Range = OutData.MaxValue - OutData.MinValue;
		OutData.HeightPerUnit = OutData.Range / FDataType::StorageRange;

		for(int32 HeightIndex = 0; HeightIndex < NumHeights; ++HeightIndex)
		{
			OutData.Heights[HeightIndex] = static_cast<typename FDataType::StorageType>((ToRealFunc(BufferView[HeightIndex]) - OutData.MinValue) / OutData.HeightPerUnit);

			int32 X = HeightIndex % (NumCols);
			int32 Y = HeightIndex / (NumCols);
			TVector<HeightfieldT, 3> Position(RealType(X), RealType(Y), OutData.MinValue + OutData.Heights[HeightIndex] * OutData.HeightPerUnit);
			if(HeightIndex == 0)
			{
				OutBounds = TAABB<HeightfieldT, 3>(Position * InScale, Position * InScale);
			}
			else
			{
				OutBounds.GrowToInclude(Position * InScale);
			}
		}
		OutBounds.Thicken(KINDA_SMALL_NUMBER);

		if(bHaveMaterials)
		{
			if(bOnlyDefaultMaterial)
			{
				OutData.MaterialIndices.Add(0);
			}
			else
			{
				const int32 NumCells = NumHeights - NumRows - NumCols + 1;
				ensure(MaterialIndexView.Num() == NumCells);
				OutData.MaterialIndices.Empty();
				OutData.MaterialIndices.Append(MaterialIndexView.GetData(), MaterialIndexView.Num());
			}
		}
	}

	template<typename HeightfieldT, typename BufferType>
	void EditGeomData(TArrayView<BufferType> BufferView, int32 InBeginRow, int32 InBeginCol, int32 NumRows, int32 NumCols, TUniqueFunction<HeightfieldT(const BufferType)> ToRealFunc, typename THeightField<HeightfieldT>::FDataType& OutData, TAABB<HeightfieldT, 3>& OutBounds)
	{
		using FDataType = typename THeightField<HeightfieldT>::FDataType;
		using RealType = typename FDataType::RealType;

		RealType MinValue = TNumericLimits<HeightfieldT>::Max();
		RealType MaxValue = TNumericLimits<HeightfieldT>::Min();

		for(BufferType& Value : BufferView)
		{
			MinValue = FMath::Min(MinValue, ToRealFunc(Value));
			MaxValue = FMath::Max(MaxValue, ToRealFunc(Value));
		}

		const int32 EndRow = InBeginRow + NumRows;
		const int32 EndCol = InBeginCol + NumCols;

		// If our range now falls outside of the original ranges we need to resample the whole heightfield to perform the edit.
		// Here we resample everything outside of the edit and update our ranges
		const bool bNeedsResample = MinValue < OutData.MinValue || MaxValue > OutData.MaxValue;
		if(bNeedsResample)
		{
			const RealType NewMin = FMath::Min(MinValue, OutData.MinValue);
			const RealType NewMax = FMath::Max(MaxValue, OutData.MaxValue);
			const RealType NewRange = NewMax - NewMin;
			const RealType NewHeightPerUnit = NewRange / FDataType::StorageRange;

			for(int32 RowIdx = 0; RowIdx < OutData.NumRows; ++RowIdx)
			{
				for(int32 ColIdx = 0; ColIdx < OutData.NumCols; ++ColIdx)
				{
					const int32 HeightIndex = RowIdx * OutData.NumCols + ColIdx;

					if(RowIdx >= InBeginRow && RowIdx < EndRow &&
						ColIdx >= InBeginCol && ColIdx < EndCol)
					{
						// From the new set
						const int32 NewSetIndex = (RowIdx - InBeginRow) * NumCols + (ColIdx - InBeginCol);
						OutData.Heights[HeightIndex] = static_cast<typename FDataType::StorageType>((ToRealFunc(BufferView[NewSetIndex]) - NewMin) / NewHeightPerUnit);
					}
					else
					{
						// Resample existing
						const RealType ExpandedHeight = OutData.MinValue + OutData.Heights[HeightIndex] * OutData.HeightPerUnit;
						OutData.Heights[HeightIndex] = static_cast<typename FDataType::StorageType>((ExpandedHeight - NewMin) / NewHeightPerUnit);
					}

					int32 X = HeightIndex % (OutData.NumCols);
					int32 Y = HeightIndex / (OutData.NumCols);
					TVector<HeightfieldT, 3> Position(RealType(X), RealType(Y), NewMin + OutData.Heights[HeightIndex] * NewHeightPerUnit);
					if(HeightIndex == 0)
					{
						OutBounds = TAABB<HeightfieldT, 3>(Position, Position);
					}
					else
					{
						OutBounds.GrowToInclude(Position);
					}
				}
			}

			OutBounds.Thicken(KINDA_SMALL_NUMBER);

			OutData.MinValue = NewMin;
			OutData.MaxValue = NewMax;
			OutData.HeightPerUnit = NewHeightPerUnit;
			OutData.Range = NewRange;
		}
		else
		{
			// No resample, just push new heights into the data
			for(int32 RowIdx = InBeginRow; RowIdx < EndRow; ++RowIdx)
			{
				for(int32 ColIdx = InBeginCol; ColIdx < EndCol; ++ColIdx)
				{
					const int32 HeightIndex = RowIdx * NumCols + ColIdx;
					const int32 NewSetIndex = (RowIdx - InBeginRow) * NumCols + (ColIdx - InBeginCol);
					OutData.Heights[HeightIndex] = static_cast<typename FDataType::StorageType>((ToRealFunc(BufferView[NewSetIndex]) - OutData.MinValue) / OutData.HeightPerUnit);
				}
			}
		}
	}

	template <typename T>
	THeightField<T>::THeightField(TArray<T>&& Height, TArray<uint8>&& InMaterialIndices, int32 NumRows, int32 NumCols, const TVector<T, 3>& InScale)
		: FImplicitObject(EImplicitObject::HasBoundingBox, ImplicitObjectType::HeightField)
	{
		BuildGeomData<T, T>(MakeArrayView(Height), MakeArrayView(InMaterialIndices), NumRows, NumCols, TVector<T, 3>(1), [](const T InVal) {return InVal; }, GeomData, LocalBounds);
		CalcBounds();
		SetScale(InScale);
	}

	template<typename T>
	Chaos::THeightField<T>::THeightField(TArrayView<const uint16> InHeights, TArrayView<uint8> InMaterialIndices, int32 InNumRows, int32 InNumCols, const TVector<T, 3>& InScale)
		: FImplicitObject(EImplicitObject::HasBoundingBox, ImplicitObjectType::HeightField)
	{
		TUniqueFunction<T(const uint16)> ConversionFunc = [](const T InVal) -> float
		{
			return (float)((int32)InVal - 32768);
		};

		BuildGeomData<T, const uint16>(InHeights, InMaterialIndices, InNumRows, InNumCols, TVector<T, 3>(1), MoveTemp(ConversionFunc), GeomData, LocalBounds);
		CalcBounds();
		SetScale(InScale);
	}

	template<typename T>
	void Chaos::THeightField<T>::EditHeights(TArrayView<const uint16> InHeights, int32 InBeginRow, int32 InBeginCol, int32 InNumRows, int32 InNumCols)
	{
		const int32 NumExpectedValues = InNumRows * InNumCols;
		const int32 EndRow = InBeginRow + InNumRows - 1;
		const int32 EndCol = InBeginCol + InNumCols - 1;

		if(ensure(InHeights.Num() == NumExpectedValues && InBeginRow >= 0 && InBeginCol >= 0 && EndRow < GeomData.NumRows && EndCol < GeomData.NumCols))
		{
			TUniqueFunction<T(const uint16)> ConversionFunc = [](const T InVal) -> float
			{
				return (float)((int32)InVal - 32768);
			};

			EditGeomData<T, const uint16>(InHeights, InBeginRow, InBeginCol, InNumRows, InNumCols, MoveTemp(ConversionFunc), GeomData, LocalBounds);

			// Slow and dumb. TODO: Actually fix CellHeights inside EditGeomData.
			CalcBounds();
		}
	}

	template<typename T>
	void Chaos::THeightField<T>::EditHeights(TArrayView<T> InHeights, int32 InBeginRow, int32 InBeginCol, int32 InNumRows, int32 InNumCols)
	{
		const int32 NumExpectedValues = InNumRows * InNumCols;
		const int32 EndRow = InBeginRow + InNumRows - 1;
		const int32 EndCol = InBeginCol + InNumCols - 1;

		if(ensure(InHeights.Num() == NumExpectedValues && InBeginRow >= 0 && InBeginCol >= 0 && EndRow < GeomData.NumRows && EndCol < GeomData.NumCols))
		{
			TUniqueFunction<T(T)> ConversionFunc = [](const T InVal) -> float
			{
				return InVal;
			};

			EditGeomData<T, T>(InHeights, InBeginRow, InBeginCol, InNumRows, InNumCols, MoveTemp(ConversionFunc), GeomData, LocalBounds);

			// Slow and dumb. TODO: Actually fix CellHeights inside EditGeomData.
			CalcBounds();
		}
	}

	template<typename T>
	bool Chaos::THeightField<T>::GetCellBounds2D(const TVector<int32, 2> InCoord, FBounds2D& OutBounds, const TVector<T, 2>& InInflate /*= {0}*/) const
	{
		if (FlatGrid.IsValid(InCoord))
		{
			OutBounds.Min = TVector<T, 2>(InCoord[0], InCoord[1]);
			OutBounds.Max = TVector<T, 2>(InCoord[0] + 1, InCoord[1] + 1);
			OutBounds.Min -= InInflate;
			OutBounds.Max += InInflate;

			return true;
		}

		return false;
	}

	template<typename T>
	T Chaos::THeightField<T>::GetHeight(int32 InIndex) const
	{
		if (CHAOS_ENSURE(InIndex >= 0 && InIndex < GeomData.Heights.Num()))
		{
			return GeomData.GetPoint(InIndex).Z;
		}

		return TNumericLimits<T>::Max();
	}

	template<typename T>
	T Chaos::THeightField<T>::GetHeight(int32 InX, int32 InY) const
	{
		const int32 Index = InY * GeomData.NumCols + InX;
		return GetHeight(Index);
	}

	template<typename T>
	uint8 Chaos::THeightField<T>::GetMaterialIndex(int32 InIndex) const
	{
		if(CHAOS_ENSURE(InIndex >= 0 && InIndex < GeomData.MaterialIndices.Num()))
		{
			return GeomData.MaterialIndices[InIndex];
		}

		return TNumericLimits<uint8>::Max();
	}

	template<typename T>
	uint8 Chaos::THeightField<T>::GetMaterialIndex(int32 InX, int32 InY) const
	{
		const int32 Index = InY * GeomData.NumCols + InX;
		return GetMaterialIndex(Index);
	}

	template<typename T>
	bool Chaos::THeightField<T>::IsHole(int32 InIndex) const
	{
		return GetMaterialIndex(InIndex) == TNumericLimits<uint8>::Max();
	}

	template<typename T>
	bool Chaos::THeightField<T>::IsHole(int32 InCellX, int32 InCellY) const
	{
		// Convert to single cell index
		const int32 Index = InCellY * (GeomData.NumCols - 1) + InCellX;
		return IsHole(Index);
	}

	template<typename T>
	T Chaos::THeightField<T>::GetHeightAt(const TVector<T, 2>& InGridLocationLocal) const
	{
		if(CHAOS_ENSURE(InGridLocationLocal == FlatGrid.Clamp(InGridLocationLocal)))
		{
			TVector<int32, 2> CellCoord = FlatGrid.Cell(InGridLocationLocal);

			const int32 SingleIndex = CellCoord[1] * (GeomData.NumCols) + CellCoord[0];
			TVector<T, 3> Pts[4];
			GeomData.GetPoints(SingleIndex, Pts);

			float FractionX = FMath::Frac(InGridLocationLocal[0]);
			float FractionY = FMath::Frac(InGridLocationLocal[1]);

			if(FractionX > FractionY)
			{
				// In the second triangle (0,3,2)
#if 0
				// Reduced form of Bary calculation - TODO confirm working
				float U = -1.0f * (FractionY - 1);
				float V = FractionX;
				float W = 1.0f - U - V;
#endif

				FVector Tri[3];
				Tri[0] = FVector(0.0f, 0.0f, 0.0f);
				Tri[1] = FVector(1.0f, 1.0f, 0.0f);
				Tri[2] = FVector(0.0f, 1.0f, 0.0f);
				
				FVector Bary = FMath::GetBaryCentric2D({ FractionX, FractionY, 0.0f }, Tri[0], Tri[1], Tri[2]);

				return Pts[0].Z * Bary[0] + Pts[3].Z * Bary[1] + Pts[2].Z * Bary[2];
			}
			else
			{
				// In the first triangle (0,1,3)

#if 0
				// Reduced form of Bary calculation - TODO confirm working
				float U = ((-1) * (FractionX - 1));
				float V = ((FractionX - 1) - (FractionY - 1));
				float W = 1.0f - U - V;
#endif

				FVector Tri[3];
				Tri[0] = FVector(0.0f, 0.0f, 0.0f);
				Tri[1] = FVector(1.0f, 0.0f, 0.0f);
				Tri[2] = FVector(1.0f, 1.0f, 0.0f);

				FVector Bary = FMath::GetBaryCentric2D({FractionX, FractionY, 0.0f}, Tri[0], Tri[1], Tri[2]);

				return Pts[0].Z * Bary[0] + Pts[1].Z * Bary[1] + Pts[3].Z * Bary[2];
			}
		}

		return 0.0f;
	}

	template<typename T>
	bool Chaos::THeightField<T>::GetCellBounds3D(const TVector<int32, 2> InCoord, TVector<T, 3>& OutMin, TVector<T, 3>& OutMax, const TVector<T, 3>& InInflate /*= {0}*/) const
	{
		if (FlatGrid.IsValid(InCoord))
		{
			//todo: just compute max height, avoid extra work since this is called from tight loop
			TVec3<T> Min,Max;
			CalcCellBounds3D(InCoord,Min,Max);

			OutMin = TVec3<T>(InCoord[0], InCoord[1], GeomData.GetMinHeight());
			OutMax = TVec3<T>(InCoord[0] + 1, InCoord[1] + 1, Max[2]);
			OutMin = OutMin - InInflate;
			OutMax = OutMax + InInflate;

			return true;
		}

		return false;
	}

	template<typename T>
	bool Chaos::THeightField<T>::GetCellBounds2DScaled(const TVector<int32, 2> InCoord, FBounds2D& OutBounds, const TVector<T, 2>& InInflate /*= {0}*/) const
	{
		if (FlatGrid.IsValid(InCoord))
		{
			OutBounds.Min = TVector<T, 2>(InCoord[0], InCoord[1]);
			OutBounds.Max = TVector<T, 2>(InCoord[0] + 1, InCoord[1] + 1);
			OutBounds.Min -= InInflate;
			OutBounds.Max += InInflate;
			const TVector<float, 2> Scale2D = TVector<T, 2>(GeomData.Scale[0], GeomData.Scale[1]);
			OutBounds.Min *= Scale2D;
			OutBounds.Max *= Scale2D;
			return true;
		}

		return false;
	}

	template<typename T>
	bool Chaos::THeightField<T>::GetCellBounds3DScaled(const TVector<int32, 2> InCoord, TVector<T, 3>& OutMin, TVector<T, 3>& OutMax, const TVector<T, 3>& InInflate /*= {0}*/) const
	{
		if (FlatGrid.IsValid(InCoord))
		{
			//todo: just compute max height, avoid extra work since this is called from tight loop
			TVec3<T> Min,Max;
			CalcCellBounds3D(InCoord,Min,Max);

			OutMin = TVec3<T>(InCoord[0], InCoord[1], GeomData.GetMinHeight());
			OutMax = TVec3<T>(InCoord[0] + 1, InCoord[1] + 1, Max[2]);
			OutMin = OutMin * GeomData.Scale - InInflate;
			OutMax = OutMax * GeomData.Scale + InInflate;
			return true;
		}

		return false;
	}

	template<typename T>
	bool Chaos::THeightField<T>::CalcCellBounds3D(const TVector<int32, 2> InCoord, TVector<T, 3>& OutMin, TVector<T, 3>& OutMax, const TVector<T, 3>& InInflate /*= {0}*/) const
	{
		if(FlatGrid.IsValid(InCoord))
		{
			int32 Index = InCoord[1] * (GeomData.NumCols) + InCoord[0];
			static TVector<T, 3> Points[4];
			GeomData.GetPoints(Index, Points);

			OutMin = OutMax = Points[0];

			for(int32 PointIndex = 1; PointIndex < 4; ++PointIndex)
			{
				const TVector<T, 3>& Point = Points[PointIndex];
				OutMin = TVector<T, 3>(FGenericPlatformMath::Min(OutMin[0], Point[0]), FGenericPlatformMath::Min(OutMin[1], Point[1]), FGenericPlatformMath::Min(OutMin[2], Point[2]));
				OutMax = TVector<T, 3>(FGenericPlatformMath::Max(OutMax[0], Point[0]), FGenericPlatformMath::Max(OutMax[1], Point[1]), FGenericPlatformMath::Max(OutMax[2], Point[2]));
			}

			OutMin -= InInflate;
			OutMax += InInflate;

			return true;
		}

		return false;
	}


	template <typename T>
	bool THeightField<T>::GridCast(const TVector<T, 3>& StartPoint, const TVector<T, 3>& Dir, const T Length, THeightfieldRaycastVisitor<T>& Visitor) const
	{
		//Is this check needed?
		if(Length < 1e-4)
		{
			return false;
		}

		T CurrentLength = Length;
		TVector<T, 2> ClippedFlatRayStart;
		TVector<T, 2> ClippedFlatRayEnd;

		// Data for fast box cast
		TVector<T, 3> Min, Max, HitPoint;
		bool bParallel[3];
		TVector<T, 3> InvDir;

		T InvCurrentLength = 1 / CurrentLength;
		for(int Axis = 0; Axis < 3; ++Axis)
		{
			bParallel[Axis] = FMath::IsNearlyZero(Dir[Axis], 1.e-8f);
			InvDir[Axis] = bParallel[Axis] ? 0 : 1 / Dir[Axis];
		}

		T TOI;
		const FBounds2D FlatBounds = GetFlatBounds();
		TAABB<T,3> Bounds(
			TVec3<T>(FlatBounds.Min[0],FlatBounds.Min[1],GeomData.GetMinHeight() * GeomData.Scale[2]),
			TVec3<T>(FlatBounds.Max[0],FlatBounds.Max[1],GeomData.GetMaxHeight() * GeomData.Scale[2])
			);
		TVector<T,3> NextStart;

		if(Bounds.RaycastFast(StartPoint, Dir, InvDir, bParallel, Length, InvCurrentLength, TOI, NextStart))
		{
			const TVector<T,2> Scale2D(GeomData.Scale[0],GeomData.Scale[1]);
			TVector<int32,2> CellIdx = FlatGrid.Cell(TVector<int32,2>(NextStart[0] / Scale2D[0], NextStart[1] / Scale2D[1]));

			// Boundaries might push us one cell over
			CellIdx = FlatGrid.ClampIndex(CellIdx);
			const T ZDx = Bounds.Extents()[2];
			const T ZMidPoint = Bounds.Min()[2] + ZDx * 0.5;
			const TVector<T,3> ScaledDx(FlatGrid.Dx()[0] * Scale2D[0],FlatGrid.Dx()[1] * Scale2D[1],ZDx);
			const TVector<T,2> ScaledDx2D(ScaledDx[0],ScaledDx[1]);
			const TVector<T,2> ScaledMin = FlatGrid.MinCorner() * Scale2D;

			//START
			do
			{
				if(GetCellBounds3DScaled(CellIdx,Min,Max))
				{
					// Check cell bounds
					//todo: can do it without raycast
					if(TAABB<T,3>(Min,Max).RaycastFast(StartPoint,Dir,InvDir,bParallel,CurrentLength,InvCurrentLength,TOI,HitPoint))
					{
						// Visit the selected cell
						bool bContinue = Visitor.VisitRaycast(CellIdx[1] * (GeomData.NumCols - 1) + CellIdx[0],CurrentLength);
						if(!bContinue)
						{
							return false;
						}
					}
				}


				//find next cell

				//We want to know which plane we used to cross into next cell
				const TVector<T,2> ScaledCellCenter2D = ScaledMin + TVector<T,2>(CellIdx[0] + 0.5,CellIdx[1] + 0.5) * ScaledDx2D;
				const TVector<T,3> ScaledCellCenter(ScaledCellCenter2D[0], ScaledCellCenter2D[1], ZMidPoint);

				T Times[3];
				T BestTime = CurrentLength;
				bool bTerminate = true;
				for(int Axis = 0; Axis < 3; ++Axis)
				{
					if(!bParallel[Axis])
					{
						const T CrossPoint = Dir[Axis] > 0 ? ScaledCellCenter[Axis] + ScaledDx[Axis] / 2 : ScaledCellCenter[Axis] - ScaledDx[Axis] / 2;
						const T Distance = CrossPoint - NextStart[Axis];	//note: CellCenter already has /2, we probably want to use the corner instead
						const T Time = Distance * InvDir[Axis];
						Times[Axis] = Time;
						if(Time < BestTime)
						{
							bTerminate = false;	//found at least one plane to pass through
							BestTime = Time;
						}
					} else
					{
						Times[Axis] = TNumericLimits<T>::Max();
					}
				}

				if(bTerminate)
				{
					return false;
				}

				const TVector<int32,2> PrevIdx = CellIdx;

				for(int Axis = 0; Axis < 2; ++Axis)
				{
					CellIdx[Axis] += (Times[Axis] <= BestTime) ? (Dir[Axis] > 0 ? 1 : -1) : 0;
					if(CellIdx[Axis] < 0 || CellIdx[Axis] >= FlatGrid.Counts()[Axis])
					{
						return false;
					}
				}

				if(PrevIdx == CellIdx)
				{
					//crossed on z plane which means no longer in heightfield bounds
					return false;
				}

				NextStart = NextStart + Dir * BestTime;
			} while(true);
		}

		return false;
	}

	struct F2DGridSet
	{
		F2DGridSet(TVector<int32, 2> Size)
			: NumX(Size[0])
			, NumY(Size[1])
		{
			int32 BitsNeeded = NumX * NumY;
			DataSize = 1 + (BitsNeeded) / 8;
			Data = MakeUnique<uint8[]>(DataSize);
			FMemory::Memzero(Data.Get(), DataSize);
		}

		bool Contains(const TVector<int32, 2>& Coordinate)
		{
			int32 Idx = Coordinate[1] * NumX + Coordinate[0];
			int32 ByteIdx = Idx / 8;
			int32 BitIdx = Idx % 8;
			check(ByteIdx >= 0 && ByteIdx < DataSize);
			bool bContains = (Data[ByteIdx] >> BitIdx) & 0x1;
			return bContains;
		}

		void Add(const TVector<int32, 2>& Coordinate)
		{
			int32 Idx = Coordinate[1] * NumX + Coordinate[0];
			int32 ByteIdx = Idx / 8;
			int32 BitIdx = Idx % 8;
			uint8 Mask = 1 << BitIdx;
			check(ByteIdx >= 0 && ByteIdx < DataSize);
			Data[ByteIdx] |= Mask;
		}

	private:
		int32 NumX;
		int32 NumY;
		TUniquePtr<uint8[]> Data;
		int32 DataSize;
	};

	template<typename T>
	template<typename SQVisitor>
	bool THeightField<T>::GridSweep(const TVector<T, 3>& StartPoint, const TVector<T, 3>& Dir, const T Length, const TVector<T, 2> InHalfExtents, SQVisitor& Visitor) const
	{
		T CurrentLength = Length;

		FBounds2D InflatedBounds = GetFlatBounds();
		InflatedBounds.Min -= InHalfExtents;
		InflatedBounds.Max += InHalfExtents;

		TVector<T, 3> HalfExtents3D(InHalfExtents[0], InHalfExtents[1], InHalfExtents[1]);

		const TVector<T, 3> EndPoint = StartPoint + Dir * Length;
		const TVector<T, 2> Start2D(StartPoint[0], StartPoint[1]);
		const TVector<T, 2> End2D(EndPoint[0], EndPoint[1]);
		const TVector<T, 2> Scale2D(GeomData.Scale[0], GeomData.Scale[1]);

		TVector<T, 2> ClippedStart;
		TVector<T, 2> ClippedEnd;

		if(InflatedBounds.ClipLine(StartPoint, StartPoint + Dir * Length, ClippedStart, ClippedEnd))
		{
			// Rasterize the line over the grid
			TVector<int32, 2> StartCell = FlatGrid.Cell(ClippedStart / Scale2D);
			TVector<int32, 2> EndCell = FlatGrid.Cell(ClippedEnd / Scale2D);

			// Boundaries might push us one cell over
			StartCell = FlatGrid.ClampIndex(StartCell);
			EndCell = FlatGrid.ClampIndex(EndCell);

			const int32 DeltaX = FMath::Abs(EndCell[0] - StartCell[0]);
			const int32 DeltaY = -FMath::Abs(EndCell[1] - StartCell[1]);
			const bool bSameCell = DeltaX == 0 && DeltaY == 0;

			const int32 DirX = StartCell[0] < EndCell[0] ? 1 : -1;
			const int32 DirY = StartCell[1] < EndCell[1] ? 1 : -1;
			int32 Error = DeltaX + DeltaY;
			const TVector<int32, 2> ThickenDir = FMath::Abs(DeltaX) > FMath::Abs(DeltaY) ? TVector<int32, 2>(0, 1) : TVector<int32, 2>(1, 0);

			struct FQueueEntry
			{
				TVector<int32, 2> Index;
				T ToI;
			};

			// Tracking data for cells to query (similar to bounding volume approach)
			F2DGridSet Seen(FlatGrid.Counts());
			TArray<FQueueEntry> Queue;
			Queue.Add({StartCell, -1});
			Seen.Add(StartCell);

			// Data for fast box cast
			TVector<T, 3> Min, Max, HitPoint;
			float ToI;
			bool bParallel[3];
			TVector<T, 3> InvDir;

			T InvCurrentLength = 1 / CurrentLength;
			for(int Axis = 0; Axis < 3; ++Axis)
			{
				bParallel[Axis] = FMath::IsNearlyZero(Dir[Axis], 1.e-8f);
				InvDir[Axis] = bParallel[Axis] ? 0 : 1 / Dir[Axis];
			}

			int32 QueueIndex = 0;
			while(QueueIndex < Queue.Num())
			{
				// Copy so we don't lost the entry through reallocs
				FQueueEntry CellCoord = Queue[QueueIndex++];

				if(CellCoord.ToI > CurrentLength)
				{
					continue;
				}

				if(bSameCell)
				{
					// Test the current cell
					bool bContinue = Visitor.VisitSweep(CellCoord.Index[1] * (GeomData.NumCols - 1) + CellCoord.Index[0], CurrentLength);
					
					if(!bContinue)
					{
						return true;
					}

					// Flatten out a double loop and skip the centre cell
					// to search cells immediately adjacent to the current cell
					static const TVector<int32, 2> Neighbors[] =
					{
						{-1, -1}, {0, -1}, {1, -1},
						{-1, 0}, {1, 0},
						{-1, 1}, {0, 1}, {1, 1}
					};

					for(const TVector<int32, 2>& Neighbor : Neighbors)
					{
						TVector<int32, 2> NeighCoord = CellCoord.Index + Neighbor;

						FBounds2D CellBounds;
						if(GetCellBounds3DScaled(NeighCoord, Min, Max, HalfExtents3D) && !Seen.Contains(NeighCoord))
						{
							if(TAABB<T, 3>(Min,Max).RaycastFast(StartPoint, Dir, InvDir, bParallel, CurrentLength, InvCurrentLength, ToI, HitPoint))
							{
								Seen.Add(NeighCoord);
								Queue.Add({NeighCoord, ToI});
							}
						}
					}
				}
				else
				{
					// Expand each cell along the thicken direction
					// Although the line should minimally thicken around the perpendicular to the line direction
					// it's cheaper to just expand in the cardinal opposite the current major direction. We end up
					// doing a broad test on more cells but avoid having to run many rasterize/walk steps for each
					// perpendicular step.
					auto Expand = [&](const TVector<int32, 2>& Begin, const TVector<int32, 2>& Direction, const int32 NumSteps)
					{
						TVector<int32, 2> CurrentCell = Begin;

						for(int32 CurrStep = 0; CurrStep < NumSteps; ++CurrStep)
						{
							CurrentCell += Direction;

							// Fail if we leave the grid
							if(CurrentCell[0] < 0 || CurrentCell[1] < 0 || CurrentCell[0] > FlatGrid.Counts()[0] - 1 || CurrentCell[1] > FlatGrid.Counts()[1] - 1)
							{
								break;
							}

							// No intersections here. We set the ToI to zero to cause an intersection check to happen
							// without any expansion when we reach this cell in the queue.
							if(!Seen.Contains(CurrentCell))
							{
								Seen.Add(CurrentCell);
								Queue.Add({CurrentCell, 0});
							}
						}
					};

					// Check the current cell, if we hit its 3D bound we can move on to narrow phase
					const TVector<int32, 2> Coord = CellCoord.Index;
					if(GetCellBounds3DScaled(Coord, Min, Max, HalfExtents3D) &&
						TAABB<T, 3>(Min,Max).RaycastFast(StartPoint, Dir, InvDir, bParallel, CurrentLength, InvCurrentLength, ToI, HitPoint))
					{
						bool bContinue = Visitor.VisitSweep(CellCoord.Index[1] * (GeomData.NumCols - 1) + CellCoord.Index[0], CurrentLength);

						if(!bContinue)
						{
							return true;
						}
					}

					// This time isn't used to reject things for this method but to flag cells that should be expanded
					if(CellCoord.ToI < 0)
					{
						// Perform expansion for thickness
						int32 ExpandAxis;
						if(ThickenDir[0] == 0)
						{
							ExpandAxis = 1;
						}
						else
						{
							ExpandAxis = 0;
						}
						T ExpandSize = HalfExtents3D[ExpandAxis];
						int32 Steps = FMath::RoundFromZero(ExpandSize / GeomData.Scale[ExpandAxis]);

						Expand(Coord, ThickenDir, Steps);
						Expand(Coord, -ThickenDir, Steps);

						// Walk the line and add to the queue
						if(StartCell != EndCell)
						{
							const int32 DoubleError = Error * 2;

							if(DoubleError >= DeltaY)
							{
								Error += DeltaY;
								StartCell[0] += DirX;
							}

							if(DoubleError <= DeltaX)
							{
								Error += DeltaX;
								StartCell[1] += DirY;
							}

							if(!Seen.Contains(StartCell))
							{
								Seen.Add(StartCell);
								Queue.Add({StartCell, -1});
							}
						}
					}
				}
			}
		}

		return false;
	}

	template <typename T>
	bool THeightField<T>::Raycast(const TVector<T, 3>& StartPoint, const TVector<T, 3>& Dir, const T Length, const T Thickness, T& OutTime, TVector<T, 3>& OutPosition, TVector<T, 3>& OutNormal, int32& OutFaceIndex) const
	{
		OutFaceIndex = INDEX_NONE;

		THeightfieldRaycastVisitor<T> Visitor(&GeomData, StartPoint, Dir, Thickness);

		if(Thickness > 0)
		{
			GridSweep(StartPoint, Dir, Length, TVector<T, 2>(Thickness), Visitor);
		}
		else
		{
			GridCast(StartPoint, Dir, Length, Visitor);
		}

		if(Visitor.OutTime <= Length)
		{
			OutTime = Visitor.OutTime;
			OutPosition = Visitor.OutPosition;
			OutNormal = Visitor.OutNormal;
			OutFaceIndex = Visitor.OutFaceIndex;
			return true;
		}

		return false;
	}

	template<typename T>
	bool Chaos::THeightField<T>::GetGridIntersections(FBounds2D InFlatBounds, TArray<TVector<int32, 2>>& OutInterssctions) const
	{
		OutInterssctions.Reset();

		const FBounds2D FlatBounds = GetFlatBounds();
		const TVector<T, 2> Scale2D(GeomData.Scale[0], GeomData.Scale[1]);

		InFlatBounds.Min = FlatBounds.Clamp(InFlatBounds.Min);
		InFlatBounds.Max = FlatBounds.Clamp(InFlatBounds.Max);
		TVector<int32, 2> MinCell = FlatGrid.Cell(InFlatBounds.Min / Scale2D);
		TVector<int32, 2> MaxCell = FlatGrid.Cell(InFlatBounds.Max / Scale2D);
		MinCell = FlatGrid.ClampIndex(MinCell);
		MaxCell = FlatGrid.ClampIndex(MaxCell);

		// We want to capture the first cell (delta == 0) as well
		const int32 NumX = MaxCell[0] - MinCell[0] + 1;
		const int32 NumY = MaxCell[1] - MinCell[1] + 1;

		for(int32 CurrX = 0; CurrX < NumX; ++CurrX)
		{
			for(int32 CurrY = 0; CurrY < NumY; ++CurrY)
			{
				OutInterssctions.Add(FlatGrid.ClampIndex(TVector<int32, 2>(MinCell[0] + CurrX, MinCell[1] + CurrY)));
			}
		}

		return OutInterssctions.Num() > 0;
	}

	template<typename T>
	typename Chaos::THeightField<T>::FBounds2D Chaos::THeightField<T>::GetFlatBounds() const
	{
		FBounds2D Result;
		Result.Min = TVector<T, 2>(CachedBounds.Min()[0], CachedBounds.Min()[1]);
		Result.Max = TVector<T, 2>(CachedBounds.Max()[0], CachedBounds.Max()[1]);
		return Result;
	}

	template <typename T>
	bool THeightField<T>::Overlap(const TVector<T, 3>& Point, const T Thickness) const
	{
		auto OverlapTriangle = [&](const TVector<T, 3>& A, const TVector<T, 3>& B, const TVector<T, 3>& C) -> bool
		{
			const TVector<T, 3> AB = B - A;
			const TVector<T, 3> AC = C - A;
			TVector<T, 3> Normal = TVector<T, 3>::CrossProduct(AB, AC);
			const T NormalLength = Normal.SafeNormalize();

			if(!ensure(NormalLength > KINDA_SMALL_NUMBER))
			{
				return false;
			}

			const TPlane<T, 3> TriPlane{A, Normal};
			const TVector<T, 3> ClosestPointOnTri = FindClosestPointOnTriangle(TriPlane, A, B, C, Point);
			const T Distance2 = (ClosestPointOnTri - Point).SizeSquared();

			if(Distance2 <= Thickness * Thickness)	//This really only has a hope in working if thickness is > 0
			{
				return true;
			}

			return false;
		};

		TAABB<T, 3> QueryBounds(Point, Point);
		QueryBounds.Thicken(Thickness);

		FBounds2D FlatQueryBounds;
		FlatQueryBounds.Min = TVector<T, 2>(QueryBounds.Min()[0], QueryBounds.Min()[1]);
		FlatQueryBounds.Max = TVector<T, 2>(QueryBounds.Max()[0], QueryBounds.Max()[1]);

		TArray<TVector<int32, 2>> Intersections;
		TVector<T, 3> Points[4];

		GetGridIntersections(FlatQueryBounds, Intersections);

		for(const TVector<int32, 2>& Cell : Intersections)
		{
			const int32 SingleIndex = Cell[1] * (GeomData.NumCols) + Cell[0];
			GeomData.GetPointsScaled(SingleIndex, Points);

			if(OverlapTriangle(Points[0], Points[1], Points[3]))
			{
				return true;
			}

			if(OverlapTriangle(Points[0], Points[3], Points[2]))
			{
				return true;
			}
		}
		
		return false;
	}


	template <typename T>
	template <typename GeomType>
	bool THeightField<T>::GJKContactPointImp(const GeomType& QueryGeom, const TRigidTransform<T, 3>& QueryTM, const T Thickness,
		TVector<T, 3>& ContactLocation, TVector<T, 3>& ContactNormal, T& ContactPhi) const
	{
		auto OverlapTriangle = [&](const TVector<T, 3>& A, const TVector<T, 3>& B, const TVector<T, 3>& C,
			TVector<T, 3>& LocalContactLocation, TVector<T, 3>& LocalContactNormal, T& LocalContactPhi) -> bool
		{
			const TVector<T, 3> AB = B - A;
			const TVector<T, 3> AC = C - A;

			const TVector<T, 3> Offset = TVector<T, 3>::CrossProduct(AB, AC);

			TTriangle<T> TriangleConvex(A, B, C);

			T Penetration;
			TVec3<T> ClosestA, ClosestB, Normal;
			if (GJKPenetration(TriangleConvex, QueryGeom, QueryTM, Penetration, ClosestA, ClosestB, Normal, (T)0))
			{
				TVec3<T> TestVector = QueryTM.InverseTransformVector(Normal);

				LocalContactLocation = ClosestB;
				LocalContactNormal = Normal; 
				LocalContactPhi = -Penetration;
				return true;
			}

			return LocalContactPhi<0.f;
		};

		bool bResult = false;
		TAABB<T, 3> QueryBounds = QueryGeom.BoundingBox();
		QueryBounds.Thicken(Thickness);
		QueryBounds = QueryBounds.TransformedAABB(QueryTM);

		FBounds2D FlatQueryBounds;
		FlatQueryBounds.Min = TVector<T, 2>(QueryBounds.Min()[0], QueryBounds.Min()[1]);
		FlatQueryBounds.Max = TVector<T, 2>(QueryBounds.Max()[0], QueryBounds.Max()[1]);

		TArray<TVector<int32, 2>> Intersections;
		TVector<T, 3> Points[4];

		GetGridIntersections(FlatQueryBounds, Intersections);

		T LocalContactPhi = FLT_MAX;
		TVector<T, 3> LocalContactLocation, LocalContactNormal;
		for (const TVector<int32, 2>& Cell : Intersections)
		{
			const int32 SingleIndex = Cell[1] * GeomData.NumCols + Cell[0];
			const int32 CellIndex = Cell[1] * (GeomData.NumCols - 1) + Cell[0];
			
			// Check for holes and skip checking if we'll never collide
			if(GeomData.MaterialIndices.IsValidIndex(CellIndex) && GeomData.MaterialIndices[CellIndex] == TNumericLimits<uint8>::Max())
			{
				continue;
			}

			// The triangle is solid so proceed to test it
			GeomData.GetPointsScaled(SingleIndex, Points);

			if (OverlapTriangle(Points[0], Points[1], Points[3], LocalContactLocation, LocalContactNormal, LocalContactPhi))
			{
				if (LocalContactPhi < ContactPhi)
				{
					ContactPhi = LocalContactPhi;
					ContactLocation = LocalContactLocation;
					ContactNormal = LocalContactNormal;
				}
			}

			if (OverlapTriangle(Points[0], Points[3], Points[2], LocalContactLocation, LocalContactNormal, LocalContactPhi))
			{
				if (LocalContactPhi < ContactPhi)
				{
					ContactPhi = LocalContactPhi;
					ContactLocation = LocalContactLocation;
					ContactNormal = LocalContactNormal;
				}
			}
		}

		if(ContactPhi < 0)
			return true;
		return false;
	}

	template <typename T>
	bool THeightField<T>::GJKContactPoint(const TBox<T, 3>& QueryGeom, const TRigidTransform<T, 3>& QueryTM, const T Thickness, TVector<T, 3>& ContactLocation, TVector<T, 3>& ContactNormal, T& ContactPhi) const
	{
		return GJKContactPointImp(QueryGeom, QueryTM, Thickness, ContactLocation, ContactNormal, ContactPhi);
	}

	template <typename T>
	bool THeightField<T>::GJKContactPoint(const TSphere<T, 3>& QueryGeom, const TRigidTransform<T, 3>& QueryTM, const T Thickness, TVector<T, 3>& ContactLocation, TVector<T, 3>& ContactNormal, T& ContactPhi) const
	{
		return GJKContactPointImp(QueryGeom, QueryTM, Thickness, ContactLocation, ContactNormal, ContactPhi);
	}

	template <typename T>
	bool THeightField<T>::GJKContactPoint(const TCapsule<T>& QueryGeom, const TRigidTransform<T, 3>& QueryTM, const T Thickness, TVector<T, 3>& ContactLocation, TVector<T, 3>& ContactNormal, T& ContactPhi) const
	{
		return GJKContactPointImp(QueryGeom, QueryTM, Thickness, ContactLocation, ContactNormal, ContactPhi);
	}

	template <typename T>
	bool THeightField<T>::GJKContactPoint(const FConvex& QueryGeom, const TRigidTransform<T, 3>& QueryTM, const T Thickness, TVector<T, 3>& ContactLocation, TVector<T, 3>& ContactNormal, T& ContactPhi) const
	{
		return GJKContactPointImp(QueryGeom, QueryTM, Thickness, ContactLocation, ContactNormal, ContactPhi);
	}

	template <typename T>
	bool THeightField<T>::GJKContactPoint(const TImplicitObjectScaled < TBox<T, 3>>& QueryGeom, const TRigidTransform<T, 3>& QueryTM, const T Thickness, TVector<T, 3>& ContactLocation, TVector<T, 3>& ContactNormal, T& ContactPhi) const
	{
		return GJKContactPointImp(QueryGeom, QueryTM, Thickness, ContactLocation, ContactNormal, ContactPhi);
	}

	template <typename T>
	bool THeightField<T>::GJKContactPoint(const TImplicitObjectScaled < TSphere<T, 3>>& QueryGeom, const TRigidTransform<T, 3>& QueryTM, const T Thickness, TVector<T, 3>& ContactLocation, TVector<T, 3>& ContactNormal, T& ContactPhi) const
	{
		return GJKContactPointImp(QueryGeom, QueryTM, Thickness, ContactLocation, ContactNormal, ContactPhi);
	}

	template <typename T>
	bool THeightField<T>::GJKContactPoint(const TImplicitObjectScaled < TCapsule<T>>& QueryGeom, const TRigidTransform<T, 3>& QueryTM, const T Thickness, TVector<T, 3>& ContactLocation, TVector<T, 3>& ContactNormal, T& ContactPhi) const
	{
		return GJKContactPointImp(QueryGeom, QueryTM, Thickness, ContactLocation, ContactNormal, ContactPhi);
	}

	template <typename T>
	bool THeightField<T>::GJKContactPoint(const TImplicitObjectScaled<FConvex>& QueryGeom, const TRigidTransform<T, 3>& QueryTM, const T Thickness, TVector<T, 3>& ContactLocation, TVector<T, 3>& ContactNormal, T& ContactPhi) const
	{
		return GJKContactPointImp(QueryGeom, QueryTM, Thickness, ContactLocation, ContactNormal, ContactPhi);
	}


	template <typename T>
	template <typename QueryGeomType>
	bool THeightField<T>::OverlapGeomImp(const QueryGeomType& QueryGeom, const TRigidTransform<T, 3>& QueryTM, const T Thickness, FMTDInfo* OutMTD) const
	{
		if (OutMTD)
		{
			OutMTD->Normal = TVec3<T>(0);
			OutMTD->Penetration = TNumericLimits<T>::Lowest();
		}

		auto OverlapTriangle = [&](const TVector<T, 3>& A, const TVector<T, 3>& B, const TVector<T, 3>& C, FMTDInfo* InnerMTD) -> bool
		{
			const TVector<T, 3> AB = B - A;
			const TVector<T, 3> AC = C - A;

			//It's most likely that the query object is in front of the triangle since queries tend to be on the outside.
			//However, maybe we should check if it's behind the triangle plane. Also, we should enforce this winding in some way
			const TVector<T, 3> Offset = TVector<T, 3>::CrossProduct(AB, AC);

			TTriangle<T> TriangleConvex(A, B, C);
			if (InnerMTD)
			{
				TVec3<T> TriangleNormal(0);
				T Penetration = 0;
				TVec3<T> ClosestA(0);
				TVec3<T> ClosestB(0);
				if (GJKPenetration(TriangleConvex, QueryGeom, QueryTM, Penetration, ClosestA, ClosestB, TriangleNormal, Thickness))
				{
					// Use Deepest MTD.
					if (Penetration > InnerMTD->Penetration)
					{
						InnerMTD->Penetration = Penetration;
						InnerMTD->Normal = TriangleNormal;
					}
					return true;
				}

				return false;
			}
			else
			{
				return GJKIntersection(TriangleConvex, QueryGeom, QueryTM, Thickness, Offset);
			}
		};

		bool bResult = false;
		TAABB<T, 3> QueryBounds = QueryGeom.BoundingBox();
		QueryBounds.Thicken(Thickness);
		QueryBounds = QueryBounds.TransformedAABB(QueryTM);

		FBounds2D FlatQueryBounds;
		FlatQueryBounds.Min = TVector<T, 2>(QueryBounds.Min()[0], QueryBounds.Min()[1]);
		FlatQueryBounds.Max = TVector<T, 2>(QueryBounds.Max()[0], QueryBounds.Max()[1]);

		TArray<TVector<int32, 2>> Intersections;
		TVector<T, 3> Points[4];

		GetGridIntersections(FlatQueryBounds, Intersections);

		bool bOverlaps = false;
		for(const TVector<int32, 2>& Cell : Intersections)
		{
			const int32 SingleIndex = Cell[1] * (GeomData.NumCols) + Cell[0];
			GeomData.GetPointsScaled(SingleIndex, Points);

			if(OverlapTriangle(Points[0], Points[1], Points[3], OutMTD))
			{
				bOverlaps = true;
				if (!OutMTD)
				{
					return true;
				}
			}

			if(OverlapTriangle(Points[0], Points[3], Points[2], OutMTD))
			{
				bOverlaps = true;
				if (!OutMTD)
				{
					return true;
				}
			}
		}

		return bOverlaps;
	}

	template <typename T>
	bool THeightField<T>::OverlapGeom(const TSphere<T, 3>& QueryGeom, const TRigidTransform<T, 3>& QueryTM, const T Thickness, FMTDInfo* OutMTD) const
	{
		return OverlapGeomImp(QueryGeom, QueryTM, Thickness, OutMTD);
	}

	template <typename T>
	bool THeightField<T>::OverlapGeom(const TBox<T, 3>& QueryGeom, const TRigidTransform<T, 3>& QueryTM, const T Thickness, FMTDInfo* OutMTD) const
	{
		return OverlapGeomImp(QueryGeom, QueryTM, Thickness, OutMTD);
	}

	template <typename T>
	bool THeightField<T>::OverlapGeom(const TCapsule<T>& QueryGeom, const TRigidTransform<T, 3>& QueryTM, const T Thickness, FMTDInfo* OutMTD) const
	{
		return OverlapGeomImp(QueryGeom, QueryTM, Thickness, OutMTD);
	}

	template <typename T>
	bool THeightField<T>::OverlapGeom(const FConvex& QueryGeom, const TRigidTransform<T, 3>& QueryTM, const T Thickness, FMTDInfo* OutMTD) const
	{
		return OverlapGeomImp(QueryGeom, QueryTM, Thickness, OutMTD);
	}

	template <typename T>
	bool THeightField<T>::OverlapGeom(const TImplicitObjectScaled<TSphere<T, 3>>& QueryGeom, const TRigidTransform<T, 3>& QueryTM, const T Thickness, FMTDInfo* OutMTD) const
	{
		return OverlapGeomImp(QueryGeom, QueryTM, Thickness, OutMTD);
	}

	template <typename T>
	bool THeightField<T>::OverlapGeom(const TImplicitObjectScaled<TBox<T, 3>>& QueryGeom, const TRigidTransform<T, 3>& QueryTM, const T Thickness, FMTDInfo* OutMTD) const
	{
		return OverlapGeomImp(QueryGeom, QueryTM, Thickness, OutMTD);
	}

	template <typename T>
	bool THeightField<T>::OverlapGeom(const TImplicitObjectScaled<TCapsule<T>>& QueryGeom, const TRigidTransform<T, 3>& QueryTM, const T Thickness, FMTDInfo* OutMTD) const
	{
		return OverlapGeomImp(QueryGeom, QueryTM, Thickness, OutMTD);
	}

	template <typename T>
	bool THeightField<T>::OverlapGeom(const TImplicitObjectScaled<FConvex>& QueryGeom, const TRigidTransform<T, 3>& QueryTM, const T Thickness, FMTDInfo* OutMTD) const
	{
		return OverlapGeomImp(QueryGeom, QueryTM, Thickness, OutMTD);
	}

	template <typename T>
	template <typename QueryGeomType>
	bool THeightField<T>::SweepGeomImp(const QueryGeomType& QueryGeom, const TRigidTransform<T, 3>& StartTM, const TVector<T, 3>& Dir, const T Length, T& OutTime, TVector<T, 3>& OutPosition, TVector<T, 3>& OutNormal, int32& OutFaceIndex, const T Thickness, bool bComputeMTD) const
	{
		bool bHit = false;
		THeightfieldSweepVisitor<QueryGeomType, T> SQVisitor(&GeomData, QueryGeom, StartTM, Dir, Thickness, bComputeMTD);
		const TAABB<T, 3> QueryBounds = QueryGeom.BoundingBox();
		const TVector<T, 3> StartPoint = StartTM.TransformPositionNoScale(QueryBounds.Center());

		const TVector<T, 3> Inflation3D = QueryBounds.Extents() * 0.5 + TVector<T, 3>(Thickness);
		GridSweep(StartPoint, Dir, Length, TVector<T, 2>(Inflation3D[0], Inflation3D[1]), SQVisitor);

		if(SQVisitor.OutTime <= Length)
		{
			OutTime = SQVisitor.OutTime;
			OutPosition = SQVisitor.OutPosition;
			OutNormal = SQVisitor.OutNormal;
			OutFaceIndex = SQVisitor.OutFaceIndex;
			bHit = true;
		}

		return bHit;
	}

	template <typename T>
	bool THeightField<T>::SweepGeom(const TSphere<T, 3>& QueryGeom, const TRigidTransform<T, 3>& StartTM, const TVector<T, 3>& Dir, const T Length, T& OutTime, TVector<T, 3>& OutPosition, TVector<T, 3>& OutNormal, int32& OutFaceIndex, const T Thickness, bool bComputeMTD) const
	{
		return SweepGeomImp(QueryGeom, StartTM, Dir, Length, OutTime, OutPosition, OutNormal, OutFaceIndex, Thickness, bComputeMTD);
	}

	template <typename T>
	bool THeightField<T>::SweepGeom(const TBox<T, 3>& QueryGeom, const TRigidTransform<T, 3>& StartTM, const TVector<T, 3>& Dir, const T Length, T& OutTime, TVector<T, 3>& OutPosition, TVector<T, 3>& OutNormal, int32& OutFaceIndex, const T Thickness, bool bComputeMTD) const
	{
		return SweepGeomImp(QueryGeom, StartTM, Dir, Length, OutTime, OutPosition, OutNormal, OutFaceIndex, Thickness, bComputeMTD);
	}

	template <typename T>
	bool THeightField<T>::SweepGeom(const TCapsule<T>& QueryGeom, const TRigidTransform<T, 3>& StartTM, const TVector<T, 3>& Dir, const T Length, T& OutTime, TVector<T, 3>& OutPosition, TVector<T, 3>& OutNormal, int32& OutFaceIndex, const T Thickness, bool bComputeMTD) const
	{
		return SweepGeomImp(QueryGeom, StartTM, Dir, Length, OutTime, OutPosition, OutNormal, OutFaceIndex, Thickness, bComputeMTD);
	}

	template <typename T>
	bool THeightField<T>::SweepGeom(const FConvex& QueryGeom, const TRigidTransform<T, 3>& StartTM, const TVector<T, 3>& Dir, const T Length, T& OutTime, TVector<T, 3>& OutPosition, TVector<T, 3>& OutNormal, int32& OutFaceIndex, const T Thickness, bool bComputeMTD) const
	{
		return SweepGeomImp(QueryGeom, StartTM, Dir, Length, OutTime, OutPosition, OutNormal, OutFaceIndex, Thickness, bComputeMTD);
	}

	template <typename T>
	bool THeightField<T>::SweepGeom(const TImplicitObjectScaled<TSphere<T, 3>>& QueryGeom, const TRigidTransform<T, 3>& StartTM, const TVector<T, 3>& Dir, const T Length, T& OutTime, TVector<T, 3>& OutPosition, TVector<T, 3>& OutNormal, int32& OutFaceIndex, const T Thickness, bool bComputeMTD) const
	{
		return SweepGeomImp(QueryGeom, StartTM, Dir, Length, OutTime, OutPosition, OutNormal, OutFaceIndex, Thickness, bComputeMTD);
	}

	template <typename T>
	bool THeightField<T>::SweepGeom(const TImplicitObjectScaled<TBox<T, 3>>& QueryGeom, const TRigidTransform<T, 3>& StartTM, const TVector<T, 3>& Dir, const T Length, T& OutTime, TVector<T, 3>& OutPosition, TVector<T, 3>& OutNormal, int32& OutFaceIndex, const T Thickness, bool bComputeMTD) const
	{
		return SweepGeomImp(QueryGeom, StartTM, Dir, Length, OutTime, OutPosition, OutNormal, OutFaceIndex, Thickness, bComputeMTD);
	}

	template <typename T>
	bool THeightField<T>::SweepGeom(const TImplicitObjectScaled<TCapsule<T>>& QueryGeom, const TRigidTransform<T, 3>& StartTM, const TVector<T, 3>& Dir, const T Length, T& OutTime, TVector<T, 3>& OutPosition, TVector<T, 3>& OutNormal, int32& OutFaceIndex, const T Thickness, bool bComputeMTD) const
	{
		return SweepGeomImp(QueryGeom, StartTM, Dir, Length, OutTime, OutPosition, OutNormal, OutFaceIndex, Thickness, bComputeMTD);
	}

	template <typename T>
	bool THeightField<T>::SweepGeom(const TImplicitObjectScaled<FConvex>& QueryGeom, const TRigidTransform<T, 3>& StartTM, const TVector<T, 3>& Dir, const T Length, T& OutTime, TVector<T, 3>& OutPosition, TVector<T, 3>& OutNormal, int32& OutFaceIndex, const T Thickness, bool bComputeMTD) const
	{
		return SweepGeomImp(QueryGeom, StartTM, Dir, Length, OutTime, OutPosition, OutNormal, OutFaceIndex, Thickness, bComputeMTD);
	}

	template <typename T>
	int32 THeightField<T>::FindMostOpposingFace(const TVector<T, 3>& Position, const TVector<T, 3>& UnitDir, int32 HintFaceIndex, T SearchDist) const
	{
		const T SearchDist2 = SearchDist * SearchDist;

		TAABB<T, 3> QueryBounds(Position - TVector<T, 3>(SearchDist), Position + TVector<T, 3>(SearchDist));
		const FBounds2D FlatBounds(QueryBounds);
		TArray<TVector<int32, 2>> PotentialIntersections;
		GetGridIntersections(FlatBounds, PotentialIntersections);

		T MostOpposingDot = TNumericLimits<T>::Max();
		int32 MostOpposingFace = HintFaceIndex;

		auto CheckTriangle = [&](int32 FaceIndex, const TVector<T, 3>& A, const TVector<T, 3>& B, const TVector<T, 3>& C)
		{
			const TVector<T, 3> AB = B - A;
			const TVector<T, 3> AC = C - A;
			TVector<T, 3> Normal = TVector<T, 3>::CrossProduct(AB, AC);
			const T NormalLength = Normal.SafeNormalize();
			if(!ensure(NormalLength > KINDA_SMALL_NUMBER))
			{
				//hitting degenerate triangle - should be fixed before we get to this stage
				return;
			}

			const TPlane<T, 3> TriPlane{A, Normal};
			const TVector<T, 3> ClosestPointOnTri = FindClosestPointOnTriangle(TriPlane, A, B, C, Position);
			const T Distance2 = (ClosestPointOnTri - Position).SizeSquared();
			if(Distance2 < SearchDist2)
			{
				const T Dot = TVector<T, 3>::DotProduct(Normal, UnitDir);
				if(Dot < MostOpposingDot)
				{
					MostOpposingDot = Dot;
					MostOpposingFace = FaceIndex;
				}
			}
		};

		ensure(PotentialIntersections.Num());
		for(const TVector<int32, 2>& CellCoord : PotentialIntersections)
		{
			const int32 CellIndex = CellCoord[1] * (GeomData.NumCols - 1) + CellCoord[0];
			const int32 SubX = CellIndex % (GeomData.NumCols - 1);
			const int32 SubY = CellIndex / (GeomData.NumCols - 1);

			const int32 FullIndex = CellIndex + SubY;

			TVector<T, 3> Points[4];

			GeomData.GetPointsScaled(FullIndex, Points);

			CheckTriangle(CellIndex * 2, Points[0], Points[1], Points[3]);
			CheckTriangle(CellIndex * 2 + 1, Points[0], Points[3], Points[2]);
		}

		return MostOpposingFace;
	}

	template <typename T>
	TVector<T, 3> THeightField<T>::FindGeometryOpposingNormal(const TVector<T, 3>& DenormDir, int32 FaceIndex, const TVector<T, 3>& OriginalNormal) const
	{
		if(ensure(FaceIndex != INDEX_NONE))
		{
			bool bSecondFace = FaceIndex % 2 != 0;

			int32 CellIndex = FaceIndex / 2;
			int32 CellY = CellIndex / (GeomData.NumCols - 1);

			TVector<T, 3> Points[4];

			GeomData.GetPointsScaled(CellIndex + CellY, Points);

			TVector<T, 3> A;
			TVector<T, 3> B;
			TVector<T, 3> C;

			if(bSecondFace)
			{
				A = Points[0];
				B = Points[3];
				C = Points[2];
			}
			else
			{
				A = Points[0];
				B = Points[1];
				C = Points[3];
			}

			const TVector<T, 3> AB = B - A;
			const TVector<T, 3> AC = C - A;
			TVector<T, 3> Normal = TVector<T, 3>::CrossProduct(AB, AC);
			const T Length = Normal.SafeNormalize();
			ensure(Length);
			return Normal;
		}

		return TVector<T, 3>(0, 0, 1);
	}

	template<typename T>
	void THeightField<T>::CalcBounds()
	{
		// Flatten out the Z axis
		FlattenedBounds = GetFlatBounds();

		BuildQueryData();

		// Cache per-cell bounds
		const int32 NumX = GeomData.NumCols - 1;
		const int32 NumY = GeomData.NumRows - 1;
	}

	template<typename T>
	void THeightField<T>::BuildQueryData()
	{
		// NumCols and NumRows are the actual heights, there are n-1 cells between those heights
		TVector<int32, 2> Cells(GeomData.NumCols - 1, GeomData.NumRows - 1);
		
		TVector<T, 2> MinCorner(0, 0);
		TVector<T, 2> MaxCorner(GeomData.NumCols - 1, GeomData.NumRows - 1);
		//MaxCorner *= {GeomData.Scale[0], GeomData.Scale[1]};

		FlatGrid = TUniformGrid<T, 2>(MinCorner, MaxCorner, Cells);
	}

}

template class Chaos::THeightField<float>;

