// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	MaterialShared.cpp: Shared material implementation.
=============================================================================*/

#include "Materials/MaterialUniformExpressions.h"
#include "CoreGlobals.h"
#include "SceneManagement.h"
#include "Materials/MaterialInstance.h"
#include "Materials/MaterialInstanceSupport.h"
#include "Materials/MaterialParameterCollection.h"
#include "ExternalTexture.h"
#include "Misc/UObjectToken.h"

#include "RenderCore.h"
#include "VirtualTexturing.h"
#include "VT/RuntimeVirtualTexture.h"

static TAutoConsoleVariable<int32> CVarSupportMaterialLayers(
	TEXT("r.SupportMaterialLayers"),
	0,
	TEXT("Support new material layering in 4.19. Disabling it reduces some overhead in place to support the experimental feature."),
	ECVF_ReadOnly | ECVF_RenderThreadSafe);

// Temporary flag for toggling experimental material layers functionality
bool AreExperimentalMaterialLayersEnabled()
{
	return CVarSupportMaterialLayers.GetValueOnAnyThread() == 1;
}

TLinkedList<FMaterialUniformExpressionType*>*& FMaterialUniformExpressionType::GetTypeList()
{
	static TLinkedList<FMaterialUniformExpressionType*>* TypeList = NULL;
	return TypeList;
}

TMap<FName,FMaterialUniformExpressionType*>& FMaterialUniformExpressionType::GetTypeMap()
{
	static TMap<FName,FMaterialUniformExpressionType*> TypeMap;

	// Move types from the type list to the type map.
	TLinkedList<FMaterialUniformExpressionType*>* TypeListLink = GetTypeList();
	while(TypeListLink)
	{
		TLinkedList<FMaterialUniformExpressionType*>* NextLink = TypeListLink->Next();
		FMaterialUniformExpressionType* Type = **TypeListLink;

		TypeMap.Add(FName(Type->Name),Type);
		TypeListLink->Unlink();
		delete TypeListLink;

		TypeListLink = NextLink;
	}

	return TypeMap;
}

FMaterialUniformExpressionType::FMaterialUniformExpressionType(
	const TCHAR* InName,
	SerializationConstructorType InSerializationConstructor
	):
	Name(InName),
	SerializationConstructor(InSerializationConstructor)
{
	// Put the type in the type list until the name subsystem/type map are initialized.
	(new TLinkedList<FMaterialUniformExpressionType*>(this))->LinkHead(GetTypeList());
}

FArchive& operator<<(FArchive& Ar,FMaterialUniformExpression*& Ref)
{
	// Serialize the expression type.
	if(Ar.IsSaving())
	{
		// Write the type name.
		check(Ref);
		FName TypeName(Ref->GetType()->Name);
		Ar << TypeName;
	}
	else if(Ar.IsLoading())
	{
		// Read the type name.
		FName TypeName = NAME_None;
		Ar << TypeName;

		// Find the expression type with a matching name.
		FMaterialUniformExpressionType* Type = FMaterialUniformExpressionType::GetTypeMap().FindRef(TypeName);
		checkf(Type, TEXT("Unable to find FMaterialUniformExpressionType for TypeName '%s'"), *TypeName.ToString());

		// Construct a new instance of the expression type.
		Ref = (*Type->SerializationConstructor)();
	}

	// Serialize the expression.
	Ref->Serialize(Ar);

	return Ar;
}

FArchive& operator<<(FArchive& Ar,FMaterialUniformExpressionTexture*& Ref)
{
	Ar << (FMaterialUniformExpression*&)Ref;
	return Ar;
}

void FUniformExpressionSet::Serialize(FArchive& Ar)
{
	Ar << UniformVectorExpressions;
	Ar << UniformScalarExpressions;
	Ar << Uniform2DTextureExpressions;
	Ar << UniformCubeTextureExpressions;
	Ar << UniformVolumeTextureExpressions;
	Ar << UniformVirtualTextureExpressions;
	Ar << UniformExternalTextureExpressions;
	Ar << VTStacks;

	// Adding 2D texture array now to prevent bumping version when the feature gets added
	TArray<TRefCountPtr<FMaterialUniformExpressionTexture> > Uniform2DTextureArrayExpressions;
	Ar << Uniform2DTextureArrayExpressions;

	Ar << ParameterCollections;

	// Recreate the uniform buffer struct after loading.
	if(Ar.IsLoading())
	{
		CreateBufferStruct();
	}
}

bool FUniformExpressionSet::IsEmpty() const
{
	return UniformVectorExpressions.Num() == 0
		&& UniformScalarExpressions.Num() == 0
		&& Uniform2DTextureExpressions.Num() == 0
		&& UniformCubeTextureExpressions.Num() == 0
		&& UniformVolumeTextureExpressions.Num() == 0
		&& UniformVirtualTextureExpressions.Num() == 0
		&& UniformExternalTextureExpressions.Num() == 0
		&& VTStacks.Num() == 0
		&& ParameterCollections.Num() == 0;
}

bool FUniformExpressionSet::operator==(const FUniformExpressionSet& ReferenceSet) const
{
	if (UniformVectorExpressions.Num() != ReferenceSet.UniformVectorExpressions.Num()
		|| UniformScalarExpressions.Num() != ReferenceSet.UniformScalarExpressions.Num()
		|| Uniform2DTextureExpressions.Num() != ReferenceSet.Uniform2DTextureExpressions.Num()
		|| UniformCubeTextureExpressions.Num() != ReferenceSet.UniformCubeTextureExpressions.Num()
		|| UniformVolumeTextureExpressions.Num() != ReferenceSet.UniformVolumeTextureExpressions.Num()
		|| UniformVirtualTextureExpressions.Num() != ReferenceSet.UniformVirtualTextureExpressions.Num()
		|| UniformExternalTextureExpressions.Num() != ReferenceSet.UniformExternalTextureExpressions.Num()
		|| VTStacks.Num() != ReferenceSet.VTStacks.Num()
		|| ParameterCollections.Num() != ReferenceSet.ParameterCollections.Num())
	{
		return false;
	}

	for (int32 i = 0; i < UniformVectorExpressions.Num(); i++)
	{
		if (!UniformVectorExpressions[i]->IsIdentical(ReferenceSet.UniformVectorExpressions[i]))
		{
			return false;
		}
	}

	for (int32 i = 0; i < UniformScalarExpressions.Num(); i++)
	{
		if (!UniformScalarExpressions[i]->IsIdentical(ReferenceSet.UniformScalarExpressions[i]))
		{
			return false;
		}
	}

	for (int32 i = 0; i < Uniform2DTextureExpressions.Num(); i++)
	{
		if (!Uniform2DTextureExpressions[i]->IsIdentical(ReferenceSet.Uniform2DTextureExpressions[i]))
		{
			return false;
		}
	}

	for (int32 i = 0; i < UniformCubeTextureExpressions.Num(); i++)
	{
		if (!UniformCubeTextureExpressions[i]->IsIdentical(ReferenceSet.UniformCubeTextureExpressions[i]))
		{
			return false;
		}
	}

	for (int32 i = 0; i < UniformVolumeTextureExpressions.Num(); i++)
	{
		if (!UniformVolumeTextureExpressions[i]->IsIdentical(ReferenceSet.UniformVolumeTextureExpressions[i]))
		{
			return false;
		}
	}

	for (int32 i = 0; i < UniformVirtualTextureExpressions.Num(); i++)
	{
		if (!UniformVirtualTextureExpressions[i]->IsIdentical(ReferenceSet.UniformVirtualTextureExpressions[i]))
		{
			return false;
		}
	}

	for (int32 i = 0; i < UniformExternalTextureExpressions.Num(); i++)
	{
		if (!UniformExternalTextureExpressions[i]->IsIdentical(ReferenceSet.UniformExternalTextureExpressions[i]))
		{
			return false;
		}
	}

	for (int32 i = 0; i < VTStacks.Num(); i++)
	{
		if (VTStacks[i] != ReferenceSet.VTStacks[i])
		{
			return false;
		}
	}

	for (int32 i = 0; i < ParameterCollections.Num(); i++)
	{
		if (ParameterCollections[i] != ReferenceSet.ParameterCollections[i])
		{
			return false;
		}
	}

	return true;
}

