/*
    Copyright 2005-2012 Intel Corporation.  All Rights Reserved.

    The source code contained or described herein and all documents related
    to the source code ("Material") are owned by Intel Corporation or its
    suppliers or licensors.  Title to the Material remains with Intel
    Corporation or its suppliers and licensors.  The Material is protected
    by worldwide copyright laws and treaty provisions.  No part of the
    Material may be used, copied, reproduced, modified, published, uploaded,
    posted, transmitted, distributed, or disclosed in any way without
    Intel's prior express written permission.

    No license under any patent, copyright, trade secret or other
    intellectual property right is granted to or conferred upon you by
    disclosure or delivery of the Materials, either expressly, by
    implication, inducement, estoppel or otherwise.  Any license under such
    intellectual property rights must be express and approved by Intel in
    writing.
*/

#ifndef __TBB_machine_H
#define __TBB_machine_H

/** This header provides basic platform abstraction layer by hooking up appropriate
    architecture/OS/compiler specific headers from the /include/tbb/machine directory.
    If a plug-in header does not implement all the required APIs, it must specify
    the missing ones by setting one or more of the following macros:

    __TBB_USE_GENERIC_PART_WORD_CAS
    __TBB_USE_GENERIC_PART_WORD_FETCH_ADD
    __TBB_USE_GENERIC_PART_WORD_FETCH_STORE
    __TBB_USE_GENERIC_FETCH_ADD
    __TBB_USE_GENERIC_FETCH_STORE
    __TBB_USE_GENERIC_DWORD_FETCH_ADD
    __TBB_USE_GENERIC_DWORD_FETCH_STORE
    __TBB_USE_GENERIC_HALF_FENCED_LOAD_STORE
    __TBB_USE_GENERIC_FULL_FENCED_LOAD_STORE
    __TBB_USE_GENERIC_RELAXED_LOAD_STORE
    __TBB_USE_FETCHSTORE_AS_FULL_FENCED_STORE

    In this case tbb_machine.h will add missing functionality based on a minimal set 
    of APIs that are required to be implemented by all plug-n headers as described
    further.
    Note that these generic implementations may be sub-optimal for a particular
    architecture, and thus should be relied upon only after careful evaluation
    or as the last resort.

    Additionally __TBB_64BIT_ATOMICS can be set to 0 on a 32-bit architecture to
    indicate that the port is not going to support double word atomics. It may also
    be set to 1 explicitly, though normally this is not necessary as tbb_machine.h
    will set it automatically.

    __TBB_BIG_ENDIAN macro can be defined by the implementation as well.
    It is used only if the __TBB_USE_GENERIC_PART_WORD_CAS is set.
    Possible values are:
       -  1 if the system is big endian,
       -  0 if it is little endian,
       -  or -1 to explicitly state that __TBB_USE_GENERIC_PART_WORD_CAS can not be used.
    -1 should be used when it is known in advance that endianness can change in run time
    or it is  not simple big or little but something more complex.
    The system will try to detect it in run time if it is not set(in assumption that it
    is either a big or little one).

    Prerequisites for each architecture port
    ----------------------------------------
    The following functions and macros have no generic implementation. Therefore they must be
    implemented in each machine architecture specific header either as a conventional
    function or as a functional macro.

    __TBB_WORDSIZE
        This is the size of machine word in bytes, i.e. for 32 bit systems it
        should be defined to 4.

    __TBB_Yield()
        Signals OS that the current thread is willing to relinquish the remainder
        of its time quantum.

    __TBB_full_memory_fence()
        Must prevent all memory operations from being reordered across it (both
        by hardware and compiler). All such fences must be totally ordered (or
        sequentially consistent).

    __TBB_machine_cmpswp4( volatile void *ptr, int32_t value, int32_t comparand )
        Must be provided if __TBB_USE_FENCED_ATOMICS is not set.

    __TBB_machine_cmpswp8( volatile void *ptr, int32_t value, int64_t comparand )
        Must be provided for 64-bit architectures if __TBB_USE_FENCED_ATOMICS is not set,
        and for 32-bit architectures if __TBB_64BIT_ATOMICS is set

    __TBB_machine_<op><S><fence>(...), where
        <op> = {cmpswp, fetchadd, fetchstore}
        <S> = {1, 2, 4, 8}
        <fence> = {full_fence, acquire, release, relaxed}
        Must be provided if __TBB_USE_FENCED_ATOMICS is set.

    __TBB_control_consistency_helper()
        Bridges the memory-semantics gap between architectures providing only
        implicit C++0x "consume" semantics (like Power Architecture) and those
        also implicitly obeying control dependencies (like IA-64).
        It must be used only in conditional code where the condition is itself
        data-dependent, and will then make subsequent code behave as if the
        original data dependency were acquired.
        It needs only a compiler fence where implied by the architecture
        either specifically (like IA-64) or because generally stronger "acquire"
        semantics are enforced (like x86).
        It is always valid, though potentially suboptimal, to replace
        control with acquire on the load and then remove the helper.

    __TBB_acquire_consistency_helper(), __TBB_release_consistency_helper()
        Must be provided if __TBB_USE_GENERIC_HALF_FENCED_LOAD_STORE is set.
        Enforce acquire and release semantics in generic implementations of fenced
        store and load operations. Depending on the particular architecture/compiler
        combination they may be a hardware fence, a compiler fence, both or nothing.
 **/

