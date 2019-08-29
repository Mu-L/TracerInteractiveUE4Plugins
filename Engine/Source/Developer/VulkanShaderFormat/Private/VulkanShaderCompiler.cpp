// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.
// .

#include "VulkanShaderFormat.h"
#include "VulkanCommon.h"
#include "ShaderPreprocessor.h"
#include "ShaderCompilerCommon.h"
#include "hlslcc.h"

#if PLATFORM_MAC
// Horrible hack as we need the enum available but the Vulkan headers do not compile on Mac
enum VkDescriptorType {
	VK_DESCRIPTOR_TYPE_SAMPLER = 0,
	VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER = 1,
	VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE = 2,
	VK_DESCRIPTOR_TYPE_STORAGE_IMAGE = 3,
	VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER = 4,
	VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER = 5,
	VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER = 6,
	VK_DESCRIPTOR_TYPE_STORAGE_BUFFER = 7,
	VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC = 8,
	VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC = 9,
	VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT = 10,
	VK_DESCRIPTOR_TYPE_BEGIN_RANGE = VK_DESCRIPTOR_TYPE_SAMPLER,
	VK_DESCRIPTOR_TYPE_END_RANGE = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,
	VK_DESCRIPTOR_TYPE_RANGE_SIZE = (VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT - VK_DESCRIPTOR_TYPE_SAMPLER + 1),
	VK_DESCRIPTOR_TYPE_MAX_ENUM = 0x7FFFFFFF
};
#else
#include "vulkan.h"
#endif
#include "VulkanBackend.h"
#include "VulkanShaderResources.h"
#include "Serialization/MemoryWriter.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"


DEFINE_LOG_CATEGORY_STATIC(LogVulkanShaderCompiler, Log, All); 

//static int32 GUseExternalShaderCompiler = 0;
//static FAutoConsoleVariableRef CVarVulkanUseExternalShaderCompiler(
//	TEXT("r.Vulkan.UseExternalShaderCompiler"),
//	GUseExternalShaderCompiler,
//	TEXT("Whether to use the internal shader compiling library or the external glslang tool.\n")
//	TEXT(" 0: Internal compiler\n")
//	TEXT(" 1: External compiler)"),
//	ECVF_Default
//	);


static TArray<ANSICHAR> ParseIdentifierANSI(const FString& Str)
{
	TArray<ANSICHAR> Result;
	Result.Reserve(Str.Len());
	for (int32 Index = 0; Index < Str.Len(); ++Index)
	{
		Result.Add(FChar::ToLower((ANSICHAR)Str[Index]));
	}
	Result.Add('\0');

	return Result;
}


inline const ANSICHAR * CStringEndOfLine(const ANSICHAR * Text)
{
	const ANSICHAR * LineEnd = FCStringAnsi::Strchr(Text, '\n');
	if (nullptr == LineEnd)
	{
		LineEnd = Text + FCStringAnsi::Strlen(Text);
	}
	return LineEnd;
}

inline bool CStringIsBlankLine(const ANSICHAR * Text)
{
	while (!FCharAnsi::IsLinebreak(*Text))
	{
		if (!FCharAnsi::IsWhitespace(*Text))
		{
			return false;
		}
		++Text;
	}
	return true;
}

static inline bool IsAlpha(ANSICHAR c)
{
	return (c >='a' && c <= 'z') || (c >='A' && c <='Z');
}

static inline bool IsDigit(ANSICHAR c)
{
	return c >= '0' && c <= '9';
}

static FString ParseIdentifier(const ANSICHAR* &Str)
{
	FString Result;

	while ((*Str >= 'A' && *Str <= 'Z')
		|| (*Str >= 'a' && *Str <= 'z')
		|| (*Str >= '0' && *Str <= '9')
		|| *Str == '_')
	{
		Result += *Str;
		++Str;
	}

	return Result;
}

inline void AppendCString(TArray<ANSICHAR> & Dest, const ANSICHAR * Source)
{
	if (Dest.Num() > 0)
	{
		Dest.Insert(Source, FCStringAnsi::Strlen(Source), Dest.Num() - 1);;
	}
	else
	{
		Dest.Append(Source, FCStringAnsi::Strlen(Source) + 1);
	}
}

inline bool MoveHashLines(TArray<ANSICHAR> & Dest, TArray<ANSICHAR> & Source)
{
	// Walk through the lines to find the first non-# line...
	const ANSICHAR * LineStart = Source.GetData();
	for (bool FoundNonHashLine = false; !FoundNonHashLine;)
	{
		const ANSICHAR * LineEnd = CStringEndOfLine(LineStart);
		if (LineStart[0] != '#' && !CStringIsBlankLine(LineStart))
		{
			FoundNonHashLine = true;
		}
		else if (LineEnd[0] == '\n')
		{
			LineStart = LineEnd + 1;
		}
		else
		{
			LineStart = LineEnd;
		}
	}
	// Copy the hash lines over, if we found any. And delete from
	// the source.
	if (LineStart > Source.GetData())
	{
		int32 LineLength = LineStart - Source.GetData();
		if (Dest.Num() > 0)
		{
			Dest.Insert(Source.GetData(), LineLength, Dest.Num() - 1);
		}
		else
		{
			Dest.Append(Source.GetData(), LineLength);
			Dest.Append("", 1);
		}
		if (Dest.Last(1) != '\n')
		{
			Dest.Insert("\n", 1, Dest.Num() - 1);
		}
		Source.RemoveAt(0, LineStart - Source.GetData());
		return true;
	}
	return false;
}

static bool Match(const ANSICHAR* &Str, ANSICHAR Char)
{
	if (*Str == Char)
	{
		++Str;
		return true;
	}

	return false;
}

template <typename T>
uint32 ParseNumber(const T* Str)
{
	check(Str);

	uint32 Num = 0;

	int32 Len = 0;
	// Find terminating character
	for(int32 Index=0; Index<128; Index++)
	{
		if(Str[Index] == 0)
		{
			Len = Index;
			break;
		}
	}

	check(Len > 0);

	// Find offset to integer type
	int32 Offset = -1;
	for(int32 Index=0; Index<Len; Index++)
	{
		if (*(Str + Index) >= '0' && *(Str + Index) <= '9')
		{
			Offset = Index;
			break;
		}
	}

	// Check if we found a number
	check(Offset >= 0);

	Str += Offset;

	while (*(Str) && *Str >= '0' && *Str <= '9')
	{
		Num = Num * 10 + *Str++ - '0';
	}

	return Num;
}

