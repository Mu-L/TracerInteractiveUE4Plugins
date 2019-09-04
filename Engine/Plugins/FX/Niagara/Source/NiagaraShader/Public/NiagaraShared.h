// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	NiagaraShared.h: Shared Niagara definitions.
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "Containers/IndirectArray.h"
#include "Misc/Guid.h"
#include "Engine/EngineTypes.h"
#include "Templates/RefCounting.h"
#include "Templates/ScopedPointer.h"
#include "Misc/SecureHash.h"
#include "RHI.h"
#include "RenderResource.h"
#include "RenderingThread.h"
#include "UniformBuffer.h"
#include "Shader.h"
#include "VertexFactory.h"
#include "SceneTypes.h"
#include "StaticParameterSet.h"
#include "Misc/Optional.h"
#include "NiagaraCompileHash.h"
#include "NiagaraShared.generated.h"

class FNiagaraShaderScript;
class FNiagaraShaderMap;
class FNiagaraShader;
class FNiagaraShaderMapId;
class UNiagaraScript;
struct FNiagaraDataInterfaceParametersCS;

#define MAX_CONCURRENT_EVENT_DATASETS 4

/** Defines the compile event types for translation/compilation.*/
UENUM()
enum class FNiagaraCompileEventSeverity : uint8
{
	Log = 0,
	Warning = 1,
	Error = 2
};

/** Records necessary information to give UI cues for errors/logs/warnings during compile.*/
USTRUCT()
struct FNiagaraCompileEvent
{
	GENERATED_USTRUCT_BODY()
public:
	FNiagaraCompileEvent()
	{
		Severity = FNiagaraCompileEventSeverity::Log;
		Message = FString();
		NodeGuid = FGuid();
		PinGuid = FGuid();
		StackGuids.Empty();
	}

	FNiagaraCompileEvent(FNiagaraCompileEventSeverity InSeverity, const FString& InMessage, FGuid InNodeGuid = FGuid(), FGuid InPinGuid = FGuid(), const TArray<FGuid>& InCallstackGuids = TArray<FGuid>())
		: Severity(InSeverity), Message(InMessage), NodeGuid(InNodeGuid), PinGuid(InPinGuid), StackGuids(InCallstackGuids) {}

	/** Whether or not this is an error, warning, or info*/
	UPROPERTY()
	FNiagaraCompileEventSeverity Severity;
	/* The message itself*/
	UPROPERTY()
	FString Message;
	/** The node guid that generated the compile event*/
	UPROPERTY()
	FGuid NodeGuid;
	/** The pin persistent id that generated the compile event*/
	UPROPERTY()
	FGuid PinGuid;
	/** The compile stack frame of node id's*/
	UPROPERTY()
	TArray<FGuid> StackGuids;
};

/**
* Data coming from that translator that describes parameters needed for each data interface.
*/
USTRUCT()
struct NIAGARASHADER_API FNiagaraDataInterfaceGPUParamInfo
{
	GENERATED_USTRUCT_BODY()
		
	/** Symbol of this DI in the hlsl. Used for binding parameters. */
	UPROPERTY()
	FString DataInterfaceHLSLSymbol;

	/** Name of the class for this data interface. Used for constructing the correct parameters struct. */
	UPROPERTY()
	FString DIClassName;

	bool Serialize(FArchive& Ar);
};

template<> struct TStructOpsTypeTraits<FNiagaraDataInterfaceGPUParamInfo> : public TStructOpsTypeTraitsBase2<FNiagaraDataInterfaceGPUParamInfo>
{
	enum
	{
		WithSerializer = true,
	};
};

/** 
* Shader side data needed for binding data interface parameters.
*/
struct FNiagaraDataInterfaceParamRef
{
public:

	FNiagaraDataInterfaceParamRef(const FNiagaraDataInterfaceGPUParamInfo& InParameterInfo);
	FNiagaraDataInterfaceParamRef();
	~FNiagaraDataInterfaceParamRef();
	friend bool operator<<(FArchive& Ar, FNiagaraDataInterfaceParamRef& Desc);

	void Bind(const class FShaderParameterMap& ParameterMap);
	void ConstructParameters();
	void InitDIClass();

	FNiagaraDataInterfaceGPUParamInfo ParameterInfo;

	/** The class of this DI which we can use to create the parameter struct. */
	UClass* DIClass;

