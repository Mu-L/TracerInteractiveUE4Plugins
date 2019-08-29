/* Copyright 2017 Google Inc.
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
*   http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*/

#include "GoogleVRLaserVisualComponent.h"
#include "Materials/MaterialInterface.h"
#include "Modules/ModuleManager.h"
#include "Engine/StaticMesh.h"

DEFINE_LOG_CATEGORY_STATIC(LogGoogleVRMotionController, Log, All);

// Sets default values for this component's properties
UGoogleVRLaserVisualComponent::UGoogleVRLaserVisualComponent()
: LaserPlaneMesh(nullptr)
, ControllerReticleMaterial(nullptr)
, TranslucentSortPriority(1)
, DefaultReticleDistance(2.5f)
, MaxPointerDistance(20.0f)
, LaserDistanceMax(0.75f)
, ReticleSize(0.05f)
, LaserPlaneComponent(nullptr)
, ReticleBillboardComponent(nullptr)
{
	// Set this component to be initialized when the game starts, and to be ticked every frame.  You can turn these features
	// off to improve performance if you don't need them.
	PrimaryComponentTick.bCanEverTick = true;
	bAutoActivate = true;

	if (FModuleManager::Get().IsModuleLoaded("GoogleVRController"))
	{
		ControllerReticleMaterial = Cast<UMaterialInterface>(StaticLoadObject(UMaterialInterface::StaticClass(), NULL, TEXT("/GoogleVRController/ControllerRetMaterial")));
		LaserPlaneMesh = Cast<UStaticMesh>(StaticLoadObject(UStaticMesh::StaticClass(), NULL, TEXT("/GoogleVRController/LaserPlane")));
	}
}

void UGoogleVRLaserVisualComponent::OnRegister()
{
	Super::OnRegister();

	check(ControllerReticleMaterial != nullptr);
	check(LaserPlaneMesh != nullptr);

	// Create the laser plane.
	LaserPlaneComponent = NewObject<UGoogleVRLaserPlaneComponent>(this, TEXT("LaserPlaneMesh"));
	LaserPlaneComponent->SetStaticMesh(LaserPlaneMesh);
	LaserPlaneComponent->SetTranslucentSortPriority(TranslucentSortPriority + 1);
	LaserPlaneComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	LaserPlaneComponent->SetupAttachment(this);
	LaserPlaneComponent->RegisterComponent();

	// Create the reticle.
	ReticleBillboardComponent = NewObject<UMaterialBillboardComponent>(this, TEXT("Reticle"));
	ReticleBillboardComponent->AddElement(ControllerReticleMaterial, nullptr, false, 1.0f, 1.0f, nullptr);
	ReticleBillboardComponent->SetTranslucentSortPriority(TranslucentSortPriority);
	ReticleBillboardComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ReticleBillboardComponent->SetupAttachment(this);
	ReticleBillboardComponent->RegisterComponent();
}

// Called when the game starts
void UGoogleVRLaserVisualComponent::BeginPlay()
{
	Super::BeginPlay();
}

// Called every frame
void UGoogleVRLaserVisualComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
}

UMaterialBillboardComponent* UGoogleVRLaserVisualComponent::GetReticle() const
{
	return ReticleBillboardComponent;
}

UGoogleVRLaserPlaneComponent* UGoogleVRLaserVisualComponent::GetLaser() const
{
	return LaserPlaneComponent;
}

UMaterialInstanceDynamic* UGoogleVRLaserVisualComponent::GetLaserMaterial() const
{
	return GetLaser() ? GetLaser()->GetLaserMaterial() : nullptr;
}

void UGoogleVRLaserVisualComponent::SetPointerDistance(float Distance, float WorldToMetersScale, FVector CameraLocation)
{
	UpdateLaserDistance(Distance, WorldToMetersScale);
	UpdateReticleDistance(Distance, WorldToMetersScale, CameraLocation);
}

