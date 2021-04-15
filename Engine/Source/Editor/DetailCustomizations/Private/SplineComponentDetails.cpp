// Copyright Epic Games, Inc. All Rights Reserved.

#include "SplineComponentDetails.h"
#include "SplineMetadataDetailsFactory.h"
#include "Misc/MessageDialog.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Class.h"
#include "UObject/UnrealType.h"
#include "Layout/Visibility.h"
#include "Misc/Attribute.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SWidget.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SButton.h"
#include "ComponentVisualizer.h"
#include "Editor/UnrealEdEngine.h"
#include "UnrealEdGlobals.h"
#include "IDetailCustomNodeBuilder.h"
#include "IDetailChildrenBuilder.h"
#include "DetailWidgetRow.h"
#include "DetailLayoutBuilder.h"
#include "Components/SplineComponent.h"
#include "DetailCategoryBuilder.h"
#include "SplineComponentVisualizer.h"
#include "Widgets/Input/SVectorInputBox.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "ScopedTransaction.h"
#include "LevelEditorViewport.h"
#include "Engine/Blueprint.h"
#include "BlueprintEditor.h"
#include "BlueprintEditorModule.h"
#include "Subsystems/AssetEditorSubsystem.h"

#define LOCTEXT_NAMESPACE "SplineComponentDetails"
DEFINE_LOG_CATEGORY_STATIC(LogSplineComponentDetails, Log, All)

