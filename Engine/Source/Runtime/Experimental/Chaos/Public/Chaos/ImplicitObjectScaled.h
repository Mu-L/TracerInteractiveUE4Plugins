// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Box.h"
#include "Chaos/ImplicitObject.h"
#include "Chaos/Transform.h"
#include "ChaosArchive.h"
#include "Templates/ChooseClass.h"
#include "Templates/EnableIf.h"
#include "Math/NumericLimits.h"
#include "ChaosCheck.h"

namespace Chaos
{

struct FMTDInfo;

template <typename TConcrete>
class TImplicitObjectInstanced final : public FImplicitObject
{
public:
	using T = typename TConcrete::TType;
	using TType = T;
	static constexpr int d = TConcrete::D;
	static constexpr int D = d;
	using ObjectType = TSharedPtr<TConcrete,ESPMode::ThreadSafe>;

	using FImplicitObject::GetTypeName;

	//needed for serialization
	TImplicitObjectInstanced()
		: FImplicitObject(EImplicitObject::HasBoundingBox,StaticType())
		, OuterMargin(0)
	{}

	TImplicitObjectInstanced(const ObjectType&& Object, const FReal InMargin = 0)
		: FImplicitObject(EImplicitObject::HasBoundingBox, Object->GetType() | ImplicitObjectType::IsInstanced)
		, MObject(MoveTemp(Object))
		, OuterMargin(InMargin)
	{
		ensure(IsInstanced(MObject->GetType()) == false);	//cannot have an instance of an instance
		this->bIsConvex = MObject->IsConvex();
		this->bDoCollide = MObject->GetDoCollide();
		SetMargin(OuterMargin + MObject->GetMargin());
	}

	TImplicitObjectInstanced(const ObjectType& Object, const FReal InMargin = 0)
		: FImplicitObject(EImplicitObject::HasBoundingBox,Object->GetType() | ImplicitObjectType::IsInstanced)
		, MObject(Object)
		, OuterMargin(InMargin)
	{
		ensure(IsInstanced(MObject->GetType()) == false);	//cannot have an instance of an instance
		this->bIsConvex = MObject->IsConvex();
		this->bDoCollide = MObject->GetDoCollide();
		SetMargin(OuterMargin + MObject->GetMargin());
	}

	static constexpr EImplicitObjectType StaticType()
	{
		return TConcrete::StaticType() | ImplicitObjectType::IsInstanced;
	}

	const TConcrete* GetInstancedObject() const
	{
		return MObject.Get();
	}

	bool GetDoCollide() const
	{
		return MObject->GetDoCollide();
	}

	virtual T PhiWithNormal(const TVector<T, d>& X, TVector<T, d>& Normal) const override
	{
		return MObject->PhiWithNormal(X, Normal);
	}

	virtual bool Raycast(const TVector<T, d>& StartPoint, const TVector<T, d>& Dir, const T Length, const T Thickness, T& OutTime, TVector<T, d>& OutPosition, TVector<T, d>& OutNormal, int32& OutFaceIndex) const override
	{
		return MObject->Raycast(StartPoint, Dir, Length, GetMargin() + Thickness, OutTime, OutPosition, OutNormal, OutFaceIndex);
	}

	virtual void Serialize(FChaosArchive& Ar) override
	{
		FChaosArchiveScopedMemory ScopedMemory(Ar, GetTypeName(), false);
		FImplicitObject::SerializeImp(Ar);
		Ar << MObject;
	}

	virtual int32 FindMostOpposingFace(const TVector<T, d>& Position, const TVector<T, d>& UnitDir, int32 HintFaceIndex, T SearchDist) const override
	{
		return MObject->FindMostOpposingFace(Position, UnitDir, HintFaceIndex, SearchDist);
	}

	virtual TVector<T, 3> FindGeometryOpposingNormal(const TVector<T, d>& DenormDir, int32 HintFaceIndex, const TVector<T, d>& OriginalNormal) const override
	{
		return MObject->FindGeometryOpposingNormal(DenormDir, HintFaceIndex, OriginalNormal);
	}

	virtual bool Overlap(const TVector<T, d>& Point, const T Thickness) const override
	{
		return MObject->Overlap(Point, OuterMargin + Thickness);
	}

	// The support position from the specified direction, including margins
	FORCEINLINE TVector<T, d> Support(const TVector<T, d>& Direction, const T Thickness) const
	{
		return MObject->Support(Direction, OuterMargin + Thickness); 
	}

