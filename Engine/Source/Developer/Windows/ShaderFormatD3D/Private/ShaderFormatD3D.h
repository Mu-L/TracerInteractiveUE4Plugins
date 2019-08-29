// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Windows/WindowsHWrapper.h"

void CompileShader_Windows_SM5(const struct FShaderCompilerInput& Input,struct FShaderCompilerOutput& Output,const class FString& WorkingDirectory);
void CompileShader_Windows_SM4(const struct FShaderCompilerInput& Input,struct FShaderCompilerOutput& Output,const class FString& WorkingDirectory);
void CompileShader_Windows_ES2(const struct FShaderCompilerInput& Input,struct FShaderCompilerOutput& Output,const class FString& WorkingDirectory);
void CompileShader_Windows_ES3_1(const struct FShaderCompilerInput& Input,struct FShaderCompilerOutput& Output,const class FString& WorkingDirectory);
