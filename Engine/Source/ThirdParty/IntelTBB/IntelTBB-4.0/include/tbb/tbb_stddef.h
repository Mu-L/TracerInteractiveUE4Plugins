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

#ifndef __TBB_tbb_stddef_H
#define __TBB_tbb_stddef_H

// Marketing-driven product version
#define TBB_VERSION_MAJOR 4
#define TBB_VERSION_MINOR 1

// Engineering-focused interface version
#define TBB_INTERFACE_VERSION 6100
#define TBB_INTERFACE_VERSION_MAJOR TBB_INTERFACE_VERSION/1000

// The oldest major interface version still supported
// To be used in SONAME, manifests, etc.
#define TBB_COMPATIBLE_INTERFACE_VERSION 2

#define __TBB_STRING_AUX(x) #x
#define __TBB_STRING(x) __TBB_STRING_AUX(x)

// We do not need defines below for resource processing on windows
#if !defined RC_INVOKED

// Define groups for Doxygen documentation
/**
 * @defgroup algorithms         Algorithms
 * @defgroup containers         Containers
 * @defgroup memory_allocation  Memory Allocation
 * @defgroup synchronization    Synchronization
 * @defgroup timing             Timing
 * @defgroup task_scheduling    Task Scheduling
 */

// Simple text that is displayed on the main page of Doxygen documentation.
/**
 * \mainpage Main Page
 *
 * Click the tabs above for information about the
 * - <a href="./modules.html">Modules</a> (groups of functionality) implemented by the library 
 * - <a href="./annotated.html">Classes</a> provided by the library
 * - <a href="./files.html">Files</a> constituting the library.
 * .
 * Please note that significant part of TBB functionality is implemented in the form of
 * template functions, descriptions of which are not accessible on the <a href="./annotated.html">Classes</a>
 * tab. Use <a href="./modules.html">Modules</a> or <a href="./namespacemembers.html">Namespace/Namespace Members</a>
 * tabs to find them.
 *
 * Additional pieces of information can be found here
 * - \subpage concepts
 * .
 */

/** \page concepts TBB concepts
    
    A concept is a set of requirements to a type, which are necessary and sufficient
    for the type to model a particular behavior or a set of behaviors. Some concepts 
    are specific to a particular algorithm (e.g. algorithm body), while other ones 
    are common to several algorithms (e.g. range concept). 

    All TBB algorithms make use of different classes implementing various concepts.
    Implementation classes are supplied by the user as type arguments of template 
    parameters and/or as objects passed as function call arguments. The library 
    provides predefined  implementations of some concepts (e.g. several kinds of 
    \ref range_req "ranges"), while other ones must always be implemented by the user. 
    
    TBB defines a set of minimal requirements each concept must conform to. Here is 
    the list of different concepts hyperlinked to the corresponding requirements specifications:
    - \subpage range_req
    - \subpage parallel_do_body_req
    - \subpage parallel_for_body_req
    - \subpage parallel_reduce_body_req
    - \subpage parallel_scan_body_req
    - \subpage parallel_sort_iter_req
**/

// Define preprocessor symbols used to determine architecture
#if _WIN32||_WIN64
#   if defined(_M_X64)||defined(__x86_64__)  // the latter for MinGW support
#       define __TBB_x86_64 1
#   elif defined(_M_IA64)
#       define __TBB_ipf 1
#   elif defined(_M_IX86)||defined(__i386__) // the latter for MinGW support
#       define __TBB_x86_32 1
#   endif
#else /* Assume generic Unix */
#   if !__linux__ && !__APPLE__
#       define __TBB_generic_os 1
#   endif
#   if __x86_64__
#       define __TBB_x86_64 1
#   elif __ia64__
#       define __TBB_ipf 1
#   elif __i386__||__i386  // __i386 is for Sun OS
#       define __TBB_x86_32 1
#   else
#       define __TBB_generic_arch 1
#   endif
#endif

// tbb_config.h should be included the first since it contains macro definitions used in other headers
#include "tbb_config.h"

#if _MSC_VER >=1400
    #define __TBB_EXPORTED_FUNC   __cdecl
    #define __TBB_EXPORTED_METHOD __thiscall
#else
    #define __TBB_EXPORTED_FUNC
    #define __TBB_EXPORTED_METHOD
#endif

#if __INTEL_COMPILER || _MSC_VER
#define __TBB_NOINLINE(decl) __declspec(noinline) decl
#elif __GNUC__
#define __TBB_NOINLINE(decl) decl __attribute__ ((noinline))
#else
#define __TBB_NOINLINE(decl) decl
#endif

#include <cstddef>      /* Need size_t and ptrdiff_t */