USplineMetadataDetailsFactoryBase::USplineMetadataDetailsFactoryBase(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{

}

class FSplinePointDetails : public IDetailCustomNodeBuilder, public TSharedFromThis<FSplinePointDetails>
{
public:
	FSplinePointDetails(USplineComponent* InOwningSplineComponent);

	//~ Begin IDetailCustomNodeBuilder interface
	virtual void SetOnRebuildChildren(FSimpleDelegate InOnRegenerateChildren) override;
	virtual void GenerateHeaderRowContent(FDetailWidgetRow& NodeRow) override;
	virtual void GenerateChildContent(IDetailChildrenBuilder& ChildrenBuilder) override;
	virtual void Tick(float DeltaTime) override;
	virtual bool RequiresTick() const override { return true; }
	virtual bool InitiallyCollapsed() const override { return false; }
	virtual FName GetName() const override;
	//~ End IDetailCustomNodeBuilder interface

	static bool bAlreadyWarnedInvalidIndex;

private:

	template <typename T>
	struct TSharedValue
	{
		TSharedValue() : bInitialized(false) {}

		void Reset()
		{
			bInitialized = false;
		}

		void Add(T InValue)
		{
			if (!bInitialized)
			{
				Value = InValue;
				bInitialized = true;
			}
			else
			{
				if (Value.IsSet() && InValue != Value.GetValue()) { Value.Reset(); }
			}
		}

		TOptional<T> Value;
		bool bInitialized;
	};

	struct FSharedVectorValue
	{
		FSharedVectorValue() : bInitialized(false) {}

		void Reset()
		{
			bInitialized = false;
		}

		bool IsValid() const { return bInitialized; }

		void Add(const FVector& V)
		{
			if (!bInitialized)
			{
				X = V.X;
				Y = V.Y;
				Z = V.Z;
				bInitialized = true;
			}
			else
			{
				if (X.IsSet() && V.X != X.GetValue()) { X.Reset(); }
				if (Y.IsSet() && V.Y != Y.GetValue()) { Y.Reset(); }
				if (Z.IsSet() && V.Z != Z.GetValue()) { Z.Reset(); }
			}
		}

		TOptional<float> X;
		TOptional<float> Y;
		TOptional<float> Z;
		bool bInitialized;
	};

	struct FSharedRotatorValue
	{
		FSharedRotatorValue() : bInitialized(false) {}

		void Reset()
		{
			bInitialized = false;
		}

		bool IsValid() const { return bInitialized; }

		void Add(const FRotator& R)
		{
			if (!bInitialized)
			{
				Roll = R.Roll;
				Pitch = R.Pitch;
				Yaw = R.Yaw;
				bInitialized = true;
			}
			else
			{
				if (Roll.IsSet() && R.Roll != Roll.GetValue()) { Roll.Reset(); }
				if (Pitch.IsSet() && R.Pitch != Pitch.GetValue()) { Pitch.Reset(); }
				if (Yaw.IsSet() && R.Yaw != Yaw.GetValue()) { Yaw.Reset(); }
			}
		}

		TOptional<float> Roll;
		TOptional<float> Pitch;
		TOptional<float> Yaw;
		bool bInitialized;
	};

	EVisibility IsEnabled() const { return (SelectedKeys.Num() > 0) ? EVisibility::Visible : EVisibility::Collapsed; }
	EVisibility IsDisabled() const { return (SelectedKeys.Num() == 0) ? EVisibility::Visible : EVisibility::Collapsed; }
	bool IsOnePointSelected() const { return SelectedKeys.Num() == 1; }
	bool ArePointsSelected() const { return (SelectedKeys.Num() > 0); };
	bool AreNoPointsSelected() const { return (SelectedKeys.Num() == 0); };
	TOptional<float> GetInputKey() const { return InputKey.Value; }
	TOptional<float> GetPositionX() const { return Position.X; }
	TOptional<float> GetPositionY() const { return Position.Y; }
	TOptional<float> GetPositionZ() const { return Position.Z; }
	TOptional<float> GetArriveTangentX() const { return ArriveTangent.X; }
	TOptional<float> GetArriveTangentY() const { return ArriveTangent.Y; }
	TOptional<float> GetArriveTangentZ() const { return ArriveTangent.Z; }
	TOptional<float> GetLeaveTangentX() const { return LeaveTangent.X; }
	TOptional<float> GetLeaveTangentY() const { return LeaveTangent.Y; }
	TOptional<float> GetLeaveTangentZ() const { return LeaveTangent.Z; }
	TOptional<float> GetRotationRoll() const { return Rotation.Roll; }
	TOptional<float> GetRotationPitch() const { return Rotation.Pitch; }
	TOptional<float> GetRotationYaw() const { return Rotation.Yaw; }
	TOptional<float> GetScaleX() const { return Scale.X; }
	TOptional<float> GetScaleY() const { return Scale.Y; }
	TOptional<float> GetScaleZ() const { return Scale.Z; }
	void OnSetInputKey(float NewValue, ETextCommit::Type CommitInfo);
	void OnSetPosition(float NewValue, ETextCommit::Type CommitInfo, int32 Axis);
	void OnSetArriveTangent(float NewValue, ETextCommit::Type CommitInfo, int32 Axis);
	void OnSetLeaveTangent(float NewValue, ETextCommit::Type CommitInfo, int32 Axis);
	void OnSetRotation(float NewValue, ETextCommit::Type CommitInfo, int32 Axis);
	void OnSetScale(float NewValue, ETextCommit::Type CommitInfo, int32 Axis);
	FText GetPointType() const;
	void OnSplinePointTypeChanged(TSharedPtr<FString> NewValue, ESelectInfo::Type SelectInfo);
	TSharedRef<SWidget> OnGenerateComboWidget(TSharedPtr<FString> InComboString);

	void GenerateSplinePointSelectionControls(IDetailChildrenBuilder& ChildrenBuilder);
	FReply OnSelectFirstLastSplinePoint(bool bFirst);
	FReply OnSelectPrevNextSplinePoint(bool bNext, bool bAddToSelection);
	FReply OnSelectAllSplinePoints();

	USplineComponent* GetSplineComponentToVisualize() const;

	void UpdateValues();

	USplineComponent* SplineComp;
	USplineComponent* SplineCompArchetype;
	TSet<int32> SelectedKeys;

	TSharedValue<float> InputKey;
	FSharedVectorValue Position;
	FSharedVectorValue ArriveTangent;
	FSharedVectorValue LeaveTangent;
	FSharedVectorValue Scale;
	FSharedRotatorValue Rotation;
	TSharedValue<ESplinePointType::Type> PointType;

	TSharedPtr<FSplineComponentVisualizer> SplineVisualizer;
	FProperty* SplineCurvesProperty;
	TArray<TSharedPtr<FString>> SplinePointTypes;
	TSharedPtr<ISplineMetadataDetails> SplineMetaDataDetails;
	FSimpleDelegate OnRegenerateChildren;
};

bool FSplinePointDetails::bAlreadyWarnedInvalidIndex = false;

FSplinePointDetails::FSplinePointDetails(USplineComponent* InOwningSplineComponent)
	: SplineComp(nullptr)
{
	TSharedPtr<FComponentVisualizer> Visualizer = GUnrealEd->FindComponentVisualizer(InOwningSplineComponent->GetClass());
	SplineVisualizer = StaticCastSharedPtr<FSplineComponentVisualizer>(Visualizer);
	check(SplineVisualizer.IsValid());

	SplineCurvesProperty = FindFProperty<FProperty>(USplineComponent::StaticClass(), GET_MEMBER_NAME_CHECKED(USplineComponent, SplineCurves));

	UEnum* SplinePointTypeEnum = StaticEnum<ESplinePointType::Type>();
	check(SplinePointTypeEnum);
	for (int32 EnumIndex = 0; EnumIndex < SplinePointTypeEnum->NumEnums() - 1; ++EnumIndex)
	{
		SplinePointTypes.Add(MakeShareable(new FString(SplinePointTypeEnum->GetNameStringByIndex(EnumIndex))));
	}

	check(InOwningSplineComponent);
	if (InOwningSplineComponent->IsTemplate())
	{
		// For blueprints, SplineComp will be set to the preview actor in UpdateValues().
		SplineComp = nullptr;
		SplineCompArchetype = InOwningSplineComponent;
	}
	else
	{
		SplineComp = InOwningSplineComponent;
		SplineCompArchetype = nullptr;
	}

	bAlreadyWarnedInvalidIndex = false;
}

void FSplinePointDetails::SetOnRebuildChildren(FSimpleDelegate InOnRegenerateChildren)
{
	OnRegenerateChildren = InOnRegenerateChildren;
}

void FSplinePointDetails::GenerateHeaderRowContent(FDetailWidgetRow& NodeRow)
{
}

void FSplinePointDetails::GenerateSplinePointSelectionControls(IDetailChildrenBuilder& ChildrenBuilder)
{
	ChildrenBuilder.AddCustomRow(LOCTEXT("SelectSplinePoints", "Select Spline Points"))
	.NameContent()
	[
		SNew(STextBlock)
		.Font(IDetailLayoutBuilder::GetDetailFont())
		.Text(LOCTEXT("SelectSplinePoints", "Select Spline Points"))
	]
	.ValueContent()
	.MaxDesiredWidth(125.f)
	.MinDesiredWidth(125.f)
	[
		SNew(SHorizontalBox)
		.Clipping(EWidgetClipping::ClipToBounds)
		+ SHorizontalBox::Slot()
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		[
			SNew(SButton)
			.ButtonStyle(FEditorStyle::Get(), "SplineComponentDetails.SelectFirst")
			.ContentPadding(2.0f)
			.ToolTipText(LOCTEXT("SelectFirstSplinePointToolTip", "Select first spline point."))
			.OnClicked(this, &FSplinePointDetails::OnSelectFirstLastSplinePoint, true)
		]
		+ SHorizontalBox::Slot()
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		[
			SNew(SButton)
			.ButtonStyle(FEditorStyle::Get(), "SplineComponentDetails.AddPrev")
			.ContentPadding(2.f)
			.ToolTipText(LOCTEXT("SelectAddPrevSplinePointToolTip", "Add previous spline point to current selection."))
			.OnClicked(this, &FSplinePointDetails::OnSelectPrevNextSplinePoint, false, true)
			.IsEnabled(this, &FSplinePointDetails::ArePointsSelected)
		]
		+ SHorizontalBox::Slot()
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		[
			SNew(SButton)
			.ButtonStyle(FEditorStyle::Get(), "SplineComponentDetails.SelectPrev")
			.ContentPadding(2.f)
			.VAlign(VAlign_Center)
			.HAlign(HAlign_Center)
			.ToolTipText(LOCTEXT("SelectPrevSplinePointToolTip", "Select previous spline point."))
			.OnClicked(this, &FSplinePointDetails::OnSelectPrevNextSplinePoint, false, false)
			.IsEnabled(this, &FSplinePointDetails::ArePointsSelected)
		]
		+ SHorizontalBox::Slot()
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		[
			SNew(SButton)
			.ButtonStyle(FEditorStyle::Get(), "SplineComponentDetails.SelectAll")
			.ContentPadding(2.f)
			.VAlign(VAlign_Center)
			.HAlign(HAlign_Center)
			.ToolTipText(LOCTEXT("SelectAllSplinePointToolTip", "Select all spline points."))
			.OnClicked(this, &FSplinePointDetails::OnSelectAllSplinePoints)
		]
		+ SHorizontalBox::Slot()
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		[
			SNew(SButton)
			.ButtonStyle(FEditorStyle::Get(), "SplineComponentDetails.SelectNext")
			.ContentPadding(2.f)
			.VAlign(VAlign_Center)
			.HAlign(HAlign_Center)
			.ToolTipText(LOCTEXT("SelectNextSplinePointToolTip", "Select next spline point."))
			.OnClicked(this, &FSplinePointDetails::OnSelectPrevNextSplinePoint, true, false)
			.IsEnabled(this, &FSplinePointDetails::ArePointsSelected)
		]
		+ SHorizontalBox::Slot()
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		[
			SNew(SButton)
			.ButtonStyle(FEditorStyle::Get(), "SplineComponentDetails.AddNext")
			.ContentPadding(2.f)
			.VAlign(VAlign_Center)
			.HAlign(HAlign_Center)
			.ToolTipText(LOCTEXT("SelectAddNextSplinePointToolTip", "Add next spline point to current selection."))
			.OnClicked(this, &FSplinePointDetails::OnSelectPrevNextSplinePoint, true, true)
			.IsEnabled(this, &FSplinePointDetails::ArePointsSelected)
		]
		+ SHorizontalBox::Slot()
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		[
			SNew(SButton)
			.ButtonStyle(FEditorStyle::Get(), "SplineComponentDetails.SelectLast")
			.ContentPadding(2.f)
			.VAlign(VAlign_Center)
			.HAlign(HAlign_Center)
			.ToolTipText(LOCTEXT("SelectLastSplinePointToolTip", "Select last spline point."))
			.OnClicked(this, &FSplinePointDetails::OnSelectFirstLastSplinePoint, false)
		]
	];
}

void FSplinePointDetails::GenerateChildContent(IDetailChildrenBuilder& ChildrenBuilder)
{
	// Select spline point buttons
	GenerateSplinePointSelectionControls(ChildrenBuilder);

	// Message which is shown when no points are selected
	ChildrenBuilder.AddCustomRow(LOCTEXT("NoneSelected", "None selected"))
	.Visibility(TAttribute<EVisibility>(this, &FSplinePointDetails::IsDisabled))
	[
		SNew(SBox)
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("NoPointsSelected", "No spline points are selected."))
			.Font(IDetailLayoutBuilder::GetDetailFont())
		]
	];

	// Input key
	ChildrenBuilder.AddCustomRow(LOCTEXT("InputKey", "Input Key"))
	.Visibility(TAttribute<EVisibility>(this, &FSplinePointDetails::IsEnabled))
	.NameContent()
	.HAlign(HAlign_Left)
	.VAlign(VAlign_Center)
	[
		SNew(STextBlock)
		.Text(LOCTEXT("InputKey", "Input Key"))
		.Font(IDetailLayoutBuilder::GetDetailFont())
	]
	.ValueContent()
	.MinDesiredWidth(125.0f)
	.MaxDesiredWidth(125.0f)
	[
		SNew(SNumericEntryBox<float>)
		.IsEnabled(TAttribute<bool>(this, &FSplinePointDetails::IsOnePointSelected))
		.Value(this, &FSplinePointDetails::GetInputKey)
		.UndeterminedString(LOCTEXT("Multiple", "Multiple"))
		.OnValueCommitted(this, &FSplinePointDetails::OnSetInputKey)
		.Font(IDetailLayoutBuilder::GetDetailFont())
	];

	// Position
	ChildrenBuilder.AddCustomRow(LOCTEXT("Position", "Position"))
	.Visibility(TAttribute<EVisibility>(this, &FSplinePointDetails::IsEnabled))
	.NameContent()
	.HAlign(HAlign_Left)
	.VAlign(VAlign_Center)
	[
		SNew(STextBlock)
		.Text(LOCTEXT("Position", "Position"))
		.Font(IDetailLayoutBuilder::GetDetailFont())
	]
	.ValueContent()
	.MinDesiredWidth(375.0f)
	.MaxDesiredWidth(375.0f)
	[
		SNew(SVectorInputBox)
		.X(this, &FSplinePointDetails::GetPositionX)
		.Y(this, &FSplinePointDetails::GetPositionY)
		.Z(this, &FSplinePointDetails::GetPositionZ)
		.AllowResponsiveLayout(true)
		.AllowSpin(false)
		.OnXCommitted(this, &FSplinePointDetails::OnSetPosition, 0)
		.OnYCommitted(this, &FSplinePointDetails::OnSetPosition, 1)
		.OnZCommitted(this, &FSplinePointDetails::OnSetPosition, 2)
		.Font(IDetailLayoutBuilder::GetDetailFont())
	];

	// ArriveTangent
	ChildrenBuilder.AddCustomRow(LOCTEXT("ArriveTangent", "Arrive Tangent"))
	.Visibility(TAttribute<EVisibility>(this, &FSplinePointDetails::IsEnabled))
	.NameContent()
	.HAlign(HAlign_Left)
	.VAlign(VAlign_Center)
	[
		SNew(STextBlock)
		.Text(LOCTEXT("ArriveTangent", "Arrive Tangent"))
		.Font(IDetailLayoutBuilder::GetDetailFont())
	]
	.ValueContent()
	.MinDesiredWidth(375.0f)
	.MaxDesiredWidth(375.0f)
	[
		SNew(SVectorInputBox)
		.X(this, &FSplinePointDetails::GetArriveTangentX)
		.Y(this, &FSplinePointDetails::GetArriveTangentY)
		.Z(this, &FSplinePointDetails::GetArriveTangentZ)
		.AllowResponsiveLayout(true)
		.AllowSpin(false)
		.OnXCommitted(this, &FSplinePointDetails::OnSetArriveTangent, 0)
		.OnYCommitted(this, &FSplinePointDetails::OnSetArriveTangent, 1)
		.OnZCommitted(this, &FSplinePointDetails::OnSetArriveTangent, 2)
		.Font(IDetailLayoutBuilder::GetDetailFont())
	];

	// LeaveTangent
	ChildrenBuilder.AddCustomRow(LOCTEXT("LeaveTangent", "Leave Tangent"))
	.Visibility(TAttribute<EVisibility>(this, &FSplinePointDetails::IsEnabled))
	.NameContent()
	.HAlign(HAlign_Left)
	.VAlign(VAlign_Center)
	[
		SNew(STextBlock)
		.Text(LOCTEXT("LeaveTangent", "Leave Tangent"))
		.Font(IDetailLayoutBuilder::GetDetailFont())
	]
	.ValueContent()
	.MinDesiredWidth(375.0f)
	.MaxDesiredWidth(375.0f)
	[
		SNew(SVectorInputBox)
		.X(this, &FSplinePointDetails::GetLeaveTangentX)
		.Y(this, &FSplinePointDetails::GetLeaveTangentY)
		.Z(this, &FSplinePointDetails::GetLeaveTangentZ)
		.AllowResponsiveLayout(true)
		.AllowSpin(false)
		.OnXCommitted(this, &FSplinePointDetails::OnSetLeaveTangent, 0)
		.OnYCommitted(this, &FSplinePointDetails::OnSetLeaveTangent, 1)
		.OnZCommitted(this, &FSplinePointDetails::OnSetLeaveTangent, 2)
		.Font(IDetailLayoutBuilder::GetDetailFont())
	];

	// Rotation
	ChildrenBuilder.AddCustomRow(LOCTEXT("Rotation", "Rotation"))
	.Visibility(TAttribute<EVisibility>(this, &FSplinePointDetails::IsEnabled))
	.NameContent()
	.HAlign(HAlign_Left)
	.VAlign(VAlign_Center)
	[
		SNew(STextBlock)
		.Text(LOCTEXT("Rotation", "Rotation"))
		.Font(IDetailLayoutBuilder::GetDetailFont())
	]
	.ValueContent()
	.MinDesiredWidth(375.0f)
	.MaxDesiredWidth(375.0f)
	[
		SNew(SVectorInputBox)
		.X(this, &FSplinePointDetails::GetRotationRoll)
		.Y(this, &FSplinePointDetails::GetRotationPitch)
		.Z(this, &FSplinePointDetails::GetRotationYaw)
		.AllowResponsiveLayout(true)
		.AllowSpin(false)
		.OnXCommitted(this, &FSplinePointDetails::OnSetRotation, 0)
		.OnYCommitted(this, &FSplinePointDetails::OnSetRotation, 1)
		.OnZCommitted(this, &FSplinePointDetails::OnSetRotation, 2)
		.Font(IDetailLayoutBuilder::GetDetailFont())
	];

	// Scale
	ChildrenBuilder.AddCustomRow(LOCTEXT("Scale", "Scale"))
	.Visibility(TAttribute<EVisibility>(this, &FSplinePointDetails::IsEnabled))
	.NameContent()
	.HAlign(HAlign_Left)
	.VAlign(VAlign_Center)
	[
		SNew(STextBlock)
		.Text(LOCTEXT("Scale", "Scale"))
		.Font(IDetailLayoutBuilder::GetDetailFont())
	]
	.ValueContent()
	.MinDesiredWidth(375.0f)
	.MaxDesiredWidth(375.0f)
	[
		SNew(SVectorInputBox)
		.X(this, &FSplinePointDetails::GetScaleX)
		.Y(this, &FSplinePointDetails::GetScaleY)
		.Z(this, &FSplinePointDetails::GetScaleZ)
		.AllowSpin(false)
		.AllowResponsiveLayout(true)
		.OnXCommitted(this, &FSplinePointDetails::OnSetScale, 0)
		.OnYCommitted(this, &FSplinePointDetails::OnSetScale, 1)
		.OnZCommitted(this, &FSplinePointDetails::OnSetScale, 2)
		.Font(IDetailLayoutBuilder::GetDetailFont())
	];

	// Type
	ChildrenBuilder.AddCustomRow(LOCTEXT("Type", "Type"))
	.Visibility(TAttribute<EVisibility>(this, &FSplinePointDetails::IsEnabled))
	.NameContent()
	.HAlign(HAlign_Left)
	.VAlign(VAlign_Center)
	[
		SNew(STextBlock)
		.Text(LOCTEXT("Type", "Type"))
		.Font(IDetailLayoutBuilder::GetDetailFont())
	]
	.ValueContent()
	.MinDesiredWidth(125.0f)
	.MaxDesiredWidth(125.0f)
	[
		SNew(SComboBox<TSharedPtr<FString>>)
		.OptionsSource(&SplinePointTypes)
		.OnGenerateWidget(this, &FSplinePointDetails::OnGenerateComboWidget)
		.OnSelectionChanged(this, &FSplinePointDetails::OnSplinePointTypeChanged)
		[
			SNew(STextBlock)
			.Font(IDetailLayoutBuilder::GetDetailFont())
			.Text(this, &FSplinePointDetails::GetPointType)
		]
	];

	if (SplineComp && SplineVisualizer.IsValid() && SplineVisualizer->GetSelectedKeys().Num() > 0)
	{
		for (TObjectIterator<UClass> ClassIterator; ClassIterator; ++ClassIterator)
		{
			if (ClassIterator->IsChildOf(USplineMetadataDetailsFactoryBase::StaticClass()) && !ClassIterator->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
			{
				USplineMetadataDetailsFactoryBase* Factory = ClassIterator->GetDefaultObject<USplineMetadataDetailsFactoryBase>();
				const USplineMetadata* SplineMetadata = SplineComp->GetSplinePointsMetadata();
				if (SplineMetadata && SplineMetadata->GetClass() == Factory->GetMetadataClass())
				{
					SplineMetaDataDetails = Factory->Create();
					IDetailGroup& Group = ChildrenBuilder.AddGroup(SplineMetaDataDetails->GetName(), SplineMetaDataDetails->GetDisplayName());
					SplineMetaDataDetails->GenerateChildContent(Group);
					break;
				}
			}
		}
	}
}