	// The support position from the specified direction, excluding margins
	FORCEINLINE TVector<T, d> SupportCore(const TVector<T, d>& Direction) const
	{
		return MObject->SupportCore(Direction);
	}

	virtual const TAABB<T, d> BoundingBox() const override 
	{ 
		if (OuterMargin == 0)
		{
			return MObject->BoundingBox();
		}
		else
		{
			return TAABB<T, d>(MObject->BoundingBox()).Thicken(OuterMargin);
		}
	}

	const ObjectType Object() const { return MObject; }

	virtual uint32 GetTypeHash() const override
	{
		return MObject->GetTypeHash();
	}

	virtual TUniquePtr<FImplicitObject> Copy() const override
	{
		return TUniquePtr<FImplicitObject>(CopyHelper(this));
	}

	static const TImplicitObjectInstanced<TConcrete>& AsInstancedChecked(const FImplicitObject& Obj)
	{
		if(TIsSame<TConcrete,FImplicitObject>::Value)
		{
			//can cast any instanced to ImplicitObject base
			check(IsInstanced(Obj.GetType()));
		} else
		{
			check(StaticType() == Obj.GetType());
		}
		return static_cast<const TImplicitObjectInstanced<TConcrete>&>(Obj);
	}

	/** This is a low level function and assumes the internal object has a SweepGeom function. Should not be called directly. See GeometryQueries.h : SweepQuery */
	template <typename QueryGeomType>
	bool LowLevelSweepGeom(const QueryGeomType& B,const TRigidTransform<T,d>& BToATM,const TVector<T,d>& LocalDir,const T Length,T& OutTime,TVector<T,d>& LocalPosition,TVector<T,d>& LocalNormal,int32& OutFaceIndex,T Thickness = 0,bool bComputeMTD = false) const
	{
		return MObject->SweepGeom(B,BToATM,LocalDir,Length,OutTime,LocalPosition,LocalNormal,OutFaceIndex,OuterMargin+Thickness,bComputeMTD);
	}

	/** This is a low level function and assumes the internal object has a OverlapGeom function. Should not be called directly. See GeometryQueries.h : OverlapQuery */
	template <typename QueryGeomType>
	bool LowLevelOverlapGeom(const QueryGeomType& B,const TRigidTransform<T,d>& BToATM,T Thickness = 0, FMTDInfo* OutMTD = nullptr) const
	{
		return MObject->OverlapGeom(B,BToATM,OuterMargin+Thickness, OutMTD);
	}

	virtual uint16 GetMaterialIndex(uint32 HintIndex) const
	{
		return MObject->GetMaterialIndex(HintIndex);
	}

protected:
	ObjectType MObject;
	FReal OuterMargin;

	static TImplicitObjectInstanced<TConcrete>* CopyHelper(const TImplicitObjectInstanced<TConcrete>* Obj)
	{
		return new TImplicitObjectInstanced<TConcrete>(Obj->MObject);
	}
};


template<typename TConcrete, bool bInstanced = true>
class TImplicitObjectScaled final : public FImplicitObject
{
public:
	using T = typename TConcrete::TType;
	using TType = T;
	static constexpr int d = TConcrete::D;
	static constexpr int D = d;

	using ObjectType = typename TChooseClass<bInstanced, TSerializablePtr<TConcrete>, TUniquePtr<TConcrete>>::Result;
	using FImplicitObject::GetTypeName;

	TImplicitObjectScaled(ObjectType Object, const TVector<T, d>& Scale, T InMargin = 0)
	    : FImplicitObject(EImplicitObject::HasBoundingBox, Object->GetType() | ImplicitObjectType::IsScaled)
	    , MObject(MoveTemp(Object))
		, MSharedPtrForRefCount(nullptr)
		, OuterMargin(InMargin)
	{
		ensureMsgf((IsScaled(MObject->GetType()) == false), TEXT("Scaled objects should not contain each other."));
		ensureMsgf((IsInstanced(MObject->GetType()) == false), TEXT("Scaled objects should not contain instances."));
		switch (MObject->GetType())
		{
		case ImplicitObjectType::Transformed:
		case ImplicitObjectType::Union:
			check(false);	//scale is only supported for concrete types like sphere, capsule, convex, levelset, etc... Nothing that contains other objects
		default:
			break;
		}
		this->bIsConvex = MObject->IsConvex();
		this->bDoCollide = MObject->GetDoCollide();
		SetScale(Scale);
	}