#if _MSC_VER
    #define __TBB_tbb_windef_H
    #include "internal/_tbb_windef.h"
    #undef __TBB_tbb_windef_H
#endif
#if !defined(_MSC_VER) || _MSC_VER>=1600
    #include <stdint.h>
#endif

//! Type for an assertion handler
typedef void(*assertion_handler_type)( const char* filename, int line, const char* expression, const char * comment );

#if TBB_USE_ASSERT

     #define __TBB_ASSERT_NS(predicate,message,ns) ((predicate)?((void)0) : ns::assertion_failure(__FILE__,__LINE__,#predicate,message))
    //! Assert that x is true.
    /** If x is false, print assertion failure message.  
        If the comment argument is not NULL, it is printed as part of the failure message.  
        The comment argument has no other effect. */
#if __TBBMALLOC_BUILD
namespace rml { namespace internal {
    #define __TBB_ASSERT(predicate,message) __TBB_ASSERT_NS(predicate,message,rml::internal)
#else
namespace tbb {
    #define __TBB_ASSERT(predicate,message) __TBB_ASSERT_NS(predicate,message,tbb)
#endif

    #define __TBB_ASSERT_EX __TBB_ASSERT

    //! Set assertion handler and return previous value of it.
    assertion_handler_type __TBB_EXPORTED_FUNC set_assertion_handler( assertion_handler_type new_handler );

    //! Process an assertion failure.
    /** Normally called from __TBB_ASSERT macro.
        If assertion handler is null, print message for assertion failure and abort.
        Otherwise call the assertion handler. */
    void __TBB_EXPORTED_FUNC assertion_failure( const char* filename, int line, const char* expression, const char* comment );

#if __TBBMALLOC_BUILD
}}  // namespace rml::internal
#else
} // namespace tbb
#endif
#else /* !TBB_USE_ASSERT */

    //! No-op version of __TBB_ASSERT.
    #define __TBB_ASSERT(predicate,comment) ((void)0)
    //! "Extended" version is useful to suppress warnings if a variable is only used with an assert
    #define __TBB_ASSERT_EX(predicate,comment) ((void)(1 && (predicate)))

#endif /* !TBB_USE_ASSERT */

//! The namespace tbb contains all components of the library.
namespace tbb {

#if _MSC_VER && _MSC_VER<1600
    namespace internal {
        typedef __int8 int8_t;
        typedef __int16 int16_t;
        typedef __int32 int32_t;
        typedef __int64 int64_t;
        typedef unsigned __int8 uint8_t;
        typedef unsigned __int16 uint16_t;
        typedef unsigned __int32 uint32_t;
        typedef unsigned __int64 uint64_t;
    } // namespace internal
#else /* Posix */
    namespace internal {
        using ::int8_t;
        using ::int16_t;
        using ::int32_t;
        using ::int64_t;
        using ::uint8_t;
        using ::uint16_t;
        using ::uint32_t;
        using ::uint64_t;
    } // namespace internal
#endif /* Posix */

