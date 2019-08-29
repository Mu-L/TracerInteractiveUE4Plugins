// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.


#pragma once

#include "CoreMinimal.h"
#include "ShaderParameters.h"
#include "Shader.h"
#include "GlobalShader.h"

struct FDummyResolveParameter {};

class FResolveDepthPS : public FGlobalShader
{
	DECLARE_EXPORTED_SHADER_TYPE(FResolveDepthPS, Global, UTILITYSHADERS_API);
public:
	
	typedef FDummyResolveParameter FParameter;
	
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) 
	{ 
		return GetMaxSupportedFeatureLevel(Parameters.Platform) >= ERHIFeatureLevel::SM5
			|| IsSimulatedPlatform(Parameters.Platform); // support resolving MSAA depth in mobile emulation
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("SIMULATED_PLATFORM"), IsSimulatedPlatform(Parameters.Platform) ? 1 : 0);
	}
		
	FResolveDepthPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer):
	FGlobalShader(Initializer)
	{
		UnresolvedSurface.Bind(Initializer.ParameterMap,TEXT("UnresolvedSurface"), SPF_Mandatory);
	}
	FResolveDepthPS() {}
	
	void SetParameters(FRHICommandList& RHICmdList, FParameter)
	{
	}

	virtual bool Serialize(FArchive& Ar) override
	{
		bool bShaderHasOutdatedParameters = FGlobalShader::Serialize(Ar);
		Ar << UnresolvedSurface;
		return bShaderHasOutdatedParameters;
	}
	
	FShaderResourceParameter UnresolvedSurface;
};

class FResolveDepth2XPS : public FGlobalShader
{
	DECLARE_EXPORTED_SHADER_TYPE(FResolveDepth2XPS, Global, UTILITYSHADERS_API);
public:

	typedef FDummyResolveParameter FParameter;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) 
	{ 
		return GetMaxSupportedFeatureLevel(Parameters.Platform) >= ERHIFeatureLevel::SM5 
			|| IsSimulatedPlatform(Parameters.Platform); // support resolving MSAA depth in mobile emulation
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("DEPTH_RESOLVE_NUM_SAMPLES"), 2);
		OutEnvironment.SetDefine(TEXT("SIMULATED_PLATFORM"), IsSimulatedPlatform(Parameters.Platform) ? 1 : 0);
	}

	FResolveDepth2XPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer) :
		FGlobalShader(Initializer)
	{
		UnresolvedSurface.Bind(Initializer.ParameterMap, TEXT("UnresolvedSurface"), SPF_Mandatory);
	}
	FResolveDepth2XPS() {}

	void SetParameters(FRHICommandList& RHICmdList, FParameter)
	{
	}

	virtual bool Serialize(FArchive& Ar) override
	{
		bool bShaderHasOutdatedParameters = FGlobalShader::Serialize(Ar);
		Ar << UnresolvedSurface;
		return bShaderHasOutdatedParameters;
	}

	FShaderResourceParameter UnresolvedSurface;
};


class FResolveDepth4XPS : public FGlobalShader
{
	DECLARE_EXPORTED_SHADER_TYPE(FResolveDepth4XPS, Global, UTILITYSHADERS_API);
public:

	typedef FDummyResolveParameter FParameter;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) 
	{ 
		return GetMaxSupportedFeatureLevel(Parameters.Platform) >= ERHIFeatureLevel::SM5
			|| IsSimulatedPlatform(Parameters.Platform); // support resolving MSAA depth in mobile emulation
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("DEPTH_RESOLVE_NUM_SAMPLES"), 4);
		OutEnvironment.SetDefine(TEXT("SIMULATED_PLATFORM"), IsSimulatedPlatform(Parameters.Platform) ? 1 : 0);
	}

	FResolveDepth4XPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer) :
		FGlobalShader(Initializer)
	{
		UnresolvedSurface.Bind(Initializer.ParameterMap, TEXT("UnresolvedSurface"), SPF_Mandatory);
	}
	FResolveDepth4XPS() {}

	void SetParameters(FRHICommandList& RHICmdList, FParameter)
	{
	}

	virtual bool Serialize(FArchive& Ar) override
	{
		bool bShaderHasOutdatedParameters = FGlobalShader::Serialize(Ar);
		Ar << UnresolvedSurface;
		return bShaderHasOutdatedParameters;
	}

	FShaderResourceParameter UnresolvedSurface;
};


