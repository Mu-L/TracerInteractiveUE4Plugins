// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Vector.h"

namespace Chaos
{
template<class T, int d>
class TArrayND;
template<class T, int d>
class TArrayFaceND;

template<class T, int d>
class CHAOS_API TUniformGridBase
{
  protected:
	TUniformGridBase() {}
	TUniformGridBase(const TVector<T, d>& MinCorner, const TVector<T, d>& MaxCorner, const TVector<int32, d>& Cells, const uint32 GhostCells)
	    : MMinCorner(MinCorner), MMaxCorner(MaxCorner), MCells(Cells)
	{
		for (int32 Axis = 0; Axis < d; ++Axis)
		{
			check(MCells[Axis] != 0);
		}

		MDx = TVector<T, d>(MMaxCorner - MMinCorner) / MCells;
		if (GhostCells > 0)
		{
			MMinCorner -= MDx * GhostCells;
			MMaxCorner += MDx * GhostCells;
			MCells += TVector<T, d>(2 * GhostCells);
		}

		if (MDx >= TVector<T, d>(SMALL_NUMBER))
		{
			const TVector<T, d> MinToDXRatio = MMinCorner / MDx;
			for (int32 Axis = 0; Axis < d; ++Axis)
			{
				ensure(FMath::Abs(MinToDXRatio[Axis]) < 1e7); //make sure we have the precision we need
			}
		}
	}
	TUniformGridBase(std::istream& Stream)
	    : MMinCorner(Stream), MMaxCorner(Stream), MCells(Stream)
	{
		MDx = TVector<T, d>(MMaxCorner - MMinCorner) / MCells;
	}

	~TUniformGridBase() {}

  public:
	void Write(std::ostream& Stream) const
	{
		MMinCorner.Write(Stream);
		MMaxCorner.Write(Stream);
		MCells.Write(Stream);
	}
	void Serialize(FArchive& Ar)
	{
		Ar << MMinCorner;
		Ar << MMaxCorner;
		Ar << MCells;
		Ar << MDx;
	}

	TVector<T, d> Location(const TVector<int32, d>& Cell) const
	{
		return MDx * Cell + MMinCorner + (MDx / 2);
	}
	TVector<T, d> Location(const Pair<int32, TVector<int32, 3>>& Face) const
	{
		return MDx * Face.Second + MMinCorner + (TVector<T, d>(1) - TVector<T, d>::AxisVector(Face.First)) * (MDx / 2);
	}

#ifdef PLATFORM_COMPILER_CLANG
	// Disable optimization (-ffast-math) since its currently causing regressions.
	//		freciprocal-math:
	//		x / y = x * rccps(y) 
	//		rcpps is faster but less accurate (12 bits of precision), this can causes incorrect CellIdx
	DISABLE_FUNCTION_OPTIMIZATION 
#endif
	TVector<int32, d> Cell(const TVector<T, d>& X) const
	{
		const TVector<T, d> Delta = X - MMinCorner;
		TVector<int32, d> Result = Delta / MDx;
		for (int Axis = 0; Axis < d; ++Axis)
		{
			if (Delta[Axis] < 0)
			{
				Result[Axis] -= 1;	//negative snaps to the right which is wrong. Consider -50 x for DX of 100: -50 / 100 = 0 but we actually want -1
			}
		}
		return Result;
	}
	TVector<int32, d> Face(const TVector<T, d>& X, const int32 Component) const
	{
		return Cell(X + (MDx / 2) * TVector<T, d>::AxisVector(Component));
	}
	TVector<T, d> DomainSize() const
	{
		return (MMaxCorner - MMinCorner);
	}
	int32 GetNumCells() const
	{
		return MCells.Product();
	}
	template<class T_SCALAR>
	T_SCALAR LinearlyInterpolate(const TArrayND<T_SCALAR, d>& ScalarN, const TVector<T, d>& X) const;
	T LinearlyInterpolateComponent(const TArrayND<T, d>& ScalarNComponent, const TVector<T, d>& X, const int32 Axis) const;
	TVector<T, d> LinearlyInterpolate(const TArrayFaceND<T, d>& ScalarN, const TVector<T, d>& X) const;
	TVector<T, d> LinearlyInterpolate(const TArrayFaceND<T, d>& ScalarN, const TVector<T, d>& X, const Pair<int32, TVector<int32, d>> Index) const;
	const TVector<int32, d>& Counts() const { return MCells; }
	const TVector<T, d>& Dx() const { return MDx; }
	const TVector<T, d>& MinCorner() const { return MMinCorner; }
	const TVector<T, d>& MaxCorner() const { return MMaxCorner; }

