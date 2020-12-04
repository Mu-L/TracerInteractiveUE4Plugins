// Copyright Epic Games, Inc. All Rights Reserved.

#include "BuoyancyComponent.h"
#include "WaterBodyActor.h"
#include "DrawDebugHelpers.h"
#include "WaterSplineComponent.h"
#include "WaterVersion.h"
#include "Physics/SimpleSuspension.h"

TAutoConsoleVariable<int32> CVarWaterDebugBuoyancy(
	TEXT("r.Water.DebugBuoyancy"),
	0,
	TEXT("Enable debug drawing for water interactions."),
	ECVF_Default);

TAutoConsoleVariable<int32> CVarWaterUseSplineKeyOptimization(
	TEXT("r.Water.UseSplineKeyOptimization"),
	1,
	TEXT("Whether to cache spline input key for water bodies."),
	ECVF_Default);

UBuoyancyComponent::UBuoyancyComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, SimulatingComponent(nullptr)
	, PontoonConfiguration(0)
	, VelocityPontoonIndex(0)
	, bIsOverlappingWaterBody(false)
	, bIsInWaterBody(false)
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

void UBuoyancyComponent::BeginPlay()
{
	Super::BeginPlay();
	for (FSphericalPontoon& Pontoon : BuoyancyData.Pontoons)
	{
		if (Pontoon.CenterSocket != NAME_None)
		{
			Pontoon.bUseCenterSocket = true;
		}
	}
	if (AActor* Owner = GetOwner())
	{
		SimulatingComponent = Cast<UPrimitiveComponent>(Owner->GetRootComponent());
		if (SimulatingComponent)
		{
			SetupWaterBodyOverlaps();
		}
	}
}

void UBuoyancyComponent::PostLoad()
{
	Super::PostLoad();

	if (GetLinkerCustomVersion(FWaterCustomVersion::GUID) < FWaterCustomVersion::UpdateBuoyancyComponentPontoonsData)
	{
		if (Pontoons_DEPRECATED.Num())
		{
			BuoyancyData.Pontoons = Pontoons_DEPRECATED;
		}
		Pontoons_DEPRECATED.Empty();
	}
}

void UBuoyancyComponent::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);

	Ar.UsingCustomVersion(FWaterCustomVersion::GUID);
}

const float ToKmh(float SpeedCms)
{
	return SpeedCms * 0.036f; //cm/s to km/h
}

void UBuoyancyComponent::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (!SimulatingComponent)
	{
		return;
	}
	const FVector PhysicsVelocity = SimulatingComponent->GetComponentVelocity();

	const FVector ForwardDir = SimulatingComponent->GetForwardVector();
	const FVector RightDir = SimulatingComponent->GetRightVector();

	const float ForwardSpeed = FVector::DotProduct(ForwardDir, PhysicsVelocity);
	const float ForwardSpeedKmh = ToKmh(ForwardSpeed);

	const float RightSpeed = FVector::DotProduct(RightDir, PhysicsVelocity);
	const float RightSpeedKmh = ToKmh(RightSpeed);

	UpdatePontoonCoefficients();

	const int32 NumPontoonsInWater = UpdatePontoons(DeltaTime, ForwardSpeed, ForwardSpeedKmh, SimulatingComponent);
	bIsInWaterBody = NumPontoonsInWater > 0;

	if (SimulatingComponent->IsSimulatingPhysics())
	{
		const ECollisionEnabled::Type Collision = SimulatingComponent->GetCollisionEnabled();
		if (Collision == ECollisionEnabled::QueryAndPhysics || Collision == ECollisionEnabled::PhysicsOnly)
		{
			ApplyBuoyancy(SimulatingComponent);

			FVector TotalForce = FVector::ZeroVector;
			FVector TotalTorque = FVector::ZeroVector;

			TotalForce += ComputeWaterForce(DeltaTime, PhysicsVelocity);

			if (BuoyancyData.bApplyDragForcesInWater)
			{
				TotalForce  += ComputeLinearDragForce(PhysicsVelocity);
				TotalTorque += ComputeAngularDragTorque(SimulatingComponent->GetPhysicsAngularVelocityInDegrees());
			}

			SimulatingComponent->AddForce(TotalForce, NAME_None, /*bAccelChange=*/true);
			SimulatingComponent->AddTorqueInDegrees(TotalTorque, NAME_None, /*bAccelChange=*/true);
		}
	}
}

