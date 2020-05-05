// Copyright Epic Games, Inc. All Rights Reserved.

#include "WidgetBlueprintFactory.h"
#include "UObject/Interface.h"
#include "Misc/MessageDialog.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "WidgetBlueprint.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Modules/ModuleManager.h"

#include "Blueprint/WidgetTree.h"
#include "UMGEditorProjectSettings.h"
#include "ClassViewerModule.h"
#include "Kismet2/SClassPickerDialog.h"
#include "ClassViewerFilter.h"
#include "Components/CanvasPanel.h"
#include "Components/HorizontalBox.h"
#include "Components/VerticalBox.h"
#include "Components/GridPanel.h"

#define LOCTEXT_NAMESPACE "UWidgetBlueprintFactory"

/*------------------------------------------------------------------------------
	UWidgetBlueprintFactory implementation.
------------------------------------------------------------------------------*/

class FWidgetClassFilter : public IClassViewerFilter
{
public:
	/** All children of these classes will be included unless filtered out by another setting. */
	TSet <const UClass*> AllowedChildrenOfClasses;

	/** Disallowed class flags. */
	EClassFlags DisallowedClassFlags;

	virtual bool IsClassAllowed(const FClassViewerInitializationOptions& InInitOptions, const UClass* InClass, TSharedRef< FClassViewerFilterFuncs > InFilterFuncs) override
	{
		return !InClass->HasAnyClassFlags(DisallowedClassFlags)
			&& InFilterFuncs->IfInChildOfClassesSet(AllowedChildrenOfClasses, InClass) != EFilterReturn::Failed;
	}
	
	virtual bool IsUnloadedClassAllowed(const FClassViewerInitializationOptions& InInitOptions, const TSharedRef< const IUnloadedBlueprintData > InUnloadedClassData, TSharedRef< FClassViewerFilterFuncs > InFilterFuncs) override
	{
		return !InUnloadedClassData->HasAnyClassFlags(DisallowedClassFlags)
			&& InFilterFuncs->IfInChildOfClassesSet(AllowedChildrenOfClasses, InUnloadedClassData) != EFilterReturn::Failed;
	}

};

UWidgetBlueprintFactory::UWidgetBlueprintFactory(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bCreateNew = true;
	bEditAfterNew = true;
	SupportedClass = UWidgetBlueprint::StaticClass();
	ParentClass = UUserWidget::StaticClass();
}

bool UWidgetBlueprintFactory::ConfigureProperties()
{
	if (GetDefault<UUMGEditorProjectSettings>()->bUseWidgetTemplateSelector)
	{
		// Load the classviewer module to display a class picker
		FClassViewerModule& ClassViewerModule = FModuleManager::LoadModuleChecked<FClassViewerModule>("ClassViewer");

		// Fill in options
		FClassViewerInitializationOptions Options;
		Options.Mode = EClassViewerMode::ClassPicker;
		Options.bShowNoneOption = true;

		Options.ExtraPickerCommonClasses.Add(UHorizontalBox::StaticClass());
		Options.ExtraPickerCommonClasses.Add(UVerticalBox::StaticClass());
		Options.ExtraPickerCommonClasses.Add(UGridPanel::StaticClass());
		Options.ExtraPickerCommonClasses.Add(UCanvasPanel::StaticClass());

		TSharedPtr<FWidgetClassFilter> Filter = MakeShareable(new FWidgetClassFilter);
		Options.ClassFilter = Filter;

		Filter->DisallowedClassFlags = CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists;
		Filter->AllowedChildrenOfClasses.Add(UPanelWidget::StaticClass());

		const FText TitleText = LOCTEXT("CreateWidgetBlueprint", "Pick Root Widget for New Widget Blueprint");
		return SClassPickerDialog::PickClass(TitleText, Options, RootWidgetClass, UPanelWidget::StaticClass());

	}
	return true;
}

bool UWidgetBlueprintFactory::ShouldShowInNewMenu() const
{
	return true;
}

UObject* UWidgetBlueprintFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn, FName CallingContext)
{
	// Make sure we are trying to factory a Anim Blueprint, then create and init one
	check(Class->IsChildOf(UWidgetBlueprint::StaticClass()));

	// If they selected an interface, force the parent class to be UInterface
	if (BlueprintType == BPTYPE_Interface)
	{
		ParentClass = UInterface::StaticClass();
	}

	if ( ( ParentClass == NULL ) || !FKismetEditorUtilities::CanCreateBlueprintOfClass(ParentClass) || !ParentClass->IsChildOf(UUserWidget::StaticClass()) )
	{
		FFormatNamedArguments Args;
		Args.Add( TEXT("ClassName"), ParentClass ? FText::FromString( ParentClass->GetName() ) : LOCTEXT("Null", "(null)") );
		FMessageDialog::Open( EAppMsgType::Ok, FText::Format( LOCTEXT("CannotCreateWidgetBlueprint", "Cannot create a Widget Blueprint based on the class '{ClassName}'."), Args ) );
		return nullptr;
	}
	else
	{
		if (!GetDefault<UUMGEditorProjectSettings>()->bUseWidgetTemplateSelector)
		{
			RootWidgetClass = GetDefault<UUMGEditorProjectSettings>()->DefaultRootWidget;
		}

		UWidgetBlueprint* NewBP = CastChecked<UWidgetBlueprint>(FKismetEditorUtilities::CreateBlueprint(ParentClass, InParent, Name, BlueprintType, UWidgetBlueprint::StaticClass(), UWidgetBlueprintGeneratedClass::StaticClass(), CallingContext));

		// Create the desired root widget specified by the project
		if ( NewBP->WidgetTree->RootWidget == nullptr )
		{
			if (TSubclassOf<UPanelWidget> RootWidgetPanel = RootWidgetClass)
			{
				UWidget* Root = NewBP->WidgetTree->ConstructWidget<UWidget>(RootWidgetPanel);
				NewBP->WidgetTree->RootWidget = Root;
			}
		}

		return NewBP;
	}
}

UObject* UWidgetBlueprintFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	return FactoryCreateNew(Class, InParent, Name, Flags, Context, Warn, NAME_None);
}

#undef LOCTEXT_NAMESPACE
