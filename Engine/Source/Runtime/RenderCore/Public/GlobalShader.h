// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	GlobalShader.h: Shader manager definitions.
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "Shader.h"
#include "Templates/UniquePtr.h"
#include "RHIResources.h"
#include "Containers/Map.h"

class FShaderCommonCompileJob;
class FShaderCompileJob;
class TargetPlatform;

/** Used to identify the global shader map in compile queues. */
extern RENDERCORE_API const int32 GlobalShaderMapId;

/** Class that encapsulates logic to create a DDC key for the global shader map. */
class FGlobalShaderMapId
{
public:

	/** Create a global shader map Id for the given platform. */
	RENDERCORE_API FGlobalShaderMapId(EShaderPlatform Platform);

	/** Append to a string that will be used as a DDC key. */
	RENDERCORE_API void AppendKeyString(FString& KeyString, const TArray<FShaderTypeDependency>& Dependencies, const ITargetPlatform* TargetPlatform) const;

	RENDERCORE_API const TMap<FString, TArray<FShaderTypeDependency>>& GetShaderFilenameToDependeciesMap() const { return ShaderFilenameToDependenciesMap; }

private:
	/** Shader types that this shader map is dependent on and their stored state. Mapped by shader filename, so every filename can have it's own DDC key. */
	TMap<FString, TArray<FShaderTypeDependency>> ShaderFilenameToDependenciesMap;

	/** Shader pipeline types that this shader map is dependent on and their stored state. */
	TArray<FShaderPipelineTypeDependency> ShaderPipelineTypeDependencies;
};

using FGlobalShaderPermutationParameters = FShaderPermutationParameters;

/**
 * A shader meta type for the simplest shaders; shaders which are not material or vertex factory linked.
 * There should only a single instance of each simple shader type.
 */
class FGlobalShaderType : public FShaderType
{
	friend class FGlobalShaderTypeCompiler;
public:

	typedef FShader::CompiledShaderInitializerType CompiledShaderInitializerType;

	FGlobalShaderType(
		FTypeLayoutDesc& InTypeLayout,
		const TCHAR* InName,
		const TCHAR* InSourceFilename,
		const TCHAR* InFunctionName,
		uint32 InFrequency,
		int32 InTotalPermutationCount,
		ConstructSerializedType InConstructSerializedRef,
		ConstructCompiledType InConstructCompiledRef,
		ModifyCompilationEnvironmentType InModifyCompilationEnvironmentRef,
		ShouldCompilePermutationType InShouldCompilePermutationRef,
		ValidateCompiledResultType InValidateCompiledResultRef,
		uint32 InTypeSize,
		const FShaderParametersMetadata* InRootParametersMetadata = nullptr
		):
		FShaderType(EShaderTypeForDynamicCast::Global, InTypeLayout, InName, InSourceFilename, InFunctionName, InFrequency, InTotalPermutationCount,
			InConstructSerializedRef,
			InConstructCompiledRef,
			InModifyCompilationEnvironmentRef,
			InShouldCompilePermutationRef,
			InValidateCompiledResultRef,
			InTypeSize,
			InRootParametersMetadata)
	{
		checkf(FPaths::GetExtension(InSourceFilename) == TEXT("usf"),
			TEXT("Incorrect virtual shader path extension for global shader '%s': Only .usf files should be "
			     "compiled."),
			InSourceFilename);
	}

	/**
	 * Checks if the shader type should be cached for a particular platform.
	 * @param Platform - The platform to check.
	 * @return True if this shader type should be cached.
	 */
	bool ShouldCompilePermutation(EShaderPlatform Platform, int32 PermutationId) const
	{
		return FShaderType::ShouldCompilePermutation(FGlobalShaderPermutationParameters(Platform, PermutationId));
	}

	/**
	 * Sets up the environment used to compile an instance of this shader type.
	 * @param Platform - Platform to compile for.
	 * @param Environment - The shader compile environment that the function modifies.
	 */
	void SetupCompileEnvironment(EShaderPlatform Platform, int32 PermutationId, FShaderCompilerEnvironment& Environment)
	{
		// Allow the shader type to modify its compile environment.
		ModifyCompilationEnvironment(FGlobalShaderPermutationParameters(Platform, PermutationId), Environment);
	}
};

