// Copyright Epic Games, Inc. All Rights Reserved.

#include "ViewModels/Stack/NiagaraStackEntry.h"
#include "ViewModels/Stack/NiagaraStackErrorItem.h"
#include "ViewModels/NiagaraSystemViewModel.h"
#include "ViewModels/NiagaraEmitterViewModel.h"
#include "NiagaraStackEditorData.h"
#include "NiagaraScriptMergeManager.h"
#include "Misc/SecureHash.h"
#include "ScopedTransaction.h"

const FName UNiagaraStackEntry::FExecutionCategoryNames::System = TEXT("System");
const FName UNiagaraStackEntry::FExecutionCategoryNames::Emitter = TEXT("Emitter");
const FName UNiagaraStackEntry::FExecutionCategoryNames::Particle = TEXT("Particle");
const FName UNiagaraStackEntry::FExecutionCategoryNames::Render = TEXT("Render");

const FName UNiagaraStackEntry::FExecutionSubcategoryNames::Settings = TEXT("Settings");
const FName UNiagaraStackEntry::FExecutionSubcategoryNames::Spawn = TEXT("Spawn");
const FName UNiagaraStackEntry::FExecutionSubcategoryNames::Update = TEXT("Update");
const FName UNiagaraStackEntry::FExecutionSubcategoryNames::Event = TEXT("Event");
const FName UNiagaraStackEntry::FExecutionSubcategoryNames::SimulationStage = TEXT("Simulation Stage");
const FName UNiagaraStackEntry::FExecutionSubcategoryNames::Render = TEXT("Render");

UNiagaraStackEntry::FStackIssueFix::FStackIssueFix()
{
}

UNiagaraStackEntry::FStackIssueFix::FStackIssueFix(FText InDescription, FStackIssueFixDelegate InFixDelegate, EStackIssueFixStyle InFixStyle)
	: Description(InDescription)
	, FixDelegate(InFixDelegate)
	, Style(InFixStyle)
	, UniqueIdentifier(FMD5::HashAnsiString(*FString::Printf(TEXT("%s"), *InDescription.ToString())))
{
	checkf(Description.IsEmptyOrWhitespace() == false, TEXT("Description can not be empty."));
	checkf(InFixDelegate.IsBound(), TEXT("Fix delegate must be bound."));
}

bool UNiagaraStackEntry::FStackIssueFix::IsValid() const
{
	return FixDelegate.IsBound();
}

const FText& UNiagaraStackEntry::FStackIssueFix::GetDescription() const
{
	return Description;
}

void UNiagaraStackEntry::FStackIssueFix::SetFixDelegate(const FStackIssueFixDelegate& InFixDelegate)
{
	FixDelegate = InFixDelegate;
}

const FString& UNiagaraStackEntry::FStackIssueFix::GetUniqueIdentifier() const
{
	return UniqueIdentifier;
}

const UNiagaraStackEntry::FStackIssueFixDelegate& UNiagaraStackEntry::FStackIssueFix::GetFixDelegate() const
{
	return FixDelegate;
}

UNiagaraStackEntry::EStackIssueFixStyle UNiagaraStackEntry::FStackIssueFix::GetStyle() const
{
	return Style;
}

UNiagaraStackEntry::FStackIssue::FStackIssue()
{
}

UNiagaraStackEntry::FStackIssue::FStackIssue(EStackIssueSeverity InSeverity, FText InShortDescription, FText InLongDescription, FString InStackEditorDataKey, bool bInCanBeDismissed, const TArray<FStackIssueFix>& InFixes)
	: Severity(InSeverity)
	, ShortDescription(InShortDescription)
	, LongDescription(InLongDescription)
	, UniqueIdentifier(FMD5::HashAnsiString(*FString::Printf(TEXT("%s-%s-%s"), *InStackEditorDataKey, *InShortDescription.ToString(), *InLongDescription.ToString())))
	, bCanBeDismissed(bInCanBeDismissed)
	, Fixes(InFixes)
{
	checkf(ShortDescription.IsEmptyOrWhitespace() == false, TEXT("Short description can not be empty."));
	//checkf(LongDescription.IsEmptyOrWhitespace() == false, TEXT("Long description can not be empty."));
	//if (LongDescription.IsEmptyOrWhitespace())
	//{
	//	LongDescription = ShortDescription;
	//}
	checkf(InStackEditorDataKey.IsEmpty() == false, TEXT("Stack editor data key can not be empty."));
}

