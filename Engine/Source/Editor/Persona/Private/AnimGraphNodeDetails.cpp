// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimGraphNodeDetails.h"
#include "Modules/ModuleManager.h"
#include "UObject/UnrealType.h"
#include "Widgets/Text/STextBlock.h"
#include "BoneContainer.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/AnimationAsset.h"
#include "Widgets/Layout/SSpacer.h"
#include "DetailWidgetRow.h"
#include "IDetailPropertyRow.h"
#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"
#include "IDetailsView.h"
#include "PropertyCustomizationHelpers.h"
#include "SlateOptMacros.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SButton.h"
#include "Animation/AnimInstance.h"
#include "Animation/EditorParentPlayerListObj.h"
#include "Animation/AnimBlueprintGeneratedClass.h"
#include "Widgets/SToolTip.h"
#include "IDocumentation.h"
#include "ObjectEditorUtils.h"
#include "AnimGraphNode_Base.h"
#include "Widgets/Views/STreeView.h"
#include "BoneSelectionWidget.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Animation/BlendProfile.h"
#include "AnimGraphNode_AssetPlayerBase.h"
#include "BlendProfilePicker.h"
#include "ISkeletonEditorModule.h"
#include "EdGraph/EdGraph.h"
#include "BlueprintEditor.h"
#include "Animation/EditorAnimCurveBoneLinks.h"
#include "IEditableSkeleton.h"
#include "IDetailChildrenBuilder.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "BoneControllers/AnimNode_SkeletalControlBase.h"
#include "AnimGraphNode_StateMachine.h"
#include "AnimGraphNode_SequencePlayer.h"
#include "Engine/SkeletalMeshSocket.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SCheckBox.h"
#include "LODInfoUILayout.h"
#include "IPersonaToolkit.h"
#include "Interfaces/Interface_BoneReferenceSkeletonProvider.h"
#include "IPropertyAccessEditor.h"
#include "Algo/Accumulate.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "ScopedTransaction.h"

#define LOCTEXT_NAMESPACE "KismetNodeWithOptionalPinsDetails"

/////////////////////////////////////////////////////
// FAnimGraphNodeDetails 

TSharedRef<IDetailCustomization> FAnimGraphNodeDetails::MakeInstance()
{
	return MakeShareable(new FAnimGraphNodeDetails());
}

void FAnimGraphNodeDetails::CustomizeDetails(class IDetailLayoutBuilder& DetailBuilder)
{
	TArray< TWeakObjectPtr<UObject> > SelectedObjectsList;
	DetailBuilder.GetObjectsBeingCustomized(SelectedObjectsList);

	// Hide the pin options property; it's represented inline per-property instead
	IDetailCategoryBuilder& PinOptionsCategory = DetailBuilder.EditCategory("PinOptions");
	TSharedRef<IPropertyHandle> AvailablePins = DetailBuilder.GetProperty("ShowPinForProperties");
	DetailBuilder.HideProperty(AvailablePins);
	TSharedRef<IPropertyHandle> PropertyBindings = DetailBuilder.GetProperty("PropertyBindings");
	DetailBuilder.HideProperty(PropertyBindings);

	// get first animgraph nodes
	UAnimGraphNode_Base* AnimGraphNode = Cast<UAnimGraphNode_Base>(SelectedObjectsList[0].Get());
	if (AnimGraphNode == nullptr)
	{
		return;
	}

	// make sure type matches with all the nodes. 
	const UAnimGraphNode_Base* FirstNodeType = AnimGraphNode;
	for (int32 Index = 1; Index < SelectedObjectsList.Num(); ++Index)
	{
		UAnimGraphNode_Base* CurrentNode = Cast<UAnimGraphNode_Base>(SelectedObjectsList[Index].Get());
		if (!CurrentNode || CurrentNode->GetClass() != FirstNodeType->GetClass())
		{
			// if type mismatches, multi selection doesn't work, just return
			return;
		}
	}

	TargetSkeleton = AnimGraphNode->GetAnimBlueprint()->TargetSkeleton;
	TargetSkeletonName = TargetSkeleton ? FString::Printf(TEXT("%s'%s'"), *TargetSkeleton->GetClass()->GetName(), *TargetSkeleton->GetPathName()) : FString(TEXT(""));

	// Get the node property
	const FStructProperty* NodeProperty = AnimGraphNode->GetFNodeProperty();
	if (NodeProperty == nullptr)
	{
		return;
	}

	// customize anim graph node's own details if needed
	AnimGraphNode->CustomizeDetails(DetailBuilder);

	// Hide the Node property as we are going to be adding its inner properties below
	TSharedRef<IPropertyHandle> NodePropertyHandle = DetailBuilder.GetProperty(NodeProperty->GetFName(), AnimGraphNode->GetClass());
	DetailBuilder.HideProperty(NodePropertyHandle);

	uint32 NumChildHandles = 0;
	FPropertyAccess::Result Result = NodePropertyHandle->GetNumChildren(NumChildHandles);
	if (Result != FPropertyAccess::Fail)
	{
		for (uint32 ChildHandleIndex = 0; ChildHandleIndex < NumChildHandles; ++ChildHandleIndex)
		{
			TSharedPtr<IPropertyHandle> TargetPropertyHandle = NodePropertyHandle->GetChildHandle(ChildHandleIndex);
			if (TargetPropertyHandle.IsValid())
			{
				FProperty* TargetProperty = TargetPropertyHandle->GetProperty();
				IDetailCategoryBuilder& CurrentCategory = DetailBuilder.EditCategory(FObjectEditorUtils::GetCategoryFName(TargetProperty));

				int32 CustomPinIndex = AnimGraphNode->ShowPinForProperties.IndexOfByPredicate([TargetProperty](const FOptionalPinFromProperty& InOptionalPin)
				{
					return TargetProperty->GetFName() == InOptionalPin.PropertyName;
				});

				if (CustomPinIndex != INDEX_NONE)
				{
					const FOptionalPinFromProperty& OptionalPin = AnimGraphNode->ShowPinForProperties[CustomPinIndex];

					// Not optional
					if (!OptionalPin.bCanToggleVisibility && OptionalPin.bShowPin)
					{
						// Always displayed as a pin, so hide the property
						DetailBuilder.HideProperty(TargetPropertyHandle);
						continue;
					}

					if (!TargetPropertyHandle->GetProperty())
					{
						continue;
					}

					// if customized, do not do anything
					if (TargetPropertyHandle->IsCustomized())
					{
						continue;
					}

					// sometimes because of order of customization
					// this gets called first for the node you'd like to customize
					// then the above statement won't work
					// so you can mark certain property to have meta data "CustomizeProperty"
					// which will trigger below statement
					if (OptionalPin.bPropertyIsCustomized)
					{
						continue;
					}

					TSharedRef<SWidget> InternalCustomWidget = CreatePropertyWidget(TargetProperty, TargetPropertyHandle.ToSharedRef(), AnimGraphNode->GetClass());

					if (OptionalPin.bCanToggleVisibility)
					{
						IDetailPropertyRow& PropertyRow = CurrentCategory.AddProperty(TargetPropertyHandle);

						TSharedPtr<SWidget> NameWidget;
						TSharedPtr<SWidget> ValueWidget;
						FDetailWidgetRow Row;
						PropertyRow.GetDefaultWidgets(NameWidget, ValueWidget, Row);

						ValueWidget = (InternalCustomWidget == SNullWidget::NullWidget) ? ValueWidget : InternalCustomWidget;

						const FName OptionalPinArrayEntryName(*FString::Printf(TEXT("ShowPinForProperties[%d].bShowPin"), CustomPinIndex));
						TSharedRef<IPropertyHandle> ShowHidePropertyHandle = DetailBuilder.GetProperty(OptionalPinArrayEntryName);

						ShowHidePropertyHandle->MarkHiddenByCustomization();

						ValueWidget->SetVisibility(TAttribute<EVisibility>::Create(TAttribute<EVisibility>::FGetter::CreateSP(this, &FAnimGraphNodeDetails::GetVisibilityOfProperty, ShowHidePropertyHandle)));

						// If we have an edit condition, that comes as part of the default name widget, so just use a text block to avoid duplicate checkboxes
						TSharedPtr<SWidget> PropertyNameWidget;
						if (TargetProperty->HasMetaData(TEXT("EditCondition")))
						{
							PropertyNameWidget = SNew(STextBlock)
							.Text(TargetProperty->GetDisplayNameText())
							.Font(IDetailLayoutBuilder::GetDetailFont())
							.ToolTipText(TargetProperty->GetToolTipText());
						}
						else
						{
							PropertyNameWidget = NameWidget;
						}

						NameWidget = PropertyNameWidget;

						// we only show children if visibility is one
						// whenever toggles, this gets called, so it will be refreshed
						const bool bShowChildren = GetVisibilityOfProperty(ShowHidePropertyHandle) == EVisibility::Visible;
						PropertyRow.CustomWidget(bShowChildren)
						.NameContent()
						.MinDesiredWidth(Row.NameWidget.MinWidth)
						.MaxDesiredWidth(Row.NameWidget.MaxWidth)
						[
							NameWidget.ToSharedRef()
						]
						.ValueContent()
						.MinDesiredWidth(Row.ValueWidget.MinWidth)
						.MaxDesiredWidth(Row.ValueWidget.MaxWidth)
						[
							ValueWidget.ToSharedRef()
						];
					}
					else if (InternalCustomWidget != SNullWidget::NullWidget)
					{
						// A few properties are internally customized within this customization. Here we
						// catch instances of these that don't have an optional pin flag.
						IDetailPropertyRow& PropertyRow = CurrentCategory.AddProperty(TargetPropertyHandle);
						PropertyRow.CustomWidget()
						.NameContent()
						[
							TargetPropertyHandle->CreatePropertyNameWidget()
						]
						.ValueContent()
						[
							InternalCustomWidget
						];
					}
					else
					{
						CurrentCategory.AddProperty(TargetPropertyHandle);
					}
				}
			}
		}
	}
}


