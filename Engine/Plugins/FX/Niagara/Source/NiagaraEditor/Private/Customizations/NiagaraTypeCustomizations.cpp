// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraTypeCustomizations.h"
#include "CoreMinimal.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "IDetailChildrenBuilder.h"
#include "NiagaraConstants.h"
#include "NiagaraConstants.h"
#include "NiagaraEditorStyle.h"
#include "NiagaraEmitter.h"
#include "NiagaraNodeOutput.h"
#include "NiagaraNodeParameterMapBase.h"
#include "NiagaraParameterMapHistory.h"
#include "NiagaraPlatformSet.h"
#include "NiagaraRendererProperties.h"
#include "NiagaraScriptSource.h"
#include "NiagaraScriptVariable.h"
#include "NiagaraSystem.h"
#include "NiagaraTypes.h"
#include "PlatformInfo.h"
#include "PropertyHandle.h"
#include "SGraphActionMenu.h"
#include "Scalability.h"
#include "ScopedTransaction.h"
#include "DeviceProfiles/DeviceProfile.h"
#include "DeviceProfiles/DeviceProfileManager.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/STextComboBox.h"
#include "Widgets/Layout/SWrapBox.h"
#include "NiagaraSimulationStageBase.h"
#include "Widgets/Text/STextBlock.h"
#include "NiagaraDataInterfaceRW.h"
#include "NiagaraSettings.h"
#include "Widgets/SNiagaraParameterName.h"

#define LOCTEXT_NAMESPACE "FNiagaraVariableAttributeBindingCustomization"

void FNiagaraNumericCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	TSharedPtr<IPropertyHandle> ValueHandle = PropertyHandle->GetChildHandle(TEXT("Value"));

	HeaderRow
		.NameContent()
		[
			PropertyHandle->CreatePropertyNameWidget()
		]
		.ValueContent()
		.MaxDesiredWidth(ValueHandle.IsValid() ? 125.f : 200.f)
		[
			// Some Niagara numeric types have no value so in that case just display their type name
			ValueHandle.IsValid()
			? ValueHandle->CreatePropertyValueWidget()
			: SNew(STextBlock)
			  .Text(FText::FromString(FName::NameToDisplayString(CastField<FStructProperty>(PropertyHandle->GetProperty())->Struct->GetName(), false)))
			  .Font(IDetailLayoutBuilder::GetDetailFont())
		];
}


void FNiagaraBoolCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	ValueHandle = PropertyHandle->GetChildHandle(TEXT("Value"));

	static const FName DefaultForegroundName("DefaultForeground");

	HeaderRow
		.NameContent()
		[
			PropertyHandle->CreatePropertyNameWidget()
		]
		.ValueContent()
		[
			SNew(SCheckBox)
			.OnCheckStateChanged(this, &FNiagaraBoolCustomization::OnCheckStateChanged)
			.IsChecked(this, &FNiagaraBoolCustomization::OnGetCheckState)
			.ForegroundColor(FEditorStyle::GetSlateColor(DefaultForegroundName))
			.Padding(0.0f)
		];
}

ECheckBoxState FNiagaraBoolCustomization::OnGetCheckState() const
{
	ECheckBoxState CheckState = ECheckBoxState::Undetermined;
	int32 Value;
	FPropertyAccess::Result Result = ValueHandle->GetValue(Value);
	if (Result == FPropertyAccess::Success)
	{
		CheckState = Value == FNiagaraBool::True ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
	}

	return CheckState;
}

void FNiagaraBoolCustomization::OnCheckStateChanged(ECheckBoxState InNewState)
{
	if (InNewState == ECheckBoxState::Checked)
	{
		ValueHandle->SetValue(FNiagaraBool::True);
	}
	else
	{
		ValueHandle->SetValue(FNiagaraBool::False);
	}
}

void FNiagaraMatrixCustomization::CustomizeChildren(TSharedRef<IPropertyHandle> PropertyHandle, IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	uint32 NumChildren = 0;
	PropertyHandle->GetNumChildren(NumChildren);

	for (uint32 ChildNum = 0; ChildNum < NumChildren; ++ChildNum)
	{
		ChildBuilder.AddProperty(PropertyHandle->GetChildHandle(ChildNum).ToSharedRef());
	}
}

TArray<FNiagaraVariableBase> FNiagaraStackAssetAction_VarBind::FindVariables(UNiagaraEmitter* InEmitter, bool bSystem, bool bEmitter, bool bParticles, bool bUser)
{
	TArray<FNiagaraVariableBase> Bindings;
	TArray<FNiagaraParameterMapHistory> Histories;

	UNiagaraScriptSource* Source = Cast<UNiagaraScriptSource>(InEmitter->GraphSource);
	if (Source)
	{
		Histories.Append(UNiagaraNodeParameterMapBase::GetParameterMaps(Source->NodeGraph));
	}

	if (bSystem || bEmitter)
	{
		UNiagaraSystem* Sys = InEmitter->GetTypedOuter<UNiagaraSystem>();
		if (Sys)
		{
			Source = Cast<UNiagaraScriptSource>(Sys->GetSystemUpdateScript()->GetSource()); 
			if (Source)
			{
				Histories.Append(UNiagaraNodeParameterMapBase::GetParameterMaps(Source->NodeGraph));
			}
		}
	}
	


	for (const FNiagaraParameterMapHistory& History : Histories)
	{
		for (const FNiagaraVariable& Var : History.Variables)
		{
			if (FNiagaraParameterMapHistory::IsAttribute(Var) && bParticles)
			{
				Bindings.AddUnique(Var);
			}
			else if (FNiagaraParameterMapHistory::IsSystemParameter(Var) && bSystem)
			{
				Bindings.AddUnique(Var);
			}
			else if (Var.IsInNameSpace(InEmitter->GetUniqueEmitterName()) && bEmitter)
			{
				TMap<FString, FString> Aliases;
				Aliases.Add(InEmitter->GetUniqueEmitterName(), FNiagaraConstants::EmitterNamespace.ToString());
				Bindings.AddUnique(Var.ResolveAliases(Var, Aliases));
			}
			else if (FNiagaraParameterMapHistory::IsAliasedEmitterParameter(Var) && bEmitter)
			{
				Bindings.AddUnique(Var);
			}
			else if (Var.IsInNameSpace(FNiagaraConstants::EmitterNamespace) && bEmitter)
			{
				Bindings.AddUnique(Var);
			}
			else if (FNiagaraParameterMapHistory::IsUserParameter(Var) && bUser)
			{
				Bindings.AddUnique(Var);
			}
		}
	}

	if (bUser)
	{
		UNiagaraSystem* Sys = InEmitter->GetTypedOuter<UNiagaraSystem>();
		if (Sys)
		{
			for (const FNiagaraVariable Var : Sys->GetExposedParameters().ReadParameterVariables())
			{
				Bindings.AddUnique(Var);
			}
		}
	}
	return Bindings;
}


FName FNiagaraVariableAttributeBindingCustomization::GetVariableName() const
{
	if (BaseEmitter && TargetVariableBinding)
	{
		return (TargetVariableBinding->GetName(RenderProps->GetCurrentSourceMode()));
	}
	return FName();
}

FText FNiagaraVariableAttributeBindingCustomization::GetCurrentText() const
{
	if (BaseEmitter && TargetVariableBinding)
	{
		return FText::FromName(TargetVariableBinding->GetName(RenderProps->GetCurrentSourceMode()));
	}
	return FText::FromString(TEXT("Missing"));
}

FText FNiagaraVariableAttributeBindingCustomization::GetTooltipText() const
{
	if (BaseEmitter && TargetVariableBinding)
	{
		FString DefaultValueStr = TargetVariableBinding->GetDefaultValueString();

		FText TooltipDesc = FText::Format(LOCTEXT("AttributeBindingTooltip", "Use the variable \"{0}\" if it exists, otherwise use the default \"{1}\" "), FText::FromName(TargetVariableBinding->GetName(RenderProps->GetCurrentSourceMode())),
			FText::FromString(DefaultValueStr));
		return TooltipDesc;
	}
	return FText::FromString(TEXT("Missing"));
}

