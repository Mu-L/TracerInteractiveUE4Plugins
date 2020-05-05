// Copyright Epic Games, Inc. All Rights Reserved.

#include "PersonaUtils.h"
#include "UObject/ObjectMacros.h"
#include "UObject/UnrealType.h"
#include "Engine/Blueprint.h"
#include "Editor.h"
#include "ComponentAssetBroker.h"
#include "Animation/AnimInstance.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Animation/AnimNodeBase.h"
#include "Animation/AnimBlueprint.h"
#include "Widgets/Text/STextBlock.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SComboButton.h"
#include "EditorStyleSet.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Images/SImage.h"

namespace PersonaUtils
{

USceneComponent* GetComponentForAttachedObject(USceneComponent* PreviewComponent, UObject* Object, const FName& AttachedTo)
{
	if (PreviewComponent)
	{
		for (USceneComponent* ChildComponent : PreviewComponent->GetAttachChildren())
		{
			UObject* Asset = FComponentAssetBrokerage::GetAssetFromComponent(ChildComponent);

			if (Asset == Object && ChildComponent->GetAttachSocketName() == AttachedTo)
			{
				return ChildComponent;
			}
		}
	}
	return nullptr;
}

int32 CopyPropertiesToCDO(UAnimInstance* InAnimInstance, const FCopyOptions& Options)
{
	check(InAnimInstance != nullptr);

	UAnimInstance* SourceInstance = InAnimInstance;
	UClass* AnimInstanceClass = SourceInstance->GetClass();
	UAnimInstance* TargetInstance = CastChecked<UAnimInstance>(AnimInstanceClass->GetDefaultObject());
	
	const bool bIsPreviewing = ( Options.Flags & ECopyOptions::PreviewOnly ) != 0;

	int32 CopiedPropertyCount = 0;

	// Copy properties from the instance to the CDO
	TSet<UObject*> ModifiedObjects;
	for( FProperty* Property = AnimInstanceClass->PropertyLink; Property != nullptr; Property = Property->PropertyLinkNext )
	{
		const bool bIsTransient = !!( Property->PropertyFlags & CPF_Transient );
		const bool bIsBlueprintReadonly = !!(Options.Flags & ECopyOptions::FilterBlueprintReadOnly) && !!( Property->PropertyFlags & CPF_BlueprintReadOnly );
		const bool bIsIdentical = Property->Identical_InContainer(SourceInstance, TargetInstance);
		const bool bIsAnimGraphNodeProperty = Property->IsA<FStructProperty>() && CastField<FStructProperty>(Property)->Struct->IsChildOf(FAnimNode_Base::StaticStruct());

		if( !bIsAnimGraphNodeProperty && !bIsTransient && !bIsIdentical && !bIsBlueprintReadonly)
		{
			const bool bIsSafeToCopy = !( Options.Flags & ECopyOptions::OnlyCopyEditOrInterpProperties ) || ( Property->HasAnyPropertyFlags( CPF_Edit | CPF_Interp ) );
			if( bIsSafeToCopy )
			{
				if (!Options.CanCopyProperty(*Property, *SourceInstance))
				{
					continue;
				}

				if( !bIsPreviewing )
				{
					if( !ModifiedObjects.Contains(TargetInstance) )
					{
						// Start modifying the target object
						TargetInstance->Modify();
						ModifiedObjects.Add(TargetInstance);
					}

					if( Options.Flags & ECopyOptions::CallPostEditChangeProperty )
					{
						TargetInstance->PreEditChange(Property);
					}

					EditorUtilities::CopySingleProperty(SourceInstance, TargetInstance, Property);

					if( Options.Flags & ECopyOptions::CallPostEditChangeProperty )
					{
						FPropertyChangedEvent PropertyChangedEvent(Property);
						TargetInstance->PostEditChangeProperty(PropertyChangedEvent);
					}
				}

				++CopiedPropertyCount;
			}
		}
	}

	if (!bIsPreviewing && CopiedPropertyCount > 0 && AnimInstanceClass->HasAllClassFlags(CLASS_CompiledFromBlueprint))
	{
		FBlueprintEditorUtils::MarkBlueprintAsModified(CastChecked<UBlueprint>(AnimInstanceClass->ClassGeneratedBy));
	}

	return CopiedPropertyCount;
}

void SetObjectBeingDebugged(UAnimBlueprint* InAnimBlueprint, UAnimInstance* InAnimInstance)
{
	UAnimBlueprint* PreviewAnimBlueprint = InAnimBlueprint->GetPreviewAnimationBlueprint();
		
	if (PreviewAnimBlueprint)
	{
		EPreviewAnimationBlueprintApplicationMethod ApplicationMethod = InAnimBlueprint->GetPreviewAnimationBlueprintApplicationMethod();
		if(ApplicationMethod == EPreviewAnimationBlueprintApplicationMethod::LinkedLayers)
		{
			// Make sure the object being debugged is the linked layer instance
			InAnimBlueprint->SetObjectBeingDebugged(InAnimInstance->GetLinkedAnimLayerInstanceByClass(InAnimBlueprint->GeneratedClass.Get()));
		}
		else if(ApplicationMethod == EPreviewAnimationBlueprintApplicationMethod::LinkedAnimGraph)
		{
			// Make sure the object being debugged is the linked instance
			InAnimBlueprint->SetObjectBeingDebugged(InAnimInstance->GetLinkedAnimGraphInstanceByTag(InAnimBlueprint->GetPreviewAnimationBlueprintTag()));
		}
	}
	else
	{
		// Make sure the object being debugged is the preview instance
		InAnimBlueprint->SetObjectBeingDebugged(InAnimInstance);
	}
}

TSharedRef<SWidget> MakeTrackButton(FText HoverText, FOnGetContent MenuContent, const TAttribute<bool>& HoverState)
{
	FSlateFontInfo SmallLayoutFont = FCoreStyle::GetDefaultFontStyle("Regular", 8);

	TSharedRef<STextBlock> ComboButtonText = SNew(STextBlock)
		.Text(HoverText)
		.Font(SmallLayoutFont)
		.ColorAndOpacity( FSlateColor::UseForeground() );

	TSharedRef<SComboButton> ComboButton =

		SNew(SComboButton)
		.HasDownArrow(false)
		.ButtonStyle(FEditorStyle::Get(), "HoverHintOnly")
		.ForegroundColor( FSlateColor::UseForeground() )
		.OnGetMenuContent(MenuContent)
		.ContentPadding(FMargin(5, 2))
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		.ButtonContent()
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(FMargin(0,0,2,0))
			[
				SNew(SImage)
				.ColorAndOpacity( FSlateColor::UseForeground() )
				.Image(FEditorStyle::GetBrush("ComboButton.Arrow"))
			]

			+ SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			.AutoWidth()
			[
				ComboButtonText
			]
		];

	auto GetRolloverVisibility = [WeakComboButton = TWeakPtr<SComboButton>(ComboButton), HoverState]()
	{
		TSharedPtr<SComboButton> ComboButton = WeakComboButton.Pin();
		if (HoverState.Get() || ComboButton->IsOpen())
		{
			return EVisibility::SelfHitTestInvisible;
		}
		else
		{
			return EVisibility::Collapsed;
		}
	};

	TAttribute<EVisibility> Visibility = TAttribute<EVisibility>::Create(TAttribute<EVisibility>::FGetter::CreateLambda(GetRolloverVisibility));
	ComboButtonText->SetVisibility(Visibility);

	return ComboButton;
}

}
