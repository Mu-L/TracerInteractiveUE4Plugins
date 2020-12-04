// Copyright Epic Games, Inc. All Rights Reserved.

#include "UsdWrappers/UsdPrim.h"

#include "USDMemory.h"

#include "UsdWrappers/SdfPath.h"
#include "UsdWrappers/UsdAttribute.h"
#include "UsdWrappers/UsdStage.h"

#if USE_USD_SDK

#include "USDIncludesStart.h"
	#include "pxr/usd/usd/attribute.h"
	#include "pxr/usd/usd/prim.h"
#include "USDIncludesEnd.h"

#endif // #if USE_USD_SDK

namespace UE
{
	namespace Internal
	{
		class FUsdPrimImpl
		{
		public:
			FUsdPrimImpl() = default;

#if USE_USD_SDK
			explicit FUsdPrimImpl( const pxr::UsdPrim& InUsdPrim )
				: PxrUsdPrim( InUsdPrim )
			{
			}

			explicit FUsdPrimImpl( pxr::UsdPrim&& InUsdPrim )
				: PxrUsdPrim( MoveTemp( InUsdPrim ) )
			{
			}

			TUsdStore< pxr::UsdPrim > PxrUsdPrim;
#endif // #if USE_USD_SDK
		};
	}

	FUsdPrim::FUsdPrim()
	{
		FScopedUnrealAllocs UnrealAllocs;
		Impl = MakeUnique< Internal::FUsdPrimImpl >();
	}

	FUsdPrim::FUsdPrim( const FUsdPrim& Other )
	{
#if USE_USD_SDK
		FScopedUnrealAllocs UnrealAllocs;
		Impl = MakeUnique< Internal::FUsdPrimImpl >( Other.Impl->PxrUsdPrim.Get() );
#endif // #if USE_USD_SDK
	}

	FUsdPrim::FUsdPrim( FUsdPrim&& Other ) = default;

	FUsdPrim::~FUsdPrim()
	{
		FScopedUnrealAllocs UnrealAllocs;
		Impl.Reset();
	}

	FUsdPrim& FUsdPrim::operator=( const FUsdPrim& Other )
	{
#if USE_USD_SDK
		FScopedUnrealAllocs UnrealAllocs;
		Impl = MakeUnique< Internal::FUsdPrimImpl >(  Other.Impl->PxrUsdPrim.Get() );
#endif // #if USE_USD_SDK
		return *this;
	}

	FUsdPrim& FUsdPrim::operator=( FUsdPrim&& Other )
	{
		FScopedUnrealAllocs UnrealAllocs;
		Impl = MoveTemp(  Other.Impl );

		return *this;
	}

	FUsdPrim::operator bool() const
	{
#if USE_USD_SDK
		return (bool)Impl->PxrUsdPrim.Get();
#else
		return false;
#endif // #if USE_USD_SDK
	}

	bool FUsdPrim::operator==( const FUsdPrim& Other ) const
	{
#if USE_USD_SDK
		return Impl->PxrUsdPrim.Get() == Other.Impl->PxrUsdPrim.Get();
#else
		return false;
#endif // #if USE_USD_SDK
	}

	bool FUsdPrim::operator!=( const FUsdPrim& Other ) const
	{
		return !( *this == Other );
	}

#if USE_USD_SDK
	FUsdPrim::FUsdPrim( const pxr::UsdPrim& InUsdPrim )
	{
		FScopedUnrealAllocs UnrealAllocs;
		Impl =  MakeUnique< Internal::FUsdPrimImpl >( InUsdPrim );
	}

	FUsdPrim::FUsdPrim( pxr::UsdPrim&& InUsdPrim )
	{
		FScopedUnrealAllocs UnrealAllocs;
		Impl = MakeUnique< Internal::FUsdPrimImpl >( MoveTemp( InUsdPrim ) );
	}

	FUsdPrim& FUsdPrim::operator=( const pxr::UsdPrim& InUsdPrim )
	{
		FScopedUnrealAllocs UnrealAllocs;
		Impl = MakeUnique< Internal::FUsdPrimImpl >( InUsdPrim );
		return *this;
	}

	FUsdPrim& FUsdPrim::operator=( pxr::UsdPrim&& InUsdPrim )
	{
		FScopedUnrealAllocs UnrealAllocs;
		Impl = MakeUnique< Internal::FUsdPrimImpl >( MoveTemp( InUsdPrim ) );
		return *this;
	}

	FUsdPrim::operator pxr::UsdPrim&()
	{
		return Impl->PxrUsdPrim.Get();
	}

	FUsdPrim::operator const pxr::UsdPrim&() const
	{
		return Impl->PxrUsdPrim.Get();
	}
#endif // #if USE_USD_SDK

	bool FUsdPrim::IsValid() const
	{
#if USE_USD_SDK
		return Impl->PxrUsdPrim.Get().IsValid();
#else
		return false;
#endif // #if USE_USD_SDK
	}

	bool FUsdPrim::IsPseudoRoot() const
	{
#if USE_USD_SDK
		return Impl->PxrUsdPrim.Get().IsPseudoRoot();
#else
		return false;
#endif // #if USE_USD_SDK
	}