static inline FString GetExtension(EHlslShaderFrequency Frequency, bool bAddDot = true)
{
	const TCHAR* Name = nullptr;
	switch (Frequency)
	{
	default:
		check(0);
		// fallthrough...

	case HSF_PixelShader:		Name = TEXT(".frag"); break;
	case HSF_VertexShader:		Name = TEXT(".vert"); break;
	case HSF_ComputeShader:		Name = TEXT(".comp"); break;
	case HSF_GeometryShader:	Name = TEXT(".geom"); break;
	case HSF_HullShader:		Name = TEXT(".tesc"); break;
	case HSF_DomainShader:		Name = TEXT(".tese"); break;
	}

	if (!bAddDot)
	{
		++Name;
	}
	return FString(Name);
}

static uint32 GetTypeComponents(const FString& Type)
{
	static const FString TypePrefix[] = { "f", "i", "u" };
	uint32 Components = 0;
	int32 PrefixLength = 0;
	for (uint32 i = 0; i<ARRAY_COUNT(TypePrefix); i++)
	{
		const FString& Prefix = TypePrefix[i];
		const int32 CmpLength = Type.Contains(Prefix, ESearchCase::CaseSensitive, ESearchDir::FromStart);
		if (CmpLength == Prefix.Len())
		{
			PrefixLength = CmpLength;
			break;
		}
	}

	check(PrefixLength > 0);
	Components = ParseNumber(*Type + PrefixLength);

	check(Components > 0);
	return Components;
}

