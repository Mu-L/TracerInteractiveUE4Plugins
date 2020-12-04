// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MeshTypes.h"
#include "MeshElementRemappings.h"
#include "Misc/TVariant.h"
#include "Templates/CopyQualifiersFromTo.h"
#include "UObject/ReleaseObjectVersion.h"


/**
 * List of attribute types which are supported.
 *
 * IMPORTANT NOTE: Do not reorder or remove any type from this tuple, or serialization will fail.
 * Types may be added at the end of this list as required.
 */
using AttributeTypes = TTuple
<
	FVector4,
	FVector,
	FVector2D,
	float,
	int,
	bool,
	FName
>;


/**
 * Helper template which generates a TVariant of all supported attribute types.
 */
template <typename Tuple> struct TVariantFromTuple;
template <typename... Ts> struct TVariantFromTuple<TTuple<Ts...>> { using Type = TVariant<FEmptyVariantState, Ts...>; };


/**
 * Class which implements a function jump table to be automatically generated at compile time.
 * This is used by TAttributesSet to provide O(1) dispatch by attribute type at runtime.
 */
template <typename FnType, uint32 Size>
struct TJumpTable
{
	template <typename... T>
	explicit constexpr TJumpTable( T... Ts ) : Fns{ Ts... } {}

	FnType* Fns[Size];
};


/**
 * Traits class to specify which attribute types can be bulk serialized.
 */
template <typename T> struct TIsBulkSerializable { static const bool Value = true; };
template <> struct TIsBulkSerializable<FName> { static const bool Value = false; };


/**
 * This defines the container used to hold mesh element attributes of a particular name and index.
 * It is a simple TArray, so that all attributes are packed contiguously for each element ID.
 *
 * Note that the container may grow arbitrarily as new elements are inserted, but it will never be
 * shrunk as elements are removed. The only operations that will shrink the container are Initialize() and Remap().
 */
template <typename AttributeType>
class TMeshAttributeArrayBase
{
public:

	/** Custom serialization for TMeshAttributeArrayBase. */
	template <typename T> friend typename TEnableIf<!TIsBulkSerializable<T>::Value, FArchive>::Type& operator<<( FArchive& Ar, TMeshAttributeArrayBase<T>& Array );
	template <typename T> friend typename TEnableIf<TIsBulkSerializable<T>::Value, FArchive>::Type& operator<<( FArchive& Ar, TMeshAttributeArrayBase<T>& Array );

	/** Return size of container */
	FORCEINLINE int32 Num() const { return Container.Num(); }

	/** Return base of data */
	FORCEINLINE const AttributeType* GetData() const { return Container.GetData(); }

	/** Initializes the array to the given size with the default value */
	FORCEINLINE void Initialize( const int32 ElementCount, const AttributeType& Default )
	{
		Container.Reset( ElementCount );
		Insert( ElementCount - 1, Default );
	}

	uint32 GetHash( uint32 Crc = 0 ) const
	{
		return FCrc::MemCrc32( Container.GetData(), Container.Num() * sizeof( AttributeType ), Crc );
	}

	/** Expands the array if necessary so that the passed element index is valid. Newly created elements will be assigned the default value. */
	void Insert( const int32 Index, const AttributeType& Default );

	/** Remaps elements according to the passed remapping table */
	void Remap( const TSparseArray<int32>& IndexRemap, const AttributeType& Default );

	/** Element accessors */
	FORCEINLINE const AttributeType& operator[]( const int32 Index ) const { return Container[ Index ]; }
	FORCEINLINE AttributeType& operator[]( const int32 Index ) { return Container[ Index ]; }

protected:
	/** The actual container, represented by a regular array */
	TArray<AttributeType> Container;
};


template <typename AttributeType>
void TMeshAttributeArrayBase<AttributeType>::Insert( const int32 Index, const AttributeType& Default )
{
	if( Index >= Container.Num() )
	{
		// If the index is off the end of the container, add as many elements as required to make it the last valid index.
		int32 StartIndex = Container.AddUninitialized( Index + 1 - Container.Num() );
		AttributeType* Data = Container.GetData() + StartIndex;

		// Construct added elements with the default value passed in
		while( StartIndex <= Index )
		{
			new( Data ) AttributeType( Default );
			StartIndex++;
			Data++;
		}
	}
}

template <typename AttributeType>
void TMeshAttributeArrayBase<AttributeType>::Remap( const TSparseArray<int32>& IndexRemap, const AttributeType& Default )
{
	TMeshAttributeArrayBase NewAttributeArray;

	for( typename TSparseArray<int32>::TConstIterator It( IndexRemap ); It; ++It )
	{
		const int32 OldElementIndex = It.GetIndex();
		const int32 NewElementIndex = IndexRemap[ OldElementIndex ];

		NewAttributeArray.Insert( NewElementIndex, Default );
		NewAttributeArray[ NewElementIndex ] = MoveTemp( Container[ OldElementIndex ] );
	}

	Container = MoveTemp( NewAttributeArray.Container );
}

template <typename T>
inline typename TEnableIf<!TIsBulkSerializable<T>::Value, FArchive>::Type& operator<<( FArchive& Ar, TMeshAttributeArrayBase<T>& Array )
{
	// Serialize types which aren't bulk serializable, which need to be serialized element-by-element
	Ar << Array.Container;
	return Ar;
}

template <typename T>
inline typename TEnableIf<TIsBulkSerializable<T>::Value, FArchive>::Type& operator<<( FArchive& Ar, TMeshAttributeArrayBase<T>& Array )
{
	if( Ar.IsLoading() && Ar.CustomVer( FReleaseObjectVersion::GUID ) < FReleaseObjectVersion::MeshDescriptionNewSerialization )
	{
		// Legacy path for old format attribute arrays. BulkSerialize has a different format from regular serialization.
		Ar << Array.Container;
	}
	else
	{
		// Serialize types which are bulk serializable, i.e. which can be memcpy'd in bulk
		Array.Container.BulkSerialize( Ar );
	}

	return Ar;
}


// This is a deprecated class which will be removed in 4.21
template <typename AttributeType, typename ElementIDType>
class TMeshAttributeArray final : public TMeshAttributeArrayBase<AttributeType>
{
	static_assert( TIsDerivedFrom<ElementIDType, FElementID>::IsDerived, "ElementIDType must be derived from FElementID" );

	using Super = TMeshAttributeArrayBase<AttributeType>;
	using Super::operator[];

public:

	/** Element accessors */
	FORCEINLINE const AttributeType& operator[]( const ElementIDType ElementID ) const { return this->Container[ ElementID.GetValue() ]; }
	FORCEINLINE AttributeType& operator[]( const ElementIDType ElementID ) { return this->Container[ ElementID.GetValue() ]; }
};



/**
 * Flags specifying properties of an attribute
 */
enum class EMeshAttributeFlags : uint32
{
	None				= 0,
	Lerpable			= ( 1 << 0 ),	/** Attribute can be automatically lerped according to the value of 2 or 3 other attributes */
	AutoGenerated		= ( 1 << 1 ),	/** Attribute is auto-generated by importer or editable mesh, rather than representing an imported property */	
	Mergeable			= ( 1 << 2 ),	/** If all vertices' attributes are mergeable, and of near-equal value, they can be welded */
	Transient			= ( 1 << 3 )	/** Attribute is not serialized */
};

ENUM_CLASS_FLAGS( EMeshAttributeFlags );


/**
 * This is the base class for an attribute array set.
 * An attribute array set is a container which holds attribute arrays, one per attribute index.
 * Many attributes have only one index, while others (such as texture coordinates) may want to define many.
 *
 * All attribute array set instances will be of derived types; this type exists for polymorphism purposes,
 * so that they can be managed by a generic TUniquePtr<FMeshAttributeArraySetBase>.
 *
 * In general, we avoid accessing them via virtual dispatch by insisting that their type be passed as
 * a template parameter in the accessor. This can be checked against the Type field to ensure that we are
 * accessing an instance by its correct type.
 */
