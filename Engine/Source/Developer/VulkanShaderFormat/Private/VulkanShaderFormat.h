// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "hlslcc.h"

enum class EVulkanShaderVersion
{
	ES3_1,
	ES3_1_NOUB,
	ES3_1_ANDROID,
	ES3_1_ANDROID_NOUB,
	SM4,
	SM4_NOUB,
	SM5,
	SM5_NOUB,
};
extern void CompileShader_Windows_Vulkan(const struct FShaderCompilerInput& Input,struct FShaderCompilerOutput& Output,const class FString& WorkingDirectory, EVulkanShaderVersion Version);


// Hold information to be able to call the compilers
struct FCompilerInfo
{
	const struct FShaderCompilerInput& Input;
	FString WorkingDirectory;
	FString Profile;
	uint32 CCFlags;
	EHlslShaderFrequency Frequency;
	bool bDebugDump;
	FString BaseSourceFilename;

	FCompilerInfo(const struct FShaderCompilerInput& InInput, const FString& InWorkingDirectory, EHlslShaderFrequency InFrequency);
};


struct FSpirv
{
	TArray<uint32> Data;
	struct FEntry
	{
		FEntry() = default;

		FEntry(const FString& InName, int32 InBinding)
			: Name(InName)
			, Binding(InBinding)
		{
		}

		FString Name;
		int32 Binding = -1;

		uint32 DescriptorSet = UINT32_MAX;

		// Index into the Spirv Word containing the descriptor set decoration
		uint32 WordDescriptorSetIndex = UINT32_MAX;

		// Index into the Spirv Word containing the binding index decoration
		uint32 WordBindingIndex = UINT32_MAX;
	};
	TArray<FEntry> ReflectionInfo;

	int32 FindBinding(const FString& Name, bool bOuter = false) const
	{
		for (const auto& Entry : ReflectionInfo)
		{
			if (Entry.Name == Name)
			{
				if (Entry.Binding == -1 && !bOuter)
				{
					// Try the outer group variable; eg 
					// layout(set=0,binding=0) buffer  CulledObjectBounds_BUFFER { vec4 CulledObjectBounds[]; };
					FString OuterName = Name;
					OuterName += TEXT("_BUFFER");
					return FindBinding(OuterName, true);
				}

				return Entry.Binding;
			}
		}

		return -1;
	}

	const FEntry* GetEntryByBindingIndex(int32 BindingIndex) const
	{
		for (int32 Index = 0; Index < ReflectionInfo.Num(); ++Index)
		{
			if (ReflectionInfo[Index].Binding == BindingIndex)
			{
				return &ReflectionInfo[Index];
			}
		}

		return nullptr;
	}

	FEntry* GetEntry(const FString& Name)
	{
		for (int32 Index = 0; Index < ReflectionInfo.Num(); ++Index)
		{
			if (ReflectionInfo[Index].Name == Name)
			{
				return &ReflectionInfo[Index];
			}
		}

		return nullptr;
	}

	FEntry const* GetEntry(const FString& Name) const
	{
		for (int32 Index = 0; Index < ReflectionInfo.Num(); ++Index)
		{
			if (ReflectionInfo[Index].Name == Name)
			{
				return &ReflectionInfo[Index];
			}
		}

		return nullptr;
	}
};
extern bool GenerateSpirv(const ANSICHAR* Source, FCompilerInfo& CompilerInfo, FString& OutErrors, const FString& DumpDebugInfoPath, FSpirv& OutSpirv);