void FSplinePointDetails::Tick(float DeltaTime)
{
	UpdateValues();
}

void FSplinePointDetails::UpdateValues()
{
	// Always update the spline component based on the spline component visualizer's currently edited component.
	SplineComp = SplineVisualizer.IsValid() ? SplineVisualizer->GetEditedSplineComponent() : nullptr;

	if (!SplineComp || !SplineVisualizer.IsValid())
	{
		SplineComp = nullptr;
		return;
	}

	bool bNeedsRebuild = false;
	const TSet<int32>& NewSelectedKeys = SplineVisualizer->GetSelectedKeys();

	if (NewSelectedKeys.Num() != SelectedKeys.Num())
	{
		bNeedsRebuild = true;
	}
	SelectedKeys = NewSelectedKeys;

	// Cache values to be shown by the details customization.
	// An unset optional value represents 'multiple values' (in the case where multiple points are selected).
	InputKey.Reset();
	Position.Reset();
	ArriveTangent.Reset();
	LeaveTangent.Reset();
	Rotation.Reset();
	Scale.Reset();
	PointType.Reset();

	// Only display point details when there are selected keys
	if (SelectedKeys.Num() > 0)
	{
		bool bValidIndices = true;
		for (int32 Index : SelectedKeys)
		{
			if (Index < 0 ||
				Index >= SplineComp->GetSplinePointsPosition().Points.Num() ||
				Index >= SplineComp->GetSplinePointsRotation().Points.Num() ||
				Index >= SplineComp->GetSplinePointsScale().Points.Num())
			{
				bValidIndices = false;
				if (!bAlreadyWarnedInvalidIndex)
				{
					UE_LOG(LogSplineComponentDetails, Error, TEXT("Spline component details selected keys contains invalid index %d for spline %s with %d points, %d rotations, %d scales"),
						Index, 
						*SplineComp->GetPathName(), 
						SplineComp->GetSplinePointsPosition().Points.Num(),
						SplineComp->GetSplinePointsRotation().Points.Num(),
						SplineComp->GetSplinePointsScale().Points.Num());
					bAlreadyWarnedInvalidIndex = true;
				}
				break;
			}
		}

		if (bValidIndices)
		{
			for (int32 Index : SelectedKeys)
			{
				InputKey.Add(SplineComp->GetSplinePointsPosition().Points[Index].InVal);
				Position.Add(SplineComp->GetSplinePointsPosition().Points[Index].OutVal);
				ArriveTangent.Add(SplineComp->GetSplinePointsPosition().Points[Index].ArriveTangent);
				LeaveTangent.Add(SplineComp->GetSplinePointsPosition().Points[Index].LeaveTangent);
				Rotation.Add(SplineComp->GetSplinePointsRotation().Points[Index].OutVal.Rotator());
				Scale.Add(SplineComp->GetSplinePointsScale().Points[Index].OutVal);
				PointType.Add(ConvertInterpCurveModeToSplinePointType(SplineComp->GetSplinePointsPosition().Points[Index].InterpMode));
			}

			if (SplineMetaDataDetails)
			{
				SplineMetaDataDetails->Update(SplineComp, SelectedKeys);
			}
		}
	}

	if (bNeedsRebuild)
	{
		OnRegenerateChildren.ExecuteIfBound();
	}
}

