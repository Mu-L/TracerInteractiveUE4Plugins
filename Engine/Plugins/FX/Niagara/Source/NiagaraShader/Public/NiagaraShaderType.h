// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	NiagaraShaderType.h: Niagara shader type definition.
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "Shader.h"
#include "GlobalShader.h"
#include "Engine/EngineTypes.h"

/** A macro to implement Niagara shaders. */
#define IMPLEMENT_NIAGARA_SHADER_TYPE(TemplatePrefix,ShaderClass,SourceFilename,FunctionName,Frequency) \
	IMPLEMENT_SHADER_TYPE( \
		TemplatePrefix, \
		ShaderClass, \
		SourceFilename, \
		FunctionName, \
		Frequency \
		);

class FNiagaraShaderScript;
class FShaderCommonCompileJob;
class FShaderCompileJob;
class FUniformExpressionSet;


/** Called for every Niagara shader to update the appropriate stats. */
extern void UpdateNiagaraShaderCompilingStats(const FNiagaraShaderScript* Script);

/**
 * Dump shader stats for a given platform.
 * 
 * @param	Platform	Platform to dump stats for.
 */
extern ENGINE_API void DumpComputeShaderStats( EShaderPlatform Platform );

struct FNiagaraShaderPermutationParameters : public FShaderPermutationParameters
{
	const FNiagaraShaderScript* Script;

	FNiagaraShaderPermutationParameters(EShaderPlatform InPlatform, const FNiagaraShaderScript* InScript)
		: FShaderPermutationParameters(InPlatform)
		, Script(InScript)
	{}
};

/**
 * A shader meta type for niagara-linked shaders.
 */
class FNiagaraShaderType : public FShaderType
{
public:
	struct CompiledShaderInitializerType : FGlobalShaderType::CompiledShaderInitializerType
	{
		//const FUniformExpressionSet& UniformExpressionSet;
		const FString DebugDescription;
		TArray< FNiagaraDataInterfaceGPUParamInfo > DIParamInfo;

		CompiledShaderInitializerType(
			FShaderType* InType,
			int32 InPermutationId,
			const FShaderCompilerOutput& CompilerOutput,
			const FSHAHash& InNiagaraShaderMapHash,
			const FString& InDebugDescription,
			const TArray< FNiagaraDataInterfaceGPUParamInfo > &InDIParamInfo
			)
		: FGlobalShaderType::CompiledShaderInitializerType(InType,InPermutationId,CompilerOutput, InNiagaraShaderMapHash,nullptr,nullptr)
		, DebugDescription(InDebugDescription)
		, DIParamInfo(InDIParamInfo)
		{}
	};

	FNiagaraShaderType(
		FTypeLayoutDesc& InTypeLayout,
		const TCHAR* InName,
		const TCHAR* InSourceFilename,
		const TCHAR* InFunctionName,
		uint32 InFrequency,					// ugly - ignored for Niagara shaders but needed for IMPLEMENT_SHADER_TYPE macro magic
		int32 InTotalPermutationCount,
		ConstructSerializedType InConstructSerializedRef,
		ConstructCompiledType InConstructCompiledRef,
		ModifyCompilationEnvironmentType InModifyCompilationEnvironmentRef,
		ShouldCompilePermutationType InShouldCompilePermutationRef,
		ValidateCompiledResultType InValidateCompiledResultRef,
		uint32 InTypeSize,
		const FShaderParametersMetadata* InRootParametersMetadata = nullptr
		):
		FShaderType(EShaderTypeForDynamicCast::Niagara, InTypeLayout, InName, InSourceFilename, InFunctionName, SF_Compute, InTotalPermutationCount,
			InConstructSerializedRef,
			InConstructCompiledRef,
			InModifyCompilationEnvironmentRef,
			InShouldCompilePermutationRef,
			InValidateCompiledResultRef,
			InTypeSize,
			InRootParametersMetadata)
	{
		check(InTotalPermutationCount == 1);
	}

	/**
	 * Enqueues a compilation for a new shader of this type.
	 * @param Script - The script to link the shader with.
	 */
	TSharedRef<class FShaderCommonCompileJob, ESPMode::ThreadSafe> BeginCompileShader(
			uint32 ShaderMapId,
			int32 PermutationId,
			const FNiagaraShaderScript* Script,
			FShaderCompilerEnvironment* CompilationEnvironment,
			EShaderPlatform Platform,
			TArray<TSharedRef<class FShaderCommonCompileJob, ESPMode::ThreadSafe>>& NewJobs,
			FShaderTarget Target,
			TArray<FNiagaraDataInterfaceGPUParamInfo>& InDIParamInfo
		);

	/**
	 * Either creates a new instance of this type or returns an equivalent existing shader.
	 * @param script - The script to link the shader with.
	 * @param CurrentJob - Compile job that was enqueued by BeginCompileShader.
	 */
	FShader* FinishCompileShader(
		const FSHAHash& NiagaraShaderMapHash,
		const FShaderCompileJob& CurrentJob,
		const FString& InDebugDescription
		);

	/**
	 * Checks if the shader type should be cached for a particular platform and script.
	 * @param Platform - The platform to check.
	 * @param script - The script to check.
	 * @return True if this shader type should be cached.
	 */
	bool ShouldCache(EShaderPlatform Platform,const FNiagaraShaderScript* Script) const
	{
		return ShouldCompilePermutation(FNiagaraShaderPermutationParameters(Platform, Script));
	}

	/** Adds include statements for uniform buffers that this shader type references, and builds a prefix for the shader file with the include statements. */
	void AddReferencedUniformBufferIncludes(FShaderCompilerEnvironment& OutEnvironment, FString& OutSourceFilePrefix, EShaderPlatform Platform);

	void CacheUniformBufferIncludes(TMap<const TCHAR*, FCachedUniformBufferDeclaration>& Cache, EShaderPlatform Platform);


protected:

	/**
	 * Sets up the environment used to compile an instance of this shader type.
	 * @param Platform - Platform to compile for.
	 * @param Environment - The shader compile environment that the function modifies.
	 */
	void SetupCompileEnvironment(EShaderPlatform Platform, const FNiagaraShaderScript* Script, FShaderCompilerEnvironment& Environment) const
	{
		ModifyCompilationEnvironment(FNiagaraShaderPermutationParameters(Platform, Script), Environment);
	}

private:
	static TMap<const FShaderCompileJob*, TArray<FNiagaraDataInterfaceGPUParamInfo> > ExtraParamInfo;
};
