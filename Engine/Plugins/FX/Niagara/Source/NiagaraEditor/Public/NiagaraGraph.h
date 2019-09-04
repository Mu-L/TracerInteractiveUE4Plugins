// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "NiagaraCommon.h"
#include "EdGraph/EdGraph.h"
#include "NiagaraScript.h"
#include "Misc/SecureHash.h"
#include "NiagaraParameterStore.h"
#include "NiagaraGraph.generated.h"

class UNiagaraScriptVariable;

/** This is the type of action that occurred on a given Niagara graph. Note that this should follow from EEdGraphActionType, leaving some slop for growth. */
enum ENiagaraGraphActionType
{
	GRAPHACTION_GenericNeedsRecompile = 0x1 << 16,
};

class UNiagaraNode;
class UNiagaraGraph;

typedef TPair<FGuid, TWeakObjectPtr<UNiagaraNode>> FNiagaraGraphParameterReference;

struct FNiagaraGraphParameterReferenceCollection
{
public:
	FNiagaraGraphParameterReferenceCollection(const bool bInCreated = false);

	/** All the references in the graph. */
	TArray<FNiagaraGraphParameterReference> ParameterReferences;

	const UNiagaraGraph* Graph;

	/** Returns true if this parameter was initially created by the user. */
	bool WasCreated() const;

private:
	/** Whether this parameter was initially created by the user. */
	bool bCreated;
};

/** Container for UNiagaraGraph cached data for managing CompileIds and Traversals.*/
USTRUCT()
struct FNiagaraGraphScriptUsageInfo
{
	GENERATED_USTRUCT_BODY()

public:
	FNiagaraGraphScriptUsageInfo();

	/** A guid which is generated when this usage info is created.  Allows for forced recompiling when the cached ids are invalidated. */
	UPROPERTY()
	FGuid BaseId;

	/** The context in which this sub-graph traversal will be used.*/
	UPROPERTY()
	ENiagaraScriptUsage UsageType;
	
	/** The particular instance of the usage type. Event scripts, for example, have potentially multiple graphs.*/
	UPROPERTY()
	FGuid UsageId;

	/** The compile ID last associated with this traversal.*/
	UPROPERTY()
	FGuid GeneratedCompileId;

	/** The hash that we calculated last traversal. */
	UPROPERTY()
	FNiagaraCompileHash CompileHash;

	/** The traversal of output to input nodes for this graph. This is not a recursive traversal, it just includes nodes from this graph.*/
	UPROPERTY()
	TArray<UNiagaraNode*> Traversal;

	void PostLoad(UObject* Owner);

private:
	UPROPERTY()
	TArray<uint8> DataHash_DEPRECATED;
};

struct FNiagaraGraphFunctionAliasContext
{
	ENiagaraScriptUsage CompileUsage;
	TArray<UEdGraphPin*> StaticSwitchValues;
};

UCLASS(MinimalAPI)
class UNiagaraGraph : public UEdGraph
{
	GENERATED_UCLASS_BODY()

	DECLARE_MULTICAST_DELEGATE(FOnDataInterfaceChanged);

	//~ Begin UObject Interface
	virtual void PostLoad() override;
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
	//~ End UObjet Interface
	
	/** Get the source that owns this graph */
	class UNiagaraScriptSource* GetSource() const;

	/** Determine if there are any nodes in this graph.*/
	bool IsEmpty() const { return Nodes.Num() == 0; }
			
	/** Find the first output node bound to the target usage type.*/
	class UNiagaraNodeOutput* FindOutputNode(ENiagaraScriptUsage TargetUsageType, FGuid TargetUsageId = FGuid()) const;
	class UNiagaraNodeOutput* FindEquivalentOutputNode(ENiagaraScriptUsage TargetUsageType, FGuid TargetUsageId = FGuid()) const;

	/** Find all output nodes.*/
	void FindOutputNodes(TArray<UNiagaraNodeOutput*>& OutputNodes) const;
	void FindOutputNodes(ENiagaraScriptUsage TargetUsageType, TArray<UNiagaraNodeOutput*>& OutputNodes) const;
	void FindEquivalentOutputNodes(ENiagaraScriptUsage TargetUsageType, TArray<UNiagaraNodeOutput*>& OutputNodes) const;

	/** Options for the FindInputNodes function */
	struct FFindInputNodeOptions
	{
		FFindInputNodeOptions()
			: bSort(false)
			, bIncludeParameters(true)
			, bIncludeAttributes(true)
			, bIncludeSystemConstants(true)
			, bIncludeTranslatorConstants(false)
			, bFilterDuplicates(false)
			, bFilterByScriptUsage(false)
			, TargetScriptUsage(ENiagaraScriptUsage::Function)
		{
		}

