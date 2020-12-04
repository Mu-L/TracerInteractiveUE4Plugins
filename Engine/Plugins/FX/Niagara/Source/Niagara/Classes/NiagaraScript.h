// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HAL/ThreadSafeBool.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "NiagaraCommon.h"
#include "NiagaraScriptBase.h"
#include "NiagaraShared.h"
#include "NiagaraShader.h"
#include "NiagaraParameters.h"
#include "NiagaraDataSet.h"
#include "NiagaraScriptExecutionParameterStore.h"
#include "NiagaraScriptHighlight.h"

#include "NiagaraScript.generated.h"

class UNiagaraDataInterface;
class FNiagaraCompileRequestDataBase;
class UNiagaraConvertInPlaceUtilityBase;

#define NIAGARA_INVALID_MEMORY (0xBA)

#define NIAGARA_SCRIPT_COMPILE_LOGGING_MEDIUM

DECLARE_STATS_GROUP(TEXT("Niagara Detailed"), STATGROUP_NiagaraDetailed, STATCAT_Advanced);

/** Defines what will happen to unused attributes when a script is run. */
UENUM()
enum class EUnusedAttributeBehaviour : uint8
{
	/** The previous value of the attribute is copied across. */
	Copy,
	/** The attribute is set to zero. */
	Zero,
	/** The attribute is untouched. */
	None,
	/** The memory for the attribute is set to NIAGARA_INVALID_MEMORY. */
	MarkInvalid, 
	/** The attribute is passed through without double buffering */
	PassThrough,
};

UENUM()
enum class ENiagaraModuleDependencyType : uint8
{
	/** The dependency belongs before the module. */
	PreDependency,
	/** The dependency belongs after the module. */
	PostDependency
};

UENUM()
enum class ENiagaraModuleDependencyScriptConstraint : uint8
{
	/** The module providing the dependency must be in the same script e.g. if the module requiring the dependency is in "Particle Spawn" the module providing the dependency must also be in "Particle Spawn". */
	SameScript,
	/** The module providing the dependency can be in any script as long as it satisfies the dependency type, e.g. if the module requiring the dependency is in "Particle Spawn" the module providing the dependency could be in "Emitter Spawn". */
	AllScripts
};

UENUM()
enum class ENiagaraScriptLibraryVisibility : uint8
{
	Invalid = 0 UMETA(Hidden),
	
	/** The script is not visible by default to the user, but can be made visible by disabling the "Library only" filter option. */
	Unexposed UMETA(DisplayName = "Unexposed"),

	/** The script is exposed to the asset library and always visible to the user. */
	Library UMETA(DisplayName = "Exposed"),

	/** The script is never visible to the user. This is useful to "soft deprecate" assets that should not be shown to a user, but should also not generate errors for existing usages. */
	Hidden UMETA(DisplayName = "Hidden")
};

USTRUCT()
struct FNiagaraModuleDependency
{
	GENERATED_USTRUCT_BODY()
public:
	/** Specifies the provided id of the required dependent module (e.g. 'ProvidesNormalizedAge') */
	UPROPERTY(AssetRegistrySearchable, EditAnywhere, Category = Script)
	FName Id;

	/** Whether the dependency belongs before or after this module */
	UPROPERTY(AssetRegistrySearchable, EditAnywhere, Category = Script)
	ENiagaraModuleDependencyType Type; // e.g. PreDependency

	/** Specifies constraints related to the source script a modules provides as dependency. */
	UPROPERTY(AssetRegistrySearchable, EditAnywhere, Category = Script)
	ENiagaraModuleDependencyScriptConstraint ScriptConstraint;
	
	/** Detailed description of the dependency */
	UPROPERTY(AssetRegistrySearchable, EditAnywhere, Category = Script, meta = (MultiLine = true))
	FText Description;

	FNiagaraModuleDependency()
	{
		Type = ENiagaraModuleDependencyType::PreDependency;
		ScriptConstraint = ENiagaraModuleDependencyScriptConstraint::SameScript;
	}
};

struct FNiagaraScriptDebuggerInfo
{
	FNiagaraScriptDebuggerInfo();
	FNiagaraScriptDebuggerInfo(FName InName, ENiagaraScriptUsage InUsage, const FGuid& InUsageId);

	bool bWaitForGPU;

	FName HandleName;

	ENiagaraScriptUsage Usage;

	FGuid UsageId;

	int32 FrameLastWriteId;

	FNiagaraDataSet Frame;

	FNiagaraParameterStore Parameters;

	TAtomic<bool> bWritten;
};

/** Struct containing all of the data necessary to look up a NiagaraScript's VM executable results from the Derived Data Cache.*/
USTRUCT()
struct NIAGARA_API FNiagaraVMExecutableDataId
{
	GENERATED_USTRUCT_BODY()
public:
	/** The version of the compiler that this needs to be built against.*/
	UPROPERTY()
	FGuid CompilerVersionID;