class FMeshAttributeArraySetBase
{
public:
	/** Constructor */
	FORCEINLINE FMeshAttributeArraySetBase( const uint32 InType, const EMeshAttributeFlags InFlags, const int32 InNumberOfElements )
		: Type( InType ),
		  NumElements( InNumberOfElements ),
		  Flags( InFlags )
	{}

	/** Virtual interface */
	virtual ~FMeshAttributeArraySetBase() = default;
	virtual TUniquePtr<FMeshAttributeArraySetBase> Clone() const = 0;
	virtual void Insert( const int32 Index ) = 0;
	virtual void Remove( const int32 Index ) = 0;
	virtual void Initialize( const int32 Count ) = 0;
	virtual uint32 GetHash() const = 0;
	virtual void Serialize( FArchive& Ar ) = 0;
	virtual void Remap( const TSparseArray<int32>& IndexRemap ) = 0;
	virtual int32 GetNumIndices() const = 0;
	virtual void SetNumIndices( const int32 NumIndices ) = 0;
	virtual void InsertIndex( const int32 Index ) = 0;
	virtual void RemoveIndex( const int32 Index ) = 0;

	/** Determine whether this attribute array set is of the given type */
	template <typename T>
	FORCEINLINE bool HasType() const
	{
		return TTupleIndex<T, AttributeTypes>::Value == Type;
	}

	/** Get the type index of this attribute array set */
	FORCEINLINE uint32 GetType() const { return Type; }

	/** Get the flags for this attribute array set */
	FORCEINLINE EMeshAttributeFlags GetFlags() const { return Flags; }

	/** Set the flags for this attribute array set */
	FORCEINLINE void SetFlags( const EMeshAttributeFlags InFlags ) { Flags = InFlags; }

	/** Return number of elements each attribute index has */
	FORCEINLINE int32 GetNumElements() const { return NumElements; }

protected:
	/** Type of the attribute array (based on the tuple element index from AttributeTypes) */
	uint32 Type;

	/** Number of elements in each index */
	int32 NumElements;

	/** Implementation-defined attribute name flags */
	EMeshAttributeFlags Flags;
};


/**
 * This is a type-specific attribute array, which is actually instanced in the attribute set.
 */
template <typename AttributeType>
class TMeshAttributeArraySet : public FMeshAttributeArraySetBase
{
	using Super = FMeshAttributeArraySetBase;

public:
	/** Constructor */
	FORCEINLINE explicit TMeshAttributeArraySet( const int32 NumberOfIndices = 0, const AttributeType& InDefaultValue = AttributeType(), const EMeshAttributeFlags InFlags = EMeshAttributeFlags::None, const int32 InNumberOfElements = 0 )
		: Super( TTupleIndex<AttributeType, AttributeTypes>::Value, InFlags, InNumberOfElements ),
		  DefaultValue( InDefaultValue )
	{
		SetNumIndices( NumberOfIndices );
	}

	/** Creates a copy of itself and returns a TUniquePtr to it */
	virtual TUniquePtr<FMeshAttributeArraySetBase> Clone() const
	{
		return MakeUnique<TMeshAttributeArraySet>( *this );
	}

	/** Insert the element at the given index */
	virtual void Insert( const int32 Index )
	{
		for( TMeshAttributeArrayBase<AttributeType>& ArrayForIndex : ArrayForIndices )
		{
			ArrayForIndex.Insert( Index, DefaultValue );
		}

		NumElements = FMath::Max( NumElements, Index + 1 );
	}

	/** Remove the element at the given index, replacing it with a default value */
	virtual void Remove( const int32 Index )
	{
		for( TMeshAttributeArrayBase<AttributeType>& ArrayForIndex : ArrayForIndices )
		{
			ArrayForIndex[ Index ] = DefaultValue;
		}
	}

	/** Sets the number of elements to the exact number provided, and initializes them to the default value */
	virtual void Initialize( const int32 Count )
	{
		NumElements = Count;
		for( TMeshAttributeArrayBase<AttributeType>& ArrayForIndex : ArrayForIndices )
		{
			ArrayForIndex.Initialize( Count, DefaultValue );
		}
	}

	virtual uint32 GetHash() const
	{
		uint32 CrcResult = 0;
		for ( const TMeshAttributeArrayBase<AttributeType>& ArrayForIndex : ArrayForIndices )
		{
			CrcResult = ArrayForIndex.GetHash( CrcResult );
		}
		return CrcResult;
	}

	/** Polymorphic serialization */
	virtual void Serialize( FArchive& Ar )
	{
		Ar << ( *this );
	}

	/** Performs an element index remap according to the passed array */
	virtual void Remap( const TSparseArray<int32>& IndexRemap )
	{
		for( TMeshAttributeArrayBase<AttributeType>& ArrayForIndex : ArrayForIndices )
		{
			ArrayForIndex.Remap( IndexRemap, DefaultValue );
			NumElements = ArrayForIndex.Num();
		}
	}

	/** Return number of indices this attribute has */
	virtual inline int32 GetNumIndices() const { return ArrayForIndices.Num(); }

	/** Sets number of indices this attribute has */
	virtual void SetNumIndices( const int32 NumIndices )
	{
		const int32 OriginalNumIndices = ArrayForIndices.Num();
		ArrayForIndices.SetNum( NumIndices );

		// If we have added new indices, ensure they are filled out with the correct number of elements
		for( int32 Index = OriginalNumIndices; Index < NumIndices; ++Index )
		{
			ArrayForIndices[ Index ].Initialize( NumElements, DefaultValue );
		}
	}

	/** Insert a new attribute index */
	virtual void InsertIndex( const int32 Index )
	{
		ArrayForIndices.InsertDefaulted( Index );
		ArrayForIndices[ Index ].Initialize( NumElements, DefaultValue );
	}

	/** Remove the array at the given index */
	virtual void RemoveIndex( const int32 Index )
	{
		ArrayForIndices.RemoveAt( Index );
	}


	/** Return the TMeshAttributeArrayBase corresponding to the given attribute index */
	FORCEINLINE const TMeshAttributeArrayBase<AttributeType>& GetArrayForIndex( const int32 Index ) const { return ArrayForIndices[ Index ]; }
	FORCEINLINE TMeshAttributeArrayBase<AttributeType>& GetArrayForIndex( const int32 Index ) { return ArrayForIndices[ Index ]; }

	/** Return default value for this attribute type */
	FORCEINLINE AttributeType GetDefaultValue() const { return DefaultValue; }

	/** Serializer */
	friend FArchive& operator<<( FArchive& Ar, TMeshAttributeArraySet& AttributeArraySet )
	{
		Ar << AttributeArraySet.NumElements;
		Ar << AttributeArraySet.ArrayForIndices;
		Ar << AttributeArraySet.DefaultValue;
		Ar << AttributeArraySet.Flags;
		return Ar;
	}

protected:
	/** An array of MeshAttributeArrays, one per attribute index */
	TArray<TMeshAttributeArrayBase<AttributeType>> ArrayForIndices;

	/** The default value for an attribute of this name */
	AttributeType DefaultValue;
};


// This is a deprecated class which will be removed in 4.21
template <typename T, typename U>
class TAttributeIndicesArray : public TMeshAttributeArraySet<T>
{
public:

	using AttributeType = T;
	using ElementIDType = U;

	/** Return the TMeshAttributeArray corresponding to the given attribute index */
	UE_DEPRECATED( 4.20, "Please use GetAttributesRef() or GetAttributesView() instead." )
	FORCEINLINE const TMeshAttributeArray<AttributeType, ElementIDType>& GetArrayForIndex( const int32 Index ) const
	{
		return static_cast<const TMeshAttributeArray<AttributeType, ElementIDType>&>( this->ArrayForIndices[ Index ] );
	}

