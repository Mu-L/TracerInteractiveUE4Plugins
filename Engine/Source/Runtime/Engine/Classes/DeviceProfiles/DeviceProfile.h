// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	DeviceProfile.h: Declares the UDeviceProfile class.
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Engine/TextureLODSettings.h"
#include "DeviceProfile.generated.h"

struct FPropertyChangedEvent;

DECLARE_DELEGATE(FOnCVarsUpdated);


UCLASS(config=DeviceProfiles, perObjectConfig)
class ENGINE_API UDeviceProfile : public UTextureLODSettings
{
	GENERATED_UCLASS_BODY()

	/** The type of this profile, I.e. IOS, Windows, PS4 etc */
	UPROPERTY(VisibleAnywhere, config, Category=DeviceSettings)
	FString DeviceType;

	/** The name of the parent profile of this object */
	UPROPERTY(EditAnywhere, config, Category=DeviceSettings)
	FString BaseProfileName;

	/** The parent object of this profile, it is the object matching this DeviceType with the BaseProfileName */
	UPROPERTY()
	UObject* Parent;

	/** Flag used in the editor to determine whether the profile is visible in the property matrix */
	bool bVisible;

	/** This is not a property, it shouldn't be set by the editor */
	FString ConfigPlatform;

public:

	/** The collection of CVars which is set from this profile */
	UPROPERTY(EditAnywhere, config, Category=ConsoleVariables)
	TArray<FString> CVars;

	/** Prefer to load the DP from its platform's hierarchy */
	virtual const TCHAR* GetConfigOverridePlatform() const override
	{
		return ConfigPlatform.Len() ? *ConfigPlatform : Super::GetConfigOverridePlatform();
	}

	/** 
	 * Get the collection of Console Variables that this profile inherits from its' parents
	 *
	 * @param CVarInformation - The list of inherited CVars in the format of<key:CVarName,value:CVarCompleteString>
	 */
	void GatherParentCVarInformationRecursively(OUT TMap<FString, FString>& CVarInformation) const;

	/** 
	 * Accessor to the delegate object fired when there has been any changes to the console variables 
	 */
	FOnCVarsUpdated& OnCVarsUpdated()
	{
		return CVarsUpdatedDelegate;
	}

public:
	/** 
	 * Access to the device profiles Texture LOD Settings
	 */
	UTextureLODSettings* GetTextureLODSettings() const;
	
private:
	// Make sure our TextureLODGroups array is sorted correctly and complete
	void ValidateTextureLODGroups();
	/** Delegate object fired when there has been any changes to the console variables */
	FOnCVarsUpdated CVarsUpdatedDelegate;

public:
	/* ValidateProfile()
	* Validate the Profile after changes by loading it's config (.ini)
	*/
	void ValidateProfile();

	//~ Begin UObject Interface
	virtual void PostInitProperties() override;
	//~ End UObject Interface

#if WITH_EDITOR
	//~ Begin UObject Interface
	virtual void PostEditChangeProperty( FPropertyChangedEvent& PropertyChangedEvent ) override;
	//~ End UObject Interface

	bool ModifyCVarValue(const FString& CVarName, const FString& CVarValue, bool bAddIfNonExistant = false);
	FString GetCVarValue(const FString& CVarName);

	/** Lazily generate a consolidated list of CVars, recursing up the device profile hierarchy */
	const TMap<FString, FString>& GetConsolidatedCVars() const;

	/** 
	 * Get the string value of a CVar that is held in this device profile, or in any parent device profile.
	 * @param	CVarName		The name of the CVar to find
	 * @param	OutString		The string value of the CVar, if found
	 * @param	bCheckDefaults	Whether to also check the IConsoleManager for the global default value for the CVar
	 * @return true if the CVar was found in this device profile
	 */
	bool GetConsolidatedCVarValue(const TCHAR* CVarName, FString& OutString, bool bCheckDefaults = false) const;

	/** 
	 * Get the int32 value of a CVar that is held in this device profile, or in any parent device profile.
	 * @param	CVarName		The name of the CVar to find
	 * @param	OutString		The int32 value of the CVar, if found
	 * @param	bCheckDefaults	Whether to also check the IConsoleManager for the global default value for the CVar
	 * @return true if the CVar was found in this device profile
	 */
	bool GetConsolidatedCVarValue(const TCHAR* CVarName, int32& OutValue, bool bCheckDefaults = false) const;

	/** 
	 * Get the float value of a CVar that is held in this device profile, or in any parent device profile.
	 * @param	CVarName		The name of the CVar to find
	 * @param	OutString		The float value of the CVar, if found
	 * @param	bCheckDefaults	Whether to also check the IConsoleManager for the global default value for the CVar
	 * @return true if the CVar was found in this device profile
	 */
	bool GetConsolidatedCVarValue(const TCHAR* CVarName, float& OutValue, bool bCheckDefaults = false) const;

private:
	/** Helper function to broadcast when CVars change and clear consolidated map */
	void HandleCVarsChanged();

private:
	/** Consolidated CVars, lazy initialized - access via GetConsolidatedCVars */
	mutable TMap<FString, FString> ConsolidatedCVars;
#endif
};
