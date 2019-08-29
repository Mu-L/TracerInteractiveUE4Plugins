// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	UniformBuffer.h: Uniform buffer declarations.
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "Containers/List.h"
#include "Containers/StaticArray.h"
#include "RHI.h"
#include "RenderingThread.h"
#include "RenderResource.h"
#include "Templates/IsArrayOrRefOfType.h"

class FShaderUniformBufferParameter;
template<typename TBufferStruct> class TShaderUniformBufferParameter;

namespace EShaderPrecisionModifier
{
	enum Type
	{
		Float,
		Half,
		Fixed
	};
};

/** A uniform buffer resource. */
template<typename TBufferStruct>
class TUniformBuffer : public FRenderResource
{
public:

	TUniformBuffer()
		: BufferUsage(UniformBuffer_MultiFrame)
		, Contents(nullptr)
	{
	}

	~TUniformBuffer()
	{
		if (Contents)
		{
			FMemory::Free(Contents);
		}
	}

	/** Sets the contents of the uniform buffer. */
	void SetContents(const TBufferStruct& NewContents)
	{
		SetContentsNoUpdate(NewContents);
		UpdateRHI();
	}
	/** Sets the contents of the uniform buffer to all zeros. */
	void SetContentsToZero()
	{
		if (!Contents)
		{
			Contents = (uint8*)FMemory::Malloc(sizeof(TBufferStruct), UNIFORM_BUFFER_STRUCT_ALIGNMENT);
		}
		FMemory::Memzero(Contents, sizeof(TBufferStruct));
		UpdateRHI();
	}

	// FRenderResource interface.
	virtual void InitDynamicRHI() override
	{
		check(IsInRenderingThread());
		UniformBufferRHI.SafeRelease();
		if (Contents)
		{
			UniformBufferRHI = RHICreateUniformBuffer(Contents,TBufferStruct::StaticStruct.GetLayout(),BufferUsage);
		}
	}
	virtual void ReleaseDynamicRHI() override
	{
		UniformBufferRHI.SafeRelease();
	}

	// Accessors.
	FUniformBufferRHIParamRef GetUniformBufferRHI() const 
	{ 
		check(UniformBufferRHI.GetReference()); // you are trying to use a UB that was never filled with anything
		return UniformBufferRHI; 
	}

	EUniformBufferUsage BufferUsage;

protected:

	/** Sets the contents of the uniform buffer. Used within calls to InitDynamicRHI */
	void SetContentsNoUpdate(const TBufferStruct& NewContents)
	{
		check(IsInRenderingThread());
		if (!Contents)
		{
			Contents = (uint8*)FMemory::Malloc(sizeof(TBufferStruct),UNIFORM_BUFFER_STRUCT_ALIGNMENT);
		}
		FMemory::Memcpy(Contents,&NewContents,sizeof(TBufferStruct));
	}

private:

	FUniformBufferRHIRef UniformBufferRHI;
	uint8* Contents;
};

/** A reference to a uniform buffer RHI resource with a specific structure. */
template<typename TBufferStruct>
class TUniformBufferRef : public FUniformBufferRHIRef
{
public:

	/** Initializes the reference to null. */
	TUniformBufferRef()
	{}

	/** Initializes the reference to point to a buffer. */
	TUniformBufferRef(const TUniformBuffer<TBufferStruct>& InBuffer)
	: FUniformBufferRHIRef(InBuffer.GetUniformBufferRHI())
	{}

	/** Creates a uniform buffer with the given value, and returns a structured reference to it. */
	static TUniformBufferRef<TBufferStruct> CreateUniformBufferImmediate(const TBufferStruct& Value, EUniformBufferUsage Usage)
	{
		check(IsInRenderingThread() || IsInRHIThread());
		return TUniformBufferRef<TBufferStruct>(RHICreateUniformBuffer(&Value,TBufferStruct::StaticStruct.GetLayout(),Usage));
	}
	/** Creates a uniform buffer with the given value, and returns a structured reference to it. */
	static FLocalUniformBuffer CreateLocalUniformBuffer(FRHICommandList& RHICmdList, const TBufferStruct& Value, EUniformBufferUsage Usage)
	{
		return RHICmdList.BuildLocalUniformBuffer(&Value, sizeof(TBufferStruct), TBufferStruct::StaticStruct.GetLayout());
	}

private:

