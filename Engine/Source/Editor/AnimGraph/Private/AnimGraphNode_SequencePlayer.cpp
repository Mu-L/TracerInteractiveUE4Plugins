// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimGraphNode_SequencePlayer.h"
#include "EdGraphSchema_K2_Actions.h"
#include "Modules/ModuleManager.h"
#include "ToolMenus.h"

#include "Kismet2/CompilerResultsLog.h"
#include "GraphEditorActions.h"
#include "ARFilter.h"
#include "AssetRegistryModule.h"
#include "BlueprintActionFilter.h"
#include "BlueprintActionDatabaseRegistrar.h"
#include "EditorCategoryUtils.h"
#include "BlueprintNodeSpawner.h"
#include "Animation/AnimComposite.h"
#include "Animation/AnimSequence.h"

#define LOCTEXT_NAMESPACE "A3Nodes"

/////////////////////////////////////////////////////
// FNewSequencePlayerAction

// Action to add a sequence player node to the graph
struct FNewSequencePlayerAction : public FEdGraphSchemaAction_K2NewNode
{
protected:
	FAssetData AssetInfo;
public:
	FNewSequencePlayerAction(const FAssetData& InAssetInfo, FText Title)
		: FEdGraphSchemaAction_K2NewNode(LOCTEXT("Animation", "Animations"), Title, LOCTEXT("EvalAnimSequenceToMakePose", "Evaluates an animation sequence to produce a pose"), 0, FText::FromName(InAssetInfo.ObjectPath))
	{
		AssetInfo = InAssetInfo;

		UAnimGraphNode_SequencePlayer* Template = NewObject<UAnimGraphNode_SequencePlayer>();
		NodeTemplate = Template;
	}

	virtual UEdGraphNode* PerformAction(class UEdGraph* ParentGraph, UEdGraphPin* FromPin, const FVector2D Location, bool bSelectNewNode = true) override
	{
		UAnimGraphNode_SequencePlayer* SpawnedNode = CastChecked<UAnimGraphNode_SequencePlayer>(FEdGraphSchemaAction_K2NewNode::PerformAction(ParentGraph, FromPin, Location, bSelectNewNode));
		SpawnedNode->Node.Sequence = Cast<UAnimSequence>(AssetInfo.GetAsset());

		return SpawnedNode;
	}
};

/////////////////////////////////////////////////////
// UAnimGraphNode_SequencePlayer

