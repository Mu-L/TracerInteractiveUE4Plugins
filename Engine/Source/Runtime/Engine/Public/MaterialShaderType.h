// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	MaterialShader.h: Material shader definitions.
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "Shader.h"
#include "GlobalShader.h"
#include "Engine/EngineTypes.h"

/** A macro to implement material shaders. */
#define IMPLEMENT_MATERIAL_SHADER_TYPE(TemplatePrefix,ShaderClass,SourceFilename,FunctionName,Frequency) \
	IMPLEMENT_SHADER_TYPE( \
		TemplatePrefix, \
		ShaderClass, \
		SourceFilename, \
		FunctionName, \
		Frequency \
		);

class FMaterial;
class FShaderCommonCompileJob;
class FShaderCompileJob;
class FUniformExpressionSet;
class FVertexFactoryType;

DECLARE_DELEGATE_RetVal_OneParam(FString, FShadingModelToStringDelegate, EMaterialShadingModel)

/** Converts an EMaterialShadingModel to a string description. */
extern ENGINE_API FString GetShadingModelString(EMaterialShadingModel ShadingModel);

/** Converts an FMaterialShadingModelField to a string description, base on the passed in delegate. */
extern ENGINE_API FString GetShadingModelFieldString(FMaterialShadingModelField ShadingModels, const FShadingModelToStringDelegate& Delegate, const FString& Delimiter = " ");

/** Converts an FMaterialShadingModelField to a string description, base on a default function. */
extern ENGINE_API FString GetShadingModelFieldString(FMaterialShadingModelField ShadingModels);

/** Converts an EBlendMode to a string description. */
extern ENGINE_API FString GetBlendModeString(EBlendMode BlendMode);

/** Called for every material shader to update the appropriate stats. */
extern void UpdateMaterialShaderCompilingStats(const FMaterial* Material);

/**
 * Dump material stats for a given platform.
 * 
 * @param	Platform	Platform to dump stats for.
 */
extern ENGINE_API void DumpMaterialStats( EShaderPlatform Platform );

struct FMaterialShaderPermutationParameters
{
	// Shader platform to compile to.
	const EShaderPlatform Platform;

	// Material to compile.
	const FMaterial* Material;

	/** Unique permutation identifier of the material shader type. */
	const int32 PermutationId;

	FMaterialShaderPermutationParameters(EShaderPlatform InPlatform, const FMaterial* InMaterial, int32 InPermutationId)
		: Platform(InPlatform)
		, Material(InMaterial)
		, PermutationId(InPermutationId)
	{
	}
};

/**
 * A shader meta type for material-linked shaders.
 */
class FMaterialShaderType : public FShaderType
{
public:
	struct CompiledShaderInitializerType : FGlobalShaderType::CompiledShaderInitializerType
	{
		const FUniformExpressionSet& UniformExpressionSet;
		const FString DebugDescription;

		CompiledShaderInitializerType(
			FShaderType* InType,
			int32 InPermutationId,
			const FShaderCompilerOutput& CompilerOutput,
			FShaderResource* InResource,
			const FUniformExpressionSet& InUniformExpressionSet,
			const FSHAHash& InMaterialShaderMapHash,
			const FShaderPipelineType* InShaderPipeline,
			FVertexFactoryType* InVertexFactoryType,
			const FString& InDebugDescription
			)
		: FGlobalShaderType::CompiledShaderInitializerType(InType,InPermutationId,CompilerOutput,InResource,InMaterialShaderMapHash,InShaderPipeline,InVertexFactoryType)
		, UniformExpressionSet(InUniformExpressionSet)
		, DebugDescription(InDebugDescription)
		{}
	};

	typedef FShader* (*ConstructCompiledType)(const CompiledShaderInitializerType&);
	typedef bool (*ShouldCompilePermutationType)(const FMaterialShaderPermutationParameters&);
	typedef bool(*ValidateCompiledResultType)(EShaderPlatform, const TArray<FMaterial*>&, const FShaderParameterMap&, TArray<FString>&);
	typedef void (*ModifyCompilationEnvironmentType)(const FMaterialShaderPermutationParameters&, FShaderCompilerEnvironment&);

	FMaterialShaderType(
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
		GetStreamOutElementsType InGetStreamOutElementsRef
		):
		FShaderType(EShaderTypeForDynamicCast::Material, InName, InSourceFilename, InFunctionName, InFrequency, InTotalPermutationCount, InConstructSerializedRef, InGetStreamOutElementsRef, nullptr),
		ConstructCompiledRef(InConstructCompiledRef),
		ShouldCompilePermutationRef(InShouldCompilePermutationRef),
		ValidateCompiledResultRef(InValidateCompiledResultRef),
		ModifyCompilationEnvironmentRef(InModifyCompilationEnvironmentRef)
	{
		checkf(FPaths::GetExtension(InSourceFilename) == TEXT("usf"),
			TEXT("Incorrect virtual shader path extension for material shader '%s': Only .usf files should be compiled."),
			InSourceFilename);
	}

	/**
	 * Enqueues a compilation for a new shader of this type.
	 * @param Material - The material to link the shader with.
	 */
	class FShaderCompileJob* BeginCompileShader(
		uint32 ShaderMapId,
		int32 PermutationId,
		const FMaterial* Material,
		FShaderCompilerEnvironment* MaterialEnvironment,
		const FShaderPipelineType* ShaderPipeline,
		EShaderPlatform Platform,
		TArray<FShaderCommonCompileJob*>& NewJobs,
		const FString& DebugDescription,
		const FString& DebugExtension
		);