	bool FUsdPrim::IsModel() const
	{
#if USE_USD_SDK
		return Impl->PxrUsdPrim.Get().IsModel();
#else
		return false;
#endif // #if USE_USD_SDK
	}

	bool FUsdPrim::IsGroup() const
	{
#if USE_USD_SDK
		return Impl->PxrUsdPrim.Get().IsGroup();
#else
		return false;
#endif // #if USE_USD_SDK
	}

	const FSdfPath FUsdPrim::GetPrimPath() const
	{
#if USE_USD_SDK
		return FSdfPath( Impl->PxrUsdPrim.Get().GetPrimPath() );
#else
		return FSdfPath();
#endif // #if USE_USD_SDK
	}

	FUsdStage FUsdPrim::GetStage() const
	{
#if USE_USD_SDK
		return FUsdStage( Impl->PxrUsdPrim.Get().GetStage() );
#else
		return FUsdStage();
#endif // #if USE_USD_SDK
	}

	FName FUsdPrim::GetName() const
	{
#if USE_USD_SDK
		return FName( ANSI_TO_TCHAR( Impl->PxrUsdPrim.Get().GetName().GetString().c_str() ) );
#else
		return FName();
#endif // #if USE_USD_SDK
	}

	FName FUsdPrim::GetTypeName() const
	{
#if USE_USD_SDK
		return FName( ANSI_TO_TCHAR( Impl->PxrUsdPrim.Get().GetTypeName().GetString().c_str() ) );
#else
		return FName();
#endif // #if USE_USD_SDK
	}

	FUsdPrim FUsdPrim::GetParent() const
	{
#if USE_USD_SDK
		return FUsdPrim( Impl->PxrUsdPrim.Get().GetParent() );
#else
		return FUsdPrim();
#endif // #if USE_USD_SDK
	}

	TArray< FUsdPrim > FUsdPrim::GetChildren() const
	{
		TArray< FUsdPrim > Children;

#if USE_USD_SDK
		FScopedUsdAllocs UsdAllocs;

		pxr::UsdPrimSiblingRange PrimChildren = Impl->PxrUsdPrim.Get().GetChildren();

		for ( const pxr::UsdPrim& Child : PrimChildren )
		{
			Children.Emplace( Child );
		}
#endif // #if USE_USD_SDK

		return Children;
	}

	TArray< FUsdPrim > FUsdPrim::GetFilteredChildren( bool bTraverseInstanceProxies ) const
	{
		TArray< FUsdPrim > Children;

#if USE_USD_SDK
		FScopedUsdAllocs UsdAllocs;

		pxr::Usd_PrimFlagsPredicate Predicate = pxr::UsdPrimDefaultPredicate;

		if ( bTraverseInstanceProxies )
		{
			Predicate = pxr::UsdTraverseInstanceProxies( Predicate ) ;
		}

		pxr::UsdPrimSiblingRange PrimChildren = Impl->PxrUsdPrim.Get().GetFilteredChildren( Predicate );

		for ( const pxr::UsdPrim& Child : PrimChildren )
		{
			Children.Emplace( Child );
		}
#endif // #if USE_USD_SDK

		return Children;
	}

	bool FUsdPrim::HasVariantSets() const
	{
#if USE_USD_SDK
		return Impl->PxrUsdPrim.Get().HasVariantSets();
#else
		return false;
#endif // #if USE_USD_SDK
	}

	bool FUsdPrim::HasAuthoredReferences() const
	{
#if USE_USD_SDK
		return Impl->PxrUsdPrim.Get().HasAuthoredReferences();
#else
		return false;
#endif // #if USE_USD_SDK
	}

	bool FUsdPrim::HasPayload() const
	{
#if USE_USD_SDK
		return Impl->PxrUsdPrim.Get().HasPayload();
#else
		return false;
#endif // #if USE_USD_SDK
	}

	bool FUsdPrim::IsLoaded() const
	{
#if USE_USD_SDK
		return Impl->PxrUsdPrim.Get().IsLoaded();
#else
		return false;
#endif // #if USE_USD_SDK
	}

	void FUsdPrim::Load()
	{
#if USE_USD_SDK
		Impl->PxrUsdPrim.Get().Load();
#endif // #if USE_USD_SDK
	}

	void FUsdPrim::Unload()
	{
#if USE_USD_SDK
		Impl->PxrUsdPrim.Get().Unload();
#endif // #if USE_USD_SDK
	}

	TArray< FUsdAttribute > FUsdPrim::GetAttributes() const
	{
		TArray< FUsdAttribute > Attributes;

#if USE_USD_SDK
		FScopedUsdAllocs UsdAllocs;

		for ( const pxr::UsdAttribute& Attribute : Impl->PxrUsdPrim.Get().GetAttributes() )
		{
			Attributes.Emplace( Attribute );
		}
#endif // #if USE_USD_SDK

		return Attributes;
	}

	FUsdAttribute FUsdPrim::GetAttribute(const TCHAR* AttrName) const
	{
#if USE_USD_SDK
		FScopedUsdAllocs UsdAllocs;

		return FUsdAttribute( Impl->PxrUsdPrim.Get().GetAttribute( pxr::TfToken( TCHAR_TO_ANSI( AttrName ) ) ) );
#else
		return FUsdAttribute{};
#endif // #if USE_USD_SDK
	}
}