class RENDERCORE_API FGlobalShaderMapContent : public FShaderMapContent
{
	using Super = FShaderMapContent;
	friend class FGlobalShaderMap;
	friend class FGlobalShaderMapSection;
	DECLARE_TYPE_LAYOUT(FGlobalShaderMapContent, NonVirtual);
public:
	const FHashedName& GetHashedSourceFilename() const { return HashedSourceFilename; }

private:
	inline FGlobalShaderMapContent(EShaderPlatform InPlatform, const FHashedName& InHashedSourceFilename)
		: Super(InPlatform)
		, HashedSourceFilename(InHashedSourceFilename)
	{}

	LAYOUT_FIELD(FHashedName, HashedSourceFilename);
};

class RENDERCORE_API FGlobalShaderMapSection : public TShaderMap<FGlobalShaderMapContent, FShaderMapPointerTable>
{
	using Super = TShaderMap<FGlobalShaderMapContent, FShaderMapPointerTable>;
	friend class FGlobalShaderMap;
public:
	static FGlobalShaderMapSection* CreateFromArchive(FArchive& Ar);

	bool Serialize(FArchive& Ar);
private:
	inline FGlobalShaderMapSection() {}

	inline FGlobalShaderMapSection(EShaderPlatform InPlatform, const FHashedName& InHashedSourceFilename)
	{
		AssignContent(new FGlobalShaderMapContent(InPlatform, InHashedSourceFilename));
	}

	TShaderRef<FShader> GetShader(FShaderType* ShaderType, int32 PermutationId = 0) const;
	FShaderPipelineRef GetShaderPipeline(const FShaderPipelineType* PipelineType) const;
};

class RENDERCORE_API FGlobalShaderMap
{
public:
	explicit FGlobalShaderMap(EShaderPlatform InPlatform);
	~FGlobalShaderMap();

	TShaderRef<FShader> GetShader(FShaderType* ShaderType, int32 PermutationId = 0) const;
	FShaderPipelineRef GetShaderPipeline(const FShaderPipelineType* PipelineType) const;

	template<typename ShaderType>
	TShaderRef<ShaderType> GetShader(int32 PermutationId = 0) const
	{
		TShaderRef<FShader> Shader = GetShader(&ShaderType::StaticType, PermutationId);
		checkf(Shader.IsValid(), TEXT("Failed to find shader type %s in Platform %s"), ShaderType::StaticType.GetName(), *LegacyShaderPlatformToShaderFormat(Platform).ToString());
		return TShaderRef<ShaderType>::Cast(Shader);
	}

	/** Finds the shader with the given type.  Asserts on failure. */
	template<typename ShaderType>
	TShaderRef<ShaderType> GetShader(const typename ShaderType::FPermutationDomain& PermutationVector) const
	{
		return GetShader<ShaderType>(PermutationVector.ToDimensionValueId());
	}

	bool HasShader(FShaderType* Type, int32 PermutationId) const
	{
		return GetShader(Type, PermutationId).IsValid();
	}

	bool HasShaderPipeline(const FShaderPipelineType* ShaderPipelineType) const
	{
		return GetShaderPipeline(ShaderPipelineType).IsValid();
	}

	bool IsEmpty() const;

	void Empty();

	FShader* FindOrAddShader(const FShaderType* ShaderType, int32 PermutationId, FShader* Shader);
	FShaderPipeline* FindOrAddShaderPipeline(const FShaderPipelineType* ShaderPipelineType, FShaderPipeline* ShaderPipeline);

	void RemoveShaderTypePermutaion(const FShaderType* Type, int32 PermutationId);
	void RemoveShaderPipelineType(const FShaderPipelineType* ShaderPipelineType);

