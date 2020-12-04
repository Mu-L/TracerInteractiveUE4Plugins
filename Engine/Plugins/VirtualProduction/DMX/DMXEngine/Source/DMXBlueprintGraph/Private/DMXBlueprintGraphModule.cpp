// Copyright Epic Games, Inc. All Rights Reserved.

#include "DMXBlueprintGraphModule.h"
#include "DMXGraphPanelPinFactory.h"
#include "K2Node_GetDMXAttributeValues.h"
#include "Customizations/K2Node_GetDMXAttributeValuesCustomization.h"
#include "DMXBlueprintGraphLog.h"
#include "Library/DMXEntityFixtureType.h"

#include "AssetToolsModule.h"
#include "PropertyEditorModule.h"
#include "AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "UObject/UObjectIterator.h"
#include "Customizations/K2Node_CastPatchToTypeCustomization.h"
#include "K2Node_CastPatchToType.h"

#define LOCTEXT_NAMESPACE "DMXBlueprintGraphModule"

DEFINE_LOG_CATEGORY(LogDMXBlueprintGraph);

void FDMXBlueprintGraphModule::StartupModule()
{
	DMXGraphPanelPinFactory = MakeShared<FDMXGraphPanelPinFactory>();
	FEdGraphUtilities::RegisterVisualPinFactory(DMXGraphPanelPinFactory);

	RegisterObjectCustomizations();

	DataTypeChangeDelegate = UDMXEntityFixtureType::GetDataTypeChangeDelegate().AddRaw(this, &FDMXBlueprintGraphModule::OnDataTypeChanged);
}

void FDMXBlueprintGraphModule::ShutdownModule()
{
	FEdGraphUtilities::UnregisterVisualPinFactory(DMXGraphPanelPinFactory);


	if (FModuleManager::Get().IsModuleLoaded("PropertyEditor"))
	{
		FPropertyEditorModule& PropertyModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");

		// Unregister all classes customized by name
		for (const FName& ClassName : RegisteredClassNames)
		{
			PropertyModule.UnregisterCustomClassLayout(ClassName);
		}

		PropertyModule.NotifyCustomizationModuleChanged();
	}

	// Remove the delegate
	if (DataTypeChangeDelegate.IsValid())
	{
		DataTypeChangeDelegate.Reset();
	}
}

void FDMXBlueprintGraphModule::RegisterObjectCustomizations()
{
	RegisterCustomClassLayout(UK2Node_GetDMXAttributeValues::StaticClass()->GetFName(), FOnGetDetailCustomizationInstance::CreateStatic(&FK2Node_GetDMXAttributeValuesCustomization::MakeInstance));

	RegisterCustomClassLayout(UK2Node_CastPatchToType::StaticClass()->GetFName(), FOnGetDetailCustomizationInstance::CreateStatic(&K2Node_CastPatchToTypeCustomization::MakeInstance));

}

void FDMXBlueprintGraphModule::RegisterCustomClassLayout(FName ClassName, FOnGetDetailCustomizationInstance DetailLayoutDelegate)
{
	check(ClassName != NAME_None);

	RegisteredClassNames.Add(ClassName);

	static FName PropertyEditor("PropertyEditor");
	FPropertyEditorModule& PropertyModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>(PropertyEditor);
	PropertyModule.RegisterCustomClassLayout(ClassName, DetailLayoutDelegate);
}

void FDMXBlueprintGraphModule::OnDataTypeChanged(const UDMXEntityFixtureType* InFixtureType, const FDMXFixtureMode& InMode)
{
	for (TObjectIterator<UK2Node_GetDMXAttributeValues> It(RF_Transient | RF_ClassDefaultObject, /** bIncludeDerivedClasses */ true, /** InternalExcludeFlags */ EInternalObjectFlags::PendingKill); It; ++It)
	{
		if (It->HasValidBlueprint())
		{
			It->OnDataTypeChanged(InFixtureType, InMode);
		}
	}
}


IMPLEMENT_MODULE(FDMXBlueprintGraphModule, DMXBlueprintGraph)

#undef LOCTEXT_NAMESPACE
