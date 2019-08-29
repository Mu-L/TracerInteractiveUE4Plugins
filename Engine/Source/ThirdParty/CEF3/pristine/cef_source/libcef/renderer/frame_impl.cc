// Copyright (c) 2012 The Chromium Embedded Framework Authors. All rights
// reserved. Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE file.

#include "libcef/renderer/frame_impl.h"

#include "base/compiler_specific.h"

// Enable deprecation warnings for MSVC. See http://crbug.com/585142.
#if defined(OS_WIN)
#pragma warning(push)
#pragma warning(default:4996)
#endif

#include "libcef/common/cef_messages.h"
#include "libcef/common/net/http_header_utils.h"
#include "libcef/common/request_impl.h"
#include "libcef/renderer/browser_impl.h"
#include "libcef/renderer/dom_document_impl.h"
#include "libcef/renderer/thread_util.h"
#include "libcef/renderer/v8_impl.h"
#include "libcef/renderer/webkit_glue.h"

#include "third_party/WebKit/public/platform/WebData.h"
#include "third_party/WebKit/public/platform/WebString.h"
#include "third_party/WebKit/public/platform/WebURL.h"
#include "third_party/WebKit/public/web/WebDocument.h"
#include "third_party/WebKit/public/web/WebFrame.h"
#include "third_party/WebKit/public/web/WebFrameContentDumper.h"
#include "third_party/WebKit/public/web/WebKit.h"
#include "third_party/WebKit/public/web/WebLocalFrame.h"
#include "third_party/WebKit/public/web/WebView.h"
#include "third_party/WebKit/public/web/WebScriptSource.h"

using blink::WebString;

CefFrameImpl::CefFrameImpl(CefBrowserImpl* browser,
                           blink::WebFrame* frame)
  : browser_(browser),
    frame_(frame),
    frame_id_(webkit_glue::GetIdentifier(frame)) {
}

CefFrameImpl::~CefFrameImpl() {
}

bool CefFrameImpl::IsValid() {
  CEF_REQUIRE_RT_RETURN(false);

  return (frame_ != NULL);
}

void CefFrameImpl::Undo() {
  ExecuteCommand("Undo");
}

void CefFrameImpl::Redo() {
  ExecuteCommand("Redo");
}

void CefFrameImpl::Cut() {
  ExecuteCommand("Cut");
}

void CefFrameImpl::Copy() {
  ExecuteCommand("Copy");
}

void CefFrameImpl::Paste() {
  ExecuteCommand("Paste");
}

void CefFrameImpl::Delete() {
  ExecuteCommand("Delete");
}

void CefFrameImpl::SelectAll() {
  ExecuteCommand("SelectAll");
}

void CefFrameImpl::ViewSource() {
  NOTREACHED() << "ViewSource cannot be called from the renderer process";
}

void CefFrameImpl::GetSource(CefRefPtr<CefStringVisitor> visitor) {
  CEF_REQUIRE_RT_RETURN_VOID();
  if (frame_ && frame_->IsWebLocalFrame()) {
    const CefString& content =
        std::string(blink::WebFrameContentDumper::DumpAsMarkup(
            frame_->ToWebLocalFrame()).Utf8());
    visitor->Visit(content);
  }
}

void CefFrameImpl::GetText(CefRefPtr<CefStringVisitor> visitor) {
  CEF_REQUIRE_RT_RETURN_VOID();

  if (frame_) {
    const CefString& content = webkit_glue::DumpDocumentText(frame_);
    visitor->Visit(content);
  }
}

void CefFrameImpl::LoadRequest(CefRefPtr<CefRequest> request) {
  CEF_REQUIRE_RT_RETURN_VOID();

  if (!browser_)
    return;

  CefMsg_LoadRequest_Params params;
  params.url = GURL(std::string(request->GetURL()));
  params.method = request->GetMethod();
  params.frame_id = frame_id_;
  params.first_party_for_cookies =
      GURL(std::string(request->GetFirstPartyForCookies()));

  CefRequest::HeaderMap headerMap;
  request->GetHeaderMap(headerMap);
  if (!headerMap.empty())
    params.headers = HttpHeaderUtils::GenerateHeaders(headerMap);

  CefRefPtr<CefPostData> postData = request->GetPostData();
  if (postData.get()) {
    CefPostDataImpl* impl = static_cast<CefPostDataImpl*>(postData.get());
    params.upload_data = new net::UploadData();
    impl->Get(*params.upload_data.get());
  }

  params.load_flags = request->GetFlags();

  browser_->LoadRequest(params);
}