UNiagaraStackEntry::FStackIssue::FStackIssue(EStackIssueSeverity InSeverity, FText InShortDescription, FText InLongDescription, FString InStackEditorDataKey, bool bInCanBeDismissed, FStackIssueFix InFix)
	: FStackIssue(InSeverity, InShortDescription, InLongDescription, InStackEditorDataKey, bInCanBeDismissed, TArray<FStackIssueFix>({InFix }))
{
}

UNiagaraStackEntry::FStackIssue::FStackIssue(EStackIssueSeverity InSeverity, FText InShortDescription, FText InLongDescription, FString InStackEditorDataKey, bool bInCanBeDismissed)
	: FStackIssue(InSeverity, InShortDescription, InLongDescription, InStackEditorDataKey, bInCanBeDismissed, TArray<FStackIssueFix>())
{
}

bool UNiagaraStackEntry::FStackIssue::IsValid()
{
	return UniqueIdentifier.IsEmpty() == false;
}

EStackIssueSeverity UNiagaraStackEntry::FStackIssue::GetSeverity() const
{
	return Severity;
}

const FText& UNiagaraStackEntry::FStackIssue::GetShortDescription() const
{
	return ShortDescription;
}

const FText& UNiagaraStackEntry::FStackIssue::GetLongDescription() const
{
	return LongDescription;
}

bool UNiagaraStackEntry::FStackIssue::GetCanBeDismissed() const
{
	return bCanBeDismissed;
}

const FString& UNiagaraStackEntry::FStackIssue::GetUniqueIdentifier() const
{
	return UniqueIdentifier;
}

const TArray<UNiagaraStackEntry::FStackIssueFix>& UNiagaraStackEntry::FStackIssue::GetFixes() const
{
	return Fixes;
}

void UNiagaraStackEntry::FStackIssue::InsertFix(int32 InsertionIdx, const UNiagaraStackEntry::FStackIssueFix& Fix)
{
	Fixes.Insert(Fix, InsertionIdx);
}


UNiagaraStackEntry::UNiagaraStackEntry()
	: IndentLevel(0)
	, bIsFinalized(false)
	, bIsSearchResult(false)
	, bOwnerIsEnabled(true)
{
}

void UNiagaraStackEntry::Initialize(FRequiredEntryData InRequiredEntryData, FString InStackEditorDataKey)
{
	checkf(bIsFinalized == false, TEXT("Can not initialize an entry after it has been finalized."));
	SystemViewModel = InRequiredEntryData.SystemViewModel;
	EmitterViewModel = InRequiredEntryData.EmitterViewModel;
	ExecutionCategoryName = InRequiredEntryData.ExecutionCategoryName;
	ExecutionSubcategoryName = InRequiredEntryData.ExecutionSubcategoryName;
	StackEditorData = InRequiredEntryData.StackEditorData;
	StackEditorDataKey = InStackEditorDataKey;
}

void UNiagaraStackEntry::Finalize()
{
	if (ensureMsgf(IsFinalized() == false, TEXT("Can not finalize a stack entry more than once.")) == false)
	{
		return;
	}

	FinalizeInternal();
	checkf(bIsFinalized, TEXT("Parent FinalizeInternal not called from overriden FinalizeInternal"));

	SystemViewModel.Reset();
	EmitterViewModel.Reset();
	StackEditorData = nullptr;

	for (UNiagaraStackEntry* Child : Children)
	{
		if(Child->GetOuter() == this)
		{
			Child->Finalize();
		}
	}
	Children.Empty();

	for (UNiagaraStackEntry* ErrorChild : ErrorChildren)
	{
		ErrorChild->Finalize();
	}
	ErrorChildren.Empty();
}

