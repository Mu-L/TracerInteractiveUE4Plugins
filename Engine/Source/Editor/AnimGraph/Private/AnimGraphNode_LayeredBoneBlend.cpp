// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimGraphNode_LayeredBoneBlend.h"
#include "ToolMenus.h"
#include "Kismet2/BlueprintEditorUtils.h"

#include "GraphEditorActions.h"
#include "ScopedTransaction.h"

/////////////////////////////////////////////////////
// UAnimGraphNode_LayeredBoneBlend

#define LOCTEXT_NAMESPACE "A3Nodes"

UAnimGraphNode_LayeredBoneBlend::UAnimGraphNode_LayeredBoneBlend(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	Node.AddPose();
}

FLinearColor UAnimGraphNode_LayeredBoneBlend::GetNodeTitleColor() const
{
	return FLinearColor(0.2f, 0.8f, 0.2f);
}

FText UAnimGraphNode_LayeredBoneBlend::GetTooltipText() const
{
	return LOCTEXT("AnimGraphNode_LayeredBoneBlend_Tooltip", "Layered blend per bone");
}

FText UAnimGraphNode_LayeredBoneBlend::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	return LOCTEXT("AnimGraphNode_LayeredBoneBlend_Title", "Layered blend per bone");
}

FString UAnimGraphNode_LayeredBoneBlend::GetNodeCategory() const
{
	return TEXT("Blends");
}

void UAnimGraphNode_LayeredBoneBlend::AddPinToBlendByFilter()
{
	FScopedTransaction Transaction( LOCTEXT("AddPinToBlend", "AddPinToBlendByFilter") );
	Modify();

	Node.AddPose();
	ReconstructNode();
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(GetBlueprint());
}

void UAnimGraphNode_LayeredBoneBlend::RemovePinFromBlendByFilter(UEdGraphPin* Pin)
{
	FScopedTransaction Transaction( LOCTEXT("RemovePinFromBlend", "RemovePinFromBlendByFilter") );
	Modify();

	FProperty* AssociatedProperty;
	int32 ArrayIndex;
	GetPinAssociatedProperty(GetFNodeType(), Pin, /*out*/ AssociatedProperty, /*out*/ ArrayIndex);

	if (ArrayIndex != INDEX_NONE)
	{
		//@TODO: ANIMREFACTOR: Need to handle moving pins below up correctly
		// setting up removed pins info 
		RemovedPinArrayIndex = ArrayIndex;
		Node.RemovePose(ArrayIndex);
		ReconstructNode();
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(GetBlueprint());
	}
}

void UAnimGraphNode_LayeredBoneBlend::GetNodeContextMenuActions(UToolMenu* Menu, UGraphNodeContextMenuContext* Context) const
{
	if (!Context->bIsDebugging)
	{
		{
			FToolMenuSection& Section = Menu->AddSection("AnimGraphNodeLayeredBoneblend", LOCTEXT("LayeredBoneBlend", "Layered Bone Blend"));
			if (Context->Pin != NULL)
			{
				// we only do this for normal BlendList/BlendList by enum, BlendList by Bool doesn't support add/remove pins
				if (Context->Pin->Direction == EGPD_Input)
				{
					//@TODO: Only offer this option on arrayed pins
					Section.AddMenuEntry(FGraphEditorCommands::Get().RemoveBlendListPin);
				}
			}
			else
			{
				Section.AddMenuEntry(FGraphEditorCommands::Get().AddBlendListPin);
			}
		}
	}
}

void UAnimGraphNode_LayeredBoneBlend::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);
	Node.ValidateData();
}

void UAnimGraphNode_LayeredBoneBlend::ValidateAnimNodeDuringCompilation(class USkeleton* ForSkeleton, class FCompilerResultsLog& MessageLog)
{
	UAnimGraphNode_Base::ValidateAnimNodeDuringCompilation(ForSkeleton, MessageLog);

	// ensure to cache the data
 	if (Node.IsCacheInvalid(ForSkeleton))
 	{
 		Node.RebuildCacheData(ForSkeleton);
 	}
}
#undef LOCTEXT_NAMESPACE
