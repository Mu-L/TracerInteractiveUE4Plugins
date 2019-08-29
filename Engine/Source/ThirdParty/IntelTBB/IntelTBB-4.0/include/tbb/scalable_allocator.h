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

#ifndef __TBB_scalable_allocator_H
#define __TBB_scalable_allocator_H
/** @file */

#include <stddef.h> /* Need ptrdiff_t and size_t from here. */
#if !_MSC_VER
#include <stdint.h> /* Need intptr_t from here. */
#endif

#if !defined(__cplusplus) && __ICC==1100
    #pragma warning (push)
    #pragma warning (disable: 991)
#endif

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#if _MSC_VER >= 1400
#define __TBB_EXPORTED_FUNC   __cdecl
#else
#define __TBB_EXPORTED_FUNC
#endif

/** The "malloc" analogue to allocate block of memory of size bytes.
  * @ingroup memory_allocation */
void * __TBB_EXPORTED_FUNC scalable_malloc (size_t size);

/** The "free" analogue to discard a previously allocated piece of memory.
    @ingroup memory_allocation */
void   __TBB_EXPORTED_FUNC scalable_free (void* ptr);

/** The "realloc" analogue complementing scalable_malloc.
    @ingroup memory_allocation */
void * __TBB_EXPORTED_FUNC scalable_realloc (void* ptr, size_t size);

/** The "calloc" analogue complementing scalable_malloc.
    @ingroup memory_allocation */
void * __TBB_EXPORTED_FUNC scalable_calloc (size_t nobj, size_t size);

/** The "posix_memalign" analogue.
    @ingroup memory_allocation */
int __TBB_EXPORTED_FUNC scalable_posix_memalign (void** memptr, size_t alignment, size_t size);

/** The "_aligned_malloc" analogue.
    @ingroup memory_allocation */
void * __TBB_EXPORTED_FUNC scalable_aligned_malloc (size_t size, size_t alignment);

/** The "_aligned_realloc" analogue.
    @ingroup memory_allocation */
void * __TBB_EXPORTED_FUNC scalable_aligned_realloc (void* ptr, size_t size, size_t alignment);

/** The "_aligned_free" analogue.
    @ingroup memory_allocation */
void __TBB_EXPORTED_FUNC scalable_aligned_free (void* ptr);

/** The analogue of _msize/malloc_size/malloc_usable_size.
    Returns the usable size of a memory block previously allocated by scalable_*,
    or 0 (zero) if ptr does not point to such a block.
    @ingroup memory_allocation */
size_t __TBB_EXPORTED_FUNC scalable_msize (void* ptr);

#ifdef __cplusplus
} /* extern "C" */
#endif /* __cplusplus */

#ifdef __cplusplus

namespace rml {
class MemoryPool;

typedef void *(*rawAllocType)(intptr_t pool_id, size_t &bytes);
typedef int   (*rawFreeType)(intptr_t pool_id, void* raw_ptr, size_t raw_bytes);

/*
MemPoolPolicy extension must be compatible with such structure fields layout

struct MemPoolPolicy {
    rawAllocType pAlloc;
    rawFreeType  pFree;
    size_t       granularity;   // granularity of pAlloc allocations
};
*/

struct MemPoolPolicy {
    enum {
        VERSION = 1
    };

    rawAllocType pAlloc;
    rawFreeType  pFree;
                 // granularity of pAlloc allocations. 0 means default used.
    size_t       granularity;
    int          version;
                 // all memory consumed at 1st pAlloc call and never returned,
                 // no more pAlloc calls after 1st
    unsigned     fixedPool : 1,
                 // memory consumed but returned only at pool termination
                 keepAllMemory : 1,
                 reserved : 30;

    MemPoolPolicy(rawAllocType pAlloc_, rawFreeType pFree_,
                  size_t granularity_ = 0, bool fixedPool_ = false,
                  bool keepAllMemory_ = false) :
        pAlloc(pAlloc_), pFree(pFree_), granularity(granularity_), version(VERSION),
        fixedPool(fixedPool_), keepAllMemory(keepAllMemory_),
        reserved(0) {}
};

enum MemPoolError {
    POOL_OK,            // pool created successfully
    INVALID_POLICY,     // invalid policy parameters found
    UNSUPPORTED_POLICY, // requested pool policy is not supported by allocator library
    NO_MEMORY           // lack of memory during pool creation
};

MemPoolError pool_create_v1(intptr_t pool_id, const MemPoolPolicy *policy,
                            rml::MemoryPool **pool);

bool  pool_destroy(MemoryPool* memPool);
void *pool_malloc(MemoryPool* memPool, size_t size);
void *pool_realloc(MemoryPool* memPool, void *object, size_t size);
void *pool_aligned_malloc(MemoryPool* mPool, size_t size, size_t alignment);
void *pool_aligned_realloc(MemoryPool* mPool, void *ptr, size_t size, size_t alignment);
bool  pool_reset(MemoryPool* memPool);
bool  pool_free(MemoryPool *memPool, void *object);
}