	TImplicitObjectScaled(TSharedPtr<TConcrete, ESPMode::ThreadSafe> Object, const TVector<T, d>& Scale, T InMargin = 0)
	    : FImplicitObject(EImplicitObject::HasBoundingBox, Object->GetType() | ImplicitObjectType::IsScaled)
	    , MObject(MakeSerializable<TConcrete, ESPMode::ThreadSafe>(Object))
		, MSharedPtrForRefCount(Object)
		, OuterMargin(InMargin)
	{
		ensureMsgf((IsScaled(MObject->GetType()) == false), TEXT("Scaled objects should not contain each other."));
		ensureMsgf((IsInstanced(MObject->GetType()) == false), TEXT("Scaled objects should not contain instances."));
		switch (MObject->GetType())
		{
		case ImplicitObjectType::Transformed:
		case ImplicitObjectType::Union:
			check(false);	//scale is only supported for concrete types like sphere, capsule, convex, levelset, etc... Nothing that contains other objects
		default:
			break;
		}
		this->bIsConvex = MObject->IsConvex();
		this->bDoCollide = MObject->GetDoCollide();
		SetScale(Scale);
	}

	TImplicitObjectScaled(ObjectType Object, TUniquePtr<Chaos::FImplicitObject> &&ObjectOwner, const TVector<T, d>& Scale, T InMargin = 0)
	    : FImplicitObject(EImplicitObject::HasBoundingBox, Object->GetType() | ImplicitObjectType::IsScaled)
	    , MObject(Object)
		, MSharedPtrForRefCount(nullptr)
		, OuterMargin(InMargin)
	{
		ensureMsgf((IsScaled(MObject->GetType(true)) == false), TEXT("Scaled objects should not contain each other."));
		ensureMsgf((IsInstanced(MObject->GetType(true)) == false), TEXT("Scaled objects should not contain instances."));
		this->bIsConvex = Object->IsConvex();
		this->bDoCollide = MObject->GetDoCollide();
		SetScale(Scale);
	}

	TImplicitObjectScaled(const TImplicitObjectScaled<TConcrete, bInstanced>& Other) = delete;
	TImplicitObjectScaled(TImplicitObjectScaled<TConcrete, bInstanced>&& Other)
	    : FImplicitObject(EImplicitObject::HasBoundingBox, Other.MObject->GetType() | ImplicitObjectType::IsScaled)
	    , MObject(MoveTemp(Other.MObject))
		, MSharedPtrForRefCount(MoveTemp(Other.MSharedPtrForRefCount))
	    , MScale(Other.MScale)
		, MInvScale(Other.MInvScale)
		, OuterMargin(Other.OuterMargin)
	    , MLocalBoundingBox(MoveTemp(Other.MLocalBoundingBox))
	{
		ensureMsgf((IsScaled(MObject->GetType()) == false), TEXT("Scaled objects should not contain each other."));
		ensureMsgf((IsInstanced(MObject->GetType()) == false), TEXT("Scaled objects should not contain instances."));
		this->bIsConvex = Other.MObject->IsConvex();
		this->bDoCollide = Other.MObject->GetDoCollide();
		SetMargin(Other.GetMargin());
	}
	~TImplicitObjectScaled() {}

	static constexpr EImplicitObjectType StaticType()
	{
		return TConcrete::StaticType() | ImplicitObjectType::IsScaled;
	}

	static const TImplicitObjectScaled<TConcrete>& AsScaledChecked(const FImplicitObject& Obj)
	{
		if (TIsSame<TConcrete, FImplicitObject>::Value)
		{
			//can cast any scaled to ImplicitObject base
			check(IsScaled(Obj.GetType()));
		}
		else
		{
			check(StaticType() == Obj.GetType());
		}
		return static_cast<const TImplicitObjectScaled<TConcrete>&>(Obj);
	}

	static TImplicitObjectScaled<TConcrete>& AsScaledChecked(FImplicitObject& Obj)
	{
		if (TIsSame<TConcrete, FImplicitObject>::Value)
		{
			//can cast any scaled to ImplicitObject base
			check(IsScaled(Obj.GetType()));
		}
		else
		{
			check(StaticType() == Obj.GetType());
		}
		return static_cast<TImplicitObjectScaled<TConcrete>&>(Obj);
	}

