// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineBaseTypes.h"
#include "UObject/ObjectMacros.h"
#include "NiagaraTypes.h"
#include "UObject/SoftObjectPath.h"
#include "RHI.h"
#include "NiagaraCore.h"
#include "NiagaraCommon.generated.h"

class UNiagaraSystem;
class UNiagaraScript;
class UNiagaraDataInterface;
class UNiagaraEmitter;
class FNiagaraSystemInstance;
class UNiagaraParameterCollection;
struct FNiagaraParameterStore;

//#define NIAGARA_NAN_CHECKING 1
#define NIAGARA_NAN_CHECKING 0

const uint32 NIAGARA_COMPUTE_THREADGROUP_SIZE = 64;
const uint32 NIAGARA_MAX_COMPUTE_THREADGROUPS = 65535;

#define INTERPOLATED_PARAMETER_PREFIX TEXT("PREV_")

/** The maximum number of spawn infos we can run on the GPU, modifying this will require a version update as it is used in the shader compiler  */
constexpr uint32 NIAGARA_MAX_GPU_SPAWN_INFOS = 8;

/** TickGroup information for Niagara.  */
constexpr ETickingGroup NiagaraFirstTickGroup = TG_PrePhysics;
constexpr ETickingGroup NiagaraLastTickGroup = TG_LastDemotable;
constexpr int NiagaraNumTickGroups = NiagaraLastTickGroup - NiagaraFirstTickGroup + 1;

/** Niagara ticking behaviour */
UENUM()
enum class ENiagaraTickBehavior : uint8
{
	/** Niagara will tick after all prereqs have ticked for attachements / data interfaces, this is the safest option. */
	UsePrereqs,
	/** Niagara will ignore prereqs (attachments / data interface dependencies) and use the tick group set on the component. */
	UseComponentTickGroup,
	/** Niagara will tick in the first tick group (default is TG_PrePhysics). */
	ForceTickFirst,
	/** Niagara will tick in the last tick group (default is TG_LastDemotable). */
	ForceTickLast,
};

enum ENiagaraBaseTypes
{
	NBT_Float,
	NBT_Int32,
	NBT_Bool,
	NBT_Max,
};

// TODO: Custom will eventually mean that the default value or binding will be overridden by a subgraph default, i.e. expose it to a "Initialize variable" node. 
// TODO: Should we add an "Uninitialized" entry, or is that too much friction? 
UENUM()
enum class ENiagaraDefaultMode : uint8
{
	// Default initialize using a value widget in the Selected Details panel. 
	Value = 0, 
	// Default initialize using a dropdown widget in the Selected Details panel. 
	Binding,   
	// Default initialization is done using a sub-graph.
	Custom,    
};

UENUM()
enum class ENiagaraSimTarget : uint8
{
	CPUSim,
	GPUComputeSim
};


/** Defines modes for updating the component's age. */
UENUM()
enum class ENiagaraAgeUpdateMode : uint8
{
	/** Update the age using the delta time supplied to the component tick function. */
	TickDeltaTime,
	/** Update the age by seeking to the DesiredAge. To prevent major perf loss, we clamp to MaxClampTime*/
	DesiredAge,
	/** Update the age by tracking changes to the desired age, but when the desired age goes backwards in time,
	or jumps forwards in time by more than a few steps, the system is reset and simulated forward by a single step.
	This mode is useful for continuous effects controlled by sequencer. */
	DesiredAgeNoSeek
};


UENUM()
enum class ENiagaraDataSetType : uint8
{
	ParticleData,
	Shared,
	Event,
};


UENUM()
enum class ENiagaraInputNodeUsage : uint8
{
	Undefined = 0 UMETA(Hidden),
	Parameter,
	Attribute,
	SystemConstant,
	TranslatorConstant,
	RapidIterationParameter
};