	UE_DEPRECATED( 4.20, "Please use GetAttributesRef() or GetAttributesView() instead." )
	FORCEINLINE TMeshAttributeArray<AttributeType, ElementIDType>& GetArrayForIndex( const int32 Index )
	{
		return static_cast<TMeshAttributeArray<AttributeType, ElementIDType>&>( this->ArrayForIndices[ Index ] );
	}
};


/**
 * This is the class used to access attribute values.
 * It is a proxy object to a TMeshAttributeArraySet<> and should be passed by value.
 * It is valid for as long as the owning FMeshDescription exists.
 */
template <typename ElementIDType, typename AttributeType>
class TMeshAttributesRef
{
	template <typename T, typename U> friend class TMeshAttributesRef;

public:
	using Type = AttributeType;
	using ArrayType = typename TCopyQualifiersFromTo<AttributeType, TMeshAttributeArraySet<typename TRemoveCV<AttributeType>::Type>>::Type;

	/** Constructor taking a pointer to a TMeshAttributeArraySet */
	FORCEINLINE explicit TMeshAttributesRef( ArrayType* InArrayPtr = nullptr )
		: ArrayPtr( InArrayPtr )
	{}

	/** Implicitly construct a TMeshAttributesRef-to-const from a regular one */
	template <typename T = AttributeType, typename TEnableIf<TIsSame<T, const T>::Value, int>::Type = 0>
	FORCEINLINE TMeshAttributesRef( TMeshAttributesRef<ElementIDType, typename TRemoveCV<T>::Type> InRef )
		: ArrayPtr( InRef.ArrayPtr )
	{}

	/** Access elements from attribute index 0 */
	FORCEINLINE AttributeType& operator[]( const ElementIDType ElementID ) const
	{
		return ArrayPtr->GetArrayForIndex( 0 )[ ElementID.GetValue() ];
	}

	/** Get the element with the given ID from index 0 */
	FORCEINLINE AttributeType Get( const ElementIDType ElementID ) const
	{
		return ArrayPtr->GetArrayForIndex( 0 )[ ElementID.GetValue() ];
	}

	/** Get the element with the given ID and index */
	FORCEINLINE AttributeType Get( const ElementIDType ElementID, const int32 Index ) const
	{
		return ArrayPtr->GetArrayForIndex( Index )[ ElementID.GetValue() ];
	}

	/** Return whether the reference is valid or not */
	FORCEINLINE bool IsValid() const { return ( ArrayPtr != nullptr ); }

	/** Return default value for this attribute type */
	FORCEINLINE AttributeType GetDefaultValue() const { return ArrayPtr->GetDefaultValue(); }

	/** Return number of indices this attribute has */
	FORCEINLINE int32 GetNumIndices() const
	{
		return ArrayPtr->ArrayType::GetNumIndices();	// note: override virtual dispatch
	}

	/** Get the number of elements in this attribute array */
	FORCEINLINE int32 GetNumElements() const
	{
		return ArrayPtr->GetNumElements();
	}

	/** Get the flags for this attribute array set */
	FORCEINLINE EMeshAttributeFlags GetFlags() const { return ArrayPtr->GetFlags(); }

	/** Set the element with the given ID and index 0 to the provided value */
	FORCEINLINE void Set( const ElementIDType ElementID, const AttributeType& Value ) const
	{
		ArrayPtr->GetArrayForIndex( 0 )[ ElementID.GetValue() ] = Value;
	}

	/** Set the element with the given ID and index to the provided value */
	FORCEINLINE void Set( const ElementIDType ElementID, const int32 Index, const AttributeType& Value ) const
	{
		ArrayPtr->GetArrayForIndex( Index )[ ElementID.GetValue() ] = Value;
	}

	/** Copies the given attribute array and index to this index */
	void Copy( TMeshAttributesRef<ElementIDType, const AttributeType> Src, const int32 DestIndex = 0, const int32 SrcIndex = 0 );

	/** Sets number of indices this attribute has */
	FORCEINLINE void SetNumIndices( const int32 NumIndices ) const
	{
		ArrayPtr->ArrayType::SetNumIndices( NumIndices );	// note: override virtual dispatch
	}

	/** Inserts an attribute index */
	FORCEINLINE void InsertIndex( const int32 Index ) const
	{
		ArrayPtr->ArrayType::InsertIndex( Index );		// note: override virtual dispatch
	}

	/** Removes an attribute index */
	FORCEINLINE void RemoveIndex( const int32 Index ) const
	{
		ArrayPtr->ArrayType::RemoveIndex( Index );		// note: override virtual dispatch
	}

protected:
	ArrayType* ArrayPtr;
};

template <typename ElementIDType, typename AttributeType>
using TMeshAttributesConstRef = TMeshAttributesRef<ElementIDType, const AttributeType>;

template <typename ElementIDType, typename AttributeType>
void TMeshAttributesRef<ElementIDType, AttributeType>::Copy( TMeshAttributesRef<ElementIDType, const AttributeType> Src, const int32 DestIndex, const int32 SrcIndex )
{
	check( Src.IsValid() );
	const TMeshAttributeArrayBase<AttributeType>& SrcArray = Src->ArrayPtr->GetArrayForIndex( SrcIndex );
	TMeshAttributeArrayBase<AttributeType>& DestArray = ArrayPtr->GetArrayForIndex( DestIndex );
	const int32 Num = FMath::Min( SrcArray.Num(), DestArray.Num() );
	for( int32 Index = 0; Index < Num; Index++ )
	{
		DestArray[ Index ] = SrcArray[ Index ];
	}
}

/**
 * This is the class used to provide a 'view' of the specified type on an attribute array.
 * Like TMeshAttributesRef, it is a proxy object which is valid for as long as the owning FMeshDescription exists,
 * and should be passed by value.
 *
 * This is the base class, and shouldn't be instanced directly.
 */
template <typename ViewType>
class TMeshAttributesViewBase
{
public:
	using Type = ViewType;
	using ArrayType = typename TCopyQualifiersFromTo<ViewType, FMeshAttributeArraySetBase>::Type;

	/** Return whether the reference is valid or not */
	FORCEINLINE bool IsValid() const { return ( ArrayPtr != nullptr ); }

	/** Return number of indices this attribute has */
	FORCEINLINE int32 GetNumIndices() const { return ArrayPtr->GetNumIndices(); }

	/** Return default value for this attribute type */
	FORCEINLINE ViewType GetDefaultValue() const;

	/** Get the number of elements in this attribute array */
	FORCEINLINE int32 GetNumElements() const
	{
		return ArrayPtr->GetNumElements();
	}

protected:
	/** Constructor taking a pointer to a FMeshAttributeArraySetBase */
	FORCEINLINE explicit TMeshAttributesViewBase( ArrayType* InArrayPtr )
		: ArrayPtr( InArrayPtr )
	{}

	/** Get the element with the given ID from index 0 */
	FORCEINLINE ViewType GetByIndex( const int32 ElementIndex ) const;

	/** Get the element with the given element and attribute indices */
	FORCEINLINE ViewType GetByIndex( const int32 ElementIndex, const int32 AttributeIndex ) const;

	/** Set the attribute index 0 element with the given index to the provided value */
	FORCEINLINE void SetByIndex( const int32 ElementIndex, const ViewType& Value ) const;

	/** Set the element with the given element and attribute indices to the provided value */
	FORCEINLINE void SetByIndex( const int32 ElementIndex, const int32 AttributeIndex, const ViewType& Value ) const;

	ArrayType* ArrayPtr;
};


/**
 * This is a derived version with typesafe element accessors, which is returned by TAttributesSet<>.
 */
template <typename ElementIDType, typename ViewType>
class TMeshAttributesView final : public TMeshAttributesViewBase<ViewType>
{
	using Super = TMeshAttributesViewBase<ViewType>;

	template <typename T, typename U> friend class TMeshAttributesView;

public:
	/** Constructor taking a pointer to a FMeshAttributeArraySetBase */
	FORCEINLINE explicit TMeshAttributesView( typename Super::ArrayType* InArrayPtr = nullptr )
		: Super( InArrayPtr )
	{}

