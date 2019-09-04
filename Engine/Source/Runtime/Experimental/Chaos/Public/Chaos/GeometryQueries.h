// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/ImplicitObject.h"
#include "Chaos/Plane.h"
#include "Chaos/Transform.h"
#include "Chaos/TriangleMeshImplicitObject.h"
#include "Chaos/HeightField.h"
#include "ImplicitObjectScaled.h"

#include "ChaosArchive.h"
#include <algorithm>
#include <utility>
#include "GJK.h"

namespace Chaos
{
	template <typename T, int d>
	bool OverlapQuery(const TImplicitObject<T, d>& A, const TRigidTransform<T,d>& ATM, const TImplicitObject<T, d>& B, const TRigidTransform<T,d>& BTM, const T Thickness = 0)
	{
		const ImplicitObjectType AType = A.GetType(true);
		const ImplicitObjectType BType = B.GetType(true);
		
		if (AType == ImplicitObjectType::Transformed)
		{
			const TImplicitObjectTransformed<T, d>& TransformedA = static_cast<const TImplicitObjectTransformed<T, d>&>(A);
			const TRigidTransform<T, d> NewATM = TransformedA.GetTransform() * ATM;
			return OverlapQuery(*TransformedA.GetTransformedObject(), NewATM, B, BTM, Thickness);
		}

		if (BType == ImplicitObjectType::Transformed)
		{
			const TImplicitObjectTransformed<T, d>& TransformedB = static_cast<const TImplicitObjectTransformed<T, d>&>(B);
			const TRigidTransform<T, d> NewBTM = TransformedB.GetTransform() * BTM;
			return OverlapQuery(A, ATM, *TransformedB.GetTransformedObject(), NewBTM, Thickness);
		}

		check(B.IsConvex());	//Query object must be convex
		const TRigidTransform<T, d> BToATM = BTM.GetRelativeTransform(ATM);

		if(BType == ImplicitObjectType::Sphere)
		{
			const TSphere<T, d>& BSphere = static_cast<const TSphere<T, d>&>(B);
			const TVector<T, d> PtInA = BToATM.TransformPositionNoScale(BSphere.Center());
			return A.Overlap(PtInA, Thickness + BSphere.Radius());
		}
		//todo: A is a sphere
		else if (A.IsConvex())
		{
			const TVector<T, d> Offset = ATM.GetLocation() - BTM.GetLocation();
			return GJKIntersection(A, B, BToATM, Thickness, Offset.SizeSquared() < 1e-4 ? TVector<T, d>(1, 0, 0) : Offset);
		}
		else
		{
			switch (AType)
			{
			case ImplicitObjectType::HeightField:
			{
				const THeightField<T>& AHeightField = static_cast<const THeightField<T>&>(A);
				return AHeightField.OverlapGeom(B, BToATM, Thickness);
			}
			case ImplicitObjectType::TriangleMesh:
			{
				const TTriangleMeshImplicitObject<T>& ATriangleMesh = static_cast<const TTriangleMeshImplicitObject<T>&>(A);
				return ATriangleMesh.OverlapGeom(B, BToATM, Thickness);
			}
			case ImplicitObjectType::Scaled:
			{
				const TImplicitObjectScaled<T, d>& AScaled = static_cast<const TImplicitObjectScaled<T, d>&>(A);
				const TImplicitObject<T, d>& UnscaledObj = *AScaled.GetUnscaledObject();
				check(UnscaledObj.GetType(true) == ImplicitObjectType::TriangleMesh);
				const TTriangleMeshImplicitObject<T>& ATriangleMesh = static_cast<const TTriangleMeshImplicitObject<T>&>(UnscaledObj);
				return ATriangleMesh.OverlapGeom(B, BToATM, Thickness, AScaled.GetScale());
			}
			default:
				check(false);	//unsupported query type

			}
		}

		return false;
	}

