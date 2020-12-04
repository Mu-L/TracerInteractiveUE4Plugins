// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IDetailCustomization.h"
#include "IPropertyTypeCustomization.h"
#include "NiagaraCommon.h"
#include "EdGraph/EdGraphSchema.h"
#include "Layout/Visibility.h"

#include "NiagaraTypeCustomizations.generated.h"

class FDetailWidgetRow;
class FNiagaraScriptViewModel;
class IPropertyHandle;
class IPropertyHandleArray;

enum class ECheckBoxState : uint8;

class FNiagaraNumericCustomization : public IPropertyTypeCustomization
{
public:
	static TSharedRef<IPropertyTypeCustomization> MakeInstance()
	{
		return MakeShared<FNiagaraNumericCustomization>();
	}

	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils);

	virtual void CustomizeChildren( TSharedRef<IPropertyHandle> PropertyHandle, IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils )
	{}

};


class FNiagaraBoolCustomization : public IPropertyTypeCustomization
{
public:
	static TSharedRef<IPropertyTypeCustomization> MakeInstance()
	{
		return MakeShared<FNiagaraBoolCustomization>();
	}

	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils);

	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> PropertyHandle, IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils)
	{}

private:

	ECheckBoxState OnGetCheckState() const;

	void OnCheckStateChanged(ECheckBoxState InNewState);


private:
	TSharedPtr<IPropertyHandle> ValueHandle;
};

class FNiagaraMatrixCustomization : public FNiagaraNumericCustomization
{
public:
	static TSharedRef<IPropertyTypeCustomization> MakeInstance()
	{
		return MakeShared<FNiagaraMatrixCustomization>();
	}

	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> PropertyHandle, IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils);


private:
};


USTRUCT()
struct FNiagaraStackAssetAction_VarBind : public FEdGraphSchemaAction
{
	GENERATED_USTRUCT_BODY();

	FName VarName;
	FNiagaraVariableBase BaseVar;
	FNiagaraVariableBase ChildVar;

	// Simple type info
	static FName StaticGetTypeId() { static FName Type("FNiagaraStackAssetAction_VarBind"); return Type; }
	virtual FName GetTypeId() const override { return StaticGetTypeId(); }

	FNiagaraStackAssetAction_VarBind()
		: FEdGraphSchemaAction()
	{}

	FNiagaraStackAssetAction_VarBind(FName InVarName,
		FText InNodeCategory, FText InMenuDesc, FText InToolTip, const int32 InGrouping, FText InKeywords)
		: FEdGraphSchemaAction(MoveTemp(InNodeCategory), MoveTemp(InMenuDesc), MoveTemp(InToolTip), InGrouping, MoveTemp(InKeywords)),
		VarName(InVarName)
	{}

	//~ Begin FEdGraphSchemaAction Interface
	virtual UEdGraphNode* PerformAction(class UEdGraph* ParentGraph, UEdGraphPin* FromPin, const FVector2D Location, bool bSelectNewNode = true) override
	{
		return nullptr;
	}
	//~ End FEdGraphSchemaAction Interface

	static TArray<FNiagaraVariableBase> FindVariables(UNiagaraEmitter* InEmitter, bool bSystem, bool bEmitter, bool bParticles, bool bUser);
};

class FNiagaraVariableAttributeBindingCustomization : public IPropertyTypeCustomization
{
public:
	/** @return A new instance of this class */
	static TSharedRef<IPropertyTypeCustomization> MakeInstance()
	{
		return MakeShared<FNiagaraVariableAttributeBindingCustomization>();
	}

	/** IPropertyTypeCustomization interface begin */
	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> StructPropertyHandle, class FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& StructCustomizationUtils) override;
	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> StructPropertyHandle, class IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& StructCustomizationUtils) override {};
	/** IPropertyTypeCustomization interface end */
