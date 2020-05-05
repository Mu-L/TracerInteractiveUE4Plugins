// Copyright Epic Games, Inc. All Rights Reserved.

#include "ShaderFormatD3D.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"
#include "Interfaces/IShaderFormat.h"
#include "Interfaces/IShaderFormatModule.h"

static FName NAME_PCD3D_SM5(TEXT("PCD3D_SM5"));
static FName NAME_PCD3D_ES3_1(TEXT("PCD3D_ES31"));

class FShaderFormatD3D : public IShaderFormat
{
	enum
	{
		/** Version for shader format, this becomes part of the DDC key. */
		UE_SHADER_PCD3D_SM5_VER = 8,
		UE_SHADER_PCD3D_ES3_1_VER = 8,
	};

	void CheckFormat(FName Format) const
	{
		check(Format == NAME_PCD3D_SM5 || Format == NAME_PCD3D_ES3_1);
	}

public:
	virtual uint32 GetVersion(FName Format) const override
	{
		CheckFormat(Format);
		if (Format == NAME_PCD3D_SM5) 
		{
			return UE_SHADER_PCD3D_SM5_VER;
		}
		else if (Format == NAME_PCD3D_ES3_1) 
		{
			return UE_SHADER_PCD3D_ES3_1_VER;
		}
		checkf(0, TEXT("Unknown Format %s"), *Format.ToString());
		return 0;
	}
	virtual void GetSupportedFormats(TArray<FName>& OutFormats) const
	{
		OutFormats.Add(NAME_PCD3D_SM5);
		OutFormats.Add(NAME_PCD3D_ES3_1);
	}

	virtual void CompileShader(FName Format, const struct FShaderCompilerInput& Input, struct FShaderCompilerOutput& Output,const FString& WorkingDirectory) const
	{
		CheckFormat(Format);
		if (Format == NAME_PCD3D_SM5)
		{
			CompileShader_Windows_SM5(Input, Output, WorkingDirectory);
		}
		else if (Format == NAME_PCD3D_ES3_1)
		{
			CompileShader_Windows_ES3_1(Input, Output, WorkingDirectory);
		}
		else
		{
			check(0);
		}
	}
	virtual const TCHAR* GetPlatformIncludeDirectory() const
	{
		return TEXT("D3D");
	}
};


/**
 * Module for D3D shaders
 */

static IShaderFormat* Singleton = NULL;

class FShaderFormatD3DModule : public IShaderFormatModule
{
public:
	virtual ~FShaderFormatD3DModule()
	{
		delete Singleton;
		Singleton = NULL;
	}
	virtual IShaderFormat* GetShaderFormat()
	{
		if (!Singleton)
		{
			Singleton = new FShaderFormatD3D();
		}
		return Singleton;
	}
};

IMPLEMENT_MODULE( FShaderFormatD3DModule, ShaderFormatD3D);