FString FUniformExpressionSet::GetSummaryString() const
{
	return FString::Printf(TEXT("(%u vectors, %u scalars, %u 2d tex, %u cube tex, %u 3d tex, %u virtual tex, %u external tex, %u VT stacks, %u collections)"),
		UniformVectorExpressions.Num(), 
		UniformScalarExpressions.Num(),
		Uniform2DTextureExpressions.Num(),
		UniformCubeTextureExpressions.Num(),
		UniformVolumeTextureExpressions.Num(),
		UniformVirtualTextureExpressions.Num(),
		UniformExternalTextureExpressions.Num(),
		VTStacks.Num(),
		ParameterCollections.Num()
		);
}

void FUniformExpressionSet::SetParameterCollections(const TArray<UMaterialParameterCollection*>& InCollections)
{
	ParameterCollections.Empty(InCollections.Num());

	for (int32 CollectionIndex = 0; CollectionIndex < InCollections.Num(); CollectionIndex++)
	{
		ParameterCollections.Add(InCollections[CollectionIndex]->StateId);
	}
}


static FName MaterialLayoutName(TEXT("Material"));

void FUniformExpressionSet::CreateBufferStruct()
{
	// Make sure FUniformExpressionSet::CreateDebugLayout() is in sync
	TArray<FShaderParametersMetadata::FMember> Members;
	uint32 NextMemberOffset = 0;

	if (VTStacks.Num())
	{
		// 2x uint4 per VTStack
		new(Members) FShaderParametersMetadata::FMember(TEXT("VTPackedPageTableUniform"), TEXT(""), NextMemberOffset, UBMT_UINT32, EShaderPrecisionModifier::Float, 1, 4, VTStacks.Num() * 2, NULL);
		NextMemberOffset += VTStacks.Num() * sizeof(FUintVector4) * 2;
	}

	if (UniformVirtualTextureExpressions.Num() > 0)
	{
		// 1x uint4 per Virtual Texture
		new(Members) FShaderParametersMetadata::FMember(TEXT("VTPackedUniform"), TEXT(""), NextMemberOffset, UBMT_UINT32, EShaderPrecisionModifier::Float, 1, 4, UniformVirtualTextureExpressions.Num(), NULL);
		NextMemberOffset += UniformVirtualTextureExpressions.Num() * sizeof(FUintVector4);
	}

	if (UniformVectorExpressions.Num())
	{
		new(Members) FShaderParametersMetadata::FMember(TEXT("VectorExpressions"),TEXT(""),NextMemberOffset,UBMT_FLOAT32,EShaderPrecisionModifier::Half,1,4,UniformVectorExpressions.Num(),NULL);
		const uint32 VectorArraySize = UniformVectorExpressions.Num() * sizeof(FVector4);
		NextMemberOffset += VectorArraySize;
	}

	if (UniformScalarExpressions.Num())
	{
		new(Members) FShaderParametersMetadata::FMember(TEXT("ScalarExpressions"),TEXT(""),NextMemberOffset,UBMT_FLOAT32,EShaderPrecisionModifier::Half,1,4,(UniformScalarExpressions.Num() + 3) / 4,NULL);
		const uint32 ScalarArraySize = (UniformScalarExpressions.Num() + 3) / 4 * sizeof(FVector4);
		NextMemberOffset += ScalarArraySize;
	}

	check((NextMemberOffset % (2 * SHADER_PARAMETER_POINTER_ALIGNMENT)) == 0);

	static FString Texture2DNames[128];
	static FString Texture2DSamplerNames[128];
	static FString TextureCubeNames[128];
	static FString TextureCubeSamplerNames[128];
	static FString VolumeTextureNames[128];
	static FString VolumeTextureSamplerNames[128];
	static FString ExternalTextureNames[128];
	static FString MediaTextureSamplerNames[128];
	static FString VirtualTexturePageTableNames0[128];
	static FString VirtualTexturePageTableNames1[128];
	static FString VirtualTexturePhysicalNames[128];
	static FString VirtualTexturePhysicalSamplerNames[128];
	static bool bInitializedTextureNames = false;
	if (!bInitializedTextureNames)
	{
		bInitializedTextureNames = true;
		for (int32 i = 0; i < 128; ++i)
		{
			Texture2DNames[i] = FString::Printf(TEXT("Texture2D_%d"), i);
			Texture2DSamplerNames[i] = FString::Printf(TEXT("Texture2D_%dSampler"), i);
			TextureCubeNames[i] = FString::Printf(TEXT("TextureCube_%d"), i);
			TextureCubeSamplerNames[i] = FString::Printf(TEXT("TextureCube_%dSampler"), i);
			VolumeTextureNames[i] = FString::Printf(TEXT("VolumeTexture_%d"), i);
			VolumeTextureSamplerNames[i] = FString::Printf(TEXT("VolumeTexture_%dSampler"), i);
			ExternalTextureNames[i] = FString::Printf(TEXT("ExternalTexture_%d"), i);
			MediaTextureSamplerNames[i] = FString::Printf(TEXT("ExternalTexture_%dSampler"), i);
			VirtualTexturePageTableNames0[i] = FString::Printf(TEXT("VirtualTexturePageTable0_%d"), i);
			VirtualTexturePageTableNames1[i] = FString::Printf(TEXT("VirtualTexturePageTable1_%d"), i);
			VirtualTexturePhysicalNames[i] = FString::Printf(TEXT("VirtualTexturePhysicalTable_%d"), i);
			VirtualTexturePhysicalSamplerNames[i] = FString::Printf(TEXT("VirtualTexturePhysicalTable_%dSampler"), i);
		}
	}

	check(Uniform2DTextureExpressions.Num() <= 128);
	check(UniformCubeTextureExpressions.Num() <= 128);
	check(UniformVolumeTextureExpressions.Num() <= 128);
	check(UniformVirtualTextureExpressions.Num() <= 128);
	check(VTStacks.Num() <= 128);

	for (int32 i = 0; i < Uniform2DTextureExpressions.Num(); ++i)
	{
		check((NextMemberOffset % SHADER_PARAMETER_POINTER_ALIGNMENT) == 0);
		new(Members) FShaderParametersMetadata::FMember(*Texture2DNames[i],TEXT("Texture2D"),NextMemberOffset,UBMT_TEXTURE,EShaderPrecisionModifier::Float,1,1,0,NULL);
		NextMemberOffset += SHADER_PARAMETER_POINTER_ALIGNMENT;
		new(Members) FShaderParametersMetadata::FMember(*Texture2DSamplerNames[i],TEXT("SamplerState"),NextMemberOffset,UBMT_SAMPLER,EShaderPrecisionModifier::Float,1,1,0,NULL);
		NextMemberOffset += SHADER_PARAMETER_POINTER_ALIGNMENT;
	}

	for (int32 i = 0; i < UniformCubeTextureExpressions.Num(); ++i)
	{
		check((NextMemberOffset % SHADER_PARAMETER_POINTER_ALIGNMENT) == 0);
		new(Members) FShaderParametersMetadata::FMember(*TextureCubeNames[i],TEXT("TextureCube"),NextMemberOffset,UBMT_TEXTURE,EShaderPrecisionModifier::Float,1,1,0,NULL);
		NextMemberOffset += SHADER_PARAMETER_POINTER_ALIGNMENT;
		new(Members) FShaderParametersMetadata::FMember(*TextureCubeSamplerNames[i],TEXT("SamplerState"),NextMemberOffset,UBMT_SAMPLER,EShaderPrecisionModifier::Float,1,1,0,NULL);
		NextMemberOffset += SHADER_PARAMETER_POINTER_ALIGNMENT;
	}

	for (int32 i = 0; i < UniformVolumeTextureExpressions.Num(); ++i)
	{
		check((NextMemberOffset % SHADER_PARAMETER_POINTER_ALIGNMENT) == 0);
		new(Members) FShaderParametersMetadata::FMember(*VolumeTextureNames[i],TEXT("Texture3D"),NextMemberOffset,UBMT_TEXTURE,EShaderPrecisionModifier::Float,1,1,0,NULL);
		NextMemberOffset += SHADER_PARAMETER_POINTER_ALIGNMENT;
		new(Members) FShaderParametersMetadata::FMember(*VolumeTextureSamplerNames[i],TEXT("SamplerState"),NextMemberOffset,UBMT_SAMPLER,EShaderPrecisionModifier::Float,1,1,0,NULL);
		NextMemberOffset += SHADER_PARAMETER_POINTER_ALIGNMENT;
	}

	for (int32 i = 0; i < UniformExternalTextureExpressions.Num(); ++i)
	{
		check((NextMemberOffset % SHADER_PARAMETER_POINTER_ALIGNMENT) == 0);
		new(Members) FShaderParametersMetadata::FMember(*ExternalTextureNames[i], TEXT("TextureExternal"), NextMemberOffset, UBMT_TEXTURE, EShaderPrecisionModifier::Float, 1, 1, 0, NULL);
		NextMemberOffset += SHADER_PARAMETER_POINTER_ALIGNMENT;
		new(Members) FShaderParametersMetadata::FMember(*MediaTextureSamplerNames[i], TEXT("SamplerState"), NextMemberOffset, UBMT_SAMPLER, EShaderPrecisionModifier::Float, 1, 1, 0, NULL);
		NextMemberOffset += SHADER_PARAMETER_POINTER_ALIGNMENT;
	}

	for (int32 i = 0; i < VTStacks.Num(); ++i)
	{
		const FMaterialVirtualTextureStack& Stack = VTStacks[i];
		check((NextMemberOffset % SHADER_PARAMETER_POINTER_ALIGNMENT) == 0);
		new(Members) FShaderParametersMetadata::FMember(*VirtualTexturePageTableNames0[i], TEXT("Texture2D<uint4>"), NextMemberOffset, UBMT_TEXTURE, EShaderPrecisionModifier::Float, 1, 1, 0, NULL);
		NextMemberOffset += SHADER_PARAMETER_POINTER_ALIGNMENT;
		if (Stack.GetNumLayers() > 4u)
		{
			new(Members) FShaderParametersMetadata::FMember(*VirtualTexturePageTableNames1[i], TEXT("Texture2D<uint4>"), NextMemberOffset, UBMT_TEXTURE, EShaderPrecisionModifier::Float, 1, 1, 0, NULL);
			NextMemberOffset += SHADER_PARAMETER_POINTER_ALIGNMENT;
		}
	}

	for (int32 i = 0; i < UniformVirtualTextureExpressions.Num(); ++i)
	{
		check((NextMemberOffset % SHADER_PARAMETER_POINTER_ALIGNMENT) == 0);

		// VT physical textures are bound as SRV, allows aliasing the same underlying texture with both sRGB/non-sRGB views
		new(Members) FShaderParametersMetadata::FMember(*VirtualTexturePhysicalNames[i], TEXT("Texture2D"), NextMemberOffset, UBMT_SRV, EShaderPrecisionModifier::Float, 1, 1, 0, NULL);
		NextMemberOffset += SHADER_PARAMETER_POINTER_ALIGNMENT;
		new(Members) FShaderParametersMetadata::FMember(*VirtualTexturePhysicalSamplerNames[i], TEXT("SamplerState"), NextMemberOffset, UBMT_SAMPLER, EShaderPrecisionModifier::Float, 1, 1, 0, NULL);
		NextMemberOffset += SHADER_PARAMETER_POINTER_ALIGNMENT;
	}

	new(Members) FShaderParametersMetadata::FMember(TEXT("Wrap_WorldGroupSettings"),TEXT("SamplerState"),NextMemberOffset,UBMT_SAMPLER,EShaderPrecisionModifier::Float,1,1,0,NULL);
	NextMemberOffset += SHADER_PARAMETER_POINTER_ALIGNMENT;

	new(Members) FShaderParametersMetadata::FMember(TEXT("Clamp_WorldGroupSettings"),TEXT("SamplerState"),NextMemberOffset,UBMT_SAMPLER,EShaderPrecisionModifier::Float,1,1,0,NULL);
	NextMemberOffset += SHADER_PARAMETER_POINTER_ALIGNMENT;

	const uint32 StructSize = Align(NextMemberOffset, SHADER_PARAMETER_STRUCT_ALIGNMENT);
	UniformBufferStruct.Emplace(
		FShaderParametersMetadata::EUseCase::DataDrivenShaderParameterStruct,
		MaterialLayoutName,
		TEXT("MaterialUniforms"),
		TEXT("Material"),
		StructSize,
		Members);
}