	static const TImplicitObjectScaled<TConcrete>* AsScaled(const FImplicitObject& Obj)
	{
		if (TIsSame<TConcrete, FImplicitObject>::Value)
		{
			//can cast any scaled to ImplicitObject base
			return IsScaled(Obj.GetType()) ? static_cast<const TImplicitObjectScaled<TConcrete>*>(&Obj) : nullptr;
		}
		else
		{
			return StaticType() == Obj.GetType() ? static_cast<const TImplicitObjectScaled<TConcrete>*>(&Obj) : nullptr;
		}
	}

	static TImplicitObjectScaled<TConcrete>* AsScaled(FImplicitObject& Obj)
	{
		if (TIsSame<TConcrete, FImplicitObject>::Value)
		{
			//can cast any scaled to ImplicitObject base
			return IsScaled(Obj.GetType()) ? static_cast<TImplicitObjectScaled<TConcrete>*>(&Obj) : nullptr;
		}
		else
		{
			return StaticType() == Obj.GetType() ? static_cast<TImplicitObjectScaled<TConcrete>*>(&Obj) : nullptr;
		}
	}

	const TConcrete* GetUnscaledObject() const
	{
		return MObject.Get();
	}

	virtual T PhiWithNormal(const TVector<T, d>& X, TVector<T, d>& Normal) const override
	{
		const TVector<T, d> UnscaledX = MInvScale * X;
		TVector<T, d> UnscaledNormal;
		const T UnscaledPhi = MObject->PhiWithNormal(UnscaledX, UnscaledNormal) - OuterMargin;
		Normal = MScale * UnscaledNormal;
		const T ScaleFactor = Normal.SafeNormalize();
		const T ScaledPhi = UnscaledPhi * ScaleFactor;
		return ScaledPhi;
	}

	virtual bool Raycast(const TVector<T, d>& StartPoint, const TVector<T, d>& Dir, const T Length, const T Thickness, T& OutTime, TVector<T, d>& OutPosition, TVector<T, d>& OutNormal, int32& OutFaceIndex) const override
	{
		ensure(Length > 0);
		ensure(FMath::IsNearlyEqual(Dir.SizeSquared(), 1, KINDA_SMALL_NUMBER));
		ensure(Thickness == 0 || (FMath::IsNearlyEqual(MScale[0], MScale[1]) && FMath::IsNearlyEqual(MScale[0], MScale[2])));	//non uniform turns sphere into an ellipsoid so no longer a raycast and requires a more expensive sweep

		const TVector<T, d> UnscaledStart = MInvScale * StartPoint;
		const TVector<T, d> UnscaledDirDenorm = MInvScale * Dir;
		const T LengthScale = UnscaledDirDenorm.Size();
		if (ensure(LengthScale > TNumericLimits<T>::Min()))
		{
			const T LengthScaleInv = 1.f / LengthScale;
			const T UnscaledLength = Length * LengthScale;
			const TVector<T, d> UnscaledDir = UnscaledDirDenorm * LengthScaleInv;
			
			TVector<T, d> UnscaledPosition;
			TVector<T, d> UnscaledNormal;
			float UnscaledTime;

			if (MObject->Raycast(UnscaledStart, UnscaledDir, UnscaledLength, OuterMargin + Thickness * MInvScale[0], UnscaledTime, UnscaledPosition, UnscaledNormal, OutFaceIndex))
			{
				//We double check that NewTime < Length because of potential precision issues. When that happens we always keep the shortest hit first
				const T NewTime = LengthScaleInv * UnscaledTime;
				if (NewTime < Length && NewTime != 0) // Normal/Position output may be uninitialized with TOI 0.
				{
					OutPosition = MScale * UnscaledPosition;
					OutNormal = (MInvScale * UnscaledNormal).GetSafeNormal(TNumericLimits<T>::Min());
					OutTime = NewTime;
					return true;
				}
			}
		}
			
		return false;
	}

