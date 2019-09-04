// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Widgets/SWidget.h"
#include "Engine/MeshMerging.h"
#include "IMergeActorsTool.h"
#include "SMeshProxyDialog.h"

#include "MeshProxyTool.generated.h"

/** Singleton wrapper to allow for using the setting structure in SSettingsView */
UCLASS(config = Engine)
class UMeshProxySettingsObject : public UObject
{
	GENERATED_BODY()
public:
	UMeshProxySettingsObject()
	{
	}

	static UMeshProxySettingsObject* Get()
	{	
		// This is a singleton, duplicate default object
		if (!bInitialized)
		{
			DefaultSettings = DuplicateObject(GetMutableDefault<UMeshProxySettingsObject>(), nullptr);
			DefaultSettings->AddToRoot();
			bInitialized = true;
		}

		return DefaultSettings;
	}

	static void Destroy()
	{
		if (bInitialized)
		{
			if (UObjectInitialized() && DefaultSettings)
			{
				DefaultSettings->RemoveFromRoot();
				DefaultSettings->MarkPendingKill();
			}

			DefaultSettings = nullptr;
			bInitialized = false;
		}
	}

protected:
	static bool bInitialized;
	static UMeshProxySettingsObject* DefaultSettings;
public:
	UPROPERTY(editanywhere, meta = (ShowOnlyInnerProperties), Category = ProxySettings)
	FMeshProxySettings  Settings;
};

/**
* Mesh Proxy Tool
*/
class FMeshProxyTool : public IMergeActorsTool
{
	friend class SMeshProxyDialog;

public:
	FMeshProxyTool();
	~FMeshProxyTool();

	// IMergeActorsTool interface
	virtual TSharedRef<SWidget> GetWidget() override;
	virtual FName GetIconName() const override { return "MergeActors.MeshProxyTool"; }
	virtual FText GetTooltipText() const override;
	virtual FString GetDefaultPackageName() const override;
	virtual bool CanMerge() const override;
	virtual bool RunMerge(const FString& PackageName) override;

protected:

	/** Pointer to the mesh merging dialog containing settings for the merge */
	TSharedPtr<SMeshProxyDialog> ProxyDialog;

	/** Pointer to singleton settings object */
	UMeshProxySettingsObject* SettingsObject;
};

/**
* Third Party Mesh Proxy Tool
*/
class FThirdPartyMeshProxyTool : public IMergeActorsTool
{
	friend class SThirdPartyMeshProxyDialog;

public:

	// IMergeActorsTool interface
	virtual TSharedRef<SWidget> GetWidget() override;
	virtual FName GetIconName() const override { return "MergeActors.MeshProxyTool"; }
	virtual FText GetTooltipText() const override;
	virtual FString GetDefaultPackageName() const override;
	virtual bool RunMerge(const FString& PackageName) override;
	virtual bool CanMerge() const override;
protected:

	FMeshProxySettings ProxySettings;
};