	/** A private constructor used to coerce an arbitrary RHI uniform buffer reference to a structured reference. */
	TUniformBufferRef(FUniformBufferRHIParamRef InRHIRef)
	: FUniformBufferRHIRef(InRHIRef)
	{}
};

ENQUEUE_UNIQUE_RENDER_COMMAND_TWOPARAMETER_DECLARE_TEMPLATE(
	SetUniformBufferContents,TBufferStruct,
	TUniformBuffer<TBufferStruct>*,UniformBuffer,&UniformBuffer,
	TBufferStruct,Struct,Struct,
	{
		UniformBuffer->SetContents(Struct);
	});

/** Sends a message to the rendering thread to set the contents of a uniform buffer.  Called by the game thread. */
template<typename TBufferStruct>
void BeginSetUniformBufferContents(
	TUniformBuffer<TBufferStruct>& UniformBuffer,
	const TBufferStruct& Struct
	)
{
	ENQUEUE_UNIQUE_RENDER_COMMAND_TWOPARAMETER_CREATE_TEMPLATE(
		SetUniformBufferContents,TBufferStruct,
		TUniformBuffer<TBufferStruct>*,&UniformBuffer,
		TBufferStruct,Struct);
}

/** Each entry in a resource table is provided to the shader compiler for creating mappings. */
struct FResourceTableEntry
{
	/** The name of the uniform buffer in which this resource exists. */
	FString UniformBufferName;
	/** The type of the resource (EUniformBufferBaseType). */
	uint16 Type;
	/** The index of the resource in the table. */
	uint16 ResourceIndex;
};

/** A uniform buffer struct. */
class RENDERCORE_API FUniformBufferStruct
{
public:
	
	/** A member of a uniform buffer type. */
	class FMember
	{
	public:

		/** Initialization constructor. */
		FMember(
			const TCHAR* InName,
			const TCHAR* InShaderType,
			uint32 InOffset,
			EUniformBufferBaseType InBaseType,
			EShaderPrecisionModifier::Type InPrecision,
			uint32 InNumRows,
			uint32 InNumColumns,
			uint32 InNumElements,
			const FUniformBufferStruct* InStruct
			)
		:	Name(InName)
		,	ShaderType(InShaderType)
		,	Offset(InOffset)
		,	BaseType(InBaseType)
		,	Precision(InPrecision)
		,	NumRows(InNumRows)
		,	NumColumns(InNumColumns)
		,	NumElements(InNumElements)
		,	Struct(InStruct)
		{}
	
		const TCHAR* GetName() const { return Name; }
		const TCHAR* GetShaderType() const { return ShaderType; }
		uint32 GetOffset() const { return Offset; }
		EUniformBufferBaseType GetBaseType() const { return BaseType; }
		EShaderPrecisionModifier::Type GetPrecision() const { return Precision; }
		uint32 GetNumRows() const { return NumRows; }
		uint32 GetNumColumns() const { return NumColumns; }
		uint32 GetNumElements() const { return NumElements; }
		const FUniformBufferStruct* GetStruct() const { return Struct; }

	private:

		const TCHAR* Name;
		const TCHAR* ShaderType;
		uint32 Offset;
		EUniformBufferBaseType BaseType;
		EShaderPrecisionModifier::Type Precision;
		uint32 NumRows;
		uint32 NumColumns;
		uint32 NumElements;
		const FUniformBufferStruct* Struct;
	};

	typedef class FShaderUniformBufferParameter* (*ConstructUniformBufferParameterType)();

	/** Initialization constructor. */
	FUniformBufferStruct(const FName& InLayoutName, const TCHAR* InStructTypeName, const TCHAR* InShaderVariableName, ConstructUniformBufferParameterType InConstructRef, uint32 InSize, const TArray<FMember>& InMembers, bool bRegisterForAutoBinding);

	virtual ~FUniformBufferStruct()
	{
		GlobalListLink.Unlink();
		GetNameStructMap().Remove(FName(StructTypeName, FNAME_Find));
	}