		/** Whether or not to sort the nodes, defaults to false. */
		bool bSort;
		/** Whether or not to include parameters, defaults to true. */
		bool bIncludeParameters;
		/** Whether or not to include attributes, defaults to true. */
		bool bIncludeAttributes;
		/** Whether or not to include system parameters, defaults to true. */
		bool bIncludeSystemConstants;
		/** Whether or not to include translator parameters, defaults to false. */
		bool bIncludeTranslatorConstants;
		/** Whether of not to filter out duplicate nodes, defaults to false. */
		bool bFilterDuplicates;
		/** Whether or not to limit to nodes connected to an output node of the specified script type.*/
		bool bFilterByScriptUsage;
		/** The specified script usage required for an input.*/
		ENiagaraScriptUsage TargetScriptUsage;
		/** The specified id within the graph of the script usage*/
		FGuid TargetScriptUsageId;
	};

	/** Finds input nodes in the graph with. */
	void FindInputNodes(TArray<class UNiagaraNodeInput*>& OutInputNodes, FFindInputNodeOptions Options = FFindInputNodeOptions()) const;

	/** Returns a list of variable inputs for all static switch nodes in the graph. */
	TArray<FNiagaraVariable> FindStaticSwitchInputs(bool bReachableOnly = false) const;

	/** Get an in-order traversal of a graph by the specified target output script usage.*/
	void BuildTraversal(TArray<class UNiagaraNode*>& OutNodesTraversed, ENiagaraScriptUsage TargetUsage, FGuid TargetUsageId) const;
	static void BuildTraversal(TArray<class UNiagaraNode*>& OutNodesTraversed, UNiagaraNode* FinalNode);

	/** Generates a list of unique input and output parameters for when this script is used as a function. */
	void GetParameters(TArray<FNiagaraVariable>& Inputs, TArray<FNiagaraVariable>& Outputs) const;

	/** Returns the index of this variable in the output node of the graph. INDEX_NONE if this is not a valid attribute. */
	int32 GetOutputNodeVariableIndex(const FNiagaraVariable& Attr)const;
	void GetOutputNodeVariables(TArray< FNiagaraVariable >& OutAttributes)const;
	void GetOutputNodeVariables(ENiagaraScriptUsage InTargetScriptUsage, TArray< FNiagaraVariable >& OutAttributes)const;

	bool HasNumericParameters()const;

	bool HasParameterMapParameters()const;

	/** Signal to listeners that the graph has changed */
	void NotifyGraphNeedsRecompile();
		
	/** Notifies the graph that a contained data interface has changed. */
	void NotifyGraphDataInterfaceChanged();

	/** Get all referenced graphs in this specified graph, including this graph. */
	void GetAllReferencedGraphs(TArray<const UNiagaraGraph*>& Graphs) const;

	/** Gather all the change ids of external references for this specific graph traversal.*/
	void GatherExternalDependencyIDs(ENiagaraScriptUsage InUsage, const FGuid& InUsageId, TArray<FNiagaraCompileHash>& InReferencedCompileHashes, TArray<FGuid>& InReferencedIDs, TArray<UObject*>& InReferencedObjs);

	/** Determine if there are any external dependencies wrt to scripts and ensure that those dependencies are sucked into the existing package.*/
	void SubsumeExternalDependencies(TMap<const UObject*, UObject*>& ExistingConversions);

	/** Determine if another item has been synchronized with this graph.*/
	bool IsOtherSynchronized(const FGuid& InChangeId) const;

	/** Identify that this graph has undergone changes that will require synchronization with a compiled script.*/
	void MarkGraphRequiresSynchronization(FString Reason);

	/** A change was made to the graph that external parties should take note of. The ChangeID will be updated.*/
	virtual void NotifyGraphChanged() override;

	/** Each graph is given a Change Id that occurs anytime the graph's content is manipulated. This key changing induces several important activities, including being a 
	value that third parties can poll to see if their cached handling of the graph needs to potentially adjust to changes. Furthermore, for script compilation we cache 
	the changes that were produced during the traversal of each output node, which are referred to as the CompileID.*/
	FGuid GetChangeID() { return ChangeId; }

	/** Recomputes the current compile id associated with the output node traversal specified by InUsage and InUsageId. If the usage is not found, an invalid Guid is returned.*/
	FGuid ComputeCompileID(ENiagaraScriptUsage InUsage, const FGuid& InUsageId);

	/** Gets the current compile data hash associated with the output node traversal specified by InUsage and InUsageId. If the usage is not found, an invalid hash is returned.*/
	FNiagaraCompileHash GetCompileDataHash(ENiagaraScriptUsage InUsage, const FGuid& InUsageId) const;

	/** Gets the current base id associated with the output node traversal specified by InUsage and InUsageId. If the usage is not found, an invalid guid is returned. */
	FGuid GetBaseId(ENiagaraScriptUsage InUsage, const FGuid& InUsageId) const;

	/** Forces the base compile id for the supplied script.  This should only be used to keep things consistent after an emitter merge. */
	void ForceBaseId(ENiagaraScriptUsage InUsage, const FGuid& InUsageId, const FGuid InForcedBaseId);

	/** Walk through the graph for an ParameterMapGet nodes and see if any of them specify a default for VariableName.*/
	UEdGraphPin* FindParameterMapDefaultValuePin(const FName VariableName, ENiagaraScriptUsage InUsage, ENiagaraScriptUsage InParentUsage) const;

	/** Gets the meta-data associated with this variable, if it exists.*/
	TOptional<FNiagaraVariableMetaData> GetMetaData(const FNiagaraVariable& InVar) const;

	/** Sets the meta-data associated with this variable.*/
	void SetMetaData(const FNiagaraVariable& InVar, const FNiagaraVariableMetaData& MetaData);

	const TMap<FNiagaraVariable, UNiagaraScriptVariable*>& GetAllMetaData() const;
	TMap<FNiagaraVariable, UNiagaraScriptVariable*>& GetAllMetaData();

	const TMap<FNiagaraVariable, FNiagaraGraphParameterReferenceCollection>& GetParameterReferenceMap() const;

	/** Adds parameter to parameters map setting it as created by the user.*/
	void AddParameter(const FNiagaraVariable& Parameter);

	/** Adds parameter to parameters map setting it as created by the user.*/
	void AddParameterReference(const FNiagaraVariable& Parameter, const UEdGraphPin* Pin);

	/** Remove parameter from map and all the pins associated. */
	void RemoveParameter(const FNiagaraVariable& Parameter);

	/** Rename parameter from map and all the pins associated. */
	bool RenameParameter(const FNiagaraVariable& Parameter, FName NewName);

	/** Gets a delegate which is called whenever a contained data interfaces changes. */
	FOnDataInterfaceChanged& OnDataInterfaceChanged();

	void SynchronizeInternalCacheWithGraph(UNiagaraGraph* Other);
	void InvalidateCachedCompileIds();

	/** Add a listener for OnGraphNeedsRecompile events */
	FDelegateHandle AddOnGraphNeedsRecompileHandler(const FOnGraphChanged::FDelegate& InHandler);

	/** Remove a listener for OnGraphNeedsRecompile events */
	void RemoveOnGraphNeedsRecompileHandler(FDelegateHandle Handle);

	FNiagaraTypeDefinition GetCachedNumericConversion(class UEdGraphPin* InPin);

	const class UEdGraphSchema_Niagara* GetNiagaraSchema() const;

	void InvalidateNumericCache();

	/** If this graph is the source of a function call, it can add a string to the function name to discern it from different
	  * function calls to the same graph. For example, if the graph contains static switches and two functions call it with
	  * different switch parameters, the final function names in the hlsl must be different.
	  */
	FString GetFunctionAliasByContext(const FNiagaraGraphFunctionAliasContext& FunctionAliasContext);

	void RebuildCachedCompileIds(bool bForce = false);

	void CopyCachedReferencesMap(UNiagaraGraph* TargetGraph);

protected:
	void RebuildNumericCache();
	bool bNeedNumericCacheRebuilt;
	TMap<TPair<FGuid, UEdGraphNode*>, FNiagaraTypeDefinition> CachedNumericConversions;
	void ResolveNumerics(TMap<UNiagaraNode*, bool>& VisitedNodes, UEdGraphNode* Node);

private:
	/** Remove any meta-data that is no longer being referenced within this graph.*/
	void PurgeUnreferencedMetaData() const;

	virtual void NotifyGraphChanged(const FEdGraphEditAction& InAction) override;

	/** Find parameters in the graph. */
	void RefreshParameterReferences() const;

	/** Marks the found parameter collections as invalid so they're rebuilt the next time they're requested. */
	void InvalidateCachedParameterData();

	/** A delegate that broadcasts a notification whenever the graph needs recompile due to structural change. */
	FOnGraphChanged OnGraphNeedsRecompile;

	/** Find all nodes in the graph that can be reached during compilation. */
	TArray<UEdGraphNode*> FindReachbleNodes() const;
	
	/** The current change identifier for this graph overall. Used to sync status with UNiagaraScripts.*/
	UPROPERTY()
	FGuid ChangeId;

	UPROPERTY()
	FGuid LastBuiltTraversalDataChangeId;

	UPROPERTY()
	TArray<FNiagaraGraphScriptUsageInfo> CachedUsageInfo;

	/** Storage of meta-data for variables defined for use explicitly with this graph.*/
	UPROPERTY()
	mutable TMap<FNiagaraVariable, FNiagaraVariableMetaData> VariableToMetaData_DEPRECATED;

	/** Storage of variables defined for use with this graph.*/
	UPROPERTY()
	mutable TMap<FNiagaraVariable, UNiagaraScriptVariable*> VariableToScriptVariable;
	
	/** A map of parameters in the graph to their referencers. */
	mutable TMap<FNiagaraVariable, FNiagaraGraphParameterReferenceCollection> ParameterToReferencesMap;

	FOnDataInterfaceChanged OnDataInterfaceChangedDelegate;

	/** Whether currently renaming a parameter to prevent recursion. */
	bool bIsRenamingParameter;

	mutable bool bParameterReferenceRefreshPending;

	mutable bool bUnreferencedMetaDataPurgePending;
};