#include "tbb_stddef.h"

namespace tbb {
namespace internal {

////////////////////////////////////////////////////////////////////////////////
// Overridable helpers declarations
//
// A machine/*.h file may choose to define these templates, otherwise it must
// request default implementation by setting appropriate __TBB_USE_GENERIC_XXX macro(s).
//
template <typename T, std::size_t S>
struct machine_load_store;

template <typename T, std::size_t S>
struct machine_load_store_relaxed;

template <typename T, std::size_t S>
struct machine_load_store_seq_cst;
//
// End of overridable helpers declarations
////////////////////////////////////////////////////////////////////////////////

template<size_t S> struct atomic_selector;

template<> struct atomic_selector<1> {
    typedef int8_t word;
    inline static word fetch_store ( volatile void* location, word value );
};

template<> struct atomic_selector<2> {
    typedef int16_t word;
    inline static word fetch_store ( volatile void* location, word value );
};

template<> struct atomic_selector<4> {
#if _MSC_VER && !_WIN64
    // Work-around that avoids spurious /Wp64 warnings
    typedef intptr_t word;
#else
    typedef int32_t word;
#endif
    inline static word fetch_store ( volatile void* location, word value );
};

template<> struct atomic_selector<8> {
    typedef int64_t word;
    inline static word fetch_store ( volatile void* location, word value );
};

}} // namespaces internal, tbb

#if _WIN32||_WIN64

#ifdef _MANAGED
#pragma managed(push, off)
#endif

    #if __MINGW64__ || __MINGW32__
        extern "C" __declspec(dllimport) int __stdcall SwitchToThread( void );
        #define __TBB_Yield()  SwitchToThread()
        #if (TBB_USE_GCC_BUILTINS && __TBB_GCC_BUILTIN_ATOMICS_PRESENT)
            #include "machine/gcc_generic.h"
        #elif __MINGW64__
            #include "machine/linux_intel64.h"
        #elif __MINGW32__
            #include "machine/linux_ia32.h"
        #endif
    #elif defined(_M_IX86)
        #include "machine/windows_ia32.h"
    #elif defined(_M_X64) 
        #include "machine/windows_intel64.h"
    #elif _XBOX
        #include "machine/xbox360_ppc.h"
    #endif

#ifdef _MANAGED
#pragma managed(pop)
#endif

#elif __TBB_DEFINE_MIC

    #include "machine/mic_common.h"
    #include "machine/linux_intel64.h"

#elif __linux__ || __FreeBSD__ || __NetBSD__

    #if (TBB_USE_GCC_BUILTINS && __TBB_GCC_BUILTIN_ATOMICS_PRESENT)
        #include "machine/gcc_generic.h"
    #elif __i386__
        #include "machine/linux_ia32.h"
    #elif __x86_64__
        #include "machine/linux_intel64.h"
    #elif __ia64__
        #include "machine/linux_ia64.h"
    #elif __powerpc__
        #include "machine/mac_ppc.h"
    #elif __TBB_GCC_BUILTIN_ATOMICS_PRESENT
        #include "machine/gcc_generic.h"
    #endif
    #include "machine/linux_common.h"

#elif __APPLE__

    #if __i386__
        #include "machine/linux_ia32.h"
    #elif __x86_64__
        #include "machine/linux_intel64.h"
    #elif __POWERPC__
        #include "machine/mac_ppc.h"
    #endif
    #include "machine/macos_common.h"

#elif _AIX

    #include "machine/ibm_aix51.h"

#elif __sun || __SUNPRO_CC

    #define __asm__ asm
    #define __volatile__ volatile

    #if __i386  || __i386__
        #include "machine/linux_ia32.h"
    #elif __x86_64__
        #include "machine/linux_intel64.h"
    #elif __sparc
        #include "machine/sunos_sparc.h"
    #endif
    #include <sched.h>

    #define __TBB_Yield() sched_yield()

#endif /* OS selection */

#ifndef __TBB_64BIT_ATOMICS
    #define __TBB_64BIT_ATOMICS 1
#endif

// Special atomic functions
#if __TBB_USE_FENCED_ATOMICS
    #define __TBB_machine_cmpswp1   __TBB_machine_cmpswp1full_fence
    #define __TBB_machine_cmpswp2   __TBB_machine_cmpswp2full_fence
    #define __TBB_machine_cmpswp4   __TBB_machine_cmpswp4full_fence
    #define __TBB_machine_cmpswp8   __TBB_machine_cmpswp8full_fence

    #if __TBB_WORDSIZE==8
        #define __TBB_machine_fetchadd8             __TBB_machine_fetchadd8full_fence
        #define __TBB_machine_fetchstore8           __TBB_machine_fetchstore8full_fence
        #define __TBB_FetchAndAddWrelease(P,V)      __TBB_machine_fetchadd8release(P,V)
        #define __TBB_FetchAndIncrementWacquire(P)  __TBB_machine_fetchadd8acquire(P,1)
        #define __TBB_FetchAndDecrementWrelease(P)  __TBB_machine_fetchadd8release(P,(-1))
    #else
        #error Define macros for 4-byte word, similarly to the above __TBB_WORDSIZE==8 branch.
    #endif /* __TBB_WORDSIZE==4 */