private:
	EVisibility IsResetToDefaultsVisible() const;
	FReply OnResetToDefaultsClicked();
	void ResetToDefault();
	FName GetVariableName() const;
	FText GetCurrentText() const;
	FText GetTooltipText() const;
	TSharedRef<SWidget> OnGetMenuContent() const;
	TArray<FName> GetNames(class UNiagaraEmitter* InEmitter) const;

	void ChangeSource(FName InVarName);
	void CollectAllActions(FGraphActionListBuilderBase& OutAllActions);
	TSharedRef<SWidget> OnCreateWidgetForAction(struct FCreateWidgetForActionData* const InCreateData);
	void OnActionSelected(const TArray< TSharedPtr<FEdGraphSchemaAction> >& SelectedActions, ESelectInfo::Type InSelectionType);

	TSharedPtr<IPropertyHandle> PropertyHandle;
	class UNiagaraEmitter* BaseEmitter;
	class UNiagaraRendererProperties* RenderProps;
	struct FNiagaraVariableAttributeBinding* TargetVariableBinding;
	const struct FNiagaraVariableAttributeBinding* DefaultVariableBinding;
};

class FNiagaraUserParameterBindingCustomization : public IPropertyTypeCustomization
{
public:
	/** @return A new instance of this class */
	static TSharedRef<IPropertyTypeCustomization> MakeInstance()
	{
		return MakeShared<FNiagaraUserParameterBindingCustomization>();
	}

	/** IPropertyTypeCustomization interface begin */
	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> StructPropertyHandle, class FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& StructCustomizationUtils) override;
	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> StructPropertyHandle, class IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& StructCustomizationUtils) override {};
	/** IPropertyTypeCustomization interface end */
private:
	FText GetCurrentText() const;
	FText GetTooltipText() const;
	FName GetVariableName() const;
	TSharedRef<SWidget> OnGetMenuContent() const;
	TArray<FName> GetNames() const;
	void ChangeSource(FName InVarName);
	void CollectAllActions(FGraphActionListBuilderBase& OutAllActions);
	TSharedRef<SWidget> OnCreateWidgetForAction(struct FCreateWidgetForActionData* const InCreateData);
	void OnActionSelected(const TArray< TSharedPtr<FEdGraphSchemaAction> >& SelectedActions, ESelectInfo::Type InSelectionType);

	TSharedPtr<IPropertyHandle> PropertyHandle;
	class UNiagaraSystem* BaseSystem;
	struct FNiagaraUserParameterBinding* TargetUserParameterBinding;

};

class FNiagaraMaterialAttributeBindingCustomization : public IPropertyTypeCustomization
{
public:
	/** @return A new instance of this class */
	static TSharedRef<IPropertyTypeCustomization> MakeInstance()
	{
		return MakeShared<FNiagaraMaterialAttributeBindingCustomization>();
	}

	/** IPropertyTypeCustomization interface begin */
	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> StructPropertyHandle, class FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& StructCustomizationUtils) override;
	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> StructPropertyHandle, class IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& StructCustomizationUtils) override ;
	/** IPropertyTypeCustomization interface end */
private:
	FText GetNiagaraCurrentText() const;
	FText GetNiagaraTooltipText() const;
	FName GetNiagaraVariableName() const;
	FText GetNiagaraChildVariableText() const;
	FName GetNiagaraChildVariableName() const; 
	EVisibility GetNiagaraChildVariableVisibility() const;

	TSharedRef<SWidget> OnGetNiagaraMenuContent() const;
	TArray<TPair<FNiagaraVariableBase, FNiagaraVariableBase>> GetNiagaraNames() const;
	void ChangeNiagaraSource(FNiagaraStackAssetAction_VarBind* InVar);
	void CollectAllNiagaraActions(FGraphActionListBuilderBase& OutAllActions);
	TSharedRef<SWidget> OnCreateWidgetForNiagaraAction(struct FCreateWidgetForActionData* const InCreateData);
	void OnNiagaraActionSelected(const TArray< TSharedPtr<FEdGraphSchemaAction> >& SelectedActions, ESelectInfo::Type InSelectionType);

	FText GetMaterialCurrentText() const;
	FText GetMaterialTooltipText() const;
	TSharedRef<SWidget> OnGetMaterialMenuContent() const;
	TArray<FName> GetMaterialNames() const;
	void ChangeMaterialSource(FName InVarName);
	void CollectAllMaterialActions(FGraphActionListBuilderBase& OutAllActions);
	TSharedRef<SWidget> OnCreateWidgetForMaterialAction(struct FCreateWidgetForActionData* const InCreateData);
	void OnMaterialActionSelected(const TArray< TSharedPtr<FEdGraphSchemaAction> >& SelectedActions, ESelectInfo::Type InSelectionType);

	bool IsCompatibleNiagaraVariable(const struct FNiagaraVariable& InVar) const;
	static FText MakeCurrentText(const FNiagaraVariableBase& BaseVar, const FNiagaraVariableBase& ChildVar);

	TSharedPtr<IPropertyHandle> PropertyHandle;
	class UNiagaraSystem* BaseSystem;
	class UNiagaraEmitter* BaseEmitter;
	class UNiagaraRendererProperties* RenderProps;
	struct FNiagaraMaterialAttributeBinding* TargetParameterBinding;

};