bool UNiagaraStackEntry::IsFinalized() const
{
	return bIsFinalized;
}

FText UNiagaraStackEntry::GetDisplayName() const
{
	return FText();
}

TOptional<FText> UNiagaraStackEntry::GetAlternateDisplayName() const
{
	return AlternateDisplayName;
}

UObject* UNiagaraStackEntry::GetDisplayedObject() const
{
	return nullptr;
}

UNiagaraStackEditorData& UNiagaraStackEntry::GetStackEditorData() const
{
	return *StackEditorData;
}

FString UNiagaraStackEntry::GetStackEditorDataKey() const
{
	return StackEditorDataKey;
}

FText UNiagaraStackEntry::GetTooltipText() const
{
	return FText();
}

bool UNiagaraStackEntry::GetCanExpand() const
{
	return true;
}

bool UNiagaraStackEntry::IsExpandedByDefault() const
{
	return true;
}

bool UNiagaraStackEntry::GetIsExpanded() const
{
	if (GetShouldShowInStack() == false || GetCanExpand() == false)
	{
		// Entries that aren't visible, or can't expand are always expanded.
		return true;
	}

	if (bIsExpandedCache.IsSet() == false)
	{
		bIsExpandedCache = StackEditorData->GetStackEntryIsExpanded(GetStackEditorDataKey(), IsExpandedByDefault());
	}
	return bIsExpandedCache.GetValue();
}

void UNiagaraStackEntry::SetIsExpanded(bool bInExpanded)
{
	if (StackEditorData && GetCanExpand())
	{
		StackEditorData->SetStackEntryIsExpanded(GetStackEditorDataKey(), bInExpanded);
	}
	bIsExpandedCache.Reset();
}

void UNiagaraStackEntry::SetIsExpanded_Recursive(bool bInExpanded)
{
	if (StackEditorData && GetCanExpand())
	{
		StackEditorData->SetStackEntryIsExpanded(GetStackEditorDataKey(), bInExpanded);
	}

	TArray<UNiagaraStackEntry*> UnfilteredChildren;
	GetUnfilteredChildren(UnfilteredChildren);
	for (UNiagaraStackEntry* Child : UnfilteredChildren)
	{
		Child->SetIsExpanded_Recursive(bInExpanded);
	}

	bIsExpandedCache.Reset();
}

bool UNiagaraStackEntry::GetIsEnabled() const
{
	return true;
}

bool UNiagaraStackEntry::GetOwnerIsEnabled() const
{
	return bOwnerIsEnabled;
}

FName UNiagaraStackEntry::GetExecutionCategoryName() const
{
	return ExecutionCategoryName;
}

FName UNiagaraStackEntry::GetExecutionSubcategoryName() const
{
	return ExecutionSubcategoryName;
}

UNiagaraStackEntry::EStackRowStyle UNiagaraStackEntry::GetStackRowStyle() const
{
	return EStackRowStyle::None;
}

bool UNiagaraStackEntry::HasFrontDivider() const
{
	UNiagaraStackEntry* Outer = Cast<UNiagaraStackEntry>(GetOuter());
	if (Outer == nullptr)
	{
		return false;
	}
	return Outer->HasFrontDivider();
}

bool UNiagaraStackEntry::GetShouldShowInStack() const
{
	return true;
}

void UNiagaraStackEntry::GetFilteredChildren(TArray<UNiagaraStackEntry*>& OutFilteredChildren) const
{
	OutFilteredChildren.Append(ErrorChildren);
	for (UNiagaraStackEntry* Child : Children)
	{
		bool bPassesFilter = true;
		for (const FOnFilterChild& ChildFilter : ChildFilters)
		{
			if (ChildFilter.Execute(*Child) == false)
			{
				bPassesFilter = false;
				break;
			}
		}

		if (bPassesFilter)
		{
			OutFilteredChildren.Add(Child);
		}
	}
}