#else /* !__TBB_USE_FENCED_ATOMICS */
    #define __TBB_FetchAndAddWrelease(P,V)      __TBB_FetchAndAddW(P,V)
    #define __TBB_FetchAndIncrementWacquire(P)  __TBB_FetchAndAddW(P,1)
    #define __TBB_FetchAndDecrementWrelease(P)  __TBB_FetchAndAddW(P,(-1))
#endif /* !__TBB_USE_FENCED_ATOMICS */

#if __TBB_WORDSIZE==4
    #define __TBB_CompareAndSwapW(P,V,C)    __TBB_machine_cmpswp4(P,V,C)
    #define __TBB_FetchAndAddW(P,V)         __TBB_machine_fetchadd4(P,V)
    #define __TBB_FetchAndStoreW(P,V)       __TBB_machine_fetchstore4(P,V)
#elif  __TBB_WORDSIZE==8
    #if __TBB_USE_GENERIC_DWORD_LOAD_STORE || __TBB_USE_GENERIC_DWORD_FETCH_ADD || __TBB_USE_GENERIC_DWORD_FETCH_STORE
        #error These macros should only be used on 32-bit platforms.
    #endif

    #define __TBB_CompareAndSwapW(P,V,C)    __TBB_machine_cmpswp8(P,V,C)
    #define __TBB_FetchAndAddW(P,V)         __TBB_machine_fetchadd8(P,V)
    #define __TBB_FetchAndStoreW(P,V)       __TBB_machine_fetchstore8(P,V)
#else /* __TBB_WORDSIZE != 8 */
    #error Unsupported machine word size.
#endif /* __TBB_WORDSIZE */

#ifndef __TBB_Pause
    inline void __TBB_Pause(int32_t) {
        __TBB_Yield();
    }
#endif

namespace tbb {

//! Sequentially consistent full memory fence.
inline void atomic_fence () { __TBB_full_memory_fence(); }

namespace internal {

//! Class that implements exponential backoff.
/** See implementation of spin_wait_while_eq for an example. */
class atomic_backoff : no_copy {
    //! Time delay, in units of "pause" instructions.
    /** Should be equal to approximately the number of "pause" instructions
        that take the same time as an context switch. */
    static const int32_t LOOPS_BEFORE_YIELD = 16;
    int32_t count;
public:
    atomic_backoff() : count(1) {}

    //! Pause for a while.
    void pause() {
        if( count<=LOOPS_BEFORE_YIELD ) {
            __TBB_Pause(count);
            // Pause twice as long the next time.
            count*=2;
        } else {
            // Pause is so long that we might as well yield CPU to scheduler.
            __TBB_Yield();
        }
    }

    // pause for a few times and then return false immediately.
    bool bounded_pause() {
        if( count<=LOOPS_BEFORE_YIELD ) {
            __TBB_Pause(count);
            // Pause twice as long the next time.
            count*=2;
            return true;
        } else {
            return false;
        }
    }