  protected:
	TVector<T, d> MMinCorner;
	TVector<T, d> MMaxCorner;
	TVector<int32, d> MCells;
	TVector<T, d> MDx;
};

template<class T, int d>
class CHAOS_API TUniformGrid : public TUniformGridBase<T, d>
{
	using TUniformGridBase<T, d>::MCells;
	using TUniformGridBase<T, d>::MMinCorner;
	using TUniformGridBase<T, d>::MMaxCorner;
	using TUniformGridBase<T, d>::MDx;

  public:
	using TUniformGridBase<T, d>::Location;

	TUniformGrid() {}
	TUniformGrid(const TVector<T, d>& MinCorner, const TVector<T, d>& MaxCorner, const TVector<int32, d>& Cells, const uint32 GhostCells = 0)
	    : TUniformGridBase<T, d>(MinCorner, MaxCorner, Cells, GhostCells) {}
	TUniformGrid(std::istream& Stream)
	    : TUniformGridBase<T, d>(Stream) {}
	~TUniformGrid() {}
	TVector<int32, d> GetIndex(const int32 Index) const;
	TVector<T, d> Center(const int32 Index) const
	{
		return TUniformGridBase<T, d>::Location(GetIndex(Index));
	}
	TVector<int32, d> ClampIndex(const TVector<int32, d>& Index) const
	{
		TVector<int32, d> Result;
		for (int32 i = 0; i < d; ++i)
		{
			if (Index[i] >= MCells[i])
				Result[i] = MCells[i] - 1;
			else if (Index[i] < 0)
				Result[i] = 0;
			else
				Result[i] = Index[i];
		}
		return Result;
	}

	TVector<T, d> Clamp(const TVector<T, d>& X) const;
	TVector<T, d> ClampMinusHalf(const TVector<T, d>& X) const;
	
	bool IsValid(const TVector<int32, d>& X) const
	{
		return X == ClampIndex(X);
	}
};

template<class T>
class CHAOS_API TUniformGrid<T, 3> : public TUniformGridBase<T, 3>
{
	using TUniformGridBase<T, 3>::MCells;
	using TUniformGridBase<T, 3>::MMinCorner;
	using TUniformGridBase<T, 3>::MMaxCorner;
	using TUniformGridBase<T, 3>::MDx;

  public:
	using TUniformGridBase<T, 3>::GetNumCells;
	using TUniformGridBase<T, 3>::Location;

	TUniformGrid() {}
	TUniformGrid(const TVector<T, 3>& MinCorner, const TVector<T, 3>& MaxCorner, const TVector<int32, 3>& Cells, const uint32 GhostCells = 0)
	    : TUniformGridBase<T, 3>(MinCorner, MaxCorner, Cells, GhostCells) {}
	TUniformGrid(std::istream& Stream)
	    : TUniformGridBase<T, 3>(Stream) {}
	~TUniformGrid() {}
	TVector<int32, 3> GetIndex(const int32 Index) const;
	Pair<int32, TVector<int32, 3>> GetFaceIndex(int32 Index) const;
	int32 GetNumFaces() const
	{
		return GetNumCells() * 3 + MCells[0] * MCells[1] + MCells[1] * MCells[2] + MCells[0] * MCells[3];
	}
	TVector<T, 3> Center(const int32 Index) const
	{
		return TUniformGridBase<T, 3>::Location(GetIndex(Index));
	}
	TVector<int32, 3> ClampIndex(const TVector<int32, 3>& Index) const;
	TVector<T, 3> Clamp(const TVector<T, 3>& X) const;
	TVector<T, 3> ClampMinusHalf(const TVector<T, 3>& X) const;
	bool IsValid(const TVector<int32, 3>& X) const;
};

template <typename T, int d>
FArchive& operator<<(FArchive& Ar, TUniformGridBase<T, d>& Value)
{
	Value.Serialize(Ar);
	return Ar;
}

#if PLATFORM_MAC || PLATFORM_LINUX
extern template class CHAOS_API Chaos::TUniformGridBase<float, 3>;
extern template class CHAOS_API Chaos::TUniformGrid<float, 3>;
extern template class CHAOS_API Chaos::TUniformGrid<float, 2>;
#endif // __clang__

}
