// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.


/*=============================================================================================
	HTML5PlatformAtomics.h: HTML5 platform Atomics functions
==============================================================================================*/

#pragma once
#include "GenericPlatform/GenericPlatformAtomics.h"

#ifdef __EMSCRIPTEN_PTHREADS__

#include <emscripten/threading.h>


// TODO: EMSCRITPEN_TOOLCHAIN_UPGRADE_CHECK
// TODO: USE "Clang/ClangPlatformAtomics.h" when HTML5 toolchain supports:
// int64 InterlockedExchange(...) & int64 InterlockedCompareExchange(...)
// without neededing emscripten_atomic_exchange_u64() & emscripten_atomic_cas_u64()
//
//#include "Clang/ClangPlatformAtomics.h"
//typedef FClangPlatformAtomics FPlatformAtomics;


struct CORE_API FHTML5PlatformAtomics	: public FGenericPlatformAtomics
{
	static FORCEINLINE int8 InterlockedIncrement(volatile int8* Value)
	{
		return __sync_fetch_and_add(Value, 1) + 1;
	}

	static FORCEINLINE int16 InterlockedIncrement(volatile int16* Value)
	{
		return __sync_fetch_and_add(Value, 1) + 1;
	}

	static FORCEINLINE int32 InterlockedIncrement(volatile int32* Value)
	{
		return __sync_fetch_and_add(Value, 1) + 1;
	}

	static FORCEINLINE int64 InterlockedIncrement(volatile int64* Value)
	{
		return __sync_fetch_and_add(Value, 1) + 1;
	}

	static FORCEINLINE int8 InterlockedDecrement(volatile int8* Value)
	{
		return __sync_fetch_and_sub(Value, 1) - 1;
	}

	static FORCEINLINE int16 InterlockedDecrement(volatile int16* Value)
	{
		return __sync_fetch_and_sub(Value, 1) - 1;
	}

	static FORCEINLINE int32 InterlockedDecrement(volatile int32* Value)
	{
		return __sync_fetch_and_sub(Value, 1) - 1;
	}

	static FORCEINLINE int64 InterlockedDecrement(volatile int64* Value)
	{
		return __sync_fetch_and_sub(Value, 1) - 1;
	}

	static FORCEINLINE int8 InterlockedAdd(volatile int8* Value, int8 Amount)
	{
		return __sync_fetch_and_add(Value, Amount);
	}

	static FORCEINLINE int16 InterlockedAdd(volatile int16* Value, int16 Amount)
	{
		return __sync_fetch_and_add(Value, Amount);
	}

	static FORCEINLINE int32 InterlockedAdd(volatile int32* Value, int32 Amount)
	{
		return __sync_fetch_and_add(Value, Amount);
	}

	static FORCEINLINE int64 InterlockedAdd(volatile int64* Value, int64 Amount)
	{
		return __sync_fetch_and_add(Value, Amount);
	}

	static FORCEINLINE int8 InterlockedExchange(volatile int8* Value, int8 Exchange)
	{
		return __sync_lock_test_and_set(Value, Exchange);
	}

	static FORCEINLINE int16 InterlockedExchange(volatile int16* Value, int16 Exchange)
	{
		return __sync_lock_test_and_set(Value, Exchange);
	}

	static FORCEINLINE int32 InterlockedExchange(volatile int32* Value, int32 Exchange)
	{
		return __sync_lock_test_and_set(Value, Exchange);
	}

	static FORCEINLINE int64 InterlockedExchange(volatile int64* Value, int64 Exchange)
	{
		return emscripten_atomic_exchange_u64((void*)Value, Exchange);
//		return __sync_lock_test_and_set(Value, Exchange);
	}

	static FORCEINLINE void* InterlockedExchangePtr(void*volatile* Dest, void* Exchange)
	{
		return __sync_lock_test_and_set(Dest, Exchange);
	}

	static FORCEINLINE int8 InterlockedCompareExchange(volatile int8* Dest, int8 Exchange, int8 Comperand)
	{
		return __sync_val_compare_and_swap(Dest, Comperand, Exchange);
	}

