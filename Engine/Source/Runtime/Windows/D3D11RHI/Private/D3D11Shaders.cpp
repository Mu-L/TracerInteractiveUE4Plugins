// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	D3D11Shaders.cpp: D3D shader RHI implementation.
=============================================================================*/

#include "D3D11RHIPrivate.h"
#include "Serialization/MemoryReader.h"

#if !PLATFORM_HOLOLENS
#include "nvapi.h"
#endif

template <typename TShaderType>
static inline void ReadShaderOptionalData(FShaderCodeReader& InShaderCode, TShaderType& OutShader)
{
	auto PackedResourceCounts = InShaderCode.FindOptionalData<FShaderCodePackedResourceCounts>();
	check(PackedResourceCounts);
	OutShader.OutputMask = PackedResourceCounts->OutputMask;
	
	uint32 UAVMask = 0;
	uint32 MinOffset = OutShader.ShaderResourceTable.UnorderedAccessViewMap.Num();
	// If the token stream isn't empty, it has a length of at least 2, because it's always terminated with 0xffffffff. If it has
	// a length of 2, it means it only contains an offset entry for a single uniform buffer, which must be 0, because there's nothing
	// for it to offset into. Therefore, we only care about streams which have 3 or more tokens (including the terminator).
	if (MinOffset > 2)
	{
		// Ignore the terminator.
		--MinOffset;

		// The token stream starts with a table of offsets, followed by a list of UAV bindings, like this:
		//		O1 O2 O3 ... On B1 B2 B3 ... Bm
		// The offsets indicate where to find the bindings for each active uniform buffer, relative to the start of the
		// data (not the start of the binding list). Therefore, this buffer:
		//		9 0 7 6 0 0 B1 B2 B3 B4
		// means that:
		//		buffer 0 starts at index 9 (B4)
		//		buffers 1, 4 and 5 are empty (offsets are 0)
		//		buffer 2 starts at index 7 (B2)
		//		buffer 3 starts at index 6 (B1)
		// We can also infer that buffer 2 has two elements in this case (B2 and B3). Since we don't know how many buffers there are,
		// we will parse the data like this:
		//		* read an offset from the start of the list
		//		* process every binding from that offset until the end, since the bindings list is contiguous
		//		* read the next offset, process bindings from that until the first biding we've already processed
		//		* stop when the next offset is at a location we've already processed as a binding (we've found the first binding in that case).
		for (uint32 BufferIdx = 0; BufferIdx < MinOffset; ++BufferIdx)
		{
			uint32 BufferOffset = OutShader.ShaderResourceTable.UnorderedAccessViewMap[BufferIdx];
			if (BufferOffset == 0 || BufferOffset >= MinOffset)
			{
				continue;
			}

			for (uint32 ElemIdx = BufferOffset; ElemIdx < MinOffset; ++ElemIdx)
			{
				uint32 UAVBinding = OutShader.ShaderResourceTable.UnorderedAccessViewMap[ElemIdx];
				const uint8 BindIndex = FRHIResourceTableEntry::GetBindIndex(UAVBinding);
				UAVMask |= (1 << BindIndex);
			}

			MinOffset = BufferOffset;
		}
	}

	OutShader.UAVMask = UAVMask;

	OutShader.bShaderNeedsGlobalConstantBuffer = PackedResourceCounts->bGlobalUniformBufferUsed;
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	OutShader.ShaderName = InShaderCode.FindOptionalData('n');

	int32 UniformBufferTableSize = 0;
	auto* UniformBufferData = InShaderCode.FindOptionalDataAndSize('u', UniformBufferTableSize);
	if (UniformBufferData && UniformBufferTableSize > 0)
	{
		FBufferReader UBReader((void*)UniformBufferData, UniformBufferTableSize, false);
		TArray<FString> Names;
		UBReader << Names;
		check(OutShader.UniformBuffers.Num() == 0);
		for (int32 Index = 0; Index < Names.Num(); ++Index)
		{
			OutShader.UniformBuffers.Add(FName(*Names[Index]));
		}
	}
#endif
	int32 VendorExtensionTableSize = 0;
	auto* VendorExtensionData = InShaderCode.FindOptionalDataAndSize(FShaderCodeVendorExtension::Key, VendorExtensionTableSize);
	if (VendorExtensionData && VendorExtensionTableSize > 0)
	{
		FBufferReader Ar((void*)VendorExtensionData, VendorExtensionTableSize, false);
		Ar << OutShader.VendorExtensions;
	}
	OutShader.bShaderNeedsGlobalConstantBuffer = PackedResourceCounts->bGlobalUniformBufferUsed;

	int32 IsSm6ShaderSize = 1;
	const uint8* IsSm6Shader = InShaderCode.FindOptionalData('6', IsSm6ShaderSize);
	OutShader.bIsSm6Shader = IsSm6Shader && IsSm6ShaderSize && *IsSm6Shader;
}