void UNiagaraStackEntry::GetUnfilteredChildren(TArray<UNiagaraStackEntry*>& OutUnfilteredChildren) const
{
	OutUnfilteredChildren.Append(ErrorChildren);
	OutUnfilteredChildren.Append(Children);
}

FDelegateHandle UNiagaraStackEntry::AddChildFilter(FOnFilterChild ChildFilter)
{
	ChildFilters.Add(ChildFilter);
	StructureChangedDelegate.Broadcast();
	return ChildFilters.Last().GetHandle();
}

void UNiagaraStackEntry::RemoveChildFilter(FDelegateHandle FilterHandle)
{
	ChildFilters.RemoveAll([=](const FOnFilterChild& ChildFilter) { return ChildFilter.GetHandle() == FilterHandle; });
	StructureChangedDelegate.Broadcast();
}

TSharedRef<FNiagaraSystemViewModel> UNiagaraStackEntry::GetSystemViewModel() const
{
	TSharedPtr<FNiagaraSystemViewModel> PinnedSystemViewModel = SystemViewModel.Pin();
	checkf(PinnedSystemViewModel.IsValid(), TEXT("Base stack entry not initialized or system view model was already deleted."));
	return PinnedSystemViewModel.ToSharedRef();
}

TSharedPtr<FNiagaraEmitterViewModel> UNiagaraStackEntry::GetEmitterViewModel() const
{
	return EmitterViewModel.Pin();
}

UNiagaraStackEntry::FOnStructureChanged& UNiagaraStackEntry::OnStructureChanged()
{
	return StructureChangedDelegate;
}

UNiagaraStackEntry::FOnDataObjectModified& UNiagaraStackEntry::OnDataObjectModified()
{
	return DataObjectModifiedDelegate;
}

UNiagaraStackEntry::FOnRequestFullRefresh& UNiagaraStackEntry::OnRequestFullRefresh()
{
	return RequestFullRefreshDelegate;
}

const UNiagaraStackEntry::FOnRequestFullRefresh& UNiagaraStackEntry::OnRequestFullRefreshDeferred() const
{
	return RequestFullRefreshDeferredDelegate;
}

UNiagaraStackEntry::FOnRequestFullRefresh& UNiagaraStackEntry::OnRequestFullRefreshDeferred()
{
	return RequestFullRefreshDeferredDelegate;
}

UNiagaraStackEntry::FOnAlternateDisplayNameChanged& UNiagaraStackEntry::OnAlternateDisplayNameChanged()
{
	return AlternateDisplayNameChangedDelegate;
}

int32 UNiagaraStackEntry::GetIndentLevel() const
{
	return IndentLevel;
}

void UNiagaraStackEntry::GetSearchItems(TArray<UNiagaraStackEntry::FStackSearchItem>& SearchItems) const
{
	SearchItems.Add({FName("DisplayName"), GetDisplayName()}); 
}

UObject* UNiagaraStackEntry::GetExternalAsset() const
{
	return nullptr;
}

bool UNiagaraStackEntry::CanDrag() const
{
	return false;
}

TOptional<UNiagaraStackEntry::FDropRequestResponse> UNiagaraStackEntry::CanDrop(const FDropRequest& DropRequest)
{
	TOptional<FDropRequestResponse> Response = CanDropInternal(DropRequest);
	if (Response.IsSet())
	{
		return Response;
	}
	else
	{
		return OnRequestCanDropDelegate.IsBound() 
			? OnRequestCanDropDelegate.Execute(*this, DropRequest)
			: TOptional<FDropRequestResponse>();
	}
}

TOptional<UNiagaraStackEntry::FDropRequestResponse> UNiagaraStackEntry::Drop(const FDropRequest& DropRequest)
{
	TOptional<FDropRequestResponse> Response = DropInternal(DropRequest);
	if (Response.IsSet())
	{
		return Response;
	}
	else
	{
		return OnRequestDropDelegate.IsBound()
			? OnRequestDropDelegate.Execute(*this, DropRequest)
			: TOptional<FDropRequestResponse>();
	}
}