TSharedRef<SWidget> FNiagaraVariableAttributeBindingCustomization::OnGetMenuContent() const
{
	FGraphActionMenuBuilder MenuBuilder;

	return SNew(SBorder)
		.BorderImage(FEditorStyle::GetBrush("Menu.Background"))
		.Padding(5)
		[
			SNew(SBox)
			[
				SNew(SGraphActionMenu)
				.OnActionSelected(const_cast<FNiagaraVariableAttributeBindingCustomization*>(this), &FNiagaraVariableAttributeBindingCustomization::OnActionSelected)
				.OnCreateWidgetForAction(SGraphActionMenu::FOnCreateWidgetForAction::CreateSP(const_cast<FNiagaraVariableAttributeBindingCustomization*>(this), &FNiagaraVariableAttributeBindingCustomization::OnCreateWidgetForAction))
				.OnCollectAllActions(const_cast<FNiagaraVariableAttributeBindingCustomization*>(this), &FNiagaraVariableAttributeBindingCustomization::CollectAllActions)
				.AutoExpandActionMenu(false)
				.ShowFilterTextBox(true)
			]
		];
}

TArray<FName> FNiagaraVariableAttributeBindingCustomization::GetNames(UNiagaraEmitter* InEmitter) const
{
	TArray<FName> Names;

	bool bSystem = true;
	bool bEmitter = true;
	bool bParticles = true;
	bool bUser = true;
	TArray<FNiagaraVariableBase> Vars = FNiagaraStackAssetAction_VarBind::FindVariables(InEmitter, bSystem, bEmitter, bParticles, bUser);
	for (const FNiagaraVariableBase& Var : Vars)
	{
		if (RenderProps && PropertyHandle.IsValid() && PropertyHandle->GetProperty() && RenderProps->IsSupportedVariableForBinding(Var, *PropertyHandle->GetProperty()->GetName()))
		{
			if (Var.GetType() == TargetVariableBinding->GetType())
				Names.AddUnique(Var.GetName());
		}
	}

	return Names;
}

void FNiagaraVariableAttributeBindingCustomization::CollectAllActions(FGraphActionListBuilderBase& OutAllActions)
{
	TArray<FName> EventNames = GetNames(BaseEmitter);
	FName EmitterName = BaseEmitter->GetFName();
	for (FName EventName : EventNames)
	{
		FText CategoryName = FText();
		FString DisplayNameString = FName::NameToDisplayString(EventName.ToString(), false);
		const FText NameText = FText::FromString(DisplayNameString);
		const FText TooltipDesc = FText::Format(LOCTEXT("SetFunctionPopupTooltip", "Use the variable \"{0}\" "), FText::FromString(DisplayNameString));
		TSharedPtr<FNiagaraStackAssetAction_VarBind> NewNodeAction(new FNiagaraStackAssetAction_VarBind(EventName, CategoryName, NameText,
			TooltipDesc, 0, FText()));
		OutAllActions.AddAction(NewNodeAction);
	}
}

TSharedRef<SWidget> FNiagaraVariableAttributeBindingCustomization::OnCreateWidgetForAction(struct FCreateWidgetForActionData* const InCreateData)
{
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SNiagaraParameterName)
			.ParameterName(((FNiagaraStackAssetAction_VarBind* const)InCreateData->Action.Get())->VarName)
			.IsReadOnly(true)
			//SNew(STextBlock)
			//.Text(InCreateData->Action->GetMenuDescription())
			.ToolTipText(InCreateData->Action->GetTooltipDescription())
		];
}


void FNiagaraVariableAttributeBindingCustomization::OnActionSelected(const TArray< TSharedPtr<FEdGraphSchemaAction> >& SelectedActions, ESelectInfo::Type InSelectionType)
{
	if (InSelectionType == ESelectInfo::OnMouseClick || InSelectionType == ESelectInfo::OnKeyPress || SelectedActions.Num() == 0)
	{
		for (int32 ActionIndex = 0; ActionIndex < SelectedActions.Num(); ActionIndex++)
		{
			TSharedPtr<FEdGraphSchemaAction> CurrentAction = SelectedActions[ActionIndex];

			if (CurrentAction.IsValid())
			{
				FSlateApplication::Get().DismissAllMenus();
				FNiagaraStackAssetAction_VarBind* EventSourceAction = (FNiagaraStackAssetAction_VarBind*)CurrentAction.Get();
				ChangeSource(EventSourceAction->VarName);
			}
		}
	}
}

void FNiagaraVariableAttributeBindingCustomization::ChangeSource(FName InVarName)
{
	FScopedTransaction Transaction(FText::Format(LOCTEXT("ChangeVariableSource", " Change Variable Source to \"{0}\" "), FText::FromName(InVarName)));
	TArray<UObject*> Objects;
	PropertyHandle->GetOuterObjects(Objects);
	for (UObject* Obj : Objects)
	{
		Obj->Modify();
	}
	check(BaseEmitter);
	check(RenderProps);

	PropertyHandle->NotifyPreChange();
	TargetVariableBinding->SetValue(InVarName, BaseEmitter, RenderProps->GetCurrentSourceMode());
	PropertyHandle->NotifyPostChange();
	PropertyHandle->NotifyFinishedChangingProperties();
}

void FNiagaraVariableAttributeBindingCustomization::ResetToDefault()
{
	UE_LOG(LogNiagaraEditor, Warning, TEXT("Reset to default!"));
}

EVisibility FNiagaraVariableAttributeBindingCustomization::IsResetToDefaultsVisible() const
{
	check(BaseEmitter);
	check(RenderProps);
	check(TargetVariableBinding);
	check(DefaultVariableBinding);
	return (!TargetVariableBinding->MatchesDefault(*DefaultVariableBinding, RenderProps->GetCurrentSourceMode()))
		? EVisibility::Visible
		: EVisibility::Hidden;
}

FReply FNiagaraVariableAttributeBindingCustomization::OnResetToDefaultsClicked()
{
	FScopedTransaction Transaction(LOCTEXT("ResetBindingParam", "Reset binding"));
	TArray<UObject*> Objects;
	PropertyHandle->GetOuterObjects(Objects);
	for (UObject* Obj : Objects)
	{
		Obj->Modify();
	}
	check(BaseEmitter);
	check(RenderProps);
	check(TargetVariableBinding);
	check(DefaultVariableBinding);

	PropertyHandle->NotifyPreChange();
	TargetVariableBinding->ResetToDefault(*DefaultVariableBinding, BaseEmitter, RenderProps->GetCurrentSourceMode());
	PropertyHandle->NotifyPostChange();
	PropertyHandle->NotifyFinishedChangingProperties();
	return FReply::Handled();
}

void FNiagaraVariableAttributeBindingCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> InPropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	RenderProps = nullptr;
	BaseEmitter = nullptr;
	PropertyHandle = InPropertyHandle;
	TArray<UObject*> Objects;
	PropertyHandle->GetOuterObjects(Objects);
	bool bAddDefault = true;

	InPropertyHandle->SetOnPropertyResetToDefault(FSimpleDelegate::CreateLambda([this]() { ResetToDefault(); }));
	//InPropertyHandle->ExecuteCustomResetToDefault

	/*FResetToDefaultOverride ResetOverride = FResetToDefaultOverride::Create(
		FIsResetToDefaultVisible::CreateSP(this, &FMotionControllerDetails::IsSourceValueModified),
		FResetToDefaultHandler::CreateSP(this, &FMotionControllerDetails::OnResetSourceValue)
	);

	PropertyRow.OverrideResetToDefault(ResetOverride); */
	InPropertyHandle->MarkResetToDefaultCustomized(true);

	if (Objects.Num() == 1)
	{
		RenderProps = Cast<UNiagaraRendererProperties>(Objects[0]);
		if (RenderProps)
		{
			BaseEmitter = Cast<UNiagaraEmitter>(RenderProps->GetOuter());

			if (BaseEmitter)
			{
				TargetVariableBinding = (FNiagaraVariableAttributeBinding*)PropertyHandle->GetValueBaseAddress((uint8*)Objects[0]);
				DefaultVariableBinding = (FNiagaraVariableAttributeBinding*)PropertyHandle->GetValueBaseAddress((uint8*)Objects[0]->GetClass()->GetDefaultObject());
				
				HeaderRow
					.NameContent()
					[
						PropertyHandle->CreatePropertyNameWidget()
					]
					.ValueContent()
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(0.0f, 0.0f, 4.0f, 0.0f)
						[
							SNew(SComboButton)
							.OnGetMenuContent(this, &FNiagaraVariableAttributeBindingCustomization::OnGetMenuContent)
							.ContentPadding(1)
							.ToolTipText(this, &FNiagaraVariableAttributeBindingCustomization::GetTooltipText)
							.ButtonStyle(FEditorStyle::Get(), "PropertyEditor.AssetComboStyle")
							.ForegroundColor(FEditorStyle::GetColor("PropertyEditor.AssetName.ColorAndOpacity"))
							.ButtonContent()
							[
								/*SNew(STextBlock)
								.Text(this, &FNiagaraVariableAttributeBindingCustomization::GetCurrentText)
								.Font(IDetailLayoutBuilder::GetDetailFont())*/
								SNew(SNiagaraParameterName)
								.ParameterName(this, &FNiagaraVariableAttributeBindingCustomization::GetVariableName)
								.IsReadOnly(true)
								//SNew(STextBlock)
								//.Text(InCreateData->Action->GetMenuDescription())
							]
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.Padding(2, 1)
						[
							SNew(SButton)
							.IsFocusable(false)
							.ToolTipText(LOCTEXT("ResetToDefaultToolTip", "Reset to Default"))
							.ButtonStyle(FEditorStyle::Get(), "NoBorder")
							.ContentPadding(0)
							.Visibility(this, &FNiagaraVariableAttributeBindingCustomization::IsResetToDefaultsVisible)
							.OnClicked(this, &FNiagaraVariableAttributeBindingCustomization::OnResetToDefaultsClicked)
							.Content()
							[
								SNew(SImage)
								.Image(FEditorStyle::GetBrush("PropertyWindow.DiffersFromDefault"))
							]
						]
					];
				bAddDefault = false;
			}
		}
	}
	

	if (bAddDefault)
	{
		HeaderRow
			.NameContent()
			[
				PropertyHandle->CreatePropertyNameWidget()
			]
		.ValueContent()
			.MaxDesiredWidth(200.f)
			[
				SNew(STextBlock)
				.Text(FText::FromString(FName::NameToDisplayString(CastField<FStructProperty>(PropertyHandle->GetProperty())->Struct->GetName(), false)))
				.Font(IDetailLayoutBuilder::GetDetailFont())
			];
	}
}