void CefFrameImpl::LoadURL(const CefString& url) {
  CEF_REQUIRE_RT_RETURN_VOID();

  if (!browser_)
    return;

  CefMsg_LoadRequest_Params params;
  params.url = GURL(url.ToString());
  params.method = "GET";
  params.frame_id = frame_id_;
  
  browser_->LoadRequest(params);
}

void CefFrameImpl::LoadString(const CefString& string,
                              const CefString& url) {
  CEF_REQUIRE_RT_RETURN_VOID();

  if (frame_) {
    GURL gurl = GURL(url.ToString());
    frame_->LoadHTMLString(string.ToString(), gurl);
  }
}

void CefFrameImpl::ExecuteJavaScript(const CefString& jsCode,
                                     const CefString& scriptUrl,
                                     int startLine) {
  CEF_REQUIRE_RT_RETURN_VOID();

  if (jsCode.empty())
    return;
  if (startLine < 1)
    startLine = 1;

  if (frame_) {
    GURL gurl = GURL(scriptUrl.ToString());
    frame_->ExecuteScript(
        blink::WebScriptSource(WebString::FromUTF16(jsCode.ToString16()), gurl,
                               startLine));
  }
}

bool CefFrameImpl::IsMain() {
  CEF_REQUIRE_RT_RETURN(false);

  if (frame_)
    return (frame_->Parent() == NULL);
  return false;
}

bool CefFrameImpl::IsFocused() {
  CEF_REQUIRE_RT_RETURN(false);

  if (frame_ && frame_->View())
    return (frame_->View()->FocusedFrame() == frame_);
  return false;
}

CefString CefFrameImpl::GetName() {
  CefString name;
  CEF_REQUIRE_RT_RETURN(name);

  if (frame_)
    name = webkit_glue::GetUniqueName(frame_);
  return name;
}

int64 CefFrameImpl::GetIdentifier() {
  CEF_REQUIRE_RT_RETURN(0);

  return frame_id_;
}

CefRefPtr<CefFrame> CefFrameImpl::GetParent() {
  CEF_REQUIRE_RT_RETURN(NULL);

  if (frame_) {
    blink::WebFrame* parent = frame_->Parent();
    if (parent)
      return browser_->GetWebFrameImpl(parent).get();
  }

  return NULL;
}

CefString CefFrameImpl::GetURL() {
  CefString url;
  CEF_REQUIRE_RT_RETURN(url);

  if (frame_) {
    GURL gurl = frame_->GetDocument().Url();
    url = gurl.spec();
  }
  return url;
}

CefRefPtr<CefBrowser> CefFrameImpl::GetBrowser() {
  CEF_REQUIRE_RT_RETURN(NULL);

  return browser_;
}

CefRefPtr<CefV8Context> CefFrameImpl::GetV8Context() {
  CEF_REQUIRE_RT_RETURN(NULL);

  if (frame_) {
    v8::Isolate* isolate = blink::MainThreadIsolate();
    v8::HandleScope handle_scope(isolate);
    return new CefV8ContextImpl(isolate, frame_->MainWorldScriptContext());
  } else {
    return NULL;
  }
}

void CefFrameImpl::VisitDOM(CefRefPtr<CefDOMVisitor> visitor) {
  CEF_REQUIRE_RT_RETURN_VOID();

  if (!frame_)
    return;

  // Create a CefDOMDocumentImpl object that is valid only for the scope of this
  // method.
  CefRefPtr<CefDOMDocumentImpl> documentImpl;
  const blink::WebDocument& document = frame_->GetDocument();
  if (!document.IsNull())
    documentImpl = new CefDOMDocumentImpl(browser_, frame_);

  visitor->Visit(documentImpl.get());

  if (documentImpl.get())
    documentImpl->Detach();
}

void CefFrameImpl::Detach() {
  browser_ = NULL;
  frame_ = NULL;
}

void CefFrameImpl::ExecuteCommand(const std::string& command) {
  CEF_REQUIRE_RT_RETURN_VOID();
  if (frame_ && frame_->IsWebLocalFrame())
    frame_->ToWebLocalFrame()->ExecuteCommand(WebString::FromUTF8(command));
}


// Enable deprecation warnings for MSVC. See http://crbug.com/585142.
#if defined(OS_WIN)
#pragma warning(pop)
#endif