	/** Pointer to parameters struct for this data interface. */
	FNiagaraDataInterfaceParametersCS* Parameters;
};




/** Stores outputs from the script compile that need to be saved. */
class FNiagaraComputeShaderCompilationOutput
{
public:
	FNiagaraComputeShaderCompilationOutput()
	{}

	NIAGARASHADER_API void Serialize(FArchive& Ar)
	{}
};



/** Contains all the information needed to uniquely identify a FNiagaraShaderMapID. */
class FNiagaraShaderMapId
{
public:
	/** The version of the compiler that this needs to be built against.*/
	FGuid CompilerVersionID;

	/** Feature level that the shader map is going to be compiled for.  */
	ERHIFeatureLevel::Type FeatureLevel;

	/**
	* The base id of the subgraph this shader primarily represents.
	*/
	FGuid BaseScriptID;

	/**
	* The hash of the subgraph this shader primarily represents.
	*/
	FNiagaraCompileHash BaseCompileHash;

	/** The compile hashes of the top level scripts the script is dependent on. */
	TArray<FNiagaraCompileHash> ReferencedCompileHashes;

	/** Guids of any functions or module scripts the script was dependent on. */
	TArray<FGuid> ReferencedDependencyIds;

	FNiagaraShaderMapId()
		: CompilerVersionID()
		, FeatureLevel(GMaxRHIFeatureLevel)
	{ }

	~FNiagaraShaderMapId()
	{ }

	//ENGINE_API void SetShaderDependencies(const TArray<FShaderType*>& ShaderTypes, const TArray<const FShaderPipelineType*>& ShaderPipelineTypes, const TArray<FVertexFactoryType*>& VFTypes);

	void Serialize(FArchive& Ar);

	friend uint32 GetTypeHash(const FNiagaraShaderMapId& Ref)
	{
		return Ref.BaseScriptID.A;
	}

	SIZE_T GetSizeBytes() const
	{
		return sizeof(*this);
	}

	/** Hashes the script-specific part of this shader map Id. */
	void GetScriptHash(FSHAHash& OutHash) const;

	/**
	* Tests this set against another for equality, disregarding override settings.
	*
	* @param ReferenceSet	The set to compare against
	* @return				true if the sets are equal
	*/
	bool operator==(const FNiagaraShaderMapId& ReferenceSet) const;

	bool operator!=(const FNiagaraShaderMapId& ReferenceSet) const
	{
		return !(*this == ReferenceSet);
	}

	/** Appends string representations of this Id to a key string. */
	void AppendKeyString(FString& KeyString) const;

	/** Returns true if the requested shader type is a dependency of this shader map Id. */
	//bool ContainsShaderType(const FShaderType* ShaderType) const;
};






// Runtime code sticks scripts to compile along with their shader map here
// Niagara Editor ticks in FNiagaraShaderQueueTickable, kicking off compile jobs
#if WITH_EDITORONLY_DATA

class FNiagaraCompilationQueue
{
public:
	struct NiagaraCompilationQueueItem
	{
		FNiagaraShaderScript* Script;
		TRefCountPtr<FNiagaraShaderMap>ShaderMap;
		FNiagaraShaderMapId ShaderMapId;
		EShaderPlatform Platform;
		bool bApply;
	};

	static FNiagaraCompilationQueue *Get()
	{
		if (Singleton == nullptr)
		{
			Singleton = new FNiagaraCompilationQueue();
		}
		return Singleton;
	}

	TArray<NiagaraCompilationQueueItem> &GetQueue()
	{
		return CompilationQueue;
	}

	void Queue(FNiagaraShaderScript *InScript, TRefCountPtr<FNiagaraShaderMap>InShaderMap, const FNiagaraShaderMapId &MapId, EShaderPlatform InPlatform, bool InApply)
	{
		check(IsInGameThread());
		NiagaraCompilationQueueItem NewQueueItem;
		NewQueueItem.Script = InScript;
		NewQueueItem.ShaderMap = InShaderMap;
		NewQueueItem.ShaderMapId = MapId;
		NewQueueItem.Platform = InPlatform;
		NewQueueItem.bApply = InApply;
		CompilationQueue.Add(NewQueueItem);
	}