static void BuildShaderOutput(
	FShaderCompilerOutput& ShaderOutput,
	const FShaderCompilerInput& ShaderInput,
	const ANSICHAR* InShaderSource,
	int32 SourceLen,
	const FVulkanBindingTable& BindingTable,
	const ANSICHAR* InShaderSourceES,
	int32 SourceLenES,
	FSpirv& Spirv,
	const FString& DebugName
	)
{
	const ANSICHAR* USFSource = InShaderSource;
	CrossCompiler::FHlslccHeader CCHeader;
	if (!CCHeader.Read(USFSource, SourceLen))
	{
		UE_LOG(LogVulkanShaderCompiler, Error, TEXT("Bad hlslcc header found"));
		return;
	}

	if (*USFSource != '#')
	{
		UE_LOG(LogVulkanShaderCompiler, Error, TEXT("Bad hlslcc header found! Missing '#'!"));
		return;
	}

	FVulkanCodeHeader Header;

	FShaderParameterMap& ParameterMap = ShaderOutput.ParameterMap;
	EShaderFrequency Frequency = (EShaderFrequency)ShaderOutput.Target.Frequency;

	TBitArray<> UsedUniformBufferSlots;
	UsedUniformBufferSlots.Init(false, 32);


	static const FString AttributePrefix = TEXT("in_ATTRIBUTE");
	static const FString GL_Prefix = TEXT("gl_");
	for (auto& Input : CCHeader.Inputs)
	{
		// Only process attributes for vertex shaders.
		if (Frequency == SF_Vertex && Input.Name.StartsWith(AttributePrefix))
		{
			int32 AttributeIndex = ParseNumber(*Input.Name + AttributePrefix.Len());
			Header.SerializedBindings.InOutMask |= (1 << AttributeIndex);
		}
#if 0
		// Record user-defined input varyings
		else if (!Input.Name.StartsWith(GL_Prefix))
		{
			FVulkanShaderVarying Var;
			Var.Location = Input.Index;
			Var.Varying = ParseIdentifierANSI(Input.Name);
			Var.Components = GetTypeComponents(Input.Type);
			Header.SerializedBindings.InputVaryings.Add(Var);
		}
#endif
	}

	static const FString TargetPrefix = "out_Target";
	static const FString GL_FragDepth = "gl_FragDepth";
	for (auto& Output : CCHeader.Outputs)
	{
		// Only targets for pixel shaders must be tracked.
		if (Frequency == SF_Pixel && Output.Name.StartsWith(TargetPrefix))
		{
			uint8 TargetIndex = ParseNumber(*Output.Name + TargetPrefix.Len());
			Header.SerializedBindings.InOutMask |= (1 << TargetIndex);
		}
		// Only depth writes for pixel shaders must be tracked.
		else if (Frequency == SF_Pixel && Output.Name.Equals(GL_FragDepth))
		{
			Header.SerializedBindings.InOutMask |= 0x8000;
		}
#if 0
		// Record user-defined output varyings
		else if (!Output.Name.StartsWith(GL_Prefix))
		{
			FVulkanShaderVarying Var;
			Var.Location = Output.Index;
			Var.Varying = ParseIdentifierANSI(Output.Name);
			Var.Components = GetTypeComponents(Output.Type);
			Header.SerializedBindings.OutputVaryings.Add(Var);
		}
#endif
	}

	// Then 'normal' uniform buffers.
	const FString CBPrefix = "HLSLCC_CB";
	for (auto& UniformBlock : CCHeader.UniformBlocks)
	{
		uint16 UBIndex = UniformBlock.Index;
		if (UniformBlock.Name.StartsWith(CBPrefix))
		{
			// Skip...
		}
		else
		{
			// Regular UB
			int32 VulkanBindingIndex = Spirv.FindBinding(UniformBlock.Name, true);
			check(VulkanBindingIndex != -1);
			check(!UsedUniformBufferSlots[VulkanBindingIndex]);
			UsedUniformBufferSlots[VulkanBindingIndex] = true;
			ParameterMap.AddParameterAllocation(*UniformBlock.Name, VulkanBindingIndex, 0, 0);
			++Header.SerializedBindings.NumUniformBuffers;
		}
	}

	const TArray<FVulkanBindingTable::FBinding>& HlslccBindings = BindingTable.GetBindings();
	Header.NEWDescriptorInfo.NumBufferInfos = 0;
	Header.NEWDescriptorInfo.NumImageInfos = 0;
	for (int32 Index = 0; Index < HlslccBindings.Num(); ++Index)
	{
		const FVulkanBindingTable::FBinding& Binding = HlslccBindings[Index];

		Header.NEWDescriptorInfo.DescriptorTypes.Add(BindingToDescriptorType(Binding.Type));

		switch (Binding.Type)
		{
		case EVulkanBindingType::Sampler:
		case EVulkanBindingType::CombinedImageSampler:
		case EVulkanBindingType::Image:
		case EVulkanBindingType::StorageImage:
			++Header.NEWDescriptorInfo.NumImageInfos;
			break;
		case EVulkanBindingType::UniformBuffer:
		case EVulkanBindingType::StorageBuffer:
			++Header.NEWDescriptorInfo.NumBufferInfos;
			break;
		case EVulkanBindingType::PackedUniformBuffer:
			{
				FVulkanCodeHeader::FPackedUBToVulkanBindingIndex* New = new(Header.NEWPackedUBToVulkanBindingIndices) FVulkanCodeHeader::FPackedUBToVulkanBindingIndex;
				New->TypeName = (CrossCompiler::EPackedTypeName)Binding.SubType;
				New->VulkanBindingIndex = Index;
				++Header.NEWDescriptorInfo.NumBufferInfos;
			}
			break;
		case EVulkanBindingType::UniformTexelBuffer:
		case EVulkanBindingType::StorageTexelBuffer:
			break;
		default:
			checkf(0, TEXT("Binding Type %d not found"), (int32)Binding.Type);
			break;
		}
	}

	const uint16 BytesPerComponent = 4;

	// Packed global uniforms
	TMap<CrossCompiler::EPackedTypeName, uint32> PackedGlobalArraySize;
	for (auto& PackedGlobal : CCHeader.PackedGlobals)
	{
		int32 Found = -1;
		for (int32 Index = 0; Index < Header.NEWPackedUBToVulkanBindingIndices.Num(); ++Index)
		{
			if (Header.NEWPackedUBToVulkanBindingIndices[Index].TypeName == (CrossCompiler::EPackedTypeName)PackedGlobal.PackedType)
			{
				Found = Index;
				break;
				}
			}
		check(Found != -1);

		ParameterMap.AddParameterAllocation(
			*PackedGlobal.Name,
			Found,
			PackedGlobal.Offset * BytesPerComponent,
			PackedGlobal.Count * BytesPerComponent
			);

		uint32& Size = PackedGlobalArraySize.FindOrAdd((CrossCompiler::EPackedTypeName)PackedGlobal.PackedType);
		Size = FMath::Max<uint32>(BytesPerComponent * (PackedGlobal.Offset + PackedGlobal.Count), Size);
	}

	// Packed Uniform Buffers
	TMap<int, TMap<CrossCompiler::EPackedTypeName, uint16> > PackedUniformBuffersSize;
	Header.UNUSED_NumNonGlobalUBs = 0;
	for (auto& PackedUB : CCHeader.PackedUBs)
	{
		//check(PackedUB.Attribute.Index == Header.SerializedBindings.NumUniformBuffers);
		check(!UsedUniformBufferSlots[Header.UNUSED_NumNonGlobalUBs]);
		UsedUniformBufferSlots[Header.UNUSED_NumNonGlobalUBs] = true;
		ParameterMap.AddParameterAllocation(*PackedUB.Attribute.Name, Header.UNUSED_NumNonGlobalUBs++, PackedUB.Attribute.Index, 0);
	}

	//#todo-rco: When using regular UBs, also set UsedUniformBufferSlots[] = 1

	// Packed Uniform Buffers copy lists & setup sizes for each UB/Precision entry
	enum EFlattenUBState
	{
		Unknown,
		GroupedUBs,
		FlattenedUBs,
	};

	EFlattenUBState UBState = Unknown;

	// Remap the destination UB index into the packed global array index
	auto RemapDestIndexIntoPackedUB = [&Header](int8 DestUBTypeName)
	{
		for (int32 Index = 0; Index < Header.NEWPackedUBToVulkanBindingIndices.Num(); ++Index)
		{
			if (Header.NEWPackedUBToVulkanBindingIndices[Index].TypeName == (CrossCompiler::EPackedTypeName)DestUBTypeName)
			{
				return Index;
			}
		}
		check(0);
		return -1;
	};

	for (auto& PackedUBCopy : CCHeader.PackedUBCopies)
	{
		CrossCompiler::FUniformBufferCopyInfo CopyInfo;
		CopyInfo.SourceUBIndex = PackedUBCopy.SourceUB;
		CopyInfo.SourceOffsetInFloats = PackedUBCopy.SourceOffset;
		CopyInfo.DestUBTypeName = PackedUBCopy.DestPackedType;
		CopyInfo.DestUBIndex = RemapDestIndexIntoPackedUB(CopyInfo.DestUBTypeName);
		CopyInfo.DestUBTypeIndex = CrossCompiler::PackedTypeNameToTypeIndex(CopyInfo.DestUBTypeName);
		CopyInfo.DestOffsetInFloats = PackedUBCopy.DestOffset;
		CopyInfo.SizeInFloats = PackedUBCopy.Count;

		Header.UniformBuffersCopyInfo.Add(CopyInfo);

		auto& UniformBufferSize = PackedUniformBuffersSize.FindOrAdd(CopyInfo.DestUBIndex);
		uint16& Size = UniformBufferSize.FindOrAdd((CrossCompiler::EPackedTypeName)CopyInfo.DestUBTypeName);
		Size = FMath::Max<uint16>(BytesPerComponent * (CopyInfo.DestOffsetInFloats + CopyInfo.SizeInFloats), Size);

		check(UBState == Unknown || UBState == GroupedUBs);
		UBState = GroupedUBs;
	}

	for (auto& PackedUBCopy : CCHeader.PackedUBGlobalCopies)
	{
		CrossCompiler::FUniformBufferCopyInfo CopyInfo;
		CopyInfo.SourceUBIndex = PackedUBCopy.SourceUB;
		CopyInfo.SourceOffsetInFloats = PackedUBCopy.SourceOffset;
		CopyInfo.DestUBTypeName = PackedUBCopy.DestPackedType;
		CopyInfo.DestUBIndex = RemapDestIndexIntoPackedUB(CopyInfo.DestUBTypeName);
		CopyInfo.DestUBTypeIndex = CrossCompiler::PackedTypeNameToTypeIndex(CopyInfo.DestUBTypeName);
		CopyInfo.DestOffsetInFloats = PackedUBCopy.DestOffset;
		CopyInfo.SizeInFloats = PackedUBCopy.Count;

		Header.UniformBuffersCopyInfo.Add(CopyInfo);

		uint32& Size = PackedGlobalArraySize.FindOrAdd((CrossCompiler::EPackedTypeName)CopyInfo.DestUBTypeName);
		Size = FMath::Max<uint32>(BytesPerComponent * (CopyInfo.DestOffsetInFloats + CopyInfo.SizeInFloats), Size);

		check(UBState == Unknown || UBState == FlattenedUBs);
		UBState = FlattenedUBs;
	}

	// Generate a shortcut table for the PackedUBGlobalCopies
	TMap<uint32, uint32> PackedUBGlobalCopiesRanges;
	{
		int32 MaxDestUBIndex = -1;
		{
			// Verify table is sorted
			int32 PrevSourceUB = -1;
			int32 Index = 0;
			for (auto& Copy : Header.UniformBuffersCopyInfo)
			{
				if (PrevSourceUB < Copy.SourceUBIndex)
				{
					PrevSourceUB = Copy.SourceUBIndex;
					MaxDestUBIndex = FMath::Max(MaxDestUBIndex, (int32)Copy.SourceUBIndex);
					PackedUBGlobalCopiesRanges.Add(Copy.SourceUBIndex) = (Index << 16) | 1;
				}
				else if (PrevSourceUB == Copy.SourceUBIndex)
				{
					++PackedUBGlobalCopiesRanges.FindChecked(Copy.SourceUBIndex);
				}
				else
				{
					// Internal error
					check(0);
				}
				++Index;
			}
		}

		Header.NEWEmulatedUBCopyRanges.AddZeroed(MaxDestUBIndex + 1);
		for (int32 Index = 0; Index <= MaxDestUBIndex; ++Index)
	{
			uint32* Found = PackedUBGlobalCopiesRanges.Find(Index);
			if (Found)
			{
				Header.NEWEmulatedUBCopyRanges[Index] = *Found;
			}
		}
	}

	// Update Packed global array sizes
	Header.NEWPackedGlobalUBSizes.AddZeroed(Header.NEWPackedUBToVulkanBindingIndices.Num());
	for (auto& Pair : PackedGlobalArraySize)
	{
		CrossCompiler::EPackedTypeName TypeName = Pair.Key;
		int32 PackedArrayIndex = -1;
		for (int32 Index = 0; Index < Header.NEWPackedUBToVulkanBindingIndices.Num(); ++Index)
		{
			if (Header.NEWPackedUBToVulkanBindingIndices[Index].TypeName == TypeName)
			{
				PackedArrayIndex = Index;
				break;
			}
		}
		check(PackedArrayIndex != -1);
		// In bytes
		Header.NEWPackedGlobalUBSizes[PackedArrayIndex] = Align((uint32)Pair.Value, (uint32)16);
	}

	TSet<FString> SharedSamplerStates;
	for (int32 i = 0; i < CCHeader.SamplerStates.Num(); i++)
	{
		const FString& Name = CCHeader.SamplerStates[i].Name;
		int32 HlslccBindingIndex = Spirv.FindBinding(Name);
		check(HlslccBindingIndex != -1);

		SharedSamplerStates.Add(Name);
		auto& Binding = HlslccBindings[HlslccBindingIndex];
		int32 BindingIndex = Spirv.FindBinding(Binding.Name, true);
		check(BindingIndex != -1);
		ParameterMap.AddParameterAllocation(
			*Name,
			0,
			BindingIndex,
			1
		);
	}

	for (auto& Sampler : CCHeader.Samplers)
	{
		int32 VulkanBindingIndex = Spirv.FindBinding(Sampler.Name, true);
		check(VulkanBindingIndex != -1);
		ParameterMap.AddParameterAllocation(
			*Sampler.Name,
			Sampler.Offset,
			VulkanBindingIndex,
			Sampler.Count
			);

		Header.SerializedBindings.NumSamplers = FMath::Max<uint8>(
			Header.SerializedBindings.NumSamplers,
			Sampler.Offset + Sampler.Count
			);

		for (auto& SamplerState : Sampler.SamplerStates)
		{
			if (!SharedSamplerStates.Contains(SamplerState))
			{
				// ParameterMap does not use a TMultiMap, so we cannot push the same entry to it more than once!  if we try to, we've done something wrong...
				check(!ParameterMap.ContainsParameterAllocation(*SamplerState));
				ParameterMap.AddParameterAllocation(
					*SamplerState,
					Sampler.Offset,
					VulkanBindingIndex,
					Sampler.Count
				);
			}
		}
	}

	for (auto& UAV : CCHeader.UAVs)
	{
		int32 VulkanBindingIndex = Spirv.FindBinding(UAV.Name);
		check(VulkanBindingIndex != -1);

		ParameterMap.AddParameterAllocation(
			*UAV.Name,
			UAV.Offset,
			VulkanBindingIndex,
			UAV.Count
			);

		Header.SerializedBindings.NumUAVs = FMath::Max<uint8>(
			Header.SerializedBindings.NumUAVs,
			UAV.Offset + UAV.Count
			);
	}

	// Lats make sure that there is some type of name visible
	Header.ShaderName = CCHeader.Name.Len() > 0 ? CCHeader.Name : DebugName;

	FSHA1::HashBuffer(USFSource, FCStringAnsi::Strlen(USFSource), (uint8*)&Header.SourceHash);

	TArray<FString> OriginalParameters;
	ShaderOutput.ParameterMap.GetAllParameterNames(OriginalParameters);

	// Build the SRT for this shader.
	{
		// Build the generic SRT for this shader.
		FShaderCompilerResourceTable GenericSRT;
		if (!BuildResourceTableMapping(ShaderInput.Environment.ResourceTableMap, ShaderInput.Environment.ResourceTableLayoutHashes, UsedUniformBufferSlots, ShaderOutput.ParameterMap, /*MaxBoundResourceTable, */GenericSRT))
		{
			ShaderOutput.Errors.Add(TEXT("Internal error on BuildResourceTableMapping."));
			return;
		}

		// Copy over the bits indicating which resource tables are active.
		Header.SerializedBindings.ShaderResourceTable.ResourceTableBits = GenericSRT.ResourceTableBits;

		Header.SerializedBindings.ShaderResourceTable.ResourceTableLayoutHashes = GenericSRT.ResourceTableLayoutHashes;

		// Now build our token streams.
		BuildResourceTableTokenStream(GenericSRT.TextureMap, GenericSRT.MaxBoundResourceTable, Header.SerializedBindings.ShaderResourceTable.TextureMap, true);
		BuildResourceTableTokenStream(GenericSRT.ShaderResourceViewMap, GenericSRT.MaxBoundResourceTable, Header.SerializedBindings.ShaderResourceTable.ShaderResourceViewMap, true);
		BuildResourceTableTokenStream(GenericSRT.SamplerMap, GenericSRT.MaxBoundResourceTable, Header.SerializedBindings.ShaderResourceTable.SamplerMap, true);
		BuildResourceTableTokenStream(GenericSRT.UnorderedAccessViewMap, GenericSRT.MaxBoundResourceTable, Header.SerializedBindings.ShaderResourceTable.UnorderedAccessViewMap, true);
	}

	TArray<FString> NewParameters;
	ShaderOutput.ParameterMap.GetAllParameterNames(NewParameters);

	// Mark all used uniform buffer indices; however some are empty (eg GBuffers) so gather those as NewParameters
	Header.UniformBuffersWithDescriptorMask = *UsedUniformBufferSlots.GetData();
	uint16 NumParams = 0;
	for (int32 Index = NewParameters.Num() - 1; Index >= 0; --Index)
	{
		uint16 OutIndex, OutBase, OutSize;
		bool bFound = ShaderOutput.ParameterMap.FindParameterAllocation(*NewParameters[Index], OutIndex, OutBase, OutSize);
		ensure(bFound);
		NumParams = FMath::Max((uint16)(OutIndex + 1), NumParams);
		if (OriginalParameters.Contains(NewParameters[Index]))
		{
			NewParameters.RemoveAtSwap(Index, 1, false);
		}
	}

	// All newly added parameters are empty uniform buffers (with no constant data used), so no Vulkan Binding is required: remove from the mask
	for (int32 Index = 0; Index < NewParameters.Num(); ++Index)
	{
		uint16 OutIndex, OutBase, OutSize;
		ShaderOutput.ParameterMap.FindParameterAllocation(*NewParameters[Index], OutIndex, OutBase, OutSize);
		Header.UniformBuffersWithDescriptorMask = Header.UniformBuffersWithDescriptorMask & ~((uint64)1 << (uint64)OutIndex);
	}

	// Write out the header and shader source code.
	FMemoryWriter Ar(ShaderOutput.ShaderCode.GetWriteAccess(), true);
	Ar << Header;

	TArray<ANSICHAR> DebugNameArray;
	AppendCString(DebugNameArray, TCHAR_TO_ANSI(*DebugName));
	Ar << DebugNameArray;

	check(Spirv.Data.Num() != 0);
	Ar << Spirv.Data;

	// store data we can pickup later with ShaderCode.FindOptionalData('n'), could be removed for shipping
	// Daniel L: This GenerateShaderName does not generate a deterministic output among shaders as the shader code can be shared. 
	//			uncommenting this will cause the project to have non deterministic materials and will hurt patch sizes
	// ShaderOutput.ShaderCode.AddOptionalData('n', TCHAR_TO_UTF8(*ShaderInput.GenerateShaderName()));

	ShaderOutput.NumInstructions = 0;
	ShaderOutput.NumTextureSamplers = Header.SerializedBindings.NumSamplers;
	ShaderOutput.bSucceeded = true;

	if (ShaderInput.ExtraSettings.bExtractShaderSource)
	{
		TArray<ANSICHAR> CodeOriginal;
		CodeOriginal.Append(USFSource, FCStringAnsi::Strlen(USFSource) + 1);
		ShaderOutput.OptionalFinalShaderSource = FString(CodeOriginal.GetData());
	}
	if (ShaderInput.ExtraSettings.OfflineCompilerPath.Len() > 0)
	{
		if (IsVulkanMobilePlatform((EShaderPlatform)ShaderInput.Target.Platform))
		{
			CompileOfflineMali(ShaderInput, ShaderOutput, (ANSICHAR *)Spirv.Data.GetData(), Spirv.Data.Num(), true);
		}
	}
}

