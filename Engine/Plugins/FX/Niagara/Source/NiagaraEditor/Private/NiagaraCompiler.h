// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NiagaraCommon.h"
#include "NiagaraHlslTranslator.h"
#include "INiagaraCompiler.h"
#include "Kismet2/CompilerResultsLog.h"
#include "ShaderCompiler.h"

class Error;
class UNiagaraGraph;
class UNiagaraNode;
class UNiagaraNodeFunctionCall;
class UNiagaraScript;
class UNiagaraScriptSource;
struct FNiagaraTranslatorOutput;

struct FNiagaraCompilerJob
{
	TSharedPtr<FShaderCompileJob, ESPMode::ThreadSafe> ShaderCompileJob;
	FNiagaraCompileResults CompileResults;
	double StartTime;
	FNiagaraTranslatorOutput TranslatorOutput;

	FNiagaraCompilerJob()
	{
		StartTime = FPlatformTime::Seconds();
	}
};

class NIAGARAEDITOR_API FHlslNiagaraCompiler : public INiagaraCompiler
{
protected:	
	/** Captures information about a script compile. */
	FNiagaraCompileResults CompileResults;

public:

	FHlslNiagaraCompiler();
	virtual ~FHlslNiagaraCompiler() {}

	//Begin INiagaraCompiler Interface
	virtual int32 CompileScript(const FNiagaraCompileRequestData* InCompileRequest, const FNiagaraCompileOptions& InOptions, const FNiagaraTranslateResults& InTranslateResults, FNiagaraTranslatorOutput *TranslatorOutput, FString& TranslatedHLSL) override;
	virtual TOptional<FNiagaraCompileResults> GetCompileResult(int32 JobID, bool bWait = false) override;

	virtual void Error(FText ErrorText) override;
	virtual void Warning(FText WarningText) override;

private:
	TUniquePtr<FNiagaraCompilerJob> CompilationJob;

	void DumpDebugInfo(FNiagaraCompileResults& CompileResult, bool bGPUScript);
};
