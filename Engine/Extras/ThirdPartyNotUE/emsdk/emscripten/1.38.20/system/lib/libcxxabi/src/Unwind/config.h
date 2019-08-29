//===----------------------------- config.h -------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.TXT for details.
//
//
//  Defines macros used within libuwind project.
//
//===----------------------------------------------------------------------===//


#ifndef LIBUNWIND_CONFIG_H
#define LIBUNWIND_CONFIG_H

#include <assert.h>

// Define static_assert() unless already defined by compiler.
#ifndef __has_feature
  #define __has_feature(__x) 0
#endif
#if !(__has_feature(cxx_static_assert))
  #define static_assert(__b, __m) \
      extern int compile_time_assert_failed[ ( __b ) ? 1 : -1 ]  \
                                                  __attribute__( ( unused ) );
#endif

// Platform specific configuration defines.
#if __APPLE__
  #include <Availability.h>
  #ifdef __cplusplus
    extern "C" {
  #endif
    void __assert_rtn(const char *, const char *, int, const char *)
                                                      __attribute__((noreturn));
  #ifdef __cplusplus
    }
  #endif

  #define _LIBUNWIND_BUILD_ZERO_COST_APIS (__i386__ || __x86_64__ || __arm64__)
  #define _LIBUNWIND_BUILD_SJLJ_APIS      (__arm__)
  #define _LIBUNWIND_SUPPORT_FRAME_APIS   (__i386__ || __x86_64__)
  #define _LIBUNWIND_EXPORT               __attribute__((visibility("default")))
  #define _LIBUNWIND_HIDDEN               __attribute__((visibility("hidden")))
  #define _LIBUNWIND_LOG(msg, ...) fprintf(stderr, "libuwind: " msg, __VA_ARGS__)
  #define _LIBUNWIND_ABORT(msg) __assert_rtn(__func__, __FILE__, __LINE__, msg)

  #if FOR_DYLD
    #define _LIBUNWIND_SUPPORT_COMPACT_UNWIND 1
    #define _LIBUNWIND_SUPPORT_DWARF_UNWIND   0
    #define _LIBUNWIND_SUPPORT_DWARF_INDEX    0
  #else
    #define _LIBUNWIND_SUPPORT_COMPACT_UNWIND 1
    #define _LIBUNWIND_SUPPORT_DWARF_UNWIND   1
    #define _LIBUNWIND_SUPPORT_DWARF_INDEX    0
  #endif

#else
  // #define _LIBUNWIND_BUILD_ZERO_COST_APIS
  // #define _LIBUNWIND_BUILD_SJLJ_APIS
  // #define _LIBUNWIND_SUPPORT_FRAME_APIS
  // #define _LIBUNWIND_EXPORT
  // #define _LIBUNWIND_HIDDEN
  // #define _LIBUNWIND_LOG()
  // #define _LIBUNWIND_ABORT()
  // #define _LIBUNWIND_SUPPORT_COMPACT_UNWIND
  // #define _LIBUNWIND_SUPPORT_DWARF_UNWIND
  // #define _LIBUNWIND_SUPPORT_DWARF_INDEX
#endif


// Macros that define away in non-Debug builds
#ifdef NDEBUG
  #define _LIBUNWIND_DEBUG_LOG(msg, ...)
  #define _LIBUNWIND_TRACE_API(msg, ...)
  #define _LIBUNWIND_TRACING_UNWINDING 0
  #define _LIBUNWIND_TRACE_UNWINDING(msg, ...)
  #define _LIBUNWIND_LOG_NON_ZERO(x) x
#else
  #ifdef __cplusplus
    extern "C" {
  #endif
    extern  bool logAPIs();
    extern  bool logUnwinding();
  #ifdef __cplusplus
    }
  #endif
  #define _LIBUNWIND_DEBUG_LOG(msg, ...)  _LIBUNWIND_LOG(msg, __VA_ARGS__)
  #define _LIBUNWIND_LOG_NON_ZERO(x) \
            do { \
              int _err = x; \
              if ( _err != 0 ) \
                _LIBUNWIND_LOG("" #x "=%d in %s", _err, __FUNCTION__); \
             } while (0)
  #define _LIBUNWIND_TRACE_API(msg, ...) \
            do { \
              if ( logAPIs() ) _LIBUNWIND_LOG(msg, __VA_ARGS__); \
            } while(0)
  #define _LIBUNWIND_TRACE_UNWINDING(msg, ...) \
            do { \
              if ( logUnwinding() ) _LIBUNWIND_LOG(msg, __VA_ARGS__); \
            } while(0)
  #define _LIBUNWIND_TRACING_UNWINDING logUnwinding()
#endif


#endif // LIBUNWIND_CONFIG_H
