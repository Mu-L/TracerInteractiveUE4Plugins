// Copyright 2020 Tracer Interactive, LLC. All Rights Reserved.
#include "WebInterfaceInteractionComponent.h"
#include "WebInterface.h"
#include "Blueprint/WidgetTree.h"
#include "Blueprint/UserWidget.h"
#include "Components/WidgetComponent.h"
#include "Kismet/GameplayStatics.h"

UWebInterfaceInteractionComponent::UWebInterfaceInteractionComponent( const FObjectInitializer& ObjectInitializer )
	: Super( ObjectInitializer )
{
	//
}

UWebInterfaceInteractionComponent::FWidgetTraceResult UWebInterfaceInteractionComponent::PerformTrace() const
{
	FWidgetTraceResult TraceResult;

	bool bLineTrace = true;
	TArray<UPrimitiveComponent*> IgnoredComponents;

	while ( bLineTrace )
	{
		bLineTrace = false;

// UWidgetInteractionComponent::PerformTrace()
		TArray<FHitResult> MultiHits;

		FVector WorldDirection;

		switch( InteractionSource )
		{
			case EWidgetInteractionSource::World:
			{
				const FVector WorldLocation = GetComponentLocation();
				const FTransform WorldTransform = GetComponentTransform();
				WorldDirection = WorldTransform.GetUnitAxis(EAxis::X);

				TArray<UPrimitiveComponent*> PrimitiveChildren;
				GetRelatedComponentsToIgnoreInAutomaticHitTesting(PrimitiveChildren);

				FCollisionQueryParams Params = FCollisionQueryParams::DefaultQueryParam;
				Params.AddIgnoredComponents(PrimitiveChildren);
				Params.AddIgnoredComponents(IgnoredComponents);

				TraceResult.LineStartLocation = WorldLocation;
				TraceResult.LineEndLocation = WorldLocation + (WorldDirection * InteractionDistance);

				GetWorld()->LineTraceMultiByChannel(MultiHits, TraceResult.LineStartLocation, TraceResult.LineEndLocation, TraceChannel, Params);
				break;
			}
			case EWidgetInteractionSource::Mouse:
			case EWidgetInteractionSource::CenterScreen:
			{
				TArray<UPrimitiveComponent*> PrimitiveChildren;
				GetRelatedComponentsToIgnoreInAutomaticHitTesting(PrimitiveChildren);

				FCollisionQueryParams Params = FCollisionQueryParams::DefaultQueryParam;
				Params.AddIgnoredComponents(PrimitiveChildren);
				Params.AddIgnoredComponents(IgnoredComponents);

				APlayerController* PlayerController = GetWorld()->GetFirstPlayerController();
				ULocalPlayer* LocalPlayer = PlayerController->GetLocalPlayer();
			
				if ( LocalPlayer && LocalPlayer->ViewportClient )
				{
					if ( InteractionSource == EWidgetInteractionSource::Mouse )
					{
						FVector2D MousePosition;
						if ( LocalPlayer->ViewportClient->GetMousePosition(MousePosition) )
						{
							FVector WorldOrigin;
							if ( UGameplayStatics::DeprojectScreenToWorld(PlayerController, MousePosition, WorldOrigin, WorldDirection) == true )
							{
								TraceResult.LineStartLocation = WorldOrigin;
								TraceResult.LineEndLocation = WorldOrigin + WorldDirection * InteractionDistance;

								GetWorld()->LineTraceMultiByChannel(MultiHits, TraceResult.LineStartLocation, TraceResult.LineEndLocation, TraceChannel, Params);
							}
						}
					}
					else if ( InteractionSource == EWidgetInteractionSource::CenterScreen )
					{
						FVector2D ViewportSize;
						LocalPlayer->ViewportClient->GetViewportSize(ViewportSize);

						FVector WorldOrigin;
						if ( UGameplayStatics::DeprojectScreenToWorld(PlayerController, ViewportSize * 0.5f, WorldOrigin, WorldDirection) == true )
						{
							TraceResult.LineStartLocation = WorldOrigin;
							TraceResult.LineEndLocation = WorldOrigin + WorldDirection * InteractionDistance;

							GetWorld()->LineTraceMultiByChannel(MultiHits, WorldOrigin, WorldOrigin + WorldDirection * InteractionDistance, TraceChannel, Params);
						}
					}
				}
				break;
			}
			case EWidgetInteractionSource::Custom:
			{
				const FTransform WorldTransform = GetComponentTransform();
				WorldDirection = WorldTransform.GetUnitAxis(EAxis::X);
				TraceResult.HitResult = CustomHitResult;
				TraceResult.bWasHit = CustomHitResult.bBlockingHit;
				TraceResult.LineStartLocation = CustomHitResult.TraceStart;
				TraceResult.LineEndLocation = CustomHitResult.TraceEnd;
				break;
			}
		}
	
		if ( InteractionSource != EWidgetInteractionSource::Custom )
		{
			for ( const FHitResult& HitResult : MultiHits )
			{
				if ( UWidgetComponent* HitWidgetComponent = Cast<UWidgetComponent>(HitResult.GetComponent()) )
				{
					if ( HitWidgetComponent->IsVisible() )
					{
						TraceResult.bWasHit = true;
						TraceResult.HitResult = HitResult;
						break;
					}
				}
				else
				{
					break;
				}
			}
		}
	
		if (TraceResult.bWasHit)
		{
			TraceResult.HitWidgetComponent = Cast<UWidgetComponent>(TraceResult.HitResult.GetComponent());
			if (TraceResult.HitWidgetComponent)
			{
				if (TraceResult.HitWidgetComponent->GetGeometryMode() == EWidgetGeometryMode::Cylinder)
				{				
					TTuple<FVector, FVector2D> CylinderHitLocation = TraceResult.HitWidgetComponent->GetCylinderHitLocation(TraceResult.HitResult.ImpactPoint, WorldDirection);
					TraceResult.HitResult.ImpactPoint = CylinderHitLocation.Get<0>();
					TraceResult.LocalHitLocation = CylinderHitLocation.Get<1>();
				}
				else
				{
					ensure(TraceResult.HitWidgetComponent->GetGeometryMode() == EWidgetGeometryMode::Plane);
					TraceResult.HitWidgetComponent->GetLocalHitLocation(TraceResult.HitResult.ImpactPoint, TraceResult.LocalHitLocation);
				}
				TraceResult.HitWidgetPath = FindHoveredWidgetPath(TraceResult);
			}
		}
// UWidgetInteractionComponent::PerformTrace()

		if ( InteractionSource == EWidgetInteractionSource::Custom )
			break;

		if ( TraceResult.bWasHit && TraceResult.HitWidgetComponent )
		{
			UUserWidget* UserWidget = TraceResult.HitWidgetComponent->GetUserWidgetObject();
			if ( UserWidget && UserWidget->WidgetTree )
			{
				bool bHit = false;
				UserWidget->WidgetTree->ForEachWidget( [ &TraceResult, &bHit ]( UWidget* Widget )
				{
					if ( Widget && !bHit )
					{
						const FGeometry& Geometry = Widget->GetCachedGeometry();
						const FVector2D Size = Geometry.GetLocalSize();

						if ( Size.X > SMALL_NUMBER && Size.Y > SMALL_NUMBER )
						{
							FVector2D Location = Geometry.AbsoluteToLocal( TraceResult.LocalHitLocation ) / Size;
							if ( Location.X >= 0 && Location.X < 1 && Location.Y >= 0 && Location.Y < 1 )
							{
								UWebInterface* WebInterface = Cast<UWebInterface>( Widget );
								if ( WebInterface && WebInterface->IsVirtualPointerTransparencyEnabled() )
								{
									const int32 X = (int32)( Location.X * WebInterface->GetTextureWidth() );
									const int32 Y = (int32)( Location.Y * WebInterface->GetTextureHeight() );

									const FLinearColor Pixel = WebInterface->ReadTexturePixel( X, Y );
									if ( Pixel.A >= WebInterface->GetTransparencyThreshold() )
										bHit = true;
								}
								else
								{
									TSharedPtr<SWidget> SafeWidget = Widget->GetCachedWidget();
									if ( SafeWidget.IsValid() && SafeWidget->GetVisibility().IsHitTestVisible() )
										bHit = true;
								}
							}
						}
					}
				} );
			
				if ( !bHit )
				{
					IgnoredComponents.Add( TraceResult.HitWidgetComponent );
					bLineTrace = true;

					TraceResult.bWasHit   = false;
					TraceResult.HitResult = FHitResult();

					TraceResult.HitWidgetComponent = nullptr;
					TraceResult.HitWidgetPath      = FWidgetPath();
				}
			}
		}
	}
	
	return TraceResult;
}
