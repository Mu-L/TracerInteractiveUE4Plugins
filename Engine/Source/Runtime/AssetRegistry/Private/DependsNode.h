// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AssetData.h"
#include "Misc/AssetRegistryInterface.h"

#define USE_DEPENDS_NODE_LIST_SETS (WITH_EDITOR || IS_PROGRAM)

#if USE_DEPENDS_NODE_LIST_SETS
typedef TSet<class FDependsNode*> FDependsNodeList;
#else
typedef TArray<class FDependsNode*> FDependsNodeList;
#endif

/** Implementation of IDependsNode */
class FDependsNode
{
public:
	FDependsNode() {}
	FDependsNode(const FAssetIdentifier& InIdentifier) : Identifier(InIdentifier) {}

	/** Prints the dependencies and referencers for this node to the log */
	void PrintNode() const;
	/** Prints the dependencies for this node to the log */
	void PrintDependencies() const;
	/** Prints the referencers to this node to the log */
	void PrintReferencers() const;
	/** Gets the list of dependencies for this node */
	void GetDependencies(TArray<FDependsNode*>& OutDependencies, EAssetRegistryDependencyType::Type InDependencyType = EAssetRegistryDependencyType::All) const;
	/** Gets the list of dependency names for this node */
	void GetDependencies(TArray<FAssetIdentifier>& OutDependencies, EAssetRegistryDependencyType::Type InDependencyType = EAssetRegistryDependencyType::All) const;
	/** Gets the list of referencers to this node */
	void GetReferencers(TArray<FDependsNode*>& OutReferencers, EAssetRegistryDependencyType::Type InDependencyType = EAssetRegistryDependencyType::All) const;
	/** Gets the name of the package that this node represents */
	FName GetPackageName() const { return Identifier.PackageName; }
	/** Sets the name of the package that this node represents */
	void SetPackageName(FName InName) { Identifier = FAssetIdentifier(InName); }
	/** Returns the entire identifier */
	const FAssetIdentifier& GetIdentifier() const { return Identifier; }
	/** Sets the entire identifier */
	void SetIdentifier(const FAssetIdentifier& InIdentifier) { Identifier = InIdentifier;  }
	/** Add a dependency to this node */
	void AddDependency(FDependsNode* InDependency, EAssetRegistryDependencyType::Type InDependencyType = EAssetRegistryDependencyType::Hard, bool bGuaranteedUnique = false);
	/** Add a referencer to this node */
	void AddReferencer(FDependsNode* InReferencer, bool bGuaranteedUnique = false);
	/** Remove a dependency from this node */
	void RemoveDependency(FDependsNode* InDependency);
	/** Remove a referencer from this node */
	void RemoveReferencer(FDependsNode* InReferencer);
	/** Clear all dependency records from this node */
	void ClearDependencies();
	/** Removes Manage dependencies on this node and clean up referencers array. Manage references are the only ones safe to remove at runtime */
	void RemoveManageReferencesToNode();
	/** Returns number of connections this node has, both references and dependencies */
	int32 GetConnectionCount() const;
	/** Returns amount of memory used by the arrays */
	uint32 GetAllocatedSize(void) const
	{
		return HardDependencies.GetAllocatedSize() + SoftDependencies.GetAllocatedSize() + NameDependencies.GetAllocatedSize() + SoftManageDependencies.GetAllocatedSize() + HardManageDependencies.GetAllocatedSize() + Referencers.GetAllocatedSize();
	}

	/** Iterate over all the dependencies of this node, filtered by the supplied type parameter, and call the supplied lambda parameter on the record */
	template <class T>
	void IterateOverDependencies(T InCallback, EAssetRegistryDependencyType::Type InDependencyType = EAssetRegistryDependencyType::All) const
	{
		IterateOverDependencyLists([&InCallback](const FDependsNodeList& InList, EAssetRegistryDependencyType::Type CurrentType)
		{
			for (FDependsNode* Dependency : InList)
			{
				InCallback(Dependency, CurrentType);
			}
		}, InDependencyType);
	}