/**
* Enumerates states a Niagara script can be in.
*/
UENUM()
enum class ENiagaraScriptCompileStatus : uint8
{
	/** Niagara script is in an unknown state. */
	NCS_Unknown,
	/** Niagara script has been modified but not recompiled. */
	NCS_Dirty,
	/** Niagara script tried but failed to be compiled. */
	NCS_Error,
	/** Niagara script has been compiled since it was last modified. */
	NCS_UpToDate,
	/** Niagara script is in the process of being created for the first time. */
	NCS_BeingCreated,
	/** Niagara script has been compiled since it was last modified. There are warnings. */
	NCS_UpToDateWithWarnings,
	/** Niagara script has been compiled for compute since it was last modified. There are warnings. */
	NCS_ComputeUpToDateWithWarnings,
	NCS_MAX,
};

USTRUCT()
struct FNiagaraDataSetID
{
	GENERATED_USTRUCT_BODY()

	FNiagaraDataSetID()
	: Name(NAME_None)
	, Type(ENiagaraDataSetType::Event)
	{}

	FNiagaraDataSetID(FName InName, ENiagaraDataSetType InType)
		: Name(InName)
		, Type(InType)
	{}

	UPROPERTY(EditAnywhere, Category = "Data Set")
	FName Name;

	UPROPERTY()
	ENiagaraDataSetType Type;

	FORCEINLINE bool operator==(const FNiagaraDataSetID& Other)const
	{
		return Name == Other.Name && Type == Other.Type;
	}

	FORCEINLINE bool operator!=(const FNiagaraDataSetID& Other)const
	{
		return !(*this == Other);
	}
};


FORCEINLINE FArchive& operator<<(FArchive& Ar, FNiagaraDataSetID& VarInfo)
{
	Ar << VarInfo.Name << VarInfo.Type;
	return Ar;
}

FORCEINLINE uint32 GetTypeHash(const FNiagaraDataSetID& Var)
{
	return HashCombine(GetTypeHash(Var.Name), (uint32)Var.Type);
}

USTRUCT()
struct FNiagaraDataSetProperties
{
	GENERATED_BODY()
	
	UPROPERTY(VisibleAnywhere, Category = "Data Set")
	FNiagaraDataSetID ID;

	UPROPERTY()
	TArray<FNiagaraVariable> Variables;
};

/** Information about an input or output of a Niagara operation node. */
class FNiagaraOpInOutInfo
{
public:
	FName Name;
	FNiagaraTypeDefinition DataType;
	FText FriendlyName;
	FText Description;
	FString Default;
	FString HlslSnippet;

	FNiagaraOpInOutInfo(FName InName, FNiagaraTypeDefinition InType, FText InFriendlyName, FText InDescription, FString InDefault, FString InHlslSnippet = TEXT(""))
		: Name(InName)
		, DataType(InType)
		, FriendlyName(InFriendlyName)
		, Description(InDescription)
		, Default(InDefault)
		, HlslSnippet(InHlslSnippet)
	{

	}
};


/** Struct containing usage information about a script. Things such as whether it reads attribute data, reads or writes events data etc.*/
USTRUCT()
struct FNiagaraScriptDataUsageInfo
{
	GENERATED_BODY()

		FNiagaraScriptDataUsageInfo()
		: bReadsAttributeData(false)
	{}

	/** If true, this script reads attribute data. */
	UPROPERTY()
	bool bReadsAttributeData;
};


USTRUCT()
struct NIAGARA_API FNiagaraFunctionSignature
{
	GENERATED_BODY()

	/** Name of the function. */
	UPROPERTY()
	FName Name;
	/** Input parameters to this function. */
	UPROPERTY()
	TArray<FNiagaraVariable> Inputs;
	/** Input parameters of this function. */
	UPROPERTY()
	TArray<FNiagaraVariable> Outputs;
	/** Id of the owner is this is a member function. */
	UPROPERTY()
	FName OwnerName;
	UPROPERTY()
	uint32 bRequiresContext : 1;
	/** True if this is the signature for a "member" function of a data interface. If this is true, the first input is the owner. */
	UPROPERTY()
	uint32 bMemberFunction : 1;
	/** Is this function experimental? */
	UPROPERTY()
	uint32 bExperimental : 1;

#if WITH_EDITORONLY_DATA
	/** The message to display when a function is marked experimental. */
	UPROPERTY(EditAnywhere, Category = Script, meta = (EditCondition = "bExperimental", MultiLine = true, SkipForCompileHash = true))
	FText ExperimentalMessage;