//////////////////////////////////////////////////////////////////////////

FName FNiagaraUserParameterBindingCustomization::GetVariableName() const
{
	if (BaseSystem && TargetUserParameterBinding)
	{
		return (TargetUserParameterBinding->Parameter.GetName());
	}
	return FName();
}

FText FNiagaraUserParameterBindingCustomization::GetCurrentText() const
{
	if (BaseSystem && TargetUserParameterBinding)
	{
		return FText::FromName(TargetUserParameterBinding->Parameter.GetName());
	}
	return FText::FromString(TEXT("Missing"));
}

FText FNiagaraUserParameterBindingCustomization::GetTooltipText() const
{
	if (BaseSystem && TargetUserParameterBinding && TargetUserParameterBinding->Parameter.IsValid())
	{
		FText TooltipDesc = FText::Format(LOCTEXT("UserParameterBindingTooltip", "Bound to the user parameter \"{0}\""), FText::FromName(TargetUserParameterBinding->Parameter.GetName()));
		return TooltipDesc;
	}
	return FText::FromString(TEXT("Missing"));
}

TSharedRef<SWidget> FNiagaraUserParameterBindingCustomization::OnGetMenuContent() const
{
	FGraphActionMenuBuilder MenuBuilder;

	return SNew(SBorder)
		.BorderImage(FEditorStyle::GetBrush("Menu.Background"))
		.Padding(5)
		[
			SNew(SBox)
			[
				SNew(SGraphActionMenu)
				.OnActionSelected(const_cast<FNiagaraUserParameterBindingCustomization*>(this), &FNiagaraUserParameterBindingCustomization::OnActionSelected)
		.OnCreateWidgetForAction(SGraphActionMenu::FOnCreateWidgetForAction::CreateSP(const_cast<FNiagaraUserParameterBindingCustomization*>(this), &FNiagaraUserParameterBindingCustomization::OnCreateWidgetForAction))
		.OnCollectAllActions(const_cast<FNiagaraUserParameterBindingCustomization*>(this), &FNiagaraUserParameterBindingCustomization::CollectAllActions)
		.AutoExpandActionMenu(false)
		.ShowFilterTextBox(true)
			]
		];
}

TArray<FName> FNiagaraUserParameterBindingCustomization::GetNames() const
{
	TArray<FName> Names;

	if (BaseSystem && TargetUserParameterBinding)
	{
		for (const FNiagaraVariable Var : BaseSystem->GetExposedParameters().ReadParameterVariables())
		{
			if (FNiagaraParameterMapHistory::IsUserParameter(Var) && Var.GetType() == TargetUserParameterBinding->Parameter.GetType())
			{
				Names.AddUnique(Var.GetName());
			}
		}
	}
	
	return Names;
}

void FNiagaraUserParameterBindingCustomization::CollectAllActions(FGraphActionListBuilderBase& OutAllActions)
{
	TArray<FName> UserParamNames = GetNames();
	for (FName UserParamName : UserParamNames)
	{
		FText CategoryName = FText();
		FString DisplayNameString = FName::NameToDisplayString(UserParamName.ToString(), false);
		const FText NameText = FText::FromString(DisplayNameString);
		const FText TooltipDesc = FText::Format(LOCTEXT("BindToUserParameter", "Bind to the User Parameter \"{0}\" "), FText::FromString(DisplayNameString));
		TSharedPtr<FNiagaraStackAssetAction_VarBind> NewNodeAction(new FNiagaraStackAssetAction_VarBind(UserParamName, CategoryName, NameText,
			TooltipDesc, 0, FText()));
		OutAllActions.AddAction(NewNodeAction);
	}
}

TSharedRef<SWidget> FNiagaraUserParameterBindingCustomization::OnCreateWidgetForAction(struct FCreateWidgetForActionData* const InCreateData)
{
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SNiagaraParameterName)
			.ParameterName(((FNiagaraStackAssetAction_VarBind* const)InCreateData->Action.Get())->VarName)
			.IsReadOnly(true)
			//SNew(STextBlock)
			//.Text(InCreateData->Action->GetMenuDescription())
			.ToolTipText(InCreateData->Action->GetTooltipDescription())
		];
}


void FNiagaraUserParameterBindingCustomization::OnActionSelected(const TArray< TSharedPtr<FEdGraphSchemaAction> >& SelectedActions, ESelectInfo::Type InSelectionType)
{
	if (InSelectionType == ESelectInfo::OnMouseClick || InSelectionType == ESelectInfo::OnKeyPress || SelectedActions.Num() == 0)
	{
		for (int32 ActionIndex = 0; ActionIndex < SelectedActions.Num(); ActionIndex++)
		{
			TSharedPtr<FEdGraphSchemaAction> CurrentAction = SelectedActions[ActionIndex];

			if (CurrentAction.IsValid())
			{
				FSlateApplication::Get().DismissAllMenus();
				FNiagaraStackAssetAction_VarBind* EventSourceAction = (FNiagaraStackAssetAction_VarBind*)CurrentAction.Get();
				ChangeSource(EventSourceAction->VarName);
			}
		}
	}
}

void FNiagaraUserParameterBindingCustomization::ChangeSource(FName InVarName)
{
	FScopedTransaction Transaction(FText::Format(LOCTEXT("ChangeUserParameterSource", " Change User Parameter Source to \"{0}\" "), FText::FromName(InVarName)));
	TArray<UObject*> Objects;
	PropertyHandle->GetOuterObjects(Objects);
	for (UObject* Obj : Objects)
	{
		Obj->Modify();
	}

	PropertyHandle->NotifyPreChange();
	TargetUserParameterBinding->Parameter.SetName(InVarName);
	//TargetUserParameterBinding->Parameter.SetType(FNiagaraTypeDefinition::GetUObjectDef()); Do not override the type here!
	//TargetVariableBinding->DataSetVariable = FNiagaraConstants::GetAttributeAsDataSetKey(TargetVariableBinding->BoundVariable);
	PropertyHandle->NotifyPostChange();
	PropertyHandle->NotifyFinishedChangingProperties();
}

void FNiagaraUserParameterBindingCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> InPropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	PropertyHandle = InPropertyHandle;
	TArray<UObject*> Objects;
	PropertyHandle->GetOuterObjects(Objects);
	bool bAddDefault = true;
	if (Objects.Num() == 1)
	{
		BaseSystem = Objects[0]->GetTypedOuter<UNiagaraSystem>();
		if (BaseSystem)
		{
			TargetUserParameterBinding = (FNiagaraUserParameterBinding*)PropertyHandle->GetValueBaseAddress((uint8*)Objects[0]);

			HeaderRow
				.NameContent()
				[
					PropertyHandle->CreatePropertyNameWidget()
				]
			.ValueContent()
				.MaxDesiredWidth(200.f)
				[
					SNew(SComboButton)
					.OnGetMenuContent(this, &FNiagaraUserParameterBindingCustomization::OnGetMenuContent)
					.ContentPadding(1)
					.ToolTipText(this, &FNiagaraUserParameterBindingCustomization::GetTooltipText)
					.ButtonStyle(FEditorStyle::Get(), "PropertyEditor.AssetComboStyle")
					.ForegroundColor(FEditorStyle::GetColor("PropertyEditor.AssetName.ColorAndOpacity"))
					.ButtonContent()
					[

						SNew(SNiagaraParameterName)
						.ParameterName(this, &FNiagaraUserParameterBindingCustomization::GetVariableName)
						.IsReadOnly(true)
					]
				];

			bAddDefault = false;
		}
	}

	if (bAddDefault)
	{
		HeaderRow
			.NameContent()
			[
				PropertyHandle->CreatePropertyNameWidget()
			]
		.ValueContent()
			.MaxDesiredWidth(200.f)
			[
				SNew(STextBlock)
				.Text(FText::FromString(FName::NameToDisplayString(CastField<FStructProperty>(PropertyHandle->GetProperty())->Struct->GetName(), false)))
			.Font(IDetailLayoutBuilder::GetDetailFont())
			];
	}
}