TSharedRef<SWidget> FAnimGraphNodeDetails::CreatePropertyWidget(FProperty* TargetProperty, TSharedRef<IPropertyHandle> TargetPropertyHandle, UClass* NodeClass)
{
	if(const FObjectPropertyBase* ObjectProperty = CastField<const FObjectPropertyBase>( TargetProperty ))
	{
		if(ObjectProperty->PropertyClass->IsChildOf(UAnimationAsset::StaticClass()))
		{
			bool bAllowClear = !(ObjectProperty->PropertyFlags & CPF_NoClear);

			return SNew(SObjectPropertyEntryBox)
				.PropertyHandle(TargetPropertyHandle)
				.AllowedClass(ObjectProperty->PropertyClass)
				.AllowClear(bAllowClear)
				.OnShouldFilterAsset(FOnShouldFilterAsset::CreateSP(this, &FAnimGraphNodeDetails::OnShouldFilterAnimAsset, NodeClass));
		}
		else if(ObjectProperty->PropertyClass->IsChildOf(UBlendProfile::StaticClass()) && TargetSkeleton)
		{
			TSharedPtr<IPropertyHandle> PropertyPtr(TargetPropertyHandle);

			UObject* PropertyValue = nullptr;
			TargetPropertyHandle->GetValue(PropertyValue);

			UBlendProfile* CurrentProfile = Cast<UBlendProfile>(PropertyValue);

			FBlendProfilePickerArgs Args;
			Args.bAllowNew = false;
			Args.bAllowRemove = false;
			Args.bAllowClear = true;
			Args.OnBlendProfileSelected = FOnBlendProfileSelected::CreateSP(this, &FAnimGraphNodeDetails::OnBlendProfileChanged, PropertyPtr);
			Args.InitialProfile = CurrentProfile;

			ISkeletonEditorModule& SkeletonEditorModule = FModuleManager::Get().LoadModuleChecked<ISkeletonEditorModule>("SkeletonEditor");
			return SkeletonEditorModule.CreateBlendProfilePicker(this->TargetSkeleton, Args);

		}
	}

	return SNullWidget::NullWidget;
}

bool FAnimGraphNodeDetails::OnShouldFilterAnimAsset( const FAssetData& AssetData, UClass* NodeToFilterFor ) const
{
	FAssetDataTagMapSharedView::FFindTagResult Result = AssetData.TagsAndValues.FindTag("Skeleton");
	if (Result.IsSet() && Result.GetValue() == TargetSkeletonName)
	{
		const UClass* AssetClass = AssetData.GetClass();
		// If node is an 'asset player', only let you select the right kind of asset for it
		if (!NodeToFilterFor->IsChildOf(UAnimGraphNode_AssetPlayerBase::StaticClass()) || SupportNodeClassForAsset(AssetClass, NodeToFilterFor))
		{
			return false;
		}
	}
	return true;
}

EVisibility FAnimGraphNodeDetails::GetVisibilityOfProperty(TSharedRef<IPropertyHandle> Handle) const
{
	bool bShowAsPin;
	if (FPropertyAccess::Success == Handle->GetValue(/*out*/ bShowAsPin))
	{
		return bShowAsPin ? EVisibility::Hidden : EVisibility::Visible;
	}
	else
	{
		return EVisibility::Visible;
	}
}

void FAnimGraphNodeDetails::OnBlendProfileChanged(UBlendProfile* NewProfile, TSharedPtr<IPropertyHandle> PropertyHandle)
{
	if(PropertyHandle.IsValid())
	{
		PropertyHandle->SetValue(NewProfile);
	}
}


TSharedRef<IPropertyTypeCustomization> FInputScaleBiasCustomization::MakeInstance() 
{
	return MakeShareable(new FInputScaleBiasCustomization());
}

float GetMinValue(float Scale, float Bias)
{
	return Scale != 0.0f ? (FMath::Abs(Bias) < SMALL_NUMBER ? 0.0f : -Bias) / Scale : 0.0f; // to avoid displaying of - in front of 0
}

float GetMaxValue(float Scale, float Bias)
{
	return Scale != 0.0f ? (1.0f - Bias) / Scale : 0.0f;
}

void UpdateInputScaleBiasWithMinValue(float MinValue, TSharedRef<class IPropertyHandle> InputBiasScaleStructPropertyHandle)
{
	InputBiasScaleStructPropertyHandle->NotifyPreChange();

	TSharedRef<class IPropertyHandle> BiasProperty = InputBiasScaleStructPropertyHandle->GetChildHandle("Bias").ToSharedRef();
	TSharedRef<class IPropertyHandle> ScaleProperty = InputBiasScaleStructPropertyHandle->GetChildHandle("Scale").ToSharedRef();
	TArray<void*> BiasDataArray;
	TArray<void*> ScaleDataArray;
	BiasProperty->AccessRawData(BiasDataArray);
	ScaleProperty->AccessRawData(ScaleDataArray);
	check(BiasDataArray.Num() == ScaleDataArray.Num());
	for(int32 DataIndex = 0; DataIndex < BiasDataArray.Num(); ++DataIndex)
	{
		float* BiasPtr = (float*)BiasDataArray[DataIndex];
		float* ScalePtr = (float*)ScaleDataArray[DataIndex];
		check(BiasPtr);
		check(ScalePtr);

		const float MaxValue = GetMaxValue(*ScalePtr, *BiasPtr);
		const float Difference = MaxValue - MinValue;
		*ScalePtr = Difference != 0.0f? 1.0f / Difference : 0.0f;
		*BiasPtr = -MinValue * *ScalePtr;
	}

	InputBiasScaleStructPropertyHandle->NotifyPostChange();
}

void UpdateInputScaleBiasWithMaxValue(float MaxValue, TSharedRef<class IPropertyHandle> InputBiasScaleStructPropertyHandle)
{
	InputBiasScaleStructPropertyHandle->NotifyPreChange();

	TSharedRef<class IPropertyHandle> BiasProperty = InputBiasScaleStructPropertyHandle->GetChildHandle("Bias").ToSharedRef();
	TSharedRef<class IPropertyHandle> ScaleProperty = InputBiasScaleStructPropertyHandle->GetChildHandle("Scale").ToSharedRef();
	TArray<void*> BiasDataArray;
	TArray<void*> ScaleDataArray;
	BiasProperty->AccessRawData(BiasDataArray);
	ScaleProperty->AccessRawData(ScaleDataArray);
	check(BiasDataArray.Num() == ScaleDataArray.Num());
	for(int32 DataIndex = 0; DataIndex < BiasDataArray.Num(); ++DataIndex)
	{
		float* BiasPtr = (float*)BiasDataArray[DataIndex];
		float* ScalePtr = (float*)ScaleDataArray[DataIndex];
		check(BiasPtr);
		check(ScalePtr);

		const float MinValue = GetMinValue(*ScalePtr, *BiasPtr);
		const float Difference = MaxValue - MinValue;
		*ScalePtr = Difference != 0.0f ? 1.0f / Difference : 0.0f;
		*BiasPtr = -MinValue * *ScalePtr;
	}

	InputBiasScaleStructPropertyHandle->NotifyPostChange();
}