	void RemovePending(FNiagaraShaderScript* InScript)
	{
		check(IsInGameThread());
		for (NiagaraCompilationQueueItem& Item : CompilationQueue)
		{
			if (Item.Script == InScript)
			{
				Item.Script = nullptr;
			}
		}
	}

private:
	TArray<NiagaraCompilationQueueItem> CompilationQueue;
	NIAGARASHADER_API static FNiagaraCompilationQueue *Singleton;
};

#endif






/**
* The set of shaders for a single script.
*/
class FNiagaraShaderMap : public TShaderMap<FNiagaraShaderType>, public FDeferredCleanupInterface
{
public:

	/**
	* Finds the shader map for a script.
	* @param Platform - The platform to lookup for
	* @return NULL if no cached shader map was found.
	*/
	static FNiagaraShaderMap* FindId(const FNiagaraShaderMapId& ShaderMapId, EShaderPlatform Platform);

	/** Flushes the given shader types from any loaded FNiagaraShaderMaps. */
	static void FlushShaderTypes(TArray<FShaderType*>& ShaderTypesToFlush);

	static void FixupShaderTypes(EShaderPlatform Platform,
		const TMap<FShaderType*, FString>& ShaderTypeNames);

	/**
	* Attempts to load the shader map for the given script from the Derived Data Cache.
	* If InOutShaderMap is valid, attempts to load the individual missing shaders instead.
	*/
	static void LoadFromDerivedDataCache(const FNiagaraShaderScript* Script, const FNiagaraShaderMapId& ShaderMapId, EShaderPlatform Platform, TRefCountPtr<FNiagaraShaderMap>& InOutShaderMap);

	FNiagaraShaderMap();

	// Destructor.
	~FNiagaraShaderMap();

	/**
	* Compiles the shaders for a script and caches them in this shader map.
	* @param script - The script to compile shaders for.
	* @param ShaderMapId - the set of static parameters to compile for
	* @param Platform - The platform to compile to
	*/
	NIAGARASHADER_API void Compile(
		FNiagaraShaderScript* Script,
		const FNiagaraShaderMapId& ShaderMapId,
		TRefCountPtr<FShaderCompilerEnvironment> CompilationEnvironment,
		const FNiagaraComputeShaderCompilationOutput& InNiagaraCompilationOutput,
		EShaderPlatform Platform,
		bool bSynchronousCompile,
		bool bApplyCompletedShaderMapForRendering
		);

	/** Sorts the incoming compiled jobs into the appropriate mesh shader maps, and finalizes this shader map so that it can be used for rendering. */
	bool ProcessCompilationResults(const TArray<class FShaderCommonCompileJob*>& InCompilationResults, int32& ResultIndex, float& TimeBudget);

	/**
	* Checks whether the shader map is missing any shader types necessary for the given script.
	* @param Script - The Niagara Script which is checked.
	* @return True if the shader map has all of the shader types necessary.
	*/
	bool IsComplete(const FNiagaraShaderScript* Script, bool bSilent);

	/** Attempts to load missing shaders from memory. */
	void LoadMissingShadersFromMemory(const FNiagaraShaderScript* Script);

	/**
	* Checks to see if the shader map is already being compiled for another script, and if so
	* adds the specified script to the list to be applied to once the compile finishes.
	* @param Script - The Niagara Script we also wish to apply the compiled shader map to.
	* @return True if the shader map was being compiled and we added Script to the list to be applied.
	*/
	bool TryToAddToExistingCompilationTask(FNiagaraShaderScript* Script);

	/** Builds a list of the shaders in a shader map. */
	NIAGARASHADER_API  void GetShaderList(TMap<FShaderId, FShader*>& OutShaders) const;

	/** Builds a list of the shader pipelines in a shader map. */
	//ENGINE_API void GetShaderPipelineList(TArray<FShaderPipeline*>& OutShaderPipelines) const;

	/** Registers a niagara shader map in the global map so it can be used by Niagara scripts. */
	void Register(EShaderPlatform InShaderPlatform);

	// Reference counting.
	NIAGARASHADER_API  void AddRef();
	NIAGARASHADER_API  void Release();

	/**
	* Removes all entries in the cache with exceptions based on a shader type
	* @param ShaderType - The shader type to flush
	*/
	void FlushShadersByShaderType(FShaderType* ShaderType);

