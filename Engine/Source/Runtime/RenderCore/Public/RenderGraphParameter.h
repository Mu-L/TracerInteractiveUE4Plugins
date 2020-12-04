// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RenderGraphDefinitions.h"

/** A helper class for identifying and accessing a render graph pass parameter. */
class FRDGParameter final
{
public:
	FRDGParameter() = default;

	bool IsResource() const
	{
		return !IsRenderTargetBindingSlots();
	}

	bool IsSRV() const
	{
		return MemberType == UBMT_RDG_TEXTURE_SRV || MemberType == UBMT_RDG_BUFFER_SRV;
	}

	bool IsUAV() const
	{
		return MemberType == UBMT_RDG_TEXTURE_UAV || MemberType == UBMT_RDG_BUFFER_UAV;
	}

	bool IsView() const
	{
		return IsSRV() || IsUAV();
	}

	bool IsTexture() const
	{
		return
			MemberType == UBMT_RDG_TEXTURE ||
			MemberType == UBMT_RDG_TEXTURE_ACCESS;
	}

	bool IsTextureAccess() const
	{
		return MemberType == UBMT_RDG_TEXTURE_ACCESS;
	}

	bool IsBuffer() const
	{
		return
			MemberType == UBMT_RDG_BUFFER ||
			MemberType == UBMT_RDG_BUFFER_ACCESS;
	}

	bool IsBufferAccess() const
	{
		return MemberType == UBMT_RDG_BUFFER_ACCESS;
	}

	bool IsUniformBuffer() const
	{
		return MemberType == UBMT_RDG_UNIFORM_BUFFER;
	}

	bool IsParentResource() const
	{
		return IsTexture() || IsBuffer();
	}

	bool IsRenderTargetBindingSlots() const
	{
		return MemberType == UBMT_RENDER_TARGET_BINDING_SLOTS;
	}

	EUniformBufferBaseType GetType() const
	{
		return MemberType;
	}

	FRDGResourceRef GetAsResource() const
	{
		check(IsResource());
		return *GetAs<FRDGResourceRef>();
	}

	FRDGUniformBufferRef GetAsUniformBuffer() const
	{
		check(IsUniformBuffer());
		return *GetAs<FRDGUniformBufferRef>();
	}

	FRDGParentResourceRef GetAsParentResource() const
	{
		check(IsParentResource());
		return *GetAs<FRDGParentResourceRef>();
	}

	FRDGViewRef GetAsView() const
	{
		check(IsView());
		return *GetAs<FRDGViewRef>();
	}

	FRDGShaderResourceViewRef GetAsSRV() const
	{
		check(IsSRV());
		return *GetAs<FRDGShaderResourceViewRef>();
	}

	FRDGUnorderedAccessViewRef GetAsUAV() const
	{
		check(IsUAV());
		return *GetAs<FRDGUnorderedAccessViewRef>();
	}

	FRDGTextureRef GetAsTexture() const
	{
		check(IsTexture());
		return *GetAs<FRDGTextureRef>();
	}

	FRDGTextureAccess GetAsTextureAccess() const
	{
		check(MemberType == UBMT_RDG_TEXTURE_ACCESS);
		return *GetAs<FRDGTextureAccess>();
	}

	FRDGBufferRef GetAsBuffer() const
	{
		check(IsBuffer());
		return *GetAs<FRDGBufferRef>();
	}

	FRDGBufferAccess GetAsBufferAccess() const
	{
		check(MemberType == UBMT_RDG_BUFFER_ACCESS);
		return *GetAs<FRDGBufferAccess>();
	}

	FRDGTextureSRVRef GetAsTextureSRV() const
	{
		check(MemberType == UBMT_RDG_TEXTURE_SRV);
		return *GetAs<FRDGTextureSRVRef>();
	}

	FRDGBufferSRVRef GetAsBufferSRV() const
	{
		check(MemberType == UBMT_RDG_BUFFER_SRV);
		return *GetAs<FRDGBufferSRVRef>();
	}

	FRDGTextureUAVRef GetAsTextureUAV() const
	{
		check(MemberType == UBMT_RDG_TEXTURE_UAV);
		return *GetAs<FRDGTextureUAVRef>();
	}

	FRDGBufferUAVRef GetAsBufferUAV() const
	{
		check(MemberType == UBMT_RDG_BUFFER_UAV);
		return *GetAs<FRDGBufferUAVRef>();
	}

	const FRenderTargetBindingSlots& GetAsRenderTargetBindingSlots() const
	{
		check(IsRenderTargetBindingSlots());
		return *GetAs<FRenderTargetBindingSlots>();
	}

private:
	FRDGParameter(EUniformBufferBaseType InMemberType, void* InMemberPtr)
		: MemberType(InMemberType)
		, MemberPtr(InMemberPtr)
	{}

	template <typename T>
	T* GetAs() const
	{
		return reinterpret_cast<T*>(MemberPtr);
	}

	const EUniformBufferBaseType MemberType = UBMT_INVALID;
	void* const MemberPtr = nullptr;

	friend class FRDGParameterStruct;
};