	/** Per function version, it is up to the discretion of the function as to what the version means. */
	UPROPERTY()
	uint32 FunctionVersion = 0;
#endif

	/** Support running on the CPU. */
	UPROPERTY()
	uint32 bSupportsCPU : 1;
	/** Support running on the GPU. */
	UPROPERTY()
	uint32 bSupportsGPU : 1;

	/** Writes to the variable this is bound to */
	UPROPERTY()
	uint32 bWriteFunction : 1;

	/** Function specifiers verified at bind time. */
	UPROPERTY()
	TMap<FName, FName> FunctionSpecifiers;

	/** Localized description of this node. Note that this is *not* used during the operator == below since it may vary from culture to culture.*/
#if WITH_EDITORONLY_DATA
	UPROPERTY(meta = (SkipForCompileHash = true))
	FText Description;
#endif

	FNiagaraFunctionSignature() 
		: bRequiresContext(false)
		, bMemberFunction(false)
		, bExperimental(false)
		, bSupportsCPU(true)
		, bSupportsGPU(true)
		, bWriteFunction(false)
	{
	}

	FNiagaraFunctionSignature(FName InName, TArray<FNiagaraVariable>& InInputs, TArray<FNiagaraVariable>& InOutputs, FName InSource, bool bInRequiresContext, bool bInMemberFunction)
		: Name(InName)
		, Inputs(InInputs)
		, Outputs(InOutputs)
		, bRequiresContext(bInRequiresContext)
		, bMemberFunction(bInMemberFunction)
		, bExperimental(false)
		, bSupportsCPU(true)
		, bSupportsGPU(true)
		, bWriteFunction(false)
	{

	}

	FNiagaraFunctionSignature(FName InName, TArray<FNiagaraVariable>& InInputs, TArray<FNiagaraVariable>& InOutputs, FName InSource, bool bInRequiresContext, bool bInMemberFunction, TMap<FName, FName>& InFunctionSpecifiers)
		: Name(InName)
		, Inputs(InInputs)
		, Outputs(InOutputs)
		, bRequiresContext(bInRequiresContext)
		, bMemberFunction(bInMemberFunction)
		, bExperimental(false)
		, bSupportsCPU(true)
		, bSupportsGPU(true)
		, bWriteFunction(false)
		, FunctionSpecifiers(InFunctionSpecifiers)
	{

	}

	bool operator==(const FNiagaraFunctionSignature& Other) const
	{
		bool bFunctionSpecifiersEqual = [&]()
		{
			if (Other.FunctionSpecifiers.Num() != FunctionSpecifiers.Num())
			{
				return false;
			}
			for (const TTuple<FName, FName>& Specifier : FunctionSpecifiers)
			{
				if (Other.FunctionSpecifiers.FindRef(Specifier.Key) != Specifier.Value)
				{
					return false;
				}
			}
			return true;
		}();
		return EqualsIgnoringSpecifiers(Other) && bFunctionSpecifiersEqual;
	}

	bool EqualsIgnoringSpecifiers(const FNiagaraFunctionSignature& Other) const
	{
		bool bMatches = Name.ToString().Equals(Other.Name.ToString());
		bMatches &= Inputs == Other.Inputs;
		bMatches &= Outputs == Other.Outputs;
		bMatches &= bRequiresContext == Other.bRequiresContext;
		bMatches &= bMemberFunction == Other.bMemberFunction;
		bMatches &= OwnerName == Other.OwnerName;
		return bMatches;
	}

	FString GetName()const { return Name.ToString(); }

	void SetDescription(const FText& Desc)
	{
	#if WITH_EDITORONLY_DATA
		Description = Desc;
	#endif
	}
	FText GetDescription() const
	{
	#if WITH_EDITORONLY_DATA
		return Description;
	#else
		return FText::FromName(Name);
	#endif
	}
	bool IsValid()const { return Name != NAME_None && (Inputs.Num() > 0 || Outputs.Num() > 0); }
};



USTRUCT()
struct NIAGARA_API FNiagaraScriptDataInterfaceInfo
{
	GENERATED_USTRUCT_BODY()
public:
	FNiagaraScriptDataInterfaceInfo()
		: DataInterface(nullptr)
		, Name(NAME_None)
		, UserPtrIdx(INDEX_NONE)
	{

	}