//////////////////////////////////////////////////////////////////////////
FName FNiagaraMaterialAttributeBindingCustomization::GetNiagaraVariableName() const
{
	if (BaseSystem && TargetParameterBinding)
	{
		return TargetParameterBinding->NiagaraVariable.GetName();
	}
	return FName();
}

FName FNiagaraMaterialAttributeBindingCustomization::GetNiagaraChildVariableName() const
{
	if (BaseSystem && TargetParameterBinding)
	{
		return TargetParameterBinding->NiagaraChildVariable.GetName();
	}
	return FName();
}

FText FNiagaraMaterialAttributeBindingCustomization::GetNiagaraCurrentText() const
{
	if (BaseSystem && TargetParameterBinding)
	{
		return MakeCurrentText(TargetParameterBinding->NiagaraVariable, TargetParameterBinding->NiagaraChildVariable);
	}
	return FText::FromString(TEXT("Missing"));
}


FText FNiagaraMaterialAttributeBindingCustomization::MakeCurrentText(const FNiagaraVariableBase& BaseVar, const FNiagaraVariableBase& ChildVar) 
{
	if (BaseVar.GetName().IsNone())
	{
		return FText::FromName(NAME_None);
	}

	FString DisplayNameString = FName::NameToDisplayString(BaseVar.GetName().ToString(), false);
	FNiagaraTypeDefinition TargetType = BaseVar.GetType();
	if (ChildVar.GetName() != NAME_None)
	{
		DisplayNameString += TEXT(" \"") + FName::NameToDisplayString(ChildVar.GetName().ToString(), false) + TEXT("\"");
		TargetType = ChildVar.GetType();
	}

	DisplayNameString += TEXT(" (") + FName::NameToDisplayString(TargetType.GetFName().ToString(), false) + TEXT(")");

	const FText NameText = FText::FromString(DisplayNameString);
	return NameText;
}

FText FNiagaraMaterialAttributeBindingCustomization::GetNiagaraTooltipText() const
{
	if (BaseSystem && TargetParameterBinding && TargetParameterBinding->NiagaraVariable.IsValid())
	{
		FText TooltipDesc = FText::Format(LOCTEXT("MaterialAttributeBindingTooltip", "Bound to the parameter \"{0}\""), MakeCurrentText(TargetParameterBinding->NiagaraVariable, TargetParameterBinding->NiagaraChildVariable));
		return TooltipDesc;
	}
	return FText::FromString(TEXT("Missing"));
}

TSharedRef<SWidget> FNiagaraMaterialAttributeBindingCustomization::OnGetNiagaraMenuContent() const
{
	FGraphActionMenuBuilder MenuBuilder;

	return SNew(SBorder)
		.BorderImage(FEditorStyle::GetBrush("Menu.Background"))
		.Padding(5)
		[
			SNew(SBox)
			[
				SNew(SGraphActionMenu)
				.OnActionSelected(const_cast<FNiagaraMaterialAttributeBindingCustomization*>(this), &FNiagaraMaterialAttributeBindingCustomization::OnNiagaraActionSelected)
				.OnCreateWidgetForAction(SGraphActionMenu::FOnCreateWidgetForAction::CreateSP(const_cast<FNiagaraMaterialAttributeBindingCustomization*>(this), &FNiagaraMaterialAttributeBindingCustomization::OnCreateWidgetForNiagaraAction))
				.OnCollectAllActions(const_cast<FNiagaraMaterialAttributeBindingCustomization*>(this), &FNiagaraMaterialAttributeBindingCustomization::CollectAllNiagaraActions)
				.AutoExpandActionMenu(false)
				.ShowFilterTextBox(true)
			]
		];
}


bool FNiagaraMaterialAttributeBindingCustomization::IsCompatibleNiagaraVariable(const FNiagaraVariable& InVar) const
{
	{

		if (InVar.GetType() == FNiagaraTypeDefinition::GetFloatDef() ||
			InVar.GetType() == FNiagaraTypeDefinition::GetVec4Def() ||
			InVar.GetType() == FNiagaraTypeDefinition::GetColorDef() ||
			InVar.GetType() == FNiagaraTypeDefinition::GetVec2Def() ||
			InVar.GetType() == FNiagaraTypeDefinition::GetVec3Def() ||
			InVar.GetType() == FNiagaraTypeDefinition::GetUObjectDef() ||
			InVar.GetType() == FNiagaraTypeDefinition::GetUTextureDef() ||
			InVar.GetType() == FNiagaraTypeDefinition::GetUTextureRenderTargetDef() )
		{
			return true;
		}
		else if (InVar.GetType().IsDataInterface())
		{
			return true;
		}
	}
	return false;
}

TArray<TPair<FNiagaraVariableBase, FNiagaraVariableBase> > FNiagaraMaterialAttributeBindingCustomization::GetNiagaraNames() const
{
	TArray<TPair<FNiagaraVariableBase, FNiagaraVariableBase>> Names;
	TArray < FNiagaraVariableBase > BaseVars;

	if (BaseSystem && BaseEmitter && TargetParameterBinding)
	{
		bool bSystem = true;
		bool bEmitter = true;
		bool bParticles = false;
		bool bUser = true;
		BaseVars = FNiagaraStackAssetAction_VarBind::FindVariables(BaseEmitter, bSystem, bEmitter, bParticles, bUser);

		TArray<UNiagaraScript*> Scripts;
		Scripts.Add(BaseSystem->GetSystemUpdateScript());
		Scripts.Add(BaseSystem->GetSystemSpawnScript());
		BaseEmitter->GetScripts(Scripts, false);

		TMap<FString, FString> EmitterAlias;
		EmitterAlias.Emplace(FNiagaraConstants::EmitterNamespace.ToString(), BaseEmitter->GetUniqueEmitterName());

		auto FindCachedDI = 
			[&](const FNiagaraVariableBase& BaseVariable) -> UNiagaraDataInterface*
			{
				FName VariableName = BaseVariable.GetName();
				if (BaseVariable.IsInNameSpace(FNiagaraConstants::EmitterNamespace))
				{
					VariableName = FNiagaraVariable::ResolveAliases(BaseVariable, EmitterAlias).GetName();
				}

				for (UNiagaraScript* Script : Scripts)
				{
					TArray<FNiagaraScriptDataInterfaceInfo>& CachedDIs = Script->GetCachedDefaultDataInterfaces();
					for (const FNiagaraScriptDataInterfaceInfo& Info : CachedDIs)
					{
						if (Info.RegisteredParameterMapWrite == VariableName)
						{
							return Info.DataInterface;
						}
					}
				}

				return BaseVariable.GetType().GetClass()->GetDefaultObject<UNiagaraDataInterface>();
			};

		for (const FNiagaraVariableBase& BaseVar : BaseVars)
		{
			if (BaseVar.IsDataInterface())
			{
				UNiagaraDataInterface* DI = FindCachedDI(BaseVar);
				if (DI && DI->CanExposeVariables())
				{
					TArray<FNiagaraVariableBase> ChildVars;
					DI->GetExposedVariables(ChildVars);
					for (const FNiagaraVariableBase& ChildVar : ChildVars)
					{
						if (IsCompatibleNiagaraVariable(ChildVar))
						{
							Names.AddUnique(TPair<FNiagaraVariableBase, FNiagaraVariableBase>(BaseVar, ChildVar));
						}
					}
				}
			}
			else if (IsCompatibleNiagaraVariable(BaseVar))
			{
				if (RenderProps && TargetParameterBinding && RenderProps->IsSupportedVariableForBinding(BaseVar, TargetParameterBinding->MaterialParameterName))
				{
					Names.AddUnique(TPair<FNiagaraVariableBase, FNiagaraVariableBase>(BaseVar, FNiagaraVariableBase()));
				}
			}
		}
	}

	return Names;
}