	static FORCEINLINE int16 InterlockedCompareExchange(volatile int16* Dest, int16 Exchange, int16 Comperand)
	{
		return __sync_val_compare_and_swap(Dest, Comperand, Exchange);
	}

	static FORCEINLINE int32 InterlockedCompareExchange(volatile int32* Dest, int32 Exchange, int32 Comperand)
	{
		return __sync_val_compare_and_swap(Dest, Comperand, Exchange);
	}

	static FORCEINLINE int64 InterlockedCompareExchange(volatile int64* Dest, int64 Exchange, int64 Comperand)
	{
		return emscripten_atomic_cas_u64((void*)Dest, Comperand, Exchange);
//		return __sync_val_compare_and_swap(Dest, Comperand, Exchange);
	}

	static FORCEINLINE int8 InterlockedAnd(volatile int8* Value, const int8 AndValue)
	{
		return __sync_fetch_and_and(Value, AndValue);
	}

	static FORCEINLINE int16 InterlockedAnd(volatile int16* Value, const int16 AndValue)
	{
		return __sync_fetch_and_and(Value, AndValue);
	}

	static FORCEINLINE int32 InterlockedAnd(volatile int32* Value, const int32 AndValue)
	{
		return __sync_fetch_and_and(Value, AndValue);
	}

	static FORCEINLINE int64 InterlockedAnd(volatile int64* Value, const int64 AndValue)
	{
		return __sync_fetch_and_and(Value, AndValue);
	}

	static FORCEINLINE int8 InterlockedOr(volatile int8* Value, const int8 OrValue)
	{
		return __sync_fetch_and_or(Value, OrValue);
	}

	static FORCEINLINE int16 InterlockedOr(volatile int16* Value, const int16 OrValue)
	{
		return __sync_fetch_and_or(Value, OrValue);
	}

	static FORCEINLINE int32 InterlockedOr(volatile int32* Value, const int32 OrValue)
	{
		return __sync_fetch_and_or(Value, OrValue);
	}

	static FORCEINLINE int64 InterlockedOr(volatile int64* Value, const int64 OrValue)
	{
		return __sync_fetch_and_or(Value, OrValue);
	}

	static FORCEINLINE int8 InterlockedXor(volatile int8* Value, const int8 XorValue)
	{
		return __sync_fetch_and_xor(Value, XorValue);
	}

	static FORCEINLINE int16 InterlockedXor(volatile int16* Value, const int16 XorValue)
	{
		return __sync_fetch_and_xor(Value, XorValue);
	}

	static FORCEINLINE int32 InterlockedXor(volatile int32* Value, const int32 XorValue)
	{
		return __sync_fetch_and_xor(Value, XorValue);
	}

	static FORCEINLINE int64 InterlockedXor(volatile int64* Value, const int64 XorValue)
	{
		return __sync_fetch_and_xor(Value, XorValue);
	}

	static FORCEINLINE int8 AtomicRead(volatile const int8* Src)
	{
		int8 Result;
		__atomic_load((volatile int8*)Src, &Result, __ATOMIC_SEQ_CST);
		return Result;
	}

	static FORCEINLINE int16 AtomicRead(volatile const int16* Src)
	{
		int16 Result;
		__atomic_load((volatile int16*)Src, &Result, __ATOMIC_SEQ_CST);
		return Result;
	}

	static FORCEINLINE int32 AtomicRead(volatile const int32* Src)
	{
		int32 Result;
		__atomic_load((volatile int32*)Src, &Result, __ATOMIC_SEQ_CST);
		return Result;
	}

	static FORCEINLINE int64 AtomicRead(volatile const int64* Src)
	{
		int64 Result;
		__atomic_load((volatile int64*)Src, &Result, __ATOMIC_SEQ_CST);
		return Result;
	}

	static FORCEINLINE int8 AtomicRead_Relaxed(volatile const int8* Src)
	{
		int8 Result;
		__atomic_load((volatile int8*)Src, &Result, __ATOMIC_RELAXED);
		return Result;
	}

	static FORCEINLINE int16 AtomicRead_Relaxed(volatile const int16* Src)
	{
		int16 Result;
		__atomic_load((volatile int16*)Src, &Result, __ATOMIC_RELAXED);
		return Result;
	}