    using std::size_t;
    using std::ptrdiff_t;

//! The function returns the interface version of the TBB shared library being used.
/**
 * The version it returns is determined at runtime, not at compile/link time.
 * So it can be different than the value of TBB_INTERFACE_VERSION obtained at compile time.
 */
extern "C" int __TBB_EXPORTED_FUNC TBB_runtime_interface_version();

//! Dummy type that distinguishes splitting constructor from copy constructor.
/**
 * See description of parallel_for and parallel_reduce for example usages.
 * @ingroup algorithms
 */
class split {
};

/**
 * @cond INTERNAL
 * @brief Identifiers declared inside namespace internal should never be used directly by client code.
 */
namespace internal {

//! Compile-time constant that is upper bound on cache line/sector size.
/** It should be used only in situations where having a compile-time upper 
    bound is more useful than a run-time exact answer.
    @ingroup memory_allocation */
const size_t NFS_MaxLineSize = 128;

/** Label for data that may be accessed from different threads, and that may eventually become wrapped
    in a formal atomic type.
    
    Note that no problems have yet been observed relating to the definition currently being empty,
    even if at least "volatile" would seem to be in order to avoid data sometimes temporarily hiding
    in a register (although "volatile" as a "poor man's atomic" lacks several other features of a proper
    atomic, some of which are now provided instead through specialized functions).

    Note that usage is intentionally compatible with a definition as qualifier "volatile",
    both as a way to have the compiler help enforce use of the label and to quickly rule out
    one potential issue.

    Note however that, with some architecture/compiler combinations, e.g. on IA-64, "volatile" 
    also has non-portable memory semantics that are needlessly expensive for "relaxed" operations.

    Note that this must only be applied to data that will not change bit patterns when cast to/from
    an integral type of the same length; tbb::atomic must be used instead for, e.g., floating-point types.

    TODO: apply wherever relevant **/
#define __TBB_atomic // intentionally empty, see above

template<class T, int S>
struct padded_base : T {
    char pad[NFS_MaxLineSize - sizeof(T) % NFS_MaxLineSize];
};
template<class T> struct padded_base<T, 0> : T {};

//! Pads type T to fill out to a multiple of cache line size.
template<class T>
struct padded : padded_base<T, sizeof(T)> {};

//! Extended variant of the standard offsetof macro
/** The standard offsetof macro is not sufficient for TBB as it can be used for
    POD-types only. The constant 0x1000 (not NULL) is necessary to appease GCC. **/
#define __TBB_offsetof(class_name, member_name) \
    ((ptrdiff_t)&(reinterpret_cast<class_name*>(0x1000)->member_name) - 0x1000)

//! Returns address of the object containing a member with the given name and address
#define __TBB_get_object_ref(class_name, member_name, member_addr) \
    (*reinterpret_cast<class_name*>((char*)member_addr - __TBB_offsetof(class_name, member_name)))

//! Throws std::runtime_error with what() returning error_code description prefixed with aux_info
void __TBB_EXPORTED_FUNC handle_perror( int error_code, const char* aux_info );

#if TBB_USE_EXCEPTIONS
    #define __TBB_TRY try
    #define __TBB_CATCH(e) catch(e)
    #define __TBB_THROW(e) throw e
    #define __TBB_RETHROW() throw
#else /* !TBB_USE_EXCEPTIONS */
    inline bool __TBB_false() { return false; }
    #define __TBB_TRY
    #define __TBB_CATCH(e) if ( tbb::internal::__TBB_false() )
    #define __TBB_THROW(e) ((void)0)
    #define __TBB_RETHROW() ((void)0)
#endif /* !TBB_USE_EXCEPTIONS */

//! Report a runtime warning.
void __TBB_EXPORTED_FUNC runtime_warning( const char* format, ... );

#if TBB_USE_ASSERT
static void* const poisoned_ptr = reinterpret_cast<void*>(-1);

//! Set p to invalid pointer value.
template<typename T>
inline void poison_pointer( T*& p ) { p = reinterpret_cast<T*>(poisoned_ptr); }

/** Expected to be used in assertions only, thus no empty form is defined. **/
template<typename T>
inline bool is_poisoned( T* p ) { return p == reinterpret_cast<T*>(poisoned_ptr); }
#else
template<typename T>
inline void poison_pointer( T* ) {/*do nothing*/}
#endif /* !TBB_USE_ASSERT */

//! Cast pointer from U* to T.
/** This method should be used sparingly as a last resort for dealing with 
    situations that inherently break strict ISO C++ aliasing rules. */
template<typename T, typename U> 
inline T punned_cast( U* ptr ) {
    uintptr_t x = reinterpret_cast<uintptr_t>(ptr);
    return reinterpret_cast<T>(x);
}

//! Base class for types that should not be assigned.
class no_assign {
    // Deny assignment
    void operator=( const no_assign& );
public:
#if __GNUC__
    //! Explicitly define default construction, because otherwise gcc issues gratuitous warning.
    no_assign() {}
#endif /* __GNUC__ */
};

//! Base class for types that should not be copied or assigned.
class no_copy: no_assign {
    //! Deny copy construction
    no_copy( const no_copy& );
public:
    //! Allow default construction
    no_copy() {}
};

//! Class for determining type of std::allocator<T>::value_type.
template<typename T>
struct allocator_type {
    typedef T value_type;
};

#if _MSC_VER
//! Microsoft std::allocator has non-standard extension that strips const from a type. 
template<typename T>
struct allocator_type<const T> {
    typedef T value_type;
};
#endif

//! A function to select either 32-bit or 64-bit value, depending on machine word size.
inline size_t size_t_select( unsigned u, unsigned long long ull ) {
    /* Explicit cast of the arguments to size_t is done to avoid compiler warnings
       (e.g. by Clang and MSVC) about possible truncation. The value of the right size,
       which is selected by ?:, is anyway not truncated or promoted.
       MSVC still warns if this trick is applied directly to constants, hence this function. */
    return (sizeof(size_t)==sizeof(u)) ? size_t(u) : size_t(ull);
}

// Struct to be used as a version tag for inline functions.
/** Version tag can be necessary to prevent loader on Linux from using the wrong 
    symbol in debug builds (when inline functions are compiled as out-of-line). **/
struct version_tag_v3 {};

typedef version_tag_v3 version_tag;

} // internal
//! @endcond

} // tbb

#endif /* RC_INVOKED */
#endif /* __TBB_tbb_stddef_H */