const FShaderParametersMetadata& FUniformExpressionSet::GetUniformBufferStruct() const
{
	return UniformBufferStruct.GetValue();
}



FUniformExpressionSet::FVTPackedStackAndLayerIndex FUniformExpressionSet::GetVTStackAndLayerIndex(int32 UniformExpressionIndex) const
{
	for (int32 VTStackIndex = 0; VTStackIndex < VTStacks.Num(); ++VTStackIndex)
	{
		const FMaterialVirtualTextureStack& VTStack = VTStacks[VTStackIndex];
		const int32 LayerIndex = VTStack.FindLayer(UniformExpressionIndex);
		if (LayerIndex >= 0)
		{
			return FVTPackedStackAndLayerIndex(VTStackIndex, LayerIndex);
		}
	}

	checkNoEntry();
	return FVTPackedStackAndLayerIndex(0xffff, 0xffff);
}

void FUniformExpressionSet::FillUniformBuffer(const FMaterialRenderContext& MaterialRenderContext, const FUniformExpressionCache& UniformExpressionCache, uint8* TempBuffer, int TempBufferSize) const
{
	check(UniformBufferStruct);
	check(IsInParallelRenderingThread());

	if (UniformBufferStruct->GetSize() > 0)
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_FUniformExpressionSet_FillUniformBuffer);

		void* BufferCursor = TempBuffer;
		check(BufferCursor <= TempBuffer + TempBufferSize);

		// Dump virtual texture per page table uniform data
		check(UniformExpressionCache.AllocatedVTs.Num() == VTStacks.Num());
		for ( int32 VTStackIndex = 0; VTStackIndex < VTStacks.Num(); ++VTStackIndex)
		{
			const IAllocatedVirtualTexture* AllocatedVT = UniformExpressionCache.AllocatedVTs[VTStackIndex];
			FUintVector4* VTPackedPageTableUniform = (FUintVector4*)BufferCursor;
			if (AllocatedVT)
			{
				AllocatedVT->GetPackedPageTableUniform(VTPackedPageTableUniform, true);
			}
			else
			{
				VTPackedPageTableUniform[0] = FUintVector4(ForceInitToZero);
				VTPackedPageTableUniform[1] = FUintVector4(ForceInitToZero);
			}
			BufferCursor = VTPackedPageTableUniform + 2;
		}
		
		// Dump virtual texture per physical texture uniform data
		for (int32 ExpressionIndex = 0; ExpressionIndex < UniformVirtualTextureExpressions.Num(); ++ExpressionIndex)
		{
			FUintVector4* VTPackedUniform = (FUintVector4*)BufferCursor;
			BufferCursor = VTPackedUniform + 1;

			bool bFoundTexture = false;

			// Check for streaming virtual texture
			if (!bFoundTexture)
			{
				const UTexture* Texture = nullptr;
				UniformVirtualTextureExpressions[ExpressionIndex]->GetTextureValue(MaterialRenderContext, MaterialRenderContext.Material, Texture);
				if (Texture != nullptr)
				{
					const FVTPackedStackAndLayerIndex StackAndLayerIndex = GetVTStackAndLayerIndex(ExpressionIndex);
					const IAllocatedVirtualTexture* AllocatedVT = UniformExpressionCache.AllocatedVTs[StackAndLayerIndex.StackIndex];
					if (AllocatedVT)
					{
						AllocatedVT->GetPackedUniform(VTPackedUniform, StackAndLayerIndex.LayerIndex);
					}
					bFoundTexture = true;
				}
			}
			
			// Now check for runtime virtual texture
			if (!bFoundTexture)
			{
				const URuntimeVirtualTexture* Texture = nullptr;
				UniformVirtualTextureExpressions[ExpressionIndex]->GetTextureValue(MaterialRenderContext.Material, Texture);
				if (Texture != nullptr)
				{
					int32 LayerIndex = UniformVirtualTextureExpressions[ExpressionIndex]->GetLayerIndex();
					IAllocatedVirtualTexture const* AllocatedVT = Texture->GetAllocatedVirtualTexture();
					if (AllocatedVT)
					{
						AllocatedVT->GetPackedUniform(VTPackedUniform, LayerIndex);
					}
				}
			}
		}

		// Dump vector expression into the buffer.
		for(int32 VectorIndex = 0;VectorIndex < UniformVectorExpressions.Num();++VectorIndex)
		{
			FLinearColor VectorValue(0, 0, 0, 0);
			UniformVectorExpressions[VectorIndex]->GetNumberValue(MaterialRenderContext, VectorValue);

			FLinearColor* DestAddress = (FLinearColor*)BufferCursor;
			*DestAddress = VectorValue;
			BufferCursor = DestAddress + 1;
			check(BufferCursor <= TempBuffer + TempBufferSize);
		}

		// Dump scalar expression into the buffer.
		for(int32 ScalarIndex = 0;ScalarIndex < UniformScalarExpressions.Num();++ScalarIndex)
		{
			FLinearColor VectorValue(0,0,0,0);
			UniformScalarExpressions[ScalarIndex]->GetNumberValue(MaterialRenderContext,VectorValue);

			float* DestAddress = (float*)BufferCursor;
			*DestAddress = VectorValue.R;
			BufferCursor = DestAddress + 1;
			check(BufferCursor <= TempBuffer + TempBufferSize);
		}

		// Offsets the cursor to next first resource.
		BufferCursor = ((float*)BufferCursor) + ((4 - UniformScalarExpressions.Num() % 4) % 4);
		check(BufferCursor <= TempBuffer + TempBufferSize);