	/** Implicitly construct a TMeshAttributesViewBase-to-const from a regular one */
	template <typename T = ViewType, typename TEnableIf<TIsSame<T, const T>::Value, int>::Type = 0>
	FORCEINLINE TMeshAttributesView( TMeshAttributesView<ElementIDType, typename TRemoveCV<T>::Type> InView )
		: Super( InView.ArrayPtr )
	{}

	/** Get the element with the given ID from index 0. This version has a typesafe element ID accessor. */
	FORCEINLINE ViewType Get( const ElementIDType ElementID ) const { return this->GetByIndex( ElementID.GetValue() ); }

	/** Get the element with the given ID and index. This version has a typesafe element ID accessor. */
	FORCEINLINE ViewType Get( const ElementIDType ElementID, const int32 Index ) const { return this->GetByIndex( ElementID.GetValue(), Index ); }

	/** Set the element with the given ID and index 0 to the provided value. This version has a typesafe element ID accessor. */
	FORCEINLINE void Set( const ElementIDType ElementID, const ViewType& Value ) const { this->SetByIndex( ElementID.GetValue(), Value ); }

	/** Set the element with the given ID and index to the provided value. This version has a typesafe element ID accessor. */
	FORCEINLINE void Set( const ElementIDType ElementID, const int32 Index, const ViewType& Value ) const { this->SetByIndex( ElementID.GetValue(), Index, Value ); }

	/** Sets number of indices this attribute has */
	FORCEINLINE void SetNumIndices( const int32 NumIndices ) const { this->ArrayPtr->SetNumIndices( NumIndices ); }

	/** Inserts an attribute index */
	FORCEINLINE void InsertIndex( const int32 Index ) const { this->ArrayPtr->InsertIndex( Index ); }

	/** Removes an attribute index */
	FORCEINLINE void RemoveIndex( const int32 Index ) const { this->ArrayPtr->RemoveIndex( Index ); }
};

template <typename ElementIDType, typename AttributeType>
using TMeshAttributesConstView = TMeshAttributesView<ElementIDType, const AttributeType>;


/**
 * This is a wrapper for an allocated attributes array.
 * It holds a TUniquePtr pointing to the actual attributes array, and performs polymorphic copy and assignment,
 * as per the actual array type.
 */
class FAttributesSetEntry
{
public:
	/**
	 * Default constructor.
	 * This breaks the invariant that Ptr be always valid, but is necessary so that it can be the value type of a TMap.
	 */
	FORCEINLINE FAttributesSetEntry() = default;

	/**
	 * Construct a valid FAttributesSetEntry of the concrete type specified.
	 */
	template <typename AttributeType>
	FORCEINLINE FAttributesSetEntry( const int32 NumberOfIndices, const AttributeType& Default, const EMeshAttributeFlags Flags, const int32 NumElements )
		: Ptr( MakeUnique<TMeshAttributeArraySet<AttributeType>>( NumberOfIndices, Default, Flags, NumElements ) )
	{}

	/** Default destructor */
	FORCEINLINE ~FAttributesSetEntry() = default;

	/** Polymorphic copy: a new copy of Other is created */
	FAttributesSetEntry( const FAttributesSetEntry& Other )
		: Ptr( Other.Ptr ? Other.Ptr->Clone() : nullptr )
	{}

	/** Default move constructor */
	FAttributesSetEntry( FAttributesSetEntry&& ) = default;

	/** Polymorphic assignment */
	FAttributesSetEntry& operator=( const FAttributesSetEntry& Other )
	{
		FAttributesSetEntry Temp( Other );
		Swap( *this, Temp );
		return *this;
	}

	/** Default move assignment */
	FAttributesSetEntry& operator=( FAttributesSetEntry&& ) = default;

	/** Transparent access through the TUniquePtr */
	FORCEINLINE const FMeshAttributeArraySetBase* Get() const { return Ptr.Get(); }
	FORCEINLINE const FMeshAttributeArraySetBase* operator->() const { return Ptr.Get(); }
	FORCEINLINE const FMeshAttributeArraySetBase& operator*() const { return *Ptr; }
	FORCEINLINE FMeshAttributeArraySetBase* Get() { return Ptr.Get(); }
	FORCEINLINE FMeshAttributeArraySetBase* operator->() { return Ptr.Get(); }
	FORCEINLINE FMeshAttributeArraySetBase& operator*() { return *Ptr; }

	/** Object can be coerced to bool to indicate if it is valid */
	FORCEINLINE explicit operator bool() const { return Ptr.IsValid(); }
	FORCEINLINE bool operator!() const { return !Ptr.IsValid(); }

	/** Given a type at runtime, allocate an attribute array of that type, owned by Ptr */
	void CreateArrayOfType( const uint32 Type );

	/** Serialization */
	friend FArchive& operator<<( FArchive& Ar, FAttributesSetEntry& Entry );

private:
	TUniquePtr<FMeshAttributeArraySetBase> Ptr;
};


/**
 * This is the container for all attributes and their arrays. It wraps a TMap, mapping from attribute name to attribute array.
 * An attribute may be of any arbitrary type; we use a mixture of polymorphism and compile-time templates to handle the different types.
 */
class FAttributesSetBase
{
public:
	/** Constructor */
	FAttributesSetBase()
		: NumElements( 0 )
	{}

	/**
	 * Register a new attribute name with the given type (must be a member of the AttributeTypes tuple).
	 * If the attribute name is already registered, it will update it to use the new type, number of indices and flags.
	 *
	 * Example of use:
	 *
	 *		VertexInstanceAttributes().RegisterAttribute<FVector2D>( "UV", 8 );
	 *                        . . .
	 *		TVertexInstanceAttributeArray<FVector2D>& UV0 = VertexInstanceAttributes().GetAttributes<FVector2D>( "UV", 0 );
	 *		UV0[ VertexInstanceID ] = FVector2D( 1.0f, 1.0f );
	 */
	template <typename AttributeType>
	void RegisterAttribute( const FName AttributeName, const int32 NumberOfIndices = 1, const AttributeType& Default = AttributeType(), const EMeshAttributeFlags Flags = EMeshAttributeFlags::None );

	/**
	 * Unregister an attribute with the given name.
	 */
	void UnregisterAttribute( const FName AttributeName )
	{
		Map.Remove( AttributeName );
	}

	/** Determines whether an attribute exists with the given name */
	bool HasAttribute( const FName AttributeName ) const
	{
		return ( Map.Contains( AttributeName ) );
	}

	/**
	 * Determines whether an attribute of the given type exists with the given name
	 */
	template <typename AttributeType>
	bool HasAttributeOfType( const FName AttributeName ) const
	{
		if( const FAttributesSetEntry* ArraySetPtr = Map.Find( AttributeName ) )
		{
			return ( *ArraySetPtr )->HasType<AttributeType>();
		}

		return false;
	}

	/** Initializes all attributes to have the given number of elements with the default value */
	void Initialize( const int32 Count )
	{
		NumElements = Count;
		for( auto& MapEntry : Map )
		{
			MapEntry.Value->Initialize( Count );
		}
	}

	/** Applies the given remapping to the attributes set */
	void Remap( const TSparseArray<int32>& IndexRemap );

	/** Returns an array of all the attribute names registered */
	template <typename Allocator>
	void GetAttributeNames( TArray<FName, Allocator>& OutAttributeNames ) const
	{
		Map.GetKeys( OutAttributeNames );
	}

	uint32 GetHash( const FName AttributeName ) const
	{
		if ( const FAttributesSetEntry* ArraySetPtr = Map.Find( AttributeName ) )
		{
			return ( *ArraySetPtr )->GetHash();
		}
		return 0;
	}

	template <typename AttributeType>
	UE_DEPRECATED( 4.20, "Please use untemplated UnregisterAttribute() instead" )
	void UnregisterAttribute( const FName AttributeName )
	{
		return UnregisterAttribute( AttributeName );
	}