	/** The type of script this was used for.*/
	UPROPERTY()
	ENiagaraScriptUsage ScriptUsageType;

	/** The instance id of this script usage type.*/
	UPROPERTY()
	FGuid ScriptUsageTypeID;

#if WITH_EDITORONLY_DATA
	/** Configuration options*/
	UPROPERTY()
	TArray<FString> AdditionalDefines;
#endif

	/** Whether or not we need to bake Rapid Iteration params. True to keep params, false to bake.*/
	UPROPERTY()
	uint32 bUsesRapidIterationParams : 1;

	/** Do we require interpolated spawning */
	UPROPERTY()
	uint32 bInterpolatedSpawn : 1;

	/** Do we require persistent IDs */
	UPROPERTY()
	uint32 bRequiresPersistentIDs : 1;

	/**
	* The GUID of the subgraph this shader primarily represents.
	*/
	UPROPERTY()
	FGuid BaseScriptID_DEPRECATED;

	/**
	* The hash of the subgraph this shader primarily represents.
	*/
	UPROPERTY()
	FNiagaraCompileHash BaseScriptCompileHash;

#if WITH_EDITORONLY_DATA
	/** Compile hashes of any top level scripts the script was dependent on that might trigger a recompile if they change. */
	UPROPERTY()
	TArray<FNiagaraCompileHash> ReferencedCompileHashes;

	/** Temp storage while generating the Id. This is NOT serialized and shouldn't be used in any comparisons*/
	TArray<FString> DebugReferencedObjects;
#endif

	FNiagaraVMExecutableDataId()
		: CompilerVersionID()
		, ScriptUsageType(ENiagaraScriptUsage::Function)
		, bUsesRapidIterationParams(true)
		, bInterpolatedSpawn(false)
		, bRequiresPersistentIDs(false)
		, BaseScriptID_DEPRECATED(0, 0, 0, 0)
	{ }


	~FNiagaraVMExecutableDataId()
	{ }

	bool IsValid() const;
	void Invalidate();
	
	friend uint32 GetTypeHash(const FNiagaraVMExecutableDataId& Ref)
	{
		return Ref.BaseScriptCompileHash.GetTypeHash();
	}

	SIZE_T GetSizeBytes() const
	{
		return sizeof(*this);
	}

	bool HasInterpolatedParameters() const;
	bool RequiresPersistentIDs() const;
#if 0
	/** Hashes the script-specific part of this shader map Id. */
	void GetScriptHash(FSHAHash& OutHash) const;
#endif

	/**
	* Tests this set against another for equality, disregarding override settings.
	*
	* @param ReferenceSet	The set to compare against
	* @return				true if the sets are equal
	*/
	bool operator==(const FNiagaraVMExecutableDataId& ReferenceSet) const;

	bool operator!=(const FNiagaraVMExecutableDataId& ReferenceSet) const
	{
		return !(*this == ReferenceSet);
	}

#if WITH_EDITORONLY_DATA
	/** Appends string representations of this Id to a key string. */
	void AppendKeyString(FString& KeyString, const FString& Delimiter = TEXT("_"), bool bAppendObjectForDebugging = false) const;

#endif
};

/** Struct containing all of the data needed to run a Niagara VM executable script.*/
USTRUCT()
struct NIAGARA_API FNiagaraVMExecutableData
{
	GENERATED_USTRUCT_BODY()
public:
	FNiagaraVMExecutableData();

	/** Byte code to execute for this system */
	UPROPERTY()
	TArray<uint8> ByteCode;

	/** Runtime optimized byte code, specific to the system we are running on, currently can not be serialized */
	UPROPERTY(transient)
	TArray<uint8> OptimizedByteCode;

	/** Number of temp registers used by this script. */
	UPROPERTY()
	int32 NumTempRegisters;

	/** Number of user pointers we must pass to the VM. */
	UPROPERTY()
	int32 NumUserPtrs;

#if WITH_EDITORONLY_DATA
	/** All the data for using external constants in the script, laid out in the order they are expected in the uniform table.*/
	UPROPERTY()
	FNiagaraParameters Parameters;

	/** All the data for using external constants in the script, laid out in the order they are expected in the uniform table.*/
	UPROPERTY()
	FNiagaraParameters InternalParameters;
#endif

	UPROPERTY()
	TArray<uint8> ScriptLiterals;

	/** Attributes used by this script. */
	UPROPERTY()
	TArray<FNiagaraVariable> Attributes;

	/** Contains various usage information for this script. */
	UPROPERTY()
	FNiagaraScriptDataUsageInfo DataUsage;

#if WITH_EDITORONLY_DATA
	UPROPERTY()
	TMap<FName, FNiagaraParameters> DataSetToParameters;

