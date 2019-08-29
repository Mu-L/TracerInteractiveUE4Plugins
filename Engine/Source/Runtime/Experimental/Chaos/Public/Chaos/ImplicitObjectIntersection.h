// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Array.h"
#include "Chaos/ImplicitObject.h"

#include <memory>

namespace Chaos
{
template<class T, int d>
class TImplicitObjectIntersection : public TImplicitObject<T, d>
{
  public:
	using TImplicitObject<T, d>::SignedDistance;

	TImplicitObjectIntersection(TArray<TUniquePtr<TImplicitObject<T, d>>>&& Objects)
	    : TImplicitObject<T, d>(EImplicitObject::HasBoundingBox)
	    , MObjects(MoveTemp(Objects))
	    , MLocalBoundingBox(MObjects[0]->BoundingBox())
	{
		for (int32 i = 1; i < MObjects.Num(); ++i)
		{
			MLocalBoundingBox.ShrinkToInclude(MObjects[i]->BoundingBox());
		}
	}
	TImplicitObjectIntersection(const TImplicitObjectIntersection<T, d>& Other) = delete;
	TImplicitObjectIntersection(TImplicitObjectIntersection<T, d>&& Other)
	    : TImplicitObject<T, d>(EImplicitObject::HasBoundingBox)
	    , MObjects(MoveTemp(Other.MObjects))
	    , MLocalBoundingBox(MoveTemp(Other.MLocalBoundingBox))
	{
	}
	virtual ~TImplicitObjectIntersection() {}

	virtual T PhiWithNormal(const TVector<T, d>& x, TVector<T, d>& Normal) const override
	{
		check(MObjects.Num());
		T Phi = MObjects[0]->PhiWithNormal(x, Normal);
		for (int32 i = 1; i < MObjects.Num(); ++i)
		{
			TVector<T, d> NextNormal;
			T NextPhi = MObjects[i]->PhiWithNormal(x, NextNormal);
			if (NextPhi > Phi)
			{
				Phi = NextPhi;
				Normal = NextNormal;
			}
			else if (NextPhi == Phi)
			{
				Normal += NextNormal;
			}
		}
		Normal.Normalize();
		return Phi;
	}

	virtual const TBox<T,d>& BoundingBox() const { return MLocalBoundingBox; }

private:
	virtual Pair<TVector<T, d>, bool> FindClosestIntersectionImp(const TVector<T, d>& StartPoint, const TVector<T, d>& EndPoint, const T Thickness) const
	{
		TArray<Pair<T, TVector<T, d>>> Intersections;
		for (int32 i = 0; i < MObjects.Num(); ++i)
		{
			auto NextClosestIntersection = MObjects[i]->FindClosestIntersection(StartPoint, EndPoint, Thickness);
			if (!NextClosestIntersection.Second)
				continue;
			Intersections.Add(MakePair((NextClosestIntersection.First - StartPoint).Size(), NextClosestIntersection.First));
		}
		Intersections.Sort([](const Pair<T, TVector<T, d>>& Elem1, const Pair<T, TVector<T, d>>& Elem2) { return Elem1.First < Elem2.First; });
		for (int32 i = 0; i < Intersections.Num(); ++i)
		{
			if (SignedDistance(Intersections[i].Second) <= (Thickness + 1e-4))
			{
				return MakePair(Intersections[i].Second, true);
			}
		}
		return MakePair(TVector<T, d>(0), false);
	}

  private:
	TArray<TUniquePtr<TImplicitObject<T, d>>> MObjects;
	TBox<T, d> MLocalBoundingBox;
};
}