void UBuoyancyComponent::EnableTick()
{
	SetComponentTickEnabled(true);
}

void UBuoyancyComponent::DisableTick()
{
	SetComponentTickEnabled(false);
}

void UBuoyancyComponent::SetupWaterBodyOverlaps()
{
	//SimulatingComponent->SetCollisionObjectType(ECollisionChannel::ECC_PhysicsBody);
	if (SimulatingComponent->GetCollisionResponseToChannel(ECollisionChannel::ECC_WorldStatic) == ECollisionResponse::ECR_Ignore)
	{
		SimulatingComponent->SetCollisionResponseToChannel(ECollisionChannel::ECC_WorldStatic, ECollisionResponse::ECR_Overlap);
	}
	SimulatingComponent->SetGenerateOverlapEvents(true);
}

void UBuoyancyComponent::AddCustomPontoon(float Radius, FName CenterSocketName)
{
	FSphericalPontoon Pontoon;
	Pontoon.Radius = Radius;
	Pontoon.CenterSocket = CenterSocketName;
	BuoyancyData.Pontoons.Add(Pontoon);
}

void UBuoyancyComponent::AddCustomPontoon(float Radius, const FVector& RelativeLocation)
{
	FSphericalPontoon Pontoon;
	Pontoon.Radius = Radius;
	Pontoon.RelativeLocation = RelativeLocation;
	BuoyancyData.Pontoons.Add(Pontoon);
}

void UBuoyancyComponent::EnteredWaterBody(AWaterBody* WaterBody)
{
	bool bIsFirstBody = !CurrentWaterBodies.Num() && WaterBody;
	CurrentWaterBodies.AddUnique(WaterBody);
	for (FSphericalPontoon& Pontoon : BuoyancyData.Pontoons)
	{
		Pontoon.SplineSegments.FindOrAdd(WaterBody, -1);
	}
	if (bIsFirstBody)
	{
		bIsOverlappingWaterBody = true;
	}
	EnableTick();
}

void UBuoyancyComponent::ExitedWaterBody(AWaterBody* WaterBody)
{
	CurrentWaterBodies.Remove(WaterBody);
	for (FSphericalPontoon& Pontoon : BuoyancyData.Pontoons)
	{
		Pontoon.SplineSegments.Remove(WaterBody);
	}
	if (!CurrentWaterBodies.Num())
	{
		bIsOverlappingWaterBody = false;
	}
	DisableTick();
}

void UBuoyancyComponent::ApplyBuoyancy(UPrimitiveComponent* PrimitiveComponent)
{
	check(GetOwner());

	if (PrimitiveComponent && bIsOverlappingWaterBody)
	{
		int PontoonIndex = 0;
		for (const FSphericalPontoon& Pontoon : BuoyancyData.Pontoons)
		{
			if (PontoonConfiguration & (1 << PontoonIndex))
			{
				PrimitiveComponent->AddForceAtLocation(Pontoon.LocalForce, Pontoon.CenterLocation);
			}
			PontoonIndex++;
		}
	}
}