	UPROPERTY()
	TArray<FNiagaraFunctionSignature> AdditionalExternalFunctions;
#endif

	/** Information about all data interfaces used by this script. */
	UPROPERTY()
	TArray<FNiagaraScriptDataInterfaceCompileInfo> DataInterfaceInfo;

	/** Array of ordered vm external functions to place in the function table. */
	UPROPERTY()
	TArray<FVMExternalFunctionBindingInfo> CalledVMExternalFunctions;

	TArray<FVMExternalFunction> CalledVMExternalFunctionBindings;

	UPROPERTY()
	TArray<FNiagaraDataSetID> ReadDataSets;

	UPROPERTY()
	TArray<FNiagaraDataSetProperties> WriteDataSets;

	/** Scopes we'll track with stats.*/
	UPROPERTY()
	TArray<FNiagaraStatScope> StatScopes;

#if WITH_EDITORONLY_DATA
	UPROPERTY()
	FString LastHlslTranslation;

	UPROPERTY()
	FString LastHlslTranslationGPU;

	UPROPERTY()
	FString LastAssemblyTranslation;

	UPROPERTY()
	uint32 LastOpCount;
#endif

	UPROPERTY()
	TArray<FNiagaraDataInterfaceGPUParamInfo> DIParamInfo; //TODO: GPU Param info should not be in the "VM executable data"

#if WITH_EDITORONLY_DATA
	/** The parameter collections used by this script. */
	UPROPERTY()
	TArray<FString> ParameterCollectionPaths;
#endif

	/** Last known compile status. Lets us determine the latest state of the script byte buffer.*/
	UPROPERTY()
	ENiagaraScriptCompileStatus LastCompileStatus;

	UPROPERTY()
	TArray<FSimulationStageMetaData> SimulationStageMetaData;

#if WITH_EDITORONLY_DATA
	UPROPERTY()
	bool bReadsAttributeData;

	UPROPERTY()
	FString ErrorMsg;

	UPROPERTY()
	float CompileTime;

	/** Array of all compile events generated last time the script was compiled.*/
	UPROPERTY()
	TArray<FNiagaraCompileEvent> LastCompileEvents;
#endif

	UPROPERTY()
	uint32 bReadsSignificanceIndex : 1;

	void SerializeData(FArchive& Ar, bool bDDCData);
	
	bool IsValid() const;

	void Reset();

#if WITH_EDITORONLY_DATA
	void BakeScriptLiterals(TArray<uint8>& OutLiterals) const;
#endif
};

/** Runtime script for a Niagara system */
UCLASS(MinimalAPI)
class UNiagaraScript : public UNiagaraScriptBase
{
	GENERATED_UCLASS_BODY()
public:
	// how this script is to be used. cannot be private due to use of GET_MEMBER_NAME_CHECKED
	UPROPERTY(AssetRegistrySearchable)
	ENiagaraScriptUsage Usage;

	/** Which instance of the usage in the graph to use.  This is now deprecated and is handled by UsageId. */
	UPROPERTY()
	int32 UsageIndex_DEPRECATED;

#if WITH_EDITOR
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnScriptCompiled, UNiagaraScript*);
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnPropertyChanged, FPropertyChangedEvent& /* PropertyChangedEvent */)
#endif

private:
	/** Specifies a unique id for use when there are multiple scripts with the same usage, e.g. events. */
	UPROPERTY()
	FGuid UsageId;

public:
#if WITH_EDITORONLY_DATA
	/** When used as a module, what are the appropriate script types for referencing this module?*/
	UPROPERTY(AssetRegistrySearchable, EditAnywhere, Category = Script, meta = (Bitmask, BitmaskEnum = ENiagaraScriptUsage))
	int32 ModuleUsageBitmask;

	/** Used to break up scripts of the same Usage type in UI display.*/
	UPROPERTY(AssetRegistrySearchable, EditAnywhere, Category = Script)
	FText Category;
	
	/** Array of Ids of dependencies provided by this module to other modules on the stack (e.g. 'ProvidesNormalizedAge') */
	UPROPERTY(EditAnywhere, Category = Script)
	TArray<FName> ProvidedDependencies;
	
	/** Dependencies required by this module from other modules on the stack */
	UPROPERTY(EditAnywhere, Category = Script)
	TArray<FNiagaraModuleDependency> RequiredDependencies;

	/* If this script is no longer meant to be used, this option should be set.*/
	UPROPERTY(AssetRegistrySearchable, EditAnywhere, Category = Script)
	uint32 bDeprecated : 1;

	/* Message to display when the script is deprecated. */
	UPROPERTY(EditAnywhere, Category = Script, meta = (EditCondition = "bDeprecated", MultiLine = true))
	FText DeprecationMessage;

	/* Which script to use if this is deprecated.*/
	UPROPERTY(EditAnywhere, Category = Script, meta = (EditCondition = "bDeprecated"))
	UNiagaraScript* DeprecationRecommendation;

	/* Custom logic to convert the contents of an existing script assignment to this script.*/
	UPROPERTY(EditAnywhere, Category = Script)
	TSubclassOf<UNiagaraConvertInPlaceUtilityBase> ConversionUtility;

	/** Is this script experimental and less supported? */
	UPROPERTY(EditAnywhere, Category = Script)
	uint32 bExperimental : 1;

	/** The message to display when a function is marked experimental. */
	UPROPERTY(EditAnywhere, Category = Script, meta = (EditCondition = "bExperimental", MultiLine = true))
	FText ExperimentalMessage;

	/* Deprecated, use LibraryVisibility instead. */
	UPROPERTY(AssetRegistrySearchable, meta = (DeprecatedProperty))
	uint32 bExposeToLibrary_DEPRECATED : 1;
	
	/* Defines if this script is visible to the user when searching for modules to add to an emitter.  */
	UPROPERTY(AssetRegistrySearchable, EditAnywhere, Category = Script)
	ENiagaraScriptLibraryVisibility LibraryVisibility;