UAnimGraphNode_SequencePlayer::UAnimGraphNode_SequencePlayer(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void UAnimGraphNode_SequencePlayer::PreloadRequiredAssets()
{
	PreloadObject(Node.Sequence);

	Super::PreloadRequiredAssets();
}

FLinearColor UAnimGraphNode_SequencePlayer::GetNodeTitleColor() const
{
	if ((Node.Sequence != NULL) && Node.Sequence->IsValidAdditive())
	{
		return FLinearColor(0.10f, 0.60f, 0.12f);
	}
	else
	{
		return FColor(200, 100, 100);
	}
}

FText UAnimGraphNode_SequencePlayer::GetTooltipText() const
{
	if (!Node.Sequence)
	{
		return FText();
	}

	const bool bAdditive = Node.Sequence->IsValidAdditive();
	return GetTitleGivenAssetInfo(FText::FromString(Node.Sequence->GetPathName()), bAdditive);
}

FText UAnimGraphNode_SequencePlayer::GetNodeTitleForSequence(ENodeTitleType::Type TitleType, UAnimSequenceBase* InSequence) const
{
	const bool bAdditive = InSequence->IsValidAdditive();
	const FText BasicTitle = GetTitleGivenAssetInfo(FText::FromName(InSequence->GetFName()), bAdditive);

	if (SyncGroup.GroupName == NAME_None)
	{
		return BasicTitle;
	}
	else
	{
		const FText SyncGroupName = FText::FromName(SyncGroup.GroupName);

		FFormatNamedArguments Args;
		Args.Add(TEXT("Title"), BasicTitle);
		Args.Add(TEXT("SyncGroup"), SyncGroupName);

		if (TitleType == ENodeTitleType::FullTitle)
		{
			return FText::Format(LOCTEXT("SequenceNodeGroupWithSubtitleFull", "{Title}\nSync group {SyncGroup}"), Args);
		}
		else
		{
			return FText::Format(LOCTEXT("SequenceNodeGroupWithSubtitleList", "{Title} (Sync group {SyncGroup})"), Args);
		}
	}
}

FText UAnimGraphNode_SequencePlayer::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	if (Node.Sequence == nullptr)
	{
		// we may have a valid variable connected or default pin value
		UEdGraphPin* SequencePin = FindPin(GET_MEMBER_NAME_STRING_CHECKED(FAnimNode_SequencePlayer, Sequence));
		if (SequencePin && SequencePin->LinkedTo.Num() > 0)
		{
			return LOCTEXT("SequenceNodeTitleVariable", "Play Animation Sequence");
		}
		else if (SequencePin && SequencePin->DefaultObject != nullptr)
		{
			return GetNodeTitleForSequence(TitleType, CastChecked<UAnimSequenceBase>(SequencePin->DefaultObject));
		}
		else
		{
			return LOCTEXT("SequenceNullTitle", "Play (None)");
		}
	}
	else
	{
		return GetNodeTitleForSequence(TitleType, Node.Sequence);
	}
}

FText UAnimGraphNode_SequencePlayer::GetTitleGivenAssetInfo(const FText& AssetName, bool bKnownToBeAdditive)
{
	FFormatNamedArguments Args;
	Args.Add(TEXT("AssetName"), AssetName);

	if (bKnownToBeAdditive)
	{
		return FText::Format(LOCTEXT("SequenceNodeTitleAdditive", "Play {AssetName} (additive)"), Args);
	}
	else
	{
		return FText::Format(LOCTEXT("SequenceNodeTitle", "Play {AssetName}"), Args);
	}
}

FText UAnimGraphNode_SequencePlayer::GetMenuCategory() const
{
	return FEditorCategoryUtils::GetCommonCategory(FCommonEditorCategory::Animation);
}