	UPROPERTY()
	class UNiagaraDataInterface* DataInterface;
	
	UPROPERTY()
	FName Name;

	/** Index of the user pointer for this data interface. */
	UPROPERTY()
	int32 UserPtrIdx;

	UPROPERTY()
	FNiagaraTypeDefinition Type;

	UPROPERTY()
	FName RegisteredParameterMapRead;

	UPROPERTY()
	FName RegisteredParameterMapWrite;

	//TODO: Allow data interfaces to own datasets
	void CopyTo(FNiagaraScriptDataInterfaceInfo* Destination, UObject* Outer) const;
};

USTRUCT()
struct NIAGARA_API FNiagaraScriptDataInterfaceCompileInfo
{
	GENERATED_USTRUCT_BODY()
public:
	FNiagaraScriptDataInterfaceCompileInfo()
		: Name(NAME_None)
		, UserPtrIdx(INDEX_NONE)
		, bIsPlaceholder(false)
	{

	}

	UPROPERTY()
	FName Name;

	/** Index of the user pointer for this data interface. */
	UPROPERTY()
	int32 UserPtrIdx;

	UPROPERTY()
	FNiagaraTypeDefinition Type;

	// Removed from cooked builds, if we need to add this back the TMap<FName, FName> FunctionSpecifiers should be replaced with an array
#if WITH_EDITORONLY_DATA
	UPROPERTY()
	TArray<FNiagaraFunctionSignature> RegisteredFunctions;
#endif

	UPROPERTY()
	FName RegisteredParameterMapRead;

	UPROPERTY()
	FName RegisteredParameterMapWrite;

	UPROPERTY()
	bool bIsPlaceholder;

	/** Would this data interface work on the target execution type? Only call this on the game thread.*/
	bool CanExecuteOnTarget(ENiagaraSimTarget SimTarget) const;

	/** Note that this is the CDO for this type of data interface, as we often cannot guarantee that the same instance of the data interface we compiled with is the one the user ultimately executes.  Only call this on the game thread.*/
	UNiagaraDataInterface* GetDefaultDataInterface() const;
};

USTRUCT()
struct FNiagaraStatScope
{
	GENERATED_USTRUCT_BODY();

	FNiagaraStatScope() {}
	FNiagaraStatScope(FName InFullName, FName InFriendlyName):FullName(InFullName), FriendlyName(InFriendlyName){}

	UPROPERTY()
	FName FullName;

	UPROPERTY()
	FName FriendlyName;

	bool operator==(const FNiagaraStatScope& Other) const { return FullName == Other.FullName; }
};

USTRUCT()
struct FVMFunctionSpecifier
{
	GENERATED_USTRUCT_BODY();

	FVMFunctionSpecifier() {}
	explicit FVMFunctionSpecifier(FName InKey, FName InValue) : Key(InKey), Value(InValue) {}

	UPROPERTY()
	FName Key;

	UPROPERTY()
	FName Value;
};

USTRUCT()
struct FVMExternalFunctionBindingInfo
{
	GENERATED_USTRUCT_BODY();

	UPROPERTY()
	FName Name;

	UPROPERTY()
	FName OwnerName;

	UPROPERTY()
	TArray<bool> InputParamLocations;

	UPROPERTY()
	int32 NumOutputs;

	UPROPERTY()
	TArray<FVMFunctionSpecifier> FunctionSpecifiers;

	FORCEINLINE int32 GetNumInputs() const { return InputParamLocations.Num(); }
	FORCEINLINE int32 GetNumOutputs() const { return NumOutputs; }

	const FVMFunctionSpecifier* FindSpecifier(const FName& Key) const
	{
		return FunctionSpecifiers.FindByPredicate([&](const FVMFunctionSpecifier& v) -> bool { return v.Key == Key; });
	}

	bool Serialize(FArchive& Ar);

#if WITH_EDITORONLY_DATA
private:
	UPROPERTY()
	TMap<FName, FName> Specifiers_DEPRECATED;
#endif
};

template<>
struct TStructOpsTypeTraits<FVMExternalFunctionBindingInfo> : public TStructOpsTypeTraitsBase2<FVMExternalFunctionBindingInfo>
{
	enum
	{
		WithSerializer = true,
	};
};