	static FORCEINLINE int32 AtomicRead_Relaxed(volatile const int32* Src)
	{
		int32 Result;
		__atomic_load((volatile int32*)Src, &Result, __ATOMIC_RELAXED);
		return Result;
	}

	static FORCEINLINE int64 AtomicRead_Relaxed(volatile const int64* Src)
	{
		int64 Result;
		__atomic_load((volatile int64*)Src, &Result, __ATOMIC_RELAXED);
		return Result;
	}

	static FORCEINLINE void AtomicStore(volatile int8* Src, int8 Val)
	{
		__atomic_store((volatile int8*)Src, &Val, __ATOMIC_SEQ_CST);
	}

	static FORCEINLINE void AtomicStore(volatile int16* Src, int16 Val)
	{
		__atomic_store((volatile int16*)Src, &Val, __ATOMIC_SEQ_CST);
	}

	static FORCEINLINE void AtomicStore(volatile int32* Src, int32 Val)
	{
		__atomic_store((volatile int32*)Src, &Val, __ATOMIC_SEQ_CST);
	}

	static FORCEINLINE void AtomicStore(volatile int64* Src, int64 Val)
	{
		__atomic_store((volatile int64*)Src, &Val, __ATOMIC_SEQ_CST);
	}

	static FORCEINLINE void AtomicStore_Relaxed(volatile int8* Src, int8 Val)
	{
		__atomic_store((volatile int8*)Src, &Val, __ATOMIC_RELAXED);
	}

	static FORCEINLINE void AtomicStore_Relaxed(volatile int16* Src, int16 Val)
	{
		__atomic_store((volatile int16*)Src, &Val, __ATOMIC_RELAXED);
	}

	static FORCEINLINE void AtomicStore_Relaxed(volatile int32* Src, int32 Val)
	{
		__atomic_store((volatile int32*)Src, &Val, __ATOMIC_RELAXED);
	}

	static FORCEINLINE void AtomicStore_Relaxed(volatile int64* Src, int64 Val)
	{
		__atomic_store((volatile int64*)Src, &Val, __ATOMIC_RELAXED);
	}

	static FORCEINLINE void* InterlockedCompareExchangePointer(void*volatile* Dest, void* Exchange, void* Comperand)
	{
		return __sync_val_compare_and_swap(Dest, Comperand, Exchange);
	}
};

typedef FHTML5PlatformAtomics FPlatformAtomics;


#else // #ifdef __EMSCRIPTEN_PTHREADS__

/**
 * HTML5 implementation of the Atomics OS functions (no threads version)
 */
struct CORE_API FHTML5PlatformAtomics	: public FGenericPlatformAtomics
{
	static FORCEINLINE int8 InterlockedIncrement( volatile int8* Value )
	{
		const int8 TempValue = static_cast<int8>(static_cast<uint8>(*Value) + 1U);
		*Value = TempValue;
		return TempValue;
	}

	static FORCEINLINE int16 InterlockedIncrement( volatile int16* Value )
	{
		const int16 TempValue = static_cast<int16>(static_cast<uint16>(*Value) + 1U);
		*Value = TempValue;
		return TempValue;
	}

	static FORCEINLINE int32 InterlockedIncrement( volatile int32* Value )
	{
		const int32 TempValue = static_cast<int32>(static_cast<uint32>(*Value) + 1U);
		*Value = TempValue;
		return TempValue;
	}

	static FORCEINLINE int64 InterlockedIncrement( volatile int64* Value )
	{
		const int64 TempValue = static_cast<int64>(static_cast<uint64>(*Value) + 1ULL);
		*Value = TempValue;
		return TempValue;
	}

	static FORCEINLINE int8 InterlockedDecrement( volatile int8* Value )
	{
		const int8 TempValue = static_cast<int8>(static_cast<uint8>(*Value) - 1U);
		*Value = TempValue;
		return TempValue;
	}

	static FORCEINLINE int16 InterlockedDecrement( volatile int16* Value )
	{
		const int16 TempValue = static_cast<int16>(static_cast<uint16>(*Value) - 1U);
		*Value = TempValue;
		return TempValue;
	}