FName FSplinePointDetails::GetName() const
{
	static const FName Name("SplinePointDetails");
	return Name;
}

void FSplinePointDetails::OnSetInputKey(float NewValue, ETextCommit::Type CommitInfo)
{
	if ((CommitInfo != ETextCommit::OnEnter && CommitInfo != ETextCommit::OnUserMovedFocus) || !SplineComp)
	{
		return;
	}

	check(SelectedKeys.Num() == 1);
	const int32 Index = *SelectedKeys.CreateConstIterator();
	TArray<FInterpCurvePoint<FVector>>& Positions = SplineComp->GetSplinePointsPosition().Points;

	const int32 NumPoints = Positions.Num();

	bool bModifyOtherPoints = false;
	if ((Index > 0 && NewValue <= Positions[Index - 1].InVal) ||
		(Index < NumPoints - 1 && NewValue >= Positions[Index + 1].InVal))
	{
		const FText Title(LOCTEXT("InputKeyTitle", "Input key out of range"));
		const FText Message(LOCTEXT("InputKeyMessage", "Spline input keys must be numerically ascending. Would you like to modify other input keys in the spline in order to be able to set this value?"));

		// Ensure input keys remain ascending
		if (FMessageDialog::Open(EAppMsgType::YesNo, Message, &Title) == EAppReturnType::No)
		{
			return;
		}

		bModifyOtherPoints = true;
	}

	const FScopedTransaction Transaction(LOCTEXT("SetSplinePointInputKey", "Set spline point input key"));
	SplineComp->Modify();

	TArray<FInterpCurvePoint<FQuat>>& Rotations = SplineComp->GetSplinePointsRotation().Points;
	TArray<FInterpCurvePoint<FVector>>& Scales = SplineComp->GetSplinePointsScale().Points;

	if (bModifyOtherPoints)
	{
		// Shuffle the previous or next input keys down or up so the input value remains in sequence
		if (Index > 0 && NewValue <= Positions[Index - 1].InVal)
		{
			float Delta = (NewValue - Positions[Index].InVal);
			for (int32 PrevIndex = 0; PrevIndex < Index; PrevIndex++)
			{
				Positions[PrevIndex].InVal += Delta;
				Rotations[PrevIndex].InVal += Delta;
				Scales[PrevIndex].InVal += Delta;
			}
		}
		else if (Index < NumPoints - 1 && NewValue >= Positions[Index + 1].InVal)
		{
			float Delta = (NewValue - Positions[Index].InVal);
			for (int32 NextIndex = Index + 1; NextIndex < NumPoints; NextIndex++)
			{
				Positions[NextIndex].InVal += Delta;
				Rotations[NextIndex].InVal += Delta;
				Scales[NextIndex].InVal += Delta;
			}
		}
	}

	Positions[Index].InVal = NewValue;
	Rotations[Index].InVal = NewValue;
	Scales[Index].InVal = NewValue;

	SplineComp->UpdateSpline();
	SplineComp->bSplineHasBeenEdited = true;
	FComponentVisualizer::NotifyPropertyModified(SplineComp, SplineCurvesProperty);
	UpdateValues();

	GEditor->RedrawLevelEditingViewports(true);
}

