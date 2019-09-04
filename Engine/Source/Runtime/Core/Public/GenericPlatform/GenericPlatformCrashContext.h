// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "HAL/PlatformMemory.h"
#include "Containers/UnrealString.h"

struct FProgramCounterSymbolInfo;

/** 
 * Symbol information associated with a program counter. 
 * FString version.
 * To be used by external tools.
 */
struct CORE_API FProgramCounterSymbolInfoEx
{
	/** Module name. */
	FString	ModuleName;

	/** Function name. */
	FString	FunctionName;

	/** Filename. */
	FString	Filename;

	/** Line number in file. */
	uint32	LineNumber;

	/** Symbol displacement of address.	*/
	uint64	SymbolDisplacement;

	/** Program counter offset into module. */
	uint64	OffsetInModule;

	/** Program counter. */
	uint64	ProgramCounter;

	/** Default constructor. */
	FProgramCounterSymbolInfoEx( FString InModuleName, FString InFunctionName, FString InFilename, uint32 InLineNumber, uint64 InSymbolDisplacement, uint64 InOffsetInModule, uint64 InProgramCounter );
};


/** Enumerates crash description versions. */
enum class ECrashDescVersions : int32
{
	/** Introduces a new crash description format. */
	VER_1_NewCrashFormat,

	/** Added misc properties (CPU,GPU,OS,etc), memory related stats and platform specific properties as generic payload. */
	VER_2_AddedNewProperties,

	/** Using crash context when available. */
	VER_3_CrashContext = 3,
};

/** Enumerates crash dump modes. */
enum class ECrashDumpMode : int32
{
	/** Default minidump settings. */
	Default = 0,

	/** Full memory crash minidump */
	FullDump = 1,

	/** Full memory crash minidump, even on ensures */
	FullDumpAlways = 2,
};

/** Portable stack frame */
struct FCrashStackFrame
{
	FString ModuleName;
	uint64 BaseAddress;
	uint64 Offset;

	FCrashStackFrame(FString ModuleNameIn, uint64 BaseAddressIn, uint64 OffsetIn)
		: ModuleName(MoveTemp(ModuleNameIn))
		, BaseAddress(BaseAddressIn)
		, Offset(OffsetIn)
	{
	}
};

enum class ECrashContextType
{
	Crash,
	Assert,
	Ensure,
	GPUCrash,
	Hang,

	Max
};

/**
 *	Contains a runtime crash's properties that are common for all platforms.
 *	This may change in the future.
 */
struct CORE_API FGenericCrashContext
{
public:

	/**
	* We can't gather memory stats in crash handling function, so we gather them just before raising
	* exception and use in crash reporting.
	*/
	static FPlatformMemoryStats CrashMemoryStats;
	
	static const ANSICHAR* CrashContextRuntimeXMLNameA;
	static const TCHAR* CrashContextRuntimeXMLNameW;

	static const ANSICHAR* CrashConfigFileNameA;
	static const TCHAR* CrashConfigFileNameW;
	static const FString CrashConfigExtension;
	static const FString ConfigSectionName;
	static const FString CrashConfigPurgeDays;
	static const FString CrashGUIDRootPrefix;

	static const FString CrashContextExtension;
	static const FString RuntimePropertiesTag;
	static const FString PlatformPropertiesTag;
	static const FString EngineDataTag;
	static const FString GameDataTag;
	static const FString EnabledPluginsTag;
	static const FString UE4MinidumpName;
	static const FString NewLineTag;
	static const int32 CrashGUIDLength = 128;

	static const FString CrashTypeCrash;
	static const FString CrashTypeAssert;
	static const FString CrashTypeEnsure;
	static const FString CrashTypeGPU;
	static const FString CrashTypeHang;

	static const FString EngineModeExUnknown;
	static const FString EngineModeExDirty;
	static const FString EngineModeExVanilla;

	// A guid that identifies this particular execution. Allows multiple crash reports from the same run of the project to be tied together
	static const FGuid ExecutionGuid;

	/** Initializes crash context related platform specific data that can be impossible to obtain after a crash. */
	static void Initialize();

	/**
	 * @return true, if the generic crash context has been initialized.
	 */
	static bool IsInitalized()
	{
		return bIsInitialized;
	}

	/** Default constructor. */
	FGenericCrashContext(ECrashContextType InType, const TCHAR* ErrorMessage);

	virtual ~FGenericCrashContext() { }

	/** Serializes all data to the buffer. */
	void SerializeContentToBuffer() const;

	/**
	 * @return the buffer containing serialized data.
	 */
	const FString& GetBuffer() const
	{
		return CommonBuffer;
	}

	/**
	 * @return a globally unique crash name.
	 */
	void GetUniqueCrashName(TCHAR* GUIDBuffer, int32 BufferSize) const;

	/**
	 * @return whether this crash is a full memory minidump
	 */
	const bool IsFullCrashDump() const;