	/** This is a low level function and assumes the internal object has a SweepGeom function. Should not be called directly. See GeometryQueries.h : SweepQuery */
	template <typename QueryGeomType>
	bool LowLevelSweepGeom(const QueryGeomType& B, const TRigidTransform<T, d>& BToATM, const TVector<T, d>& LocalDir, const T Length, T& OutTime, TVector<T, d>& LocalPosition, TVector<T, d>& LocalNormal, int32& OutFaceIndex, T Thickness = 0, bool bComputeMTD = false) const
	{
		ensure(Length > 0);
		ensure(FMath::IsNearlyEqual(LocalDir.SizeSquared(), 1, KINDA_SMALL_NUMBER));
		ensure(Thickness == 0 || (FMath::IsNearlyEqual(MScale[0], MScale[1]) && FMath::IsNearlyEqual(MScale[0], MScale[2])));

		const TVector<T, d> UnscaledDirDenorm = MInvScale * LocalDir;
		const T LengthScale = UnscaledDirDenorm.Size();
		if (ensure(LengthScale > TNumericLimits<T>::Min()))
		{
			const T LengthScaleInv = 1.f / LengthScale;
			const T UnscaledLength = Length * LengthScale;
			const TVector<T, d> UnscaledDir = UnscaledDirDenorm * LengthScaleInv;

			TVector<T, d> UnscaledPosition;
			TVector<T, d> UnscaledNormal;
			float UnscaledTime;

			auto ScaledB = MakeScaledHelper(B, MInvScale);

			TRigidTransform<T, d> BToATMNoScale(BToATM.GetLocation() * MInvScale, BToATM.GetRotation());
			
			if (MObject->SweepGeom(ScaledB, BToATMNoScale, UnscaledDir, UnscaledLength, UnscaledTime, UnscaledPosition, UnscaledNormal, OutFaceIndex, OuterMargin + Thickness, bComputeMTD, MScale))
			{
				const T NewTime = LengthScaleInv * UnscaledTime;
				//We double check that NewTime < Length because of potential precision issues. When that happens we always keep the shortest hit first
				if (NewTime < Length)
				{
					OutTime = NewTime;
					LocalPosition = MScale * UnscaledPosition;
					LocalNormal = (MInvScale * UnscaledNormal).GetSafeNormal();
					return true;
				}
			}
		}

		return false;
	}

	template <typename QueryGeomType>
	bool GJKContactPoint(const QueryGeomType& A, const FRigidTransform3& AToBTM, const FReal Thickness, FVec3& Location, FVec3& Normal, FReal& Penetration) const
	{
		TRigidTransform<T, d> AToBTMNoScale(AToBTM.GetLocation() * MInvScale, AToBTM.GetRotation());

		auto ScaledA = MakeScaledHelper(A, MInvScale);
		return MObject->GJKContactPoint(ScaledA, AToBTMNoScale, OuterMargin + Thickness, Location, Normal, Penetration, MScale);
	}

	/** This is a low level function and assumes the internal object has a OverlapGeom function. Should not be called directly. See GeometryQueries.h : OverlapQuery */
	template <typename QueryGeomType>
	bool LowLevelOverlapGeom(const QueryGeomType& B, const TRigidTransform<T, d>& BToATM, T Thickness = 0, FMTDInfo* OutMTD = nullptr) const
	{
		ensure(Thickness == 0 || (FMath::IsNearlyEqual(MScale[0], MScale[1]) && FMath::IsNearlyEqual(MScale[0], MScale[2])));

		auto ScaledB = MakeScaledHelper(B, MInvScale);
		TRigidTransform<T, d> BToATMNoScale(BToATM.GetLocation() * MInvScale, BToATM.GetRotation());
		return MObject->OverlapGeom(ScaledB, BToATMNoScale, OuterMargin + Thickness, OutMTD, MScale);
	}

	// Get the index of the plane that most opposes the normal
	int32 GetMostOpposingPlane(const FVec3& Normal) const
	{
		return MObject->GetMostOpposingPlane(GetInverseScaledNormal(Normal));
	}

	// Get the index of the plane that most opposes the normal, assuming it passes through the specified vertex
	int32 GetMostOpposingPlaneWithVertex(int32 VertexIndex, const FVec3& Normal) const
	{
		return MObject->GetMostOpposingPlane(VertexIndex, GetInverseScaledNormal(Normal));
	}

	// Get the set of planes that pass through the specified vertex
	TArrayView<const int32> GetVertexPlanes(int32 VertexIndex) const
	{
		return MObject->GetVertexPlanes(VertexIndex);
	}

	// Get the list of vertices that form the boundary of the specified face
	TArrayView<const int32> GetPlaneVertices(int32 FaceIndex) const
	{
		return MObject->GetPlaneVertices(FaceIndex);
	}

	int32 NumPlanes() const
	{
		return MObject->NumPlanes();
	}

	int32 NumVertices() const
	{
		return MObject->NumVertices();
	}