static bool ApplyVendorExtensions(ID3D11Device* Direct3DDevice, EShaderFrequency Frequency, const TArray<FShaderCodeVendorExtension>& VendorExtensions)
{
	bool IsValidHardwareExtension = true;
#if !PLATFORM_HOLOLENS
	for (int32 ExtensionIndex = 0; ExtensionIndex < VendorExtensions.Num(); ++ExtensionIndex)
	{
		const FShaderCodeVendorExtension& Extension = VendorExtensions[ExtensionIndex];
		if (Extension.VendorId == 0x10DE) // NVIDIA
		{
			if (!IsRHIDeviceNVIDIA())
			{
				IsValidHardwareExtension = false;
				break;
			}

			// https://developer.nvidia.com/unlocking-gpu-intrinsics-hlsl
			if (Extension.Parameter.Type == EShaderParameterType::UAV)
			{
				NvAPI_D3D11_SetNvShaderExtnSlot(Direct3DDevice, Extension.Parameter.BaseIndex);
			}
		}
		else if (Extension.VendorId == 0x1002) // AMD
		{
			if (!IsRHIDeviceAMD())
			{
				IsValidHardwareExtension = false;
				break;
			}
			// TODO: https://github.com/GPUOpen-LibrariesAndSDKs/AGS_SDK/blob/master/ags_lib/hlsl/ags_shader_intrinsics_dx11.hlsl
		}
		else if (Extension.VendorId == 0x8086) // Intel
		{
			if (!IsRHIDeviceIntel())
			{
				IsValidHardwareExtension = false;
				break;
			}
			// TODO: https://github.com/intel/intel-graphics-compiler/blob/master/inc/IntelExtensions.hlsl
		}
	}
#endif
	return IsValidHardwareExtension;
}

static void ResetVendorExtensions(ID3D11Device* Direct3DDevice, EShaderFrequency Frequency, const TArray<FShaderCodeVendorExtension>& VendorExtensions)
{
#if !PLATFORM_HOLOLENS
	for (int32 ExtensionIndex = 0; ExtensionIndex < VendorExtensions.Num(); ++ExtensionIndex)
	{
		const FShaderCodeVendorExtension& Extension = VendorExtensions[ExtensionIndex];
		if (Extension.VendorId == 0x10DE)
		{
			if (Extension.Parameter.Type == EShaderParameterType::UAV)
			{
				NvAPI_D3D11_SetNvShaderExtnSlot(Direct3DDevice, ~uint32(0));
			}
		}
		else if (Extension.VendorId == 0x1002) // AMD
		{
		}
		else if (Extension.VendorId == 0x8086) // Intel
		{
		}
	}
#endif
}

template <typename TShaderType>
static inline void InitUniformBufferStaticSlots(TShaderType* Shader)
{
	const FBaseShaderResourceTable& SRT = Shader->ShaderResourceTable;

	Shader->StaticSlots.Reserve(SRT.ResourceTableLayoutHashes.Num());

	for (uint32 LayoutHash : SRT.ResourceTableLayoutHashes)
	{
		if (const FShaderParametersMetadata* Metadata = FindUniformBufferStructByLayoutHash(LayoutHash))
		{
			Shader->StaticSlots.Add(Metadata->GetLayout().StaticSlot);
		}
		else
		{
			Shader->StaticSlots.Add(MAX_UNIFORM_BUFFER_STATIC_SLOTS);
		}
	}
}

