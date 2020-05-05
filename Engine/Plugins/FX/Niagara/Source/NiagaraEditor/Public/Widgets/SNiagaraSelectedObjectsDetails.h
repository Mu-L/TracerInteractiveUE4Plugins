// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/SCompoundWidget.h"
#include "Delegates/Delegate.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "PropertyEditorDelegates.h"

class FNiagaraObjectSelection;
class IDetailsView;

/** A widget for viewing and editing a set of selected objects with a details panel. */
class SNiagaraSelectedObjectsDetails : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SNiagaraSelectedObjectsDetails)
	{}

	SLATE_END_ARGS();

	NIAGARAEDITOR_API void Construct(const FArguments& InArgs, TSharedRef<FNiagaraObjectSelection> InSelectedObjects);
	NIAGARAEDITOR_API void Construct(const FArguments& InArgs, TSharedRef<FNiagaraObjectSelection> InSelectedObjects, TSharedRef<FNiagaraObjectSelection> InSelectedObjects2);

	/** Delegate to know when one of the properties has been changed.*/
	FOnFinishedChangingProperties& OnFinishedChangingProperties() { return OnFinishedChangingPropertiesDelegate; }

	void SelectedObjectsChanged();
private:
	/** Called whenever the object selection changes. */
	void SelectedObjectsChangedSecond();

	/** Internal delegate to route to third parties. */
	void OnDetailsPanelFinishedChangingProperties(const FPropertyChangedEvent& InEvent);

private:
	/** The selected objects being viewed and edited by this widget. */
	TArray<TSharedPtr<FNiagaraObjectSelection>> SelectedObjectsArray;

	/** The details view for the selected object. */
	TSharedPtr<IDetailsView> DetailsView;

	/** Delegate for third parties to be notified when properties have changed.*/
	FOnFinishedChangingProperties OnFinishedChangingPropertiesDelegate;
};