//static void BuildShaderOutput(
//	FShaderCompilerOutput& ShaderOutput,
//	const FShaderCompilerInput& ShaderInput,
//	const ANSICHAR* InShaderSource,
//	int32 SourceLen,
//	const FVulkanBindingTable& BindingTable,
//	const ANSICHAR* InShaderSourceES,
//	int32 SourceLenES,
//	const FString& SPVFile,
//	const FString& DebugName
//	)
//{
//	TArray<uint8> Spirv;
//	FFileHelper::LoadFileToArray(Spirv, *SPVFile);
//
//	BuildShaderOutput(
//		ShaderOutput,
//		ShaderInput,
//		InShaderSource,
//		SourceLen,
//		BindingTable,
//		InShaderSourceES,
//		SourceLenES,
//		Spirv,
//		DebugName
//		);
//}

static bool StringToFile(const FString& Filepath, const char* str)
{
	int32 StrLength = str ? FCStringAnsi::Strlen(str) : 0;

	if(StrLength == 0)
	{
		return false;
	}

	FArchive* FileWriter = IFileManager::Get().CreateFileWriter(*Filepath);
	if (FileWriter)
	{
		// const cast...
		FileWriter->Serialize((void*)str, StrLength+1);
		FileWriter->Close();
		delete FileWriter;
	}

	return true;
}