	/** Iterate over all the referencers of this node and call the supplied lambda parameter on the record */
	template <class T>
	void IterateOverReferencers(T InCallback) const
	{
		for (FDependsNode* Referencer : Referencers)
		{
			InCallback(Referencer);
		}
	}

	void Reserve(int32 InNumHardDependencies, int32 InNumSoftDependencies, int32 InNumNameDependencies, int32 InNumSoftManageDependencies, int32 InNumHardManageDependencies, int32 InNumReferencers)
	{
		HardDependencies.Reserve(InNumHardDependencies);
		SoftDependencies.Reserve(InNumSoftDependencies);
		NameDependencies.Reserve(InNumNameDependencies);
		SoftManageDependencies.Reserve(InNumSoftManageDependencies);
		HardManageDependencies.Reserve(InNumHardManageDependencies);
		Referencers.Reserve(InNumReferencers);
	}

	void Reserve(const FDependsNode* Other) {
		Reserve(Other->HardDependencies.Num(), Other->SoftDependencies.Num(), Other->NameDependencies.Num(), Other->SoftManageDependencies.Num(), Other->HardManageDependencies.Num(), Other->Referencers.Num());
	}

private:

	/** Iterate over all the separate dependency arrays. Const cast to avoid duplication */
	template <class T>
	FORCEINLINE void IterateOverDependencyLists(T InCallback, EAssetRegistryDependencyType::Type InDependencyType = EAssetRegistryDependencyType::All) const
	{
		// This iteration is specific so it gets the "most important" references first in the array
		if (InDependencyType & EAssetRegistryDependencyType::Hard)
		{
			InCallback(const_cast<FDependsNodeList&>(HardDependencies), EAssetRegistryDependencyType::Hard);
		}

		if (InDependencyType & EAssetRegistryDependencyType::Soft)
		{
			InCallback(const_cast<FDependsNodeList&>(SoftDependencies), EAssetRegistryDependencyType::Soft);
		}

		if (InDependencyType & EAssetRegistryDependencyType::HardManage)
		{
			InCallback(const_cast<FDependsNodeList&>(HardManageDependencies), EAssetRegistryDependencyType::HardManage);
		}

		if (InDependencyType & EAssetRegistryDependencyType::SoftManage)
		{
			InCallback(const_cast<FDependsNodeList&>(SoftManageDependencies), EAssetRegistryDependencyType::SoftManage);
		}

		if (InDependencyType & EAssetRegistryDependencyType::SearchableName)
		{
			InCallback(const_cast<FDependsNodeList&>(NameDependencies), EAssetRegistryDependencyType::SearchableName);
		}
	}

	/** Recursively prints dependencies of the node starting with the specified indent. VisitedNodes should be an empty set at first which is populated recursively. */
	void PrintDependenciesRecursive(const FString& Indent, TSet<const FDependsNode*>& VisitedNodes) const;
	/** Recursively prints referencers to the node starting with the specified indent. VisitedNodes should be an empty set at first which is populated recursively. */
	void PrintReferencersRecursive(const FString& Indent, TSet<const FDependsNode*>& VisitedNodes) const;

	/** The name of the package/object this node represents */
	FAssetIdentifier Identifier;
	/** The list of hard dependencies for this node */
	FDependsNodeList HardDependencies;
	/** The list of soft dependencies for this node */
	FDependsNodeList SoftDependencies;
	/** The list of searchable name dependencies for this node */
	FDependsNodeList NameDependencies;
	/** The list of hard manage dependencies for this node */
	FDependsNodeList SoftManageDependencies;
	/** The list of soft manage dependencies for this node */
	FDependsNodeList HardManageDependencies;
	/** The list of referencers to this node */
	FDependsNodeList Referencers;
};