/**
Helper for reseting/reinitializing Niagara systems currently active when they are being edited. 
Can be used inside a scope with Systems being reinitialized on destruction or you can store the context and use CommitUpdate() to trigger reinitialization.
For example, this can be split between PreEditChange and PostEditChange to ensure problematic data is not modified during execution of a system.
This can be made a UPROPERTY() to ensure safey in cases where a GC could be possible between Add() and CommitUpdate().
*/
USTRUCT()
struct NIAGARA_API FNiagaraSystemUpdateContext
{
	GENERATED_BODY()

	FNiagaraSystemUpdateContext(const UNiagaraSystem* System, bool bReInit, bool bInDestroyOnAdd = false, bool bInOnlyActive = false) :bDestroyOnAdd(bInDestroyOnAdd), bOnlyActive(bInOnlyActive) { Add(System, bReInit); }
#if WITH_EDITORONLY_DATA
	FNiagaraSystemUpdateContext(const UNiagaraEmitter* Emitter, bool bReInit, bool bInDestroyOnAdd = false, bool bInOnlyActive = false) :bDestroyOnAdd(bInDestroyOnAdd), bOnlyActive(bInOnlyActive)  { Add(Emitter, bReInit); }
	FNiagaraSystemUpdateContext(const UNiagaraScript* Script, bool bReInit, bool bInDestroyOnAdd = false, bool bInOnlyActive = false) :bDestroyOnAdd(bInDestroyOnAdd), bOnlyActive(bInOnlyActive)  { Add(Script, bReInit); }
	//FNiagaraSystemUpdateContext(UNiagaraDataInterface* Interface, bool bReinit) : Add(Interface, bReinit) {}
	FNiagaraSystemUpdateContext(const UNiagaraParameterCollection* Collection, bool bReInit, bool bInDestroyOnAdd = false, bool bInOnlyActive = false) :bDestroyOnAdd(bInDestroyOnAdd), bOnlyActive(bInOnlyActive) { Add(Collection, bReInit); }
#endif
	FNiagaraSystemUpdateContext():bDestroyOnAdd(false), bOnlyActive(false){ }

	~FNiagaraSystemUpdateContext();

	void SetDestroyOnAdd(bool bInDestroyOnAdd) { bDestroyOnAdd = bInDestroyOnAdd; }
	void SetOnlyActive(bool bInOnlyActive) { bOnlyActive = bInOnlyActive; }

	void Add(const UNiagaraSystem* System, bool bReInit);
#if WITH_EDITORONLY_DATA
	void Add(const UNiagaraEmitter* Emitter, bool bReInit);
	void Add(const UNiagaraScript* Script, bool bReInit);
	//void Add(UNiagaraDataInterface* Interface, bool bReinit);
	void Add(const UNiagaraParameterCollection* Collection, bool bReInit);
#endif

	/** Adds all currently active systems.*/
	void AddAll(bool bReInit);

	/** Handles any pending reinits or resets of system instances in this update context. */
	void CommitUpdate();

private:
	void AddInternal(class UNiagaraComponent* Comp, bool bReInit);
	FNiagaraSystemUpdateContext(FNiagaraSystemUpdateContext& Other) :bDestroyOnAdd(false) { }

	UPROPERTY(transient)
	TArray<UNiagaraComponent*> ComponentsToReset;
	UPROPERTY(transient)
	TArray<UNiagaraComponent*> ComponentsToReInit;
	UPROPERTY(transient)
	TArray<UNiagaraSystem*> SystemSimsToDestroy;

	bool bDestroyOnAdd;
	bool bOnlyActive;
	//TODO: When we allow component less systems we'll also want to find and reset those.
};