#if DO_CHECK
		{
			uint32 NumPageTableTextures = 0u;
			for (int i = 0; i < VTStacks.Num(); ++i)
			{
				NumPageTableTextures += VTStacks[i].GetNumLayers() > 4u ? 2: 1;
			}
	
			check(UniformBufferStruct->GetLayout().Resources.Num() == 
				Uniform2DTextureExpressions.Num() * 2
				+ UniformCubeTextureExpressions.Num() * 2 
				+ UniformVolumeTextureExpressions.Num() * 2 
				+ UniformExternalTextureExpressions.Num() * 2
				+ UniformVirtualTextureExpressions.Num() * 2
				+ NumPageTableTextures
				+ 2);
		}
#endif // DO_CHECK

		// Cache 2D texture uniform expressions.
		for(int32 ExpressionIndex = 0;ExpressionIndex < Uniform2DTextureExpressions.Num();ExpressionIndex++)
		{
			const UTexture* Value;
			Uniform2DTextureExpressions[ExpressionIndex]->GetTextureValue(MaterialRenderContext,MaterialRenderContext.Material,Value);
			if (Value)
			{
				// Pre-application validity checks (explicit ensures to avoid needless string allocation)
				const FMaterialUniformExpressionTextureParameter* TextureParameter = (Uniform2DTextureExpressions[ExpressionIndex]->GetType() == &FMaterialUniformExpressionTextureParameter::StaticType) ?
					&static_cast<const FMaterialUniformExpressionTextureParameter&>(*Uniform2DTextureExpressions[ExpressionIndex]) : nullptr;

				// gmartin: Trying to locate UE-23902
				if (!Value->IsValidLowLevel())
				{
					ensureMsgf(false, TEXT("Texture not valid! UE-23902! Parameter (%s)"), TextureParameter ? *TextureParameter->GetParameterName().ToString() : TEXT("non-parameter"));
				}

				// Do not allow external textures to be applied to normal texture samplers
				if (Value->GetMaterialType() == MCT_TextureExternal)
				{
					FText MessageText = FText::Format(
						NSLOCTEXT("MaterialExpressions", "IncompatibleExternalTexture", " applied to a non-external Texture2D sampler. This may work by chance on some platforms but is not portable. Please change sampler type to 'External'. Parameter '{0}' (slot {1}) in material '{2}'"),
						FText::FromName(TextureParameter ? TextureParameter->GetParameterName() : FName()),
						ExpressionIndex,
						FText::FromString(*MaterialRenderContext.Material.GetFriendlyName()));

					GLog->Logf(ELogVerbosity::Warning, TEXT("%s"), *MessageText.ToString());
				}
			}

			void** ResourceTableTexturePtr = (void**)((uint8*)BufferCursor + 0 * SHADER_PARAMETER_POINTER_ALIGNMENT);
			void** ResourceTableSamplerPtr = (void**)((uint8*)BufferCursor + 1 * SHADER_PARAMETER_POINTER_ALIGNMENT);
			BufferCursor = ((uint8*)BufferCursor) + (SHADER_PARAMETER_POINTER_ALIGNMENT * 2);
			check(BufferCursor <= TempBuffer + TempBufferSize);

			// ExternalTexture is allowed here, with warning above
			// VirtualTexture is allowed here, as these may be demoted to regular textures on platforms that don't have VT support
			const uint32 ValidTextureTypes = MCT_Texture2D | MCT_TextureVirtual | MCT_TextureExternal;

			// TextureReference.TextureReferenceRHI is cleared from a render command issued by UTexture::BeginDestroy
			// It's possible for this command to trigger before a given material is cleaned up and removed from deferred update list
			// Technically I don't think it's necessary to check 'Resource' for nullptr here, as if TextureReferenceRHI has been initialized, that should be enough
			// Going to leave the check for now though, to hopefully avoid any unexpected problems
			if (Value && Value->Resource && Value->TextureReference.TextureReferenceRHI && (Value->GetMaterialType() & ValidTextureTypes) != 0u)
			{
				//@todo-rco: Help track down a invalid values
				checkf(Value->IsA(UTexture::StaticClass()), TEXT("Expecting a UTexture! Value='%s' class='%s'"), *Value->GetName(), *Value->GetClass()->GetName());

				*ResourceTableTexturePtr = Value->TextureReference.TextureReferenceRHI;
				FSamplerStateRHIRef* SamplerSource = &Value->Resource->SamplerStateRHI;

				ESamplerSourceMode SourceMode = Uniform2DTextureExpressions[ExpressionIndex]->GetSamplerSource();
				if (SourceMode == SSM_Wrap_WorldGroupSettings)
				{
					SamplerSource = &Wrap_WorldGroupSettings->SamplerStateRHI;
				}
				else if (SourceMode == SSM_Clamp_WorldGroupSettings)
				{
					SamplerSource = &Clamp_WorldGroupSettings->SamplerStateRHI;
				}

				checkf(*SamplerSource, TEXT("Texture %s of class %s had invalid sampler source. Material %s with texture expression in slot %i"),
					*Value->GetName(), *Value->GetClass()->GetName(),
					*MaterialRenderContext.Material.GetFriendlyName(), ExpressionIndex);
				*ResourceTableSamplerPtr = *SamplerSource;
			}
			else
			{
				check(GWhiteTexture->TextureRHI);
				*ResourceTableTexturePtr = GWhiteTexture->TextureRHI;
				check(GWhiteTexture->SamplerStateRHI);
				*ResourceTableSamplerPtr = GWhiteTexture->SamplerStateRHI;
			}
		}

		// Cache cube texture uniform expressions.
		for(int32 ExpressionIndex = 0;ExpressionIndex < UniformCubeTextureExpressions.Num();ExpressionIndex++)
		{
			const UTexture* Value;
			UniformCubeTextureExpressions[ExpressionIndex]->GetTextureValue(MaterialRenderContext,MaterialRenderContext.Material,Value);

			void** ResourceTableTexturePtr = (void**)((uint8*)BufferCursor + 0 * SHADER_PARAMETER_POINTER_ALIGNMENT);
			void** ResourceTableSamplerPtr = (void**)((uint8*)BufferCursor + 1 * SHADER_PARAMETER_POINTER_ALIGNMENT);
			BufferCursor = ((uint8*)BufferCursor) + (SHADER_PARAMETER_POINTER_ALIGNMENT * 2);
			check(BufferCursor <= TempBuffer + TempBufferSize);

			if(Value && Value->Resource && (Value->GetMaterialType() & MCT_TextureCube) != 0u)
			{
				check(Value->TextureReference.TextureReferenceRHI);
				*ResourceTableTexturePtr = Value->TextureReference.TextureReferenceRHI;
				FSamplerStateRHIRef* SamplerSource = &Value->Resource->SamplerStateRHI;

				ESamplerSourceMode SourceMode = UniformCubeTextureExpressions[ExpressionIndex]->GetSamplerSource();
				if (SourceMode == SSM_Wrap_WorldGroupSettings)
				{
					SamplerSource = &Wrap_WorldGroupSettings->SamplerStateRHI;
				}
				else if (SourceMode == SSM_Clamp_WorldGroupSettings)
				{
					SamplerSource = &Clamp_WorldGroupSettings->SamplerStateRHI;
				}

				check(*SamplerSource);
				*ResourceTableSamplerPtr = *SamplerSource;
			}
			else
			{
				check(GWhiteTextureCube->TextureRHI);
				*ResourceTableTexturePtr = GWhiteTextureCube->TextureRHI;
				check(GWhiteTextureCube->SamplerStateRHI);
				*ResourceTableSamplerPtr = GWhiteTextureCube->SamplerStateRHI;
			}
		}

		// Cache volume texture uniform expressions.
		for (int32 ExpressionIndex = 0;ExpressionIndex < UniformVolumeTextureExpressions.Num();ExpressionIndex++)
		{
			const UTexture* Value;
			UniformVolumeTextureExpressions[ExpressionIndex]->GetTextureValue(MaterialRenderContext,MaterialRenderContext.Material,Value);

			void** ResourceTableTexturePtr = (void**)((uint8*)BufferCursor + 0 * SHADER_PARAMETER_POINTER_ALIGNMENT);
			void** ResourceTableSamplerPtr = (void**)((uint8*)BufferCursor + 1 * SHADER_PARAMETER_POINTER_ALIGNMENT);
			BufferCursor = ((uint8*)BufferCursor) + (SHADER_PARAMETER_POINTER_ALIGNMENT * 2);
			check(BufferCursor <= TempBuffer + TempBufferSize);

			if(Value && Value->Resource && (Value->GetMaterialType() & MCT_VolumeTexture) != 0u)
			{
				check(Value->TextureReference.TextureReferenceRHI);
				*ResourceTableTexturePtr = Value->TextureReference.TextureReferenceRHI;
				FSamplerStateRHIRef* SamplerSource = &Value->Resource->SamplerStateRHI;

				ESamplerSourceMode SourceMode = UniformVolumeTextureExpressions[ExpressionIndex]->GetSamplerSource();
				if (SourceMode == SSM_Wrap_WorldGroupSettings)
				{
					SamplerSource = &Wrap_WorldGroupSettings->SamplerStateRHI;
				}
				else if (SourceMode == SSM_Clamp_WorldGroupSettings)
				{
					SamplerSource = &Clamp_WorldGroupSettings->SamplerStateRHI;
				}

				check(*SamplerSource);
				*ResourceTableSamplerPtr = *SamplerSource;
			}
			else
			{
				check(GBlackVolumeTexture->TextureRHI);
				*ResourceTableTexturePtr = GBlackVolumeTexture->TextureRHI;
				check(GBlackVolumeTexture->SamplerStateRHI);
				*ResourceTableSamplerPtr = GBlackVolumeTexture->SamplerStateRHI;
			}
		}

		// Cache external texture uniform expressions.
		uint32 ImmutableSamplerIndex = 0;
		FImmutableSamplerState& ImmutableSamplerState = MaterialRenderContext.MaterialRenderProxy->ImmutableSamplerState;
		ImmutableSamplerState.Reset();
		for (int32 ExpressionIndex = 0; ExpressionIndex < UniformExternalTextureExpressions.Num(); ExpressionIndex++)
		{
			FTextureRHIRef TextureRHI;
			FSamplerStateRHIRef SamplerStateRHI;

			void** ResourceTableTexturePtr = (void**)((uint8*)BufferCursor + 0 * SHADER_PARAMETER_POINTER_ALIGNMENT);
			void** ResourceTableSamplerPtr = (void**)((uint8*)BufferCursor + 1 * SHADER_PARAMETER_POINTER_ALIGNMENT);
			BufferCursor = ((uint8*)BufferCursor) + (SHADER_PARAMETER_POINTER_ALIGNMENT * 2);
			check(BufferCursor <= TempBuffer + TempBufferSize);

			if (UniformExternalTextureExpressions[ExpressionIndex]->GetExternalTexture(MaterialRenderContext, TextureRHI, SamplerStateRHI))
			{
				*ResourceTableTexturePtr = TextureRHI;
				*ResourceTableSamplerPtr = SamplerStateRHI;

				if (SamplerStateRHI->IsImmutable())
				{
					ImmutableSamplerState.ImmutableSamplers[ImmutableSamplerIndex++] = SamplerStateRHI;
				}
			}
			else
			{
				check(GWhiteTexture->TextureRHI);
				*ResourceTableTexturePtr = GWhiteTexture->TextureRHI;
				check(GWhiteTexture->SamplerStateRHI);
				*ResourceTableSamplerPtr = GWhiteTexture->SamplerStateRHI;
			}
		}

		// Cache virtual texture page table uniform expressions.
		for (int32 VTStackIndex = 0; VTStackIndex < VTStacks.Num(); ++VTStackIndex)
		{
			void** ResourceTablePageTexture0Ptr = (void**)((uint8*)BufferCursor + 0 * SHADER_PARAMETER_POINTER_ALIGNMENT);
			BufferCursor = ((uint8*)BufferCursor) + SHADER_PARAMETER_POINTER_ALIGNMENT;

			void** ResourceTablePageTexture1Ptr = nullptr;
			if (VTStacks[VTStackIndex].GetNumLayers() > 4u)
			{
				ResourceTablePageTexture1Ptr = (void**)((uint8*)BufferCursor + 0 * SHADER_PARAMETER_POINTER_ALIGNMENT);
				BufferCursor = ((uint8*)BufferCursor) + SHADER_PARAMETER_POINTER_ALIGNMENT;
			}

			const IAllocatedVirtualTexture* AllocatedVT = UniformExpressionCache.AllocatedVTs[VTStackIndex];
			if (AllocatedVT != nullptr)
			{
				FRHITexture* PageTable0RHI = AllocatedVT->GetPageTableTexture(0u);
				ensure(PageTable0RHI);
				*ResourceTablePageTexture0Ptr = PageTable0RHI;

				if (ResourceTablePageTexture1Ptr != nullptr)
				{
					FRHITexture* PageTable1RHI = AllocatedVT->GetPageTableTexture(1u);
					ensure(PageTable1RHI);
					*ResourceTablePageTexture1Ptr = PageTable1RHI;
				}
			}
			else
			{
				// Don't have valid resources to bind for this VT, so make sure something is bound
				*ResourceTablePageTexture0Ptr = GWhiteTexture->TextureRHI;
				if (ResourceTablePageTexture1Ptr != nullptr)
				{
					*ResourceTablePageTexture1Ptr = GWhiteTexture->TextureRHI;
				}
			}
		}

		// Cache virtual texture physical uniform expressions.
		for (int32 ExpressionIndex = 0; ExpressionIndex < UniformVirtualTextureExpressions.Num(); ExpressionIndex++)
		{
			FTextureRHIRef TexturePhysicalRHI;
			FSamplerStateRHIRef PhysicalSamplerStateRHI;

			bool bValidResources = false;
			void** ResourceTablePhysicalTexturePtr = (void**)((uint8*)BufferCursor + 0 * SHADER_PARAMETER_POINTER_ALIGNMENT);
			void** ResourceTablePhysicalSamplerPtr = (void**)((uint8*)BufferCursor + 1 * SHADER_PARAMETER_POINTER_ALIGNMENT);
			BufferCursor = ((uint8*)BufferCursor) + (SHADER_PARAMETER_POINTER_ALIGNMENT * 2);

			// Check for streaming virtual texture
			if (!bValidResources)
			{
				const UTexture* Texture;
				UniformVirtualTextureExpressions[ExpressionIndex]->GetTextureValue(MaterialRenderContext, MaterialRenderContext.Material, Texture);
				if (Texture && Texture->Resource)
				{
					const FVTPackedStackAndLayerIndex StackAndLayerIndex = GetVTStackAndLayerIndex(ExpressionIndex);
					FVirtualTexture2DResource* VTResource = (FVirtualTexture2DResource*)Texture->Resource;
					check(VTResource);

					const IAllocatedVirtualTexture* AllocatedVT = UniformExpressionCache.AllocatedVTs[StackAndLayerIndex.StackIndex];
					if (AllocatedVT != nullptr)
					{
						FRHIShaderResourceView* PhysicalViewRHI = AllocatedVT->GetPhysicalTextureView(StackAndLayerIndex.LayerIndex, VTResource->bSRGB);
						if (PhysicalViewRHI)
						{
							*ResourceTablePhysicalTexturePtr = PhysicalViewRHI;
							*ResourceTablePhysicalSamplerPtr = VTResource->SamplerStateRHI;
							bValidResources = true;
						}
					}
				}
			}
			
			// Now check for runtime virtual texture
			if (!bValidResources)
			{
				const URuntimeVirtualTexture* Texture;
				UniformVirtualTextureExpressions[ExpressionIndex]->GetTextureValue(MaterialRenderContext.Material, Texture);
				if (Texture != nullptr)
				{
					IAllocatedVirtualTexture const* AllocatedVT = Texture->GetAllocatedVirtualTexture();
					if (AllocatedVT != nullptr)
					{
						const int32 LayerIndex = UniformVirtualTextureExpressions[ExpressionIndex]->GetLayerIndex();
						FRHIShaderResourceView* PhysicalViewRHI = AllocatedVT->GetPhysicalTextureView(LayerIndex, Texture->IsLayerSRGB(LayerIndex));
						if (PhysicalViewRHI != nullptr)
						{
							*ResourceTablePhysicalTexturePtr = PhysicalViewRHI;
							*ResourceTablePhysicalSamplerPtr = TStaticSamplerState<SF_AnisotropicPoint, AM_Clamp, AM_Clamp, AM_Clamp, 0, 8>::GetRHI();
							bValidResources = true;
						}
					}
				}
			}
			// Don't have valid resources to bind for this VT, so make sure something is bound
			if (!bValidResources)
			{
				*ResourceTablePhysicalTexturePtr = GBlackTextureWithSRV->ShaderResourceViewRHI;
				*ResourceTablePhysicalSamplerPtr = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp, 0, 8>::GetRHI();
			}
		}

		{
			void** Wrap_WorldGroupSettingsSamplerPtr = (void**)((uint8*)BufferCursor + 0 * SHADER_PARAMETER_POINTER_ALIGNMENT);
			check(Wrap_WorldGroupSettings->SamplerStateRHI);
			*Wrap_WorldGroupSettingsSamplerPtr = Wrap_WorldGroupSettings->SamplerStateRHI;

			void** Clamp_WorldGroupSettingsSamplerPtr = (void**)((uint8*)BufferCursor + 1 * SHADER_PARAMETER_POINTER_ALIGNMENT);
			check(Clamp_WorldGroupSettings->SamplerStateRHI);
			*Clamp_WorldGroupSettingsSamplerPtr = Clamp_WorldGroupSettings->SamplerStateRHI;

			BufferCursor = ((uint8*)BufferCursor) + (SHADER_PARAMETER_POINTER_ALIGNMENT * 2);
			check(BufferCursor <= TempBuffer + TempBufferSize);
		}
	}
}

