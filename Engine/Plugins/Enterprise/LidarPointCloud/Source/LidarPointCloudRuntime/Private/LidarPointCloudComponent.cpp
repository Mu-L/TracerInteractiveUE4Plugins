// Copyright Epic Games, Inc. All Rights Reserved.

#include "LidarPointCloudComponent.h"
#include "Engine/CollisionProfile.h"
#include "UObject/ConstructorHelpers.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstance.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/Texture2D.h"
#include "PhysicsEngine/BodySetup.h"
#include "LidarPointCloud.h"

#if WITH_EDITOR
#include "Misc/MessageDialog.h"
#endif

#define IS_PROPERTY(Name) PropertyChangedEvent.MemberProperty->GetName().Equals(#Name)

ULidarPointCloudComponent::ULidarPointCloudComponent()
	: CustomMaterial(nullptr)
	, MinScreenSize(0.05f)
	, PointSize(1.0f)
	, ColorSource(ELidarPointCloudColorationMode::Data)
	, PointShape(ELidarPointCloudSpriteShape::Square)
	, ElevationColorBottom(FLinearColor::Red)
	, ElevationColorTop(FLinearColor::Green)
	, PointSizeBias(0.035f)
	, Saturation(FVector::OneVector)
	, Contrast(FVector::OneVector)
	, Gamma(FVector::OneVector)
	, Gain(FVector::OneVector)
	, Offset(FVector::ZeroVector)
	, ColorTint(FLinearColor::White)
	, IntensityInfluence(0.0f)
	, MinDepth(0)
	, MaxDepth(-1)
	, bDrawNodeBounds(false)
	, Material(nullptr)
	, OwningViewportClient(nullptr)
{
	PrimaryComponentTick.bCanEverTick = false;
	Mobility = EComponentMobility::Movable;

	CastShadow = false;
	SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);

	static ConstructorHelpers::FObjectFinder<UMaterial> M_PointCloud(TEXT("/LidarPointCloud/Materials/M_LidarPointCloud"));
	MasterMaterial = M_PointCloud.Object;

	static ConstructorHelpers::FObjectFinder<UMaterial> M_PointCloud_Masked(TEXT("/LidarPointCloud/Materials/M_LidarPointCloud_Masked"));
	MasterMaterialMasked = M_PointCloud_Masked.Object;
}

FBoxSphereBounds ULidarPointCloudComponent::CalcBounds(const FTransform& LocalToWorld) const
{
	return PointCloud ? PointCloud->GetBounds().TransformBy(LocalToWorld) : USceneComponent::CalcBounds(LocalToWorld);
}

void ULidarPointCloudComponent::UpdateMaterial()
{
	// If the custom material is an instance, apply it directly...
	if (CustomMaterial && (Cast<UMaterialInstanceDynamic>(CustomMaterial) || Cast<UMaterialInstanceConstant>(CustomMaterial)))
	{
		Material = CustomMaterial;
	}
	// ... otherwise, create MID from it
	else
	{
		Material = UMaterialInstanceDynamic::Create(CustomMaterial ? CustomMaterial : PointShape != ELidarPointCloudSpriteShape::Square ? MasterMaterialMasked : MasterMaterial, nullptr);
	}

	ApplyRenderingParameters();
}

void ULidarPointCloudComponent::AttachPointCloudListener()
{
	if (PointCloud)
	{
		PointCloud->OnPointCloudRebuilt().AddUObject(this, &ULidarPointCloudComponent::OnPointCloudRebuilt);
		PointCloud->OnPointCloudCollisionUpdated().AddUObject(this, &ULidarPointCloudComponent::OnPointCloudCollisionUpdated);
	}
}

void ULidarPointCloudComponent::RemovePointCloudListener()
{
	if (PointCloud)
	{
		PointCloud->OnPointCloudRebuilt().RemoveAll(this);
		PointCloud->OnPointCloudCollisionUpdated().RemoveAll(this);
	}
}

void ULidarPointCloudComponent::OnPointCloudRebuilt()
{
	MarkRenderStateDirty();
	UpdateBounds();
	UpdateMaterial();

	if (PointCloud)
	{
		if (ClassificationColors.Num() == 0)
		{
			for (uint8& Classification : PointCloud->GetClassificationsImported())
			{
				ClassificationColors.Emplace(Classification, FLinearColor::White);
			}
		}
	}
}

void ULidarPointCloudComponent::OnPointCloudCollisionUpdated()
{
	if (bPhysicsStateCreated)
	{
		RecreatePhysicsState();
	}

	MarkRenderStateDirty();
}

void ULidarPointCloudComponent::PostPointCloudSet()
{
	AttachPointCloudListener();

	if (PointCloud)
	{	
		for (uint8& Classification : PointCloud->GetClassificationsImported())
		{
			ClassificationColors.Emplace(Classification, FLinearColor::White);
		}
	}
}

void ULidarPointCloudComponent::SetPointCloud(ULidarPointCloud *InPointCloud)
{
	if (PointCloud != InPointCloud)
	{
		RemovePointCloudListener();
		PointCloud = InPointCloud;		
		PostPointCloudSet();
		OnPointCloudRebuilt();
	}
}

void ULidarPointCloudComponent::ApplyRenderingParameters()
{
	if (UMaterialInstanceDynamic* DynMaterial = Cast<UMaterialInstanceDynamic>(Material))
	{
		DynMaterial->SetVectorParameterValue("PC__Gain", FVector(Gain.X, Gain.Y, Gain.Z) * Gain.W);
	}
}

void ULidarPointCloudComponent::AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector)
{
	ULidarPointCloudComponent* This = CastChecked<ULidarPointCloudComponent>(InThis);
	Super::AddReferencedObjects(This, Collector);
}

void ULidarPointCloudComponent::PostLoad()
{
	Super::PostLoad();
	AttachPointCloudListener();
	UpdateMaterial();
}

void ULidarPointCloudComponent::SetMaterial(int32 ElementIndex, UMaterialInterface* InMaterial)
{
	// If the material cannot be used with LidarPointClouds, then warn and cancel
	if (InMaterial && !InMaterial->CheckMaterialUsage(MATUSAGE_LidarPointCloud))
	{
#if WITH_EDITOR
		FMessageDialog::Open(EAppMsgType::Ok, NSLOCTEXT("LidarPointCloud", "Error_Material_PointCloud", "Can't use the specified material because it has not been compiled with bUsedWithLidarPointCloud."));
#endif
		return;
	}

	CustomMaterial = InMaterial;
	OnPointCloudRebuilt();
}

UBodySetup* ULidarPointCloudComponent::GetBodySetup()
{	
	return PointCloud ? PointCloud->GetBodySetup() : nullptr;
}

#if WITH_EDITOR
void ULidarPointCloudComponent::PreEditChange(FProperty* PropertyThatWillChange)
{
	Super::PreEditChange(PropertyThatWillChange);

	if (PropertyThatWillChange)
	{
		if (PropertyThatWillChange->GetName().Equals("PointCloud"))
		{
			RemovePointCloudListener();
		}
	}
}

void ULidarPointCloudComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	if (PropertyChangedEvent.MemberProperty)
	{
		if (IS_PROPERTY(PointCloud))
		{
			PostPointCloudSet();
		}

		if (IS_PROPERTY(CustomMaterial))
		{
			SetMaterial(0, CustomMaterial);
		}

		if (IS_PROPERTY(Gain))
		{
			ApplyRenderingParameters();
		}

		if (IS_PROPERTY(PointShape))
		{
			UpdateMaterial();
		}
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif