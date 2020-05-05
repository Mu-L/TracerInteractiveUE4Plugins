// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "../Private/NiagaraHlslTranslator.h"
#include "UObject/ObjectMacros.h"
#include "EdGraph/EdGraphNode.h"
#include "NiagaraEditorCommon.h"
#include "NiagaraParameterMapHistory.h"
#include "Misc/Guid.h"
#include "UObject/UnrealType.h"
#include "NiagaraNode.generated.h"

class UEdGraphPin;
class INiagaraCompiler;
struct FNiagaraGraphFunctionAliasContext;
class FSHA1;

UCLASS()
class NIAGARAEDITOR_API UNiagaraNode : public UEdGraphNode
{
	GENERATED_UCLASS_BODY()
protected:

	bool ReallocatePins(bool bMarkNeedsResynchronizeOnChange = true);

	bool CompileInputPins(class FHlslNiagaraTranslator *Translator, TArray<int32>& OutCompiledInputs);

public:

	DECLARE_MULTICAST_DELEGATE_OneParam(FOnNodeVisualsChanged, UNiagaraNode*);

	//~ Begin UObject interface
	virtual void PostLoad() override;
	//~ End UObject interface

	//~ Begin EdGraphNode Interface
	virtual void PostPlacedNewNode() override;
	virtual void AutowireNewNode(UEdGraphPin* FromPin)override;	
	virtual void PinDefaultValueChanged(UEdGraphPin* Pin) override;
	virtual void PinConnectionListChanged(UEdGraphPin* Pin) override;
	virtual void PinTypeChanged(UEdGraphPin* Pin) override;
	virtual void OnRenameNode(const FString& NewName) override;
	virtual void OnPinRemoved(UEdGraphPin* InRemovedPin) override;
	virtual void NodeConnectionListChanged() override;	
	virtual TSharedPtr<SGraphNode> CreateVisualWidget() override; 
	virtual void GetPinHoverText(const UEdGraphPin& Pin, FString& HoverTextOut) const override;
	virtual void GetNodeContextMenuActions(class UToolMenu* Menu, class UGraphNodeContextMenuContext* Context) const override;
	virtual bool CanCreateUnderSpecifiedSchema(const UEdGraphSchema* Schema) const override;
	//~ End EdGraphNode Interface

	/** Get the Niagara graph that owns this node */
	const class UNiagaraGraph* GetNiagaraGraph()const;
	class UNiagaraGraph* GetNiagaraGraph();

	/** Get the source object */
	class UNiagaraScriptSource* GetSource()const;

	/** Gets the asset referenced by this node, or nullptr if there isn't one. */
	virtual UObject* GetReferencedAsset() const { return nullptr; }

	/** Refreshes the node due to external changes, e.g. the underlying function changed for a function call node. Return true if the graph changed.*/
	virtual bool RefreshFromExternalChanges() { return false; }

	virtual void Compile(class FHlslNiagaraTranslator *Translator, TArray<int32>& Outputs);

	UEdGraphPin* GetInputPin(int32 InputIndex) const;
	void GetInputPins(TArray<class UEdGraphPin*>& OutInputPins) const;
	void GetInputPins(TArray<const class UEdGraphPin*>& OutInputPins) const;
	UEdGraphPin* GetOutputPin(int32 OutputIndex) const;
	void GetOutputPins(TArray<class UEdGraphPin*>& OutOutputPins) const;
	void GetOutputPins(TArray<const class UEdGraphPin*>& OutOutputPins) const;
	UEdGraphPin* GetPinByPersistentGuid(const FGuid& InGuid) const;
	virtual void ResolveNumerics(const UEdGraphSchema_Niagara* Schema, bool bSetInline, TMap<TPair<FGuid, UEdGraphNode*>, FNiagaraTypeDefinition>* PinCache);

	/** Apply any node-specific logic to determine if it is safe to add this node to the graph. This is meant to be called only in the Editor before placing the node.*/
	virtual bool CanAddToGraph(UNiagaraGraph* TargetGraph, FString& OutErrorMsg) const;

	/** Gets which mode to use when deducing the type of numeric output pins from the types of the input pins. */
	virtual ENiagaraNumericOutputTypeSelectionMode GetNumericOutputTypeSelectionMode() const;

	/** Convert the type of an existing numeric pin to a more known type.*/
	virtual bool ConvertNumericPinToType(UEdGraphPin* InGraphPin, FNiagaraTypeDefinition TypeDef);

	/** Determine if there are any external dependencies wrt to scripts and ensure that those dependencies are sucked into the existing package.*/
	virtual void SubsumeExternalDependencies(TMap<const UObject*, UObject*>& ExistingConversions) {};

	/** Determine whether or not a pin should be renamable. */
	virtual bool IsPinNameEditable(const UEdGraphPin* GraphPinObj) const { return false; }

	/** Determine whether or not a specific pin should immediately be opened for rename.*/
	virtual bool IsPinNameEditableUponCreation(const UEdGraphPin* GraphPinObj) const { return false; }

	/** Verify that the potential rename has produced acceptable results for a pin.*/
	virtual bool VerifyEditablePinName(const FText& InName, FText& OutErrorMessage, const UEdGraphPin* InGraphPinObj) const { return false; }

	/** Verify that the potential rename has produced acceptable results for a pin.*/
	virtual bool CommitEditablePinName(const FText& InName,  UEdGraphPin* InGraphPinObj, bool bSuppressEvents = false) { return false; }
	/** Notify the rename was cancelled.*/
	virtual bool CancelEditablePinName(const FText& InName, UEdGraphPin* InGraphPinObj) { return false; }

	/** Returns whether or not the supplied pin has a rename pending. */
	bool GetIsPinRenamePending(const UEdGraphPin* Pin);

	/** Sets whether or not the supplied pin has a rename pending. */
	void SetIsPinRenamePending(const UEdGraphPin* Pin, bool bInIsRenamePending);

	virtual void BuildParameterMapHistory(FNiagaraParameterMapHistoryBuilder& OutHistory, bool bRecursive = true, bool bFilterForCompilation = true) const;
	
	/** Go through all the external dependencies of this node in isolation and add them to the reference id list.*/
	virtual void GatherExternalDependencyData(ENiagaraScriptUsage InMasterUsage, const FGuid& InMasterUsageId, TArray<FNiagaraCompileHash>& InReferencedCompileHashes, TArray<FString>& InReferencedObjs) const {};

	/** Traces one of this node's output pins to its source output pin if it is a reroute node output pin.*/
	virtual UEdGraphPin* GetTracedOutputPin(UEdGraphPin* LocallyOwnedOutputPin) const {return LocallyOwnedOutputPin;}
	static UEdGraphPin* TraceOutputPin(UEdGraphPin* LocallyOwnedOutputPin, bool bFilterForCompilation = true);

	/** Allows a node to replace a pin that is about to be compiled with another pin. This can be used for either optimizations or features such as the static switch. Returns true if the pin was successfully replaced, false otherwise. */
	virtual bool SubstituteCompiledPin(FHlslNiagaraTranslator* Translator, UEdGraphPin** LocallyOwnedPin);

	virtual UEdGraphPin* GetPassThroughPin(const UEdGraphPin* LocallyOwnedOutputPin) const override { return nullptr; }
	virtual UEdGraphPin* GetPassThroughPin(const UEdGraphPin* LocallyOwnedOutputPin, ENiagaraScriptUsage MasterUsage) const { return nullptr; }

	/** Identify that this node has undergone changes that will require synchronization with a compiled script.*/
	void MarkNodeRequiresSynchronization(FString Reason, bool bRaiseGraphNeedsRecompile);

	/** Get the change id for this node. This change id is updated whenever the node is manipulated in a way that should force a recompile.*/
	const FGuid& GetChangeId() const { return ChangeId; }

	/** Set the change id for his node to an explicit value. This should only be called by internal code. */
	void ForceChangeId(const FGuid& InId, bool bRaiseGraphNeedsRecompile);

	FOnNodeVisualsChanged& OnVisualsChanged();

	virtual void AppendFunctionAliasForContext(const FNiagaraGraphFunctionAliasContext& InFunctionAliasContext, FString& InOutFunctionAlias) { };

	/** Old style compile hash code. To be removed in the future.*/
	virtual void UpdateCompileHashForNode(FSHA1& HashState) const;

	/** Entry point for generating the compile hash.*/
	bool AppendCompileHash(FNiagaraCompileHashVisitor* InVisitor) const;

protected:
	/** Go through all class members for a given UClass on this object and hash them into the visitor.*/
	virtual bool GenerateCompileHashForClassMembers(const UClass* InClass, FNiagaraCompileHashVisitor* InVisitor) const;

	/** Write out the specific entries for UNiagaraNode into the visitor hash. */
	bool NiagaraNodeAppendCompileHash(FNiagaraCompileHashVisitor* InVisitor) const;

	/** Write out the specific entries of this pin to the visitor hash.*/
	virtual bool PinAppendCompileHash(const UEdGraphPin* InPin, FNiagaraCompileHashVisitor* InVisitor) const;
	
	/** Helper function to hash arbitrary UProperty entries (Arrays, Maps, Structs, etc).*/
	virtual bool NestedPropertiesAppendCompileHash(const void* Container, const UStruct* Struct, EFieldIteratorFlags::SuperClassFlags IteratorFlags, const FString& BaseName, FNiagaraCompileHashVisitor* InVisitor) const;
	
	/** For a simple Plain old data type UProperty, hash the data.*/
	virtual bool PODPropertyAppendCompileHash(const void* Container, FProperty* Property, const FString& PropertyName, FNiagaraCompileHashVisitor* InVisitor) const;

	virtual int32 CompileInputPin(class FHlslNiagaraTranslator *Translator, UEdGraphPin* Pin);
	virtual bool IsValidPinToCompile(UEdGraphPin* Pin) const;

	void NumericResolutionByPins(const UEdGraphSchema_Niagara* Schema, TArray<UEdGraphPin*>& InputPins, TArray<UEdGraphPin*>& OutputPins,
		bool bFixInline, TMap<TPair<FGuid, UEdGraphNode*>, FNiagaraTypeDefinition>* PinCache);

	/** Route input parameter map to output parameter map if it exists. Note that before calling this function,
		the input pins should have been visited already.*/
	virtual void RouteParameterMapAroundMe(FNiagaraParameterMapHistoryBuilder& OutHistory, bool bRecursive) const;
	
#if WITH_EDITORONLY_DATA
	virtual void GatherForLocalization(FPropertyLocalizationDataGatherer& PropertyLocalizationDataGatherer, const EPropertyLocalizationGathererTextFlags GatherTextFlags) const override;
#endif

	/** The current change identifier for this node. Used to sync status with UNiagaraScripts.*/
	UPROPERTY()
	FGuid ChangeId;

	FOnNodeVisualsChanged VisualsChangedDelegate;

	TArray<FGuid> PinsGuidsWithRenamePending;
};
