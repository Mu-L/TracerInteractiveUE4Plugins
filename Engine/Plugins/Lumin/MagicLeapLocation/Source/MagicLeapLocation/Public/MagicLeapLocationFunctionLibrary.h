// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Engine/Engine.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "MagicLeapLocationTypes.h"
#include "MagicLeapLocationFunctionLibrary.generated.h"

UCLASS(ClassGroup = MagicLeap)
class MAGICLEAPLOCATION_API UMagicLeapLocationFunctionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
		Attempts to retrieve the latitude, longitude and postcode of the device.
		@param OutLocation If successful this will contain the latitude, longitude and postcode of the device.
		@param bUseFineLocation Flags whether or not to request a fine or coarse location.
		@return True if the location data is valid, false otherwise.
	*/
	UFUNCTION(BlueprintCallable, Category = "Location Function Library | MagicLeap")
	static bool GetLastLocation(FMagicLeapLocationData& OutLocation, bool bUseFineLocation = true);

	/**
		Attempts to retrieve the latitude, longitude and postcode of the device asynchronously.
		@param The delegate to notify once the privilege has been granted.
		@param bUseFineLocation Flags whether or not to request a fine or coarse location.
		@return True if the location is immediately resolved, false otherwise.
	*/
	UFUNCTION(BlueprintCallable, Category = "Location | MagicLeap")
	static bool GetLastLocationAsync(const FMagicLeapLocationResultDelegate& InResultDelegate, bool bUseFineLocation = true);

	/**
		Attempts to retrieve a point on a sphere representing the location of the device.
		@param OutLocation If successful this will be a valid point on a sphere representing the location of the device.
		@param bUseFineLocation Flags whether or not to request a fine or coarse location.
		@return True if the location is valid, false otherwise.
	*/
	UFUNCTION(BlueprintCallable, Category = "Location Function Library | MagicLeap")
	static bool GetLastLocationOnSphere(float InRadius, FVector& OutLocation, bool bUseFineLocation = true);

	/**
		Attempts to retrieve a point on a sphere representing the location of the device asynchronously.
		@param The delegate to notify once the privilege has been granted.
		@param InRadius The radius of the sphere that the location will be projected onto.
		@param bUseFineLocation Flags whether or not to request a fine or coarse location.
		@return True if the location is immediately resolved, false otherwise.
	*/
	UFUNCTION(BlueprintCallable, Category = "Location | MagicLeap")
	static bool GetLastLocationOnSphereAsync(const FMagicLeapLocationOnSphereResultDelegate& InResultDelegate, float InRadius, bool bUseFineLocation = true);
};
