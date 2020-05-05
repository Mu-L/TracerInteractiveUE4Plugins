// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EditorUtilityWidget.h"
#include "Toolkits/IToolkitHost.h"
#include "AssetTypeActions_Base.h"


class FAssetTypeActions_EditorUtilityWidgetBlueprint : public FAssetTypeActions_Base
{
public:
	// IAssetTypeActions interface
	virtual FText GetName() const override;
	virtual FColor GetTypeColor() const override;
	virtual UClass* GetSupportedClass() const override;
	virtual bool HasActions(const TArray<UObject*>& InObjects) const override;
	virtual void GetActions(const TArray<UObject*>& InObjects, struct FToolMenuSection& Section) override;
	virtual void OpenAssetEditor(const TArray<UObject*>& InObjects, TSharedPtr<class IToolkitHost> EditWithinLevelEditor = TSharedPtr<IToolkitHost>()) override;
	virtual uint32 GetCategories() override;
	virtual void PerformAssetDiff(UObject* Asset1, UObject* Asset2, const struct FRevisionInfo& OldRevision, const struct FRevisionInfo& NewRevision) const override;
	virtual bool CanLocalize() const override { return false; }
	// End of IAssetTypeActions interface

protected:
	typedef TArray< TWeakObjectPtr<class UWidgetBlueprint> > FWeakBlueprintPointerArray;

	void ExecuteRun(FWeakBlueprintPointerArray Objects);
};