	void AddSection(FGlobalShaderMapSection* InSection);
	FGlobalShaderMapSection* FindSection(const FHashedName& HashedShaderFilename);
	FGlobalShaderMapSection* FindOrAddSection(const FShaderType* ShaderType);
	
	void LoadFromGlobalArchive(FArchive& Ar);
	void SaveToGlobalArchive(FArchive& Ar);

	void BeginCreateAllShaders();

#if WITH_EDITOR
	void GetOutdatedTypes(TArray<const FShaderType*>& OutdatedShaderTypes, TArray<const FShaderPipelineType*>& OutdatedShaderPipelineTypes, TArray<const FVertexFactoryType*>& OutdatedFactoryTypes) const;
	void SaveShaderStableKeys(EShaderPlatform TargetShaderPlatform);
#endif // WITH_EDITOR

private:
	TMap<FHashedName, FGlobalShaderMapSection*> SectionMap;
	EShaderPlatform Platform;
};

extern RENDERCORE_API FGlobalShaderMap* GGlobalShaderMap[SP_NumPlatforms];


/**
 * FGlobalShader
 * 
 * Global shaders derive from this class to set their default recompile group as a global one
 */
class FGlobalShader : public FShader
{
	DECLARE_EXPORTED_TYPE_LAYOUT(FGlobalShader, RENDERCORE_API, NonVirtual);
public:
	using ShaderMetaType = FGlobalShaderType;

	FGlobalShader() : FShader() {}

	RENDERCORE_API FGlobalShader(const ShaderMetaType::CompiledShaderInitializerType& Initializer);
	
	template<typename TViewUniformShaderParameters, typename ShaderRHIParamRef, typename TRHICmdList>
	inline void SetParameters(TRHICmdList& RHICmdList, const ShaderRHIParamRef ShaderRHI, FRHIUniformBuffer* ViewUniformBuffer)
	{
		const auto& ViewUniformBufferParameter = static_cast<const FShaderUniformBufferParameter&>(GetUniformBufferParameter<TViewUniformShaderParameters>());
		SetUniformBufferParameter(RHICmdList, ShaderRHI, ViewUniformBufferParameter, ViewUniformBuffer);
	}

	
	
};

/**
 * An internal dummy pixel shader to use when the user calls RHISetPixelShader(NULL).
 */
class FNULLPS : public FGlobalShader
{
	DECLARE_EXPORTED_SHADER_TYPE(FNULLPS,Global, RENDERCORE_API);
public:

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return true;
	}

	FNULLPS( )	{ }
	FNULLPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer):
		FGlobalShader(Initializer)
	{
	}
};

/**
* Container for Backup/RestoreGlobalShaderMap functions.
* Includes shader data from any populated feature levels.
*/
struct FGlobalShaderBackupData
{
	TUniquePtr<TArray<uint8>> FeatureLevelShaderData[ERHIFeatureLevel::Num];
};

/** Backs up all global shaders to memory through serialization, and removes all references to FShaders from the global shader map. */
extern RENDERCORE_API void BackupGlobalShaderMap(FGlobalShaderBackupData& OutGlobalShaderBackup);

/** Recreates shaders in the global shader map from the serialized memory. */
extern RENDERCORE_API void RestoreGlobalShaderMap(const FGlobalShaderBackupData& GlobalShaderData);

/**
 * Accesses the global shader map.  This is a global FGlobalShaderMap which contains an instance of each global shader type.
 *
 * @param Platform Which platform's global shader map to use
 * @param bRefreshShaderMap If true, the existing global shader map will be tossed first
 * @return A reference to the global shader map.
 */
extern RENDERCORE_API FGlobalShaderMap* GetGlobalShaderMap(EShaderPlatform Platform);

/**
  * Overload for the above GetGlobalShaderMap which takes a feature level and translates to the appropriate shader platform
  *
  * @param FeatureLevel - Which feature levels shader map to use
  * @param bRefreshShaderMap If true, the existing global shader map will be tossed first
  * @return A reference to the global shader map.
  *
  **/
inline FGlobalShaderMap* GetGlobalShaderMap(ERHIFeatureLevel::Type FeatureLevel)
{ 
	return GetGlobalShaderMap(GShaderPlatformForFeatureLevel[FeatureLevel]); 
}