	/** Removes a Script from NiagaraShaderMapsBeingCompiled. */
	NIAGARASHADER_API static void RemovePendingScript(FNiagaraShaderScript* Script);
	NIAGARASHADER_API static void RemovePendingMap(FNiagaraShaderMap* Map);

	/** Finds a shader map currently being compiled that was enqueued for the given script. */
	static const FNiagaraShaderMap* GetShaderMapBeingCompiled(const FNiagaraShaderScript* Script);

	/** Serializes the shader map. */
	void Serialize(FArchive& Ar, bool bInlineShaderResources = true);

	/** Saves this shader map to the derived data cache. */
	void SaveToDerivedDataCache();

	/** Registers all shaders that have been loaded in Serialize */
	virtual void RegisterSerializedShaders(bool bCookedMaterial) override;
	virtual void DiscardSerializedShaders() override;

	/** Backs up any FShaders in this shader map to memory through serialization and clears FShader references. */
	TArray<uint8>* BackupShadersToMemory();
	/** Recreates FShaders from the passed in memory, handling shader key changes. */
	void RestoreShadersFromMemory(const TArray<uint8>& ShaderData);

	/** Serializes a shader map to an archive (used with recompiling shaders for a remote console) */
	NIAGARASHADER_API  static void SaveForRemoteRecompile(FArchive& Ar, const TMap<FString, TArray<TRefCountPtr<FNiagaraShaderMap> > >& CompiledShaderMaps, const TArray<FShaderResourceId>& ClientResourceIds);
	NIAGARASHADER_API  static void LoadForRemoteRecompile(FArchive& Ar, EShaderPlatform ShaderPlatform, const TArray<FString>& ScriptsForShaderMaps);

	/** Computes the memory used by this shader map without counting the shaders themselves. */
	uint32 GetSizeBytes() const
	{
		return sizeof(*this)
			+FriendlyName.GetAllocatedSize();
	}

	/** Returns the maximum number of texture samplers used by any shader in this shader map. */
	uint32 GetMaxTextureSamplers() const;


	// Accessors.
	const FNiagaraShaderMapId& GetShaderMapId() const { return ShaderMapId; }
	EShaderPlatform GetShaderPlatform() const	{ return Platform; }
	const FString& GetFriendlyName() const		{ return FriendlyName; }
	uint32 GetCompilingId() const				{ return CompilingId; }
	bool IsCompilationFinalized() const			{ return bCompilationFinalized; }
	bool CompiledSuccessfully() const			{ return bCompiledSuccessfully; }
	const FString& GetDebugDescription() const	{ return DebugDescription; }

	bool IsValid() const
	{
		return bCompilationFinalized && bCompiledSuccessfully && !bDeletedThroughDeferredCleanup;
	}

	//const FUniformExpressionSet& GetUniformExpressionSet() const { return NiagaraCompilationOutput.UniformExpressionSet; }

	int32 GetNumRefs() const { return NumRefs; }
	uint32 GetCompilingId()  { return CompilingId; }
	static TMap<TRefCountPtr<FNiagaraShaderMap>, TArray<FNiagaraShaderScript*> > &GetInFlightShaderMaps() 
	{
		//All access to NiagaraShaderMapsBeingCompiled must be done on the game thread!
		check(IsInGameThread());
		return NiagaraShaderMapsBeingCompiled; 
	}

	void SetCompiledSuccessfully(bool bSuccess) { bCompiledSuccessfully = bSuccess; }
private:

	/**
	* A global map from a script's ID and static switch set to any shader map cached for that script.
	* Note: this does not necessarily contain all script shader maps in memory.  Shader maps with the same key can evict each other.
	* No ref counting needed as these are removed on destruction of the shader map.
	*/
	static TMap<FNiagaraShaderMapId, FNiagaraShaderMap*> GIdToNiagaraShaderMap[SP_NumPlatforms];

	/**
	* All script shader maps in memory.
	* No ref counting needed as these are removed on destruction of the shader map.
	*/
	static TArray<FNiagaraShaderMap*> AllNiagaraShaderMaps;

	/** The script's user friendly name, typically the object name. */
	FString FriendlyName;

	/** The platform this shader map was compiled with */
	EShaderPlatform Platform;

	/** The static parameter set that this shader map was compiled with */
	FNiagaraShaderMapId ShaderMapId;

	/** Shader compilation output */
	FNiagaraComputeShaderCompilationOutput NiagaraCompilationOutput;