    void reset() {
        count = 1;
    }
};

//! Spin WHILE the value of the variable is equal to a given value
/** T and U should be comparable types. */
template<typename T, typename U>
void spin_wait_while_eq( const volatile T& location, U value ) {
    atomic_backoff backoff;
    while( location==value ) backoff.pause();
}

//! Spin UNTIL the value of the variable is equal to a given value
/** T and U should be comparable types. */
template<typename T, typename U>
void spin_wait_until_eq( const volatile T& location, const U value ) {
    atomic_backoff backoff;
    while( location!=value ) backoff.pause();
}

//TODO: add static_assert for the requirements stated below
//TODO: check if it works with signed types

// there are following restrictions/limitations for this operation:
//  - T should be unsigned, otherwise sign propagation will break correctness of bit manipulations.
//  - T should be integer type of at most 4 bytes, for the casts and calculations to work.
//      (Together, these rules limit applicability of Masked CAS to uint8_t and uint16_t only,
//      as it does nothing useful for 4 bytes).
//  - The operation assumes that the architecture consistently uses either little-endian or big-endian:
//      it does not support mixed-endian or page-specific bi-endian architectures.
// This function is the only use of __TBB_BIG_ENDIAN.
#if (__TBB_BIG_ENDIAN!=-1)
    #if ( __TBB_USE_GENERIC_PART_WORD_CAS)
        #error generic implementation of part-word CAS was explicitly disabled for this configuration
    #endif
template<typename T>
inline T __TBB_MaskedCompareAndSwap (volatile T * const ptr, const T value, const T comparand ) {
    struct endianness{ static bool is_big_endian(){
        #ifndef __TBB_BIG_ENDIAN
            const uint32_t probe = 0x03020100;
            return (((const char*)(&probe))[0]==0x03);
        #elif (__TBB_BIG_ENDIAN==0) || (__TBB_BIG_ENDIAN==1)
            return __TBB_BIG_ENDIAN;
        #else
            #error unexpected value of __TBB_BIG_ENDIAN
        #endif
    }};

    const uint32_t byte_offset            = (uint32_t) ((uintptr_t)ptr & 0x3);
    volatile uint32_t * const aligned_ptr = (uint32_t*)((uintptr_t)ptr - byte_offset );

    // location of T within uint32_t for a C++ shift operation
    const uint32_t bits_to_shift     = 8*(endianness::is_big_endian() ? (4 - sizeof(T) - (byte_offset)) : byte_offset);
    const uint32_t mask              = (((uint32_t)1<<(sizeof(T)*8)) - 1 )<<bits_to_shift;
    const uint32_t shifted_comparand = ((uint32_t)comparand << bits_to_shift)&mask;
    const uint32_t shifted_value     = ((uint32_t)value     << bits_to_shift)&mask;

    for(atomic_backoff b;;b.pause()) {
        const uint32_t surroundings  = *aligned_ptr & ~mask ; // reload the aligned_ptr value which might change during the pause
        const uint32_t big_comparand = surroundings | shifted_comparand ;
        const uint32_t big_value     = surroundings | shifted_value     ;
        // __TBB_machine_cmpswp4 presumed to have full fence.
        // Cast shuts up /Wp64 warning
        const uint32_t big_result = (uint32_t)__TBB_machine_cmpswp4( aligned_ptr, big_value, big_comparand );
        if( big_result == big_comparand                    // CAS succeeded
          || ((big_result ^ big_comparand) & mask) != 0)   // CAS failed and the bits of interest have changed
        {
            return T((big_result & mask) >> bits_to_shift);
        }
        else continue;                                     // CAS failed but the bits of interest left unchanged
    }
}
#endif
template<size_t S, typename T>
inline T __TBB_CompareAndSwapGeneric (volatile void *ptr, T value, T comparand );

template<>
inline uint8_t __TBB_CompareAndSwapGeneric <1,uint8_t> (volatile void *ptr, uint8_t value, uint8_t comparand ) {
#if __TBB_USE_GENERIC_PART_WORD_CAS
    return __TBB_MaskedCompareAndSwap<uint8_t>((volatile uint8_t *)ptr,value,comparand);
#else
    return __TBB_machine_cmpswp1(ptr,value,comparand);
#endif
}

template<>
inline uint16_t __TBB_CompareAndSwapGeneric <2,uint16_t> (volatile void *ptr, uint16_t value, uint16_t comparand ) {
#if __TBB_USE_GENERIC_PART_WORD_CAS
    return __TBB_MaskedCompareAndSwap<uint16_t>((volatile uint16_t *)ptr,value,comparand);
#else
    return __TBB_machine_cmpswp2(ptr,value,comparand);
#endif
}

template<>
inline uint32_t __TBB_CompareAndSwapGeneric <4,uint32_t> (volatile void *ptr, uint32_t value, uint32_t comparand ) {
    // Cast shuts up /Wp64 warning
    return (uint32_t)__TBB_machine_cmpswp4(ptr,value,comparand);
}

#if __TBB_64BIT_ATOMICS
template<>
inline uint64_t __TBB_CompareAndSwapGeneric <8,uint64_t> (volatile void *ptr, uint64_t value, uint64_t comparand ) {
    return __TBB_machine_cmpswp8(ptr,value,comparand);
}
#endif

template<size_t S, typename T>
inline T __TBB_FetchAndAddGeneric (volatile void *ptr, T addend) {
    atomic_backoff b;
    T result;
    for(;;) {
        result = *reinterpret_cast<volatile T *>(ptr);
        // __TBB_CompareAndSwapGeneric presumed to have full fence.
        if( __TBB_CompareAndSwapGeneric<S,T> ( ptr, result+addend, result )==result )
            break;
        b.pause();
    }
    return result;
}

template<size_t S, typename T>
inline T __TBB_FetchAndStoreGeneric (volatile void *ptr, T value) {
    atomic_backoff b;
    T result;
    for(;;) {
        result = *reinterpret_cast<volatile T *>(ptr);
        // __TBB_CompareAndSwapGeneric presumed to have full fence.
        if( __TBB_CompareAndSwapGeneric<S,T> ( ptr, value, result )==result )
            break;
        b.pause();
    }
    return result;
}

#if __TBB_USE_GENERIC_PART_WORD_CAS
#define __TBB_machine_cmpswp1 tbb::internal::__TBB_CompareAndSwapGeneric<1,uint8_t>
#define __TBB_machine_cmpswp2 tbb::internal::__TBB_CompareAndSwapGeneric<2,uint16_t>
#endif

#if __TBB_USE_GENERIC_FETCH_ADD || __TBB_USE_GENERIC_PART_WORD_FETCH_ADD
#define __TBB_machine_fetchadd1 tbb::internal::__TBB_FetchAndAddGeneric<1,uint8_t>
#define __TBB_machine_fetchadd2 tbb::internal::__TBB_FetchAndAddGeneric<2,uint16_t>
#endif

#if __TBB_USE_GENERIC_FETCH_ADD
#define __TBB_machine_fetchadd4 tbb::internal::__TBB_FetchAndAddGeneric<4,uint32_t>
#endif

#if __TBB_USE_GENERIC_FETCH_ADD || __TBB_USE_GENERIC_DWORD_FETCH_ADD
#define __TBB_machine_fetchadd8 tbb::internal::__TBB_FetchAndAddGeneric<8,uint64_t>
#endif

#if __TBB_USE_GENERIC_FETCH_STORE || __TBB_USE_GENERIC_PART_WORD_FETCH_STORE
#define __TBB_machine_fetchstore1 tbb::internal::__TBB_FetchAndStoreGeneric<1,uint8_t>
#define __TBB_machine_fetchstore2 tbb::internal::__TBB_FetchAndStoreGeneric<2,uint16_t>
#endif

#if __TBB_USE_GENERIC_FETCH_STORE
#define __TBB_machine_fetchstore4 tbb::internal::__TBB_FetchAndStoreGeneric<4,uint32_t>
#endif

#if __TBB_USE_GENERIC_FETCH_STORE || __TBB_USE_GENERIC_DWORD_FETCH_STORE
#define __TBB_machine_fetchstore8 tbb::internal::__TBB_FetchAndStoreGeneric<8,uint64_t>
#endif

#if __TBB_USE_FETCHSTORE_AS_FULL_FENCED_STORE
#define __TBB_MACHINE_DEFINE_ATOMIC_SELECTOR_FETCH_STORE(S)                                             \
    atomic_selector<S>::word atomic_selector<S>::fetch_store ( volatile void* location, word value ) {  \
        return __TBB_machine_fetchstore##S( location, value );                                          \
    }

__TBB_MACHINE_DEFINE_ATOMIC_SELECTOR_FETCH_STORE(1)
__TBB_MACHINE_DEFINE_ATOMIC_SELECTOR_FETCH_STORE(2)
__TBB_MACHINE_DEFINE_ATOMIC_SELECTOR_FETCH_STORE(4)
__TBB_MACHINE_DEFINE_ATOMIC_SELECTOR_FETCH_STORE(8)

#undef __TBB_MACHINE_DEFINE_ATOMIC_SELECTOR_FETCH_STORE
#endif /* __TBB_USE_FETCHSTORE_AS_FULL_FENCED_STORE */

#if __TBB_USE_GENERIC_DWORD_LOAD_STORE
inline void __TBB_machine_store8 (volatile void *ptr, int64_t value) {
    for(;;) {
        int64_t result = *(int64_t *)ptr;
        if( __TBB_machine_cmpswp8(ptr,value,result)==result ) break;
    }
}

inline int64_t __TBB_machine_load8 (const volatile void *ptr) {
    // Comparand and new value may be anything, they only must be equal, and
    // the value should have a low probability to be actually found in 'location'.
    const int64_t anyvalue = 2305843009213693951LL;
    return __TBB_machine_cmpswp8(const_cast<volatile void *>(ptr),anyvalue,anyvalue);
}
#endif /* __TBB_USE_GENERIC_DWORD_LOAD_STORE */

#if __TBB_USE_GENERIC_HALF_FENCED_LOAD_STORE
/** Fenced operations use volatile qualifier to prevent compiler from optimizing
    them out, and on on architectures with weak memory ordering to induce compiler
    to generate code with appropriate acquire/release semantics.
    On architectures like IA32, Intel64 (and likely and Sparc TSO) volatile has
    no effect on code gen, and consistency helpers serve as a compiler fence (the
    latter being true for IA64/gcc as well to fix a bug in some gcc versions). **/
template <typename T, size_t S>
struct machine_load_store {
    static T load_with_acquire ( const volatile T& location ) {
        T to_return = location;
        __TBB_acquire_consistency_helper();
        return to_return;
    }
    static void store_with_release ( volatile T &location, T value ) {
        __TBB_release_consistency_helper();
        location = value;
    }
};

//in general, plain load and store of 32bit compiler is not atomic for 64bit types
#if __TBB_WORDSIZE==4 && __TBB_64BIT_ATOMICS
template <typename T>
struct machine_load_store<T,8> {
    static T load_with_acquire ( const volatile T& location ) {
        return (T)__TBB_machine_load8( (const volatile void*)&location );
    }
    static void store_with_release ( volatile T& location, T value ) {
        __TBB_machine_store8( (volatile void*)&location, (int64_t)value );
    }
};
#endif /* __TBB_WORDSIZE==4 && __TBB_64BIT_ATOMICS */
#endif /* __TBB_USE_GENERIC_HALF_FENCED_LOAD_STORE */

#if __TBB_USE_GENERIC_SEQUENTIAL_CONSISTENCY_LOAD_STORE
template <typename T, size_t S>
struct machine_load_store_seq_cst {
    static T load ( const volatile T& location ) {
        __TBB_full_memory_fence();
        return machine_load_store<T,S>::load_with_acquire( location );
    }
#if __TBB_USE_FETCHSTORE_AS_FULL_FENCED_STORE
    static void store ( volatile T &location, T value ) {
        atomic_selector<S>::fetch_store( (volatile void*)&location, (typename atomic_selector<S>::word)value );
    }
#else /* !__TBB_USE_FETCHSTORE_AS_FULL_FENCED_STORE */
    static void store ( volatile T &location, T value ) {
        machine_load_store<T,S>::store_with_release( location, value );
        __TBB_full_memory_fence();
    }
#endif /* !__TBB_USE_FETCHSTORE_AS_FULL_FENCED_STORE */
};

#if __TBB_WORDSIZE==4 && __TBB_64BIT_ATOMICS
/** The implementation does not use functions __TBB_machine_load8/store8 as they
    are not required to be sequentially consistent. **/
template <typename T>
struct machine_load_store_seq_cst<T,8> {
    static T load ( const volatile T& location ) {
        // Comparand and new value may be anything, they only must be equal, and
        // the value should have a low probability to be actually found in 'location'.
        const int64_t anyvalue = 2305843009213693951LL;
        return __TBB_machine_cmpswp8( (volatile void*)const_cast<volatile T*>(&location), anyvalue, anyvalue );
    }
    static void store ( volatile T &location, T value ) {
        int64_t result = (volatile int64_t&)location;
        while ( __TBB_machine_cmpswp8((volatile void*)&location, (int64_t)value, result) != result )
            result = (volatile int64_t&)location;
    }
};
#endif /* __TBB_WORDSIZE==4 && __TBB_64BIT_ATOMICS */
#endif /*__TBB_USE_GENERIC_SEQUENTIAL_CONSISTENCY_LOAD_STORE */

#if __TBB_USE_GENERIC_RELAXED_LOAD_STORE
// Relaxed operations add volatile qualifier to prevent compiler from optimizing them out.
/** Volatile should not incur any additional cost on IA32, Intel64, and Sparc TSO
    architectures. However on architectures with weak memory ordering compiler may
    generate code with acquire/release semantics for operations on volatile data. **/
template <typename T, size_t S>
struct machine_load_store_relaxed {
    static inline T load ( const volatile T& location ) {
        return location;
    }
    static inline void store ( volatile T& location, T value ) {
        location = value;
    }
};

#if __TBB_WORDSIZE==4 && __TBB_64BIT_ATOMICS
template <typename T>
struct machine_load_store_relaxed<T,8> {
    static inline T load ( const volatile T& location ) {
        return (T)__TBB_machine_load8( (const volatile void*)&location );
    }
    static inline void store ( volatile T& location, T value ) {
        __TBB_machine_store8( (volatile void*)&location, (int64_t)value );
    }
};
#endif /* __TBB_WORDSIZE==4 && __TBB_64BIT_ATOMICS */
#endif /* __TBB_USE_GENERIC_RELAXED_LOAD_STORE */

#undef __TBB_WORDSIZE //this macro is forbidden to use outside of atomic machinery

template<typename T>
inline T __TBB_load_with_acquire(const volatile T &location) {
    return machine_load_store<T,sizeof(T)>::load_with_acquire( location );
}
template<typename T, typename V>
inline void __TBB_store_with_release(volatile T& location, V value) {
    machine_load_store<T,sizeof(T)>::store_with_release( location, T(value) );
}
//! Overload that exists solely to avoid /Wp64 warnings.
inline void __TBB_store_with_release(volatile size_t& location, size_t value) {
    machine_load_store<size_t,sizeof(size_t)>::store_with_release( location, value );
}

template<typename T>
inline T __TBB_load_full_fence(const volatile T &location) {
    return machine_load_store_seq_cst<T,sizeof(T)>::load( location );
}
template<typename T, typename V>
inline void __TBB_store_full_fence(volatile T& location, V value) {
    machine_load_store_seq_cst<T,sizeof(T)>::store( location, T(value) );
}
//! Overload that exists solely to avoid /Wp64 warnings.
inline void __TBB_store_full_fence(volatile size_t& location, size_t value) {
    machine_load_store_seq_cst<size_t,sizeof(size_t)>::store( location, value );
}

template<typename T>
inline T __TBB_load_relaxed (const volatile T& location) {
    return machine_load_store_relaxed<T,sizeof(T)>::load( const_cast<T&>(location) );
}
template<typename T, typename V>
inline void __TBB_store_relaxed ( volatile T& location, V value ) {
    machine_load_store_relaxed<T,sizeof(T)>::store( const_cast<T&>(location), T(value) );
}
//! Overload that exists solely to avoid /Wp64 warnings.
inline void __TBB_store_relaxed ( volatile size_t& location, size_t value ) {
    machine_load_store_relaxed<size_t,sizeof(size_t)>::store( const_cast<size_t&>(location), value );
}

// Macro __TBB_TypeWithAlignmentAtLeastAsStrict(T) should be a type with alignment at least as
// strict as type T.  The type should have a trivial default constructor and destructor, so that
// arrays of that type can be declared without initializers.
// It is correct (but perhaps a waste of space) if __TBB_TypeWithAlignmentAtLeastAsStrict(T) expands
// to a type bigger than T.
// The default definition here works on machines where integers are naturally aligned and the
// strictest alignment is 64.
#ifndef __TBB_TypeWithAlignmentAtLeastAsStrict

#if __TBB_ATTRIBUTE_ALIGNED_PRESENT

#define __TBB_DefineTypeWithAlignment(PowerOf2)       \
struct __TBB_machine_type_with_alignment_##PowerOf2 { \
    uint32_t member[PowerOf2/sizeof(uint32_t)];       \
} __attribute__((aligned(PowerOf2)));
#define __TBB_alignof(T) __alignof__(T)

#elif __TBB_DECLSPEC_ALIGN_PRESENT

#define __TBB_DefineTypeWithAlignment(PowerOf2)       \
__declspec(align(PowerOf2))                           \
struct __TBB_machine_type_with_alignment_##PowerOf2 { \
    uint32_t member[PowerOf2/sizeof(uint32_t)];       \
};
#define __TBB_alignof(T) __alignof(T)