class FResolveDepth8XPS : public FGlobalShader
{
	DECLARE_EXPORTED_SHADER_TYPE(FResolveDepth8XPS, Global, UTILITYSHADERS_API);
public:

	typedef FDummyResolveParameter FParameter;

	static bool ShouldCache(EShaderPlatform Platform) { return GetMaxSupportedFeatureLevel(Platform) >= ERHIFeatureLevel::SM5; }

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return GetMaxSupportedFeatureLevel(Parameters.Platform) >= ERHIFeatureLevel::SM5
			|| IsSimulatedPlatform(Parameters.Platform); // support resolving MSAA depth in mobile emulation
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("DEPTH_RESOLVE_NUM_SAMPLES"), 8);
		OutEnvironment.SetDefine(TEXT("SIMULATED_PLATFORM"), IsSimulatedPlatform(Parameters.Platform) ? 1 : 0);
	}

	FResolveDepth8XPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer) :
		FGlobalShader(Initializer)
	{
		UnresolvedSurface.Bind(Initializer.ParameterMap, TEXT("UnresolvedSurface"), SPF_Mandatory);
	}
	FResolveDepth8XPS() {}

	void SetParameters(FRHICommandList& RHICmdList, FParameter)
	{
	}

	virtual bool Serialize(FArchive& Ar) override
	{
		bool bShaderHasOutdatedParameters = FGlobalShader::Serialize(Ar);
		Ar << UnresolvedSurface;
		return bShaderHasOutdatedParameters;
	}

	FShaderResourceParameter UnresolvedSurface;
};


class FResolveDepthNonMSPS : public FGlobalShader
{
	DECLARE_EXPORTED_SHADER_TYPE(FResolveDepthNonMSPS, Global, UTILITYSHADERS_API);
public:
	
	typedef FDummyResolveParameter FParameter;
	
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return GetMaxSupportedFeatureLevel(Parameters.Platform) <= ERHIFeatureLevel::SM4; }
	
	FResolveDepthNonMSPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer):
	FGlobalShader(Initializer)
	{
		UnresolvedSurface.Bind(Initializer.ParameterMap,TEXT("UnresolvedSurfaceNonMS"), SPF_Mandatory);
	}
	FResolveDepthNonMSPS() {}
	
	void SetParameters(FRHICommandList& RHICmdList, FParameter)
	{
	}
	
	virtual bool Serialize(FArchive& Ar) override
	{
		bool bShaderHasOutdatedParameters = FGlobalShader::Serialize(Ar);
		Ar << UnresolvedSurface;
		return bShaderHasOutdatedParameters;
	}
	
	FShaderResourceParameter UnresolvedSurface;
};

class FResolveSingleSamplePS : public FGlobalShader
{
	DECLARE_EXPORTED_SHADER_TYPE(FResolveSingleSamplePS, Global, UTILITYSHADERS_API);
public:
	
	typedef uint32 FParameter;
	
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return Parameters.Platform == SP_PCD3D_SM5; }
	
	FResolveSingleSamplePS(const ShaderMetaType::CompiledShaderInitializerType& Initializer):
	FGlobalShader(Initializer)
	{
		UnresolvedSurface.Bind(Initializer.ParameterMap,TEXT("UnresolvedSurface"), SPF_Mandatory);
		SingleSampleIndex.Bind(Initializer.ParameterMap,TEXT("SingleSampleIndex"), SPF_Mandatory);
	}
	FResolveSingleSamplePS() {}
	
	UTILITYSHADERS_API void SetParameters(FRHICommandList& RHICmdList, uint32 SingleSampleIndexValue);
	
	virtual bool Serialize(FArchive& Ar) override
	{
		bool bShaderHasOutdatedParameters = FGlobalShader::Serialize(Ar);
		Ar << UnresolvedSurface;
		Ar << SingleSampleIndex;
		return bShaderHasOutdatedParameters;
	}
	
	FShaderResourceParameter UnresolvedSurface;
	FShaderParameter SingleSampleIndex;
};

/**
 * A vertex shader for rendering a textured screen element.
 */
class FResolveVS : public FGlobalShader
{
	DECLARE_EXPORTED_SHADER_TYPE(FResolveVS, Global, UTILITYSHADERS_API);
public:
	
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return true; }
	
	FResolveVS(const ShaderMetaType::CompiledShaderInitializerType& Initializer):
	FGlobalShader(Initializer)
	{}
	FResolveVS() {}
};