	/** Next value for CompilingId. */
	static uint32 NextCompilingId;

	/** Tracks resources and their shader maps that need to be compiled but whose compilation is being deferred. */
	static TMap<TRefCountPtr<FNiagaraShaderMap>, TArray<FNiagaraShaderScript*> > NiagaraShaderMapsBeingCompiled;

	/** Uniquely identifies this shader map during compilation, needed for deferred compilation where shaders from multiple shader maps are compiled together. */
	uint32 CompilingId;

	mutable int32 NumRefs;

	/** Used to catch errors where the shader map is deleted directly. */
	bool bDeletedThroughDeferredCleanup;

	/** Indicates whether this shader map has been registered in GIdToNiagaraShaderMap */
	uint32 bRegistered : 1;

	/**
	* Indicates whether this shader map has had ProcessCompilationResults called after Compile.
	* The shader map must not be used on the rendering thread unless bCompilationFinalized is true.
	*/
	uint32 bCompilationFinalized : 1;

	uint32 bCompiledSuccessfully : 1;

	/** Indicates whether the shader map should be stored in the shader cache. */
	uint32 bIsPersistent : 1;

	/** Debug information about how the shader map was compiled. */
	FString DebugDescription;

	FShader* ProcessCompilationResultsForSingleJob(class FShaderCommonCompileJob* SingleJob, const FSHAHash& ShaderMapHash);

	bool IsNiagaraShaderComplete(const FNiagaraShaderScript* Script, const FNiagaraShaderType* ShaderType, bool bSilent);

	/** Initializes OrderedMeshShaderMaps from the contents of MeshShaderMaps. */
	void InitOrderedMeshShaderMaps();

	friend NIAGARASHADER_API  void DumpNiagaraStats(EShaderPlatform Platform);
	friend class FShaderCompilingManager;
};


DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnNiagaraScriptCompilationComplete);

/**
 * FNiagaraShaderScript represents a Niagara script to the shader compilation process
 */
class NIAGARASHADER_VTABLE FNiagaraShaderScript
{
public:

	/**
	 * Minimal initialization constructor.
	 */
	FNiagaraShaderScript() :
		GameThreadShaderMap(NULL),
		RenderingThreadShaderMap(NULL),
		FeatureLevel(GMaxRHIFeatureLevel),
		bLoadedCookedShaderMapId(false)
	{}

	/**
	 * Destructor
	 */
	NIAGARASHADER_API  virtual ~FNiagaraShaderScript();

	/**
	 * Caches the shaders for this script with no static parameters on the given platform.
	 * This is used by FNiagaraShaderScript
	 */
	NIAGARASHADER_API  bool CacheShaders(EShaderPlatform Platform, bool bApplyCompletedShaderMapForRendering, bool bForceRecompile, bool bSynchronous = false);
	bool CacheShaders(const FNiagaraShaderMapId& ShaderMapId, EShaderPlatform Platform, bool bApplyCompletedShaderMapForRendering, bool bForceRecompile, bool bSynchronous = false);

	/**
	 * Should the shader for this script with the given platform, shader type and vertex
	 * factory type combination be compiled
	 *
	 * @param Platform		The platform currently being compiled for
	 * @param ShaderType	Which shader is being compiled
	 * @param VertexFactory	Which vertex factory is being compiled (can be NULL)
	 *
	 * @return true if the shader should be compiled
	 */
	NIAGARASHADER_API  virtual bool ShouldCache(EShaderPlatform Platform, const FShaderType* ShaderType) const;

	/** Serializes the script. */
	NIAGARASHADER_API  virtual void LegacySerialize(FArchive& Ar);

	NIAGARASHADER_API void SerializeShaderMap(FArchive& Ar);
	/** Releases this script's shader map.  Must only be called on scripts not exposed to the rendering thread! */
	NIAGARASHADER_API void ReleaseShaderMap();

	void GetDependentShaderTypes(EShaderPlatform Platform, TArray<FShaderType*>& OutShaderTypes) const;
	NIAGARASHADER_API  virtual void GetShaderMapId(EShaderPlatform Platform, FNiagaraShaderMapId& OutId) const;

	NIAGARASHADER_API void Invalidate();

	/**
	 * Should shaders compiled for this script be saved to disk?
	 */
	virtual bool IsPersistent() const { return true; }