	/** Serializes crash's informations to the specified filename. Should be overridden for platforms where using FFileHelper is not safe, all POSIX platforms. */
	virtual void SerializeAsXML( const TCHAR* Filename ) const;

	/** Writes a common property to the buffer. */
	void AddCrashProperty( const TCHAR* PropertyName, const TCHAR* PropertyValue ) const;

	/** Writes a common property to the buffer. */
	template <typename Type>
	void AddCrashProperty( const TCHAR* PropertyName, const Type& Value ) const
	{
		AddCrashProperty( PropertyName, *TTypeToString<Type>::ToString( Value ) );
	}

	/** Escapes and appends specified text to XML string */
	static void AppendEscapedXMLString( FString& OutBuffer, const TCHAR* Text );

	/** Unescapes a specified XML string, naive implementation. */
	static FString UnescapeXMLString( const FString& Text );

	/** Helper to get the standard string for the crash type based on crash event bool values. */
	static const TCHAR* GetCrashTypeString(ECrashContextType Type);

	/** Get the Game Name of the crash */
	static FString GetCrashGameName();

	/** Gets the "vanilla" status string. */
	static const TCHAR* EngineModeExString();

	/** Helper to get the crash report client config filepath saved by this instance and copied to each crash report folder. */
	static const TCHAR* GetCrashConfigFilePath();

	/** Helper to get the crash report client config folder used by GetCrashConfigFilePath(). */
	static const TCHAR* GetCrashConfigFolder();

	/** Helper to clean out old files in the crash report client config folder. */
	static void PurgeOldCrashConfig();

	/** Clears the engine data dictionary */
	static void ResetEngineData();

	/** Updates (or adds if not already present) arbitrary engine data to the crash context (will remove the key if passed an empty string) */
	static void SetEngineData(const FString& Key, const FString& Value);

	/** Clears the game data dictionary */
	static void ResetGameData();

	/** Updates (or adds if not already present) arbitrary game data to the crash context (will remove the key if passed an empty string) */
	static void SetGameData(const FString& Key, const FString& Value);

	/** Adds a plugin descriptor string to the enabled plugins list in the crash context */
	static void AddPlugin(const FString& PluginDesc);

	/** Sets the number of stack frames to ignore when symbolicating from a minidump */
	void SetNumMinidumpFramesToIgnore(int32 InNumMinidumpFramesToIgnore);

	/** Generate raw call stack for crash report (image base + offset) */
	void CapturePortableCallStack(int32 NumStackFramesToIgnore, void* Context);
	
	/** Sets the portable callstack to a specified stack */
	virtual void SetPortableCallStack(const uint64* StackFrames, int32 NumStackFrames);

	/** Gets the portable callstack to a specified stack and puts it into OutCallStack */
	virtual void GetPortableCallStack(const uint64* StackFrames, int32 NumStackFrames, TArray<FCrashStackFrame>& OutCallStack);

	/**
	 * @return whether this crash is a non-crash event
	 */
	ECrashContextType GetType() const { return Type; }

	/**
	 * Set the current deployment name (ie. EpicApp)
	 */
	static void SetDeploymentName(const FString& EpicApp);

protected:
	/**
	 * @OutStr - a stream of Thread XML elements containing info (e.g. callstack) specific to an active thread
	 * @return - whether the operation was successful
	 */
	virtual bool GetPlatformAllThreadContextsString(FString& OutStr) const { return false; }

	ECrashContextType Type;
	const TCHAR* ErrorMessage;
	int NumMinidumpFramesToIgnore;
	TArray<FCrashStackFrame> CallStack;

private:

	/** Serializes platform specific properties to the buffer. */
	virtual void AddPlatformSpecificProperties() const;

	/** Add callstack information to the crash report xml */
	void AddPortableCallStack() const;

	/** Produces a hash based on the offsets of the portable callstack and adds it to the xml */
	void AddPortableCallStackHash() const;

	/** Writes header information to the buffer. */
	void AddHeader() const;

	/** Writes footer to the buffer. */
	void AddFooter() const;

	void BeginSection( const TCHAR* SectionName ) const;
	void EndSection( const TCHAR* SectionName ) const;

	/** Called once when GConfig is initialized. Opportunity to cache values from config. */
	static void InitializeFromConfig();

	/** Called to update any localized strings in the crash context */
	static void UpdateLocalizedStrings();

	/**	Whether the Initialize() has been called */
	static bool bIsInitialized;

	/**	Static counter records how many crash contexts have been constructed */
	static int32 StaticCrashContextIndex;

	/** The buffer used to store the crash's properties. */
	mutable FString CommonBuffer;

	/**	Records which crash context we were using the StaticCrashContextIndex counter */
	int32 CrashContextIndex;

	// FNoncopyable
	FGenericCrashContext( const FGenericCrashContext& ) = delete;
	FGenericCrashContext& operator=(const FGenericCrashContext&) = delete;
};

struct CORE_API FGenericMemoryWarningContext
{};