void UBuoyancyComponent::ComputeBuoyancy(FSphericalPontoon& Pontoon, float ForwardSpeedKmh)
{
	AActor* Owner = GetOwner();
	check(Owner);

	auto ComputeBuoyantForce = [&](FVector CenterLocation, float Radius, float InBuoyancyCoefficient, float CurrentWaterLevel) -> float
	{
		const float Bottom = CenterLocation.Z - Radius;
		const float SubDiff = FMath::Clamp(CurrentWaterLevel - Bottom, 0.f, 2.f * Radius);

		// The following was obtained by integrating the volume of a sphere
		// over a linear section of SubmersionDiff length.
		static const float Pi = (float)PI;
		const float SubDiffSq = SubDiff * SubDiff;
		const float SubVolume = (Pi / 3.f) * SubDiffSq * ((3.f * Radius) - SubDiff);

#if ENABLE_DRAW_DEBUG
		if (CVarWaterDebugBuoyancy.GetValueOnAnyThread())
		{
			const FVector WaterPoint = FVector(CenterLocation.X, CenterLocation.Y, CurrentWaterLevel);
			DrawDebugLine(GetWorld(), WaterPoint - 50.f * FVector::ForwardVector, WaterPoint + 50.f * FVector::ForwardVector, FColor::Blue, false, -1.f, 0, 3.f);
			DrawDebugLine(GetWorld(), WaterPoint - 50.f * FVector::RightVector, WaterPoint + 50.f * FVector::RightVector, FColor::Blue, false, -1.f, 0, 3.f);
		}
#endif

		check(SimulatingComponent && SimulatingComponent->GetBodyInstance());
		const float VelocityZ = SimulatingComponent->GetBodyInstance()->GetUnrealWorldVelocity().Z;
		const float FirstOrderDrag = BuoyancyData.BuoyancyDamp * VelocityZ;
		const float SecondOrderDrag = FMath::Sign(VelocityZ) * BuoyancyData.BuoyancyDamp2 * VelocityZ * VelocityZ;
		const float DampingFactor = -FMath::Max(FirstOrderDrag + SecondOrderDrag, 0.f);
		// The buoyant force scales with submersed volume
		return SubVolume * (InBuoyancyCoefficient) + DampingFactor;
	};

	const float MinVelocity = BuoyancyData.BuoyancyRampMinVelocity;
	const float MaxVelocity = BuoyancyData.BuoyancyRampMaxVelocity;
	const float RampFactor = FMath::Clamp((ForwardSpeedKmh - MinVelocity) / (MaxVelocity - MinVelocity), 0.f, 1.f);
	const float BuoyancyRamp = RampFactor * (BuoyancyData.BuoyancyRampMax - 1);
	float BuoyancyCoefficientWithRamp = BuoyancyData.BuoyancyCoefficient * (1 + BuoyancyRamp);

	const float BuoyantForce = FMath::Clamp(ComputeBuoyantForce(Pontoon.CenterLocation, Pontoon.Radius, BuoyancyCoefficientWithRamp, Pontoon.WaterHeight), 0.f, BuoyancyData.MaxBuoyantForce);
	Pontoon.LocalForce = FVector::UpVector * BuoyantForce * Pontoon.PontoonCoefficient;
}

void UBuoyancyComponent::ComputePontoonCoefficients()
{
	TArray<float>& PontoonCoefficients = ConfiguredPontoonCoefficients.FindOrAdd(PontoonConfiguration);
	if (PontoonCoefficients.Num() == 0)
	{
		TArray<FVector> LocalPontoonLocations;
		if (!SimulatingComponent)
		{
			return;
		}
		for (int32 PontoonIndex = 0; PontoonIndex < BuoyancyData.Pontoons.Num(); ++PontoonIndex)
		{
			if (PontoonConfiguration & (1 << PontoonIndex))
			{
				const FVector LocalPosition = SimulatingComponent->GetSocketTransform(BuoyancyData.Pontoons[PontoonIndex].CenterSocket, ERelativeTransformSpace::RTS_ParentBoneSpace).GetLocation();
				LocalPontoonLocations.Add(LocalPosition);
			}
		}
		PontoonCoefficients.AddZeroed(LocalPontoonLocations.Num());
		if (FBodyInstance* BodyInstance = SimulatingComponent->GetBodyInstance())
		{
			const FVector& LocalCOM = BodyInstance->GetMassSpaceLocal().GetLocation();
			//Distribute a mass of 1 to each pontoon so that we get a scaling factor based on position relative to CoM
			FSimpleSuspensionHelpers::ComputeSprungMasses(LocalPontoonLocations, LocalCOM, 1.f, PontoonCoefficients);
		}
	}

	// Apply the coefficients
	for (int32 PontoonIndex = 0, CoefficientIdx = 0; PontoonIndex < BuoyancyData.Pontoons.Num(); ++PontoonIndex)
	{
		if (PontoonConfiguration & (1 << PontoonIndex))
		{
			BuoyancyData.Pontoons[PontoonIndex].PontoonCoefficient = PontoonCoefficients[CoefficientIdx++];
		}
	}
}