	void InitializeLayout();

	void GetNestedStructs(TArray<const FUniformBufferStruct*>& OutNestedStructs) const;

	void AddResourceTableEntries(TMap<FString, FResourceTableEntry>& ResourceTableMap, TMap<FString, uint32>& ResourceTableLayoutHashes) const;

	const TCHAR* GetStructTypeName() const { return StructTypeName; }
	const TCHAR* GetShaderVariableName() const { return ShaderVariableName; }
	const uint32 GetSize() const { return Size; }
	const FRHIUniformBufferLayout& GetLayout() const 
	{ 
		check(bLayoutInitialized);
		return Layout; 
	}
	const TArray<FMember>& GetMembers() const { return Members; }
	FShaderUniformBufferParameter* ConstructTypedParameter() const { return (*ConstructUniformBufferParameterRef)(); }

	static TLinkedList<FUniformBufferStruct*>*& GetStructList();
	/** Speed up finding the uniform buffer by its name */
	static TMap<FName, FUniformBufferStruct*>& GetNameStructMap();

	static void InitializeStructs();

private:
	const TCHAR* StructTypeName;
	const TCHAR* ShaderVariableName;
	ConstructUniformBufferParameterType ConstructUniformBufferParameterRef;
	uint32 Size;
	bool bLayoutInitialized;
	FRHIUniformBufferLayout Layout;
	TArray<FMember> Members;
	TLinkedList<FUniformBufferStruct*> GlobalListLink;

	void AddResourceTableEntriesRecursive(const TCHAR* UniformBufferName, const TCHAR* Prefix, uint16& ResourceIndex, TMap<FString, FResourceTableEntry>& ResourceTableMap) const;
};

/** TFixedSizePointerWrapper acts like a pointer of the given type but enforces a fixed 64 bit size. */
template<class T>
class TFixedSizePointerWrapper
{
public:
	void operator=(T Other)
	{
		Member = Other;
	}

	operator T&()
	{
		return Member;
	}

private:

	union
	{
		T Member;
		uint64 _MinSizeDummy;
	};
	
	static_assert(sizeof(T) == sizeof(void*), "T should be a pointer.");
	static_assert(sizeof(T) <= sizeof(uint64), "Assuming max 64 bit platform");
};

//
// Uniform buffer alignment tools (should only be used by the uniform buffer type infos below).
//

	template<typename T,uint32 Alignment>
	class TAlignedTypedef;

	#define IMPLEMENT_ALIGNED_TYPE(Alignment) \
	template<typename T> \
	class TAlignedTypedef<T,Alignment> \
	{ \
	public: \
		typedef MS_ALIGN(Alignment) T TAlignedType GCC_ALIGN(Alignment); \
	};

	IMPLEMENT_ALIGNED_TYPE(1);
	IMPLEMENT_ALIGNED_TYPE(2);
	IMPLEMENT_ALIGNED_TYPE(4);
	IMPLEMENT_ALIGNED_TYPE(8);
	IMPLEMENT_ALIGNED_TYPE(16);