	static FORCEINLINE int32 InterlockedDecrement( volatile int32* Value )
	{
		const int32 TempValue = static_cast<int32>(static_cast<uint32>(*Value) - 1U);
		*Value = TempValue;
		return TempValue;
	}

	static FORCEINLINE int64 InterlockedDecrement( volatile int64* Value )
	{
		const int64 TempValue = static_cast<int64>(static_cast<uint64>(*Value) - 1ULL);
		*Value = TempValue;
		return TempValue;
	}

	static FORCEINLINE int8 InterlockedAdd( volatile int8* Value, int8 Amount )
	{
		int8 Result = *Value;
		*Value = static_cast<int8>(static_cast<uint8>(Result) + static_cast<uint8>(Amount));
		return Result;
	}

	static FORCEINLINE int16 InterlockedAdd( volatile int16* Value, int16 Amount )
	{
		int16 Result = *Value;
		*Value = static_cast<int16>(static_cast<uint16>(Result) + static_cast<uint16>(Amount));
		return Result;
	}

	static FORCEINLINE int32 InterlockedAdd( volatile int32* Value, int32 Amount )
	{
		int32 Result = *Value;
		*Value = static_cast<int32>(static_cast<uint32>(Result) + static_cast<uint32>(Amount));
		return Result;
	}

	static FORCEINLINE int64 InterlockedAdd( volatile int64* Value, int64 Amount )
	{
		int64 Result = *Value;
		*Value = static_cast<int64>(static_cast<uint64>(Result) + static_cast<uint64>(Amount));
		return Result;
	}

	static FORCEINLINE int8 InterlockedExchange( volatile int8* Value, int8 Exchange )
	{
		int8 Result = *Value;
		*Value = Exchange;
		return Result;
	}

	static FORCEINLINE int16 InterlockedExchange( volatile int16* Value, int16 Exchange )
	{
		int16 Result = *Value;
		*Value = Exchange;
		return Result;
	}

	static FORCEINLINE int32 InterlockedExchange( volatile int32* Value, int32 Exchange )
	{
		int32 Result = *Value;
		*Value = Exchange;
		return Result;
	}

	static FORCEINLINE int64 InterlockedExchange( volatile int64* Value, int64 Exchange )
	{
		int64 Result = *Value;
		*Value = Exchange;
		return Result;
	}

	static FORCEINLINE void* InterlockedExchangePtr( void*volatile* Dest, void* Exchange )
	{
		void* Result = *Dest;
		*Dest = Exchange;
		return Result;
	}

	static FORCEINLINE int8 InterlockedCompareExchange( volatile int8* Dest, int8 Exchange, int8 Comperand )
	{
		int8 Result = *Dest;
		if (Result == Comperand)
		{
			*Dest = Exchange;
		}
		return Result;
	}

	static FORCEINLINE int16 InterlockedCompareExchange( volatile int16* Dest, int16 Exchange, int16 Comperand )
	{
		int16 Result = *Dest;
		if (Result == Comperand)
		{
			*Dest = Exchange;
		}
		return Result;
	}

	static FORCEINLINE int32 InterlockedCompareExchange( volatile int32* Dest, int32 Exchange, int32 Comperand )
	{
		int32 Result = *Dest;
		if (Result == Comperand)
		{
			*Dest = Exchange;
		}
		return Result;
	}

	static FORCEINLINE int64 InterlockedCompareExchange( volatile int64* Dest, int64 Exchange, int64 Comperand )
	{
		int64 Result = *Dest;
		if (Result == Comperand)
		{
			*Dest = Exchange;
		}
		return Result;
	}

	static FORCEINLINE int8 InterlockedAnd(volatile int8* Value, const int8 AndValue)
	{
		const int8 Result = *Value;
		*Value = Result & AndValue;
		return Result;
	}

	static FORCEINLINE int16 InterlockedAnd(volatile int16* Value, const int16 AndValue)
	{
		const int16 Result = *Value;
		*Value = Result & AndValue;
		return Result;
	}

	static FORCEINLINE int32 InterlockedAnd(volatile int32* Value, const int32 AndValue)
	{
		const int32 Result = *Value;
		*Value = Result & AndValue;
		return Result;
	}