uint32 FUniformExpressionSet::GetReferencedTexture2DRHIHash(const FMaterialRenderContext& MaterialRenderContext) const
{
	uint32 BaseHash = 0;

	for (int32 ExpressionIndex = 0; ExpressionIndex < Uniform2DTextureExpressions.Num(); ExpressionIndex++)
	{
		const UTexture* Value;
		Uniform2DTextureExpressions[ExpressionIndex]->GetTextureValue(MaterialRenderContext, MaterialRenderContext.Material, Value);

		const uint32 ValidTextureTypes = MCT_Texture2D | MCT_TextureVirtual | MCT_TextureExternal;

		FRHITexture* TexturePtr = nullptr;
		if (Value && Value->Resource && Value->TextureReference.TextureReferenceRHI && (Value->GetMaterialType() & ValidTextureTypes) != 0u)
		{
			TexturePtr = Value->TextureReference.TextureReferenceRHI->GetReferencedTexture();
		}
		BaseHash = PointerHash(TexturePtr, BaseHash);
	}

	return BaseHash;
}

FMaterialUniformExpressionTexture::FMaterialUniformExpressionTexture() :
	TextureIndex(INDEX_NONE),
	LayerIndex(INDEX_NONE),
#if WITH_EDITORONLY_DATA
	SamplerType(SAMPLERTYPE_Color),