/** Defines different usages for a niagara script. */
UENUM()
enum class ENiagaraScriptUsage : uint8
{
	/** The script defines a function for use in modules. */
	Function,
	/** The script defines a module for use in particle, emitter, or system scripts. */
	Module,
	/** The script defines a dynamic input for use in particle, emitter, or system scripts. */
	DynamicInput,
	/** The script is called when spawning particles. */
	ParticleSpawnScript,
	/** Particle spawn script that handles intra-frame spawning and also pulls in the update script. */
	ParticleSpawnScriptInterpolated UMETA(Hidden),
	/** The script is called to update particles every frame. */
	ParticleUpdateScript,
	/** The script is called to update particles in response to an event. */
	ParticleEventScript ,
	/** The script is called as a particle simulation stage. */
	ParticleSimulationStageScript,
	/** The script is called to update particles on the GPU. */
	ParticleGPUComputeScript UMETA(Hidden),
	/** The script is called once when the emitter spawns. */
	EmitterSpawnScript,
	/** The script is called every frame to tick the emitter. */
	EmitterUpdateScript ,
	/** The script is called once when the system spawns. */
	SystemSpawnScript ,
	/** The script is called every frame to tick the system. */
	SystemUpdateScript,
};

UENUM()
enum class ENiagaraScriptGroup : uint8
{
	Particle = 0,
	Emitter,
	System,
	Max
};


UENUM()
enum class ENiagaraIterationSource : uint8
{
	Particles = 0,
	DataInterface
};


/** Defines all you need to know about a variable.*/
USTRUCT()
struct FNiagaraVariableInfo
{
	GENERATED_USTRUCT_BODY();

	FNiagaraVariableInfo() : DataInterface(nullptr) {}

	UPROPERTY()
	FNiagaraVariable Variable;

	UPROPERTY()
	FText Definition;

	UPROPERTY()
	UNiagaraDataInterface* DataInterface;
};

USTRUCT()
struct FNiagaraVariableAttributeBinding
{
	GENERATED_USTRUCT_BODY();

	FNiagaraVariableAttributeBinding() {}
	FNiagaraVariableAttributeBinding(const FNiagaraVariable& InVar, const FNiagaraVariable& InAttrVar) : BoundVariable(InVar), DataSetVariable(InAttrVar), DefaultValueIfNonExistent(InAttrVar)
	{
		check(InVar.GetType() == InAttrVar.GetType());
	}
	FNiagaraVariableAttributeBinding(const FNiagaraVariable& InVar, const FNiagaraVariable& InAttrVar, const FNiagaraVariable& InNonExistentValue) : BoundVariable(InVar), DataSetVariable(InAttrVar), DefaultValueIfNonExistent(InNonExistentValue)
	{
		check(InVar.GetType() == InAttrVar.GetType() && InNonExistentValue.GetType() == InAttrVar.GetType());
	}


	UPROPERTY()
	FNiagaraVariable BoundVariable;

	UPROPERTY()
	FNiagaraVariable DataSetVariable;

	UPROPERTY()
	FNiagaraVariable DefaultValueIfNonExistent;
};

USTRUCT()
struct FNiagaraVariableDataInterfaceBinding
{
	GENERATED_USTRUCT_BODY();

	FNiagaraVariableDataInterfaceBinding() {}
	FNiagaraVariableDataInterfaceBinding(const FNiagaraVariable& InVar) : BoundVariable(InVar)
	{
		ensure(InVar.IsDataInterface() == true);
	}

	UPROPERTY()
	FNiagaraVariable BoundVariable;

};

/** Primarily a wrapper around an FName to be used for customizations in the Selected Details panel 
    to select a default binding to initialize module inputs. The customization implementation
    is FNiagaraScriptVariableBindingCustomization. */
USTRUCT()
struct FNiagaraScriptVariableBinding
{
	GENERATED_USTRUCT_BODY();

	FNiagaraScriptVariableBinding() {}
	FNiagaraScriptVariableBinding(const FNiagaraVariable& InVar) : Name(InVar.GetName())
	{
		
	}
	FNiagaraScriptVariableBinding(const FName& InName) : Name(InName)
	{
		
	}

	UPROPERTY(EditAnywhere, Category = "Variable")
	FName Name;

	FName GetName() const { return Name; }
	void SetName(FName InName) { Name = InName; }
	bool IsValid() const { return Name != NAME_None; }
};

namespace FNiagaraUtilities
{
	/** Builds a unique name from a candidate name and a set of existing names.  The candidate name will be made unique
	if necessary by adding a 3 digit index to the end. */
	FName NIAGARA_API GetUniqueName(FName CandidateName, const TSet<FName>& ExistingNames);