FVertexShaderRHIRef FD3D11DynamicRHI::RHICreateVertexShader(TArrayView<const uint8> Code, const FSHAHash& Hash)
{
	FShaderCodeReader ShaderCode(Code);

	FD3D11VertexShader* Shader = new FD3D11VertexShader;

	FMemoryReaderView Ar( Code, true );
	Ar << Shader->ShaderResourceTable;
	int32 Offset = Ar.Tell();
	const uint8* CodePtr = Code.GetData() + Offset;
	const size_t CodeSize = ShaderCode.GetActualShaderCodeSize() - Offset;

	ReadShaderOptionalData(ShaderCode, *Shader);
	if (!Shader->bIsSm6Shader && ApplyVendorExtensions(Direct3DDevice, SF_Vertex, Shader->VendorExtensions))
	{
		VERIFYD3D11SHADERRESULT(Direct3DDevice->CreateVertexShader((void*)CodePtr, CodeSize, nullptr, Shader->Resource.GetInitReference()), Shader, Direct3DDevice);
		ResetVendorExtensions(Direct3DDevice, SF_Vertex, Shader->VendorExtensions);
		InitUniformBufferStaticSlots(Shader);
	}
	
	// TEMP
	Shader->Code = Code;
	Shader->Offset = Offset;

	return Shader;
}

FVertexShaderRHIRef FD3D11DynamicRHI::CreateVertexShader_RenderThread(
	class FRHICommandListImmediate& RHICmdList,
	TArrayView<const uint8> Code, const FSHAHash& Hash)
{
	return RHICreateVertexShader(Code, Hash);
}

FGeometryShaderRHIRef FD3D11DynamicRHI::RHICreateGeometryShader(TArrayView<const uint8> Code, const FSHAHash& Hash)
{ 
	FShaderCodeReader ShaderCode(Code);

	FD3D11GeometryShader* Shader = new FD3D11GeometryShader;

	FMemoryReaderView Ar( Code, true );
	Ar << Shader->ShaderResourceTable;
	int32 Offset = Ar.Tell();
	const uint8* CodePtr = Code.GetData() + Offset;
	const size_t CodeSize = ShaderCode.GetActualShaderCodeSize() - Offset;

	ReadShaderOptionalData(ShaderCode, *Shader);
	if (!Shader->bIsSm6Shader && ApplyVendorExtensions(Direct3DDevice, SF_Geometry, Shader->VendorExtensions))
	{
		VERIFYD3D11SHADERRESULT(Direct3DDevice->CreateGeometryShader((void*)CodePtr, CodeSize, nullptr, Shader->Resource.GetInitReference()), Shader, Direct3DDevice);
		ResetVendorExtensions(Direct3DDevice, SF_Geometry, Shader->VendorExtensions);
		InitUniformBufferStaticSlots(Shader);
	}

	return Shader;
}

FGeometryShaderRHIRef FD3D11DynamicRHI::CreateGeometryShader_RenderThread(
	class FRHICommandListImmediate& RHICmdList,
	TArrayView<const uint8> Code, const FSHAHash& Hash)
{
	return RHICreateGeometryShader(Code, Hash);
}

FHullShaderRHIRef FD3D11DynamicRHI::RHICreateHullShader(TArrayView<const uint8> Code, const FSHAHash& Hash)
{ 
	FShaderCodeReader ShaderCode(Code);

	FD3D11HullShader* Shader = new FD3D11HullShader;

	FMemoryReaderView Ar( Code, true );
	Ar << Shader->ShaderResourceTable;
	int32 Offset = Ar.Tell();
	const uint8* CodePtr = Code.GetData() + Offset;
	const size_t CodeSize = ShaderCode.GetActualShaderCodeSize() - Offset;

	ReadShaderOptionalData(ShaderCode, *Shader);
	if (!Shader->bIsSm6Shader && ApplyVendorExtensions(Direct3DDevice, SF_Hull, Shader->VendorExtensions))
	{
		VERIFYD3D11SHADERRESULT(Direct3DDevice->CreateHullShader((void*)CodePtr, CodeSize, nullptr, Shader->Resource.GetInitReference()), Shader, Direct3DDevice);
		ResetVendorExtensions(Direct3DDevice, SF_Hull, Shader->VendorExtensions);
		InitUniformBufferStaticSlots(Shader);
	}

	return Shader;
}