#endif
	SamplerSource(SSM_FromTextureAsset),
	bVirtualTexture(false),
	TransientOverrideValue_GameThread(NULL),
	TransientOverrideValue_RenderThread(NULL)
{}

FMaterialUniformExpressionTexture::FMaterialUniformExpressionTexture(int32 InTextureIndex, EMaterialSamplerType InSamplerType, ESamplerSourceMode InSamplerSource, bool InVirtualTexture) :
	TextureIndex(InTextureIndex),
	LayerIndex(INDEX_NONE),
#if WITH_EDITORONLY_DATA
	SamplerType(InSamplerType),
#endif
	SamplerSource(InSamplerSource),
	bVirtualTexture(InVirtualTexture),
	TransientOverrideValue_GameThread(NULL),
	TransientOverrideValue_RenderThread(NULL)
{
}

FMaterialUniformExpressionTexture::FMaterialUniformExpressionTexture(int32 InTextureIndex, int32 InLayerIndex, EMaterialSamplerType InSamplerType)
	: TextureIndex(InTextureIndex)
	, LayerIndex(InLayerIndex)
#if WITH_EDITORONLY_DATA
	, SamplerType(InSamplerType)
#endif
	, SamplerSource(SSM_Wrap_WorldGroupSettings)
	, bVirtualTexture(true)
	, TransientOverrideValue_GameThread(NULL)
	, TransientOverrideValue_RenderThread(NULL)
{
}

void FMaterialUniformExpressionTexture::Serialize(FArchive& Ar)
{
	int32 SamplerSourceInt = (int32)SamplerSource;
	Ar << TextureIndex << LayerIndex << SamplerSourceInt << bVirtualTexture;
	SamplerSource = (ESamplerSourceMode)SamplerSourceInt;
}

void FMaterialUniformExpressionTexture::SetTransientOverrideTextureValue( UTexture* InOverrideTexture )
{
	TransientOverrideValue_GameThread = InOverrideTexture;
	FMaterialUniformExpressionTexture* ExpressionTexture = this;
	ENQUEUE_RENDER_COMMAND(SetTransientOverrideTextureValueCommand)(
		[ExpressionTexture, InOverrideTexture](FRHICommandListImmediate& RHICmdList)
		{
			ExpressionTexture->TransientOverrideValue_RenderThread = InOverrideTexture;
		});
}

void FMaterialUniformExpressionTexture::GetTextureValue(const FMaterialRenderContext& Context, const FMaterial& Material, const UTexture*& OutValue) const
{
	check(IsInParallelRenderingThread());
	if (TransientOverrideValue_RenderThread != NULL)
	{
		OutValue = TransientOverrideValue_RenderThread;
	}
	else
	{
		OutValue = GetIndexedTexture<UTexture>(Material, TextureIndex);
	}
}

void FMaterialUniformExpressionTexture::GetTextureValue(const FMaterial& Material, const URuntimeVirtualTexture*& OutValue) const
{
	check(IsInParallelRenderingThread());
	OutValue = GetIndexedTexture<URuntimeVirtualTexture>(Material, TextureIndex);
}

void FMaterialUniformExpressionTexture::GetGameThreadTextureValue(const UMaterialInterface* MaterialInterface, const FMaterial& Material, UTexture*& OutValue, bool bAllowOverride) const
{
	check(IsInGameThread());
	if (bAllowOverride && TransientOverrideValue_GameThread)
	{
		OutValue = TransientOverrideValue_GameThread;
	}
	else
	{
		OutValue = GetIndexedTexture<UTexture>(Material, TextureIndex);
	}
}