void FNiagaraMaterialAttributeBindingCustomization::CollectAllNiagaraActions(FGraphActionListBuilderBase& OutAllActions)
{
	TArray<TPair<FNiagaraVariableBase, FNiagaraVariableBase>> ParamNames = GetNiagaraNames();
	for (TPair<FNiagaraVariableBase, FNiagaraVariableBase> ParamPair : ParamNames)
	{
		FText CategoryName = FText();
		const FText NameText = MakeCurrentText(ParamPair.Key, ParamPair.Value);
		const FText TooltipDesc = FText::Format(LOCTEXT("BindToNiagaraParameter", "Bind to the Niagara Variable \"{0}\" "), NameText);
		FNiagaraStackAssetAction_VarBind* VarBind = new FNiagaraStackAssetAction_VarBind(ParamPair.Key.GetName(), CategoryName, NameText,
			TooltipDesc, 0, FText());
		VarBind->BaseVar = ParamPair.Key;
		VarBind->ChildVar = ParamPair.Value;
		TSharedPtr<FNiagaraStackAssetAction_VarBind> NewNodeAction(VarBind);
		OutAllActions.AddAction(NewNodeAction);
	}
}


FText FNiagaraMaterialAttributeBindingCustomization::GetNiagaraChildVariableText() const
{

	FName ChildVarName = GetNiagaraChildVariableName();
	FText ChildVarNameText = ChildVarName.IsNone() == false ? FText::FromString(TEXT("| ") + ChildVarName.ToString()) : FText::GetEmpty();
	return ChildVarNameText;
}

EVisibility FNiagaraMaterialAttributeBindingCustomization::GetNiagaraChildVariableVisibility() const
{
	FName ChildVarName = GetNiagaraChildVariableName();
	return ChildVarName.IsNone() ? EVisibility::Collapsed : EVisibility::Visible;
}

TSharedRef<SWidget> FNiagaraMaterialAttributeBindingCustomization::OnCreateWidgetForNiagaraAction(struct FCreateWidgetForActionData* const InCreateData)
{
	FName ChildVarName = (((FNiagaraStackAssetAction_VarBind* const)InCreateData->Action.Get())->ChildVar.GetName());
	FText ChildVarNameText = ChildVarName.IsNone() == false ? FText::FromString(TEXT("| ") + ChildVarName.ToString()) : FText::GetEmpty();
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(3, 0)
			[
				SNew(SNiagaraParameterName)
				.ParameterName(((FNiagaraStackAssetAction_VarBind* const)InCreateData->Action.Get())->VarName)
				.IsReadOnly(true)
				//SNew(STextBlock)
				//.Text(InCreateData->Action->GetMenuDescription())
				.ToolTipText(InCreateData->Action->GetTooltipDescription())
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(3, 0)
			[
				SNew(STextBlock)
				.Visibility(ChildVarName.IsNone() ? EVisibility::Collapsed : EVisibility::Visible)
				.Text(ChildVarNameText)
			]
		];
}


void FNiagaraMaterialAttributeBindingCustomization::OnNiagaraActionSelected(const TArray< TSharedPtr<FEdGraphSchemaAction> >& SelectedActions, ESelectInfo::Type InSelectionType)
{
	if (InSelectionType == ESelectInfo::OnMouseClick || InSelectionType == ESelectInfo::OnKeyPress || SelectedActions.Num() == 0)
	{
		for (int32 ActionIndex = 0; ActionIndex < SelectedActions.Num(); ActionIndex++)
		{
			TSharedPtr<FEdGraphSchemaAction> CurrentAction = SelectedActions[ActionIndex];

			if (CurrentAction.IsValid())
			{
				FSlateApplication::Get().DismissAllMenus();
				FNiagaraStackAssetAction_VarBind* EventSourceAction = (FNiagaraStackAssetAction_VarBind*)CurrentAction.Get();
				ChangeNiagaraSource(EventSourceAction);
			}
		}
	}
}

void FNiagaraMaterialAttributeBindingCustomization::ChangeNiagaraSource(FNiagaraStackAssetAction_VarBind* InVar)
{
	FScopedTransaction Transaction(FText::Format(LOCTEXT("ChangeParameterSource", " Change Parameter Source to \"{0}\" "), FText::FromName(InVar->VarName)));
	TArray<UObject*> Objects;
	PropertyHandle->GetOuterObjects(Objects);
	for (UObject* Obj : Objects)
	{
		Obj->Modify();
	}

	PropertyHandle->NotifyPreChange();
	TargetParameterBinding->NiagaraVariable = InVar->BaseVar;
	TargetParameterBinding->NiagaraChildVariable = InVar->ChildVar;
	TargetParameterBinding->CacheValues(BaseEmitter);
	//TargetParameterBinding->Parameter.SetType(FNiagaraTypeDefinition::GetUObjectDef()); Do not override the type here!
	//TargetVariableBinding->DataSetVariable = FNiagaraConstants::GetAttributeAsDataSetKey(TargetVariableBinding->BoundVariable);
	PropertyHandle->NotifyPostChange();
	PropertyHandle->NotifyFinishedChangingProperties();
}

FText FNiagaraMaterialAttributeBindingCustomization::GetMaterialCurrentText() const
{
	if (BaseSystem && TargetParameterBinding)
	{
		return FText::FromName(TargetParameterBinding->MaterialParameterName);
	}
	return FText::FromString(TEXT("Missing"));
}

FText FNiagaraMaterialAttributeBindingCustomization::GetMaterialTooltipText() const
{
	if (BaseSystem && TargetParameterBinding && TargetParameterBinding->MaterialParameterName.IsValid())
	{
		FText TooltipDesc = FText::Format(LOCTEXT("MaterialParameterBindingTooltip", "Bound to the parameter \"{0}\""), FText::FromName(TargetParameterBinding->MaterialParameterName));
		return TooltipDesc;
	}
	return FText::FromString(TEXT("Missing"));
}

TSharedRef<SWidget> FNiagaraMaterialAttributeBindingCustomization::OnGetMaterialMenuContent() const
{
	FGraphActionMenuBuilder MenuBuilder;

	return SNew(SBorder)
		.BorderImage(FEditorStyle::GetBrush("Menu.Background"))
		.Padding(5)
		[
			SNew(SBox)
			[
				SNew(SGraphActionMenu)
				.OnActionSelected(const_cast<FNiagaraMaterialAttributeBindingCustomization*>(this), &FNiagaraMaterialAttributeBindingCustomization::OnMaterialActionSelected)
				.OnCreateWidgetForAction(SGraphActionMenu::FOnCreateWidgetForAction::CreateSP(const_cast<FNiagaraMaterialAttributeBindingCustomization*>(this), &FNiagaraMaterialAttributeBindingCustomization::OnCreateWidgetForMaterialAction))
				.OnCollectAllActions(const_cast<FNiagaraMaterialAttributeBindingCustomization*>(this), &FNiagaraMaterialAttributeBindingCustomization::CollectAllMaterialActions)
				.AutoExpandActionMenu(false)
				.ShowFilterTextBox(true)
			]
		];
}

TArray<FName> FNiagaraMaterialAttributeBindingCustomization::GetMaterialNames() const
{
	TArray<FName> Names;

	if (BaseSystem && TargetParameterBinding && PropertyHandle)
	{
		TArray<UObject*> Objects;
		PropertyHandle->GetOuterObjects(Objects);

		if (Objects.Num() == 1)
		{
			UNiagaraRendererProperties* RendererProperties = Cast<UNiagaraRendererProperties>(Objects[0]);
			TArray<UMaterialInterface*> Materials;
			if (RendererProperties)
			{
				RendererProperties->GetUsedMaterials(nullptr, Materials);
			}

			TArray<FMaterialParameterInfo> ParameterInfo;
			for (UMaterialInterface* Material : Materials)
			{
				if (!Material)
				{
					continue;
				}

				{
					TArray<FMaterialParameterInfo> LocalParameterInfo;
					TArray<FGuid> ParameterIds;
					Material->GetAllTextureParameterInfo(LocalParameterInfo, ParameterIds);
					ParameterInfo.Append(LocalParameterInfo);
				}
				{
					TArray<FMaterialParameterInfo> LocalParameterInfo;
					TArray<FGuid> ParameterIds;
					Material->GetAllScalarParameterInfo(LocalParameterInfo, ParameterIds);
					ParameterInfo.Append(LocalParameterInfo);
				}
				{
					TArray<FMaterialParameterInfo> LocalParameterInfo;
					TArray<FGuid> ParameterIds;
					Material->GetAllVectorParameterInfo(LocalParameterInfo, ParameterIds);
					ParameterInfo.Append(LocalParameterInfo);
				}
			}

			for (const FMaterialParameterInfo& Var : ParameterInfo)
			{
				Names.AddUnique(Var.Name);
			}
		}
	}

	return Names;
}