	// Get the plane at the specified index (e.g., indices from GetVertexPlanes)
	const TPlaneConcrete<FReal, 3> GetPlane(int32 FaceIndex) const
	{
		const TPlaneConcrete<FReal, 3> InnerPlane = MObject->GetPlane(FaceIndex);
		return TPlaneConcrete<FReal, 3>(MScale * InnerPlane.X(), GetScaledNormal(InnerPlane.Normal()));
	}

	// Get the vertex at the specified index (e.g., indices from GetPlaneVertices)
	const FVec3 GetVertex(int32 VertexIndex) const
	{
		const FVec3 InnerVertex = MObject->GetVertex(VertexIndex);
		return MScale * InnerVertex;
	}


	virtual int32 FindMostOpposingFace(const TVector<T, d>& Position, const TVector<T, d>& UnitDir, int32 HintFaceIndex, T SearchDist) const override
	{
		//ensure(OuterMargin == 0);	//not supported: do we care?
		ensure(FMath::IsNearlyEqual(UnitDir.SizeSquared(), 1, KINDA_SMALL_NUMBER));

		const TVector<T, d> UnscaledPosition = MInvScale * Position;
		const TVector<T, d> UnscaledDirDenorm = MInvScale * UnitDir;
		const float LengthScale = UnscaledDirDenorm.Size();
		const TVector<T, d> UnscaledDir
			= ensure(LengthScale > TNumericLimits<T>::Min())
			? UnscaledDirDenorm / LengthScale
			: TVector<T, d>(0.f, 0.f, 1.f);
		const T UnscaledSearchDist = SearchDist * MScale.Max();	//this is not quite right since it's no longer a sphere, but the whole thing is fuzzy anyway
		return MObject->FindMostOpposingFace(UnscaledPosition, UnscaledDir, HintFaceIndex, UnscaledSearchDist);
	}

	virtual TVector<T, 3> FindGeometryOpposingNormal(const TVector<T, d>& DenormDir, int32 HintFaceIndex, const TVector<T, d>& OriginalNormal) const override
	{
		//ensure(OuterMargin == 0);	//not supported: do we care?
		ensure(FMath::IsNearlyEqual(OriginalNormal.SizeSquared(), 1, KINDA_SMALL_NUMBER));

		// Get unscaled dir and normal
		const TVector<T, 3> LocalDenormDir = DenormDir * MInvScale;
		const TVector<T, 3> LocalOriginalNormalDenorm = OriginalNormal * MInvScale;
		const float NormalLengthScale = LocalOriginalNormalDenorm.Size();
		const TVector<T, 3> LocalOriginalNormal
			= ensure(NormalLengthScale > SMALL_NUMBER)
			? LocalOriginalNormalDenorm / NormalLengthScale
			: TVector<T, d>(0, 0, 1);

		// Compute final normal
		const TVector<T, d> LocalNormal = MObject->FindGeometryOpposingNormal(LocalDenormDir, HintFaceIndex, LocalOriginalNormal);
		TVector<T, d> Normal = LocalNormal;
		if (CHAOS_ENSURE(Normal.SafeNormalize(TNumericLimits<T>::Min())) == 0)
		{
			Normal = TVector<T,3>(0,0,1);
		}

		return Normal;
	}

	virtual bool Overlap(const TVector<T, d>& Point, const T Thickness) const override
	{
		const TVector<T, d> UnscaledPoint = MInvScale * Point;

		// TODO: consider alternative that handles thickness scaling properly in 3D, only works for uniform scaling right now
		const T UnscaleThickness = MInvScale[0] * Thickness; 

		return MObject->Overlap(UnscaledPoint, OuterMargin + UnscaleThickness);
	}

	virtual Pair<TVector<T, d>, bool> FindClosestIntersectionImp(const TVector<T, d>& StartPoint, const TVector<T, d>& EndPoint, const T Thickness) const override
	{
		ensure(OuterMargin == 0);	//not supported: do we care?
		const TVector<T,d> UnscaledStart = MInvScale * StartPoint;
		const TVector<T, d> UnscaledEnd = MInvScale * EndPoint;
		auto ClosestIntersection = MObject->FindClosestIntersection(UnscaledStart, UnscaledEnd, Thickness);
		if (ClosestIntersection.Second)
		{
			ClosestIntersection.First = MScale * ClosestIntersection.First;
		}
		return ClosestIntersection;
	}