bool FMaterialUniformExpressionTexture::IsIdentical(const FMaterialUniformExpression* OtherExpression) const
{
	if (GetType() != OtherExpression->GetType())
	{
		return false;
	}
	FMaterialUniformExpressionTexture* OtherTextureExpression = (FMaterialUniformExpressionTexture*)OtherExpression;

	return TextureIndex == OtherTextureExpression->TextureIndex && LayerIndex == OtherTextureExpression->LayerIndex && bVirtualTexture == OtherTextureExpression->bVirtualTexture;
}

FMaterialUniformExpressionExternalTextureBase::FMaterialUniformExpressionExternalTextureBase(int32 InSourceTextureIndex)
	: SourceTextureIndex(InSourceTextureIndex)
{}

FMaterialUniformExpressionExternalTextureBase::FMaterialUniformExpressionExternalTextureBase(const FGuid& InGuid)
	: SourceTextureIndex(INDEX_NONE)
	, ExternalTextureGuid(InGuid)
{
}

void FMaterialUniformExpressionExternalTextureBase::Serialize(FArchive& Ar)
{
	Ar << SourceTextureIndex;
	Ar << ExternalTextureGuid;
}

bool FMaterialUniformExpressionExternalTextureBase::IsIdentical(const FMaterialUniformExpression* OtherExpression) const
{
	if (GetType() != OtherExpression->GetType())
	{
		return false;
	}

	const auto* Other = static_cast<const FMaterialUniformExpressionExternalTextureBase*>(OtherExpression);
	return SourceTextureIndex == Other->SourceTextureIndex && ExternalTextureGuid == Other->ExternalTextureGuid;
}

FGuid FMaterialUniformExpressionExternalTextureBase::ResolveExternalTextureGUID(const FMaterialRenderContext& Context, TOptional<FName> ParameterName) const
{
	// Use the compile-time GUID if it is set
	if (ExternalTextureGuid.IsValid())
	{
		return ExternalTextureGuid;
	}

	const UTexture* TextureParameterObject = nullptr;
	if (ParameterName.IsSet() && Context.MaterialRenderProxy && Context.MaterialRenderProxy->GetTextureValue(ParameterName.GetValue(), &TextureParameterObject, Context) && TextureParameterObject)
	{
		return TextureParameterObject->GetExternalTextureGuid();
	}

	// Otherwise attempt to use the texture index in the material, if it's valid
	const UTexture* TextureObject = SourceTextureIndex != INDEX_NONE ? GetIndexedTexture<UTexture>(Context.Material, SourceTextureIndex) : nullptr;
	if (TextureObject)
	{
		return TextureObject->GetExternalTextureGuid();
	}

	return FGuid();
}

bool FMaterialUniformExpressionExternalTexture::GetExternalTexture(const FMaterialRenderContext& Context, FTextureRHIRef& OutTextureRHI, FSamplerStateRHIRef& OutSamplerStateRHI) const
{
	check(IsInParallelRenderingThread());

	FGuid GuidToLookup = ResolveExternalTextureGUID(Context);
	return FExternalTextureRegistry::Get().GetExternalTexture(Context.MaterialRenderProxy, GuidToLookup, OutTextureRHI, OutSamplerStateRHI);
}

FMaterialUniformExpressionExternalTextureParameter::FMaterialUniformExpressionExternalTextureParameter()
{}

FMaterialUniformExpressionExternalTextureParameter::FMaterialUniformExpressionExternalTextureParameter(FName InParameterName, int32 InTextureIndex)
	: Super(InTextureIndex)
	, ParameterName(InParameterName)
{}

void FMaterialUniformExpressionExternalTextureParameter::Serialize(FArchive& Ar)
{
	Ar << ParameterName;
	Super::Serialize(Ar);
}

bool FMaterialUniformExpressionExternalTextureParameter::GetExternalTexture(const FMaterialRenderContext& Context, FTextureRHIRef& OutTextureRHI, FSamplerStateRHIRef& OutSamplerStateRHI) const
{
	check(IsInParallelRenderingThread());

	FGuid GuidToLookup = ResolveExternalTextureGUID(Context, ParameterName);
	return FExternalTextureRegistry::Get().GetExternalTexture(Context.MaterialRenderProxy, GuidToLookup, OutTextureRHI, OutSamplerStateRHI);
}

bool FMaterialUniformExpressionExternalTextureParameter::IsIdentical(const FMaterialUniformExpression* OtherExpression) const
{
	if (GetType() != OtherExpression->GetType())
	{
		return false;
	}

	auto* Other = static_cast<const FMaterialUniformExpressionExternalTextureParameter*>(OtherExpression);
	return ParameterName == Other->ParameterName && Super::IsIdentical(OtherExpression);
}

void FMaterialUniformExpressionVectorParameter::GetGameThreadNumberValue(const UMaterialInterface* SourceMaterialToCopyFrom, FLinearColor& OutValue) const
{
	check(IsInGameThread());
	checkSlow(SourceMaterialToCopyFrom);

	const UMaterialInterface* It = SourceMaterialToCopyFrom;

	for (;;)
	{
		const UMaterialInstance* MatInst = Cast<UMaterialInstance>(It);

		if (MatInst)
		{
			const FVectorParameterValue* ParameterValue = GameThread_FindParameterByName(MatInst->VectorParameterValues, ParameterInfo);
			if(ParameterValue)
			{
				OutValue = ParameterValue->ParameterValue;
				break;
			}

			// go up the hierarchy
			It = MatInst->Parent;
		}
		else
		{
			// we reached the base material
			// get the copy form the base material
			GetDefaultValue(OutValue);
			break;
		}
	}
}

void FMaterialUniformExpressionScalarParameter::GetGameThreadNumberValue(const UMaterialInterface* SourceMaterialToCopyFrom, float& OutValue) const
{
	check(IsInGameThread());
	checkSlow(SourceMaterialToCopyFrom);

	const UMaterialInterface* It = SourceMaterialToCopyFrom;

	for (;;)
	{
		const UMaterialInstance* MatInst = Cast<UMaterialInstance>(It);

		if (MatInst)
		{
			const FScalarParameterValue* ParameterValue = GameThread_FindParameterByName(MatInst->ScalarParameterValues, ParameterInfo);
			if(ParameterValue)
			{
				OutValue = ParameterValue->ParameterValue;
				break;
			}

			// go up the hierarchy
			It = MatInst->Parent;
		}
		else
		{
			// we reached the base material
			// get the copy form the base material
			GetDefaultValue(OutValue);
			break;
		}
	}
}

void FMaterialUniformExpressionScalarParameter::GetGameThreadUsedAsAtlas(const UMaterialInterface* SourceMaterialToCopyFrom, bool& OutValue, TSoftObjectPtr<class UCurveLinearColor>& Curve, TSoftObjectPtr<class UCurveLinearColorAtlas>& Atlas) const
{
	check(IsInGameThread());
	checkSlow(SourceMaterialToCopyFrom);

	const UMaterialInterface* It = SourceMaterialToCopyFrom;

	const UMaterialInstance* MatInst = Cast<UMaterialInstance>(It);

	if (MatInst)
	{
		MatInst->IsScalarParameterUsedAsAtlasPosition(ParameterInfo, OutValue, Curve, Atlas);		
	}
}

namespace
{
	void SerializeOptional(FArchive& Ar, TOptional<FName>& OptionalName)
	{
		bool bIsSet = OptionalName.IsSet();
		Ar << bIsSet;

		if (bIsSet)
		{
			if (!OptionalName.IsSet())
			{
				OptionalName.Emplace();
			}

			Ar << OptionalName.GetValue();
		}
	}
}

void FMaterialUniformExpressionExternalTextureCoordinateScaleRotation::Serialize(FArchive& Ar)
{
	// Write out the optional parameter name
	SerializeOptional(Ar, ParameterName);

	Super::Serialize(Ar);
}

bool FMaterialUniformExpressionExternalTextureCoordinateScaleRotation::IsIdentical(const FMaterialUniformExpression* OtherExpression) const
{
	if (GetType() != OtherExpression->GetType() || !Super::IsIdentical(OtherExpression))
	{
		return false;
	}

	const auto* Other = static_cast<const FMaterialUniformExpressionExternalTextureCoordinateScaleRotation*>(OtherExpression);
	return ParameterName == Other->ParameterName;
}