int32 UBuoyancyComponent::UpdatePontoons(float DeltaTime, float ForwardSpeed, float ForwardSpeedKmh, UPrimitiveComponent* PrimitiveComponent)
{
	AActor* Owner = GetOwner();
	check(Owner);

	int32 NumPontoonsInWater = 0;
	if (bIsOverlappingWaterBody)
	{
		int PontoonIndex = 0;
		for (FSphericalPontoon& Pontoon : BuoyancyData.Pontoons)
		{
			if (PontoonConfiguration & (1 << PontoonIndex))
			{
				if (Pontoon.bUseCenterSocket)
				{
					const FTransform& SimulatingComponentTransform = PrimitiveComponent->GetSocketTransform(Pontoon.CenterSocket);
					Pontoon.CenterLocation = SimulatingComponentTransform.GetLocation() + Pontoon.Offset;
					Pontoon.SocketRotation = SimulatingComponentTransform.GetRotation();
				}
				else
				{
					Pontoon.CenterLocation = PrimitiveComponent->GetComponentTransform().TransformPosition(Pontoon.RelativeLocation);
				}
				GetWaterSplineKey(Pontoon.CenterLocation, Pontoon.SplineInputKeys, Pontoon.SplineSegments);
				const FVector PontoonBottom = Pontoon.CenterLocation - FVector(0, 0, Pontoon.Radius);
				/*Pass in large negative default value so we don't accidentally assume we're in water when we're not.*/
				Pontoon.WaterHeight = GetWaterHeight(PontoonBottom - FVector::UpVector * 100.f, Pontoon.SplineInputKeys, -100000.f, Pontoon.CurrentWaterBody, Pontoon.WaterDepth, Pontoon.WaterPlaneLocation, Pontoon.WaterPlaneNormal, Pontoon.WaterSurfacePosition, Pontoon.WaterVelocity, Pontoon.WaterBodyIndex);

				const bool bPrevIsInWater = Pontoon.bIsInWater;
				const float ImmersionDepth = Pontoon.WaterHeight - PontoonBottom.Z;
				/*check if the pontoon is currently in water*/
				if (ImmersionDepth >= 0.f)
				{
					Pontoon.bIsInWater = true;
					Pontoon.ImmersionDepth = ImmersionDepth;
					NumPontoonsInWater++;
				}
				else
				{
					Pontoon.bIsInWater = false;
					Pontoon.ImmersionDepth = 0.f;
				}

#if ENABLE_DRAW_DEBUG
				if (CVarWaterDebugBuoyancy.GetValueOnAnyThread())
				{
					DrawDebugSphere(GetWorld(), Pontoon.CenterLocation, Pontoon.Radius, 16, FColor::Red, false, -1.f, 0, 1.f);
				}
#endif
				ComputeBuoyancy(Pontoon, ForwardSpeedKmh);

				if (Pontoon.bIsInWater && !bPrevIsInWater)
				{
					Pontoon.SplineSegments.Reset();
					// BlueprintImplementables don't really work on the actor component level unfortunately, so call back in to the function defined on the actor itself.
					OnPontoonEnteredWater(Pontoon);
				}
				if (!Pontoon.bIsInWater && bPrevIsInWater)
				{
					Pontoon.SplineSegments.Reset();
					OnPontoonExitedWater(Pontoon);
				}
			}
			PontoonIndex++;
		}

#if ENABLE_DRAW_DEBUG
		if (CVarWaterDebugBuoyancy.GetValueOnAnyThread())
		{
			TMap<const AWaterBody*, float> DebugSplineKeyMap;
			TMap<const AWaterBody*, float> DebugSplineSegmentsMap;
			for (int i = 0; i < 30; ++i)
			{
				for (int j = 0; j < 30; ++j)
				{
					FVector Location = PrimitiveComponent->GetComponentLocation() + (FVector::RightVector * (i - 15) * 30) + (FVector::ForwardVector * (j - 15) * 30);
					GetWaterSplineKey(Location, DebugSplineKeyMap, DebugSplineSegmentsMap);
					FVector Point(Location.X, Location.Y, GetWaterHeight(Location - FVector::UpVector * 200.f, DebugSplineKeyMap, GetOwner()->GetActorLocation().Z));
					DrawDebugPoint(GetWorld(), Point, 5.f, IsOverlappingWaterBody() ? FColor::Green : FColor::Red, false, -1.f, 0);
				}
			}
		}
#endif
	}
	return NumPontoonsInWater;
}