static char* PatchGLSLVersionPosition(const char* InSourceGLSL)
{
	if(!InSourceGLSL)
	{
		return nullptr;
	}

	const int32 InSrcLength = FCStringAnsi::Strlen(InSourceGLSL);
	if(InSrcLength <= 0)
	{
		return nullptr;
	}

	char* GlslSource = (char*)malloc(InSrcLength+1);
	check(GlslSource);
	memcpy(GlslSource, InSourceGLSL, InSrcLength+1);

	// Find begin of "#version" line
	char* VersionBegin = strstr(GlslSource, "#version");

	// Find end of "#version line"
	char* VersionEnd = VersionBegin ? strstr(VersionBegin, "\n") : nullptr;

	if(VersionEnd)
	{
		// Add '\n' character
		VersionEnd++;

		const int32 VersionLineLength = VersionEnd - VersionBegin - 1;

		// Copy version line into a temporary buffer (+1 for term-char).
		const int32 TmpStrBytes = (VersionEnd - VersionBegin) + 1;
		char* TmpVersionLine = (char*)malloc(TmpStrBytes);
		check(TmpVersionLine);
		memset(TmpVersionLine, 0, TmpStrBytes);
		memcpy(TmpVersionLine, VersionBegin, VersionEnd - VersionBegin);

		// Erase current version number, just replace it with spaces...
		for(char* str=VersionBegin; str<(VersionEnd-1); str++)
		{
			*str=' ';
		}

		// Allocate new source buffer to place version string on the first line.
		char* NewSource = (char*)malloc(InSrcLength + TmpStrBytes);
		check(NewSource);

		// Copy version line
		memcpy(NewSource, TmpVersionLine, TmpStrBytes);

		// Copy original source after the source line
		// -1 to offset back from the term-char
		memcpy(NewSource + TmpStrBytes - 1, GlslSource, InSrcLength + 1);

		free(TmpVersionLine);
		TmpVersionLine = nullptr;
							
		// Update string pointer
		free(GlslSource);
		GlslSource = NewSource;
	}

	return GlslSource;
}