FHullShaderRHIRef FD3D11DynamicRHI::CreateHullShader_RenderThread(
	class FRHICommandListImmediate& RHICmdList,
	TArrayView<const uint8> Code, const FSHAHash& Hash)
{
	return RHICreateHullShader(Code, Hash);
}

FDomainShaderRHIRef FD3D11DynamicRHI::RHICreateDomainShader(TArrayView<const uint8> Code, const FSHAHash& Hash)
{ 
	FShaderCodeReader ShaderCode(Code);

	FD3D11DomainShader* Shader = new FD3D11DomainShader;

	FMemoryReaderView Ar(Code, true);
	Ar << Shader->ShaderResourceTable;
	int32 Offset = Ar.Tell();
	const uint8* CodePtr = Code.GetData() + Offset;
	const size_t CodeSize = ShaderCode.GetActualShaderCodeSize() - Offset;

	ReadShaderOptionalData(ShaderCode, *Shader);
	if (!Shader->bIsSm6Shader && ApplyVendorExtensions(Direct3DDevice, SF_Domain, Shader->VendorExtensions))
	{
		VERIFYD3D11SHADERRESULT(Direct3DDevice->CreateDomainShader((void*)CodePtr, CodeSize, nullptr, Shader->Resource.GetInitReference()), Shader, Direct3DDevice);
		ResetVendorExtensions(Direct3DDevice, SF_Domain, Shader->VendorExtensions);
		InitUniformBufferStaticSlots(Shader);
	}

	return Shader;
}

FDomainShaderRHIRef FD3D11DynamicRHI::CreateDomainShader_RenderThread(
	class FRHICommandListImmediate& RHICmdList,
	TArrayView<const uint8> Code, const FSHAHash& Hash)
{
	return RHICreateDomainShader(Code, Hash);
}

FPixelShaderRHIRef FD3D11DynamicRHI::RHICreatePixelShader(TArrayView<const uint8> Code, const FSHAHash& Hash)
{
	FShaderCodeReader ShaderCode(Code);

	FD3D11PixelShader* Shader = new FD3D11PixelShader;

	FMemoryReaderView Ar( Code, true );
	Ar << Shader->ShaderResourceTable;
	int32 Offset = Ar.Tell();
	const uint8* CodePtr = Code.GetData() + Offset;
	const size_t CodeSize = ShaderCode.GetActualShaderCodeSize() - Offset;

	ReadShaderOptionalData(ShaderCode, *Shader);
	if (!Shader->bIsSm6Shader && ApplyVendorExtensions(Direct3DDevice, SF_Pixel, Shader->VendorExtensions))
	{
		VERIFYD3D11SHADERRESULT(Direct3DDevice->CreatePixelShader((void*)CodePtr, CodeSize, nullptr, Shader->Resource.GetInitReference()), Shader, Direct3DDevice);
		ResetVendorExtensions(Direct3DDevice, SF_Pixel, Shader->VendorExtensions);
		InitUniformBufferStaticSlots(Shader);
	}

	return Shader;
}

FPixelShaderRHIRef FD3D11DynamicRHI::CreatePixelShader_RenderThread(
	class FRHICommandListImmediate& RHICmdList,
	TArrayView<const uint8> Code, const FSHAHash& Hash)
{
	return RHICreatePixelShader(Code, Hash);
}