	template <typename AttributeType>
	UE_DEPRECATED( 4.20, "Please use untemplated HasAttribute() instead" )
	bool HasAttribute( const FName AttributeName )
	{
		return HasAttribute( AttributeName );
	}

protected:
	/**
	 * Insert a new element at the given index.
	 * The public API version of this function takes an ID of ElementIDType instead of a typeless index.
	 */
	void Insert( const int32 Index )
	{
		NumElements = FMath::Max( NumElements, Index + 1 );

		for( auto& MapEntry : Map )
		{
			MapEntry.Value->Insert( Index );
			check( MapEntry.Value->GetNumElements() == NumElements );
		}
	}

	/**
	 * Remove an element at the given index.
	 * The public API version of this function takes an ID of ElementIDType instead of a typeless index.
	 */
	void Remove( const int32 Index )
	{
		for( auto& MapEntry : Map )
		{
			MapEntry.Value->Remove( Index );
		}
	}

	/** Serialization */
	friend MESHDESCRIPTION_API FArchive& operator<<( FArchive& Ar, FAttributesSetBase& AttributesSet );

	template <typename T>
	friend void SerializeLegacy( FArchive& Ar, FAttributesSetBase& AttributesSet );

	/** The actual container */
	TMap<FName, FAttributesSetEntry> Map;

	/** The number of elements in each attribute array */
	int32 NumElements;
};


template <typename AttributeType>
void FAttributesSetBase::RegisterAttribute( const FName AttributeName, const int32 NumberOfIndices, const AttributeType& Default, const EMeshAttributeFlags Flags )
{
	if( FAttributesSetEntry* ArraySetPtr = Map.Find( AttributeName ) )
	{
		if( ( *ArraySetPtr )->HasType<AttributeType>() )
		{
			using ArrayType = TMeshAttributeArraySet<AttributeType>;
			static_cast<ArrayType*>( ArraySetPtr->Get() )->ArrayType::SetNumIndices( NumberOfIndices );	// note: override virtual dispatch
			( *ArraySetPtr )->SetFlags( Flags );
		}
		else
		{
			Map.Remove( AttributeName );
			Map.Emplace( AttributeName, FAttributesSetEntry( NumberOfIndices, Default, Flags, NumElements ) );
		}
	}
	else
	{
		Map.Emplace( AttributeName, FAttributesSetEntry( NumberOfIndices, Default, Flags, NumElements ) );
	}
}


/**
 * This is a version of the attributes set container which accesses elements by typesafe IDs.
 * This prevents access of (for example) vertex instance attributes by vertex IDs.
 */
template <typename ElementIDType>
class TAttributesSet final : public FAttributesSetBase
{
	using FAttributesSetBase::Insert;
	using FAttributesSetBase::Remove;

public:
	/**
	 * Get an attribute array with the given type and name.
	 * The attribute type must correspond to the type passed as the template parameter.
	 *
	 * Example of use:
	 *
	 *		TVertexAttributesConstRef<FVector> VertexPositions = VertexAttributes().GetAttributesRef<FVector>( "Position" ); // note: assign to value type
	 *		for( const FVertexID VertexID : GetVertices().GetElementIDs() )
	 *		{
	 *			const FVector Position = VertexPositions.Get( VertexID );
	 *			DoSomethingWith( Position );
	 *		}
	 *
	 * Note that the returned object is a value type which should be assigned and passed by value, not reference.
	 * It is valid for as long as this TAttributesSet object exists.
	 */
	template <typename AttributeType>
	TMeshAttributesConstRef<ElementIDType, AttributeType> GetAttributesRef( const FName AttributeName ) const
	{
		if( const FAttributesSetEntry* ArraySetPtr = this->Map.Find( AttributeName ) )
		{
			if( ( *ArraySetPtr )->HasType<AttributeType>() )
			{
				return TMeshAttributesConstRef<ElementIDType, AttributeType>( static_cast<const TMeshAttributeArraySet<AttributeType>*>( ArraySetPtr->Get() ) );
			}
		}

		return TMeshAttributesConstRef<ElementIDType, AttributeType>();
	}

	template <typename AttributeType>
	TMeshAttributesRef<ElementIDType, AttributeType> GetAttributesRef( const FName AttributeName )
	{
		if( FAttributesSetEntry* ArraySetPtr = this->Map.Find( AttributeName ) )
		{
			if( ( *ArraySetPtr )->HasType<AttributeType>() )
			{
				return TMeshAttributesRef<ElementIDType, AttributeType>( static_cast<TMeshAttributeArraySet<AttributeType>*>( ArraySetPtr->Get() ) );
			}
		}

		return TMeshAttributesRef<ElementIDType, AttributeType>();
	}

	template <typename ViewType>
	UE_DEPRECATED(4.26, "GetAttributesView() will no longer be supported; please use GetAttributesRef() instead.")
	TMeshAttributesConstView<ElementIDType, ViewType> GetAttributesView( const FName AttributeName ) const
	{
		if( const FAttributesSetEntry* ArraySetPtr = this->Map.Find( AttributeName ) )
		{
			return TMeshAttributesConstView<ElementIDType, ViewType>( ArraySetPtr->Get() );
		}

		return TMeshAttributesConstView<ElementIDType, ViewType>();
	}

	template <typename ViewType>
	UE_DEPRECATED(4.26, "GetAttributesView() will no longer be supported; please use GetAttributesRef() instead.")
	TMeshAttributesView<ElementIDType, ViewType> GetAttributesView( const FName AttributeName )
	{
		if( FAttributesSetEntry* ArraySetPtr = this->Map.Find( AttributeName ) )
		{
			return TMeshAttributesView<ElementIDType, ViewType>( ArraySetPtr->Get() );
		}

		return TMeshAttributesView<ElementIDType, ViewType>();
	}

	/** Returns the number of indices for the attribute with the given name */
	template <typename AttributeType>
	int32 GetAttributeIndexCount( const FName AttributeName ) const
	{
		if( const FAttributesSetEntry* ArraySetPtr = this->Map.Find( AttributeName ) )
		{
			if( ( *ArraySetPtr )->HasType<AttributeType>() )
			{
				using ArrayType = TMeshAttributeArraySet<AttributeType>;
				return static_cast<const ArrayType*>( ArraySetPtr->Get() )->ArrayType::GetNumIndices();	// note: override virtual dispatch
			}
		}

		return 0;
	}

	/** Sets the number of indices for the attribute with the given name */
	template <typename AttributeType>
	void SetAttributeIndexCount( const FName AttributeName, const int32 NumIndices )
	{
		if( FAttributesSetEntry* ArraySetPtr = this->Map.Find( AttributeName ) )
		{
			if( ( *ArraySetPtr )->HasType<AttributeType>() )
			{
				using ArrayType = TMeshAttributeArraySet<AttributeType>;
				static_cast<ArrayType*>( ArraySetPtr->Get() )->ArrayType::SetNumIndices( NumIndices );	// note: override virtual dispatch
			}
		}
	}

	/** Insert a new index for the attribute with the given name */
	template <typename AttributeType>
	void InsertAttributeIndex( const FName AttributeName, const int32 Index )
	{
		if( FAttributesSetEntry* ArraySetPtr = this->Map.Find( AttributeName ) )
		{
			if( ( *ArraySetPtr )->HasType<AttributeType>() )
			{
				using ArrayType = TMeshAttributeArraySet<AttributeType>;
				static_cast<ArrayType*>( ArraySetPtr->Get() )->ArrayType::InsertIndex( Index );	// note: override virtual dispatch
			}
		}
	}

	/** Remove an existing index from the attribute with the given name */
	template <typename AttributeType>
	void RemoveAttributeIndex( const FName AttributeName, const int32 Index )
	{
		if( FAttributesSetEntry* ArraySetPtr = this->Map.Find( AttributeName ) )
		{
			if( ( *ArraySetPtr )->HasType<AttributeType>() )
			{
				using ArrayType = TMeshAttributeArraySet<AttributeType>;
				static_cast<ArrayType*>( ArraySetPtr->Get() )->ArrayType::RemoveIndex( Index );	// note: override virtual dispatch
			}
		}
	}