TOptional<float> GetMinValueInputScaleBias(TSharedRef<class IPropertyHandle> InputBiasScaleStructPropertyHandle)
{
	TSharedRef<class IPropertyHandle> BiasProperty = InputBiasScaleStructPropertyHandle->GetChildHandle("Bias").ToSharedRef();
	TSharedRef<class IPropertyHandle> ScaleProperty = InputBiasScaleStructPropertyHandle->GetChildHandle("Scale").ToSharedRef();
	float Scale = 1.0f;
	float Bias = 0.0f;
	if(ScaleProperty->GetValue(Scale) == FPropertyAccess::Success && BiasProperty->GetValue(Bias) == FPropertyAccess::Success)
	{
		return GetMinValue(Scale, Bias);
	}

	return TOptional<float>();
}

TOptional<float> GetMaxValueInputScaleBias(TSharedRef<class IPropertyHandle> InputBiasScaleStructPropertyHandle)
{
	TSharedRef<class IPropertyHandle> BiasProperty = InputBiasScaleStructPropertyHandle->GetChildHandle("Bias").ToSharedRef();
	TSharedRef<class IPropertyHandle> ScaleProperty = InputBiasScaleStructPropertyHandle->GetChildHandle("Scale").ToSharedRef();
	float Scale = 1.0f;
	float Bias = 0.0f;
	if(ScaleProperty->GetValue(Scale) == FPropertyAccess::Success && BiasProperty->GetValue(Bias) == FPropertyAccess::Success)
	{
		return GetMaxValue(Scale, Bias);
	}

	return TOptional<float>();
}


void FInputScaleBiasCustomization::CustomizeHeader(TSharedRef<class IPropertyHandle> StructPropertyHandle, class FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& StructCustomizationUtils)
{

}

BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION
void FInputScaleBiasCustomization::CustomizeChildren( TSharedRef<class IPropertyHandle> StructPropertyHandle, class IDetailChildrenBuilder& StructBuilder, IPropertyTypeCustomizationUtils& StructCustomizationUtils )
{
	TWeakPtr<IPropertyHandle> WeakStructPropertyHandle = StructPropertyHandle;

	StructBuilder
	.AddProperty(StructPropertyHandle)
	.CustomWidget()
	.NameContent()
	[
		StructPropertyHandle->CreatePropertyNameWidget()
	]
	.ValueContent()
	.MinDesiredWidth(250.0f)
	.MaxDesiredWidth(250.0f)
	[
		SNew(SHorizontalBox)
		+SHorizontalBox::Slot()
		.Padding(FMargin(0.0f, 2.0f, 3.0f, 2.0f))
		[
			SNew(SNumericEntryBox<float>)
			.Font(IDetailLayoutBuilder::GetDetailFont())
			.ToolTipText(LOCTEXT("MinInputScaleBias", "Minimum input value"))
			.AllowSpin(true)
			.MinSliderValue(0.0f)
			.MaxSliderValue(2.0f)
			.Value_Lambda([WeakStructPropertyHandle]()
			{
				return GetMinValueInputScaleBias(WeakStructPropertyHandle.Pin().ToSharedRef());
			})
			.OnValueChanged_Lambda([WeakStructPropertyHandle](float InValue)
			{
				UpdateInputScaleBiasWithMinValue(InValue, WeakStructPropertyHandle.Pin().ToSharedRef());
			})
		]
		+SHorizontalBox::Slot()
		.Padding(FMargin(0.0f, 2.0f, 0.0f, 2.0f))
		[
			SNew(SNumericEntryBox<float>)
			.Font(IDetailLayoutBuilder::GetDetailFont())
			.ToolTipText(LOCTEXT("MaxInputScaleBias", "Maximum input value"))
			.AllowSpin(true)
			.MinSliderValue(0.0f)
			.MaxSliderValue(2.0f)
			.Value_Lambda([WeakStructPropertyHandle]()
			{
				return GetMaxValueInputScaleBias(WeakStructPropertyHandle.Pin().ToSharedRef());
			})
			.OnValueChanged_Lambda([WeakStructPropertyHandle](float InValue)
			{
				UpdateInputScaleBiasWithMaxValue(InValue, WeakStructPropertyHandle.Pin().ToSharedRef());
			})
		]
	];
}
END_SLATE_FUNCTION_BUILD_OPTIMIZATION

/////////////////////////////////////////////////////////////////////////////////////////////
//  FBoneReferenceCustomization
/////////////////////////////////////////////////////////////////////////////////////////////

TSharedRef<IPropertyTypeCustomization> FBoneReferenceCustomization::MakeInstance()
{
	return MakeShareable(new FBoneReferenceCustomization());
}

void FBoneReferenceCustomization::CustomizeHeader( TSharedRef<IPropertyHandle> StructPropertyHandle, class FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& StructCustomizationUtils )
{
	// set property handle 
	SetPropertyHandle(StructPropertyHandle);
	// set editable skeleton info from struct
	SetEditableSkeleton(StructPropertyHandle);
	if (TargetEditableSkeleton.IsValid() && BoneNameProperty->IsValidHandle())
	{
		HeaderRow
		.NameContent()
		[
			StructPropertyHandle->CreatePropertyNameWidget()
		]
		.ValueContent()
		.MaxDesiredWidth(0.0f)
		[
			SNew(SBoneSelectionWidget)
			.ToolTipText(StructPropertyHandle->GetToolTipText())
			.OnBoneSelectionChanged(this, &FBoneReferenceCustomization::OnBoneSelectionChanged)
			.OnGetSelectedBone(this, &FBoneReferenceCustomization::GetSelectedBone)
			.OnGetReferenceSkeleton(this, &FBoneReferenceCustomization::GetReferenceSkeleton)
		];
	}
	else
	{
		// if this FBoneReference is used by some other Outers, this will fail	
		// should warn programmers instead of silent fail
		ensureAlways(!bEnsureOnInvalidSkeleton);
		UE_LOG(LogAnimation, Warning, TEXT("FBoneReferenceCustomization::CustomizeHeader: SetEditableSkeleton failed to find an appropriate skeleton!"));
	}
}

void FBoneReferenceCustomization::CustomizeChildren(TSharedRef<IPropertyHandle> StructPropertyHandle, class IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& StructCustomizationUtils)
{

}
void FBoneReferenceCustomization::SetEditableSkeleton(TSharedRef<IPropertyHandle> StructPropertyHandle) 
{
	TArray<UObject*> Objects;
	StructPropertyHandle->GetOuterObjects(Objects);

	USkeleton* TargetSkeleton = nullptr;
	TSharedPtr<IEditableSkeleton> EditableSkeleton;

	bEnsureOnInvalidSkeleton = true;

	for (UObject* Outer : Objects)
	{
		if (UAnimGraphNode_Base* AnimGraphNode = Cast<UAnimGraphNode_Base>(Outer))
		{
			TargetSkeleton = AnimGraphNode->GetAnimBlueprint()->TargetSkeleton;
			break;
		}

		if (USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(Outer))
		{
			TargetSkeleton = SkeletalMesh->Skeleton;
			break;
		}

		if (ULODInfoUILayout* LODInfoUILayout = Cast<ULODInfoUILayout>(Outer))
		{
			USkeletalMesh* SkeletalMesh = LODInfoUILayout->GetPersonaToolkit()->GetPreviewMesh();
			check(SkeletalMesh);
			TargetSkeleton = SkeletalMesh->Skeleton;
			break;
		}

		if (UAnimationAsset* AnimationAsset = Cast<UAnimationAsset>(Outer))
		{
			TargetSkeleton = AnimationAsset->GetSkeleton();
			break;
		}

		if (UAnimInstance* AnimInstance = Cast<UAnimInstance>(Outer))
		{
			if (AnimInstance->CurrentSkeleton)
			{
				TargetSkeleton = AnimInstance->CurrentSkeleton;
				break;
			}
			else if (UAnimBlueprintGeneratedClass* AnimBPClass = Cast<UAnimBlueprintGeneratedClass>(AnimInstance->GetClass()))
			{
				TargetSkeleton = AnimBPClass->TargetSkeleton;
				break;
			}
		}

		// editor animation curve bone links are responsible for linking joints to curve
		// this is editor object that only exists for editor
		if (UEditorAnimCurveBoneLinks* AnimCurveObj = Cast<UEditorAnimCurveBoneLinks>(Outer))
		{
			EditableSkeleton = AnimCurveObj->EditableSkeleton.Pin();
			break;
		}

		if (IBoneReferenceSkeletonProvider* SkeletonProvider = Cast<IBoneReferenceSkeletonProvider>(Outer))
		{
			TargetSkeleton = SkeletonProvider->GetSkeleton(bEnsureOnInvalidSkeleton);
			break;
		}
	}

	if (TargetSkeleton != nullptr)
	{
		ISkeletonEditorModule& SkeletonEditorModule = FModuleManager::LoadModuleChecked<ISkeletonEditorModule>("SkeletonEditor");
		EditableSkeleton = SkeletonEditorModule.CreateEditableSkeleton(TargetSkeleton);
	}

	TargetEditableSkeleton = EditableSkeleton;
}