	/**
	* Called when compilation finishes, after the GameThreadShaderMap is set and the render command to set the RenderThreadShaderMap is queued
	*/
	NIAGARASHADER_API virtual void NotifyCompilationFinished();

	/**
	* Cancels all outstanding compilation jobs
	*/
	NIAGARASHADER_API  void CancelCompilation();

	/**
	 * Blocks until compilation has completed. Returns immediately if a compilation is not outstanding.
	 */
	NIAGARASHADER_API  void FinishCompilation();

	/**
	 * Checks if the compilation for this shader is finished
	 *
	 * @return returns true if compilation is complete false otherwise
	 */
	NIAGARASHADER_API  bool IsCompilationFinished() const;

	/**
	* Checks if there is a valid GameThreadShaderMap, that is, the script can be run
	*
	* @return returns true if there is a GameThreadShaderMap.
	*/
	NIAGARASHADER_API  bool HasValidGameThreadShaderMap() const;


	// Accessors.
	const TArray<FString>& GetCompileErrors() const { return CompileErrors; }
	void SetCompileErrors(const TArray<FString>& InCompileErrors) { CompileErrors = InCompileErrors; }

	ERHIFeatureLevel::Type GetFeatureLevel() const { return FeatureLevel; }

	class FNiagaraShaderMap* GetGameThreadShaderMap() const
	{
		checkSlow(IsInGameThread() || IsInAsyncLoadingThread());
		return GameThreadShaderMap;
	}

	NIAGARASHADER_API void RegisterShaderMap();

	NIAGARASHADER_API void DiscardShaderMap();

	/** Note: SetRenderingThreadShaderMap must also be called with the same value, but from the rendering thread. */
	void SetGameThreadShaderMap(FNiagaraShaderMap* InShaderMap)
	{
		checkSlow(IsInGameThread() || IsInAsyncLoadingThread());
		GameThreadShaderMap = InShaderMap;
	}

	/** Note: SetGameThreadShaderMap must also be called with the same value, but from the game thread. */
	NIAGARASHADER_API  void SetRenderingThreadShaderMap(FNiagaraShaderMap* InShaderMap);

	void AddCompileId(uint32 Id) 
	{
		check(IsInGameThread());
		OutstandingCompileShaderMapIds.Add(Id);
	}

	void SetShaderMap(FNiagaraShaderMap* InShaderMap)
	{
		checkSlow(IsInGameThread() || IsInAsyncLoadingThread());
		GameThreadShaderMap = InShaderMap;
		bLoadedCookedShaderMapId = true;
		CookedShaderMapId = InShaderMap->GetShaderMapId();

	}

	NIAGARASHADER_API  class FNiagaraShaderMap* GetRenderingThreadShaderMap() const;


	NIAGARASHADER_API void RemoveOutstandingCompileId(const int32 OldOutstandingCompileShaderMapId);

	NIAGARASHADER_API  virtual void AddReferencedObjects(FReferenceCollector& Collector);

	//virtual const TArray<UTexture*>& GetReferencedTextures() const = 0;


	/** Returns true if this script is allowed to make development shaders via the global CVar CompileShadersForDevelopment. */
	//virtual bool GetAllowDevelopmentShaderCompile()const{ return true; }

	/**
	* Get user source code for the shader
	* @param OutSource - generated source code
	* @param OutHighlightMap - source code highlight list
	* @return - true on Success
	*/
	NIAGARASHADER_API  bool GetScriptHLSLSource(FString& OutSource) {
		OutSource = HlslOutput;
		return true;
	};

	const FString& GetFriendlyName()	const { return FriendlyName; }


	NIAGARASHADER_API void SetScript(UNiagaraScript *InScript, ERHIFeatureLevel::Type InFeatureLevel, const FGuid& InCompilerVersion, const FGuid& InBaseScriptID,
		const FNiagaraCompileHash& InBaseCompileHash, const TArray<FNiagaraCompileHash>& InReferencedCompileHashes, const TArray<FGuid>& InReferencedDependencyIds, FString InFriendlyName);

	UNiagaraScript *GetBaseVMScript()
	{
		return BaseVMScript;
	}

	void SetCompileErrors(TArray<FString> &InErrors)
	{
		CompileErrors = InErrors;
	}

	FString SourceName;
	
	FString HlslOutput;