	template <typename AttributeType>
	AttributeType GetAttribute( const ElementIDType ElementID, const FName AttributeName, const int32 AttributeIndex = 0 ) const
	{
		const FMeshAttributeArraySetBase* ArraySetPtr = this->Map.FindChecked( AttributeName ).Get();
		check( ArraySetPtr->HasType<AttributeType>() );
		return static_cast<const TMeshAttributeArraySet<AttributeType>*>( ArraySetPtr )->GetArrayForIndex( AttributeIndex )[ ElementID.GetValue() ];
	}

	template <typename AttributeType>
	void SetAttribute( const ElementIDType ElementID, const FName AttributeName, const int32 AttributeIndex, const AttributeType& AttributeValue )
	{
		FMeshAttributeArraySetBase* ArraySetPtr = this->Map.FindChecked( AttributeName ).Get();
		check( ArraySetPtr->HasType<AttributeType>() );
		static_cast<TMeshAttributeArraySet<AttributeType>*>( ArraySetPtr )->GetArrayForIndex( AttributeIndex )[ ElementID.GetValue() ] = AttributeValue;
	}

	/** Inserts a default-initialized value for all attributes of the given ID */
	FORCEINLINE void Insert( const ElementIDType ElementID )
	{
		this->Insert( ElementID.GetValue() );
	}

	/** Removes all attributes with the given ID */
	FORCEINLINE void Remove( const ElementIDType ElementID )
	{
		this->Remove( ElementID.GetValue() );
	}

	/**
	 * Call the supplied function on each attribute.
	 * The prototype should be Func( const FName AttributeName, auto AttributesRef );
	 */
	template <typename ForEachFunc> void ForEach( ForEachFunc Func );

	/**
	* Call the supplied function on each attribute.
	* The prototype should be Func( const FName AttributeName, auto AttributesConstRef );
	*/
	template <typename ForEachFunc> void ForEach( ForEachFunc Func ) const;

	/**
	 * Call the supplied function on each attribute.
	 * The prototype should be Func( const FName AttributeName, auto& AttributeIndicesArray );
	 */
	template <typename FuncType>
	UE_DEPRECATED( 4.20, "This is no longer supported; please use ForEach() instead and amend your lambda to accept an auto of type TMeshAttributesRef instead." )
	void ForEachAttributeIndicesArray( const FuncType& Func )
	{
		check( false );
	}

	template <typename FuncType>
	UE_DEPRECATED( 4.20, "This is no longer supported; please use ForEach() instead and amend your lambda to accept an auto of type TMeshAttributesConstRef instead." )
	void ForEachAttributeIndicesArray( const FuncType& Func ) const
	{
		check( false );
	}

	template <typename AttributeType>
	UE_DEPRECATED( 4.20, "Please use GetAttributesRef() or GetAttributesView() instead." )
	TMeshAttributeArray<AttributeType, ElementIDType>& GetAttributes( const FName AttributeName, const int32 AttributeIndex = 0 )
	{
		FMeshAttributeArraySetBase* ArraySetPtr = this->Map.FindChecked( AttributeName ).Get();
		check( ArraySetPtr->HasType<AttributeType>() );
		return static_cast<TAttributeIndicesArray<AttributeType, ElementIDType>*>( ArraySetPtr )->GetArrayForIndex( AttributeIndex );
	}

	template <typename AttributeType>
	UE_DEPRECATED( 4.20, "Please use GetAttributesRef() or GetAttributesView() instead." )
	const TMeshAttributeArray<AttributeType, ElementIDType>& GetAttributes( const FName AttributeName, const int32 AttributeIndex = 0 ) const
	{
		const FMeshAttributeArraySetBase* ArraySetPtr = this->Map.FindChecked( AttributeName ).Get();
		check( ArraySetPtr->HasType<AttributeType>() );
		return static_cast<const TAttributeIndicesArray<AttributeType, ElementIDType>*>( ArraySetPtr )->GetArrayForIndex( AttributeIndex );
	}

	template <typename AttributeType>
	UE_DEPRECATED( 4.20, "Please use GetAttributesRef() or GetAttributesView() instead." )
	TAttributeIndicesArray<AttributeType, ElementIDType>& GetAttributesSet( const FName AttributeName )
	{
		FMeshAttributeArraySetBase* ArraySetPtr = this->Map.FindChecked( AttributeName ).Get();
		check( ArraySetPtr->HasType<AttributeType>() );
		return static_cast<TAttributeIndicesArray<AttributeType, ElementIDType>&>( *ArraySetPtr );
	}

	template <typename AttributeType>
	UE_DEPRECATED( 4.20, "Please use GetAttributesRef() or GetAttributesView() instead." )
	const TAttributeIndicesArray<AttributeType, ElementIDType>& GetAttributesSet( const FName AttributeName ) const
	{
		const FMeshAttributeArraySetBase* ArraySetPtr = this->Map.FindChecked( AttributeName ).Get();
		check( ArraySetPtr->HasType<AttributeType>() );
		return static_cast<const TAttributeIndicesArray<AttributeType, ElementIDType>&>( *ArraySetPtr );
	}
};


/**
 * We need a mechanism by which we can iterate all items in the attribute map and perform an arbitrary operation on each.
 * We require polymorphic behavior, as attribute arrays are templated on their attribute type, and derived from a generic base class.
 * However, we cannot have a virtual templated method, so we use a different approach.
 *
 * Effectively, we wish to cast the attribute array depending on the type member of the base class as we iterate through the map.
 * This might look something like this:
 *
 *    template <typename FuncType>
 *    void ForEach(FuncType Func)
 *    {
 *        for (const auto& MapEntry : Map)
 *        {
 *            const uint32 Type = MapEntry.Value->GetType();
 *            switch (Type)
 *            {
 *                case 0: Func(static_cast<TMeshAttributeArraySet<FVector>*>(MapEntry.Value.Get()); break;
 *                case 1: Func(static_cast<TMeshAttributeArraySet<FVector4>*>(MapEntry.Value.Get()); break;
 *                case 2: Func(static_cast<TMeshAttributeArraySet<FVector2D>*>(MapEntry.Value.Get()); break;
 *                case 3: Func(static_cast<TMeshAttributeArraySet<float>*>(MapEntry.Value.Get()); break;
 *                      ....
 *            }
 *        }
 *    }
 *
 * (The hope is that the compiler would optimize the switch into a jump table so we get O(1) dispatch even as the number of attribute types
 * increases.)
 *
 * The approach taken here is to generate a jump table at compile time, one entry per possible attribute type.
 * The function Dispatch(...) is the actual function which gets called.
 * MakeJumpTable() is the constexpr function which creates a static jump table at compile time.
 */
namespace ForEachImpl
{
	// Declare type of jump table used to dispatch functions
	template <typename ElementIDType, typename ForEachFunc>
	using JumpTableType = TJumpTable<void( FName, ForEachFunc, FMeshAttributeArraySetBase* ), TTupleArity<AttributeTypes>::Value>;

	// Define dispatch function
	template <typename ElementIDType, typename ForEachFunc, uint32 I>
	static void Dispatch( FName Name, ForEachFunc Fn, FMeshAttributeArraySetBase* Attributes )
	{
		using AttributeType = typename TTupleElement<I, AttributeTypes>::Type;
		Fn( Name, TMeshAttributesRef<ElementIDType, AttributeType>( static_cast<TMeshAttributeArraySet<AttributeType>*>( Attributes ) ) );
	}

	// Build ForEach jump table at compile time, a separate instantiation of Dispatch for each attribute type
	template <typename ElementIDType, typename ForEachFunc, uint32... Is>
	static constexpr JumpTableType<ElementIDType, ForEachFunc> MakeJumpTable( TIntegerSequence< uint32, Is...> )
	{
		return JumpTableType<ElementIDType, ForEachFunc>( Dispatch<ElementIDType, ForEachFunc, Is>... );
	}
}