	virtual int32 FindClosestFaceAndVertices(const FVec3& Position, TArray<FVec3>& FaceVertices, FReal SearchDist = 0.01) const override
	{
		const FVec3 UnscaledPoint = MInvScale * Position;
		const FReal UnscaledSearchDist = SearchDist * MInvScale.Max();	//this is not quite right since it's no longer a sphere, but the whole thing is fuzzy anyway
		int32 FaceIndex =  MObject->FindClosestFaceAndVertices(UnscaledPoint, FaceVertices, UnscaledSearchDist);
		if (FaceIndex != INDEX_NONE)
		{
			for (FVec3& Vec : FaceVertices)
			{
				Vec = Vec * MScale;
			}
		}
		return FaceIndex;
	}


	FORCEINLINE_DEBUGGABLE TVector<T, d> Support(const TVector<T, d>& Direction, const T Thickness) const
	{
		// Support_obj(dir) = pt => for all x in obj, pt \dot dir >= x \dot dir
		// We want Support_objScaled(dir) = Support_obj(dir') where dir' is some modification of dir so we can use the unscaled support function
		// If objScaled = Aobj where A is a transform, then we can say Support_objScaled(dir) = pt => for all x in obj, pt \dot dir >= Ax \dot dir
		// But this is the same as pt \dot dir >= dir^T Ax = (dir^TA) x = (A^T dir)^T x
		//So let dir' = A^T dir.
		//Since we only support scaling on the principal axes A is a diagonal (and therefore symmetric) matrix and so a simple component wise multiplication is sufficient
		const TVector<T, d> UnthickenedPt = MObject->Support(Direction * MScale, OuterMargin) * MScale;
		return Thickness > 0 ? TVector<T, d>(UnthickenedPt + Direction.GetSafeNormal() * Thickness) : UnthickenedPt;
	}

	FORCEINLINE_DEBUGGABLE TVector<T, d> SupportCore(const TVector<T, d>& Direction) const
	{
		return MObject->SupportCore(Direction * MScale) * MScale;
	}

	const TVector<T, d>& GetScale() const { return MScale; }
	const TVector<T, d>& GetInvScale() const { return MInvScale; }
	void SetScale(const TVector<T, d>& Scale)
	{
		constexpr T MinMagnitude = 1e-6;
		for (int Axis = 0; Axis < d; ++Axis)
		{
			if (!CHAOS_ENSURE(FMath::Abs(Scale[Axis]) >= MinMagnitude))
			{
				MScale[Axis] = MinMagnitude;
			}
			else
			{
				MScale[Axis] = Scale[Axis];
			}

			MInvScale[Axis] = 1 / MScale[Axis];
		}
		SetMargin(OuterMargin + Scale[0] * MObject->GetMargin());
		UpdateBounds();
	}

	virtual const TAABB<T, d> BoundingBox() const override { return MLocalBoundingBox; }

	const FReal GetVolume() const
	{
		// TODO: More precise volume!
		return BoundingBox().GetVolume();
	}

	const FMatrix33 GetInertiaTensor(const FReal Mass) const
	{
		// TODO: More precise inertia!
		return BoundingBox().GetInertiaTensor(Mass);
	}

	const FVec3 GetCenterOfMass() const
	{
		// TODO: I'm not sure this is correct in all cases
		return MScale * MObject->GetCenterOfMass();
	}

	const ObjectType Object() const { return MObject; }

	// Only should be retrieved for copy purposes. Do not modify or access.
	TSharedPtr<TConcrete, ESPMode::ThreadSafe> GetSharedObject() const { return MSharedPtrForRefCount; }
	
	virtual void Serialize(FChaosArchive& Ar) override
	{
		FChaosArchiveScopedMemory ScopedMemory(Ar, GetTypeName(), false);
		FImplicitObject::SerializeImp(Ar);
		Ar << MObject << MScale << MInvScale;
		TBox<T,d>::SerializeAsAABB(Ar, MLocalBoundingBox);
		ensure(OuterMargin == 0);	//not supported: do we care?

		Ar.UsingCustomVersion(FExternalPhysicsCustomObjectVersion::GUID);
		if (Ar.CustomVer(FExternalPhysicsCustomObjectVersion::GUID) < FExternalPhysicsCustomObjectVersion::ScaledGeometryIsConcrete)
		{
			this->Type = MObject->GetType() | ImplicitObjectType::IsScaled;	//update type so downcasts work
		}
	}

	virtual uint32 GetTypeHash() const override
	{
		return HashCombine(MObject->GetTypeHash(), ::GetTypeHash(MScale));
	}