/** Wraps a pass parameter struct payload and provides helpers for traversing members. */
class RENDERCORE_API FRDGParameterStruct
{
public:
	template <typename ParameterStructType>
	explicit FRDGParameterStruct(ParameterStructType* Parameters)
		: FRDGParameterStruct(Parameters, &ParameterStructType::FTypeInfo::GetStructMetadata()->GetLayout())
	{}

	explicit FRDGParameterStruct(const void* InContents, const FRHIUniformBufferLayout* InLayout)
		: Contents(reinterpret_cast<const uint8*>(InContents))
		, Layout(InLayout)
	{
		checkf(Contents && Layout, TEXT("Pass parameter struct created with null inputs."));
	}

	/** Returns the contents of the struct. */
	const uint8* GetContents() const { return Contents; }

	/** Returns the layout associated with this struct. */
	const FRHIUniformBufferLayout& GetLayout() const { return *Layout; }

	/** Helpful forwards from the layout. */
	FORCEINLINE bool HasRenderTargets() const   { return Layout->HasRenderTargets(); }
	FORCEINLINE bool HasExternalOutputs() const { return Layout->HasExternalOutputs(); }
	FORCEINLINE bool HasStaticSlot() const      { return Layout->HasStaticSlot(); }

	/** Returns the number of buffer parameters present on the layout. */
	uint32 GetBufferParameterCount() const  { return Layout->GraphBuffers.Num(); }

	/** Returns the number of texture parameters present on the layout. */
	uint32 GetTextureParameterCount() const { return Layout->GraphTextures.Num(); }

	/** Returns the render target binding slots. Asserts if they don't exist. */
	const FRenderTargetBindingSlots& GetRenderTargets() const
	{
		check(HasRenderTargets());
		return *reinterpret_cast<const FRenderTargetBindingSlots*>(Contents + Layout->RenderTargetsOffset);
	}

	/** Enumerates all graph parameters on the layout. Graph uniform buffers are traversed recursively but are
	 *  also included in the enumeration.
	 *  Expected function signature: void(FRDGParameter).
	 */
	template <typename FunctionType>
	void Enumerate(FunctionType Function) const;

	/** Same as Enumerate, but only texture parameters are included. */
	template <typename FunctionType>
	void EnumerateTextures(FunctionType Function) const;

	/** Same as Enumerate, but only buffer parameters are included. */
	template <typename FunctionType>
	void EnumerateBuffers(FunctionType Function) const;

	/** Enumerates all non-null uniform buffers. Expected function signature: void(FRDGUniformBuffer*). */
	template <typename FunctionType>
	void EnumerateUniformBuffers(FunctionType Function) const;

	/** Returns a set of static global uniform buffer bindings for the parameter struct. */
	FUniformBufferStaticBindings GetGlobalUniformBuffers() const;

	/** Returns the render pass info generated from the render target binding slots. */
	FRHIRenderPassInfo GetRenderPassInfo() const;

private:
	FRDGParameter GetParameterInternal(TArrayView<const FRHIUniformBufferLayout::FResourceParameter> Parameters, uint32 ParameterIndex) const
	{
		checkf(ParameterIndex < static_cast<uint32>(Parameters.Num()), TEXT("Attempted to access RDG pass parameter outside of index for Layout '%s'"), *Layout->GetDebugName());
		const EUniformBufferBaseType MemberType = Parameters[ParameterIndex].MemberType;
		const uint16 MemberOffset = Parameters[ParameterIndex].MemberOffset;
		return FRDGParameter(MemberType, const_cast<uint8*>(Contents + MemberOffset));
	}

	const uint8* Contents;
	const FRHIUniformBufferLayout* Layout;

	friend class FRDGPass;
};

template <typename ParameterStructType>
class TRDGParameterStruct
	: public FRDGParameterStruct
{
public:
	explicit TRDGParameterStruct(ParameterStructType* Parameters)
		: FRDGParameterStruct(Parameters, &ParameterStructType::FTypeInfo::GetStructMetadata()->GetLayout())
	{}

	/** Returns the contents of the struct. */
	const ParameterStructType* GetContents() const
	{
		return reinterpret_cast<const ParameterStructType*>(FRDGParameterStruct::GetContents());
	}

	const ParameterStructType* operator->() const
	{
		return GetContents();
	}
};

/** Helper function to get RHI render pass info from a pass parameter struct. Must be called
 *  within an RDG pass with the pass parameters; otherwise, the RHI access checks will assert.
 *  This helper is useful when you want to control the mechanics of render passes within an
 *  RDG raster pass by specifying 'SkipRenderPass'.
 */
template <typename TParameterStruct>
FORCEINLINE static FRHIRenderPassInfo GetRenderPassInfo(TParameterStruct* Parameters)
{
	return FRDGParameterStruct(Parameters).GetRenderPassInfo();
}

/** Helper function to get RHI global uniform buffers out of a pass parameters struct. */
template <typename TParameterStruct>
FORCEINLINE static FUniformBufferStaticBindings GetGlobalUniformBuffers(TParameterStruct* Parameters)
{
	return FRDGParameterStruct(Parameters).GetGlobalUniformBuffers();
}