class FNiagaraDataInterfaceBindingCustomization : public IPropertyTypeCustomization
{
public:
	/** @return A new instance of this class */
	static TSharedRef<IPropertyTypeCustomization> MakeInstance()
	{
		return MakeShared<FNiagaraDataInterfaceBindingCustomization>();
	}

	/** IPropertyTypeCustomization interface begin */
	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> StructPropertyHandle, class FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& StructCustomizationUtils) override;
	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> StructPropertyHandle, class IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& StructCustomizationUtils) override {};
	/** IPropertyTypeCustomization interface end */
private:
	FText GetCurrentText() const;
	FText GetTooltipText() const;
	FName GetVariableName() const;
	TSharedRef<SWidget> OnGetMenuContent() const;
	TArray<FName> GetNames() const;
	void ChangeSource(FName InVarName);
	void CollectAllActions(FGraphActionListBuilderBase& OutAllActions);
	TSharedRef<SWidget> OnCreateWidgetForAction(struct FCreateWidgetForActionData* const InCreateData);
	void OnActionSelected(const TArray< TSharedPtr<FEdGraphSchemaAction> >& SelectedActions, ESelectInfo::Type InSelectionType);

	TSharedPtr<IPropertyHandle> PropertyHandle;
	class UNiagaraSimulationStageBase* BaseStage;
	struct FNiagaraVariableDataInterfaceBinding* TargetDataInterfaceBinding;

};


/** The primary goal of this class is to search through type matched and defined Niagara variables 
    in the UNiagaraScriptVariable customization panel to provide a default binding for module inputs. */
class FNiagaraScriptVariableBindingCustomization : public IPropertyTypeCustomization
{
public:
	static TSharedRef<IPropertyTypeCustomization> MakeInstance()
	{
		return MakeShared<FNiagaraScriptVariableBindingCustomization>();
	}

	/** IPropertyTypeCustomization interface begin */
	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> StructPropertyHandle, class FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& StructCustomizationUtils) override;
	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> StructPropertyHandle, class IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& StructCustomizationUtils) override {};
	/** IPropertyTypeCustomization interface end */
private:
   /** Helpers */
	FName GetVariableName() const;
	FText GetCurrentText() const;
	FText GetTooltipText() const;
	TSharedRef<SWidget> OnGetMenuContent() const;
	TArray<FName> GetNames() const;
	void ChangeSource(FName InVarName);
	void CollectAllActions(FGraphActionListBuilderBase& OutAllActions);
	TSharedRef<SWidget> OnCreateWidgetForAction(struct FCreateWidgetForActionData* const InCreateData);
	void OnActionSelected(const TArray< TSharedPtr<FEdGraphSchemaAction> >& SelectedActions, ESelectInfo::Type InSelectionType);

	/** State */
	TSharedPtr<IPropertyHandle> PropertyHandle;
	class UNiagaraGraph* BaseGraph;
	class UNiagaraScriptVariable* BaseScriptVariable;
	struct FNiagaraScriptVariableBinding* TargetVariableBinding;
};