	template <typename T, int d>
	bool SweepQuery(const TImplicitObject<T, d>& A, const TRigidTransform<T,d>& ATM, const TImplicitObject<T, d>& B, const TRigidTransform<T, d>& BTM, const TVector<T,d>& Dir, const T Length, T& OutTime, TVector<T,d>& OutPosition, TVector<T,d>& OutNormal, const T Thickness = 0)
	{
		const ImplicitObjectType AType = A.GetType(true);
		const ImplicitObjectType BType = B.GetType(true);

		bool bResult = false;
		
		if (AType == ImplicitObjectType::Transformed)
		{
			const TImplicitObjectTransformed<T, d>& TransformedA = static_cast<const TImplicitObjectTransformed<T, d>&>(A);
			const TRigidTransform<T, d> NewATM = TransformedA.GetTransform() * ATM;
			return SweepQuery(*TransformedA.GetTransformedObject(), NewATM, B, BTM, Dir, Length, OutTime, OutPosition, OutNormal, Thickness);
		}

		if (BType == ImplicitObjectType::Transformed)
		{
			const TImplicitObjectTransformed<T, d>& TransformedB = static_cast<const TImplicitObjectTransformed<T, d>&>(B);
			const TRigidTransform<T, d> NewBTM = TransformedB.GetTransform() * BTM;
			return SweepQuery(A, ATM, *TransformedB.GetTransformedObject(), NewBTM, Dir, Length, OutTime, OutPosition, OutNormal, Thickness);
		}

		check(B.IsConvex());	//Object being swept must be convex
		
		TVector<T, d> LocalPosition;
		TVector<T, d> LocalNormal;

		const TRigidTransform<T, d> BToATM = BTM.GetRelativeTransform(ATM);
		const TVector<T, d> LocalDir = ATM.InverseTransformVectorNoScale(Dir);

		if (BType == ImplicitObjectType::Sphere)
		{
			const TSphere<T, d>& BSphere = static_cast<const TSphere<T, d>&>(B);
			const TVector<T, d> Start = BToATM.TransformPositionNoScale(BSphere.Center());
			bResult = B.Raycast(Start, LocalDir, Length, Thickness + BSphere.Radius(), OutTime, LocalPosition, LocalNormal);
		}
		//todo: handle case where A is a sphere
		else if (A.IsConvex())
		{
			const TVector<T, d> Offset = ATM.GetLocation() - BTM.GetLocation();
			bResult = GJKRaycast<T>(A, B, BToATM, BToATM.GetLocation(), LocalDir, Length, OutTime, LocalPosition, LocalNormal, Thickness, Offset);
		}
		else
		{
			switch (AType)
			{
			case ImplicitObjectType::HeightField:
			{
				const THeightField<T>& AHeightField = static_cast<const THeightField<T>&>(A);
				bResult = AHeightField.SweepGeom(B, BToATM, LocalDir, Length, OutTime, LocalPosition, LocalNormal, Thickness);
				break;
			}
			case ImplicitObjectType::TriangleMesh:
			{
				const TTriangleMeshImplicitObject<T>& ATriangleMesh = static_cast<const TTriangleMeshImplicitObject<T>&>(A);
				bResult = ATriangleMesh.SweepGeom(B, BToATM, LocalDir, Length, OutTime, LocalPosition, LocalNormal, Thickness);
				break;
			}
			case ImplicitObjectType::Scaled:
			{
				const TImplicitObjectScaled<T, d>& AScaled = static_cast<const TImplicitObjectScaled<T, d>&>(A);
				const TImplicitObject<T, d>& UnscaledObj = *AScaled.GetUnscaledObject();
				check(UnscaledObj.GetType(true) == ImplicitObjectType::TriangleMesh);
				const TTriangleMeshImplicitObject<T>& ATriangleMesh = static_cast<const TTriangleMeshImplicitObject<T>&>(UnscaledObj);
				bResult = ATriangleMesh.SweepGeom(B, BToATM, LocalDir, Length, OutTime, LocalPosition, LocalNormal, Thickness, AScaled.GetScale());
				break;
			}
			default:
				check(false);	//unsupported query type

			}
		}

		//put back into world space
		OutNormal = ATM.TransformVectorNoScale(LocalNormal);
		OutPosition = ATM.TransformPositionNoScale(LocalPosition);

		return bResult;
	}
}