void UAnimGraphNode_SequencePlayer::GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const
{
	auto LoadedAssetSetup = [](UEdGraphNode* NewNode, bool /*bIsTemplateNode*/, TWeakObjectPtr<UAnimSequence> SequencePtr)
	{
		UAnimGraphNode_SequencePlayer* SequencePlayerNode = CastChecked<UAnimGraphNode_SequencePlayer>(NewNode);
		SequencePlayerNode->Node.Sequence = SequencePtr.Get();
	};

	auto UnloadedAssetSetup = [](UEdGraphNode* NewNode, bool bIsTemplateNode, const FAssetData AssetData)
	{
		UAnimGraphNode_SequencePlayer* SequencePlayerNode = CastChecked<UAnimGraphNode_SequencePlayer>(NewNode);
		if (bIsTemplateNode)
		{
			AssetData.GetTagValue("Skeleton", SequencePlayerNode->UnloadedSkeletonName);
		}
		else
		{
			UAnimSequence* Sequence = Cast<UAnimSequence>(AssetData.GetAsset());
			check(Sequence != nullptr);
			SequencePlayerNode->Node.Sequence = Sequence;
		}
	};	

	const UObject* QueryObject = ActionRegistrar.GetActionKeyFilter();
	if (QueryObject == nullptr)
	{
		FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
		// define a filter to help in pulling UAnimSequence asset data from the registry
		FARFilter Filter;
		Filter.ClassNames.Add(UAnimSequence::StaticClass()->GetFName());
		Filter.bRecursiveClasses = true;
		// Find matching assets and add an entry for each one
		TArray<FAssetData> SequenceList;
		AssetRegistryModule.Get().GetAssets(Filter, /*out*/SequenceList);

		for (auto AssetIt = SequenceList.CreateConstIterator(); AssetIt; ++AssetIt)
		{
			const FAssetData& Asset = *AssetIt;			

			UBlueprintNodeSpawner* NodeSpawner = UBlueprintNodeSpawner::Create(GetClass());
			if (Asset.IsAssetLoaded())
			{
				UAnimSequence* AnimSequence = Cast<UAnimSequence>(Asset.GetAsset());
				if(AnimSequence)
				{
					NodeSpawner->CustomizeNodeDelegate = UBlueprintNodeSpawner::FCustomizeNodeDelegate::CreateStatic(LoadedAssetSetup, TWeakObjectPtr<UAnimSequence>(AnimSequence));
					NodeSpawner->DefaultMenuSignature.MenuName = GetTitleGivenAssetInfo(FText::FromName(AnimSequence->GetFName()), AnimSequence->IsValidAdditive());
					NodeSpawner->DefaultMenuSignature.Tooltip = GetTitleGivenAssetInfo(FText::FromString(AnimSequence->GetPathName()), AnimSequence->IsValidAdditive());
				}
			}
			else
			{
				const FString TagValue = Asset.GetTagValueRef<FString>(GET_MEMBER_NAME_CHECKED(UAnimSequence, AdditiveAnimType));
				const bool bKnownToBeAdditive = (!TagValue.IsEmpty() && !TagValue.Equals(TEXT("AAT_None")));

				NodeSpawner->CustomizeNodeDelegate = UBlueprintNodeSpawner::FCustomizeNodeDelegate::CreateStatic(UnloadedAssetSetup, Asset);
				NodeSpawner->DefaultMenuSignature.MenuName = GetTitleGivenAssetInfo(FText::FromName(Asset.AssetName), bKnownToBeAdditive);
				NodeSpawner->DefaultMenuSignature.Tooltip = GetTitleGivenAssetInfo(FText::FromName(Asset.ObjectPath), bKnownToBeAdditive);
			}
			ActionRegistrar.AddBlueprintAction(Asset, NodeSpawner);
		}
	}
	else if (const UAnimSequence* AnimSequence = Cast<UAnimSequence>(QueryObject))
	{
		UBlueprintNodeSpawner* NodeSpawner = UBlueprintNodeSpawner::Create(GetClass());

		TWeakObjectPtr<UAnimSequence> SequencePtr = MakeWeakObjectPtr(const_cast<UAnimSequence*>(AnimSequence));
		NodeSpawner->CustomizeNodeDelegate = UBlueprintNodeSpawner::FCustomizeNodeDelegate::CreateStatic(LoadedAssetSetup, SequencePtr);
		NodeSpawner->DefaultMenuSignature.MenuName = GetTitleGivenAssetInfo(FText::FromName(AnimSequence->GetFName()), AnimSequence->IsValidAdditive());
		NodeSpawner->DefaultMenuSignature.Tooltip = GetTitleGivenAssetInfo(FText::FromString(AnimSequence->GetPathName()), AnimSequence->IsValidAdditive());

		ActionRegistrar.AddBlueprintAction(QueryObject, NodeSpawner);
	}
	else if (QueryObject == GetClass())
	{
		FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
		// define a filter to help in pulling UAnimSequence asset data from the registry
		FARFilter Filter;
		Filter.ClassNames.Add(UAnimSequence::StaticClass()->GetFName());
		Filter.bRecursiveClasses = true;
		// Find matching assets and add an entry for each one
		TArray<FAssetData> SequenceList;
		AssetRegistryModule.Get().GetAssets(Filter, /*out*/SequenceList);

		for (auto AssetIt = SequenceList.CreateConstIterator(); AssetIt; ++AssetIt)
		{
			const FAssetData& Asset = *AssetIt;
			if (Asset.IsAssetLoaded())
			{
				continue;
			}

			UBlueprintNodeSpawner* NodeSpawner = UBlueprintNodeSpawner::Create(GetClass());
			NodeSpawner->CustomizeNodeDelegate = UBlueprintNodeSpawner::FCustomizeNodeDelegate::CreateStatic(UnloadedAssetSetup, Asset);

			const FString TagValue = Asset.GetTagValueRef<FString>(GET_MEMBER_NAME_CHECKED(UAnimSequence, AdditiveAnimType));
			const bool bKnownToBeAdditive = (!TagValue.IsEmpty() && !TagValue.Equals(TEXT("AAT_None")));

			NodeSpawner->DefaultMenuSignature.MenuName = GetTitleGivenAssetInfo(FText::FromName(Asset.AssetName), bKnownToBeAdditive);
			NodeSpawner->DefaultMenuSignature.Tooltip = GetTitleGivenAssetInfo(FText::FromName(Asset.ObjectPath), bKnownToBeAdditive);
			ActionRegistrar.AddBlueprintAction(Asset, NodeSpawner);
		}
	}	
}