FComputeShaderRHIRef FD3D11DynamicRHI::RHICreateComputeShader(TArrayView<const uint8> Code, const FSHAHash& Hash)
{ 
	FShaderCodeReader ShaderCode(Code);

	FD3D11ComputeShader* Shader = new FD3D11ComputeShader;

	FMemoryReaderView Ar( Code, true );
	Ar << Shader->ShaderResourceTable;
	int32 Offset = Ar.Tell();
	const uint8* CodePtr = Code.GetData() + Offset;
	const size_t CodeSize = ShaderCode.GetActualShaderCodeSize() - Offset;

	ReadShaderOptionalData(ShaderCode, *Shader);
	if (!Shader->bIsSm6Shader && ApplyVendorExtensions(Direct3DDevice, SF_Compute, Shader->VendorExtensions))
	{
		VERIFYD3D11SHADERRESULT(Direct3DDevice->CreateComputeShader((void*)CodePtr, CodeSize, nullptr, Shader->Resource.GetInitReference()), Shader, Direct3DDevice);
		ResetVendorExtensions(Direct3DDevice, SF_Compute, Shader->VendorExtensions);
		InitUniformBufferStaticSlots(Shader);
	}

	return Shader;
}

FComputeShaderRHIRef FD3D11DynamicRHI::CreateComputeShader_RenderThread(
	class FRHICommandListImmediate& RHICmdList,
	TArrayView<const uint8> Code, const FSHAHash& Hash)
{
	return RHICreateComputeShader(Code, Hash);
}

void FD3D11DynamicRHI::RHISetMultipleViewports(uint32 Count, const FViewportBounds* Data) 
{ 
	check(Count > 0);
	check(Data);

	// structures are chosen to be directly mappable
	D3D11_VIEWPORT* D3DData = (D3D11_VIEWPORT*)Data;

	StateCache.SetViewports(Count, D3DData);
}

FD3D11BoundShaderState::FD3D11BoundShaderState(
	FRHIVertexDeclaration* InVertexDeclarationRHI,
	FRHIVertexShader* InVertexShaderRHI,
	FRHIPixelShader* InPixelShaderRHI,
	FRHIHullShader* InHullShaderRHI,
	FRHIDomainShader* InDomainShaderRHI,
	FRHIGeometryShader* InGeometryShaderRHI,
	ID3D11Device* Direct3DDevice
	):
	CacheLink(InVertexDeclarationRHI,InVertexShaderRHI,InPixelShaderRHI,InHullShaderRHI,InDomainShaderRHI,InGeometryShaderRHI,this)
{
	INC_DWORD_STAT(STAT_D3D11NumBoundShaderState);

	FD3D11VertexDeclaration* InVertexDeclaration = FD3D11DynamicRHI::ResourceCast(InVertexDeclarationRHI);
	FD3D11VertexShader* InVertexShader = FD3D11DynamicRHI::ResourceCast(InVertexShaderRHI);
	FD3D11PixelShader* InPixelShader = FD3D11DynamicRHI::ResourceCast(InPixelShaderRHI);
	FD3D11HullShader* InHullShader = FD3D11DynamicRHI::ResourceCast(InHullShaderRHI);
	FD3D11DomainShader* InDomainShader = FD3D11DynamicRHI::ResourceCast(InDomainShaderRHI);
	FD3D11GeometryShader* InGeometryShader = FD3D11DynamicRHI::ResourceCast(InGeometryShaderRHI);

	// Create an input layout for this combination of vertex declaration and vertex shader.
	D3D11_INPUT_ELEMENT_DESC NullInputElement;
	FMemory::Memzero(&NullInputElement,sizeof(D3D11_INPUT_ELEMENT_DESC));

	FShaderCodeReader VertexShaderCode(InVertexShader->Code);

	if (InVertexDeclaration == nullptr)
	{
		InputLayout = nullptr;
	}
	else
	{
		FMemory::Memcpy(StreamStrides, InVertexDeclaration->StreamStrides, sizeof(StreamStrides));

		VERIFYD3D11RESULT_EX(
		Direct3DDevice->CreateInputLayout(
			InVertexDeclaration && InVertexDeclaration->VertexElements.Num() ? InVertexDeclaration->VertexElements.GetData() : &NullInputElement,
			InVertexDeclaration ? InVertexDeclaration->VertexElements.Num() : 0,
			&InVertexShader->Code[ InVertexShader->Offset ],			// TEMP ugly
			VertexShaderCode.GetActualShaderCodeSize() - InVertexShader->Offset,
			InputLayout.GetInitReference()
			),
		Direct3DDevice
		);
	}

	VertexShader = InVertexShader->Resource;
	PixelShader = InPixelShader ? InPixelShader->Resource : nullptr;
	HullShader = InHullShader ? InHullShader->Resource : nullptr;
	DomainShader = InDomainShader ? InDomainShader->Resource : nullptr;
	GeometryShader = InGeometryShader ? InGeometryShader->Resource : nullptr;

	FMemory::Memzero(&bShaderNeedsGlobalConstantBuffer,sizeof(bShaderNeedsGlobalConstantBuffer));

	bShaderNeedsGlobalConstantBuffer[SF_Vertex] = InVertexShader->bShaderNeedsGlobalConstantBuffer;
	bShaderNeedsGlobalConstantBuffer[SF_Hull] = InHullShader ? InHullShader->bShaderNeedsGlobalConstantBuffer : false;
	bShaderNeedsGlobalConstantBuffer[SF_Domain] = InDomainShader ? InDomainShader->bShaderNeedsGlobalConstantBuffer : false;
	bShaderNeedsGlobalConstantBuffer[SF_Pixel] = InPixelShader ? InPixelShader->bShaderNeedsGlobalConstantBuffer : false;
	bShaderNeedsGlobalConstantBuffer[SF_Geometry] = InGeometryShader ? InGeometryShader->bShaderNeedsGlobalConstantBuffer : false;

	static_assert(UE_ARRAY_COUNT(bShaderNeedsGlobalConstantBuffer) == SF_NumStandardFrequencies, "EShaderFrequency size should match with array count of bShaderNeedsGlobalConstantBuffer.");
}