TSharedPtr<IPropertyHandle> FBoneReferenceCustomization::FindStructMemberProperty(TSharedRef<IPropertyHandle> PropertyHandle, const FName& PropertyName)
{
	uint32 NumChildren = 0;
	PropertyHandle->GetNumChildren(NumChildren);
	for (uint32 ChildIdx = 0; ChildIdx < NumChildren; ++ChildIdx)
	{
		TSharedPtr<IPropertyHandle> ChildHandle = PropertyHandle->GetChildHandle(ChildIdx);
		if (ChildHandle->GetProperty()->GetFName() == PropertyName)
		{
			return ChildHandle;
		}
	}

	return TSharedPtr<IPropertyHandle>();
}

void FBoneReferenceCustomization::SetPropertyHandle(TSharedRef<IPropertyHandle> StructPropertyHandle)
{
	BoneNameProperty = FindStructMemberProperty(StructPropertyHandle, GET_MEMBER_NAME_CHECKED(FBoneReference, BoneName));
	check(BoneNameProperty->IsValidHandle());
}

void FBoneReferenceCustomization::OnBoneSelectionChanged(FName Name)
{
	BoneNameProperty->SetValue(Name);
}

FName FBoneReferenceCustomization::GetSelectedBone(bool& bMultipleValues) const
{
	FString OutText;
	
	FPropertyAccess::Result Result = BoneNameProperty->GetValueAsFormattedString(OutText);
	bMultipleValues = (Result == FPropertyAccess::MultipleValues);

	return FName(*OutText);
}

const struct FReferenceSkeleton&  FBoneReferenceCustomization::GetReferenceSkeleton() const
{
	// retruning dummy skeleton if any reason, it is invalid
	static FReferenceSkeleton DummySkeleton;

	return (TargetEditableSkeleton.IsValid()) ? TargetEditableSkeleton.Get()->GetSkeleton().GetReferenceSkeleton() : DummySkeleton;
}

/////////////////////////////////////////////////////////////////////////////////////////////
//  FBoneSocketTargetCustomization
/////////////////////////////////////////////////////////////////////////////////////////////
TSharedRef<IPropertyTypeCustomization> FBoneSocketTargetCustomization::MakeInstance()
{
	return MakeShareable(new FBoneSocketTargetCustomization());
}

void FBoneSocketTargetCustomization::CustomizeChildren(TSharedRef<IPropertyHandle> StructPropertyHandle, class IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& StructCustomizationUtils)
{
	// set property handle 
	SetPropertyHandle(StructPropertyHandle);
	// set editable skeleton info from struct
	SetEditableSkeleton(StructPropertyHandle);
	Build(StructPropertyHandle, ChildBuilder);
}

void FBoneSocketTargetCustomization::SetPropertyHandle(TSharedRef<IPropertyHandle> StructPropertyHandle)
{
	TSharedPtr<IPropertyHandle> BoneReferenceProperty = FindStructMemberProperty(StructPropertyHandle, GET_MEMBER_NAME_CHECKED(FBoneSocketTarget, BoneReference));
	check(BoneReferenceProperty->IsValidHandle());
	BoneNameProperty = FindStructMemberProperty(BoneReferenceProperty.ToSharedRef(), GET_MEMBER_NAME_CHECKED(FBoneReference, BoneName));
	TSharedPtr<IPropertyHandle> SocketReferenceProperty = FindStructMemberProperty(StructPropertyHandle, GET_MEMBER_NAME_CHECKED(FBoneSocketTarget, SocketReference));
	check(SocketReferenceProperty->IsValidHandle());
	SocketNameProperty = FindStructMemberProperty(SocketReferenceProperty.ToSharedRef(), GET_MEMBER_NAME_CHECKED(FSocketReference, SocketName));
	UseSocketProperty = FindStructMemberProperty(StructPropertyHandle, GET_MEMBER_NAME_CHECKED(FBoneSocketTarget, bUseSocket));

	check(BoneNameProperty->IsValidHandle() && SocketNameProperty->IsValidHandle() && UseSocketProperty->IsValidHandle());
}

BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION
void FBoneSocketTargetCustomization::Build(TSharedRef<IPropertyHandle> StructPropertyHandle, class IDetailChildrenBuilder& ChildBuilder)
{
	if (TargetEditableSkeleton.IsValid() && BoneNameProperty->IsValidHandle())
	{
		ChildBuilder
		.AddProperty(StructPropertyHandle)
		.CustomWidget()
		.NameContent()
		[
			StructPropertyHandle->CreatePropertyNameWidget()
		]
		.ValueContent()
		[
			SNew(SBoneSelectionWidget)
			.ToolTipText(StructPropertyHandle->GetToolTipText())
			.bShowSocket(true)
			.OnBoneSelectionChanged(this, &FBoneSocketTargetCustomization::OnBoneSelectionChanged)
			.OnGetSelectedBone(this, &FBoneSocketTargetCustomization::GetSelectedBone)
			.OnGetReferenceSkeleton(this, &FBoneReferenceCustomization::GetReferenceSkeleton)
			.OnGetSocketList(this, &FBoneSocketTargetCustomization::GetSocketList)
		];
	}
	else
	{
		// if this FBoneSocketTarget is used by some other Outers, this will fail	
		// should warn programmers instead of silent fail
		ensureAlways(false);
	}
}
END_SLATE_FUNCTION_BUILD_OPTIMIZATION

TSharedPtr<IPropertyHandle> FBoneSocketTargetCustomization::GetNameProperty() const
{
	bool bUseSocket = false;
	if (UseSocketProperty->GetValue(bUseSocket) == FPropertyAccess::Success)
	{
		if (bUseSocket)
		{
			return SocketNameProperty;
		}

		return BoneNameProperty;
	}

	return TSharedPtr<IPropertyHandle>();
}
void FBoneSocketTargetCustomization::OnBoneSelectionChanged(FName Name)
{
	// figure out if the name is BoneName or socket name
	if (TargetEditableSkeleton.IsValid())
	{
		bool bUseSocket = false;
		if (GetReferenceSkeleton().FindBoneIndex(Name) == INDEX_NONE)
		{
			// make sure socket exists
			const TArray<class USkeletalMeshSocket*>& Sockets = GetSocketList();
			for (int32 Idx = 0; Idx < Sockets.Num(); ++Idx)
			{
				if (Sockets[Idx]->SocketName == Name)
				{
					bUseSocket = true;
					break;
				}
			}

			// we should find one
			ensure(bUseSocket);
		}

		// set correct value
		UseSocketProperty->SetValue(bUseSocket);

		TSharedPtr<IPropertyHandle> NameProperty = GetNameProperty();
		if (ensureAlways(NameProperty.IsValid()))
		{
			NameProperty->SetValue(Name);
		}
	}
}

FName FBoneSocketTargetCustomization::GetSelectedBone(bool& bMultipleValues) const
{
	FString OutText;

	TSharedPtr<IPropertyHandle> NameProperty = GetNameProperty();
	if (NameProperty.IsValid())
	{
		FPropertyAccess::Result Result = NameProperty->GetValueAsFormattedString(OutText);
		bMultipleValues = (Result == FPropertyAccess::MultipleValues);
	}
	else
	{
		// there is no single value
		bMultipleValues = true;
		return NAME_None;
	}

	return FName(*OutText);
}

const TArray<class USkeletalMeshSocket*>& FBoneSocketTargetCustomization::GetSocketList() const
{
	if (TargetEditableSkeleton.IsValid())
	{
		return  TargetEditableSkeleton.Get()->GetSkeleton().Sockets;
	}

	static TArray<class USkeletalMeshSocket*> DummyList;
	return DummyList;
}

/////////////////////////////////////////////////////////////////////////////////////////////

TSharedRef<IDetailCustomization> FAnimGraphParentPlayerDetails::MakeInstance(TSharedRef<FBlueprintEditor> InBlueprintEditor)
{
	return MakeShareable(new FAnimGraphParentPlayerDetails(InBlueprintEditor));
}