void FNiagaraMaterialAttributeBindingCustomization::CollectAllMaterialActions(FGraphActionListBuilderBase& OutAllActions)
{
	TArray<FName> ParamNames = GetMaterialNames();
	for (FName ParamName : ParamNames)
	{
		FText CategoryName = FText();
		FString DisplayNameString = FName::NameToDisplayString(ParamName.ToString(), false);
		const FText NameText = FText::FromString(DisplayNameString);
		const FText TooltipDesc = FText::Format(LOCTEXT("BindToMaterialParameter", "Bind to the Material Variable \"{0}\" "), FText::FromString(DisplayNameString));
		TSharedPtr<FNiagaraStackAssetAction_VarBind> NewNodeAction(new FNiagaraStackAssetAction_VarBind(ParamName, CategoryName, NameText,
			TooltipDesc, 0, FText()));
		OutAllActions.AddAction(NewNodeAction);
	}
}

TSharedRef<SWidget> FNiagaraMaterialAttributeBindingCustomization::OnCreateWidgetForMaterialAction(struct FCreateWidgetForActionData* const InCreateData)
{
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(STextBlock)
			.Text(InCreateData->Action->GetMenuDescription())
		.ToolTipText(InCreateData->Action->GetTooltipDescription())
		];
}


void FNiagaraMaterialAttributeBindingCustomization::OnMaterialActionSelected(const TArray< TSharedPtr<FEdGraphSchemaAction> >& SelectedActions, ESelectInfo::Type InSelectionType)
{
	if (InSelectionType == ESelectInfo::OnMouseClick || InSelectionType == ESelectInfo::OnKeyPress || SelectedActions.Num() == 0)
	{
		for (int32 ActionIndex = 0; ActionIndex < SelectedActions.Num(); ActionIndex++)
		{
			TSharedPtr<FEdGraphSchemaAction> CurrentAction = SelectedActions[ActionIndex];

			if (CurrentAction.IsValid())
			{
				FSlateApplication::Get().DismissAllMenus();
				FNiagaraStackAssetAction_VarBind* EventSourceAction = (FNiagaraStackAssetAction_VarBind*)CurrentAction.Get();
				ChangeMaterialSource(EventSourceAction->VarName);
			}
		}
	}
}

void FNiagaraMaterialAttributeBindingCustomization::ChangeMaterialSource(FName InVarName)
{
	FScopedTransaction Transaction(FText::Format(LOCTEXT("ChangeParameterSource", " Change Parameter Source to \"{0}\" "), FText::FromName(InVarName)));
	TArray<UObject*> Objects;
	PropertyHandle->GetOuterObjects(Objects);
	for (UObject* Obj : Objects)
	{
		Obj->Modify();
	}

	PropertyHandle->NotifyPreChange();
	TargetParameterBinding->MaterialParameterName = InVarName;
	//TargetParameterBinding->Parameter.SetType(FMaterialTypeDefinition::GetUObjectDef()); Do not override the type here!
	//TargetVariableBinding->DataSetVariable = FMaterialConstants::GetAttributeAsDataSetKey(TargetVariableBinding->BoundVariable);
	PropertyHandle->NotifyPostChange();
	PropertyHandle->NotifyFinishedChangingProperties();
}

void FNiagaraMaterialAttributeBindingCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> InPropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	PropertyHandle = InPropertyHandle;
	bool bAddDefault = true;
	

	if (bAddDefault)
	{
		HeaderRow
			.NameContent()
			[
				PropertyHandle->CreatePropertyNameWidget()
			]
		.ValueContent()
			.MaxDesiredWidth(200.f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("ParamHeaderValue", "Binding"))
				.Font(IDetailLayoutBuilder::GetDetailFont())
			];
	}
}


void FNiagaraMaterialAttributeBindingCustomization::CustomizeChildren(TSharedRef<IPropertyHandle> StructPropertyHandle, class IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& StructCustomizationUtils)
{
	RenderProps = nullptr;
	BaseSystem =  nullptr;
	BaseEmitter = nullptr;
	  
	TArray<UObject*> Objects;
	PropertyHandle->GetOuterObjects(Objects);
	if (Objects.Num() == 1)
	{
		RenderProps = Cast<UNiagaraRendererProperties>(Objects[0]);
		BaseSystem = Objects[0]->GetTypedOuter<UNiagaraSystem>();
		BaseEmitter = Objects[0]->GetTypedOuter<UNiagaraEmitter>();
		if (BaseSystem)
		{
			TargetParameterBinding = (FNiagaraMaterialAttributeBinding*)PropertyHandle->GetValueBaseAddress((uint8*)Objects[0]);

			TSharedPtr<IPropertyHandle> ChildPropertyHandle = StructPropertyHandle->GetChildHandle(0);
			FDetailWidgetRow& RowMaterial = ChildBuilder.AddCustomRow(FText::GetEmpty());
			RowMaterial
				.NameContent()
				[
					ChildPropertyHandle->CreatePropertyNameWidget()
				]
			.ValueContent()
				.MaxDesiredWidth(200.f)
				[
					SNew(SComboButton)
					.OnGetMenuContent(this, &FNiagaraMaterialAttributeBindingCustomization::OnGetMaterialMenuContent)
					.ContentPadding(1)
					.ToolTipText(this, &FNiagaraMaterialAttributeBindingCustomization::GetMaterialTooltipText)
					.ButtonStyle(FEditorStyle::Get(), "PropertyEditor.AssetComboStyle")
					.ForegroundColor(FEditorStyle::GetColor("PropertyEditor.AssetName.ColorAndOpacity"))
					.ButtonContent()
					[
						SNew(STextBlock)
						.Text(this, &FNiagaraMaterialAttributeBindingCustomization::GetMaterialCurrentText)
						.Font(IDetailLayoutBuilder::GetDetailFont())
					]
				];

			ChildPropertyHandle = StructPropertyHandle->GetChildHandle(1);
			FDetailWidgetRow& RowNiagara = ChildBuilder.AddCustomRow(FText::GetEmpty());
			RowNiagara
				.NameContent()
				[
					ChildPropertyHandle->CreatePropertyNameWidget()
				]
			.ValueContent()
				.MaxDesiredWidth(200.f)
				[
					SNew(SComboButton)
					.OnGetMenuContent(this, &FNiagaraMaterialAttributeBindingCustomization::OnGetNiagaraMenuContent)
					.ContentPadding(1)
					.ToolTipText(this, &FNiagaraMaterialAttributeBindingCustomization::GetNiagaraTooltipText)
					.ButtonStyle(FEditorStyle::Get(), "PropertyEditor.AssetComboStyle")
					.ForegroundColor(FEditorStyle::GetColor("PropertyEditor.AssetName.ColorAndOpacity"))
					.ButtonContent()
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.VAlign(VAlign_Center)
						.Padding(5, 0)
						[
							SNew(SNiagaraParameterName)
							.ParameterName(this, &FNiagaraMaterialAttributeBindingCustomization::GetNiagaraVariableName)
							.IsReadOnly(true)
						]
						+ SHorizontalBox::Slot()
						.VAlign(VAlign_Center)
						.Padding(5, 0)
						[
							SNew(STextBlock)
							.Visibility(this, &FNiagaraMaterialAttributeBindingCustomization::GetNiagaraChildVariableVisibility)
							.Text(this, &FNiagaraMaterialAttributeBindingCustomization::GetNiagaraChildVariableText)
						]
					]
				];
		}
	}
}

//////////////////////////////////////////////////////////////////////////

FName FNiagaraDataInterfaceBindingCustomization::GetVariableName() const
{
	if (BaseStage && TargetDataInterfaceBinding)
	{
		return (TargetDataInterfaceBinding->BoundVariable.GetName());
	}
	return FName();
}

FText FNiagaraDataInterfaceBindingCustomization::GetCurrentText() const
{
	if (BaseStage && TargetDataInterfaceBinding)
	{
		return FText::FromName(TargetDataInterfaceBinding->BoundVariable.GetName());
	}
	return FText::FromString(TEXT("Missing"));
}

FText FNiagaraDataInterfaceBindingCustomization::GetTooltipText() const
{
	if (BaseStage && TargetDataInterfaceBinding && TargetDataInterfaceBinding->BoundVariable.IsValid())
	{
		FText TooltipDesc = FText::Format(LOCTEXT("DataInterfaceBindingTooltip", "Bound to the user parameter \"{0}\""), FText::FromName(TargetDataInterfaceBinding->BoundVariable.GetName()));
		return TooltipDesc;
	}
	return FText::FromString(TEXT("Missing"));
}