static void PatchForToWhileLoop(char** InOutSourceGLSL)
{
	//checkf(InOutSourceGLSL, TEXT("Attempting to patch an invalid glsl source-string"));

	char* srcGlsl = *InOutSourceGLSL;
	//checkf(srcGlsl, TEXT("Attempting to patch an invalid glsl source-string"));

	const size_t InSrcLength = strlen(srcGlsl);
	//checkf(InSrcLength > 0, TEXT("Attempting to patch an empty glsl source-string."));

	// This is what we are relacing
	const char* srcPatchable = "for (;;)";
	const size_t srcPatchableLength = strlen(srcPatchable);

	// This is where we are replacing with
	const char* dstPatchable = "while(true)";
	const size_t dstPatchableLength = strlen(dstPatchable);
	
	// Find number of occurances
	int numNumberOfOccurances = 0;
	for(char* dstReplacePos = strstr(srcGlsl, srcPatchable);
		dstReplacePos != NULL;
		dstReplacePos = strstr(dstReplacePos+srcPatchableLength, srcPatchable))
	{
		numNumberOfOccurances++;
	}

	// No patching needed
	if(numNumberOfOccurances == 0)
	{
		return;
	}

	// Calc new required string-length
	const size_t newLength = InSrcLength + (dstPatchableLength-srcPatchableLength)*numNumberOfOccurances;

	// Allocate destination buffer + 1 char for terminating character
	char* GlslSource = (char*)malloc(newLength+1);
	check(GlslSource)
	memset(GlslSource, 0, sizeof(char)*(newLength+1));
	memcpy(GlslSource, srcGlsl, InSrcLength);

	// Scan and replace
	char* dstReplacePos = strstr(GlslSource, srcPatchable);
	char* srcReplacePos = strstr(srcGlsl, srcPatchable);
	int bytesRemaining = (int)newLength;
	while(dstReplacePos != NULL && srcReplacePos != NULL)
	{
		// Replace the string
		bytesRemaining = (int)newLength - (int)(dstReplacePos - GlslSource);
		memcpy(dstReplacePos, dstPatchable, dstPatchableLength);
		
		// Increment positions
		dstReplacePos+=dstPatchableLength;
		srcReplacePos+=srcPatchableLength;

		// Append remaining code
		int bytesToCopy = InSrcLength - (int)(srcReplacePos - srcGlsl);
		memcpy(dstReplacePos, srcReplacePos, bytesToCopy);

		dstReplacePos = strstr(dstReplacePos, srcPatchable);
		srcReplacePos = strstr(srcReplacePos, srcPatchable);
	}

	free(*InOutSourceGLSL);

	*InOutSourceGLSL = GlslSource;
}

static FString CreateShaderCompileCommandLine(FCompilerInfo& CompilerInfo, EHlslCompileTarget Target)
{
	//const FString OutputFileNoExt = FPaths::GetBaseFilename(OutputFile);
	FString CmdLine;
	FString GLSLFile = CompilerInfo.Input.DumpDebugInfoPath / (TEXT("Output") + GetExtension(CompilerInfo.Frequency));
	FString SPVFile = CompilerInfo.Input.DumpDebugInfoPath / TEXT("Output.spv");
	FString SPVDisasmFile = CompilerInfo.Input.DumpDebugInfoPath / TEXT("Output.spvasm");

	CmdLine += TEXT("\n\"");
#if PLATFORM_WINDOWS
	CmdLine += *(FPaths::RootDir() / TEXT("Engine/Binaries/ThirdParty/glslang/glslangValidator.exe"));
#elif PLATFORM_LINUX
	CmdLine += *(FPaths::RootDir() / TEXT("Engine/Binaries/ThirdParty/glslang/glslangValidator"));
#endif
	CmdLine += TEXT("\"");
	CmdLine += TEXT(" -V -H -r -o \"") + SPVFile + TEXT("\" \"") + GLSLFile + TEXT("\" > \"" + SPVDisasmFile + "\"");
	CmdLine += TEXT("\npause\n");

	return CmdLine;
}


FCompilerInfo::FCompilerInfo(const FShaderCompilerInput& InInput, const FString& InWorkingDirectory, EHlslShaderFrequency InFrequency) :
	Input(InInput),
	WorkingDirectory(InWorkingDirectory),
	CCFlags(0),
	Frequency(InFrequency),
	bDebugDump(false)
{
	bDebugDump = Input.DumpDebugInfoPath != TEXT("") && IFileManager::Get().DirectoryExists(*Input.DumpDebugInfoPath);
	BaseSourceFilename = Input.GetSourceFilename();
}

/**
 * Compile a shader using the internal shader compiling library
 */
static bool CompileUsingInternal(FCompilerInfo& CompilerInfo, FVulkanBindingTable& BindingTable, TArray<ANSICHAR>& GlslSource, FString& EntryPointName, FShaderCompilerOutput& Output)
{
	FString Errors;
	FSpirv Spirv;
	if (GenerateSpirv(GlslSource.GetData(), CompilerInfo, Errors, CompilerInfo.Input.DumpDebugInfoPath, Spirv))
	{
		FString DebugName = CompilerInfo.Input.DumpDebugInfoPath.Right(CompilerInfo.Input.DumpDebugInfoPath.Len() - CompilerInfo.Input.DumpDebugInfoRootPath.Len());

		Output.Target = CompilerInfo.Input.Target;
		BuildShaderOutput(Output, CompilerInfo.Input,
			GlslSource.GetData(), GlslSource.Num(),
			BindingTable, nullptr, 0, Spirv, DebugName);
		return true;
	}
	else
	{
		if (Errors.Len() > 0)
		{
			FShaderCompilerError* Error = new(Output.Errors) FShaderCompilerError();
			Error->ErrorLineString = Errors;
		}

		return false;
	}
}