#else /* A compiler with unknown syntax for data alignment */
#error Must define __TBB_TypeWithAlignmentAtLeastAsStrict(T)
#endif

/* Now declare types aligned to useful powers of two */
// TODO: Is __TBB_DefineTypeWithAlignment(8) needed on 32 bit platforms?
__TBB_DefineTypeWithAlignment(16)
__TBB_DefineTypeWithAlignment(32)
__TBB_DefineTypeWithAlignment(64)

typedef __TBB_machine_type_with_alignment_64 __TBB_machine_type_with_strictest_alignment;

// Primary template is a declaration of incomplete type so that it fails with unknown alignments
template<size_t N> struct type_with_alignment;

// Specializations for allowed alignments
template<> struct type_with_alignment<1> { char member; };
template<> struct type_with_alignment<2> { uint16_t member; };
template<> struct type_with_alignment<4> { uint32_t member; };
template<> struct type_with_alignment<8> { uint64_t member; };
template<> struct type_with_alignment<16> {__TBB_machine_type_with_alignment_16 member; };
template<> struct type_with_alignment<32> {__TBB_machine_type_with_alignment_32 member; };
template<> struct type_with_alignment<64> {__TBB_machine_type_with_alignment_64 member; };

#if __TBB_ALIGNOF_NOT_INSTANTIATED_TYPES_BROKEN
//! Work around for bug in GNU 3.2 and MSVC compilers.
/** Bug is that compiler sometimes returns 0 for __alignof(T) when T has not yet been instantiated.
    The work-around forces instantiation by forcing computation of sizeof(T) before __alignof(T). */