void FSplinePointDetails::OnSetPosition(float NewValue, ETextCommit::Type CommitInfo, int32 Axis)
{
	if (!SplineComp)
	{
		return;
	}

	const FScopedTransaction Transaction(LOCTEXT("SetSplinePointPosition", "Set spline point position"));
	SplineComp->Modify();

	for (int32 Index : SelectedKeys)
	{
		if (Index < 0 || Index >= SplineComp->GetSplinePointsPosition().Points.Num())
		{
			UE_LOG(LogSplineComponentDetails, Error, TEXT("Set spline point location: invalid index %d in selected points for spline component %s which contains %d spline points."),
				Index, *SplineComp->GetPathName(), SplineComp->GetSplinePointsPosition().Points.Num());
			continue;
		}

		FVector PointPosition = SplineComp->GetSplinePointsPosition().Points[Index].OutVal;
		PointPosition.Component(Axis) = NewValue;
		SplineComp->GetSplinePointsPosition().Points[Index].OutVal = PointPosition;
	}

	SplineComp->UpdateSpline();
	SplineComp->bSplineHasBeenEdited = true;
	FComponentVisualizer::NotifyPropertyModified(SplineComp, SplineCurvesProperty);
	UpdateValues();

	GEditor->RedrawLevelEditingViewports(true);
}