TSharedRef<SWidget> FNiagaraDataInterfaceBindingCustomization::OnGetMenuContent() const
{
	FGraphActionMenuBuilder MenuBuilder;

	return SNew(SBorder)
		.BorderImage(FEditorStyle::GetBrush("Menu.Background"))
		.Padding(5)
		[
			SNew(SBox)
			[
				SNew(SGraphActionMenu)
				.OnActionSelected(const_cast<FNiagaraDataInterfaceBindingCustomization*>(this), &FNiagaraDataInterfaceBindingCustomization::OnActionSelected)
		.OnCreateWidgetForAction(SGraphActionMenu::FOnCreateWidgetForAction::CreateSP(const_cast<FNiagaraDataInterfaceBindingCustomization*>(this), &FNiagaraDataInterfaceBindingCustomization::OnCreateWidgetForAction))
		.OnCollectAllActions(const_cast<FNiagaraDataInterfaceBindingCustomization*>(this), &FNiagaraDataInterfaceBindingCustomization::CollectAllActions)
		.AutoExpandActionMenu(false)
		.ShowFilterTextBox(true)
			]
		];
}

TArray<FName> FNiagaraDataInterfaceBindingCustomization::GetNames() const
{
	TArray<FName> Names;

	if (BaseStage && TargetDataInterfaceBinding)
	{
		UNiagaraEmitter* Emitter = BaseStage->GetTypedOuter<UNiagaraEmitter>();

		if (Emitter)
		{
			// Find all used emitter and particle data interface variables that can be iterated upon.
			TArray<UNiagaraScript*> AllScripts;
			Emitter->GetScripts(AllScripts, false);

			TArray<UNiagaraGraph*> Graphs;
			for (const UNiagaraScript* Script : AllScripts)
			{
				const UNiagaraScriptSource* Source = Cast<UNiagaraScriptSource>(Script->GetSource());
				if (Source)
				{
					Graphs.AddUnique(Source->NodeGraph);
				}
			}

			for (const UNiagaraGraph* Graph : Graphs)
			{
				const TMap<FNiagaraVariable, FNiagaraGraphParameterReferenceCollection>& ParameterReferenceMap = Graph->GetParameterReferenceMap();
				for (const auto& ParameterToReferences : ParameterReferenceMap)
				{
					const FNiagaraVariable& ParameterVariable = ParameterToReferences.Key;
					if (ParameterVariable.IsDataInterface())
					{
						const UClass* Class = ParameterVariable.GetType().GetClass();
						if (Class)
						{
							const UObject* DefaultObjDI = Class->GetDefaultObject();
							if (DefaultObjDI != nullptr && DefaultObjDI->IsA<UNiagaraDataInterfaceRWBase>())
							{
								Names.AddUnique(ParameterVariable.GetName());
							}
						}
					}
				}
			}
		}
	}

	return Names;
}

void FNiagaraDataInterfaceBindingCustomization::CollectAllActions(FGraphActionListBuilderBase& OutAllActions)
{
	TArray<FName> UserParamNames = GetNames();
	for (FName UserParamName : UserParamNames)
	{
		FText CategoryName = FText();
		FString DisplayNameString = FName::NameToDisplayString(UserParamName.ToString(), false);
		const FText NameText = FText::FromString(DisplayNameString);
		const FText TooltipDesc = FText::Format(LOCTEXT("BindToDataInterface", "Bind to the User Parameter \"{0}\" "), FText::FromString(DisplayNameString));
		TSharedPtr<FNiagaraStackAssetAction_VarBind> NewNodeAction(new FNiagaraStackAssetAction_VarBind(UserParamName, CategoryName, NameText,
			TooltipDesc, 0, FText()));
		OutAllActions.AddAction(NewNodeAction);
	}
}

TSharedRef<SWidget> FNiagaraDataInterfaceBindingCustomization::OnCreateWidgetForAction(struct FCreateWidgetForActionData* const InCreateData)
{
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SNiagaraParameterName)
			.ParameterName(((FNiagaraStackAssetAction_VarBind* const)InCreateData->Action.Get())->VarName)
			.IsReadOnly(true)
			//SNew(STextBlock)
			//.Text(InCreateData->Action->GetMenuDescription())
			.ToolTipText(InCreateData->Action->GetTooltipDescription())
		];
}


void FNiagaraDataInterfaceBindingCustomization::OnActionSelected(const TArray< TSharedPtr<FEdGraphSchemaAction> >& SelectedActions, ESelectInfo::Type InSelectionType)
{
	if (InSelectionType == ESelectInfo::OnMouseClick || InSelectionType == ESelectInfo::OnKeyPress || SelectedActions.Num() == 0)
	{
		for (int32 ActionIndex = 0; ActionIndex < SelectedActions.Num(); ActionIndex++)
		{
			TSharedPtr<FEdGraphSchemaAction> CurrentAction = SelectedActions[ActionIndex];

			if (CurrentAction.IsValid())
			{
				FSlateApplication::Get().DismissAllMenus();
				FNiagaraStackAssetAction_VarBind* EventSourceAction = (FNiagaraStackAssetAction_VarBind*)CurrentAction.Get();
				ChangeSource(EventSourceAction->VarName);
			}
		}
	}
}

void FNiagaraDataInterfaceBindingCustomization::ChangeSource(FName InVarName)
{
	FScopedTransaction Transaction(FText::Format(LOCTEXT("ChangeDataParameterSource", " Change Data Interface Source to \"{0}\" "), FText::FromName(InVarName)));
	TArray<UObject*> Objects;
	PropertyHandle->GetOuterObjects(Objects);
	for (UObject* Obj : Objects)
	{
		Obj->Modify();
	}

	PropertyHandle->NotifyPreChange();
	TargetDataInterfaceBinding->BoundVariable.SetName(InVarName);
	PropertyHandle->NotifyPostChange();
	PropertyHandle->NotifyFinishedChangingProperties();
}

void FNiagaraDataInterfaceBindingCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> InPropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	PropertyHandle = InPropertyHandle;
	TArray<UObject*> Objects;
	PropertyHandle->GetOuterObjects(Objects);
	bool bAddDefault = true;
	if (Objects.Num() == 1)
	{
		BaseStage = Cast<UNiagaraSimulationStageBase>(Objects[0]);
		if (BaseStage)
		{
			TargetDataInterfaceBinding = (FNiagaraVariableDataInterfaceBinding*)PropertyHandle->GetValueBaseAddress((uint8*)Objects[0]);

			HeaderRow
				.NameContent()
				[
					PropertyHandle->CreatePropertyNameWidget()
				]
			.ValueContent()
				.MaxDesiredWidth(200.f)
				[
					SNew(SComboButton)
					.OnGetMenuContent(this, &FNiagaraDataInterfaceBindingCustomization::OnGetMenuContent)
					.ContentPadding(1)
					.ToolTipText(this, &FNiagaraDataInterfaceBindingCustomization::GetTooltipText)
					.ButtonStyle(FEditorStyle::Get(), "PropertyEditor.AssetComboStyle")
					.ForegroundColor(FEditorStyle::GetColor("PropertyEditor.AssetName.ColorAndOpacity"))
					.ButtonContent()
				[
					SNew(SNiagaraParameterName)
					.ParameterName(this, &FNiagaraDataInterfaceBindingCustomization::GetVariableName)
					.IsReadOnly(true)
				]
				];
			bAddDefault = false;
		}
	}


	if (bAddDefault)
	{
		HeaderRow
			.NameContent()
			[
				PropertyHandle->CreatePropertyNameWidget()
			]
			.ValueContent()
			.MaxDesiredWidth(200.f)
			[
				SNew(STextBlock)
				.Text(FText::FromString(FName::NameToDisplayString(CastField<FStructProperty>(PropertyHandle->GetProperty())->Struct->GetName(), false)))
				.Font(IDetailLayoutBuilder::GetDetailFont())
			];
	}
}

////////////////////////
FName FNiagaraScriptVariableBindingCustomization::GetVariableName() const
{
	if (BaseGraph && TargetVariableBinding && TargetVariableBinding->IsValid())
	{
		return (TargetVariableBinding->Name);
	}
	return FName();
}

FText FNiagaraScriptVariableBindingCustomization::GetCurrentText() const
{
	if (BaseGraph && TargetVariableBinding && TargetVariableBinding->IsValid())
	{
		return FText::FromName(TargetVariableBinding->Name);
	}
	return FText::FromString(TEXT("None"));
}