	FNiagaraVariable NIAGARA_API ConvertVariableToRapidIterationConstantName(FNiagaraVariable InVar, const TCHAR* InEmitterName, ENiagaraScriptUsage InUsage);

	void CollectScriptDataInterfaceParameters(const UObject& Owner, const TArrayView<UNiagaraScript*>& Scripts, FNiagaraParameterStore& OutDataInterfaceParameters);

	inline bool SupportsNiagaraRendering(ERHIFeatureLevel::Type FeatureLevel)
	{
		return FeatureLevel == ERHIFeatureLevel::SM5 || FeatureLevel == ERHIFeatureLevel::ES3_1;
	}

	inline bool SupportsNiagaraRendering(EShaderPlatform ShaderPlatform)
	{
		// Note:
		// IsFeatureLevelSupported does a FeatureLevel < MaxFeatureLevel(ShaderPlatform) so checking ES3.1 support will return true for SM5. I added it explicitly to be clear what we are doing.
		return IsFeatureLevelSupported(ShaderPlatform, ERHIFeatureLevel::SM5) || IsFeatureLevelSupported(ShaderPlatform, ERHIFeatureLevel::ES3_1);
	}

	// Whether the platform supports GPU particles. A static function that doesn't not rely on any runtime switches.
	inline bool SupportsGPUParticles(EShaderPlatform ShaderPlatform)
	{
		return RHISupportsComputeShaders(ShaderPlatform);
	}

	// Whether GPU particles are currently allowed. Could change depending on config and runtime switches.
	bool AllowGPUParticles(EShaderPlatform ShaderPlatform);

	// Whether compute shaders are allowed. Could change depending on config and runtime switches.
	bool AllowComputeShaders(EShaderPlatform ShaderPlatform);
	
#if WITH_EDITORONLY_DATA
	/**
	 * Prepares rapid iteration parameter stores for simulation by removing old parameters no longer used by functions, by initializing new parameters
	 * added to functions, and by copying parameters across parameter stores for interscript dependencies.
	 * @param Scripts The scripts who's rapid iteration parameter stores will be processed.
	 * @param ScriptDependencyMap A map of script dependencies where the key is the source script and the value is the script which depends on the source.  All scripts in this
	 * map must be contained in the Scripts array, both keys and values.
	 * @param ScriptToEmitterNameMap An array of scripts to the name of the emitter than owns them.  If this is a system script the name can be empty.  All scripts in the
	 * scripts array must have an entry in this map.
	 */
	void NIAGARA_API PrepareRapidIterationParameters(const TArray<UNiagaraScript*>& Scripts, const TMap<UNiagaraScript*, UNiagaraScript*>& ScriptDependencyMap, const TMap<UNiagaraScript*, FString>& ScriptToEmitterNameMap);
#endif

	void NIAGARA_API DumpHLSLText(const FString& SourceCode, const FString& DebugName);

	NIAGARA_API FString SystemInstanceIDToString(FNiagaraSystemInstanceID ID);
};

USTRUCT()
struct FNiagaraUserParameterBinding
{
	GENERATED_USTRUCT_BODY()

	FNiagaraUserParameterBinding();

	UPROPERTY(EditAnywhere, Category = "User Parameter")
	FNiagaraVariable Parameter;

	FORCEINLINE bool operator==(const FNiagaraUserParameterBinding& Other)const
	{
		return Other.Parameter == Parameter;
	}
};

USTRUCT()
struct FNiagaraRandInfo
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(EditAnywhere, Category = "Random")
	int32 Seed1;
	
	UPROPERTY(EditAnywhere, Category = "Random")
	int32 Seed2;

	UPROPERTY(EditAnywhere, Category = "Random")
	int32 Seed3;
};


//////////////////////////////////////////////////////////////////////////
// Legacy Anim Trail Support


/** 
Controls the way that the width scale property affects animation trails. 
Only used for Legacy Anim Trail support when converting from Cascade to Niagara.
*/
UENUM()
enum class ENiagaraLegacyTrailWidthMode : uint32
{
	FromCentre,
	FromFirst,
	FromSecond,
};