template<size_t Size, typename T>
struct work_around_alignment_bug {
    static const size_t alignment = __TBB_alignof(T);
};
#define __TBB_TypeWithAlignmentAtLeastAsStrict(T) tbb::internal::type_with_alignment<tbb::internal::work_around_alignment_bug<sizeof(T),T>::alignment>
#else
#define __TBB_TypeWithAlignmentAtLeastAsStrict(T) tbb::internal::type_with_alignment<__TBB_alignof(T)>
#endif  /* __TBB_ALIGNOF_NOT_INSTANTIATED_TYPES_BROKEN */

#endif  /* __TBB_TypeWithAlignmentAtLeastAsStrict */

// Template class here is to avoid instantiation of the static data for modules that don't use it
template<typename T>
struct reverse {
    static const T byte_table[256];
};
// An efficient implementation of the reverse function utilizes a 2^8 lookup table holding the bit-reversed
// values of [0..2^8 - 1]. Those values can also be computed on the fly at a slightly higher cost.
template<typename T>
const T reverse<T>::byte_table[256] = {
    0x00, 0x80, 0x40, 0xC0, 0x20, 0xA0, 0x60, 0xE0, 0x10, 0x90, 0x50, 0xD0, 0x30, 0xB0, 0x70, 0xF0,
    0x08, 0x88, 0x48, 0xC8, 0x28, 0xA8, 0x68, 0xE8, 0x18, 0x98, 0x58, 0xD8, 0x38, 0xB8, 0x78, 0xF8,
    0x04, 0x84, 0x44, 0xC4, 0x24, 0xA4, 0x64, 0xE4, 0x14, 0x94, 0x54, 0xD4, 0x34, 0xB4, 0x74, 0xF4,
    0x0C, 0x8C, 0x4C, 0xCC, 0x2C, 0xAC, 0x6C, 0xEC, 0x1C, 0x9C, 0x5C, 0xDC, 0x3C, 0xBC, 0x7C, 0xFC,
    0x02, 0x82, 0x42, 0xC2, 0x22, 0xA2, 0x62, 0xE2, 0x12, 0x92, 0x52, 0xD2, 0x32, 0xB2, 0x72, 0xF2,
    0x0A, 0x8A, 0x4A, 0xCA, 0x2A, 0xAA, 0x6A, 0xEA, 0x1A, 0x9A, 0x5A, 0xDA, 0x3A, 0xBA, 0x7A, 0xFA,
    0x06, 0x86, 0x46, 0xC6, 0x26, 0xA6, 0x66, 0xE6, 0x16, 0x96, 0x56, 0xD6, 0x36, 0xB6, 0x76, 0xF6,
    0x0E, 0x8E, 0x4E, 0xCE, 0x2E, 0xAE, 0x6E, 0xEE, 0x1E, 0x9E, 0x5E, 0xDE, 0x3E, 0xBE, 0x7E, 0xFE,
    0x01, 0x81, 0x41, 0xC1, 0x21, 0xA1, 0x61, 0xE1, 0x11, 0x91, 0x51, 0xD1, 0x31, 0xB1, 0x71, 0xF1,
    0x09, 0x89, 0x49, 0xC9, 0x29, 0xA9, 0x69, 0xE9, 0x19, 0x99, 0x59, 0xD9, 0x39, 0xB9, 0x79, 0xF9,
    0x05, 0x85, 0x45, 0xC5, 0x25, 0xA5, 0x65, 0xE5, 0x15, 0x95, 0x55, 0xD5, 0x35, 0xB5, 0x75, 0xF5,
    0x0D, 0x8D, 0x4D, 0xCD, 0x2D, 0xAD, 0x6D, 0xED, 0x1D, 0x9D, 0x5D, 0xDD, 0x3D, 0xBD, 0x7D, 0xFD,
    0x03, 0x83, 0x43, 0xC3, 0x23, 0xA3, 0x63, 0xE3, 0x13, 0x93, 0x53, 0xD3, 0x33, 0xB3, 0x73, 0xF3,
    0x0B, 0x8B, 0x4B, 0xCB, 0x2B, 0xAB, 0x6B, 0xEB, 0x1B, 0x9B, 0x5B, 0xDB, 0x3B, 0xBB, 0x7B, 0xFB,
    0x07, 0x87, 0x47, 0xC7, 0x27, 0xA7, 0x67, 0xE7, 0x17, 0x97, 0x57, 0xD7, 0x37, 0xB7, 0x77, 0xF7,
    0x0F, 0x8F, 0x4F, 0xCF, 0x2F, 0xAF, 0x6F, 0xEF, 0x1F, 0x9F, 0x5F, 0xDF, 0x3F, 0xBF, 0x7F, 0xFF
};

} // namespace internal
} // namespace tbb

