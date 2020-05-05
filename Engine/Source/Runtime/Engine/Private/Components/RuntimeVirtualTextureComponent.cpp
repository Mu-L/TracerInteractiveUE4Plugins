// Copyright Epic Games, Inc. All Rights Reserved.

#include "Components/RuntimeVirtualTextureComponent.h"

#include "Components/PrimitiveComponent.h"
#include "GameFramework/Actor.h"
#include "SceneInterface.h"

URuntimeVirtualTextureComponent::URuntimeVirtualTextureComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, SceneProxy(nullptr)
{
	PrimaryComponentTick.bCanEverTick = true;
	bTickInEditor = true;
}

bool URuntimeVirtualTextureComponent::IsVisible() const
{
	return Super::IsVisible() && UseVirtualTexturing(GetScene()->GetFeatureLevel());
}

void URuntimeVirtualTextureComponent::CreateRenderState_Concurrent(FRegisterComponentContext* Context)
{
	if (ShouldRender() && VirtualTexture != nullptr)
	{
		// This will modify the URuntimeVirtualTexture and allocate its VT
		GetScene()->AddRuntimeVirtualTexture(this);
	}

	Super::CreateRenderState_Concurrent(Context);
}

void URuntimeVirtualTextureComponent::SendRenderTransform_Concurrent()
{
	if (ShouldRender() && VirtualTexture != nullptr)
	{
		// This will modify the URuntimeVirtualTexture and allocate its VT
		GetScene()->AddRuntimeVirtualTexture(this);
	}

	Super::SendRenderTransform_Concurrent();
}

void URuntimeVirtualTextureComponent::DestroyRenderState_Concurrent()
{
	// This will modify the URuntimeVirtualTexture and free its VT
	GetScene()->RemoveRuntimeVirtualTexture(this);

	Super::DestroyRenderState_Concurrent();
}

FBoxSphereBounds URuntimeVirtualTextureComponent::CalcBounds(const FTransform& LocalToWorld) const
{
	// Bounds are based on the unit box centered on the origin
	return FBoxSphereBounds(FBox(FVector(-.5f, -.5f, -1.f), FVector(.5f, .5f, 1.f))).TransformBy(LocalToWorld);
}

FTransform URuntimeVirtualTextureComponent::GetVirtualTextureTransform() const
{
	// Transform is based on bottom left of the URuntimeVirtualTextureComponent unit box (which is centered on the origin)
	return FTransform(FVector(-0.5f, -0.5f, 0.f)) * GetComponentTransform();
}

bool URuntimeVirtualTextureComponent::IsStreamingLowMips() const
{
#if WITH_EDITOR
	return bUseStreamingLowMipsInEditor;
#else
	return true;
#endif
}

#if WITH_EDITOR

void URuntimeVirtualTextureComponent::SetRotation()
{
	if (BoundsSourceActor != nullptr)
	{
		// Copy the source actor rotation and notify the parent actor
		SetWorldRotation(BoundsSourceActor->GetTransform().GetRotation());
		GetOwner()->PostEditMove(true);
	}
}

void URuntimeVirtualTextureComponent::SetTransformToBounds()
{
	if (BoundsSourceActor != nullptr)
	{
		// Calculate the bounds in our local rotation space translated to the BoundsSourceActor center
		const FQuat TargetRotation = GetComponentToWorld().GetRotation();
		const FVector InitialPosition = BoundsSourceActor->GetComponentsBoundingBox().GetCenter();

		FTransform LocalTransform;
		LocalTransform.SetComponents(TargetRotation, InitialPosition, FVector::OneVector);
		FTransform WorldToLocal = LocalTransform.Inverse();

		FBox BoundBox(ForceInit);
		for (const UActorComponent* Component : BoundsSourceActor->GetComponents())
		{
			// Only gather visual components in the bounds calculation
			const UPrimitiveComponent* PrimitiveComponent = Cast<const UPrimitiveComponent>(Component);
			if (PrimitiveComponent != nullptr && PrimitiveComponent->IsRegistered())
			{
				const FTransform ComponentToActor = PrimitiveComponent->GetComponentTransform() * WorldToLocal;
				FBoxSphereBounds LocalSpaceComponentBounds = PrimitiveComponent->CalcBounds(ComponentToActor);
				if (LocalSpaceComponentBounds.GetBox().GetVolume() > 0.f)
				{
					BoundBox += LocalSpaceComponentBounds.GetBox();
				}
			}
		}

		// Create transform from bounds
		FVector Origin;
		FVector Extent;
		BoundBox.GetCenterAndExtents(Origin, Extent);

		Origin = LocalTransform.TransformPosition(Origin);
		Extent *= FVector(2.f, 2.f, 1.f); // Account for ARuntimeVirtualTextureVolume:Box offset which centers it on origin

		FTransform Transform;
		Transform.SetComponents(TargetRotation, Origin, Extent);

		// Apply final result and notify the parent actor
		SetWorldTransform(Transform);
		GetOwner()->PostEditMove(true);
	}
}

#endif