void FAnimGraphParentPlayerDetails::CustomizeDetails(class IDetailLayoutBuilder& DetailBuilder)
{
	TArray<TWeakObjectPtr<UObject>> SelectedObjects;
	DetailBuilder.GetObjectsBeingCustomized(SelectedObjects);
	check(SelectedObjects.Num() == 1);

	EditorObject = Cast<UEditorParentPlayerListObj>(SelectedObjects[0].Get());
	check(EditorObject);
	
	IDetailCategoryBuilder& Category = DetailBuilder.EditCategory("AnimGraphOverrides");
	DetailBuilder.HideProperty("Overrides");

	struct FObjectToEntryBuilder
	{
	private:
		TMap<UObject*, TSharedPtr<FPlayerTreeViewEntry>> ObjectToEntryMap;
		TArray<TSharedPtr<FPlayerTreeViewEntry>>& ListEntries;

	private:
		TSharedPtr<FPlayerTreeViewEntry> AddObject(UObject* Object)
		{
			TSharedPtr<FPlayerTreeViewEntry> Result = ObjectToEntryMap.FindRef(Object);
			if (!Result.IsValid() && (Object != nullptr))
			{
				bool bTopLevel = false;
				TSharedPtr<FPlayerTreeViewEntry> ThisNode;

				if (UBlueprint* Blueprint = Cast<UBlueprint>(Object))
				{
					ThisNode = MakeShareable(new FPlayerTreeViewEntry(Blueprint->GetName(), EPlayerTreeViewEntryType::Blueprint));
					bTopLevel = true;
				}
				else if (UAnimGraphNode_StateMachine* StateMachineNode = Cast<UAnimGraphNode_StateMachine>(Object))
				{
					// Don't create a node for these, the graph speaks for it
				}
				else if (UAnimGraphNode_AssetPlayerBase * AssetPlayerBase = Cast<UAnimGraphNode_AssetPlayerBase>(Object))
				{
					FString Title = AssetPlayerBase->GetNodeTitle(ENodeTitleType::FullTitle).ToString();
					ThisNode = MakeShareable(new FPlayerTreeViewEntry(Title, EPlayerTreeViewEntryType::Node));
				}
				else if (UAnimGraphNode_Base* Node = Cast<UAnimGraphNode_Base>(Object))
				{
					ThisNode = MakeShareable(new FPlayerTreeViewEntry(Node->GetName(), EPlayerTreeViewEntryType::Node));
				}
				else if (UEdGraph* Graph = Cast<UEdGraph>(Object))
				{
					ThisNode = MakeShareable(new FPlayerTreeViewEntry(Graph->GetName(), EPlayerTreeViewEntryType::Graph));
				}

				if (ThisNode.IsValid())
				{
					ObjectToEntryMap.Add(Object, ThisNode);
				}

				if (bTopLevel)
				{
					ListEntries.Add(ThisNode);
					Result = ThisNode;
				}
				else
				{
					TSharedPtr<FPlayerTreeViewEntry> Outer = AddObject(Object->GetOuter());
					Result = Outer;

					if (ThisNode.IsValid())
					{
						Result = ThisNode;
						check(Outer.IsValid())
						Outer->Children.Add(Result);
					}
				}
			}

			return Result;
		}

		void SortInternal(TArray<TSharedPtr<FPlayerTreeViewEntry>>& ListToSort)
		{
			ListToSort.Sort([](TSharedPtr<FPlayerTreeViewEntry> A, TSharedPtr<FPlayerTreeViewEntry> B) { return A->EntryName < B->EntryName; });

			for (TSharedPtr<FPlayerTreeViewEntry>& Entry : ListToSort)
			{
				SortInternal(Entry->Children);
			}
		}

	public:
		FObjectToEntryBuilder(TArray<TSharedPtr<FPlayerTreeViewEntry>>& InListEntries)
			: ListEntries(InListEntries)
		{
		}

		void AddNode(UAnimGraphNode_Base* Node, FAnimParentNodeAssetOverride& Override)
		{
			TSharedPtr<FPlayerTreeViewEntry> Result = AddObject(Node);
			if (Result.IsValid())
			{
				Result->Override = &Override;
			}
		}

		void Sort()
		{
			SortInternal(ListEntries);
		}
	};

	FObjectToEntryBuilder EntryBuilder(ListEntries);

	// Build a hierarchy of entires for a tree view in the form of Blueprint->Graph->Node
	for (FAnimParentNodeAssetOverride& Override : EditorObject->Overrides)
	{
		UAnimGraphNode_Base* Node = EditorObject->GetVisualNodeFromGuid(Override.ParentNodeGuid);
		EntryBuilder.AddNode(Node, Override);
	}

	// Sort the nodes
	EntryBuilder.Sort();

	FDetailWidgetRow& Row = Category.AddCustomRow(FText::GetEmpty());
	TSharedRef<STreeView<TSharedPtr<FPlayerTreeViewEntry>>> TreeView = SNew(STreeView<TSharedPtr<FPlayerTreeViewEntry>>)
		.SelectionMode(ESelectionMode::None)
		.OnGenerateRow(this, &FAnimGraphParentPlayerDetails::OnGenerateRow)
		.OnGetChildren(this, &FAnimGraphParentPlayerDetails::OnGetChildren)
		.TreeItemsSource(&ListEntries)
		.HeaderRow
		(
			SNew(SHeaderRow)
			+SHeaderRow::Column(FName("Name"))
			.FillWidth(0.5f)
			.DefaultLabel(LOCTEXT("ParentPlayer_NameCol", "Name"))

			+SHeaderRow::Column(FName("Asset"))
			.FillWidth(0.5f)
			.DefaultLabel(LOCTEXT("ParentPlayer_AssetCol", "Asset"))
		);

	// Expand top level (blueprint) entries so the panel seems less empty
	for (TSharedPtr<FPlayerTreeViewEntry> Entry : ListEntries)
	{
		TreeView->SetItemExpansion(Entry, true);
	}

	Row
	[
		TreeView->AsShared()
	];
}

TSharedRef<ITableRow> FAnimGraphParentPlayerDetails::OnGenerateRow(TSharedPtr<FPlayerTreeViewEntry> EntryPtr, const TSharedRef< STableViewBase >& OwnerTable)
{
	return SNew(SParentPlayerTreeRow, OwnerTable).Item(EntryPtr).OverrideObject(EditorObject).BlueprintEditor(BlueprintEditorPtr);
}

void FAnimGraphParentPlayerDetails::OnGetChildren(TSharedPtr<FPlayerTreeViewEntry> InParent, TArray< TSharedPtr<FPlayerTreeViewEntry> >& OutChildren)
{
	OutChildren.Append(InParent->Children);
}

void SParentPlayerTreeRow::Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwnerTableView)
{
	Item = InArgs._Item;
	EditorObject = InArgs._OverrideObject;
	BlueprintEditor = InArgs._BlueprintEditor;

	if(Item->Override)
	{
		GraphNode = EditorObject->GetVisualNodeFromGuid(Item->Override->ParentNodeGuid);
	}
	else
	{
		GraphNode = NULL;
	}

	SMultiColumnTableRow<TSharedPtr<FAnimGraphParentPlayerDetails>>::Construct(FSuperRowType::FArguments(), InOwnerTableView);
}

TSharedRef<SWidget> SParentPlayerTreeRow::GenerateWidgetForColumn(const FName& ColumnName)
{
	TSharedPtr<SHorizontalBox> HorizBox;
	SAssignNew(HorizBox, SHorizontalBox);

	if(ColumnName == "Name")
	{
		HorizBox->AddSlot()
			.VAlign(VAlign_Center)
			.AutoWidth()
			[
				SNew(SExpanderArrow, SharedThis(this))
			];

		Item->GenerateNameWidget(HorizBox);
	}
	else if(Item->Override)
	{
		HorizBox->AddSlot()
			.Padding(2)
			.VAlign(VAlign_Center)
			.AutoWidth()
			[
				SNew(SButton)
				.ButtonStyle(FEditorStyle::Get(), "ToggleButton")
				.ToolTip(IDocumentation::Get()->CreateToolTip(LOCTEXT("FocusNodeButtonTip", "Open the graph that contains this node in read-only mode and focus on the node"), NULL, "Shared/Editors/Persona", "FocusNodeButton"))
				.OnClicked(FOnClicked::CreateSP(this, &SParentPlayerTreeRow::OnFocusNodeButtonClicked))
				.Content()
				[
					SNew(SImage)
					.Image(FEditorStyle::GetBrush("GenericViewButton"))
				]
				
			];
		
		TArray<const UClass*> AllowedClasses;
		AllowedClasses.Add(UAnimationAsset::StaticClass());
		HorizBox->AddSlot()
			.VAlign(VAlign_Center)
			.FillWidth(1.f)
			[
				SNew(SObjectPropertyEntryBox)
				.ObjectPath(this, &SParentPlayerTreeRow::GetCurrentAssetPath)
				.OnShouldFilterAsset(this, &SParentPlayerTreeRow::OnShouldFilterAsset)
				.OnObjectChanged(this, &SParentPlayerTreeRow::OnAssetSelected)
				.AllowedClass(GetCurrentAssetToUse()->GetClass())
			];

		HorizBox->AddSlot()
			.VAlign(VAlign_Center)
			.AutoWidth()
			[
				SNew(SButton)
				.ButtonStyle(FEditorStyle::Get(), "NoBorder")
				.Visibility(this, &SParentPlayerTreeRow::GetResetToDefaultVisibility)
				.OnClicked(this, &SParentPlayerTreeRow::OnResetButtonClicked)
				.ToolTip(IDocumentation::Get()->CreateToolTip(LOCTEXT("ResetToParentButtonTip", "Undo the override, returning to the default asset for this node"), NULL, "Shared/Editors/Persona", "ResetToParentButton"))
				.Content()
				[
					SNew(SImage)
					.Image(FEditorStyle::GetBrush("PropertyWindow.DiffersFromDefault"))
				]
			];
	}

	return HorizBox.ToSharedRef();
}