	static FORCEINLINE int64 InterlockedAnd(volatile int64* Value, const int64 AndValue)
	{
		const int64 Result = *Value;
		*Value = Result & AndValue;
		return Result;
	}

	static FORCEINLINE int8 InterlockedOr(volatile int8* Value, const int8 OrValue)
	{
		const int8 Result = *Value;
		*Value = Result | OrValue;
		return Result;
	}

	static FORCEINLINE int16 InterlockedOr(volatile int16* Value, const int16 OrValue)
	{
		const int16 Result = *Value;
		*Value = Result | OrValue;
		return Result;
	}

	static FORCEINLINE int32 InterlockedOr(volatile int32* Value, const int32 OrValue)
	{
		const int32 Result = *Value;
		*Value = Result | OrValue;
		return Result;
	}

	static FORCEINLINE int64 InterlockedOr(volatile int64* Value, const int64 OrValue)
	{
		const int64 Result = *Value;
		*Value = Result | OrValue;
		return Result;
	}

	static FORCEINLINE int8 InterlockedXor(volatile int8* Value, const int8 XorValue)
	{
		const int8 Result = *Value;
		*Value = Result ^ XorValue;
		return Result;
	}

	static FORCEINLINE int16 InterlockedXor(volatile int16* Value, const int16 XorValue)
	{
		const int16 Result = *Value;
		*Value = Result ^ XorValue;
		return Result;
	}

	static FORCEINLINE int32 InterlockedXor(volatile int32* Value, const int32 XorValue)
	{
		const int32 Result = *Value;
		*Value = Result ^ XorValue;
		return Result;
	}

	static FORCEINLINE int64 InterlockedXor(volatile int64* Value, const int64 XorValue)
	{
		const int64 Result = *Value;
		*Value = Result ^ XorValue;
		return Result;
	}

	static FORCEINLINE int8 AtomicRead(volatile const int8* Src)
	{
		return *Src;
	}

	static FORCEINLINE int16 AtomicRead(volatile const int16* Src)
	{
		return *Src;
	}

	static FORCEINLINE int32 AtomicRead(volatile const int32* Src)
	{
		return *Src;
	}

	static FORCEINLINE int64 AtomicRead(volatile const int64* Src)
	{
		return *Src;
	}

	static FORCEINLINE int8 AtomicRead_Relaxed(volatile const int8* Src)
	{
		return *Src;
	}

	static FORCEINLINE int16 AtomicRead_Relaxed(volatile const int16* Src)
	{
		return *Src;
	}

	static FORCEINLINE int32 AtomicRead_Relaxed(volatile const int32* Src)
	{
		return *Src;
	}

	static FORCEINLINE int64 AtomicRead_Relaxed(volatile const int64* Src)
	{
		return *Src;
	}

	static FORCEINLINE void AtomicStore(volatile int8* Src, int8 Val)
	{
		*Src = Val;
	}

	static FORCEINLINE void AtomicStore(volatile int16* Src, int16 Val)
	{
		*Src = Val;
	}

	static FORCEINLINE void AtomicStore(volatile int32* Src, int32 Val)
	{
		*Src = Val;
	}

	static FORCEINLINE void AtomicStore(volatile int64* Src, int64 Val)
	{
		*Src = Val;
	}

	static FORCEINLINE void AtomicStore_Relaxed(volatile int8* Src, int8 Val)
	{
		*Src = Val;
	}

	static FORCEINLINE void AtomicStore_Relaxed(volatile int16* Src, int16 Val)
	{
		*Src = Val;
	}

	static FORCEINLINE void AtomicStore_Relaxed(volatile int32* Src, int32 Val)
	{
		*Src = Val;
	}

	static FORCEINLINE void AtomicStore_Relaxed(volatile int64* Src, int64 Val)
	{
		*Src = Val;
	}

	static FORCEINLINE void* InterlockedCompareExchangePointer( void*volatile* Dest, void* Exchange, void* Comperand )
	{
		void* Result = *Dest;
		if (Result == Comperand)
		{
			*Dest = Exchange;
		}
		return Result;
	}
};


typedef FHTML5PlatformAtomics FPlatformAtomics;

#endif // #else // #ifdef __EMSCRIPTEN_PTHREADS__