static bool CallHlslcc(const FString& PreprocessedShader, FVulkanBindingTable& BindingTable, FCompilerInfo& CompilerInfo, FString& EntryPointName, EHlslCompileTarget HlslCompilerTarget, FShaderCompilerOutput& Output, TArray<ANSICHAR>& OutGlsl)
{
	char* GlslShaderSource = nullptr;
	char* ErrorLog = nullptr;

	auto InnerFunction = [&]()
	{
		// Call hlslcc
		FVulkanCodeBackend VulkanBackend(CompilerInfo.CCFlags, BindingTable, HlslCompilerTarget);
		FHlslCrossCompilerContext CrossCompilerContext(CompilerInfo.CCFlags, CompilerInfo.Frequency, HlslCompilerTarget);
		const bool bShareSamplers = false;
		FVulkanLanguageSpec VulkanLanguageSpec(true);
		int32 Result = 0;
		if (CrossCompilerContext.Init(TCHAR_TO_ANSI(*CompilerInfo.Input.VirtualSourceFilePath), &VulkanLanguageSpec))
		{
			Result = CrossCompilerContext.Run(
				TCHAR_TO_ANSI(*PreprocessedShader),
				TCHAR_TO_ANSI(*EntryPointName),
				&VulkanBackend,
				&GlslShaderSource,
				&ErrorLog
				) ? 1 : 0;
		}

		if (Result == 0)
		{
			FString Tmp = ANSI_TO_TCHAR(ErrorLog);
			TArray<FString> ErrorLines;
			Tmp.ParseIntoArray(ErrorLines, TEXT("\n"), true);
			for (int32 LineIndex = 0; LineIndex < ErrorLines.Num(); ++LineIndex)
			{
				const FString& Line = ErrorLines[LineIndex];
				CrossCompiler::ParseHlslccError(Output.Errors, Line, CompilerInfo.Input.bSkipPreprocessedCache);
			}

			return false;
		}

		check(GlslShaderSource);

		// Patch GLSL source
		PatchForToWhileLoop(&GlslShaderSource);

		if (CompilerInfo.bDebugDump)
		{
			FString DumpedGlslFile = CompilerInfo.Input.DumpDebugInfoPath / (TEXT("Output") + GetExtension(CompilerInfo.Frequency));
			FArchive* FileWriter = IFileManager::Get().CreateFileWriter(*DumpedGlslFile);
			if (FileWriter)
			{
				FileWriter->Serialize(GlslShaderSource, FCStringAnsi::Strlen(GlslShaderSource));
				FileWriter->Close();
				delete FileWriter;
			}
		}

		int32 Length = FCStringAnsi::Strlen(GlslShaderSource);
		OutGlsl.AddUninitialized(Length + 1);
		FCStringAnsi::Strcpy(OutGlsl.GetData(), Length + 1, GlslShaderSource);

		return true;
	};

	bool bResult = InnerFunction();

	if (ErrorLog)
	{
		free(ErrorLog);
	}

	if (GlslShaderSource)
	{
		free(GlslShaderSource);
	}

	return bResult;
}