bool SParentPlayerTreeRow::OnShouldFilterAsset(const FAssetData& AssetData)
{
	const FString SkeletonName = AssetData.GetTagValueRef<FString>("Skeleton");

	if(!SkeletonName.IsEmpty())
	{
		USkeleton* CurrentSkeleton = GraphNode->GetAnimBlueprint()->TargetSkeleton;
		if(SkeletonName == FString::Printf(TEXT("%s'%s'"), *CurrentSkeleton->GetClass()->GetName(), *CurrentSkeleton->GetPathName()))
		{
			return false;
		}
	}

	return true;
}

void SParentPlayerTreeRow::OnAssetSelected(const FAssetData& AssetData)
{
	Item->Override->NewAsset = Cast<UAnimationAsset>(AssetData.GetAsset());
	EditorObject->ApplyOverrideToBlueprint(*Item->Override);
}

FReply SParentPlayerTreeRow::OnFocusNodeButtonClicked()
{
	TSharedPtr<FBlueprintEditor> SharedBlueprintEditor = BlueprintEditor.Pin();
	if(SharedBlueprintEditor.IsValid())
	{
		if(GraphNode)
		{
			UEdGraph* EdGraph = GraphNode->GetGraph();
			TSharedPtr<SGraphEditor> GraphEditor = SharedBlueprintEditor->OpenGraphAndBringToFront(EdGraph);
			if (GraphEditor.IsValid())
			{
				GraphEditor->JumpToNode(GraphNode, false);
			}
		}
		return FReply::Handled();
	}

	return FReply::Unhandled();
}

const UAnimationAsset* SParentPlayerTreeRow::GetCurrentAssetToUse() const
{
	if(Item->Override->NewAsset)
	{
		return Item->Override->NewAsset;
	}
	
	if(GraphNode)
	{
		return GraphNode->GetAnimationAsset();
	}

	return NULL;
}

EVisibility SParentPlayerTreeRow::GetResetToDefaultVisibility() const
{
	FAnimParentNodeAssetOverride* HierarchyOverride = EditorObject->GetBlueprint()->GetAssetOverrideForNode(Item->Override->ParentNodeGuid, true);

	if(HierarchyOverride)
	{
		return Item->Override->NewAsset != HierarchyOverride->NewAsset ? EVisibility::Visible : EVisibility::Hidden;
	}

	return Item->Override->NewAsset != GraphNode->GetAnimationAsset() ? EVisibility::Visible : EVisibility::Hidden;
}

FReply SParentPlayerTreeRow::OnResetButtonClicked()
{
	FAnimParentNodeAssetOverride* HierarchyOverride = EditorObject->GetBlueprint()->GetAssetOverrideForNode(Item->Override->ParentNodeGuid, true);
	
	Item->Override->NewAsset = HierarchyOverride ? HierarchyOverride->NewAsset : GraphNode->GetAnimationAsset();

	// Apply will remove the override from the object
	EditorObject->ApplyOverrideToBlueprint(*Item->Override);
	return FReply::Handled();
}

FString SParentPlayerTreeRow::GetCurrentAssetPath() const
{
	const UAnimationAsset* Asset = GetCurrentAssetToUse();
	return Asset ? Asset->GetPathName() : FString("");
}

FORCENOINLINE bool FPlayerTreeViewEntry::operator==(const FPlayerTreeViewEntry& Other)
{
	return EntryName == Other.EntryName;
}

void FPlayerTreeViewEntry::GenerateNameWidget(TSharedPtr<SHorizontalBox> Box)
{
	// Get an appropriate image icon for the row
	const FSlateBrush* EntryImageBrush = NULL;
	switch(EntryType)
	{
		case EPlayerTreeViewEntryType::Blueprint:
			EntryImageBrush = FEditorStyle::GetBrush("ClassIcon.Blueprint");
			break;
		case EPlayerTreeViewEntryType::Graph:
			EntryImageBrush = FEditorStyle::GetBrush("GraphEditor.EventGraph_16x");
			break;
		case EPlayerTreeViewEntryType::Node:
			EntryImageBrush = FEditorStyle::GetBrush("GraphEditor.Default_16x");
			break;
		default:
			break;
	}

	Box->AddSlot()
		.VAlign(VAlign_Center)
		.AutoWidth()
		[
			SNew(SImage)
			.Image(EntryImageBrush)
		];

	Box->AddSlot()
		.VAlign(VAlign_Center)
		.Padding(FMargin(5.0f, 0.0f, 0.0f, 0.0f))
		.AutoWidth()
		[
			SNew(STextBlock)
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
			.Text(FText::FromString(EntryName))
		];
}

void FAnimGraphNodeBindingExtension::GetOptionalPinData(const IPropertyHandle& PropertyHandle, int32& OutOptionalPinIndex, UAnimGraphNode_Base*& OutAnimGraphNode) const
{
	OutOptionalPinIndex = INDEX_NONE;

	TArray<UObject*> Objects;
	PropertyHandle.GetOuterObjects(Objects);

	FProperty* Property = PropertyHandle.GetProperty();
	if(Property)
	{
		OutAnimGraphNode = Cast<UAnimGraphNode_Base>(Objects[0]);
		if (OutAnimGraphNode != nullptr)
		{
			OutOptionalPinIndex = OutAnimGraphNode->ShowPinForProperties.IndexOfByPredicate([Property](const FOptionalPinFromProperty& InOptionalPin)
			{
				return Property->GetFName() == InOptionalPin.PropertyName;
			});
		}
	}
}

bool FAnimGraphNodeBindingExtension::IsPropertyExtendable(const UClass* InObjectClass, const IPropertyHandle& PropertyHandle) const
{
	int32 OptionalPinIndex;
	UAnimGraphNode_Base* AnimGraphNode;
	GetOptionalPinData(PropertyHandle, OptionalPinIndex, AnimGraphNode);

	if(OptionalPinIndex != INDEX_NONE)
	{
		const FOptionalPinFromProperty& OptionalPin = AnimGraphNode->ShowPinForProperties[OptionalPinIndex];

		// Not optional
		if (!OptionalPin.bCanToggleVisibility && OptionalPin.bShowPin)
		{
			return false;
		}

		if(!PropertyHandle.GetProperty())
		{
			return false;
		}

		return OptionalPin.bCanToggleVisibility;
	}

	return false;
}

// Legacy binding widget
class SShowAsWidget : public SCompoundWidget
{
	SLATE_BEGIN_ARGS(SShowAsWidget) {}

	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedRef<IPropertyHandle>& InPropertyHandle)
	{
		PropertyHandle = InPropertyHandle;

		TSharedRef<SHorizontalBox> HorizontalBox = 
			SNew(SHorizontalBox)
			.ToolTipText(LOCTEXT("AsPinTooltip", "Show/hide this property as a pin on the node"));

		TWeakPtr<SWidget> WeakHorizontalBox = HorizontalBox;

		HorizontalBox->AddSlot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("ExposeAsPinLabel", "Expose"))
				.Font(IDetailLayoutBuilder::GetDetailFont())
				.Visibility_Lambda([WeakHorizontalBox](){ return WeakHorizontalBox.IsValid() && WeakHorizontalBox.Pin()->IsHovered() ? EVisibility::Visible : EVisibility::Collapsed; })
			];

		HorizontalBox->AddSlot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(3.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(SCheckBox)
				.IsChecked(this, &SShowAsWidget::IsChecked)
				.OnCheckStateChanged(this, &SShowAsWidget::OnCheckStateChanged)
			];

		ChildSlot
		[
			HorizontalBox
		];
	}

	ECheckBoxState IsChecked() const
	{
		bool bValue;
		FPropertyAccess::Result Result = PropertyHandle->GetValue(bValue);
		if(Result == FPropertyAccess::MultipleValues)
		{
			return ECheckBoxState::Undetermined;
		}
		else
		{
			return bValue ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
		}

		return ECheckBoxState::Unchecked;
	}

	void OnCheckStateChanged(ECheckBoxState InCheckBoxState)
	{
		bool bValue = InCheckBoxState == ECheckBoxState::Checked;
		PropertyHandle->SetValue(bValue);
	}

	TSharedPtr<IPropertyHandle> PropertyHandle;
};