template <typename ElementIDType>
template <typename ForEachFunc>
void TAttributesSet<ElementIDType>::ForEach( ForEachFunc Func )
{
	// Construct compile-time jump table for dispatching ForEachImpl::Dispatch() by the attribute type at runtime
	static constexpr ForEachImpl::JumpTableType<ElementIDType, ForEachFunc>
		JumpTable = ForEachImpl::MakeJumpTable<ElementIDType, ForEachFunc>( TMakeIntegerSequence<uint32, TTupleArity<AttributeTypes>::Value>() );

	for( auto& MapEntry : this->Map )
	{
		const uint32 Type = MapEntry.Value->GetType();
		JumpTable.Fns[ Type ]( MapEntry.Key, Func, MapEntry.Value.Get() );
	}
}

namespace ForEachConstImpl
{
	// Declare type of jump table used to dispatch functions
	template <typename ElementIDType, typename ForEachFunc>
	using JumpTableType = TJumpTable<void( FName, ForEachFunc, const FMeshAttributeArraySetBase* ), TTupleArity<AttributeTypes>::Value>;

	// Define dispatch function
	template <typename ElementIDType, typename ForEachFunc, uint32 I>
	static void Dispatch( FName Name, ForEachFunc Fn, const FMeshAttributeArraySetBase* Attributes )
	{
		using AttributeType = typename TTupleElement<I, AttributeTypes>::Type;
		Fn( Name, TMeshAttributesConstRef<ElementIDType, AttributeType>( static_cast<const TMeshAttributeArraySet<AttributeType>*>( Attributes ) ) );
	}

	// Build ForEach jump table at compile time, a separate instantiation of Dispatch for each attribute type
	template <typename ElementIDType, typename ForEachFunc, uint32... Is>
	static constexpr JumpTableType<ElementIDType, ForEachFunc> MakeJumpTable( TIntegerSequence< uint32, Is...> )
	{
		return JumpTableType<ElementIDType, ForEachFunc>( Dispatch<ElementIDType, ForEachFunc, Is>... );
	}
}

template <typename ElementIDType>
template <typename ForEachFunc>
void TAttributesSet<ElementIDType>::ForEach( ForEachFunc Func ) const
{
	// Construct compile-time jump table for dispatching ForEachImpl::Dispatch() by the attribute type at runtime
	static constexpr ForEachConstImpl::JumpTableType<ElementIDType, ForEachFunc>
		JumpTable = ForEachConstImpl::MakeJumpTable<ElementIDType, ForEachFunc>( TMakeIntegerSequence<uint32, TTupleArity<AttributeTypes>::Value>() );

	for( const auto& MapEntry : this->Map )
	{
		const uint32 Type = MapEntry.Value->GetType();
		JumpTable.Fns[ Type ]( MapEntry.Key, Func, MapEntry.Value.Get() );
	}
}

/**
 * This is a similar approach to ForEach, above.
 * Given a type index, at runtime, we wish to create an attribute array of the corresponding type; essentially a factory.
 *
 * We generate a jump table at compile time, containing generated functions to register attributes of each type.
 */
namespace CreateTypeImpl
{
	// Declare type of jump table used to dispatch functions
	using JumpTableType = TJumpTable<TUniquePtr<FMeshAttributeArraySetBase>(), TTupleArity<AttributeTypes>::Value>;

	// Define dispatch function
	template <uint32 I>
	static TUniquePtr<FMeshAttributeArraySetBase> Dispatch()
	{
		using AttributeType = typename TTupleElement<I, AttributeTypes>::Type;
		return MakeUnique<TMeshAttributeArraySet<AttributeType>>();
	}

	// Build RegisterAttributeOfType jump table at compile time, a separate instantiation of Dispatch for each attribute type
	template <uint32... Is>
	static constexpr JumpTableType MakeJumpTable( TIntegerSequence< uint32, Is...> )
	{
		return JumpTableType( Dispatch<Is>... );
	}
}

inline void FAttributesSetEntry::CreateArrayOfType( const uint32 Type )
{
	static constexpr CreateTypeImpl::JumpTableType JumpTable = CreateTypeImpl::MakeJumpTable( TMakeIntegerSequence<uint32, TTupleArity<AttributeTypes>::Value>() );
	Ptr = JumpTable.Fns[ Type ]();
}


/**
 * Helper struct which determines whether ViewType and the I'th type from AttributeTypes are mutually constructible from each other.
 */
template <typename ViewType, uint32 I>
struct TIsViewable
{
	enum { Value = TIsConstructible<ViewType, typename TTupleElement<I, AttributeTypes>::Type>::Value &&
				   TIsConstructible<typename TTupleElement<I, AttributeTypes>::Type, ViewType>::Value };
};

/**
 * Implementation for TMeshAttributesConstViewBase::Get(ElementIndex).
 *
 * This is implemented similarly to the above. A jump table is built, so the correct implementation is dispatched according to the array type.
 * This cannot be a regular virtual function because the return type depends on the array type.
 */
namespace AttributesViewGetImpl
{
	// Declare type of jump table used to dispatch functions
	template <typename ViewType>
	using JumpTableType = TJumpTable<ViewType( const FMeshAttributeArraySetBase*, int32 ), TTupleArity<AttributeTypes>::Value>;

	// Define dispatch functions
	template <typename ViewType, uint32 I, typename TEnableIf<TIsViewable<ViewType, I>::Value, int>::Type = 0>
	static ViewType Dispatch( const FMeshAttributeArraySetBase* Array, const int32 Index )
	{
		// Implementation when the attribute type is convertible to the view type
		return ViewType( static_cast<const TMeshAttributeArraySet<typename TTupleElement<I, AttributeTypes>::Type>*>( Array )->GetArrayForIndex( 0 )[ Index ] );
	}

	template <typename ViewType, uint32 I, typename TEnableIf<!TIsViewable<ViewType, I>::Value, int>::Type = 0>
	static ViewType Dispatch( const FMeshAttributeArraySetBase* Array, const int32 Index )
	{
		// Implementation when the attribute type is not convertible to the view type
		check( false );
		return ViewType();
	}

	// Build jump table at compile time, a separate instantiation of Dispatch for each view type
	template <typename ViewType, uint32... Is>
	static constexpr JumpTableType<ViewType> MakeJumpTable( TIntegerSequence< uint32, Is...> )
	{
		return JumpTableType<ViewType>( Dispatch<ViewType, Is>... );
	}
}

template <typename ViewType>
FORCEINLINE ViewType TMeshAttributesViewBase<ViewType>::GetByIndex( const int32 ElementIndex ) const
{
	static constexpr AttributesViewGetImpl::JumpTableType<ViewType>
		JumpTable = AttributesViewGetImpl::MakeJumpTable<ViewType>( TMakeIntegerSequence<uint32, TTupleArity<AttributeTypes>::Value>() );

	return JumpTable.Fns[ ArrayPtr->GetType() ]( ArrayPtr, ElementIndex );
}


/**
 * Implementation for TMeshAttributesConstViewBase::Get(ElementIndex, AttributeIndex).
 */
namespace AttributesViewGetWithIndexImpl
{
	// Declare type of jump table used to dispatch functions
	template <typename ViewType>
	using JumpTableType = TJumpTable<ViewType( const FMeshAttributeArraySetBase*, int32, int32 ), TTupleArity<AttributeTypes>::Value>;

	// Define dispatch functions
	template <typename ViewType, uint32 I, typename TEnableIf<TIsViewable<ViewType, I>::Value, int>::Type = 0>
	static ViewType Dispatch( const FMeshAttributeArraySetBase* Array, const int32 ElementIndex, const int32 AttributeIndex )
	{
		return ViewType( static_cast<const TMeshAttributeArraySet<typename TTupleElement<I, AttributeTypes>::Type>*>( Array )->GetArrayForIndex( AttributeIndex )[ ElementIndex ] );
	}