void CompileShader_Windows_Vulkan(const FShaderCompilerInput& Input, FShaderCompilerOutput& Output, const class FString& WorkingDirectory, EVulkanShaderVersion Version)
{
	check(IsVulkanPlatform((EShaderPlatform)Input.Target.Platform));

	//if (GUseExternalShaderCompiler)
	//{
	//	// Old path...
	//	CompileUsingExternal(Input, Output, WorkingDirectory, Version);
	//	return;
	//}

	const bool bIsSM5 = (Version == EVulkanShaderVersion::SM5 || Version == EVulkanShaderVersion::SM5_NOUB);
	const bool bIsSM4 = (Version == EVulkanShaderVersion::SM4 || Version == EVulkanShaderVersion::SM4_NOUB);

	const EHlslShaderFrequency FrequencyTable[] =
	{
		HSF_VertexShader,
		bIsSM5 ? HSF_HullShader : HSF_InvalidFrequency,
		bIsSM5 ? HSF_DomainShader : HSF_InvalidFrequency,
		HSF_PixelShader,
		(bIsSM4 || bIsSM5) ? HSF_GeometryShader : HSF_InvalidFrequency,
		bIsSM5 ? HSF_ComputeShader : HSF_InvalidFrequency
	};

	const EHlslShaderFrequency Frequency = FrequencyTable[Input.Target.Frequency];
	if (Frequency == HSF_InvalidFrequency)
	{
		Output.bSucceeded = false;
		FShaderCompilerError* NewError = new(Output.Errors) FShaderCompilerError();
		NewError->StrippedErrorMessage = FString::Printf(
			TEXT("%s shaders not supported for use in Vulkan."),
			CrossCompiler::GetFrequencyName((EShaderFrequency)Input.Target.Frequency));
		return;
	}

	FString PreprocessedShader;
	FShaderCompilerDefinitions AdditionalDefines;
	EHlslCompileTarget HlslCompilerTarget = HCT_FeatureLevelES3_1Ext;
	EHlslCompileTarget HlslCompilerTargetES = HCT_FeatureLevelES3_1Ext;
	AdditionalDefines.SetDefine(TEXT("COMPILER_HLSLCC"), 1);
	AdditionalDefines.SetDefine(TEXT("COMPILER_VULKAN"), 1);
	if (Version == EVulkanShaderVersion::ES3_1 || Version == EVulkanShaderVersion::ES3_1_ANDROID)
	{
		HlslCompilerTarget = HCT_FeatureLevelES3_1Ext;
		HlslCompilerTargetES = HCT_FeatureLevelES3_1Ext;
		AdditionalDefines.SetDefine(TEXT("USE_LOWER_PRECISION"), 1);
		AdditionalDefines.SetDefine(TEXT("ES2_PROFILE"), 1);
		AdditionalDefines.SetDefine(TEXT("VULKAN_PROFILE"), 1);
	}
	else if (bIsSM4)
	{
		HlslCompilerTarget = HCT_FeatureLevelSM4;
		HlslCompilerTargetES = HCT_FeatureLevelSM4;
		AdditionalDefines.SetDefine(TEXT("VULKAN_PROFILE_SM4"), 1);
	}
	else if (bIsSM5)
	{
		HlslCompilerTarget = HCT_FeatureLevelSM5;
		HlslCompilerTargetES = HCT_FeatureLevelSM5;
		AdditionalDefines.SetDefine(TEXT("VULKAN_PROFILE_SM5"), 1);
	}
	AdditionalDefines.SetDefine(TEXT("row_major"), TEXT(""));

	AdditionalDefines.SetDefine(TEXT("COMPILER_SUPPORTS_ATTRIBUTES"), (uint32)1);

	const bool bUseFullPrecisionInPS = Input.Environment.CompilerFlags.Contains(CFLAG_UseFullPrecisionInPS);
	if (bUseFullPrecisionInPS)
	{
		AdditionalDefines.SetDefine(TEXT("FORCE_FLOATS"), (uint32)1);
	}

	//#todo-rco: Glslang doesn't allow this yet
	AdditionalDefines.SetDefine(TEXT("noperspective"), TEXT(""));

	// Preprocess the shader.
	FString PreprocessedShaderSource;
	if (Input.bSkipPreprocessedCache)
	{
		if (!FFileHelper::LoadFileToString(PreprocessedShaderSource, *Input.VirtualSourceFilePath))
		{
			return;
		}

		// Remove const as we are on debug-only mode
		CrossCompiler::CreateEnvironmentFromResourceTable(PreprocessedShaderSource, (FShaderCompilerEnvironment&)Input.Environment);
	}
	else
	{
		if (!PreprocessShader(PreprocessedShaderSource, Output, Input, AdditionalDefines))
		{
			// The preprocessing stage will add any relevant errors.
			return;
		}

		// Disable instanced stereo until supported for Vulkan
		StripInstancedStereo(PreprocessedShaderSource);
	}

	FString EntryPointName = Input.EntryPointName;

	RemoveUniformBuffersFromSource(Input.Environment, PreprocessedShaderSource);

	FCompilerInfo CompilerInfo(Input, WorkingDirectory, Frequency);

	CompilerInfo.CCFlags |= HLSLCC_PackUniforms;
	CompilerInfo.CCFlags |= HLSLCC_PackUniformsIntoUniformBuffers;
	if (Version == EVulkanShaderVersion::SM4 || Version == EVulkanShaderVersion::SM5 || Version == EVulkanShaderVersion::ES3_1_ANDROID || Version == EVulkanShaderVersion::ES3_1)
	{
		CompilerInfo.CCFlags |= HLSLCC_FlattenUniformBufferStructures;
	}
	else
	{
		CompilerInfo.CCFlags |= HLSLCC_FlattenUniformBuffers;
	}

	if (bUseFullPrecisionInPS)
	{
		CompilerInfo.CCFlags |= HLSLCC_UseFullPrecisionInPS;
	}

	CompilerInfo.CCFlags |= HLSLCC_SeparateShaderObjects;
	CompilerInfo.CCFlags |= HLSLCC_KeepSamplerAndImageNames;

	// ES doesn't support origin layout
	CompilerInfo.CCFlags |= HLSLCC_DX11ClipSpace;

	// Required as we added the RemoveUniformBuffersFromSource() function (the cross-compiler won't be able to interpret comments w/o a preprocessor)
	CompilerInfo.CCFlags &= ~HLSLCC_NoPreprocess;

	// Write out the preprocessed file and a batch file to compile it if requested (DumpDebugInfoPath is valid)
	if (CompilerInfo.bDebugDump)
	{
		FString DumpedUSFFile = CompilerInfo.Input.DumpDebugInfoPath / CompilerInfo.BaseSourceFilename;
		FArchive* FileWriter = IFileManager::Get().CreateFileWriter(*DumpedUSFFile);
		if (FileWriter)
		{
			auto AnsiSourceFile = StringCast<ANSICHAR>(*PreprocessedShaderSource);
			FileWriter->Serialize((ANSICHAR*)AnsiSourceFile.Get(), AnsiSourceFile.Length());
			{
				FString Line = CrossCompiler::CreateResourceTableFromEnvironment(Input.Environment);

				Line += TEXT("#if 0 /*DIRECT COMPILE*/\n");
				Line += CreateShaderCompilerWorkerDirectCommandLine(Input);
				Line += TEXT("\n#endif /*DIRECT COMPILE*/\n");

				FileWriter->Serialize(TCHAR_TO_ANSI(*Line), Line.Len());
			}
			FileWriter->Close();
			delete FileWriter;
		}

		const FString BatchFileContents = CreateShaderCompileCommandLine(CompilerInfo, HlslCompilerTarget);
		FFileHelper::SaveStringToFile(BatchFileContents, *(CompilerInfo.Input.DumpDebugInfoPath / TEXT("CompileSPIRV.bat")));

		if (Input.bGenerateDirectCompileFile)
		{
			FFileHelper::SaveStringToFile(CreateShaderCompilerWorkerDirectCommandLine(Input), *(Input.DumpDebugInfoPath / TEXT("DirectCompile.txt")));
		}
	}

	TArray<ANSICHAR> GeneratedGlslSource;
	FVulkanBindingTable BindingTable(CompilerInfo.Frequency);
	if (CallHlslcc(PreprocessedShaderSource, BindingTable, CompilerInfo, EntryPointName, HlslCompilerTarget, Output, GeneratedGlslSource))
	{
		//#todo-rco: Once it's all cleaned up...
		//if (GUseExternalShaderCompiler)
		//{
		//	CompileUsingExternal(CompilerInfo, BindingTable, GeneratedGlslSource, EntryPointName, Output);
		//}
		//else
		{
			// For debugging: if you hit an error from Glslang/Spirv, use the SourceNoHeader for line numbers
			auto* SourceWithHeader = GeneratedGlslSource.GetData();
			char* SourceNoHeader = strstr(SourceWithHeader, "#version");
			bool bSuccess = CompileUsingInternal(CompilerInfo, BindingTable, GeneratedGlslSource, EntryPointName, Output);
			if (Input.bSkipPreprocessedCache)
			{
				FPlatformMisc::LowLevelOutputDebugStringf(TEXT("Success: %d\n%s\n"), bSuccess, ANSI_TO_TCHAR(SourceWithHeader));
			}
		}
	}
	
	if (Input.bSkipPreprocessedCache)
	{
		for (const auto& Error : Output.Errors)
		{
			FPlatformMisc::LowLevelOutputDebugStringf(TEXT("%s\n"), *Error.GetErrorString());
		}
	}
}