//
// Template specializations used to map C++ types to uniform buffer member types.
//

	template<typename, bool>
	class TUniformBufferTypeInfo
	{
	public:
		enum { BaseType = UBMT_INVALID };
		enum { NumRows = 0 };
		enum { NumColumns = 0 };
		enum { NumElements = 0 };
		enum { Alignment = 1 };
		enum { IsResource = 0 };
		static const FUniformBufferStruct* GetStruct() { return NULL; }
	};

	template<>
	class TUniformBufferTypeInfo<bool, false>
	{
	public:
		enum { BaseType = UBMT_BOOL };
		enum { NumRows = 1 };
		enum { NumColumns = 1 };
		enum { NumElements = 0 };
		enum { Alignment = 4 };
		enum { IsResource = 0 };
		typedef TAlignedTypedef<bool,Alignment>::TAlignedType TAlignedType;
		static const FUniformBufferStruct* GetStruct() { return NULL; }
	};

	template<>
	class TUniformBufferTypeInfo<uint32, false>
	{
	public:
		enum { BaseType = UBMT_UINT32 };
		enum { NumRows = 1 };
		enum { NumColumns = 1 };
		enum { NumElements = 0 };
		enum { Alignment = 4 };
		enum { IsResource = 0 };
		typedef uint32 TAlignedType;
		static const FUniformBufferStruct* GetStruct() { return NULL; }
	};
	

	template<>
	class TUniformBufferTypeInfo<int32, false>
	{
	public:
		enum { BaseType = UBMT_INT32 };
		enum { NumRows = 1 };
		enum { NumColumns = 1 };
		enum { NumElements = 0 };
		enum { Alignment = 4 };
		enum { IsResource = 0 };
		typedef int32 TAlignedType;
		static const FUniformBufferStruct* GetStruct() { return NULL; }
	};

	template<>
	class TUniformBufferTypeInfo<float, false>
	{
	public:
		enum { BaseType = UBMT_FLOAT32 };
		enum { NumRows = 1 };
		enum { NumColumns = 1 };
		enum { NumElements = 0 };
		enum { Alignment = 4 };
		enum { IsResource = 0 };
		typedef float TAlignedType;
		static const FUniformBufferStruct* GetStruct() { return NULL; }
	};

	template<>
	class TUniformBufferTypeInfo<FVector2D, false>
	{
	public:
		enum { BaseType = UBMT_FLOAT32 };
		enum { NumRows = 1 };
		enum { NumColumns = 2 };
		enum { NumElements = 0 };
		enum { Alignment = 8 };
		enum { IsResource = 0 };
		typedef TAlignedTypedef<FVector2D,Alignment>::TAlignedType TAlignedType;
		static const FUniformBufferStruct* GetStruct() { return NULL; }
	};

	template<>
	class TUniformBufferTypeInfo<FVector, false>
	{
	public:
		enum { BaseType = UBMT_FLOAT32 };
		enum { NumRows = 1 };
		enum { NumColumns = 3 };
		enum { NumElements = 0 };
		enum { Alignment = 16 };
		enum { IsResource = 0 };
		typedef TAlignedTypedef<FVector,Alignment>::TAlignedType TAlignedType;
		static const FUniformBufferStruct* GetStruct() { return NULL; }
	};

	template<>
	class TUniformBufferTypeInfo<FVector4, false>
	{
	public:
		enum { BaseType = UBMT_FLOAT32 };
		enum { NumRows = 1 };
		enum { NumColumns = 4 };
		enum { NumElements = 0 };
		enum { Alignment = 16 };
		enum { IsResource = 0 };
		typedef TAlignedTypedef<FVector4,Alignment>::TAlignedType TAlignedType;
		static const FUniformBufferStruct* GetStruct() { return NULL; }
	};
	
	template<>
	class TUniformBufferTypeInfo<FLinearColor, false>
	{
	public:
		enum { BaseType = UBMT_FLOAT32 };
		enum { NumRows = 1 };
		enum { NumColumns = 4 };
		enum { NumElements = 0 };
		enum { Alignment = 16 };
		enum { IsResource = 0 };
		typedef TAlignedTypedef<FLinearColor,Alignment>::TAlignedType TAlignedType;
		static const FUniformBufferStruct* GetStruct() { return NULL; }
	};
	
	template<>
	class TUniformBufferTypeInfo<FIntPoint, false>
	{
	public:
		enum { BaseType = UBMT_INT32 };
		enum { NumRows = 1 };
		enum { NumColumns = 2 };
		enum { NumElements = 0 };
		enum { Alignment = 8 };
		enum { IsResource = 0 };
		typedef TAlignedTypedef<FIntPoint,Alignment>::TAlignedType TAlignedType;
		static const FUniformBufferStruct* GetStruct() { return NULL; }
	};

	template<>
	class TUniformBufferTypeInfo<FIntVector, false>
	{
	public:
		enum { BaseType = UBMT_INT32 };
		enum { NumRows = 1 };
		enum { NumColumns = 3 };
		enum { NumElements = 0 };
		enum { Alignment = 16 };
		enum { IsResource = 0 };
		typedef TAlignedTypedef<FIntVector,Alignment>::TAlignedType TAlignedType;
		static const FUniformBufferStruct* GetStruct() { return NULL; }
	};

	template<>
	class TUniformBufferTypeInfo<FIntRect, false>
	{
	public:
		enum { BaseType = UBMT_INT32 };
		enum { NumRows = 1 };
		enum { NumColumns = 4 };
		enum { NumElements = 0 };
		enum { Alignment = 16 };
		enum { IsResource = 0 };
		typedef TAlignedTypedef<FIntRect,Alignment>::TAlignedType TAlignedType;
		static const FUniformBufferStruct* GetStruct() { return NULL; }
	};

	template<>
	class TUniformBufferTypeInfo<FMatrix, false>
	{
	public:
		enum { BaseType = UBMT_FLOAT32 };
		enum { NumRows = 4 };
		enum { NumColumns = 4 };
		enum { NumElements = 0 };
		enum { Alignment = 16 };
		enum { IsResource = 0 };
		typedef TAlignedTypedef<FMatrix,Alignment>::TAlignedType TAlignedType;
		static const FUniformBufferStruct* GetStruct() { return NULL; }
	};
	
	template<typename T,size_t InNumElements>
	class TUniformBufferTypeInfo<T[InNumElements], false>
	{
	public:
		enum { BaseType = TUniformBufferTypeInfo<T, false>::BaseType };
		enum { NumRows = TUniformBufferTypeInfo<T, false>::NumRows };
		enum { NumColumns = TUniformBufferTypeInfo<T, false>::NumColumns };
		enum { NumElements = InNumElements };
		enum { Alignment = TUniformBufferTypeInfo<T, false>::Alignment };
		enum { IsResource = TUniformBufferTypeInfo<T, false>::IsResource };
		typedef TStaticArray<T,InNumElements,16> TAlignedType;
		static const FUniformBufferStruct* GetStruct() { return TUniformBufferTypeInfo<T, false>::GetStruct(); }
	};
	
	template<typename T,size_t InNumElements,uint32 IgnoredAlignment>
	class TUniformBufferTypeInfo<TStaticArray<T,InNumElements,IgnoredAlignment>, false >
	{
	public:
		enum { BaseType = TUniformBufferTypeInfo<T, false>::BaseType };
		enum { NumRows = TUniformBufferTypeInfo<T, false>::NumRows };
		enum { NumColumns = TUniformBufferTypeInfo<T, false>::NumColumns };
		enum { NumElements = InNumElements };
		enum { Alignment = TUniformBufferTypeInfo<T, false>::Alignment };
		enum { IsResource = TUniformBufferTypeInfo<T, false>::IsResource };
		typedef TStaticArray<T,InNumElements,16> TAlignedType;
		static const FUniformBufferStruct* GetStruct() { return TUniformBufferTypeInfo<T, false>::GetStruct(); }
	};

	template<>
	class TUniformBufferTypeInfo<TFixedSizePointerWrapper<FShaderResourceViewRHIParamRef>, false>
	{
	public:
		enum { BaseType = UBMT_SRV };
		enum { NumRows = 1 };
		enum { NumColumns = 1 };
		enum { NumElements = 0 };
		enum { Alignment = sizeof(TFixedSizePointerWrapper<FShaderResourceViewRHIParamRef>) };
		enum { IsResource = 1 };
		typedef TAlignedTypedef<TFixedSizePointerWrapper<FShaderResourceViewRHIParamRef>,Alignment>::TAlignedType TAlignedType;
		static const FUniformBufferStruct* GetStruct() { return NULL; }
	};
	// RHICreateUniformBuffer assumes C++ constant layout matches the shader layout when extracting float constants, yet the C++ struct contains pointers.  
	// Enforce a min size of 64 bits on pointer types in uniform buffer structs to guarantee layout matching between languages.
	static_assert(sizeof(TFixedSizePointerWrapper<FShaderResourceViewRHIParamRef>) == sizeof(uint64), "Uniform buffer layout must not be platform dependent.");

	template<>
	class TUniformBufferTypeInfo<TFixedSizePointerWrapper<FUnorderedAccessViewRHIParamRef>, false>
	{
	public:
		enum { BaseType = UBMT_UAV };
		enum { NumRows = 1 };
		enum { NumColumns = 1 };
		enum { NumElements = 0 };
		enum { Alignment = sizeof(TFixedSizePointerWrapper<FUnorderedAccessViewRHIParamRef>) };
		enum { IsResource = 1 };
		typedef TAlignedTypedef<TFixedSizePointerWrapper<FUnorderedAccessViewRHIParamRef>,Alignment>::TAlignedType TAlignedType;
		static const FUniformBufferStruct* GetStruct() { return NULL; }
	};
	static_assert(sizeof(TFixedSizePointerWrapper<FUnorderedAccessViewRHIParamRef>) == sizeof(uint64), "Uniform buffer layout must not be platform dependent.");

	template<>
	class TUniformBufferTypeInfo<TFixedSizePointerWrapper<FSamplerStateRHIParamRef>, false>
	{
	public:
		enum { BaseType = UBMT_SAMPLER };
		enum { NumRows = 1 };
		enum { NumColumns = 1 };
		enum { NumElements = 0 };
		enum { Alignment = sizeof(TFixedSizePointerWrapper<FSamplerStateRHIParamRef>) };
		enum { IsResource = 1 };
		typedef TAlignedTypedef<TFixedSizePointerWrapper<FSamplerStateRHIParamRef>,Alignment>::TAlignedType TAlignedType;
		static const FUniformBufferStruct* GetStruct() { return NULL; }
	};
	static_assert(sizeof(TFixedSizePointerWrapper<FSamplerStateRHIParamRef>) == sizeof(uint64), "Uniform buffer layout must not be platform dependent.");

	template<>
	class TUniformBufferTypeInfo<TFixedSizePointerWrapper<FTextureRHIParamRef>, false>
	{
	public:
		enum { BaseType = UBMT_TEXTURE };
		enum { NumRows = 1 };
		enum { NumColumns = 1 };
		enum { NumElements = 0 };
		enum { Alignment = sizeof(TFixedSizePointerWrapper<FTextureRHIParamRef>) };
		enum { IsResource = 1 };
		typedef TAlignedTypedef<TFixedSizePointerWrapper<FTextureRHIParamRef>,Alignment>::TAlignedType TAlignedType;
		static const FUniformBufferStruct* GetStruct() { return NULL; }
	};
	static_assert(sizeof(TFixedSizePointerWrapper<FTextureRHIParamRef>) == sizeof(uint64), "Uniform buffer layout must not be platform dependent.");

	template<class StructType>
	class TUniformBufferTypeInfo<StructType, true> 
	{ 
	public: 
		enum { BaseType = UBMT_STRUCT }; 
		enum { NumRows = 1 }; 
		enum { NumColumns = 1 };
		enum { NumElements = 0 };
		enum { Alignment = UNIFORM_BUFFER_STRUCT_ALIGNMENT }; 
		enum { IsResource = 0 };
		typedef StructType TAlignedType;
		static const FUniformBufferStruct* GetStruct() { return &StructType::StaticStruct; } 
	};

