// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NiagaraEditorModule.h"
#include "Modules/ModuleInterface.h"
#include "Delegates/IDelegateInstance.h"
#include "UObject/ObjectKey.h"
#include "Templates/SharedPointer.h"

class FNiagaraStackCurveEditorOptions
{
public:
	FNiagaraStackCurveEditorOptions();

	bool GetNeedsInitializeView() const;
	void InitializeView(float InViewMinInput, float InViewMaxInput, float InViewMinOutput, float InViewMaxOutput);

	float GetViewMinInput() const;
	float GetViewMaxInput() const;
	void SetInputViewRange(float InViewMinInput, float InViewMaxInput);

	float GetViewMinOutput() const;
	float GetViewMaxOutput() const;
	void SetOutputViewRange(float InViewMinOutput, float InViewMaxOutput);

	bool GetAreCurvesVisible() const;
	void SetAreCurvesVisible(bool bInAreCurvesVisible);

	float GetTimelineLength() const;

	float GetHeight() const;
	void SetHeight(float InHeight);

private:
	float ViewMinInput;
	float ViewMaxInput;
	float ViewMinOutput;
	float ViewMaxOutput;
	bool bAreCurvesVisible;
	bool bNeedsInitializeView;
	float Height;
};

class UObject;

/** A module containing widgets for editing niagara data. */
class FNiagaraEditorWidgetsModule : public IModuleInterface
{
private:
	class FNiagaraEditorWidgetProvider : public INiagaraEditorWidgetProvider
	{
	public:
		virtual TSharedRef<SWidget> CreateStackView(UNiagaraStackViewModel& StackViewModel) const override;
		virtual TSharedRef<SWidget> CreateSystemOverview(TSharedRef<FNiagaraSystemViewModel> SystemViewModel) const override;
		virtual TSharedRef<SWidget> CreateStackIssueIcon(UNiagaraStackViewModel& StackViewModel, UNiagaraStackEntry& StackEntry) const override;
		virtual TSharedRef<SWidget> CreateScriptScratchPad(UNiagaraScratchPadViewModel& ScriptScratchPadViewModel) const override;
		virtual FLinearColor GetColorForExecutionCategory(FName ExecutionCategory) const override;
		virtual FLinearColor GetColorForParameterScope(ENiagaraParameterScope ParameterScope) const override;
	};

public:
	// IModuleInterface
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	TSharedRef<FNiagaraStackCurveEditorOptions> GetOrCreateStackCurveEditorOptionsForObject(UObject* Object, bool bDefaultAreCurvesVisible, float DefaultHeight);

private:
	void ReinitializeStyle();

private:
	TMap<FObjectKey, TSharedRef<FNiagaraStackCurveEditorOptions>> ObjectToStackCurveEditorOptionsMap;

	TSharedPtr<FNiagaraEditorWidgetProvider> WidgetProvider;

	IConsoleCommand* ReinitializeStyleCommand;
};