void UNiagaraStackEntry::SetOnRequestCanDrop(FOnRequestDrop InOnRequestCanDrop)
{
	OnRequestCanDropDelegate = InOnRequestCanDrop;
}

void UNiagaraStackEntry::SetOnRequestDrop(FOnRequestDrop InOnRequestDrop)
{
	OnRequestDropDelegate = InOnRequestDrop;
}

const bool UNiagaraStackEntry::GetIsSearchResult() const
{
	return bIsSearchResult;
}

void UNiagaraStackEntry::SetIsSearchResult(bool bInIsSearchResult)
{
	bIsSearchResult = bInIsSearchResult;
}

bool UNiagaraStackEntry::HasBaseEmitter() const
{
	if (bHasBaseEmitterCache.IsSet() == false)
	{
		const UNiagaraEmitter* BaseEmitter = GetEmitterViewModel().IsValid() ? GetEmitterViewModel()->GetParentEmitter() : nullptr;
		bHasBaseEmitterCache = BaseEmitter != nullptr;
	}
	return bHasBaseEmitterCache.GetValue();
}

bool UNiagaraStackEntry::HasIssuesOrAnyChildHasIssues() const
{
	return TotalNumberOfErrorIssues > 0 || TotalNumberOfWarningIssues > 0 || TotalNumberOfInfoIssues > 0;
}

int32 UNiagaraStackEntry::GetTotalNumberOfInfoIssues() const
{
	return TotalNumberOfInfoIssues;
}

int32 UNiagaraStackEntry::GetTotalNumberOfWarningIssues() const
{
	return TotalNumberOfWarningIssues;
}

int32 UNiagaraStackEntry::GetTotalNumberOfErrorIssues() const
{
	return TotalNumberOfErrorIssues;
}

const TArray<UNiagaraStackEntry::FStackIssue>& UNiagaraStackEntry::GetIssues() const
{
	return StackIssues;
}

const TArray<UNiagaraStackEntry*>& UNiagaraStackEntry::GetAllChildrenWithIssues() const
{
	return ChildrenWithIssues;
}

TOptional<UNiagaraStackEntry::FDropRequestResponse> UNiagaraStackEntry::CanDropInternal(const FDropRequest& DropRequest)
{
	return TOptional<FDropRequestResponse>();
}

TOptional<UNiagaraStackEntry::FDropRequestResponse> UNiagaraStackEntry::DropInternal(const FDropRequest& DropRequest)
{
	return TOptional<FDropRequestResponse>();
}

TOptional<UNiagaraStackEntry::FDropRequestResponse> UNiagaraStackEntry::ChildRequestCanDropInternal(const UNiagaraStackEntry& TargetChild, const FDropRequest& DropRequest)
{
	return TOptional<FDropRequestResponse>();
}

TOptional<UNiagaraStackEntry::FDropRequestResponse> UNiagaraStackEntry::ChildRequestDropInternal(const UNiagaraStackEntry& TargetChild, const FDropRequest& DropRequest)
{
	return TOptional<FDropRequestResponse>();
}

void UNiagaraStackEntry::ChildStructureChangedInternal()
{
}

void UNiagaraStackEntry::FinalizeInternal()
{
	bIsFinalized = true;
}