	template <typename ViewType, uint32 I, typename TEnableIf<!TIsViewable<ViewType, I>::Value, int>::Type = 0>
	static ViewType Dispatch( const FMeshAttributeArraySetBase* Array, const int32 ElementIndex, const int32 AttributeIndex )
	{
		check( false );
		return ViewType();
	}

	// Build jump table at compile time, a separate instantiation of Dispatch for each attribute type
	template <typename ViewType, uint32... Is>
	static constexpr JumpTableType<ViewType> MakeJumpTable( TIntegerSequence< uint32, Is...> )
	{
		return JumpTableType<ViewType>( Dispatch<ViewType, Is>... );
	}
}

template <typename ViewType>
FORCEINLINE ViewType TMeshAttributesViewBase<ViewType>::GetByIndex( const int32 ElementIndex, const int32 AttributeIndex ) const
{
	static constexpr AttributesViewGetWithIndexImpl::JumpTableType<ViewType>
		JumpTable = AttributesViewGetWithIndexImpl::MakeJumpTable<ViewType>( TMakeIntegerSequence<uint32, TTupleArity<AttributeTypes>::Value>() );

	return JumpTable.Fns[ ArrayPtr->GetType() ]( ArrayPtr, ElementIndex, AttributeIndex );
}


/**
 * Implementation for TMeshAttributesViewBase::Set(ElementIndex, Value).
 */
namespace AttributesViewSetImpl
{
	// Declare type of jump table used to dispatch functions
	template <typename ViewType>
	using JumpTableType = TJumpTable<void( FMeshAttributeArraySetBase*, int32, const ViewType& ), TTupleArity<AttributeTypes>::Value>;

	// Define dispatch functions
	template <typename ViewType, uint32 I, typename TEnableIf<TIsViewable<ViewType, I>::Value, int>::Type = 0>
	static void Dispatch( FMeshAttributeArraySetBase* Array, const int32 Index, const ViewType& Value )
	{
		// Implementation when the attribute type is convertible to the view type
		using AttributeType = typename TTupleElement<I, AttributeTypes>::Type;
		static_cast<TMeshAttributeArraySet<AttributeType>*>( Array )->GetArrayForIndex( 0 )[ Index ] = AttributeType( Value );
	}

	template <typename ViewType, uint32 I, typename TEnableIf<!TIsViewable<ViewType, I>::Value, int>::Type = 0>
	static void Dispatch( FMeshAttributeArraySetBase* Array, const int32 Index, const ViewType& Value )
	{
		// Implementation when the attribute type is not convertible to the view type
		check( false );
	}

	// Build jump table at compile time, a separate instantiation of Dispatch for each view type
	template <typename ViewType, uint32... Is>
	static constexpr JumpTableType<ViewType> MakeJumpTable( TIntegerSequence< uint32, Is...> )
	{
		return JumpTableType<ViewType>( Dispatch<ViewType, Is>... );
	}
}

template <typename ViewType>
FORCEINLINE void TMeshAttributesViewBase<ViewType>::SetByIndex( const int32 ElementIndex, const ViewType& Value ) const
{
	static constexpr AttributesViewSetImpl::JumpTableType<ViewType>
		JumpTable = AttributesViewSetImpl::MakeJumpTable<ViewType>( TMakeIntegerSequence<uint32, TTupleArity<AttributeTypes>::Value>() );

	JumpTable.Fns[ ArrayPtr->GetType() ]( ArrayPtr, ElementIndex, Value );
}


/**
 * Implementation for TMeshAttributesViewBase::Set(ElementIndex, AttributeIndex, Value).
 */
namespace AttributesViewSetWithIndexImpl
{
	// Declare type of jump table used to dispatch functions
	template <typename ViewType>
	using JumpTableType = TJumpTable<void( FMeshAttributeArraySetBase*, int32, int32, const ViewType& ), TTupleArity<AttributeTypes>::Value>;

	// Define dispatch functions
	template <typename ViewType, uint32 I, typename TEnableIf<TIsViewable<ViewType, I>::Value, int>::Type = 0>
	static void Dispatch( FMeshAttributeArraySetBase* Array, const int32 ElementIndex, const int32 AttributeIndex, const ViewType& Value )
	{
		// Implementation when the attribute type is convertible to the view type
		using AttributeType = typename TTupleElement<I, AttributeTypes>::Type;
		static_cast<TMeshAttributeArraySet<AttributeType>*>( Array )->GetArrayForIndex( AttributeIndex )[ ElementIndex ] = AttributeType( Value );
	}

	template <typename ViewType, uint32 I, typename TEnableIf<!TIsViewable<ViewType, I>::Value, int>::Type = 0>
	static void Dispatch( FMeshAttributeArraySetBase* Array, const int32 ElementIndex, const int32 AttributeIndex, const ViewType& Value )
	{
		// Implementation when the attribute type is not convertible to the view type
		check( false );
	}

	// Build jump table at compile time, a separate instantiation of Dispatch for each view type
	template <typename ViewType, uint32... Is>
	static constexpr JumpTableType<ViewType> MakeJumpTable( TIntegerSequence< uint32, Is...> )
	{
		return JumpTableType<ViewType>( Dispatch<ViewType, Is>... );
	}
}

template <typename ViewType>
FORCEINLINE void TMeshAttributesViewBase<ViewType>::SetByIndex( const int32 ElementIndex, const int32 AttributeIndex, const ViewType& Value ) const
{
	static constexpr AttributesViewSetWithIndexImpl::JumpTableType<ViewType>
		JumpTable = AttributesViewSetWithIndexImpl::MakeJumpTable<ViewType>( TMakeIntegerSequence<uint32, TTupleArity<AttributeTypes>::Value>() );

	JumpTable.Fns[ ArrayPtr->GetType() ]( ArrayPtr, ElementIndex, AttributeIndex, Value );
}


/**
 * Implementation for TMeshAttributesViewBase::GetDefaultValue().
 */
namespace AttributesViewGetDefaultImpl
{
	// Declare type of jump table used to dispatch functions
	template <typename ViewType>
	using JumpTableType = TJumpTable<ViewType( const FMeshAttributeArraySetBase* ), TTupleArity<AttributeTypes>::Value>;

	// Define dispatch functions
	template <typename ViewType, uint32 I, typename TEnableIf<TIsViewable<ViewType, I>::Value, int>::Type = 0>
	static ViewType Dispatch( const FMeshAttributeArraySetBase* Array )
	{
		// Implementation when the attribute type is convertible to the view type
		return ViewType( static_cast<const TMeshAttributeArraySet<typename TTupleElement<I, AttributeTypes>::Type>*>( Array )->GetDefaultValue() );
	}

	template <typename ViewType, uint32 I, typename TEnableIf<!TIsViewable<ViewType, I>::Value, int>::Type = 0>
	static ViewType Dispatch( const FMeshAttributeArraySetBase* Array )
	{
		// Implementation when the attribute type is not convertible to the view type
		check( false );
		return ViewType();
	}

	// Build jump table at compile time, a separate instantiation of Dispatch for each view type
	template <typename ViewType, uint32... Is>
	static constexpr JumpTableType<ViewType> MakeJumpTable( TIntegerSequence< uint32, Is...> )
	{
		return JumpTableType<ViewType>( Dispatch<ViewType, Is>... );
	}
}

template <typename ViewType>
FORCEINLINE ViewType TMeshAttributesViewBase<ViewType>::GetDefaultValue() const
{
	static constexpr AttributesViewGetDefaultImpl::JumpTableType<ViewType>
		JumpTable = AttributesViewGetDefaultImpl::MakeJumpTable<ViewType>( TMakeIntegerSequence<uint32, TTupleArity<AttributeTypes>::Value>() );

	return JumpTable.Fns[ ArrayPtr->GetType() ]( ArrayPtr );
}