// Preserving access to legacy APIs
using tbb::internal::__TBB_load_with_acquire;
using tbb::internal::__TBB_store_with_release;

// Mapping historically used names to the ones expected by atomic_load_store_traits
#define __TBB_load_acquire  __TBB_load_with_acquire
#define __TBB_store_release __TBB_store_with_release

#ifndef __TBB_Log2
inline intptr_t __TBB_Log2( uintptr_t x ) {
    if( x==0 ) return -1;
    intptr_t result = 0;
    uintptr_t tmp;

    if( sizeof(x)>4 && (tmp = ((uint64_t)x)>>32)) { x=tmp; result += 32; }
    if( (tmp = x>>16) ) { x=tmp; result += 16; }
    if( (tmp = x>>8) )  { x=tmp; result += 8; }
    if( (tmp = x>>4) )  { x=tmp; result += 4; }
    if( (tmp = x>>2) )  { x=tmp; result += 2; }
    return (x&2)? result+1: result;
}
#endif

#ifndef __TBB_AtomicOR
inline void __TBB_AtomicOR( volatile void *operand, uintptr_t addend ) {
    tbb::internal::atomic_backoff b;
    for(;;) {
        uintptr_t tmp = *(volatile uintptr_t *)operand;
        uintptr_t result = __TBB_CompareAndSwapW(operand, tmp|addend, tmp);
        if( result==tmp ) break;
        b.pause();
    }
}
#endif