	NIAGARASHADER_API  FNiagaraShader* GetShader() const;
	NIAGARASHADER_API  FNiagaraShader* GetShaderGameThread() const;
	
	NIAGARASHADER_API void SetDataInterfaceParamInfo(TArray< FNiagaraDataInterfaceGPUParamInfo >& InDIParamInfo);
	NIAGARASHADER_API void SetDataInterfaceParamInfo(TArray< FNiagaraDataInterfaceParamRef >& InDIParamRefs);

	TArray< FNiagaraDataInterfaceGPUParamInfo >& GetDataInterfaceParamInfo()
	{
		return DIParamInfo;
	}

	NIAGARASHADER_API FOnNiagaraScriptCompilationComplete& OnCompilationComplete()
	{
		return OnCompilationCompleteDelegate;
	}

	bool IsSame(const FNiagaraShaderMapId& InId) const;
protected:

	// shared code needed for GetUniformScalarParameterExpressions, GetUniformVectorParameterExpressions, GetUniformCubeTextureExpressions..
	// @return can be 0
	const FNiagaraShaderMap* GetShaderMapToUse() const;

	/**
	* Fills the passed array with IDs of shader maps unfinished compilation jobs.
	*/
	void GetShaderMapIDsWithUnfinishedCompilation(TArray<int32>& ShaderMapIds);


	void SetFeatureLevel(ERHIFeatureLevel::Type InFeatureLevel)
	{
		FeatureLevel = InFeatureLevel;
	}

private:
	UNiagaraScript* BaseVMScript;

	TArray<FString> CompileErrors;

	/** 
	 * Game thread tracked shader map, which is ref counted and manages shader map lifetime. 
	 * The shader map uses deferred deletion so that the rendering thread has a chance to process a release command when the shader map is no longer referenced.
	 * Code that sets this is responsible for updating RenderingThreadShaderMap in a thread safe way.
	 * During an async compile, this will be NULL and will not contain the actual shader map until compilation is complete.
	 */
	TRefCountPtr<FNiagaraShaderMap> GameThreadShaderMap;

	/** 
	 * Shader map for this FNiagaraShaderScript which is accessible by the rendering thread. 
	 * This must be updated along with GameThreadShaderMap, but on the rendering thread.
	 */
	FNiagaraShaderMap* RenderingThreadShaderMap;

	// Information describing data interface parameters. These come from the HLSL translators and need to be passed down to the shader for binding
	TArray< FNiagaraDataInterfaceGPUParamInfo > DIParamInfo;

	/** Guid id for base script*/
	FGuid BaseScriptId;

	/** Compile hash for the base script. */
	FNiagaraCompileHash BaseCompileHash;

	/** The compiler version the script was generated with.*/
	FGuid CompilerVersionId;

	/** The compile hashes for the top level scripts referenced by the script. */
	TArray<FNiagaraCompileHash> ReferencedCompileHashes;

	/** Dependencies of the script*/
	TArray<FGuid> ReferencedDependencyIds;

	/** 
	 * Contains the compiling id of this shader map when it is being compiled asynchronously. 
	 * This can be used to access the shader map during async compiling, since GameThreadShaderMap will not have been set yet.
	 */
	TArray<int32, TInlineAllocator<1> > OutstandingCompileShaderMapIds;

	/** Feature level that this script is representing. */
	ERHIFeatureLevel::Type FeatureLevel;

	uint32 bLoadedCookedShaderMapId : 1;
	FNiagaraShaderMapId CookedShaderMapId;

	FOnNiagaraScriptCompilationComplete OnCompilationCompleteDelegate;

	/**
	* Compiles this script for Platform, storing the result in OutShaderMap if the compile was synchronous
	*/
	bool BeginCompileShaderMap(
		const FNiagaraShaderMapId& ShaderMapId,
		EShaderPlatform Platform, 
		TRefCountPtr<class FNiagaraShaderMap>& OutShaderMap, 
		bool bApplyCompletedShaderMapForRendering,
		bool bSynchronous = false);

	/** Populates OutEnvironment with defines needed to compile shaders for this script. */
	void SetupShaderCompilationEnvironment(
		EShaderPlatform Platform,
		FShaderCompilerEnvironment& OutEnvironment
		) const;


	FString FriendlyName;

	friend class FNiagaraShaderMap;
	friend class FShaderCompilingManager;
};