void FSplinePointDetails::OnSetArriveTangent(float NewValue, ETextCommit::Type CommitInfo, int32 Axis)
{
	if (!SplineComp)
	{
		return;
	}

	const FScopedTransaction Transaction(LOCTEXT("SetSplinePointTangent", "Set spline point tangent"));
	SplineComp->Modify();

	for (int32 Index : SelectedKeys)
	{
		if (Index < 0 || Index >= SplineComp->GetSplinePointsPosition().Points.Num())
		{
			UE_LOG(LogSplineComponentDetails, Error, TEXT("Set spline point arrive tangent: invalid index %d in selected points for spline component %s which contains %d spline points."),
				Index, *SplineComp->GetPathName(), SplineComp->GetSplinePointsPosition().Points.Num());
			continue;
		}

		FVector PointTangent = SplineComp->GetSplinePointsPosition().Points[Index].ArriveTangent;
		PointTangent.Component(Axis) = NewValue;
		SplineComp->GetSplinePointsPosition().Points[Index].ArriveTangent = PointTangent;
		SplineComp->GetSplinePointsPosition().Points[Index].InterpMode = CIM_CurveUser;
	}

	SplineComp->UpdateSpline();
	SplineComp->bSplineHasBeenEdited = true;
	FComponentVisualizer::NotifyPropertyModified(SplineComp, SplineCurvesProperty);
	UpdateValues();

	GEditor->RedrawLevelEditingViewports(true);
}

