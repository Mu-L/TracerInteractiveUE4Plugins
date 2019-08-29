// Copyright (c) 2017 The Chromium Embedded Framework Authors. All rights
// reserved. Use of this source code is governed by a BSD-style license that
// can be found in the LICENSE file.
//
// ---------------------------------------------------------------------------
//
// This file was generated by the CEF translator tool. If making changes by
// hand only do so within the body of existing method and function
// implementations. See the translator.README.txt file in the tools directory
// for more information.
//

#ifndef CEF_LIBCEF_DLL_CTOCPP_FIND_HANDLER_CTOCPP_H_
#define CEF_LIBCEF_DLL_CTOCPP_FIND_HANDLER_CTOCPP_H_
#pragma once

#if !defined(BUILDING_CEF_SHARED)
#error This file can be included DLL-side only
#endif

#include "include/cef_find_handler.h"
#include "include/capi/cef_find_handler_capi.h"
#include "libcef_dll/ctocpp/ctocpp_ref_counted.h"

// Wrap a C structure with a C++ class.
// This class may be instantiated and accessed DLL-side only.
class CefFindHandlerCToCpp
    : public CefCToCppRefCounted<CefFindHandlerCToCpp, CefFindHandler,
        cef_find_handler_t> {
 public:
  CefFindHandlerCToCpp();

  // CefFindHandler methods.
  void OnFindResult(CefRefPtr<CefBrowser> browser, int identifier, int count,
      const CefRect& selectionRect, int activeMatchOrdinal,
      bool finalUpdate) override;
};

#endif  // CEF_LIBCEF_DLL_CTOCPP_FIND_HANDLER_CTOCPP_H_