float GetWaterSplineKeyFast(FVector Location, const AWaterBody* WaterBody, TMap<const AWaterBody*, float>& OutSegmentMap)/*const*/
{
	if (!OutSegmentMap.Contains(WaterBody))
	{
		OutSegmentMap.Add(WaterBody, -1);
	}

	const UWaterSplineComponent* WaterSpline = WaterBody->GetWaterSpline();
	const FVector LocalLocation = WaterSpline->GetComponentTransform().InverseTransformPosition(Location);
	const FInterpCurveVector& InterpCurve = WaterSpline->GetSplinePointsPosition();
	float& Segment = OutSegmentMap[WaterBody];

	if (Segment == -1)
	{
		float DummyDistance;
		return InterpCurve.InaccurateFindNearest(LocalLocation, DummyDistance, Segment);
	}

	//We have the cached segment, so search for the best point as in FInterpCurve<T>::InaccurateFindNearest
	//but only in the current segment and the two immediate neighbors

	//River splines aren't looped, so we don't have to handle that case
	const int32 NumPoints = InterpCurve.Points.Num();
	const int32 LastSegmentIdx = FMath::Max(0, NumPoints - 2);
	if (NumPoints > 1)
	{
		float BestDistanceSq = BIG_NUMBER;
		float BestResult = BIG_NUMBER;
		float BestSegment = Segment;
		for (int32 i = Segment - 1; i <= Segment + 1; ++i)
		{
			const int32 SegmentIdx = FMath::Clamp(i, 0, LastSegmentIdx);
			float LocalDistanceSq;
			float LocalResult = InterpCurve.InaccurateFindNearestOnSegment(LocalLocation, SegmentIdx, LocalDistanceSq);
			if (LocalDistanceSq < BestDistanceSq)
			{
				BestDistanceSq = LocalDistanceSq;
				BestResult = LocalResult;
				BestSegment = SegmentIdx;
			}
		}

		if (FMath::IsNearlyEqual(BestResult, Segment - 1) || FMath::IsNearlyEqual(BestResult, Segment + 1))
		{
			//We're at either end of the search - it's possible we skipped a segment so just do a full lookup in this case
			float DummyDistance;
			return InterpCurve.InaccurateFindNearest(LocalLocation, DummyDistance, Segment);
		}

		Segment = BestSegment;
		return BestResult;
	}

	if (NumPoints == 1)
	{
		Segment = 0;
		return InterpCurve.Points[0].InVal;
	}

	return 0.0f;
}

void UBuoyancyComponent::GetWaterSplineKey(FVector Location, TMap<const AWaterBody*, float>& OutMap, TMap<const AWaterBody*, float>& OutSegmentMap) const
{
	OutMap.Reset();
	for (const AWaterBody* WaterBody : CurrentWaterBodies)
	{
		if (WaterBody && WaterBody->GetWaterBodyType() == EWaterBodyType::River)
		{
			float SplineInputKey;
			if (CVarWaterUseSplineKeyOptimization.GetValueOnAnyThread())
			{
				SplineInputKey = GetWaterSplineKeyFast(Location, WaterBody, OutSegmentMap);
			}
			else
			{
				SplineInputKey = WaterBody->FindInputKeyClosestToWorldLocation(Location);
			}
			OutMap.Add(WaterBody, SplineInputKey);
		}
	}
}

