// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NiagaraStackEntry.generated.h"

class FNiagaraSystemViewModel;
class FNiagaraEmitterViewModel;

UCLASS()
class NIAGARAEDITOR_API UNiagaraStackEntry : public UObject
{
	GENERATED_BODY()

public:
	DECLARE_MULTICAST_DELEGATE(FOnStructureChanged);
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnDataObjectModified, UObject*);
	DECLARE_DELEGATE_RetVal_OneParam(bool, FOnFilterChild, const UNiagaraStackEntry&);

public:
	UNiagaraStackEntry();

	void Initialize(TSharedRef<FNiagaraSystemViewModel> InSystemViewModel, TSharedRef<FNiagaraEmitterViewModel> InEmitterViewModel);

	virtual FText GetDisplayName() const;

	virtual FText GetTooltipText() const;

	virtual FName GetTextStyleName() const;

	virtual bool GetCanExpand() const;

	virtual bool IsExpandedByDefault() const;

	virtual bool GetIsExpanded() const;

	virtual void SetIsExpanded(bool bInExpanded);

	virtual FName GetGroupBackgroundName() const;

	virtual FName GetGroupForegroundName() const;

	virtual FName GetItemBackgroundName() const;

	virtual FName GetItemForegroundName() const;

	virtual int32 GetItemIndentLevel() const;

	virtual bool GetShouldShowInStack() const;

	void GetFilteredChildren(TArray<UNiagaraStackEntry*>& OutFilteredChildren);

	void GetUnfilteredChildren(TArray<UNiagaraStackEntry*>& OutUnfilteredChildren);

	FOnStructureChanged& OnStructureChanged();

	FOnDataObjectModified& OnDataObjectModified();

	void RefreshChildren();
	void RefreshErrors();

	FDelegateHandle AddChildFilter(FOnFilterChild ChildFilter);
	void RemoveChildFilter(FDelegateHandle FilterHandle);

	virtual int32 GetErrorCount() const { return 0; }
	virtual bool GetErrorFixable(int32 ErrorIdx) const { return false; }
	virtual bool TryFixError(int32 ErrorIdx) { return false; }
	virtual FText GetErrorText(int32 ErrorIdx) const { return FText(); }
	virtual FText GetErrorSummaryText(int32 ErrorIdx) const { return FText(); }

	TSharedRef<FNiagaraSystemViewModel> GetSystemViewModel() const;
	TSharedRef<FNiagaraEmitterViewModel> GetEmitterViewModel() const;

	template<typename ChildType, typename PredicateType>
	static ChildType* FindCurrentChildOfTypeByPredicate(const TArray<UNiagaraStackEntry*>& CurrentChildren, PredicateType Predicate)
	{
		for (UNiagaraStackEntry* CurrentChild : CurrentChildren)
		{
			ChildType* TypedCurrentChild = Cast<ChildType>(CurrentChild);
			if (TypedCurrentChild != nullptr && Predicate(TypedCurrentChild))
			{
				return TypedCurrentChild;
			}
		}
		return nullptr;
	}

	template<typename ChildType, typename PredicateType>
	ChildType* FindChildOfTypeByPredicate(PredicateType Predicate)
	{
		TArray<UNiagaraStackEntry*> CurrentChildren;
		GetUnfilteredChildren(CurrentChildren);
		return FindCurrentChildOfTypeByPredicate<ChildType>(CurrentChildren, Predicate);
	}

protected:
	virtual void RefreshChildrenInternal(const TArray<UNiagaraStackEntry*>& CurrentChildren, TArray<UNiagaraStackEntry*>& NewChildren);

private:
	void ChildStructureChanged();
	
	void ChildDataObjectModified(UObject* ChangedObject);

private:
	TWeakPtr<FNiagaraSystemViewModel> SystemViewModel;
	TWeakPtr<FNiagaraEmitterViewModel> EmitterViewModel;

	FOnStructureChanged StructureChangedDelegate;

	FOnDataObjectModified DataObjectModifiedDelegate;

	TArray<FOnFilterChild> ChildFilters;

	UPROPERTY()
	TArray<UNiagaraStackEntry*> Children;

	UPROPERTY()
	TArray<UNiagaraStackEntry*> ErrorChildren;

	bool bIsExpanded;
};