#endif

	/** Contains all of the top-level values that are iterated on in the UI. These are usually "Module" variables in the graph. They don't necessarily have to be in the order that they are expected in the uniform table.*/
	UPROPERTY()
	FNiagaraParameterStore RapidIterationParameters;

#if WITH_EDITORONLY_DATA
	/** The mode to use when deducing the type of numeric output pins from the types of the input pins. */
	UPROPERTY(EditAnywhere, Category=Script)
	ENiagaraNumericOutputTypeSelectionMode NumericOutputTypeSelectionMode;

	UPROPERTY(AssetRegistrySearchable, EditAnywhere, Category = Script, meta = (MultiLine = true))
	FText Description;

	/** A list of space separated keywords which can be used to find this script in editor menus. */
	UPROPERTY(AssetRegistrySearchable, EditAnywhere, Category = Script)
	FText Keywords;

	/** The format for the text to display in the stack if the value is collapsed.
	 *  This supports formatting placeholders for the function inputs, for example "myfunc({0}, {1})" will be converted to "myfunc(1.23, Particles.Position)". */
	UPROPERTY(EditAnywhere, Category = Script, meta = (EditCondition = "Usage == ENiagaraScriptUsage::DynamicInput"))
	FText CollapsedViewFormat;

	UPROPERTY(EditAnywhere, Category = Script)
	TArray<FNiagaraScriptHighlight> Highlights;

	UPROPERTY(EditAnywhere, Category = Script, DisplayName = "Script Metadata", meta = (ToolTip = "Script Metadata"))
	TMap<FName, FString> ScriptMetaData;

	NIAGARA_API static const FName NiagaraCustomVersionTagName;