bool UAnimGraphNode_SequencePlayer::IsActionFilteredOut(class FBlueprintActionFilter const& Filter)
{
	bool bIsFilteredOut = false;
	FBlueprintActionContext const& FilterContext = Filter.Context;

	for (UBlueprint* Blueprint : FilterContext.Blueprints)
	{
		if (UAnimBlueprint* AnimBlueprint = Cast<UAnimBlueprint>(Blueprint))
		{
			if(Node.Sequence)
			{
				if(Node.Sequence->GetSkeleton() != AnimBlueprint->TargetSkeleton)
				{
					// Sequence does not use the same skeleton as the Blueprint, cannot use
					bIsFilteredOut = true;
					break;
				}
			}
			else 
			{
				FAssetData SkeletonData(AnimBlueprint->TargetSkeleton);
				if(UnloadedSkeletonName != SkeletonData.GetExportTextName())
				{
					bIsFilteredOut = true;
					break;
				}
			}
		}
		else
		{
			// Not an animation Blueprint, cannot use
			bIsFilteredOut = true;
			break;
		}
	}
	return bIsFilteredOut;
}

EAnimAssetHandlerType UAnimGraphNode_SequencePlayer::SupportsAssetClass(const UClass* AssetClass) const
{
	if (AssetClass->IsChildOf(UAnimSequence::StaticClass()) || AssetClass->IsChildOf(UAnimComposite::StaticClass()))
	{
		return EAnimAssetHandlerType::PrimaryHandler;
	}
	else
	{
		return EAnimAssetHandlerType::NotSupported;
	}
}

void UAnimGraphNode_SequencePlayer::ValidateAnimNodeDuringCompilation(class USkeleton* ForSkeleton, class FCompilerResultsLog& MessageLog)
{
	Super::ValidateAnimNodeDuringCompilation(ForSkeleton, MessageLog);

	UAnimSequenceBase* SequenceToCheck = Node.Sequence;
	UEdGraphPin* SequencePin = FindPin(GET_MEMBER_NAME_STRING_CHECKED(FAnimNode_SequencePlayer, Sequence));
	if (SequencePin != nullptr && SequenceToCheck == nullptr)
	{
		SequenceToCheck = Cast<UAnimSequenceBase>(SequencePin->DefaultObject);
	}

	if (SequenceToCheck == nullptr)
	{
		// Check for bindings
		bool bHasBinding = false;
		if(SequencePin != nullptr)
		{
			if (FAnimGraphNodePropertyBinding* BindingPtr = PropertyBindings.Find(SequencePin->GetFName()))
			{
				bHasBinding = true;
			}
		}

		// we may have a connected node or binding
		if (SequencePin == nullptr || (SequencePin->LinkedTo.Num() == 0 && !bHasBinding))
		{
			MessageLog.Error(TEXT("@@ references an unknown sequence"), this);
		}
	}
	else if(SupportsAssetClass(SequenceToCheck->GetClass()) == EAnimAssetHandlerType::NotSupported)
	{
		MessageLog.Error(*FText::Format(LOCTEXT("UnsupportedAssetError", "@@ is trying to play a {0} as a sequence, which is not allowed."), SequenceToCheck->GetClass()->GetDisplayNameText()).ToString(), this);
	}
	else
	{
		USkeleton* SeqSkeleton = SequenceToCheck->GetSkeleton();
		if (SeqSkeleton&& // if anim sequence doesn't have skeleton, it might be due to anim sequence not loaded yet, @todo: wait with anim blueprint compilation until all assets are loaded?
			!SeqSkeleton->IsCompatible(ForSkeleton))
		{
			MessageLog.Error(TEXT("@@ references sequence that uses different skeleton @@"), this, SeqSkeleton);
		}
	}
}