void FSplinePointDetails::OnSetLeaveTangent(float NewValue, ETextCommit::Type CommitInfo, int32 Axis)
{
	if (!SplineComp)
	{
		return;
	}

	const FScopedTransaction Transaction(LOCTEXT("SetSplinePointTangent", "Set spline point tangent"));
	SplineComp->Modify();

	for (int32 Index : SelectedKeys)
	{
		if (Index < 0 || Index >= SplineComp->GetSplinePointsPosition().Points.Num())
		{
			UE_LOG(LogSplineComponentDetails, Error, TEXT("Set spline point leave tangent: invalid index %d in selected points for spline component %s which contains %d spline points."),
				Index, *SplineComp->GetPathName(), SplineComp->GetSplinePointsPosition().Points.Num());
			continue;
		}

		FVector PointTangent = SplineComp->GetSplinePointsPosition().Points[Index].LeaveTangent;
		PointTangent.Component(Axis) = NewValue;
		SplineComp->GetSplinePointsPosition().Points[Index].LeaveTangent = PointTangent;
		SplineComp->GetSplinePointsPosition().Points[Index].InterpMode = CIM_CurveUser;
	}

	SplineComp->UpdateSpline();
	SplineComp->bSplineHasBeenEdited = true;
	FComponentVisualizer::NotifyPropertyModified(SplineComp, SplineCurvesProperty);
	UpdateValues();

	GEditor->RedrawLevelEditingViewports(true);
}

void FSplinePointDetails::OnSetRotation(float NewValue, ETextCommit::Type CommitInfo, int32 Axis)
{
	if (!SplineComp)
	{
		return;
	}

	const FScopedTransaction Transaction(LOCTEXT("SetSplinePointRotation", "Set spline point rotation"));
	SplineComp->Modify();

	for (int32 Index : SelectedKeys)
	{
		if (Index < 0 || Index >= SplineComp->GetSplinePointsRotation().Points.Num())
		{
			UE_LOG(LogSplineComponentDetails, Error, TEXT("Set spline point rotation: invalid index %d in selected points for spline component %s which contains %d spline points."),
				Index, *SplineComp->GetPathName(), SplineComp->GetSplinePointsRotation().Points.Num());
			continue;
		}

		FRotator PointRotation = SplineComp->GetSplinePointsRotation().Points[Index].OutVal.Rotator();

		switch (Axis)
		{
			case 0: PointRotation.Roll = NewValue; break;
			case 1: PointRotation.Pitch = NewValue; break;
			case 2: PointRotation.Yaw = NewValue; break;
		}

		SplineComp->GetSplinePointsRotation().Points[Index].OutVal = PointRotation.Quaternion();
	}

	SplineComp->UpdateSpline();
	SplineComp->bSplineHasBeenEdited = true;
	FComponentVisualizer::NotifyPropertyModified(SplineComp, SplineCurvesProperty);
	UpdateValues();

	GEditor->RedrawLevelEditingViewports(true);
}

void FSplinePointDetails::OnSetScale(float NewValue, ETextCommit::Type CommitInfo, int32 Axis)
{
	if (!SplineComp)
	{
		return;
	}

	const FScopedTransaction Transaction(LOCTEXT("SetSplinePointScale", "Set spline point scale"));
	SplineComp->Modify();

	for (int32 Index : SelectedKeys)
	{
		if (Index < 0 || Index >= SplineComp->GetSplinePointsScale().Points.Num())
		{
			UE_LOG(LogSplineComponentDetails, Error, TEXT("Set spline point scale: invalid index %d in selected points for spline component %s which contains %d spline points."),
				Index, *SplineComp->GetPathName(), SplineComp->GetSplinePointsScale().Points.Num());
			continue;
		}

		FVector PointScale = SplineComp->GetSplinePointsScale().Points[Index].OutVal;
		PointScale.Component(Axis) = NewValue;
		SplineComp->GetSplinePointsScale().Points[Index].OutVal = PointScale;
	}

	SplineComp->UpdateSpline();
	SplineComp->bSplineHasBeenEdited = true;
	FComponentVisualizer::NotifyPropertyModified(SplineComp, SplineCurvesProperty);
	UpdateValues();

	GEditor->RedrawLevelEditingViewports(true);
}

FText FSplinePointDetails::GetPointType() const
{
	if (PointType.Value.IsSet())
	{
		return FText::FromString(*SplinePointTypes[PointType.Value.GetValue()]);
	}

	return LOCTEXT("MultipleTypes", "Multiple Types");
}

void FSplinePointDetails::OnSplinePointTypeChanged(TSharedPtr<FString> NewValue, ESelectInfo::Type SelectInfo)
{
	if (!SplineComp)
	{
		return;
	}

	const FScopedTransaction Transaction(LOCTEXT("SetSplinePointType", "Set spline point type"));
	SplineComp->Modify();

	EInterpCurveMode Mode = ConvertSplinePointTypeToInterpCurveMode((ESplinePointType::Type)SplinePointTypes.Find(NewValue));

	for (int32 Index : SelectedKeys)
	{
		if (Index < 0 || Index >= SplineComp->GetSplinePointsPosition().Points.Num())
		{
			UE_LOG(LogSplineComponentDetails, Error, TEXT("Set spline point type: invalid index %d in selected points for spline component %s which contains %d spline points."),
				Index, *SplineComp->GetPathName(), SplineComp->GetSplinePointsPosition().Points.Num());
			continue;
		}

		SplineComp->GetSplinePointsPosition().Points[Index].InterpMode = Mode;
	}

	SplineComp->UpdateSpline();
	SplineComp->bSplineHasBeenEdited = true;
	FComponentVisualizer::NotifyPropertyModified(SplineComp, SplineCurvesProperty);
	UpdateValues();

	GEditor->RedrawLevelEditingViewports(true);
}

