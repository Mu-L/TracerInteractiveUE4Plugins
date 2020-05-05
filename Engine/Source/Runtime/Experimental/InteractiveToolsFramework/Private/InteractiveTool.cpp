// Copyright Epic Games, Inc. All Rights Reserved.


#include "InteractiveTool.h"
#include "InteractiveToolManager.h"
#include "UObject/Class.h"


#define LOCTEXT_NAMESPACE "UInteractiveTool"


UInteractiveTool::UInteractiveTool()
{
	// tools need to be transactional or undo/redo won't work on their uproperties
	SetFlags(RF_Transactional);

	// tools don't get saved but this isn't necessary because they are created in the transient package...
	//SetFlags(RF_Transient);

	InputBehaviors = NewObject<UInputBehaviorSet>(this, TEXT("InputBehaviors"));

	// initialize ToolInfo
#if WITH_EDITORONLY_DATA
	DefaultToolInfo.ToolDisplayName = GetClass()->GetDisplayNameText();
#else
	DefaultToolInfo.ToolDisplayName = FText(LOCTEXT("DefaultInteractiveToolName", "DefaultToolName"));
#endif
}

void UInteractiveTool::Setup()
{
}

void UInteractiveTool::Shutdown(EToolShutdownType ShutdownType)
{
	InputBehaviors->RemoveAll();
	ToolPropertyObjects.Reset();
}

void UInteractiveTool::Render(IToolsContextRenderAPI* RenderAPI)
{
}


void UInteractiveTool::AddInputBehavior(UInputBehavior* Behavior)
{
	InputBehaviors->Add(Behavior);
}

const UInputBehaviorSet* UInteractiveTool::GetInputBehaviors() const
{
	return InputBehaviors;
}


void UInteractiveTool::AddToolPropertySource(UObject* PropertyObject)
{
	check(ToolPropertyObjects.Contains(PropertyObject) == false);
	ToolPropertyObjects.Add(PropertyObject);

	OnPropertySetsModified.Broadcast();
}

void UInteractiveTool::AddToolPropertySource(UInteractiveToolPropertySet* PropertySet)
{
	check(ToolPropertyObjects.Contains(PropertySet) == false);
	ToolPropertyObjects.Add(PropertySet);
	// @todo do we need to create a lambda every time for this?
	PropertySet->GetOnModified().AddLambda([this](UObject* PropertySetArg, FProperty* PropertyArg)
	{
		OnPropertyModified(PropertySetArg, PropertyArg);
	});

	OnPropertySetsModified.Broadcast();
}

bool UInteractiveTool::RemoveToolPropertySource(UInteractiveToolPropertySet* PropertySet)
{
	int32 NumRemoved = ToolPropertyObjects.Remove(PropertySet);
	if (NumRemoved == 0)
	{
		return false;
	}

	PropertySet->GetOnModified().Clear();
	OnPropertySetsModified.Broadcast();
	return true;
}

bool UInteractiveTool::ReplaceToolPropertySource(UInteractiveToolPropertySet* CurPropertySet, UInteractiveToolPropertySet* ReplaceWith, bool bSetToEnabled)
{
	int32 Index = ToolPropertyObjects.Find(CurPropertySet);
	if (Index == INDEX_NONE)
	{
		return false;
	}
	CurPropertySet->GetOnModified().Clear();

	ReplaceWith->GetOnModified().AddLambda([this](UObject* PropertySetArg, FProperty* PropertyArg)
	{
		OnPropertyModified(PropertySetArg, PropertyArg);
	});

	ToolPropertyObjects[Index] = ReplaceWith;

	if (bSetToEnabled)
	{
		ReplaceWith->bIsPropertySetEnabled = true;
	}

	OnPropertySetsModified.Broadcast();
	return true;
}

bool UInteractiveTool::SetToolPropertySourceEnabled(UInteractiveToolPropertySet* PropertySet, bool bEnabled)
{
	int32 Index = ToolPropertyObjects.Find(PropertySet);
	if (Index == INDEX_NONE)
	{
		return false;
	}
	if (PropertySet->bIsPropertySetEnabled != bEnabled)
	{
		PropertySet->bIsPropertySetEnabled = bEnabled;
		OnPropertySetsModified.Broadcast();
	}
	return true;
}

TArray<UObject*> UInteractiveTool::GetToolProperties(bool bEnabledOnly) const
{
	if (bEnabledOnly == false)
	{
		return ToolPropertyObjects;
	}

	TArray<UObject*> Properties;
	for (UObject* Object : ToolPropertyObjects)
	{
		UInteractiveToolPropertySet* Prop = Cast<UInteractiveToolPropertySet>(Object);
		if (Prop == nullptr || Prop->IsPropertySetEnabled())
		{
			Properties.Add(Object);
		}
	}

	return MoveTemp(Properties);
}


void UInteractiveTool::RegisterActions(FInteractiveToolActionSet& ActionSet)
{
}

FInteractiveToolActionSet* UInteractiveTool::GetActionSet()
{
	if (ToolActionSet == nullptr)
	{
		ToolActionSet = new FInteractiveToolActionSet();
		RegisterActions(*ToolActionSet);
	}
	return ToolActionSet;
}

void UInteractiveTool::ExecuteAction(int32 ActionID)
{
	GetActionSet()->ExecuteAction(ActionID);
}



bool UInteractiveTool::HasCancel() const
{
	return false;
}

bool UInteractiveTool::HasAccept() const
{
	return false;
}

bool UInteractiveTool::CanAccept() const
{
	return false;
}


void UInteractiveTool::Tick(float DeltaTime)
{
}

UInteractiveToolManager* UInteractiveTool::GetToolManager() const
{
	UInteractiveToolManager* ToolManager = Cast<UInteractiveToolManager>(GetOuter());
	check(ToolManager != nullptr);
	return ToolManager;
}


#undef LOCTEXT_NAMESPACE