void UAnimGraphNode_SequencePlayer::GetNodeContextMenuActions(UToolMenu* Menu, UGraphNodeContextMenuContext* Context) const
{
	if (!Context->bIsDebugging)
	{
		// add an option to convert to single frame
		{
			FToolMenuSection& Section = Menu->AddSection("AnimGraphNodeSequencePlayer", NSLOCTEXT("A3Nodes", "SequencePlayerHeading", "Sequence Player"));
			Section.AddMenuEntry(FGraphEditorCommands::Get().OpenRelatedAsset);
			Section.AddMenuEntry(FGraphEditorCommands::Get().ConvertToSeqEvaluator);
		}
	}
}

void UAnimGraphNode_SequencePlayer::SetAnimationAsset(UAnimationAsset* Asset)
{
	if (UAnimSequenceBase* Seq = Cast<UAnimSequenceBase>(Asset))
	{
		Node.Sequence = Seq;
	}
}

void UAnimGraphNode_SequencePlayer::BakeDataDuringCompilation(class FCompilerResultsLog& MessageLog)
{
	UAnimBlueprint* AnimBlueprint = GetAnimBlueprint();
	AnimBlueprint->FindOrAddGroup(SyncGroup.GroupName);
	Node.GroupName = SyncGroup.GroupName;
	Node.GroupRole = SyncGroup.GroupRole;
	Node.GroupScope = SyncGroup.GroupScope;
}

void UAnimGraphNode_SequencePlayer::GetAllAnimationSequencesReferred(TArray<UAnimationAsset*>& AnimationAssets) const
{
	if(Node.Sequence)
	{
		HandleAnimReferenceCollection(Node.Sequence, AnimationAssets);
	}
}

void UAnimGraphNode_SequencePlayer::ReplaceReferredAnimations(const TMap<UAnimationAsset*, UAnimationAsset*>& AnimAssetReplacementMap)
{
	HandleAnimReferenceReplacement(Node.Sequence, AnimAssetReplacementMap);
}


bool UAnimGraphNode_SequencePlayer::DoesSupportTimeForTransitionGetter() const
{
	return true;
}

UAnimationAsset* UAnimGraphNode_SequencePlayer::GetAnimationAsset() const 
{
	UAnimSequenceBase* Sequence = Node.Sequence;
	UEdGraphPin* SequencePin = FindPin(GET_MEMBER_NAME_STRING_CHECKED(FAnimNode_SequencePlayer, Sequence));
	if (SequencePin != nullptr && Sequence == nullptr)
	{
		Sequence = Cast<UAnimSequenceBase>(SequencePin->DefaultObject);
	}

	return Sequence;
}

const TCHAR* UAnimGraphNode_SequencePlayer::GetTimePropertyName() const 
{
	return TEXT("InternalTimeAccumulator");
}