USplineComponent* FSplinePointDetails::GetSplineComponentToVisualize() const
{
	if (SplineCompArchetype) 
	{
		check(SplineCompArchetype->IsTemplate());

		FBlueprintEditorModule& BlueprintEditorModule = FModuleManager::LoadModuleChecked<FBlueprintEditorModule>("Kismet");

		const UClass* BPClass;
		if (const AActor* OwningCDO = SplineCompArchetype->GetOwner())
		{
			// Native component template
			BPClass = OwningCDO->GetClass();
		}
		else
		{
			// Non-native component template
			BPClass = Cast<UClass>(SplineCompArchetype->GetOuter());
		}

		if (BPClass)
		{
			if (UBlueprint* Blueprint = UBlueprint::GetBlueprintFromClass(BPClass))
			{
				if (FBlueprintEditor* BlueprintEditor = StaticCast<FBlueprintEditor*>(GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->FindEditorForAsset(Blueprint, false)))
				{
					const AActor* PreviewActor = BlueprintEditor->GetPreviewActor();
					TArray<UObject*> Instances;
					SplineCompArchetype->GetArchetypeInstances(Instances);

					for (UObject* Instance : Instances)
					{
						USplineComponent* SplineCompInstance = Cast<USplineComponent>(Instance);
						if (SplineCompInstance->GetOwner() == PreviewActor)
						{
							return SplineCompInstance;
						}
					}
				}
			}
		}

		// If we failed to find an archetype instance, must return nullptr 
		// since component visualizer cannot visualize the archetype.
		return nullptr;
	}

	return SplineComp;
}

FReply FSplinePointDetails::OnSelectFirstLastSplinePoint(bool bFirst)
{
	if (SplineVisualizer.IsValid())
	{
		bool bActivateComponentVis = false;

		if (!SplineComp)
		{
			SplineComp = GetSplineComponentToVisualize();
			bActivateComponentVis = true;
		}

		if (SplineComp)
		{
			if (SplineVisualizer->HandleSelectFirstLastSplinePoint(SplineComp, bFirst))
			{
				if (bActivateComponentVis)
				{
					TSharedPtr<FComponentVisualizer> Visualizer = StaticCastSharedPtr<FComponentVisualizer>(SplineVisualizer);
					GUnrealEd->ComponentVisManager.SetActiveComponentVis(GCurrentLevelEditingViewportClient, Visualizer);
				}
			}
		}
	}
	return FReply::Handled();
}

FReply FSplinePointDetails::OnSelectPrevNextSplinePoint(bool bNext, bool bAddToSelection)
{
	if (SplineVisualizer.IsValid())
	{
		SplineVisualizer->OnSelectPrevNextSplinePoint(bNext, bAddToSelection);
	}
	return FReply::Handled();
}

FReply FSplinePointDetails::OnSelectAllSplinePoints()
{
	if (SplineVisualizer.IsValid())
	{
		bool bActivateComponentVis = false;

		if (!SplineComp)
		{
			SplineComp = GetSplineComponentToVisualize();
			bActivateComponentVis = true;
		}

		if (SplineComp)
		{
			if (SplineVisualizer->HandleSelectAllSplinePoints(SplineComp))
			{
				if (bActivateComponentVis)
				{
					TSharedPtr<FComponentVisualizer> Visualizer = StaticCastSharedPtr<FComponentVisualizer>(SplineVisualizer);
					GUnrealEd->ComponentVisManager.SetActiveComponentVis(GCurrentLevelEditingViewportClient, Visualizer);
				}
			}
		}
	}
	return FReply::Handled();
}

TSharedRef<SWidget> FSplinePointDetails::OnGenerateComboWidget(TSharedPtr<FString> InComboString)
{
	return SNew(STextBlock)
		.Text(FText::FromString(*InComboString))
		.Font(IDetailLayoutBuilder::GetDetailFont());
}

////////////////////////////////////

TSharedRef<IDetailCustomization> FSplineComponentDetails::MakeInstance()
{
	return MakeShareable(new FSplineComponentDetails);
}

void FSplineComponentDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	// Hide the SplineCurves property
	TSharedPtr<IPropertyHandle> SplineCurvesProperty = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(USplineComponent, SplineCurves));
	SplineCurvesProperty->MarkHiddenByCustomization();
	 

	TArray<TWeakObjectPtr<UObject>> ObjectsBeingCustomized;
	DetailBuilder.GetObjectsBeingCustomized(ObjectsBeingCustomized);

	if (ObjectsBeingCustomized.Num() == 1)
	{
		if (USplineComponent* SplineComp = Cast<USplineComponent>(ObjectsBeingCustomized[0]))
		{
			// Set the spline points details as important in order to have it on top 
			IDetailCategoryBuilder& Category = DetailBuilder.EditCategory("Selected Points", FText::GetEmpty(), ECategoryPriority::Important);
			TSharedRef<FSplinePointDetails> SplinePointDetails = MakeShareable(new FSplinePointDetails(SplineComp));
			Category.AddCustomBuilder(SplinePointDetails);
		}
	}
}

#undef LOCTEXT_NAMESPACE