void FMaterialUniformExpressionExternalTextureCoordinateScaleRotation::GetNumberValue(const FMaterialRenderContext& Context, FLinearColor& OutValue) const
{
	FGuid GuidToLookup = ResolveExternalTextureGUID(Context, ParameterName);
	if (!GuidToLookup.IsValid() || !FExternalTextureRegistry::Get().GetExternalTextureCoordinateScaleRotation(GuidToLookup, OutValue))
	{
		OutValue = FLinearColor(1.f, 0.f, 0.f, 1.f);
	}
}

void FMaterialUniformExpressionExternalTextureCoordinateOffset::Serialize(FArchive& Ar)
{
	// Write out the optional parameter name
	SerializeOptional(Ar, ParameterName);

	Super::Serialize(Ar);
}

bool FMaterialUniformExpressionExternalTextureCoordinateOffset::IsIdentical(const FMaterialUniformExpression* OtherExpression) const
{
	if (GetType() != OtherExpression->GetType() || !Super::IsIdentical(OtherExpression))
	{
		return false;
	}

	const auto* Other = static_cast<const FMaterialUniformExpressionExternalTextureCoordinateOffset*>(OtherExpression);
	return ParameterName == Other->ParameterName;
}

void FMaterialUniformExpressionExternalTextureCoordinateOffset::GetNumberValue(const FMaterialRenderContext& Context, FLinearColor& OutValue) const
{
	FGuid GuidToLookup = ResolveExternalTextureGUID(Context, ParameterName);
	if (!GuidToLookup.IsValid() || !FExternalTextureRegistry::Get().GetExternalTextureCoordinateOffset(GuidToLookup, OutValue))
	{
		OutValue.R = OutValue.G = 0;
		OutValue.B = OutValue.A = 0;
	}
}

FMaterialUniformExpressionRuntimeVirtualTextureParameter::FMaterialUniformExpressionRuntimeVirtualTextureParameter()
	: TextureIndex(INDEX_NONE)
	, ParamIndex(INDEX_NONE)
{
}

FMaterialUniformExpressionRuntimeVirtualTextureParameter::FMaterialUniformExpressionRuntimeVirtualTextureParameter(int32 InTextureIndex, int32 InParamIndex)
	: TextureIndex(InTextureIndex)
	, ParamIndex(InParamIndex)
{
}

void FMaterialUniformExpressionRuntimeVirtualTextureParameter::Serialize(FArchive& Ar)
{
	Ar << TextureIndex;
	Ar << ParamIndex;
}

bool FMaterialUniformExpressionRuntimeVirtualTextureParameter::IsIdentical(const FMaterialUniformExpression* OtherExpression) const
{
	if (GetType() != OtherExpression->GetType())
	{
		return false;
	}

	const auto* Other = static_cast<const FMaterialUniformExpressionRuntimeVirtualTextureParameter*>(OtherExpression);
	return TextureIndex == Other->TextureIndex && ParamIndex == Other->ParamIndex;
}

void FMaterialUniformExpressionRuntimeVirtualTextureParameter::GetNumberValue(const struct FMaterialRenderContext& Context, FLinearColor& OutValue) const
{
	URuntimeVirtualTexture* Texture = GetIndexedTexture<URuntimeVirtualTexture>(Context.Material, TextureIndex);
	if (Texture != nullptr && ParamIndex != INDEX_NONE)
	{
		OutValue = FLinearColor(Texture->GetUniformParameter(ParamIndex));
	}
	else
	{
		OutValue = FLinearColor(0.f, 0.f, 0.f, 0.f);
	}
}


IMPLEMENT_MATERIALUNIFORMEXPRESSION_TYPE(FMaterialUniformExpressionTexture);
IMPLEMENT_MATERIALUNIFORMEXPRESSION_TYPE(FMaterialUniformExpressionConstant);
IMPLEMENT_MATERIALUNIFORMEXPRESSION_TYPE(FMaterialUniformExpressionVectorParameter);
IMPLEMENT_MATERIALUNIFORMEXPRESSION_TYPE(FMaterialUniformExpressionScalarParameter);
IMPLEMENT_MATERIALUNIFORMEXPRESSION_TYPE(FMaterialUniformExpressionTextureParameter);
IMPLEMENT_MATERIALUNIFORMEXPRESSION_TYPE(FMaterialUniformExpressionExternalTextureBase);
IMPLEMENT_MATERIALUNIFORMEXPRESSION_TYPE(FMaterialUniformExpressionExternalTexture);
IMPLEMENT_MATERIALUNIFORMEXPRESSION_TYPE(FMaterialUniformExpressionExternalTextureParameter);
IMPLEMENT_MATERIALUNIFORMEXPRESSION_TYPE(FMaterialUniformExpressionExternalTextureCoordinateScaleRotation);
IMPLEMENT_MATERIALUNIFORMEXPRESSION_TYPE(FMaterialUniformExpressionExternalTextureCoordinateOffset);
IMPLEMENT_MATERIALUNIFORMEXPRESSION_TYPE(FMaterialUniformExpressionRuntimeVirtualTextureParameter);
IMPLEMENT_MATERIALUNIFORMEXPRESSION_TYPE(FMaterialUniformExpressionFlipBookTextureParameter);
IMPLEMENT_MATERIALUNIFORMEXPRESSION_TYPE(FMaterialUniformExpressionSine);
IMPLEMENT_MATERIALUNIFORMEXPRESSION_TYPE(FMaterialUniformExpressionSquareRoot);
IMPLEMENT_MATERIALUNIFORMEXPRESSION_TYPE(FMaterialUniformExpressionLength);
IMPLEMENT_MATERIALUNIFORMEXPRESSION_TYPE(FMaterialUniformExpressionLogarithm2);
IMPLEMENT_MATERIALUNIFORMEXPRESSION_TYPE(FMaterialUniformExpressionLogarithm10);
IMPLEMENT_MATERIALUNIFORMEXPRESSION_TYPE(FMaterialUniformExpressionFoldedMath);
IMPLEMENT_MATERIALUNIFORMEXPRESSION_TYPE(FMaterialUniformExpressionPeriodic);
IMPLEMENT_MATERIALUNIFORMEXPRESSION_TYPE(FMaterialUniformExpressionAppendVector);
IMPLEMENT_MATERIALUNIFORMEXPRESSION_TYPE(FMaterialUniformExpressionMin);
IMPLEMENT_MATERIALUNIFORMEXPRESSION_TYPE(FMaterialUniformExpressionMax);
IMPLEMENT_MATERIALUNIFORMEXPRESSION_TYPE(FMaterialUniformExpressionClamp);
IMPLEMENT_MATERIALUNIFORMEXPRESSION_TYPE(FMaterialUniformExpressionSaturate);
IMPLEMENT_MATERIALUNIFORMEXPRESSION_TYPE(FMaterialUniformExpressionComponentSwizzle);
IMPLEMENT_MATERIALUNIFORMEXPRESSION_TYPE(FMaterialUniformExpressionFloor);
IMPLEMENT_MATERIALUNIFORMEXPRESSION_TYPE(FMaterialUniformExpressionCeil);
IMPLEMENT_MATERIALUNIFORMEXPRESSION_TYPE(FMaterialUniformExpressionFrac);
IMPLEMENT_MATERIALUNIFORMEXPRESSION_TYPE(FMaterialUniformExpressionFmod);
IMPLEMENT_MATERIALUNIFORMEXPRESSION_TYPE(FMaterialUniformExpressionAbs);
IMPLEMENT_MATERIALUNIFORMEXPRESSION_TYPE(FMaterialUniformExpressionTextureProperty);
IMPLEMENT_MATERIALUNIFORMEXPRESSION_TYPE(FMaterialUniformExpressionTrigMath);
IMPLEMENT_MATERIALUNIFORMEXPRESSION_TYPE(FMaterialUniformExpressionRound);
IMPLEMENT_MATERIALUNIFORMEXPRESSION_TYPE(FMaterialUniformExpressionTruncate);
IMPLEMENT_MATERIALUNIFORMEXPRESSION_TYPE(FMaterialUniformExpressionSign);