	static void BeginCompileShaderPipeline(
		uint32 ShaderMapId,
		EShaderPlatform Platform,
		const FMaterial* Material,
		FShaderCompilerEnvironment* MaterialEnvironment,
		const FShaderPipelineType* ShaderPipeline,
		const TArray<FMaterialShaderType*>& ShaderStages,
		TArray<FShaderCommonCompileJob*>& NewJobs,
		const FString& DebugDescription,
		const FString& DebugExtension
	);

	/**
	 * Either creates a new instance of this type or returns an equivalent existing shader.
	 * @param Material - The material to link the shader with.
	 * @param CurrentJob - Compile job that was enqueued by BeginCompileShader.
	 */
	FShader* FinishCompileShader(
		const FUniformExpressionSet& UniformExpressionSet,
		const FSHAHash& MaterialShaderMapHash,
		const FShaderCompileJob& CurrentJob,
		const FShaderPipelineType* ShaderPipeline,
		const FString& InDebugDescription
		);

	/**
	 * Checks if the shader type should be cached for a particular platform and material.
	 * @param Platform - The platform to check.
	 * @param Material - The material to check.
	 * @return True if this shader type should be cached.
	 */
	bool ShouldCompilePermutation(EShaderPlatform Platform, const FMaterial* Material, int32 PermutationId) const
	{
		return (*ShouldCompilePermutationRef)(FMaterialShaderPermutationParameters(Platform, Material, PermutationId));
	}

	/**
	* Checks if the shader type should pass compilation for a particular set of parameters.
	* @param Platform - Platform to validate.
	* @param Materials - Materials to validate.
	* @param ParameterMap - Shader parameters to validate.
	* @param OutError - List for appending validation errors.
	*/
	bool ValidateCompiledResult(EShaderPlatform Platform, const TArray<FMaterial*>& Materials, const FShaderParameterMap& ParameterMap, TArray<FString>& OutError) const
	{
		return (*ValidateCompiledResultRef)(Platform, Materials, ParameterMap, OutError);
	}

protected:

	/**
	 * Sets up the environment used to compile an instance of this shader type.
	 * @param Platform - Platform to compile for.
	 * @param Environment - The shader compile environment that the function modifies.
	 */
	void SetupCompileEnvironment(EShaderPlatform Platform, const FMaterial* Material, int32 PermutationId, FShaderCompilerEnvironment& Environment)
	{
		// Allow the shader type to modify its compile environment.
		(*ModifyCompilationEnvironmentRef)(FMaterialShaderPermutationParameters(Platform, Material, PermutationId), Environment);
	}

private:
	ConstructCompiledType ConstructCompiledRef;
	ShouldCompilePermutationType ShouldCompilePermutationRef;
	ValidateCompiledResultType ValidateCompiledResultRef;
	ModifyCompilationEnvironmentType ModifyCompilationEnvironmentRef;
};

/** DECLARE_MATERIAL_SHADER and IMPLEMENT_MATERIAL_SHADER setup a material shader class's boiler plate. They are meant to be used like so:
 *
 * class FMyMaterialShaderPS : public FMaterialShader
 * {
 *		// Setup the shader's boiler plate.
 *		DECLARE_MATERIAL_SHADER(FMyMaterialShaderPS);
 *
 *		// Setup the shader's permutation domain. If no dimensions, can do FPermutationDomain = FShaderPermutationNone.
 *		using FPermutationDomain = TShaderPermutationDomain<DIMENSIONS...>;
 *
 *		// ...
 * };
 *
 * // Instantiates shader's global variable that will take care of compilation process of the shader. This needs imperatively to be
 * done in a .cpp file regardless of whether FMyMaterialShaderPS is in a header or not.
 * IMPLEMENT_MATERIAL_SHADER(FMyMaterialShaderPS, "/Engine/Private/MyShaderFile.usf", "MainPS", SF_Pixel);
 */
#define DECLARE_MATERIAL_SHADER(ShaderClass) \
	public: \
	using ShaderMetaType = FMaterialShaderType; \
	\
	static ShaderMetaType StaticType; \
	\
	static FShader* ConstructSerializedInstance() { return new ShaderClass(); } \
	static FShader* ConstructCompiledInstance(const ShaderMetaType::CompiledShaderInitializerType& Initializer) \
	{ return new ShaderClass(Initializer); } \
	\
	virtual uint32 GetTypeSize() const override { return sizeof(*this); } \
	\
	static void ModifyCompilationEnvironmentImpl( \
		const FMaterialShaderPermutationParameters& Parameters, \
		FShaderCompilerEnvironment& OutEnvironment) \
	{ \
		FPermutationDomain PermutationVector(Parameters.PermutationId); \
		PermutationVector.ModifyCompilationEnvironment(OutEnvironment); \
		ShaderClass::ModifyCompilationEnvironment(Parameters, OutEnvironment); \
	}

#define IMPLEMENT_MATERIAL_SHADER(ShaderClass,SourceFilename,FunctionName,Frequency) \
	ShaderClass::ShaderMetaType ShaderClass::StaticType( \
		TEXT(#ShaderClass), \
		TEXT(SourceFilename), \
		TEXT(FunctionName), \
		Frequency, \
		ShaderClass::FPermutationDomain::PermutationCount, \
		ShaderClass::ConstructSerializedInstance, \
		ShaderClass::ConstructCompiledInstance, \
		ShaderClass::ModifyCompilationEnvironmentImpl, \
		ShaderClass::ShouldCompilePermutation, \
		ShaderClass::ValidateCompiledResult, \
		ShaderClass::GetStreamOutElements \
		)