//
// Macros for declaring uniform buffer structures.
//

#define IMPLEMENT_UNIFORM_BUFFER_STRUCT(StructTypeName,ShaderVariableName) \
	FUniformBufferStruct StructTypeName::StaticStruct( \
	FName(TEXT(#StructTypeName)), \
	TEXT(#StructTypeName), \
	ShaderVariableName, \
	StructTypeName::ConstructUniformBufferParameter, \
	sizeof(StructTypeName), \
	StructTypeName::zzGetMembers(), \
	true);

/** Begins a uniform buffer struct declaration. */
#define BEGIN_UNIFORM_BUFFER_STRUCT_EX(StructTypeName,PrefixKeywords,ConstructorSuffix) \
	MS_ALIGN(UNIFORM_BUFFER_STRUCT_ALIGNMENT) class PrefixKeywords StructTypeName \
	{ \
	public: \
		StructTypeName () ConstructorSuffix \
		static FUniformBufferStruct StaticStruct; \
		static FShaderUniformBufferParameter* ConstructUniformBufferParameter() { return new TShaderUniformBufferParameter<StructTypeName>(); } \
		static FUniformBufferRHIRef CreateUniformBuffer(const StructTypeName& InContents, EUniformBufferUsage InUsage) \
		{ \
			return RHICreateUniformBuffer(&InContents,StaticStruct.GetLayout(),InUsage); \
		} \
	private: \
		typedef StructTypeName zzTThisStruct; \
		struct zzFirstMemberId { enum { HasDeclaredResource = 0 }; }; \
		static TArray<FUniformBufferStruct::FMember> zzGetMembersBefore(zzFirstMemberId) \
		{ \
			return TArray<FUniformBufferStruct::FMember>(); \
		} \
		typedef zzFirstMemberId

/** Declares a member of a uniform buffer struct. */
#define DECLARE_UNIFORM_BUFFER_STRUCT_MEMBER_EXPLICIT(MemberType,MemberName,ArrayDecl,Precision,OptionalShaderType,IsMemberStruct) \
		zzMemberId##MemberName; \
	public: \
		typedef MemberType zzA##MemberName ArrayDecl; \
		typedef TUniformBufferTypeInfo<zzA##MemberName, IsMemberStruct>::TAlignedType zzT##MemberName; \
		zzT##MemberName MemberName; \
		static_assert(TUniformBufferTypeInfo<zzA##MemberName, IsMemberStruct>::BaseType != UBMT_INVALID, "Invalid type " #MemberType " of member " #MemberName "."); \
		static_assert(TUniformBufferTypeInfo<zzA##MemberName, IsMemberStruct>::BaseType != UBMT_UAV, "UAV is not yet supported in resource tables for " #MemberName " of type " #MemberType "."); \
	private: \
		struct zzNextMemberId##MemberName { enum { HasDeclaredResource = zzMemberId##MemberName::HasDeclaredResource || TUniformBufferTypeInfo<zzT##MemberName, IsMemberStruct>::IsResource }; }; \
		static TArray<FUniformBufferStruct::FMember> zzGetMembersBefore(zzNextMemberId##MemberName) \
		{ \
			static_assert(TUniformBufferTypeInfo<zzT##MemberName, IsMemberStruct>::IsResource == 1 || zzMemberId##MemberName::HasDeclaredResource == 0, "All resources must be declared last for " #MemberName "."); \
			static_assert(TUniformBufferTypeInfo<zzT##MemberName, IsMemberStruct>::IsResource == 0 || TIsArrayOrRefOfType<decltype(OptionalShaderType), TCHAR>::Value, "No shader type for " #MemberName "."); \
			/* Route the member enumeration on to the function for the member following this. */ \
			TArray<FUniformBufferStruct::FMember> OutMembers = zzGetMembersBefore(zzMemberId##MemberName()); \
			/* Add this member. */ \
			OutMembers.Add(FUniformBufferStruct::FMember( \
				TEXT(#MemberName), \
				OptionalShaderType, \
				STRUCT_OFFSET(zzTThisStruct,MemberName), \
				(EUniformBufferBaseType)TUniformBufferTypeInfo<zzA##MemberName, IsMemberStruct>::BaseType, \
				Precision, \
				TUniformBufferTypeInfo<zzA##MemberName, IsMemberStruct>::NumRows, \
				TUniformBufferTypeInfo<zzA##MemberName, IsMemberStruct>::NumColumns, \
				TUniformBufferTypeInfo<zzA##MemberName, IsMemberStruct>::NumElements, \
				TUniformBufferTypeInfo<zzA##MemberName, IsMemberStruct>::GetStruct() \
				)); \
			static_assert( \
				(STRUCT_OFFSET(zzTThisStruct,MemberName) & (TUniformBufferTypeInfo<zzA##MemberName, IsMemberStruct>::Alignment - 1)) == 0, \
				"Misaligned uniform buffer struct member " #MemberName "."); \
			return OutMembers; \
		} \
		typedef zzNextMemberId##MemberName

#define UNIFORM_MEMBER_ARRAY_EX(MemberType,MemberName,ArrayDecl,Precision) DECLARE_UNIFORM_BUFFER_STRUCT_MEMBER_EXPLICIT(MemberType,MemberName,ArrayDecl,Precision,TEXT(""),false)

#define UNIFORM_MEMBER_ARRAY(MemberType,MemberName,ArrayDecl) DECLARE_UNIFORM_BUFFER_STRUCT_MEMBER_EXPLICIT(MemberType,MemberName,ArrayDecl,EShaderPrecisionModifier::Float,TEXT(""),false)

#define UNIFORM_MEMBER(MemberType,MemberName) DECLARE_UNIFORM_BUFFER_STRUCT_MEMBER_EXPLICIT(MemberType,MemberName,,EShaderPrecisionModifier::Float,TEXT(""),false)

#define UNIFORM_MEMBER_EX(MemberType,MemberName,Precision) DECLARE_UNIFORM_BUFFER_STRUCT_MEMBER_EXPLICIT(MemberType,MemberName,,Precision,TEXT(""),false)

#define UNIFORM_MEMBER_SRV(ShaderType,MemberName) DECLARE_UNIFORM_BUFFER_STRUCT_MEMBER_EXPLICIT(TFixedSizePointerWrapper<FShaderResourceViewRHIParamRef>,MemberName,,EShaderPrecisionModifier::Float,TEXT(#ShaderType),false)

// NOT SUPPORTED YET
//#define DECLARE_UNIFORM_BUFFER_STRUCT_MEMBER_UAV(ShaderType,MemberName) DECLARE_UNIFORM_BUFFER_STRUCT_MEMBER_EXPLICIT(TFixedSizePointerWrapper<FUnorderedAccessViewRHIParamRef>,MemberName,,EShaderPrecisionModifier::Float,TEXT(#ShaderType))

#define UNIFORM_MEMBER_SAMPLER(ShaderType,MemberName) DECLARE_UNIFORM_BUFFER_STRUCT_MEMBER_EXPLICIT(TFixedSizePointerWrapper<FSamplerStateRHIParamRef>,MemberName,,EShaderPrecisionModifier::Float,TEXT(#ShaderType),false)

#define UNIFORM_MEMBER_TEXTURE(ShaderType,MemberName) DECLARE_UNIFORM_BUFFER_STRUCT_MEMBER_EXPLICIT(TFixedSizePointerWrapper<FTextureRHIParamRef>,MemberName,,EShaderPrecisionModifier::Float,TEXT(#ShaderType),false)

#define UNIFORM_MEMBER_STRUCT(StructType,MemberName) DECLARE_UNIFORM_BUFFER_STRUCT_MEMBER_EXPLICIT(StructType,MemberName,,EShaderPrecisionModifier::Float,TEXT(#StructType),true)

/** Ends a uniform buffer struct declaration. */
#define END_UNIFORM_BUFFER_STRUCT(Name) \
			zzLastMemberId; \
		static TArray<FUniformBufferStruct::FMember> zzGetMembers() { return zzGetMembersBefore(zzLastMemberId()); } \
	} GCC_ALIGN(UNIFORM_BUFFER_STRUCT_ALIGNMENT); \
	template<> class TUniformBufferTypeInfo<Name, false> \
	{ \
	public: \
		enum { BaseType = UBMT_STRUCT }; \
		enum { NumRows = 1 }; \
		enum { NumColumns = 1 }; \
		enum { Alignment = UNIFORM_BUFFER_STRUCT_ALIGNMENT }; \
		static const FUniformBufferStruct* GetStruct() { return &Name::StaticStruct; } \
	};

#define BEGIN_UNIFORM_BUFFER_STRUCT(StructTypeName,PrefixKeywords) BEGIN_UNIFORM_BUFFER_STRUCT_EX(StructTypeName,PrefixKeywords,{})
#define BEGIN_UNIFORM_BUFFER_STRUCT_WITH_CONSTRUCTOR(StructTypeName,PrefixKeywords) BEGIN_UNIFORM_BUFFER_STRUCT_EX(StructTypeName,PrefixKeywords,;)

/** Finds the FUniformBufferStruct corresponding to the given name, or NULL if not found. */
extern RENDERCORE_API FUniformBufferStruct* FindUniformBufferStructByName(const TCHAR* StructName);
extern RENDERCORE_API FUniformBufferStruct* FindUniformBufferStructByFName(FName StructName);