/** DECLARE_GLOBAL_SHADER and IMPLEMENT_GLOBAL_SHADER setup a global shader class's boiler plate. They are meant to be used like so:
 *
 * class FMyGlobalShaderPS : public FGlobalShader
 * {
 *		// Setup the shader's boiler plate.
 *		DECLARE_GLOBAL_SHADER(FMyGlobalShaderPS);
 *
 *		// Setup the shader's permutation domain. If no dimensions, can do FPermutationDomain = FShaderPermutationNone.
 *		using FPermutationDomain = TShaderPermutationDomain<DIMENSIONS...>;
 *
 *		// ...
 * };
 *
 * // Instantiates global shader's global variable that will take care of compilation process of the shader. This needs imperatively to be
 * done in a .cpp file regardless of whether FMyGlobalShaderPS is in a header or not.
 * IMPLEMENT_GLOBAL_SHADER(FMyGlobalShaderPS, "/Engine/Private/MyShaderFile.usf", "MainPS", SF_Pixel);
 *
 * When the shader class is a public header, let say in RenderCore module public header, the shader class then should have the RENDERCORE_API
 * like this:
 *
 * class RENDERCORE_API FMyGlobalShaderPS : public FGlobalShader
 * {
 *		// Setup the shader's boiler plate.
 *		DECLARE_GLOBAL_SHADER(FMyGlobalShaderPS);
 *
 *		// ...
 * };
 */
/*#define DECLARE_GLOBAL_SHADER(ShaderClass) \
	public: \
	\
	using ShaderMetaType = FGlobalShaderType; \
	\
	using ShaderMapType = FGlobalShaderMap; \
	\
	static ShaderMetaType StaticType; \
	\
	static FShader* ConstructSerializedInstance() { return new ShaderClass(); } \
	static FShader* ConstructCompiledInstance(const ShaderMetaType::CompiledShaderInitializerType& Initializer) \
	{ return new ShaderClass(Initializer); } \
	\
	static void ModifyCompilationEnvironmentImpl( \
		const FShaderPermutationParameters& Parameters, \
		FShaderCompilerEnvironment& OutEnvironment) \
	{ \
		FPermutationDomain PermutationVector(Parameters.PermutationId); \
		PermutationVector.ModifyCompilationEnvironment(OutEnvironment); \
		ShaderClass::ModifyCompilationEnvironment(Parameters, OutEnvironment); \
	}

#define IMPLEMENT_GLOBAL_SHADER(ShaderClass,SourceFilename,FunctionName,Frequency) \
	ShaderClass::ShaderMetaType ShaderClass::StaticType( \
		TEXT(#ShaderClass), \
		TEXT(SourceFilename), \
		TEXT(FunctionName), \
		Frequency, \
		ShaderClass::FPermutationDomain::PermutationCount, \
		ShaderClass::ConstructSerializedInstance, \
		ShaderClass::ConstructCompiledInstance, \
		ShaderClass::ConstructEditorContent, \
		ShaderClass::DestroyInstance, \
		ShaderClass::DestroyEditorContent, \
		ShaderClass::ModifyCompilationEnvironmentImpl, \
		ShaderClass::ShouldCompilePermutation, \
		ShaderClass::ValidateCompiledResult, \
		ShaderClass::FreezeMemoryImageImpl, \
		ShaderClass::FreezeMemoryImageEditorImpl, \
		sizeof(ShaderClass), \
		sizeof(ShaderClass::FEditorContent), \
		ShaderClass::GetRootParametersMetadata() \
		)
		*/

#define DECLARE_GLOBAL_SHADER(ShaderClass) DECLARE_SHADER_TYPE(ShaderClass, Global)
#define IMPLEMENT_GLOBAL_SHADER(ShaderClass,SourceFilename,FunctionName,Frequency) IMPLEMENT_SHADER_TYPE(,ShaderClass,TEXT(SourceFilename),TEXT(FunctionName),Frequency)