void UNiagaraStackEntry::RefreshChildren()
{
	checkf(bIsFinalized == false, TEXT("Can not refresh children on an entry after it has been finalized."));
	checkf(SystemViewModel.IsValid(), TEXT("Base stack entry not initialized."));

	for (UNiagaraStackEntry* Child : Children)
	{
		Child->OnStructureChanged().RemoveAll(this);
		Child->OnDataObjectModified().RemoveAll(this);
		Child->OnRequestFullRefresh().RemoveAll(this);
		Child->OnRequestFullRefreshDeferred().RemoveAll(this);
		UNiagaraStackEntry* OuterOwner = Cast<UNiagaraStackEntry>(Child->GetOuter());
		if (OuterOwner == this)
		{
			Child->SetOnRequestCanDrop(FOnRequestDrop());
			Child->SetOnRequestDrop(FOnRequestDrop());
		}
	}
	for (UNiagaraStackErrorItem* ErrorChild : ErrorChildren)
	{
		ErrorChild->OnStructureChanged().RemoveAll(this);
		ErrorChild->OnDataObjectModified().RemoveAll(this);
		ErrorChild->OnRequestFullRefresh().RemoveAll(this);
		ErrorChild->OnRequestFullRefreshDeferred().RemoveAll(this);
		ErrorChild->OnIssueModified().RemoveAll(this);
	}

	bHasBaseEmitterCache.Reset();

	TArray<UNiagaraStackEntry*> NewChildren;
	TArray<FStackIssue> NewStackIssues;
	RefreshChildrenInternal(Children, NewChildren, NewStackIssues);

	// If any of the current children were not moved to the new children collection, and they're owned by this entry than finalize them since
	// they weren't reused.
	for (UNiagaraStackEntry* Child : Children)
	{
		if (NewChildren.Contains(Child) == false && Child->GetOuter() == this)
		{
			Child->Finalize();
		}
	}

	Children.Empty();
	Children.Append(NewChildren);

	TotalNumberOfInfoIssues = 0;
	TotalNumberOfWarningIssues = 0;
	TotalNumberOfErrorIssues = 0;
	ChildrenWithIssues.Empty();

	for (UNiagaraStackEntry* Child : Children)
	{
		UNiagaraStackEntry* OuterOwner = Cast<UNiagaraStackEntry>(Child->GetOuter());
		Child->IndentLevel = GetChildIndentLevel();
		Child->bOwnerIsEnabled = OuterOwner == nullptr || (OuterOwner->GetIsEnabled() && OuterOwner->GetOwnerIsEnabled());
		Child->RefreshChildren();
		Child->OnStructureChanged().AddUObject(this, &UNiagaraStackEntry::ChildStructureChanged);
		Child->OnDataObjectModified().AddUObject(this, &UNiagaraStackEntry::ChildDataObjectModified);
		Child->OnRequestFullRefresh().AddUObject(this, &UNiagaraStackEntry::ChildRequestFullRefresh);
		Child->OnRequestFullRefreshDeferred().AddUObject(this, &UNiagaraStackEntry::ChildRequestFullRefreshDeferred);
		if (OuterOwner == this)
		{
			Child->SetOnRequestCanDrop(FOnRequestDrop::CreateUObject(this, &UNiagaraStackEntry::ChildRequestCanDrop));
			Child->SetOnRequestDrop(FOnRequestDrop::CreateUObject(this, &UNiagaraStackEntry::ChildRequestDrop));
		}

		TotalNumberOfInfoIssues += Child->GetTotalNumberOfInfoIssues();
		TotalNumberOfWarningIssues += Child->GetTotalNumberOfWarningIssues();
		TotalNumberOfErrorIssues += Child->GetTotalNumberOfErrorIssues();

		if (Child->GetIssues().Num() > 0)
		{
			ChildrenWithIssues.Add(Child);
		}
		ChildrenWithIssues.Append(Child->GetAllChildrenWithIssues());
	}
	
	// Stack issues refresh
	NewStackIssues.RemoveAll([=](const FStackIssue& Issue) { return Issue.GetCanBeDismissed() && GetStackEditorData().GetDismissedStackIssueIds().Contains(Issue.GetUniqueIdentifier()); }); 

	for (const FStackIssue& Issue : NewStackIssues)
	{
		if (Issue.GetSeverity() == EStackIssueSeverity::Info)
		{
			TotalNumberOfInfoIssues++;
		}
		else if (Issue.GetSeverity() == EStackIssueSeverity::Warning)
		{
			TotalNumberOfWarningIssues++;
		}
		else if (Issue.GetSeverity() == EStackIssueSeverity::Error)
		{
			TotalNumberOfErrorIssues++;
		}
	}

	StackIssues.Empty();
	StackIssues.Append(NewStackIssues);
	RefreshStackErrorChildren();
	for (UNiagaraStackErrorItem* ErrorChild : ErrorChildren)
	{
		ErrorChild->IndentLevel = GetChildIndentLevel();
		ErrorChild->RefreshChildren();
		ErrorChild->OnStructureChanged().AddUObject(this, &UNiagaraStackEntry::ChildStructureChanged);
		ErrorChild->OnDataObjectModified().AddUObject(this, &UNiagaraStackEntry::ChildDataObjectModified);
		ErrorChild->OnRequestFullRefresh().AddUObject(this, &UNiagaraStackEntry::ChildRequestFullRefresh);
		ErrorChild->OnRequestFullRefreshDeferred().AddUObject(this, &UNiagaraStackEntry::ChildRequestFullRefreshDeferred);
		ErrorChild->OnIssueModified().AddUObject(this, &UNiagaraStackEntry::IssueModified);
	}

	const FText* NewAlternateName = StackEditorData->GetStackEntryDisplayName(StackEditorDataKey);
	if (NewAlternateName != nullptr && NewAlternateName->IsEmptyOrWhitespace() == false)
	{
		if (AlternateDisplayName.IsSet() == false || NewAlternateName->IdenticalTo(AlternateDisplayName.GetValue()) == false)
		{
			AlternateDisplayName = *NewAlternateName;
			AlternateDisplayNameChangedDelegate.Broadcast();
		}
	}
	else if(AlternateDisplayName.IsSet())
	{
		AlternateDisplayName.Reset();
		AlternateDisplayNameChangedDelegate.Broadcast();
	}

	PostRefreshChildrenInternal();

	StructureChangedDelegate.Broadcast();
}