FD3D11BoundShaderState::~FD3D11BoundShaderState()
{
	DEC_DWORD_STAT(STAT_D3D11NumBoundShaderState);
}

/**
* Creates a bound shader state instance which encapsulates a decl, vertex shader, and pixel shader
* @param VertexDeclaration - existing vertex decl
* @param StreamStrides - optional stream strides
* @param VertexShader - existing vertex shader
* @param HullShader - existing hull shader
* @param DomainShader - existing domain shader
* @param PixelShader - existing pixel shader
* @param GeometryShader - existing geometry shader
*/
FBoundShaderStateRHIRef FD3D11DynamicRHI::RHICreateBoundShaderState(
	FRHIVertexDeclaration* VertexDeclarationRHI,
	FRHIVertexShader* VertexShaderRHI,
	FRHIHullShader* HullShaderRHI,
	FRHIDomainShader* DomainShaderRHI,
	FRHIPixelShader* PixelShaderRHI,
	FRHIGeometryShader* GeometryShaderRHI
	)
{
	check(IsInRenderingThread() || IsInRHIThread());

	SCOPE_CYCLE_COUNTER(STAT_D3D11CreateBoundShaderStateTime);

	checkf(GIsRHIInitialized && Direct3DDeviceIMContext,(TEXT("Bound shader state RHI resource was created without initializing Direct3D first")));

	// Check for an existing bound shader state which matches the parameters
	FCachedBoundShaderStateLink* CachedBoundShaderStateLink = GetCachedBoundShaderState(
		VertexDeclarationRHI,
		VertexShaderRHI,
		PixelShaderRHI,
		HullShaderRHI,
		DomainShaderRHI,
		GeometryShaderRHI
		);
	if(CachedBoundShaderStateLink)
	{
		// If we've already created a bound shader state with these parameters, reuse it.
		return CachedBoundShaderStateLink->BoundShaderState;
	}
	else
	{
		SCOPE_CYCLE_COUNTER(STAT_D3D11NewBoundShaderStateTime);
		return new FD3D11BoundShaderState(VertexDeclarationRHI,VertexShaderRHI,PixelShaderRHI,HullShaderRHI,DomainShaderRHI,GeometryShaderRHI,Direct3DDevice);
	}
}
