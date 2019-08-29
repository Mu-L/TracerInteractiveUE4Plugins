// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.


#pragma once

#include "CoreMinimal.h"
#include "ShaderParameters.h"
#include "Shader.h"
#include "GlobalShader.h"
#include "ShaderParameterUtils.h"

/**
 * Vertex shader for rendering a single, constant color.
 */
template<bool bUsingNDCPositions=true, bool bUsingVertexLayers=false>
class TOneColorVS : public FGlobalShader
{
	DECLARE_EXPORTED_SHADER_TYPE(TOneColorVS, Global, UTILITYSHADERS_API);

	/** Default constructor. */
	TOneColorVS() {}

public:

	TOneColorVS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		DepthParameter.Bind(Initializer.ParameterMap, TEXT("InputDepth"), SPF_Mandatory);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("USING_NDC_POSITIONS"), (uint32)(bUsingNDCPositions ? 1 : 0));
		OutEnvironment.SetDefine(TEXT("USING_LAYERS"), (uint32)(bUsingVertexLayers ? 1 : 0));
	}

	void SetDepthParameter(FRHICommandList& RHICmdList, float Depth)
	{
		SetShaderValue(RHICmdList, GetVertexShader(), DepthParameter, Depth);
	}

	// FShader interface.
	virtual bool Serialize(FArchive& Ar) override
	{
		bool bShaderHasOutdatedParameters = FGlobalShader::Serialize(Ar);
		Ar << DepthParameter;
		return bShaderHasOutdatedParameters;
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return true;
	}
	
	static const TCHAR* GetSourceFilename()
	{
		return TEXT("/Engine/Private/OneColorShader.usf");
	}
	
	static const TCHAR* GetFunctionName()
	{
		return TEXT("MainVertexShader");
	}

private:
	FShaderParameter DepthParameter;
};

/**
 * Pixel shader for rendering a single, constant color.
 */
class FOneColorPS : public FGlobalShader
{
	DECLARE_EXPORTED_SHADER_TYPE(FOneColorPS,Global,UTILITYSHADERS_API);
public:
	
	FOneColorPS( )	{ }
	FOneColorPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
	:	FGlobalShader( Initializer )
	{
		//ColorParameter.Bind( Initializer.ParameterMap, TEXT("DrawColorMRT"), SPF_Mandatory);
	}

	virtual void SetColors(FRHICommandList& RHICmdList, const FLinearColor* Colors, int32 NumColors);	

	// FShader interface.
	virtual bool Serialize(FArchive& Ar) override
	{
		bool bShaderHasOutdatedParameters = FGlobalShader::Serialize(Ar);
		//Ar << ColorParameter;
		return bShaderHasOutdatedParameters;
	}
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return true;
	}

	/** The parameter to use for setting the draw Color. */
	//FShaderParameter ColorParameter;
};

/**
 * Pixel shader for rendering a single, constant color to MRTs.
 */
template<int32 NumOutputs>
class TOneColorPixelShaderMRT : public FOneColorPS
{
	DECLARE_EXPORTED_SHADER_TYPE(TOneColorPixelShaderMRT,Global,UTILITYSHADERS_API);
public:
	TOneColorPixelShaderMRT( )	{ }
	TOneColorPixelShaderMRT(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
	:	FOneColorPS( Initializer )
	{
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		if( NumOutputs > 1 )
		{
			return IsFeatureLevelSupported( Parameters.Platform, ERHIFeatureLevel::ES3_1);
		}
		return true;
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FOneColorPS::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("NUM_OUTPUTS"), NumOutputs);
	}
};

/**
 * Compute shader for writing values
 */
class FFillTextureCS : public FGlobalShader
{
	DECLARE_EXPORTED_SHADER_TYPE(FFillTextureCS,Global,UTILITYSHADERS_API);
public:
	FFillTextureCS( )	{ }
	FFillTextureCS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
	:	FGlobalShader( Initializer )
	{
		FillValue.Bind( Initializer.ParameterMap, TEXT("FillValue"), SPF_Mandatory);
		Params0.Bind( Initializer.ParameterMap, TEXT("Params0"), SPF_Mandatory);
		Params1.Bind( Initializer.ParameterMap, TEXT("Params1"), SPF_Mandatory);
		Params2.Bind( Initializer.ParameterMap, TEXT("Params2"), SPF_Optional);
		FillTexture.Bind( Initializer.ParameterMap, TEXT("FillTexture"), SPF_Mandatory);
	}

	// FShader interface.
	virtual bool Serialize(FArchive& Ar) override
	{
		bool bShaderHasOutdatedParameters = FGlobalShader::Serialize(Ar);
		Ar << FillValue << Params0 << Params1 << Params2 << FillTexture;
		return bShaderHasOutdatedParameters;
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}

	FShaderParameter FillValue;
	FShaderParameter Params0;	// Texture Width,Height (.xy); Use Exclude Rect 1 : 0 (.z)
	FShaderParameter Params1;	// Include X0,Y0 (.xy) - X1,Y1 (.zw)
	FShaderParameter Params2;	// ExcludeRect X0,Y0 (.xy) - X1,Y1 (.zw)
	FShaderResourceParameter FillTexture;
};

class FLongGPUTaskPS : public FGlobalShader
{
	DECLARE_EXPORTED_SHADER_TYPE(FLongGPUTaskPS,Global,UTILITYSHADERS_API);
public:
	FLongGPUTaskPS( )	{ }
	FLongGPUTaskPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
	:	FGlobalShader( Initializer )
	{
	}
	
	// FShader interface.
	virtual bool Serialize(FArchive& Ar) override
	{
		bool bShaderHasOutdatedParameters = FGlobalShader::Serialize(Ar);
		return bShaderHasOutdatedParameters;
	}
	
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		// MLCHANGES BEGIN
		return true;
		// MLCHANGES END
	}
};