void UGoogleVRLaserVisualComponent::UpdateLaserDistance(float Distance, float WorldToMetersScale)
{
	if (GetLaser() != nullptr)
	{
		float LaserClampedDistance = FMath::Clamp(Distance, 0.0f, LaserDistanceMax * WorldToMetersScale);
		GetLaser()->UpdateLaserDistance(LaserClampedDistance);
	}
}

void UGoogleVRLaserVisualComponent::SetDefaultLaserDistance(float WorldToMetersScale)
{
	UpdateLaserDistance(LaserDistanceMax * WorldToMetersScale, WorldToMetersScale);
}

void UGoogleVRLaserVisualComponent::UpdateLaserCorrection(FVector Correction)
{
	if (GetLaser() != nullptr)
	{
		GetLaser()->UpdateLaserCorrection(Correction);
	}
}

void UGoogleVRLaserVisualComponent::SetDefaultReticleDistance(float WorldToMetersScale, FVector CameraLocation)
{
	UpdateReticleDistance(DefaultReticleDistance * WorldToMetersScale, WorldToMetersScale, CameraLocation);
}

float UGoogleVRLaserVisualComponent::GetDefaultReticleDistance(float WorldToMetersScale)
{
	return DefaultReticleDistance * WorldToMetersScale;
}

void UGoogleVRLaserVisualComponent::UpdateReticleDistance(float Distance, float WorldToMetersScale, FVector CameraLocation)
{
	if (GetReticle() != nullptr)
	{
		GetReticle()->SetRelativeLocation(FVector(Distance, 0.0f, 0.0f));
	}
	UpdateReticleSize(CameraLocation);
}

void UGoogleVRLaserVisualComponent::UpdateReticleLocation(FVector Location, FVector OriginLocation, float WorldToMetersScale, FVector CameraLocation)
{
	if (GetReticle() != nullptr)
	{
		FVector Difference = Location - OriginLocation;
		Location = OriginLocation + Difference * ReticleClippingOffsetFactor;
		GetReticle()->SetWorldLocation(Location);
	}
	UpdateReticleSize(CameraLocation);
}

void UGoogleVRLaserVisualComponent::UpdateReticleSize(FVector CameraLocation)
{
	if (GetReticle() != nullptr)
	{
		float ReticleDistanceFromCamera = (GetReticle()->GetComponentLocation() - CameraLocation).Size();
		float SpriteSize = ReticleSize * ReticleDistanceFromCamera;

		FMaterialSpriteElement& Sprite = GetReticle()->Elements[0];
		if (Sprite.BaseSizeX != SpriteSize)
		{
			Sprite.BaseSizeX = SpriteSize;
			Sprite.BaseSizeY = SpriteSize;
			GetReticle()->MarkRenderStateDirty();
		}
	}
}

float UGoogleVRLaserVisualComponent::GetMaxPointerDistance(float WorldToMetersScale) const
{
	return MaxPointerDistance * WorldToMetersScale;
}

float UGoogleVRLaserVisualComponent::GetReticleSize()
{
	return ReticleSize;
}

FMaterialSpriteElement* UGoogleVRLaserVisualComponent::GetReticleSprite() const
{
	if (GetReticle() != nullptr)
	{
		return &(GetReticle()->Elements[0]);
	}
	return nullptr;
}

FVector UGoogleVRLaserVisualComponent::GetReticleLocation()
{
	if (GetReticle() != nullptr)
	{
		return GetReticle()->GetComponentLocation();
	}
	return FVector::ZeroVector;
}

void UGoogleVRLaserVisualComponent::SetSubComponentsEnabled(bool bNewEnabled)
{
	if (GetLaser() != nullptr)
	{
		GetLaser()->SetActive(bNewEnabled);
		GetLaser()->SetVisibility(bNewEnabled);
	}

	if (GetReticle() != nullptr)
	{
		GetReticle()->SetActive(bNewEnabled);
		GetReticle()->SetVisibility(bNewEnabled);
	}
}