#include <new>      /* To use new with the placement argument */

/* Ensure that including this header does not cause implicit linkage with TBB */
#ifndef __TBB_NO_IMPLICIT_LINKAGE
    #define __TBB_NO_IMPLICIT_LINKAGE 1
    #include "tbb_stddef.h"
    #undef  __TBB_NO_IMPLICIT_LINKAGE
#else
    #include "tbb_stddef.h"
#endif

#if __TBB_CPP11_RVALUE_REF_PRESENT && !__TBB_CPP11_STD_FORWARD_BROKEN
 #include <utility> // std::forward
#endif

namespace tbb {

#if _MSC_VER && !defined(__INTEL_COMPILER)
    // Workaround for erroneous "unreferenced parameter" warning in method destroy.
    #pragma warning (push)
    #pragma warning (disable: 4100)
#endif

//! Meets "allocator" requirements of ISO C++ Standard, Section 20.1.5
/** The members are ordered the same way they are in section 20.4.1
    of the ISO C++ standard.
    @ingroup memory_allocation */
template<typename T>
class scalable_allocator {
public:
    typedef typename internal::allocator_type<T>::value_type value_type;
    typedef value_type* pointer;
    typedef const value_type* const_pointer;
    typedef value_type& reference;
    typedef const value_type& const_reference;
    typedef size_t size_type;
    typedef ptrdiff_t difference_type;
    template<class U> struct rebind {
        typedef scalable_allocator<U> other;
    };

    scalable_allocator() throw() {}
    scalable_allocator( const scalable_allocator& ) throw() {}
    template<typename U> scalable_allocator(const scalable_allocator<U>&) throw() {}

    pointer address(reference x) const {return &x;}
    const_pointer address(const_reference x) const {return &x;}

    //! Allocate space for n objects.
    pointer allocate( size_type n, const void* /*hint*/ =0 ) {
        return static_cast<pointer>( scalable_malloc( n * sizeof(value_type) ) );
    }

    //! Free previously allocated block of memory
    void deallocate( pointer p, size_type ) {
        scalable_free( p );
    }

    //! Largest value for which method allocate might succeed.
    size_type max_size() const throw() {
        size_type absolutemax = static_cast<size_type>(-1) / sizeof (value_type);
        return (absolutemax > 0 ? absolutemax : 1);
    }
#if __TBB_CPP11_VARIADIC_TEMPLATES_PRESENT && __TBB_CPP11_RVALUE_REF_PRESENT
    template<typename... Args>
    void construct(pointer p, Args&&... args)
 #if __TBB_CPP11_STD_FORWARD_BROKEN
        { ::new((void *)p) T((args)...); }
 #else
        { ::new((void *)p) T(std::forward<Args>(args)...); }
 #endif
#else // __TBB_CPP11_VARIADIC_TEMPLATES_PRESENT && __TBB_CPP11_RVALUE_REF_PRESENT
    void construct( pointer p, const value_type& value ) {::new((void*)(p)) value_type(value);}
#endif // __TBB_CPP11_VARIADIC_TEMPLATES_PRESENT && __TBB_CPP11_RVALUE_REF_PRESENT
    void destroy( pointer p ) {p->~value_type();}
};

#if _MSC_VER && !defined(__INTEL_COMPILER)
    #pragma warning (pop)
#endif // warning 4100 is back

//! Analogous to std::allocator<void>, as defined in ISO C++ Standard, Section 20.4.1
/** @ingroup memory_allocation */
template<>
class scalable_allocator<void> {
public:
    typedef void* pointer;
    typedef const void* const_pointer;
    typedef void value_type;
    template<class U> struct rebind {
        typedef scalable_allocator<U> other;
    };
};

template<typename T, typename U>
inline bool operator==( const scalable_allocator<T>&, const scalable_allocator<U>& ) {return true;}

template<typename T, typename U>
inline bool operator!=( const scalable_allocator<T>&, const scalable_allocator<U>& ) {return false;}

} // namespace tbb

#if _MSC_VER
    #if (__TBB_BUILD || __TBBMALLOC_BUILD) && !defined(__TBBMALLOC_NO_IMPLICIT_LINKAGE)
        #define __TBBMALLOC_NO_IMPLICIT_LINKAGE 1
    #endif

    #if !__TBBMALLOC_NO_IMPLICIT_LINKAGE
        #ifdef _DEBUG
            #pragma comment(lib, "tbbmalloc_debug.lib")
        #else
            #pragma comment(lib, "tbbmalloc.lib")
        #endif
    #endif


#endif

#endif /* __cplusplus */

#if !defined(__cplusplus) && __ICC==1100
    #pragma warning (pop)
#endif // ICC 11.0 warning 991 is back

#endif /* __TBB_scalable_allocator_H */