float UBuoyancyComponent::GetWaterHeight(FVector Position, const TMap<const AWaterBody*, float>& SplineKeyMap, float DefaultHeight, AWaterBody*& OutWaterBody, float& OutWaterDepth, FVector& OutWaterPlaneLocation, FVector& OutWaterPlaneNormal, FVector& OutWaterSurfacePosition, FVector& OutWaterVelocity, int32& OutWaterBodyIdx, bool bShouldIncludeWaves)
{
	float WaterHeight = DefaultHeight;
	OutWaterBody = nullptr;
	OutWaterDepth = 0.f;
	OutWaterPlaneLocation = FVector::ZeroVector;
	OutWaterPlaneNormal = FVector::UpVector;

	float MaxImmersionDepth = -1.f;
	for (AWaterBody* CurrentWaterBody : CurrentWaterBodies)
	{
		if (CurrentWaterBody)
		{
			const float SplineInputKey = SplineKeyMap.FindRef(CurrentWaterBody);

			EWaterBodyQueryFlags QueryFlags =
				EWaterBodyQueryFlags::ComputeLocation
				| EWaterBodyQueryFlags::ComputeNormal
				| EWaterBodyQueryFlags::ComputeImmersionDepth
				| EWaterBodyQueryFlags::ComputeVelocity;

			if (bShouldIncludeWaves)
			{
				QueryFlags |= EWaterBodyQueryFlags::IncludeWaves;
			}

			FWaterBodyQueryResult QueryResult = CurrentWaterBody->QueryWaterInfoClosestToWorldLocation(Position, QueryFlags, SplineInputKey);
			if (QueryResult.IsInWater() && QueryResult.GetImmersionDepth() > MaxImmersionDepth)
			{
				check(!QueryResult.IsInExclusionVolume());
				WaterHeight = Position.Z + QueryResult.GetImmersionDepth();
				OutWaterBody = CurrentWaterBody;
				if (EnumHasAnyFlags(QueryResult.GetQueryFlags(), EWaterBodyQueryFlags::ComputeDepth))
				{
					OutWaterDepth = QueryResult.GetWaterSurfaceDepth();
				}
				OutWaterPlaneLocation = QueryResult.GetWaterPlaneLocation();
				OutWaterPlaneNormal = QueryResult.GetWaterPlaneNormal();
				OutWaterSurfacePosition = QueryResult.GetWaterSurfaceLocation();
				OutWaterVelocity = QueryResult.GetVelocity();
				OutWaterBodyIdx = CurrentWaterBody ? CurrentWaterBody->WaterBodyIndex : 0;
				MaxImmersionDepth = QueryResult.GetImmersionDepth();
			}
		}
	}
	return WaterHeight;
}

float UBuoyancyComponent::GetWaterHeight(FVector Position, const TMap<const AWaterBody*, float>& SplineKeyMap, float DefaultHeight, bool bShouldIncludeWaves /*= true*/)
{
	AWaterBody* DummyActor;
	float DummyDepth;
	FVector DummyWaterPlaneLocation;
	FVector DummyWaterPlaneNormal;
	FVector DummyWaterSurfacePosition;
	FVector DummyWaterVelocity;
	int32 DummyWaterBodyIndex;
	return GetWaterHeight(Position, SplineKeyMap, DefaultHeight, DummyActor, DummyDepth, DummyWaterPlaneLocation, DummyWaterPlaneNormal, DummyWaterSurfacePosition, DummyWaterVelocity, DummyWaterBodyIndex, bShouldIncludeWaves);
}

void UBuoyancyComponent::OnPontoonEnteredWater(const FSphericalPontoon& Pontoon)
{
	OnEnteredWaterDelegate.Broadcast(Pontoon);
}

void UBuoyancyComponent::OnPontoonExitedWater(const FSphericalPontoon& Pontoon)
{
	OnExitedWaterDelegate.Broadcast(Pontoon);
}

void UBuoyancyComponent::GetLastWaterSurfaceInfo(FVector& OutWaterPlaneLocation, FVector& OutWaterPlaneNormal, FVector& OutWaterSurfacePosition, float& OutWaterDepth, int32& OutWaterBodyIdx, FVector& OutWaterVelocity)
{
	if (BuoyancyData.Pontoons.Num())
	{
		OutWaterPlaneLocation = BuoyancyData.Pontoons[0].WaterPlaneLocation;
		OutWaterPlaneNormal = BuoyancyData.Pontoons[0].WaterPlaneNormal;
		OutWaterSurfacePosition = BuoyancyData.Pontoons[0].WaterSurfacePosition;
		OutWaterDepth = BuoyancyData.Pontoons[0].WaterDepth;
		OutWaterBodyIdx = BuoyancyData.Pontoons[0].WaterBodyIndex;
		OutWaterVelocity = BuoyancyData.Pontoons[0].WaterVelocity;
	}
}