UScriptStruct* UAnimGraphNode_SequencePlayer::GetTimePropertyStruct() const 
{
	return FAnimNode_SequencePlayer::StaticStruct();
}

void UAnimGraphNode_SequencePlayer::CustomizePinData(UEdGraphPin* Pin, FName SourcePropertyName, int32 ArrayIndex) const
{
	Super::CustomizePinData(Pin, SourcePropertyName, ArrayIndex);

	if (Pin->PinName == GET_MEMBER_NAME_STRING_CHECKED(FAnimNode_SequencePlayer, PlayRate))
	{
		if (!Pin->bHidden)
		{
			// Draw value for PlayRateBasis if the pin is not exposed
			UEdGraphPin* PlayRateBasisPin = FindPin(GET_MEMBER_NAME_STRING_CHECKED(FAnimNode_SequencePlayer, PlayRateBasis));
			if (!PlayRateBasisPin || PlayRateBasisPin->bHidden)
			{
				if (Node.PlayRateBasis != 1.f)
				{
					FFormatNamedArguments Args;
					Args.Add(TEXT("PinFriendlyName"), Pin->PinFriendlyName);
					Args.Add(TEXT("PlayRateBasis"), FText::AsNumber(Node.PlayRateBasis));
					Pin->PinFriendlyName = FText::Format(LOCTEXT("FAnimNode_SequencePlayer_PlayRateBasis_Value", "({PinFriendlyName} / {PlayRateBasis})"), Args);
				}
			}
			else // PlayRateBasisPin is visible
			{
				FFormatNamedArguments Args;
				Args.Add(TEXT("PinFriendlyName"), Pin->PinFriendlyName);
				Pin->PinFriendlyName = FText::Format(LOCTEXT("FAnimNode_SequencePlayer_PlayRateBasis_Name", "({PinFriendlyName} / PlayRateBasis)"), Args);
			}

			Pin->PinFriendlyName = Node.PlayRateScaleBiasClamp.GetFriendlyName(Pin->PinFriendlyName);
		}
	}
}

void UAnimGraphNode_SequencePlayer::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	const FName PropertyName = (PropertyChangedEvent.Property ? PropertyChangedEvent.Property->GetFName() : NAME_None);

	// Reconstruct node to show updates to PinFriendlyNames.
	if ((PropertyName == GET_MEMBER_NAME_STRING_CHECKED(FAnimNode_SequencePlayer, PlayRateBasis))
		|| (PropertyName == GET_MEMBER_NAME_STRING_CHECKED(FInputScaleBiasClamp, bMapRange))
		|| (PropertyName == GET_MEMBER_NAME_STRING_CHECKED(FInputRange, Min))
		|| (PropertyName == GET_MEMBER_NAME_STRING_CHECKED(FInputRange, Max))
		|| (PropertyName == GET_MEMBER_NAME_STRING_CHECKED(FInputScaleBiasClamp, Scale))
		|| (PropertyName == GET_MEMBER_NAME_STRING_CHECKED(FInputScaleBiasClamp, Bias))
		|| (PropertyName == GET_MEMBER_NAME_STRING_CHECKED(FInputScaleBiasClamp, bClampResult))
		|| (PropertyName == GET_MEMBER_NAME_STRING_CHECKED(FInputScaleBiasClamp, ClampMin))
		|| (PropertyName == GET_MEMBER_NAME_STRING_CHECKED(FInputScaleBiasClamp, ClampMax))
		|| (PropertyName == GET_MEMBER_NAME_STRING_CHECKED(FInputScaleBiasClamp, bInterpResult))
		|| (PropertyName == GET_MEMBER_NAME_STRING_CHECKED(FInputScaleBiasClamp, InterpSpeedIncreasing))
		|| (PropertyName == GET_MEMBER_NAME_STRING_CHECKED(FInputScaleBiasClamp, InterpSpeedDecreasing)))
	{
		ReconstructNode();
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);
}

#undef LOCTEXT_NAMESPACE