#endif

	NIAGARA_API void ComputeVMCompilationId(FNiagaraVMExecutableDataId& Id) const;
	NIAGARA_API const FNiagaraVMExecutableDataId& GetComputedVMCompilationId() const
	{
#if WITH_EDITORONLY_DATA
		if (!IsCooked)
		{
			return LastGeneratedVMId;
		}
#endif
		return CachedScriptVMId;
	}

	void SetUsage(ENiagaraScriptUsage InUsage) { Usage = InUsage; }
	ENiagaraScriptUsage GetUsage() const { return Usage; }

	void SetUsageId(FGuid InUsageId) { UsageId = InUsageId; }
	FGuid GetUsageId() const { return UsageId; }

	NIAGARA_API bool ContainsUsage(ENiagaraScriptUsage InUsage) const;
	bool IsEquivalentUsage(ENiagaraScriptUsage InUsage) const {return (InUsage == Usage) || (Usage == ENiagaraScriptUsage::ParticleSpawnScript && InUsage == ENiagaraScriptUsage::ParticleSpawnScriptInterpolated) || (Usage == ENiagaraScriptUsage::ParticleSpawnScriptInterpolated && InUsage == ENiagaraScriptUsage::ParticleSpawnScript);}
	static bool IsEquivalentUsage(ENiagaraScriptUsage InUsageA, ENiagaraScriptUsage InUsageB) { return (InUsageA == InUsageB) || (InUsageB == ENiagaraScriptUsage::ParticleSpawnScript && InUsageA == ENiagaraScriptUsage::ParticleSpawnScriptInterpolated) || (InUsageB == ENiagaraScriptUsage::ParticleSpawnScriptInterpolated && InUsageA == ENiagaraScriptUsage::ParticleSpawnScript); }
	/** Is usage A dependent on Usage B?*/
	NIAGARA_API static bool IsUsageDependentOn(ENiagaraScriptUsage InUsageA, ENiagaraScriptUsage InUsageB);

	bool IsParticleSpawnScript() const { return Usage == ENiagaraScriptUsage::ParticleSpawnScript || Usage == ENiagaraScriptUsage::ParticleSpawnScriptInterpolated; }
	bool IsInterpolatedParticleSpawnScript()const { return Usage == ENiagaraScriptUsage::ParticleSpawnScriptInterpolated; }
	bool IsParticleUpdateScript()const { return Usage == ENiagaraScriptUsage::ParticleUpdateScript; }
	bool IsModuleScript()const { return Usage == ENiagaraScriptUsage::Module; }
	bool IsFunctionScript()	const { return Usage == ENiagaraScriptUsage::Function; }
	bool IsDynamicInputScript()	const { return Usage == ENiagaraScriptUsage::DynamicInput; }
	bool IsParticleEventScript()const { return Usage == ENiagaraScriptUsage::ParticleEventScript; }
	bool IsParticleScript() const {	return Usage >= ENiagaraScriptUsage::ParticleSpawnScript && Usage <= ENiagaraScriptUsage::ParticleGPUComputeScript;}

	bool IsNonParticleScript()const { return Usage >= ENiagaraScriptUsage::EmitterSpawnScript; }
	
	bool IsSystemSpawnScript()const { return Usage == ENiagaraScriptUsage::SystemSpawnScript; }
	bool IsSystemUpdateScript()const { return Usage == ENiagaraScriptUsage::SystemUpdateScript; }
	bool IsEmitterSpawnScript()const { return Usage == ENiagaraScriptUsage::EmitterSpawnScript; }
	bool IsEmitterUpdateScript()const { return Usage == ENiagaraScriptUsage::EmitterUpdateScript; }
	bool IsStandaloneScript() const { return IsDynamicInputScript() || IsFunctionScript() || IsModuleScript(); }

	bool IsSpawnScript()const { return IsParticleSpawnScript() || IsEmitterSpawnScript() || IsSystemSpawnScript(); }

	bool IsCompilable() const { return !IsEmitterSpawnScript() && !IsEmitterUpdateScript(); }


	static bool IsGPUScript(ENiagaraScriptUsage Usage) { return Usage == ENiagaraScriptUsage::ParticleGPUComputeScript; }
	static bool IsParticleSpawnScript(ENiagaraScriptUsage Usage)  { return Usage == ENiagaraScriptUsage::ParticleSpawnScript || Usage == ENiagaraScriptUsage::ParticleSpawnScriptInterpolated; }
	static bool IsInterpolatedParticleSpawnScript(ENiagaraScriptUsage Usage) { return Usage == ENiagaraScriptUsage::ParticleSpawnScriptInterpolated; }
	static bool IsParticleUpdateScript(ENiagaraScriptUsage Usage) { return Usage == ENiagaraScriptUsage::ParticleUpdateScript; }
	static bool IsModuleScript(ENiagaraScriptUsage Usage) { return Usage == ENiagaraScriptUsage::Module; }
	static bool IsFunctionScript(ENiagaraScriptUsage Usage)	 { return Usage == ENiagaraScriptUsage::Function; }
	static bool IsDynamicInputScript(ENiagaraScriptUsage Usage)	 { return Usage == ENiagaraScriptUsage::DynamicInput; }
	static bool IsParticleEventScript(ENiagaraScriptUsage Usage) { return Usage == ENiagaraScriptUsage::ParticleEventScript; }
	static bool IsParticleScript(ENiagaraScriptUsage Usage)  { return Usage >= ENiagaraScriptUsage::ParticleSpawnScript && Usage <= ENiagaraScriptUsage::ParticleGPUComputeScript; }

	static bool IsNonParticleScript(ENiagaraScriptUsage Usage) { return Usage >= ENiagaraScriptUsage::EmitterSpawnScript; }

	static bool IsSystemSpawnScript(ENiagaraScriptUsage Usage) { return Usage == ENiagaraScriptUsage::SystemSpawnScript; }
	static bool IsSystemUpdateScript(ENiagaraScriptUsage Usage) { return Usage == ENiagaraScriptUsage::SystemUpdateScript; }
	static bool IsSystemScript(ENiagaraScriptUsage Usage) { return IsSystemSpawnScript(Usage) || IsSystemUpdateScript(Usage);}
	static bool IsEmitterSpawnScript(ENiagaraScriptUsage Usage) { return Usage == ENiagaraScriptUsage::EmitterSpawnScript; }
	static bool IsEmitterUpdateScript(ENiagaraScriptUsage Usage) { return Usage == ENiagaraScriptUsage::EmitterUpdateScript; }
	static bool IsStandaloneScript(ENiagaraScriptUsage Usage)  { return IsDynamicInputScript(Usage) || IsFunctionScript(Usage) || IsModuleScript(Usage); }

	static bool IsSpawnScript(ENiagaraScriptUsage Usage) { return IsParticleSpawnScript(Usage) || IsEmitterSpawnScript(Usage) || IsSystemSpawnScript(Usage); }

	static bool IsCompilable(ENiagaraScriptUsage Usage)  { return !IsEmitterSpawnScript(Usage) && !IsEmitterUpdateScript(Usage); }

	static bool NIAGARA_API ConvertUsageToGroup(ENiagaraScriptUsage InUsage, ENiagaraScriptGroup& OutGroup);