void UNiagaraStackEntry::RefreshStackErrorChildren()
{
	// keep the error entries that are already built
	TArray<UNiagaraStackErrorItem*> NewErrorChildren;
	for (FStackIssue Issue : StackIssues)
	{
		UNiagaraStackErrorItem* ErrorEntry = nullptr;
		UNiagaraStackErrorItem** Found = ErrorChildren.FindByPredicate(
			[&](UNiagaraStackErrorItem* CurrentChild) { return CurrentChild->GetStackIssue().GetUniqueIdentifier() == Issue.GetUniqueIdentifier(); });
		if (Found == nullptr)
		{
			ErrorEntry = NewObject<UNiagaraStackErrorItem>(this);
			ErrorEntry->Initialize(CreateDefaultChildRequiredData(), Issue, GetStackEditorDataKey());
		}
		else
		{
			ErrorEntry = *Found;
			ErrorEntry->SetStackIssue(Issue); // we found the entry by id but we want to properly refresh the subentries of the issue (specifically its fixes), too
		}
		if (ensureMsgf(NewErrorChildren.Contains(ErrorEntry) == false,
			TEXT("Duplicate stack issue rows detected, this is caused by two different issues generating the same unique id. Issue Short description: %s Issue Long description: %s.  This issue will not be shown in the UI."),
			*Issue.GetShortDescription().ToString(),
			*Issue.GetLongDescription().ToString()))
		{
			NewErrorChildren.Add(ErrorEntry);
		}
	}

	// If any of the current error children were not moved to the new error children collection than finalize them since
	// they weren't reused.
	for (UNiagaraStackEntry* ErrorChild : ErrorChildren)
	{
		if(NewErrorChildren.Contains(ErrorChild) == false)
		{
			ErrorChild->Finalize();
		}
	}

	ErrorChildren.Empty();
	ErrorChildren.Append(NewErrorChildren);
}

void UNiagaraStackEntry::IssueModified()
{
	if (bIsFinalized == false)
	{
		// Fixing an issue may have caused this entry to be deleted and finalized, so don't refresh in that case.
		RefreshChildren();
	}
}

void UNiagaraStackEntry::BeginDestroy()
{
	ensureMsgf(HasAnyFlags(RF_ClassDefaultObject) || bIsFinalized, TEXT("Stack entry being destroyed but it was not finalized."));
	Super::BeginDestroy();
}