static FText MakeTextPath(const TArray<FString>& InPath)
{
	return FText::FromString(Algo::Accumulate(InPath, FString(), [](const FString& InResult, const FString& InSegment)
		{ 
			return InResult.IsEmpty() ? InSegment : (InResult + TEXT(".") + InSegment);
		}));	
}

TSharedRef<SWidget> FAnimGraphNodeBindingExtension::GenerateExtensionWidget(const IDetailLayoutBuilder& InDetailBuilder, const UClass* InObjectClass, TSharedPtr<IPropertyHandle> InPropertyHandle)
{
	int32 OptionalPinIndex;
	UAnimGraphNode_Base* AnimGraphNode;
	GetOptionalPinData(*InPropertyHandle.Get(), OptionalPinIndex, AnimGraphNode);
	check(OptionalPinIndex != INDEX_NONE);

	TArray<UObject*> OuterObjects;
	InPropertyHandle->GetOuterObjects(OuterObjects);

	FProperty* AnimNodeProperty = InPropertyHandle->GetProperty();
	const FName PropertyName = AnimNodeProperty->GetFName();

	const FName OptionalPinArrayEntryName(*FString::Printf(TEXT("ShowPinForProperties[%d].bShowPin"), OptionalPinIndex));
	TSharedRef<IPropertyHandle> ShowPinPropertyHandle = InDetailBuilder.GetProperty(OptionalPinArrayEntryName, UAnimGraphNode_Base::StaticClass());
	ShowPinPropertyHandle->MarkHiddenByCustomization();

	UBlueprint* Blueprint = AnimGraphNode->GetAnimBlueprint();

	if(IModularFeatures::Get().IsModularFeatureAvailable("PropertyAccessEditor"))
	{
		FPropertyBindingWidgetArgs Args;

		Args.Property = InPropertyHandle->GetProperty();

		Args.OnCanBindProperty = FOnCanBindProperty::CreateLambda([AnimNodeProperty](FProperty* InProperty)
		{
			// Note: We support type promotion here
			IPropertyAccessEditor& PropertyAccessEditor = IModularFeatures::Get().GetModularFeature<IPropertyAccessEditor>("PropertyAccessEditor");
			return PropertyAccessEditor.GetPropertyCompatibility(InProperty, AnimNodeProperty) != EPropertyAccessCompatibility::Incompatible;
		});

		Args.OnCanBindFunction = FOnCanBindFunction::CreateLambda([AnimNodeProperty](UFunction* InFunction)
		{
			IPropertyAccessEditor& PropertyAccessEditor = IModularFeatures::Get().GetModularFeature<IPropertyAccessEditor>("PropertyAccessEditor");

			// Note: We support type promotion here
			return InFunction->NumParms == 1 
				&& PropertyAccessEditor.GetPropertyCompatibility(InFunction->GetReturnProperty(), AnimNodeProperty) != EPropertyAccessCompatibility::Incompatible
				&& InFunction->HasAnyFunctionFlags(FUNC_BlueprintPure);
		});

		Args.OnCanBindToClass = FOnCanBindToClass::CreateLambda([](UClass* InClass)
		{
			return true;
		});

		Args.OnAddBinding = FOnAddBinding::CreateLambda([OuterObjects, Blueprint, ShowPinPropertyHandle, AnimNodeProperty](FName InPropertyName, const TArray<FBindingChainElement>& InBindingChain)
		{
			const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
			IPropertyAccessEditor& PropertyAccessEditor = IModularFeatures::Get().GetModularFeature<IPropertyAccessEditor>("PropertyAccessEditor");

			for(UObject* OuterObject : OuterObjects)
			{
				if(UAnimGraphNode_Base* AnimGraphNode = Cast<UAnimGraphNode_Base>(OuterObject))
				{
					AnimGraphNode->Modify();

					const FFieldVariant& LeafField = InBindingChain.Last().Field;

					FAnimGraphNodePropertyBinding Binding;
					Binding.PropertyName = InPropertyName;
					PropertyAccessEditor.MakeStringPath(InBindingChain, Binding.PropertyPath);
					Binding.PathAsText = MakeTextPath(Binding.PropertyPath);
					Binding.Type = LeafField.IsA<UFunction>() ? EAnimGraphNodePropertyBindingType::Function : EAnimGraphNodePropertyBindingType::Property;
					Binding.bIsBound = true;
					if(LeafField.IsA<FProperty>())
					{
						const FProperty* LeafProperty = LeafField.Get<FProperty>();
						if(LeafProperty)
						{
							if(PropertyAccessEditor.GetPropertyCompatibility(LeafProperty, AnimNodeProperty) == EPropertyAccessCompatibility::Promotable)
							{
								Binding.bIsPromotion = true;
								Schema->ConvertPropertyToPinType(LeafProperty, Binding.PromotedPinType);
							}

							Schema->ConvertPropertyToPinType(LeafProperty, Binding.PinType);
						}
					}
					else if(LeafField.IsA<UFunction>())
					{
						const UFunction* LeafFunction = LeafField.Get<UFunction>();
						if(LeafFunction)
						{
							if(FProperty* ReturnProperty = LeafFunction->GetReturnProperty())
							{
								if(PropertyAccessEditor.GetPropertyCompatibility(ReturnProperty, AnimNodeProperty) == EPropertyAccessCompatibility::Promotable)
								{
									Binding.bIsPromotion = true;
									Schema->ConvertPropertyToPinType(ReturnProperty, Binding.PromotedPinType);
								}

								Schema->ConvertPropertyToPinType(ReturnProperty, Binding.PinType);
							}
						}
					}
					AnimGraphNode->PropertyBindings.Add(InPropertyName, Binding);
				}

				FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
			}

			ShowPinPropertyHandle->SetValue(true);
		});

		Args.OnRemoveBinding = FOnRemoveBinding::CreateLambda([OuterObjects, Blueprint](FName InPropertyName)
		{
			for(UObject* OuterObject : OuterObjects)
			{
				if(UAnimGraphNode_Base* AnimGraphNode = Cast<UAnimGraphNode_Base>(OuterObject))
				{
					AnimGraphNode->Modify();

					AnimGraphNode->PropertyBindings.Remove(InPropertyName);
				}
			}

			FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
		});

		Args.OnCanRemoveBinding = FOnCanRemoveBinding::CreateLambda([OuterObjects](FName InPropertyName)
		{
			for(UObject* OuterObject : OuterObjects)
			{
				if(UAnimGraphNode_Base* AnimGraphNode = Cast<UAnimGraphNode_Base>(OuterObject))
				{
					if(AnimGraphNode->PropertyBindings.Contains(InPropertyName))
					{
						return true;
					}
				}
			}

			return false;
		});

		enum class ECurrentValueType : int32
		{
			None,
			Pin,
			Binding,
			MultipleValues,
		};

		Args.CurrentBindingText = MakeAttributeLambda([OuterObjects, PropertyName, ShowPinPropertyHandle]()
		{
			ECurrentValueType CurrentValueType = ECurrentValueType::None;

			const FText MultipleValues = LOCTEXT("MultipleValues", "Multiple Values");
			const FText Bind = LOCTEXT("Bind", "Bind");
			const FText ExposedAsPin = LOCTEXT("ExposedAsPin", "Exposed As Pin");
			FText CurrentValue = Bind;

			auto SetAssignValue = [&CurrentValueType, &CurrentValue, &MultipleValues](const FText& InValue, ECurrentValueType InType)
			{
				if(CurrentValueType != ECurrentValueType::MultipleValues)
				{
					if(CurrentValueType == ECurrentValueType::None)
					{
						CurrentValueType = InType;
						CurrentValue = InValue;
					}
					else if(CurrentValueType == InType)
					{
						if(!CurrentValue.EqualTo(InValue))
						{
							CurrentValueType = ECurrentValueType::MultipleValues;
							CurrentValue = MultipleValues;
						}
					}
					else
					{
						CurrentValueType = ECurrentValueType::MultipleValues;
						CurrentValue = MultipleValues;
					}
				}
			};

			for(UObject* OuterObject : OuterObjects)
			{
				if(UAnimGraphNode_Base* AnimGraphNode = Cast<UAnimGraphNode_Base>(OuterObject))
				{
					if(FAnimGraphNodePropertyBinding* BindingPtr = AnimGraphNode->PropertyBindings.Find(PropertyName))
					{
						SetAssignValue(BindingPtr->PathAsText, ECurrentValueType::Binding);
					}
					else
					{
						bool bAsPin = false;
						FPropertyAccess::Result Result = ShowPinPropertyHandle->GetValue(bAsPin);
						if(Result == FPropertyAccess::MultipleValues)
						{
							SetAssignValue(MultipleValues, ECurrentValueType::MultipleValues);
						}
						else if(bAsPin)
						{
							SetAssignValue(ExposedAsPin, ECurrentValueType::Pin);
						}
						else
						{
							SetAssignValue(Bind, ECurrentValueType::None);
						}
					}
				}
			}

			return CurrentValue;
		});

		Args.CurrentBindingImage = MakeAttributeLambda([OuterObjects, PropertyName, OptionalPinIndex]() -> const FSlateBrush*
		{
			static FName PropertyIcon(TEXT("Kismet.Tabs.Variables"));
			static FName FunctionIcon(TEXT("GraphEditor.Function_16x"));

			EAnimGraphNodePropertyBindingType BindingType = EAnimGraphNodePropertyBindingType::None;
			for(UObject* OuterObject : OuterObjects)
			{
				if(UAnimGraphNode_Base* AnimGraphNode = Cast<UAnimGraphNode_Base>(OuterObject))
				{
					if(AnimGraphNode->ShowPinForProperties[OptionalPinIndex].bShowPin)
					{
						BindingType = EAnimGraphNodePropertyBindingType::None;
						break;
					}
					else if(FAnimGraphNodePropertyBinding* BindingPtr = AnimGraphNode->PropertyBindings.Find(PropertyName))
					{
						if(BindingType == EAnimGraphNodePropertyBindingType::None)
						{
							BindingType = BindingPtr->Type;
						}
						else if(BindingType != BindingPtr->Type)
						{
							BindingType = EAnimGraphNodePropertyBindingType::None;
							break;
						}
					}
					else if(BindingType != EAnimGraphNodePropertyBindingType::None)
					{
						BindingType = EAnimGraphNodePropertyBindingType::None;
						break;
					}
				}
			}

			if (BindingType == EAnimGraphNodePropertyBindingType::Function)
			{
				return FEditorStyle::GetBrush(FunctionIcon);
			}
			else
			{
				return FEditorStyle::GetBrush(PropertyIcon);
			}
		});

		Args.CurrentBindingColor = MakeAttributeLambda([OuterObjects, InPropertyHandle, OptionalPinIndex, PropertyName]() -> FLinearColor
		{
			const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();

			FEdGraphPinType PinType;
			Schema->ConvertPropertyToPinType(InPropertyHandle->GetProperty(), PinType);
			FLinearColor BindingColor = Schema->GetPinTypeColor(PinType);

			enum class EPromotionState
			{
				NotChecked,
				NotPromoted,
				Promoted,
			} Promotion = EPromotionState::NotChecked;

			for(UObject* OuterObject : OuterObjects)
			{
				if(UAnimGraphNode_Base* AnimGraphNode = Cast<UAnimGraphNode_Base>(OuterObject))
				{
					if(AnimGraphNode->ShowPinForProperties[OptionalPinIndex].bShowPin)
					{
						if(Promotion == EPromotionState::NotChecked)
						{
							Promotion = EPromotionState::NotPromoted;
						}
						else if(Promotion == EPromotionState::Promoted)
						{
							BindingColor = FLinearColor::Gray;
							break;
						}
					}
					else if(FAnimGraphNodePropertyBinding* BindingPtr = AnimGraphNode->PropertyBindings.Find(PropertyName))
					{
						if(Promotion == EPromotionState::NotChecked)
						{
							if(BindingPtr->bIsPromotion)
							{
								Promotion = EPromotionState::Promoted;
								BindingColor = Schema->GetPinTypeColor(BindingPtr->PromotedPinType);
							}
							else
							{
								Promotion = EPromotionState::NotPromoted;
							}
						}
						else
						{
							EPromotionState NewPromotion = BindingPtr->bIsPromotion ? EPromotionState::Promoted : EPromotionState::NotPromoted;
							if(Promotion != NewPromotion)
							{
								BindingColor = FLinearColor::Gray;
								break;
							}
						}
					}
				}
			}

			return BindingColor;
		});

		Args.MenuExtender = MakeShared<FExtender>();
		Args.MenuExtender->AddMenuExtension("BindingActions", EExtensionHook::Before, nullptr, FMenuExtensionDelegate::CreateLambda([ShowPinPropertyHandle, OuterObjects, PropertyName, Blueprint](FMenuBuilder& InMenuBuilder)
		{
			InMenuBuilder.BeginSection("Pins", LOCTEXT("Pin", "Pin"));
			{
				InMenuBuilder.AddMenuEntry(
					LOCTEXT("ExposeAsPin", "Expose As Pin"),
					LOCTEXT("ExposeAsPinTooltip", "Show/hide this property as a pin on the node"),
					FSlateIcon("EditorStyle", "GraphEditor.PinIcon"),
					FUIAction(
						FExecuteAction::CreateLambda([ShowPinPropertyHandle, OuterObjects, PropertyName, Blueprint]()
						{
							bool bValue = false;
							ShowPinPropertyHandle->GetValue(bValue);

							bool bHasBinding = false;

							for(UObject* OuterObject : OuterObjects)
							{
								if(UAnimGraphNode_Base* AnimGraphNode = Cast<UAnimGraphNode_Base>(OuterObject))
								{
									bHasBinding |= AnimGraphNode->PropertyBindings.Find(PropertyName) != nullptr;
								}
							}

							{
								FScopedTransaction Transaction(LOCTEXT("PinExposure", "Pin Exposure"));

								// Pins are exposed if we have a binding or not, so treat as unchecked only if we have
								// no binding
								ShowPinPropertyHandle->SetValue(!bValue || bHasBinding);

								// Switching from non-pin to pin, remove any bindings
								for(UObject* OuterObject : OuterObjects)
								{
									if(UAnimGraphNode_Base* AnimGraphNode = Cast<UAnimGraphNode_Base>(OuterObject))
									{
										AnimGraphNode->Modify();

										AnimGraphNode->PropertyBindings.Remove(PropertyName);
									}
								}

								FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
							}
						}),
						FCanExecuteAction(),
						FGetActionCheckState::CreateLambda([ShowPinPropertyHandle, OuterObjects, PropertyName]()
						{
							bool bValue;
							FPropertyAccess::Result Result = ShowPinPropertyHandle->GetValue(bValue);
							if(Result == FPropertyAccess::MultipleValues)
							{
								return ECheckBoxState::Undetermined;
							}
							else
							{
								bool bHasBinding = false;

								for(UObject* OuterObject : OuterObjects)
								{
									if(UAnimGraphNode_Base* AnimGraphNode = Cast<UAnimGraphNode_Base>(OuterObject))
									{
										bHasBinding |= AnimGraphNode->PropertyBindings.Find(PropertyName) != nullptr;
									}
								}

								// Pins are exposed if we have a binding or not, so treat as unchecked only if we have
								// no binding
								bValue = bValue && !bHasBinding;

								return bValue ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
							}

							return ECheckBoxState::Unchecked;
						})
					),
					NAME_None,
					EUserInterfaceActionType::Check
				);
			}
			InMenuBuilder.EndSection();
		}));

		Args.bAllowNewBindings = false;
		Args.bAllowArrayElementBindings = true;
		Args.bAllowUObjectFunctions = true;

		IPropertyAccessEditor& PropertyAccessEditor = IModularFeatures::Get().GetModularFeature<IPropertyAccessEditor>("PropertyAccessEditor");
		return PropertyAccessEditor.MakePropertyBindingWidget(AnimGraphNode->GetAnimBlueprint(), Args);
	}
	else
	{
		return SNew(SShowAsWidget, ShowPinPropertyHandle);
	}
}

#undef LOCTEXT_NAMESPACE