#if WITH_EDITORONLY_DATA
	NIAGARA_API TArray<ENiagaraParameterScope> GetUnsupportedParameterScopes() const;
	NIAGARA_API TArray<ENiagaraScriptUsage> GetSupportedUsageContexts() const;
	static NIAGARA_API TArray<ENiagaraScriptUsage> GetSupportedUsageContextsForBitmask(int32 InModuleUsageBitmask, bool bIncludeHiddenUsages = false);
	static NIAGARA_API bool IsSupportedUsageContextForBitmask(int32 InModuleUsageBitmask, ENiagaraScriptUsage InUsageContext, bool bIncludeHiddenUsages = false);
	static NIAGARA_API bool ContainsEquivilentUsage(const TArray<ENiagaraScriptUsage>& Usages, ENiagaraScriptUsage InUsage);
#endif

	NIAGARA_API bool CanBeRunOnGpu() const;
	NIAGARA_API bool IsReadyToRun(ENiagaraSimTarget SimTarget) const;
	NIAGARA_API bool ShouldCacheShadersForCooking(const ITargetPlatform* TargetPlatform) const;

#if WITH_EDITORONLY_DATA
	class UNiagaraScriptSourceBase *GetSource() { return Source; }
	const class UNiagaraScriptSourceBase *GetSource() const  { return Source; }
	void SetSource(class UNiagaraScriptSourceBase *InSource) { Source = InSource; }

	NIAGARA_API FGuid GetBaseChangeID() const;
	NIAGARA_API ENiagaraScriptCompileStatus GetLastCompileStatus() const;
	void ForceGraphToRecompileOnNextCheck();

	NIAGARA_API bool HandleVariableRenames(const TMap<FNiagaraVariable, FNiagaraVariable>& OldToNewVars, const FString& UniqueEmitterName);
#endif

	//~ Begin UObject interface
	void PreSave(const class ITargetPlatform* TargetPlatform) override;
	void Serialize(FArchive& Ar)override;
	virtual void PostLoad() override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
	virtual void GetAssetRegistryTags(TArray<FAssetRegistryTag>& OutTags) const override;

	virtual bool IsEditorOnly() const override;

	virtual NIAGARA_API void BeginDestroy() override;
	virtual NIAGARA_API bool IsReadyForFinishDestroy() override;
	//~ End UObject interface

	//~ Begin UNiagaraScriptBase interface
	virtual TConstArrayView<FSimulationStageMetaData> GetSimulationStageMetaData() const override { return MakeArrayView(CachedScriptVM.SimulationStageMetaData); }
	//~ End UNiagaraScriptBase interface

	// Infrastructure for GPU compute Shaders
#if WITH_EDITOR
	NIAGARA_API void CacheResourceShadersForCooking(EShaderPlatform ShaderPlatform, TArray<FNiagaraShaderScript*>& InOutCachedResources, const ITargetPlatform* TargetPlatform = nullptr);

	NIAGARA_API void CacheResourceShadersForRendering(bool bRegenerateId, bool bForceRecompile=false);
	void BeginCacheForCookedPlatformData(const ITargetPlatform *TargetPlatform);
	virtual bool IsCachedCookedPlatformDataLoaded(const ITargetPlatform* TargetPlatform) override;
	void CacheShadersForResources(FNiagaraShaderScript* ResourceToCache, bool bApplyCompletedShaderMapForRendering, bool bForceRecompile = false, bool bCooking=false, const ITargetPlatform* TargetPlatform = nullptr);
#endif // WITH_EDITOR
	FNiagaraShaderScript* AllocateResource();
	FNiagaraShaderScript* GetRenderThreadScript()
	{
		return ScriptResource.Get();
	}

	const FNiagaraShaderScript* GetRenderThreadScript() const
	{
		return ScriptResource.Get();
	}

	NIAGARA_API void GenerateStatIDs();

	NIAGARA_API bool IsScriptCompilationPending(bool bGPUScript) const;
	NIAGARA_API bool DidScriptCompilationSucceed(bool bGPUScript) const;