	virtual uint16 GetMaterialIndex(uint32 HintIndex) const override
	{
		return MObject->GetMaterialIndex(HintIndex);
	}

#if 0
	virtual TUniquePtr<FImplicitObject> Copy() const override
	{
		return TUniquePtr<FImplicitObject>(CopyHelper(this));
	}
#endif
private:
	ObjectType MObject;
	TSharedPtr<TConcrete, ESPMode::ThreadSafe> MSharedPtrForRefCount; // Temporary solution to force ref counting on trianglemesh from body setup.
	TVector<T, d> MScale;
	TVector<T, d> MInvScale;
	T OuterMargin;	//Allows us to inflate the instance before the scale is applied. This is useful when sweeps need to apply a non scale on a geometry with uniform thickness
	TAABB<T, d> MLocalBoundingBox;

	//needed for serialization
	TImplicitObjectScaled()
	: FImplicitObject(EImplicitObject::HasBoundingBox, StaticType())
	, OuterMargin(0)
	{}
	friend FImplicitObject;	//needed for serialization

	friend class FImplicitObjectScaled;

	static TImplicitObjectScaled<TConcrete, true>* CopyHelper(const TImplicitObjectScaled<TConcrete, true>* Obj)
	{
		return new TImplicitObjectScaled<TConcrete, true>(Obj->MObject, Obj->MScale, Obj->OuterMargin);
	}

	static TImplicitObjectScaled<TConcrete, false>* CopyHelper(const TImplicitObjectScaled<TConcrete, false>* Obj)
	{
		return new TImplicitObjectScaled<TConcrete, false>(Obj->MObject->Copy(), Obj->MScale, Obj->OuterMargin);
	}

	// Convert a normal in the scaled object space into a normal in the inner object space.
	FVec3 GetInverseScaledNormal(const TVector<T, d>& OuterNormal) const
	{
		const TVector<T, d> UnscaledDirDenorm = MInvScale * OuterNormal;
		const float LengthScale = UnscaledDirDenorm.Size();
		const TVector<T, d> UnscaledDir
			= ensure(LengthScale > TNumericLimits<T>::Min())
			? UnscaledDirDenorm / LengthScale
			: TVector<T, d>(0.f, 0.f, 1.f);
		return UnscaledDir;
	}

	// Convert a normal in the inner object space (unscaled) into a normal in the outer scaled object space
	FVec3 GetScaledNormal(const TVector<T, d>& InnerNormal) const
	{
		const TVector<T, d> ScaledDirDenorm = MScale * InnerNormal;
		const float LengthScale = ScaledDirDenorm.Size();
		const TVector<T, d> ScaledDir
			= ensure(LengthScale > TNumericLimits<T>::Min())
			? ScaledDirDenorm / LengthScale
			: TVector<T, d>(0.f, 0.f, 1.f);
		return ScaledDir;
	}

	void UpdateBounds()
	{
		const TAABB<T, d> UnscaledBounds = MObject->BoundingBox();
		const TVector<T, d> Vector1 = UnscaledBounds.Min() * MScale;
		MLocalBoundingBox = TAABB<T, d>(Vector1, Vector1);	//need to grow it out one vector at a time in case scale is negative
		const TVector<T, d> Vector2 = UnscaledBounds.Max() *MScale;
		MLocalBoundingBox.GrowToInclude(Vector2);
		MLocalBoundingBox.Thicken(OuterMargin);
	}

	template <typename QueryGeomType>
	static auto MakeScaledHelper(const QueryGeomType& B, const TVector<T,d>& InvScale )
	{
		TUniquePtr<QueryGeomType> HackBPtr(const_cast<QueryGeomType*>(&B));	//todo: hack, need scaled object to accept raw ptr similar to transformed implicit
		TImplicitObjectScaled<QueryGeomType> ScaledB(MakeSerializable(HackBPtr), InvScale);
		HackBPtr.Release();
		return ScaledB;
	}

	template <typename QueryGeomType>
	static auto MakeScaledHelper(const TImplicitObjectScaled<QueryGeomType>& B, const TVector<T,d>& InvScale)
	{
		//if scaled of scaled just collapse into one scaled
		TImplicitObjectScaled<QueryGeomType> ScaledB(B.Object(), InvScale * B.GetScale());
		return ScaledB;
	}

};

template <typename TConcrete>
using TImplicitObjectScaledNonSerializable = TImplicitObjectScaled<TConcrete, false>;

template <typename T, int d>
using TImplicitObjectScaledGeneric = TImplicitObjectScaled<FImplicitObject>;

}