void UBuoyancyComponent::UpdatePontoonCoefficients()
{
	// Get current configuration mask
	uint32 NewPontoonConfiguration = 0;
	for (int32 PontoonIndex = 0; PontoonIndex < BuoyancyData.Pontoons.Num(); ++PontoonIndex)
	{
		if (BuoyancyData.Pontoons[PontoonIndex].bEnabled)
		{
			NewPontoonConfiguration |= 1 << PontoonIndex;
		}
	}

	// Store the new configuration, and return true if its value has changed.
	const bool bConfigurationChanged = PontoonConfiguration != NewPontoonConfiguration;
	PontoonConfiguration = NewPontoonConfiguration;

	// If the configuration changed, update coefficients
	if (bConfigurationChanged)
	{
		// Apply new configuration, recomputing coefficients if necessary
		ComputePontoonCoefficients();
	}
}

FVector UBuoyancyComponent::ComputeWaterForce(const float DeltaTime, const FVector LinearVelocity) const
{
	AActor* Owner = GetOwner();
	check(Owner);

	if (BuoyancyData.Pontoons.Num())
	{
		const FSphericalPontoon& Pontoon = BuoyancyData.Pontoons[VelocityPontoonIndex];
		const AWaterBody* WaterBody = Pontoon.CurrentWaterBody;
		if (WaterBody && WaterBody->GetWaterBodyType() == EWaterBodyType::River)
		{
			float InputKey = Pontoon.SplineInputKeys[WaterBody];
			const float WaterSpeed = WaterBody->GetWaterVelocityAtSplineInputKey(InputKey);

			const FVector SplinePointLocation = WaterBody->GetWaterSpline()->GetLocationAtSplineInputKey(InputKey, ESplineCoordinateSpace::World);
			// Move away from spline
			const FVector ShoreDirection = (Pontoon.CenterLocation - SplinePointLocation).GetSafeNormal2D();

			const float WaterShorePushFactor = BuoyancyData.WaterShorePushFactor;
			const FVector WaterDirection = WaterBody->GetWaterSpline()->GetDirectionAtSplineInputKey(InputKey, ESplineCoordinateSpace::World) * (1 - WaterShorePushFactor)
				+ ShoreDirection * (WaterShorePushFactor);
			const FVector WaterVelocity = WaterDirection * WaterSpeed;
			check(SimulatingComponent && SimulatingComponent->GetBodyInstance());
			const FVector ActorVelocity = SimulatingComponent->GetBodyInstance()->GetUnrealWorldVelocity();
			const float ActorSpeedInWaterDir = FMath::Abs(FVector::DotProduct(ActorVelocity, WaterDirection));
			if (ActorSpeedInWaterDir < WaterSpeed)
			{
				const FVector Acceleration = (WaterVelocity / DeltaTime) * BuoyancyData.WaterVelocityStrength;
				const float MaxWaterAcceleration = BuoyancyData.MaxWaterForce;
				return Acceleration.GetClampedToSize(-MaxWaterAcceleration, MaxWaterAcceleration);
			}
		}
	}
	return FVector::ZeroVector;
}

FVector UBuoyancyComponent::ComputeLinearDragForce(const FVector& PhyiscsVelocity) const
{
	FVector DragForce = FVector::ZeroVector;
	if (BuoyancyData.bApplyDragForcesInWater && IsInWaterBody() && SimulatingComponent)
	{
		FVector PlaneVelocity = PhyiscsVelocity;
		PlaneVelocity.Z = 0;
		const FVector VelocityDir = PlaneVelocity.GetSafeNormal();
		const float SpeedKmh = ToKmh(PlaneVelocity.Size());
		const float ClampedSpeed = FMath::Clamp(SpeedKmh, -BuoyancyData.MaxDragSpeed, BuoyancyData.MaxDragSpeed);

		const float Resistance = ClampedSpeed * BuoyancyData.DragCoefficient;
		DragForce += -Resistance * VelocityDir;

		const float Resistance2 = ClampedSpeed * ClampedSpeed * BuoyancyData.DragCoefficient2;
		DragForce += -Resistance2 * VelocityDir * FMath::Sign(SpeedKmh);
	}
	return DragForce;
}

FVector UBuoyancyComponent::ComputeAngularDragTorque(const FVector& AngularVelocity) const
{
	FVector DragTorque = FVector::ZeroVector;
	if (BuoyancyData.bApplyDragForcesInWater && IsInWaterBody())
	{
		DragTorque = -AngularVelocity * BuoyancyData.AngularDragCoefficient;
	}
	return DragTorque;
}