#if WITH_EDITORONLY_DATA
	NIAGARA_API void InvalidateCompileResults(const FString& Reason);
	FText GetDescription() { return Description.IsEmpty() ? FText::FromString(GetName()) : Description; }

	/** Helper to convert the struct from its binary data out of the DDC to it's actual in-memory version.
		Do not call this on anything other than the game thread as it depends on the FObjectAndNameAsStringProxyArchive,
		which calls FindStaticObject which can fail when used in any other thread!*/
	static bool BinaryToExecData(const UNiagaraScript* Script, const TArray<uint8>& InBinaryData, FNiagaraVMExecutableData& OutExecData);

	/** Reverse of the BinaryToExecData() function */
	static bool ExecToBinaryData(const UNiagaraScript* Script, TArray<uint8>& OutBinaryData, FNiagaraVMExecutableData& InExecData);

	/** Makes a deep copy of any script dependencies, including itself.*/
	NIAGARA_API virtual UNiagaraScript* MakeRecursiveDeepCopy(UObject* DestOuter, TMap<const UObject*, UObject*>& ExistingConversions) const;

	/** Determine if there are any external dependencies with respect to scripts and ensure that those dependencies are sucked into the existing package.*/
	NIAGARA_API virtual void SubsumeExternalDependencies(TMap<const UObject*, UObject*>& ExistingConversions);

	/** Determine if the Script and its source graph are in sync.*/
	NIAGARA_API bool AreScriptAndSourceSynchronized() const;

	/** Ensure that the Script and its source graph are marked out of sync.*/
	NIAGARA_API void MarkScriptAndSourceDesynchronized(FString Reason);
	
	/** Request a synchronous compile for the script, possibly forcing it to compile.*/
	NIAGARA_API void RequestCompile(bool bForceCompile = false);

	/** Request an asynchronous compile for the script, possibly forcing it to compile. The output values are the compilation id of the data as well as the async handle to 
		gather up the results with. The function returns whether or not any compiles were actually issued. */
	NIAGARA_API bool RequestExternallyManagedAsyncCompile(const TSharedPtr<FNiagaraCompileRequestDataBase, ESPMode::ThreadSafe>& RequestData, FNiagaraVMExecutableDataId& OutCompileId, uint32& OutAsyncHandle);

	/** Builds the DDC string for the derived data cache using the supplied CompiledId */
	static FString BuildNiagaraDDCKeyString(const FNiagaraVMExecutableDataId& CompileId);

	/** Creates a string key for the derived data cache */
	FString GetNiagaraDDCKeyString();

	/** Callback issued whenever a VM script compilation successfully happened (even if the results are a script that cannot be executed due to errors)*/
	NIAGARA_API FOnScriptCompiled& OnVMScriptCompiled();

	/** Callback issued whenever a GPU script compilation successfully happened (even if the results are a script that cannot be executed due to errors)*/
	NIAGARA_API FOnScriptCompiled& OnGPUScriptCompiled();

	/** Callback issues whenever post edit changed is called on this script. */
	NIAGARA_API FOnPropertyChanged& OnPropertyChanged();

	/** External call used to identify the values for a successful VM script compilation. OnVMScriptCompiled will be issued in this case.*/
	void SetVMCompilationResults(const FNiagaraVMExecutableDataId& InCompileId, FNiagaraVMExecutableData& InScriptVM, FNiagaraCompileRequestDataBase* InRequestData);

	/** In the event where we "merge" we duplicate the changes of the master copy onto the newly cloned copy. This function will synchronize the compiled script 
		results assuming that the scripts themselves are bound to the same key. This saves looking things up in the DDC. It returns true if successfully synchronized and 
		false if not.*/
	NIAGARA_API bool SynchronizeExecutablesWithMaster(const UNiagaraScript* Script, const TMap<FString, FString>& RenameMap);

	NIAGARA_API FString GetFriendlyName() const;

	NIAGARA_API void SyncAliases(const TMap<FString, FString>& RenameMap);
#endif
	
	UFUNCTION()
	void RaiseOnGPUCompilationComplete();


	NIAGARA_API FORCEINLINE FNiagaraVMExecutableData& GetVMExecutableData() { return CachedScriptVM; }
	NIAGARA_API FORCEINLINE const FNiagaraVMExecutableData& GetVMExecutableData() const { return CachedScriptVM; }
	NIAGARA_API FORCEINLINE const FNiagaraVMExecutableDataId& GetVMExecutableDataCompilationId() const { return CachedScriptVMId; }

	TArray<UNiagaraParameterCollection*>& GetCachedParameterCollectionReferences() { return CachedParameterCollectionReferences; }
	TArray<FNiagaraScriptDataInterfaceInfo>& GetCachedDefaultDataInterfaces() { return CachedDefaultDataInterfaces; }

#if STATS
	TArrayView<const TStatId> GetStatScopeIDs() const { return MakeArrayView(StatScopesIDs); }
#elif ENABLE_STATNAMEDEVENTS
	TArrayView<const FString> GetStatNamedEvents() const { return MakeArrayView(StatNamedEvents); }