#ifndef __TBB_AtomicAND
inline void __TBB_AtomicAND( volatile void *operand, uintptr_t addend ) {
    tbb::internal::atomic_backoff b;
    for(;;) {
        uintptr_t tmp = *(volatile uintptr_t *)operand;
        uintptr_t result = __TBB_CompareAndSwapW(operand, tmp&addend, tmp);
        if( result==tmp ) break;
        b.pause();
    }
}
#endif

#ifndef __TBB_Flag
typedef unsigned char __TBB_Flag;
#endif
typedef __TBB_atomic __TBB_Flag __TBB_atomic_flag;

#ifndef __TBB_TryLockByte
inline bool __TBB_TryLockByte( __TBB_atomic_flag &flag ) {
    return __TBB_machine_cmpswp1(&flag,1,0)==0;
}
#endif

#ifndef __TBB_LockByte
inline __TBB_Flag __TBB_LockByte( __TBB_atomic_flag& flag ) {
    if ( !__TBB_TryLockByte(flag) ) {
        tbb::internal::atomic_backoff b;
        do {
            b.pause();
        } while ( !__TBB_TryLockByte(flag) );
    }
    return 0;
}
#endif

#ifndef  __TBB_UnlockByte
#define __TBB_UnlockByte __TBB_store_with_release
#endif

#ifndef __TBB_ReverseByte
inline unsigned char __TBB_ReverseByte(unsigned char src) {
    return tbb::internal::reverse<unsigned char>::byte_table[src];
}
#endif

template<typename T>
T __TBB_ReverseBits(T src) {
    T dst;
    unsigned char *original = (unsigned char *) &src;
    unsigned char *reversed = (unsigned char *) &dst;

    for( int i = sizeof(T)-1; i >= 0; i-- )
        reversed[i] = __TBB_ReverseByte( original[sizeof(T)-i-1] );

    return dst;
}

#endif /* __TBB_machine_H */