void UNiagaraStackEntry::RefreshChildrenInternal(const TArray<UNiagaraStackEntry*>& CurrentChildren, TArray<UNiagaraStackEntry*>& NewChildren, TArray<FStackIssue>& NewIssues)
{
}

void UNiagaraStackEntry::PostRefreshChildrenInternal()
{
}

UNiagaraStackEntry::FRequiredEntryData UNiagaraStackEntry::CreateDefaultChildRequiredData() const
{
	return FRequiredEntryData(GetSystemViewModel(), GetEmitterViewModel(), GetExecutionCategoryName(), GetExecutionSubcategoryName(), GetStackEditorData());
}

int32 UNiagaraStackEntry::GetChildIndentLevel() const
{
	return GetShouldShowInStack() ? GetIndentLevel() + 1 : GetIndentLevel();
}

void UNiagaraStackEntry::ChildStructureChanged()
{
	ChildStructureChangedInternal();
	StructureChangedDelegate.Broadcast();
}

void UNiagaraStackEntry::ChildDataObjectModified(UObject* ChangedObject)
{
	DataObjectModifiedDelegate.Broadcast(ChangedObject);
}

void UNiagaraStackEntry::ChildRequestFullRefresh()
{
	RequestFullRefreshDelegate.Broadcast();
}

void UNiagaraStackEntry::ChildRequestFullRefreshDeferred()
{
	RequestFullRefreshDeferredDelegate.Broadcast();
}

TOptional<UNiagaraStackEntry::FDropRequestResponse> UNiagaraStackEntry::ChildRequestCanDrop(const UNiagaraStackEntry& TargetChild, const FDropRequest& DropRequest)
{
	TOptional<FDropRequestResponse> Response = ChildRequestCanDropInternal(TargetChild, DropRequest);
	if (Response.IsSet())
	{
		return Response;
	}
	else
	{
		return OnRequestCanDropDelegate.IsBound()
			? OnRequestCanDropDelegate.Execute(TargetChild, DropRequest)
			: TOptional<FDropRequestResponse>();
	}
}

TOptional<UNiagaraStackEntry::FDropRequestResponse> UNiagaraStackEntry::ChildRequestDrop(const UNiagaraStackEntry& TargetChild, const FDropRequest& DropRequest)
{
	TOptional<FDropRequestResponse> Response = ChildRequestDropInternal(TargetChild, DropRequest);
	if (Response.IsSet())
	{
		return Response;
	}
	else
	{
		return OnRequestDropDelegate.IsBound()
			? OnRequestDropDelegate.Execute(TargetChild, DropRequest)
			: TOptional<FDropRequestResponse>();
	}
}

bool UNiagaraStackEntry::GetIsRenamePending() const
{
	return SupportsRename() && GetStackEditorData().GetStackEntryIsRenamePending(StackEditorDataKey);
}

void UNiagaraStackEntry::SetIsRenamePending(bool bIsRenamePending)
{
	if (SupportsRename())
	{
		GetStackEditorData().SetStackEntryIsRenamePending(StackEditorDataKey, bIsRenamePending);
	}
}

void UNiagaraStackEntry::OnRenamed(FText NewName)
{
	if (SupportsRename())
	{
		if (!NewName.EqualTo(AlternateDisplayName.Get(FText::GetEmpty())))
		{
			if (NewName.IsEmptyOrWhitespace() || NewName.EqualTo(GetDisplayName()))
			{
				AlternateDisplayName.Reset();
			}
			else
			{
				AlternateDisplayName = NewName;
			}

			FScopedTransaction ScopedTransaction(NSLOCTEXT("NiagaraStackEntry", "RenameModule", "Rename Module"));
			
			GetStackEditorData().Modify();
			GetStackEditorData().SetStackEntryDisplayName(GetStackEditorDataKey(), AlternateDisplayName.Get(FText::GetEmpty()));

			AlternateDisplayNameChangedDelegate.Broadcast();
		}
	}
}