#endif

	bool UsesCollection(const class UNiagaraParameterCollection* Collection)const;
	
	virtual ~UNiagaraScript();

	const FNiagaraScriptExecutionParameterStore* GetExecutionReadyParameterStore(ENiagaraSimTarget SimTarget);
	void InvalidateExecutionReadyParameterStores();

	bool IsScriptCooked() const
	{
#if WITH_EDITORONLY_DATA
		return IsCooked;
#else
		return true;
#endif
	}

private:
	bool OwnerCanBeRunOnGpu() const;
	bool LegacyCanBeRunOnGpu()const;

	void ProcessSerializedShaderMaps();
	void SerializeNiagaraShaderMaps(FArchive& Ar, int32 NiagaraVer, bool IsValidShaderScript);

	/** Return the expected SimTarget for this script. Only returns a valid target if there is valid data to run with. */
	TOptional<ENiagaraSimTarget> GetSimTarget() const;

	/** Kicks off an async job to convert the ByteCode into an optimized version for the platform we are running on. */
	void AsyncOptimizeByteCode();

	/** Generates all of the function bindings for DI that don't require user data */
	void GenerateDefaultFunctionBindings();

	/** Returns whether the parameter store bindings are valid */
	bool HasValidParameterBindings() const;

#if WITH_EDITORONLY_DATA
	UPROPERTY(Transient)
	FNiagaraScriptExecutionParameterStore ScriptExecutionParamStoreCPU;

	UPROPERTY(Transient)
	FNiagaraScriptExecutionParameterStore ScriptExecutionParamStoreGPU;
#endif // WITH_EDITORONLY_DATA

	/** The equivalent of ScriptExecutionParamStoreCPU (or GPU) cooked for the given platform.*/
	UPROPERTY()
	FNiagaraScriptExecutionParameterStore ScriptExecutionParamStore;
	/** The cooked binding data between ScriptExecutionParamStore and RapidIterationParameters.*/
	UPROPERTY()
	TArray<FNiagaraBoundParameter> ScriptExecutionBoundParameters;

#if WITH_EDITORONLY_DATA
	class UNiagaraSystem* FindRootSystem();

	bool HasIdsRequiredForShaderCaching() const;

	/** 'Source' data/graphs for this script */
	UPROPERTY()
	class UNiagaraScriptSourceBase*	Source;
	
	/** A multicast delegate which is called whenever the script has been compiled (successfully or not). */
	FOnScriptCompiled OnVMScriptCompiledDelegate;
	FOnScriptCompiled OnGPUScriptCompiledDelegate;
	FOnPropertyChanged OnPropertyChangedDelegate;

	mutable FNiagaraVMExecutableDataId LastReportedVMId;

	mutable TOptional<TMap<FName, FString>> CustomAssetRegistryTagCache;
#endif

	/** Adjusted every time that we compile this script. Lets us know that we might differ from any cached versions.*/
	UPROPERTY()
	FNiagaraVMExecutableDataId CachedScriptVMId;

#if WITH_EDITORONLY_DATA
	/** Adjusted every time ComputeVMCompilationId is called.*/
	UPROPERTY()
	mutable FNiagaraVMExecutableDataId LastGeneratedVMId;
#endif

	TUniquePtr<FNiagaraShaderScript> ScriptResource;

#if WITH_EDITORONLY_DATA
	TArray<FNiagaraShaderScript> LoadedScriptResources;
	FNiagaraShaderScript* ScriptResourcesByFeatureLevel[ERHIFeatureLevel::Num];
#endif

	/** Compute shader compiled for this script */
	FComputeShaderRHIRef ScriptShader;

	/** Runtime stat IDs generated from StatScopes. */
#if STATS
	TArray<TStatId> StatScopesIDs;
#elif ENABLE_STATNAMEDEVENTS
	TArray<FString> StatNamedEvents;
#endif

#if WITH_EDITORONLY_DATA
	/* script resources being cached for cooking. */
	TMap<const class ITargetPlatform*, TArray<FNiagaraShaderScript*>> CachedScriptResourcesForCooking;

	UPROPERTY(Transient)
	TArray<UObject*> ActiveCompileRoots;

	/* Flag set on load based on whether the serialized data includes editor only data */
	bool IsCooked;
#endif

	/** Compiled VM bytecode and data necessary to run this script.*/
	UPROPERTY()
	FNiagaraVMExecutableData CachedScriptVM;
	
	UPROPERTY()
	TArray<UNiagaraParameterCollection*> CachedParameterCollectionReferences;

	UPROPERTY()
	TArray<FNiagaraScriptDataInterfaceInfo> CachedDefaultDataInterfaces;

	static UNiagaraDataInterface* CopyDataInterface(UNiagaraDataInterface* Src, UObject* Owner);

	/** Flag used to guarantee that the RT isn't accessing the FNiagaraScriptResource before cleanup. */
	FThreadSafeBool ReleasedByRT;
};