FText FNiagaraScriptVariableBindingCustomization::GetTooltipText() const
{
	if (BaseGraph && TargetVariableBinding && TargetVariableBinding->IsValid())
	{
		FText TooltipDesc = FText::Format(LOCTEXT("BindingTooltip", "Use the variable \"{0}\" if it is defined, otherwise use the type's default value."), FText::FromName(TargetVariableBinding->Name));
		return TooltipDesc;
	}
	return FText::FromString(TEXT("There is no default binding selected."));
}

TSharedRef<SWidget> FNiagaraScriptVariableBindingCustomization::OnGetMenuContent() const
{
	FGraphActionMenuBuilder MenuBuilder; // TODO: Is this necessary? It's included in all the other implementations above, but it's never used. Spooky

	return SNew(SBorder)
		.BorderImage(FEditorStyle::GetBrush("Menu.Background"))
		.Padding(5)
		[
			SNew(SBox)
			[
				SNew(SGraphActionMenu)
				.OnActionSelected(const_cast<FNiagaraScriptVariableBindingCustomization*>(this), &FNiagaraScriptVariableBindingCustomization::OnActionSelected)
				.OnCreateWidgetForAction(SGraphActionMenu::FOnCreateWidgetForAction::CreateSP(const_cast<FNiagaraScriptVariableBindingCustomization*>(this), &FNiagaraScriptVariableBindingCustomization::OnCreateWidgetForAction))
				.OnCollectAllActions(const_cast<FNiagaraScriptVariableBindingCustomization*>(this), &FNiagaraScriptVariableBindingCustomization::CollectAllActions)
				.AutoExpandActionMenu(false)
				.ShowFilterTextBox(true)
			]
		];
}

TArray<FName> FNiagaraScriptVariableBindingCustomization::GetNames() const
{
	// TODO: Only show Particles attributes for valid graphs,
	//       i.e. only show Particles attributes for Particle scripts
	//       and only show Emitter attributes for Emitter and Particle scripts.
	TArray<FName> Names;

	for (const FNiagaraParameterMapHistory& History : UNiagaraNodeParameterMapBase::GetParameterMaps(BaseGraph))
	{
		for (const FNiagaraVariable& Var : History.Variables)
		{
			FString Namespace = FNiagaraParameterMapHistory::GetNamespace(Var);
			if (Namespace == TEXT("Module."))
			{
				// TODO: Skip module inputs for now. Does it make sense to bind module inputs to module inputs?
				continue;
			}
			if (Var.GetType() == BaseScriptVariable->Variable.GetType())
			{
				Names.AddUnique(Var.GetName());
			}
		}
	}

	for (const auto& Var : BaseGraph->GetParameterReferenceMap())
	{
		FString Namespace = FNiagaraParameterMapHistory::GetNamespace(Var.Key);
		if (Namespace == TEXT("Module."))
		{
			// TODO: Skip module inputs for now. Does it make sense to bind module inputs to module inputs?
			continue;
		}
		if (Var.Key.GetType() == BaseScriptVariable->Variable.GetType())
		{
			Names.AddUnique(Var.Key.GetName());
		}
	}

	for (const FNiagaraVariable& Var : FNiagaraConstants::GetEngineConstants())
	{
		if (Var.GetType() == BaseScriptVariable->Variable.GetType())
		{
			Names.AddUnique(Var.GetName());
		}
	}

	for (const FNiagaraVariable& Var : FNiagaraConstants::GetCommonParticleAttributes())
	{
		if (Var.GetType() == BaseScriptVariable->Variable.GetType())
		{
			Names.AddUnique(Var.GetName());
		}
	}

	return Names;
}

void FNiagaraScriptVariableBindingCustomization::CollectAllActions(FGraphActionListBuilderBase& OutAllActions)
{
	if (BaseGraph)
	{
		for (FName Name : GetNames())
		{
			const FText NameText = FText::FromName(Name);
			const FText TooltipDesc = FText::Format(LOCTEXT("SetFunctionPopupTooltip", "Use the variable \"{0}\" "), NameText);
			TSharedPtr<FNiagaraStackAssetAction_VarBind> NewNodeAction(
				new FNiagaraStackAssetAction_VarBind(Name, FText(), NameText, TooltipDesc, 0, FText())
			);
			OutAllActions.AddAction(NewNodeAction);
		}
	}
}

TSharedRef<SWidget> FNiagaraScriptVariableBindingCustomization::OnCreateWidgetForAction(struct FCreateWidgetForActionData* const InCreateData)
{
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SNiagaraParameterName)
			.ParameterName(((FNiagaraStackAssetAction_VarBind* const)InCreateData->Action.Get())->VarName)
			.IsReadOnly(true)
			//SNew(STextBlock)
			//.Text(InCreateData->Action->GetMenuDescription())
			.ToolTipText(InCreateData->Action->GetTooltipDescription())
		];
}

void FNiagaraScriptVariableBindingCustomization::OnActionSelected(const TArray< TSharedPtr<FEdGraphSchemaAction> >& SelectedActions, ESelectInfo::Type InSelectionType)
{
	if (InSelectionType == ESelectInfo::OnMouseClick || InSelectionType == ESelectInfo::OnKeyPress || SelectedActions.Num() == 0)
	{
		for (auto& CurrentAction : SelectedActions)
		{
			if (CurrentAction.IsValid())
			{
				FSlateApplication::Get().DismissAllMenus();
				FNiagaraStackAssetAction_VarBind* EventSourceAction = (FNiagaraStackAssetAction_VarBind*)CurrentAction.Get();
				ChangeSource(EventSourceAction->VarName);
			}
		}
	}
}

void FNiagaraScriptVariableBindingCustomization::ChangeSource(FName InVarName)
{
	FScopedTransaction Transaction(FText::Format(LOCTEXT("ChangeBinding", " Change default binding to \"{0}\" "), FText::FromName(InVarName)));
	TArray<UObject*> Objects;
	PropertyHandle->GetOuterObjects(Objects);
	for (UObject* Obj : Objects)
	{
		Obj->Modify();
	}

	PropertyHandle->NotifyPreChange();
	TargetVariableBinding->Name = InVarName;
	PropertyHandle->NotifyPostChange();
	PropertyHandle->NotifyFinishedChangingProperties();
}

void FNiagaraScriptVariableBindingCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> InPropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	PropertyHandle = InPropertyHandle;
	TArray<UObject*> Objects;
	PropertyHandle->GetOuterObjects(Objects);
	bool bAddDefault = true;
	if (Objects.Num() == 1)
	{
		BaseScriptVariable = Cast<UNiagaraScriptVariable>(Objects[0]);
		if (BaseScriptVariable)
		{
		    BaseGraph = Cast<UNiagaraGraph>(BaseScriptVariable->GetOuter());
			if (BaseGraph)
			{
				TargetVariableBinding = (FNiagaraScriptVariableBinding*)PropertyHandle->GetValueBaseAddress((uint8*)Objects[0]);

				HeaderRow
					.NameContent()
					[
						PropertyHandle->CreatePropertyNameWidget()
					]
				.ValueContent()
					.MaxDesiredWidth(200.f)
					[
						SNew(SComboButton)
						.OnGetMenuContent(this, &FNiagaraScriptVariableBindingCustomization::OnGetMenuContent)
						.ContentPadding(1)
						.ToolTipText(this, &FNiagaraScriptVariableBindingCustomization::GetTooltipText)
						.ButtonStyle(FEditorStyle::Get(), "PropertyEditor.AssetComboStyle")
						.ForegroundColor(FEditorStyle::GetColor("PropertyEditor.AssetName.ColorAndOpacity"))
						.ButtonContent()
						[
							SNew(SNiagaraParameterName)
							.ParameterName(this, &FNiagaraScriptVariableBindingCustomization::GetVariableName)
							.IsReadOnly(true)
						]
					];
				bAddDefault = false;
			}
			else
			{
				BaseScriptVariable = nullptr;
			}
		}
		else
		{
			BaseGraph = nullptr;
		}
	}
	
	if (bAddDefault)
	{
		HeaderRow
			.NameContent()
			[
				PropertyHandle->CreatePropertyNameWidget()
			]
		.ValueContent()
			.MaxDesiredWidth(200.f)
			[
				SNew(STextBlock)
				.Text(FText::FromString(FName::NameToDisplayString(CastField<FStructProperty>(PropertyHandle->GetProperty())->Struct->GetName(), false)))
				.Font(IDetailLayoutBuilder::GetDetailFont())
			];
	}
}


#undef LOCTEXT_NAMESPACE
