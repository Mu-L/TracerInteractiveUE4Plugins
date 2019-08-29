// Copyright (c) 2012 The Chromium Embedded Framework Authors. All rights
// reserved. Use of this source code is governed by a BSD-style license that
// can be found in the LICENSE file.

#include <map>
#include <sstream>

#include "include/base/cef_bind.h"
#include "include/cef_scheme.h"
#include "include/cef_task.h"
#include "include/cef_urlrequest.h"
#include "include/cef_waitable_event.h"
#include "include/wrapper/cef_closure_task.h"
#include "include/wrapper/cef_scoped_temp_dir.h"
#include "tests/ceftests/file_util.h"
#include "tests/ceftests/test_handler.h"
#include "tests/ceftests/test_suite.h"
#include "tests/ceftests/test_util.h"
#include "tests/gtest/include/gtest/gtest.h"
#include "tests/shared/renderer/client_app_renderer.h"

using client::ClientAppRenderer;

// How to add a new test:
// 1. Add a new value to the RequestTestMode enumeration.
// 2. Add methods to set up and run the test in RequestTestRunner.
// 3. Add a line for the test in the RequestTestRunner constructor.
// 4. Add lines for the test in the "Define the tests" section at the bottom of
//    the file.

namespace {

// Unique values for URLRequest tests.
const char* kRequestTestUrl = "http://tests/URLRequestTest.Test";
const char* kRequestTestMsg = "URLRequestTest.Test";
const char* kRequestScheme = "urcustom";
const char* kRequestHost = "test";
const char* kRequestOrigin = "urcustom://test";
const char* kRequestSendCookieName = "urcookie_send";
const char* kRequestSaveCookieName = "urcookie_save";

enum RequestTestMode {
  REQTEST_GET = 0,
  REQTEST_GET_NODATA,
  REQTEST_GET_ALLOWCOOKIES,
  REQTEST_GET_REDIRECT,
  REQTEST_GET_REFERRER,
  REQTEST_POST,
  REQTEST_POST_FILE,
  REQTEST_POST_WITHPROGRESS,
  REQTEST_HEAD,
};

enum ContextTestMode {
  CONTEXT_GLOBAL,
  CONTEXT_INMEMORY,
  CONTEXT_ONDISK,
};

struct RequestRunSettings {
  RequestRunSettings()
    : expect_upload_progress(false),
      expect_download_progress(true),
      expect_download_data(true),
      expected_status(UR_SUCCESS),
      expected_error_code(ERR_NONE),
      expect_send_cookie(false),
      expect_save_cookie(false),
      expect_follow_redirect(true) {
  }

  // Request that will be sent.
  CefRefPtr<CefRequest> request;

  // Response that will be returned by the scheme handler.
  CefRefPtr<CefResponse> response;

  // Optional response data that will be returned by the scheme handler.
  std::string response_data;

  // If true upload progress notification will be expected.
  bool expect_upload_progress;

  // If true download progress notification will be expected.
  bool expect_download_progress;

  // If true download data will be expected.
  bool expect_download_data;

  // Expected status value.
  CefURLRequest::Status expected_status;

  // Expected error code value.
  CefURLRequest::ErrorCode expected_error_code;

  // If true the request cookie should be sent to the server.
  bool expect_send_cookie;

  // If true the response cookie should be save.
  bool expect_save_cookie;

  // If specified the test will begin with this redirect request and response.
  CefRefPtr<CefRequest> redirect_request;
  CefRefPtr<CefResponse> redirect_response;

  // If true the redirect is expected to be followed.
  bool expect_follow_redirect;
};

void SetUploadData(CefRefPtr<CefRequest> request,
                   const std::string& data) {
  CefRefPtr<CefPostData> postData = CefPostData::Create();
  CefRefPtr<CefPostDataElement> element = CefPostDataElement::Create();
  element->SetToBytes(data.size(), data.c_str());
  postData->AddElement(element);
  request->SetPostData(postData);
}

void SetUploadFile(CefRefPtr<CefRequest> request,
                   const std::string& file) {
  CefRefPtr<CefPostData> postData = CefPostData::Create();
  CefRefPtr<CefPostDataElement> element = CefPostDataElement::Create();
  element->SetToFile(file);
  postData->AddElement(element);
  request->SetPostData(postData);
}

void GetUploadData(CefRefPtr<CefRequest> request,
                   std::string& data) {
  CefRefPtr<CefPostData> postData = request->GetPostData();
  EXPECT_TRUE(postData.get());
  CefPostData::ElementVector elements;
  postData->GetElements(elements);
  EXPECT_EQ((size_t)1, elements.size());
  CefRefPtr<CefPostDataElement> element = elements[0];
  EXPECT_TRUE(element.get());

  size_t size = element->GetBytesCount();

  data.resize(size);
  EXPECT_EQ(size, data.size());
  EXPECT_EQ(size, element->GetBytes(size, const_cast<char*>(data.c_str())));
}

// Set a cookie so that we can test if it's sent with the request.
void SetTestCookie(CefRefPtr<CefRequestContext> request_context) {
  EXPECT_TRUE(CefCurrentlyOn(TID_FILE));

  class Callback : public CefSetCookieCallback {
   public:
    explicit Callback(CefRefPtr<CefWaitableEvent> event)
      : event_(event) {
      EXPECT_TRUE(event_);
    }

    void OnComplete(bool success) override {
      EXPECT_TRUE(success);
      event_->Signal();
      event_ = nullptr;
    }

   private:
    CefRefPtr<CefWaitableEvent> event_;

    IMPLEMENT_REFCOUNTING(Callback);
  };

  CefRefPtr<CefWaitableEvent> event =
      CefWaitableEvent::CreateWaitableEvent(false, false);

  CefCookie cookie;
  CefString(&cookie.name) = kRequestSendCookieName;
  CefString(&cookie.value) = "send-cookie-value";
  CefString(&cookie.domain) = kRequestHost;
  CefString(&cookie.path) = "/";
  cookie.has_expires = false;
  EXPECT_TRUE(request_context->GetDefaultCookieManager(NULL)->SetCookie(
      kRequestOrigin, cookie, new Callback(event)));

  // Wait for the Callback.
  event->TimedWait(2000);
  EXPECT_TRUE(event->IsSignaled());
}

// Tests if the save cookie has been set. If set, it will be deleted at the same
// time.
void GetTestCookie(CefRefPtr<CefRequestContext> request_context,
                   bool* cookie_exists) {
  EXPECT_TRUE(CefCurrentlyOn(TID_FILE));

  class Visitor : public CefCookieVisitor {
   public:
    Visitor(CefRefPtr<CefWaitableEvent> event, bool* cookie_exists)
      : event_(event),
        cookie_exists_(cookie_exists) {
      EXPECT_TRUE(event_);
    }
    ~Visitor() override {
      event_->Signal();
    }

    bool Visit(const CefCookie& cookie, int count, int total,
                bool& deleteCookie) override {
      std::string cookie_name = CefString(&cookie.name);
      if (cookie_name == kRequestSaveCookieName) {
        *cookie_exists_ = true;
        deleteCookie = true;
        return false;
      }
      return true;
    }

   private:
    CefRefPtr<CefWaitableEvent> event_;
    bool* cookie_exists_;

    IMPLEMENT_REFCOUNTING(Visitor);
  };

  CefRefPtr<CefWaitableEvent> event =
      CefWaitableEvent::CreateWaitableEvent(false, false);

  CefRefPtr<CefCookieManager> cookie_manager =
      request_context->GetDefaultCookieManager(NULL);
  cookie_manager->VisitUrlCookies(
      kRequestOrigin, true, new Visitor(event, cookie_exists));

  // Wait for the Visitor.
  event->TimedWait(2000);
  EXPECT_TRUE(event->IsSignaled());
}


// Serves request responses.
class RequestSchemeHandler : public CefResourceHandler {
 public:
  explicit RequestSchemeHandler(const RequestRunSettings& settings)
    : settings_(settings),
      offset_(0) {
  }

  bool ProcessRequest(CefRefPtr<CefRequest> request,
                      CefRefPtr<CefCallback> callback) override {
    EXPECT_TRUE(CefCurrentlyOn(TID_IO));

    // Shouldn't get here if we're not following redirects.
    EXPECT_TRUE(settings_.expect_follow_redirect);

    // Verify that the request was sent correctly.
    TestRequestEqual(settings_.request, request, true);

    // HEAD requests are identical to GET requests except no response data is
    // sent.
    if (request->GetMethod() == "HEAD")
      settings_.response_data.clear();

    CefRequest::HeaderMap headerMap;
    CefRequest::HeaderMap::iterator headerIter;
    request->GetHeaderMap(headerMap);

    // Check if the default headers were sent.
    headerIter = headerMap.find("User-Agent");
    EXPECT_TRUE(headerIter != headerMap.end() && !headerIter->second.empty());
    headerIter = headerMap.find("Accept-Language");
    EXPECT_TRUE(headerIter != headerMap.end() && !headerIter->second.empty());

    // Verify that we get the value that was set via
    // CefSettings.accept_language_list in CefTestSuite::GetSettings().
    EXPECT_STREQ(CEF_SETTINGS_ACCEPT_LANGUAGE,
                 headerIter->second.ToString().data());

    // Check if the request cookie was sent.
    bool has_send_cookie = false;
    headerIter = headerMap.find("Cookie");
    if (headerIter != headerMap.end()) {
      std::string cookie = headerIter->second;
      if (cookie.find(kRequestSendCookieName) != std::string::npos)
        has_send_cookie = true;
    }

    if (settings_.expect_send_cookie)
      EXPECT_TRUE(has_send_cookie);
    else
      EXPECT_FALSE(has_send_cookie);

    // Continue immediately.
    callback->Continue();
    return true;
  }

  void GetResponseHeaders(CefRefPtr<CefResponse> response,
                          int64& response_length,
                          CefString& redirectUrl) override {
    EXPECT_TRUE(CefCurrentlyOn(TID_IO));

    response->SetStatus(settings_.response->GetStatus());
    response->SetStatusText(settings_.response->GetStatusText());
    response->SetMimeType(settings_.response->GetMimeType());

    CefResponse::HeaderMap headerMap;
    settings_.response->GetHeaderMap(headerMap);

    if (settings_.expect_save_cookie) {
      std::stringstream ss;
      ss << kRequestSaveCookieName << "=" << "save-cookie-value";
      headerMap.insert(std::make_pair("Set-Cookie", ss.str()));
    }

    response->SetHeaderMap(headerMap);

    response_length = settings_.response_data.length();
  }

  bool ReadResponse(void* response_data_out,
                    int bytes_to_read,
                    int& bytes_read,
                    CefRefPtr<CefCallback> callback) override {
    EXPECT_TRUE(CefCurrentlyOn(TID_IO));

    bool has_data = false;
    bytes_read = 0;

    size_t size = settings_.response_data.length();
    if (offset_ < size) {
      int transfer_size =
          std::min(bytes_to_read, static_cast<int>(size - offset_));
      memcpy(response_data_out,
             settings_.response_data.c_str() + offset_,
             transfer_size);
      offset_ += transfer_size;

      bytes_read = transfer_size;
      has_data = true;
    }

    return has_data;
  }

  void Cancel() override {
    EXPECT_TRUE(CefCurrentlyOn(TID_IO));
  }

 private:
  RequestRunSettings settings_;
  size_t offset_;

  IMPLEMENT_REFCOUNTING(RequestSchemeHandler);
};

// Serves redirect request responses.
class RequestRedirectSchemeHandler : public CefResourceHandler {
 public:
  explicit RequestRedirectSchemeHandler(CefRefPtr<CefRequest> request,
                                        CefRefPtr<CefResponse> response)
    : request_(request),
      response_(response) {
  }

  bool ProcessRequest(CefRefPtr<CefRequest> request,
                      CefRefPtr<CefCallback> callback) override {
    EXPECT_TRUE(CefCurrentlyOn(TID_IO));

    // Verify that the request was sent correctly.
    TestRequestEqual(request_, request, true);

    // Continue immediately.
    callback->Continue();
    return true;
  }

  void GetResponseHeaders(CefRefPtr<CefResponse> response,
                          int64& response_length,
                          CefString& redirectUrl) override {
    EXPECT_TRUE(CefCurrentlyOn(TID_IO));

    response->SetStatus(response_->GetStatus());
    response->SetStatusText(response_->GetStatusText());
    response->SetMimeType(response_->GetMimeType());

    CefResponse::HeaderMap headerMap;
    response_->GetHeaderMap(headerMap);
    response->SetHeaderMap(headerMap);

    response_length = 0;
  }

  bool ReadResponse(void* response_data_out,
                    int bytes_to_read,
                    int& bytes_read,
                    CefRefPtr<CefCallback> callback) override {
    EXPECT_TRUE(CefCurrentlyOn(TID_IO));
    NOTREACHED();
    return false;
  }

  void Cancel() override {
    EXPECT_TRUE(CefCurrentlyOn(TID_IO));
  }

 private:
  CefRefPtr<CefRequest> request_;
  CefRefPtr<CefResponse> response_;

  IMPLEMENT_REFCOUNTING(RequestRedirectSchemeHandler);
};


class RequestSchemeHandlerFactory : public CefSchemeHandlerFactory {
 public:
  RequestSchemeHandlerFactory() {
  }

  CefRefPtr<CefResourceHandler> Create(
      CefRefPtr<CefBrowser> browser,
      CefRefPtr<CefFrame> frame,
      const CefString& scheme_name,
      CefRefPtr<CefRequest> request) override {
    EXPECT_TRUE(CefCurrentlyOn(TID_IO));
    std::string url = request->GetURL();
    
    // Try to find a test match.
    {
      HandlerMap::const_iterator it = handler_map_.find(url);
      if (it != handler_map_.end())
        return new RequestSchemeHandler(it->second);
    }

    // Try to find a redirect match.
    {
      RedirectHandlerMap::const_iterator it = redirect_handler_map_.find(url);
      if (it != redirect_handler_map_.end()) {
        return new RequestRedirectSchemeHandler(it->second.first,
                                                it->second.second);
      }
    }

    // Unknown test.
    ADD_FAILURE();
    return NULL;
  }

  void AddSchemeHandler(const RequestRunSettings& settings) {
    // Verify that the scheme is correct.
    std::string url = settings.request->GetURL();
    EXPECT_EQ((unsigned long)0, url.find(kRequestScheme));

    handler_map_.insert(std::make_pair(url, settings));
  }

  void AddRedirectSchemeHandler(CefRefPtr<CefRequest> redirect_request,
                                CefRefPtr<CefResponse> redirect_response) {
    // Verify that the scheme is correct.
    std::string url = redirect_request->GetURL();
    EXPECT_EQ((unsigned long)0, url.find(kRequestScheme));

    redirect_handler_map_.insert(
        std::make_pair(url,
            std::make_pair(redirect_request, redirect_response)));
  }

 private:
  typedef std::map<std::string, RequestRunSettings> HandlerMap;
  HandlerMap handler_map_;

  typedef std::map<std::string,
                  std::pair<CefRefPtr<CefRequest>, CefRefPtr<CefResponse> > >
                  RedirectHandlerMap;
  RedirectHandlerMap redirect_handler_map_;

  IMPLEMENT_REFCOUNTING(RequestSchemeHandlerFactory);
};


// Implementation of CefURLRequestClient that stores response information.
class RequestClient : public CefURLRequestClient {
 public:
  class Delegate {
   public:
    // Used to notify the handler when the request has completed.
    virtual void OnRequestComplete(CefRefPtr<RequestClient> client) =0;
   protected:
    virtual ~Delegate() {}
  };

  explicit RequestClient(Delegate* delegate)
    : delegate_(delegate),
      request_complete_ct_(0),
      upload_progress_ct_(0),
      download_progress_ct_(0),
      download_data_ct_(0),
      upload_total_(0),
      download_total_(0) {
  }

  void OnRequestComplete(CefRefPtr<CefURLRequest> request) override {
    request_complete_ct_++;

    request_ = request->GetRequest();
    EXPECT_TRUE(request_->IsReadOnly());
    status_ = request->GetRequestStatus();
    error_code_ = request->GetRequestError();
    response_ = request->GetResponse();
    EXPECT_TRUE(response_.get());
    EXPECT_TRUE(response_->IsReadOnly());

    delegate_->OnRequestComplete(this);
  }

  void OnUploadProgress(CefRefPtr<CefURLRequest> request,
                        int64 current,
                        int64 total) override {
    upload_progress_ct_++;
    upload_total_ = total;
  }

  void OnDownloadProgress(CefRefPtr<CefURLRequest> request,
                          int64 current,
                          int64 total) override {
    response_ = request->GetResponse();
    EXPECT_TRUE(response_.get());
    EXPECT_TRUE(response_->IsReadOnly());
    download_progress_ct_++;
    download_total_ = total;
  }

  void OnDownloadData(CefRefPtr<CefURLRequest> request,
                      const void* data,
                      size_t data_length) override {
    response_ = request->GetResponse();
    EXPECT_TRUE(response_.get());
    EXPECT_TRUE(response_->IsReadOnly());
    download_data_ct_++;
    download_data_ += std::string(static_cast<const char*>(data), data_length);
  }

   bool GetAuthCredentials(bool isProxy,
                           const CefString& host,
                           int port,
                           const CefString& realm,
                           const CefString& scheme,
                           CefRefPtr<CefAuthCallback> callback) override {
     return false;
   }

 private:
  Delegate* delegate_;

 public:
  int request_complete_ct_;
  int upload_progress_ct_;
  int download_progress_ct_;
  int download_data_ct_;

  uint64 upload_total_;
  uint64 download_total_;
  std::string download_data_;
  CefRefPtr<CefRequest> request_;
  CefURLRequest::Status status_;
  CefURLRequest::ErrorCode error_code_;
  CefRefPtr<CefResponse> response_;

 private:
  IMPLEMENT_REFCOUNTING(RequestClient);
};


// Executes the tests.
class RequestTestRunner : public base::RefCountedThreadSafe<RequestTestRunner> {
 public:
  typedef base::Callback<void(const base::Closure&)> TestCallback;

  // Delegate methods will be called on the same thread that constructed the
  // RequestTestRunner object.
  class Delegate {
   public:
    // Setup has completed.
    virtual void OnRunnerSetupComplete() =0;

    // Run has completed.
    virtual void OnRunnerRunComplete() =0;

   protected:
    virtual ~Delegate() {}
  };

  RequestTestRunner(Delegate* delegate,
                    bool is_browser_process)
    : delegate_(delegate),
      is_browser_process_(is_browser_process) {
    owner_task_runner_ = CefTaskRunner::GetForCurrentThread();
    EXPECT_TRUE(owner_task_runner_.get());
    EXPECT_TRUE(owner_task_runner_->BelongsToCurrentThread());

    // Helper macro for registering test callbacks.
    #define REGISTER_TEST(test_mode, setup_method, run_method) \
        RegisterTest(test_mode, \
                     base::Bind(&RequestTestRunner::setup_method, this), \
                     base::Bind(&RequestTestRunner::run_method, this));

    // Register the test callbacks.
    REGISTER_TEST(REQTEST_GET, SetupGetTest, GenericRunTest);
    REGISTER_TEST(REQTEST_GET_NODATA, SetupGetNoDataTest, GenericRunTest);
    REGISTER_TEST(REQTEST_GET_ALLOWCOOKIES, SetupGetAllowCookiesTest,
                  GenericRunTest);
    REGISTER_TEST(REQTEST_GET_REDIRECT, SetupGetRedirectTest, GenericRunTest);
    REGISTER_TEST(REQTEST_GET_REFERRER, SetupGetReferrerTest, GenericRunTest);
    REGISTER_TEST(REQTEST_POST, SetupPostTest, GenericRunTest);
    REGISTER_TEST(REQTEST_POST_FILE, SetupPostFileTest, GenericRunTest);
    REGISTER_TEST(REQTEST_POST_WITHPROGRESS, SetupPostWithProgressTest,
                  GenericRunTest);
    REGISTER_TEST(REQTEST_HEAD, SetupHeadTest, GenericRunTest);
  }

  void Destroy() {
    owner_task_runner_ = nullptr;
    request_context_ = nullptr;
  }

  // Called in the browser process to set the request context that will be used
  // when creating the URL request.
  void SetRequestContext(CefRefPtr<CefRequestContext> request_context) {
    request_context_ = request_context;
  }
  CefRefPtr<CefRequestContext> GetRequestContext() const {
    return request_context_;
  }

  // Called in both the browser and render process to setup the test.
  void SetupTest(RequestTestMode test_mode) {
    EXPECT_TRUE(owner_task_runner_->BelongsToCurrentThread());

    const base::Closure& complete_callback =
        base::Bind(&RequestTestRunner::SetupComplete, this);
    TestMap::const_iterator it = test_map_.find(test_mode);
    if (it != test_map_.end()) {
      it->second.setup.Run(
          base::Bind(&RequestTestRunner::SetupContinue, this,
                     complete_callback));
    } else {
      // Unknown test.
      ADD_FAILURE();
      complete_callback.Run();
    }
  }

  // Called in either the browser or render process to run the test.
  void RunTest(RequestTestMode test_mode) {
    EXPECT_TRUE(owner_task_runner_->BelongsToCurrentThread());

    const base::Closure& complete_callback =
        base::Bind(&RequestTestRunner::RunComplete, this);
    TestMap::const_iterator it = test_map_.find(test_mode);
    if (it != test_map_.end()) {
      it->second.run.Run(complete_callback);
    } else {
      // Unknown test.
      ADD_FAILURE();
      complete_callback.Run();
    }
  }

 private:
  // Continued after |settings_| is populated for the test.
  void SetupContinue(const base::Closure& complete_callback) {
    if (!owner_task_runner_->BelongsToCurrentThread()) {
      owner_task_runner_->PostTask(CefCreateClosureTask(
          base::Bind(&RequestTestRunner::SetupContinue, this,
                     complete_callback)));
      return;
    }

    if (is_browser_process_)
      AddSchemeHandler();

    complete_callback.Run();
  }

  void SetupGetTestShared() {
    settings_.request = CefRequest::Create();
    settings_.request->SetURL(MakeSchemeURL("GetTest.html"));
    settings_.request->SetMethod("GET");

    settings_.response = CefResponse::Create();
    settings_.response->SetMimeType("text/html");
    settings_.response->SetStatus(200);
    settings_.response->SetStatusText("OK");

    settings_.response_data = "GET TEST SUCCESS";
  }

  void SetupGetTest(const base::Closure& complete_callback) {
    SetupGetTestShared();
    complete_callback.Run();
  }

  void SetupGetNoDataTest(const base::Closure& complete_callback) {
    // Start with the normal get test.
    SetupGetTestShared();

    // Disable download data notifications.
    settings_.request->SetFlags(UR_FLAG_NO_DOWNLOAD_DATA);

    settings_.expect_download_data = false;

    complete_callback.Run();
  }

  void SetupGetAllowCookiesTest(const base::Closure& complete_callback) {
    // Start with the normal get test.
    SetupGetTestShared();

    // Send cookies.
    settings_.request->SetFlags(UR_FLAG_ALLOW_CACHED_CREDENTIALS);

    settings_.expect_save_cookie = true;
    settings_.expect_send_cookie = true;

    complete_callback.Run();
  }

  void SetupGetRedirectTest(const base::Closure& complete_callback) {
    // Start with the normal get test.
    SetupGetTestShared();

    // Add a redirect request.
    settings_.redirect_request = CefRequest::Create();
    settings_.redirect_request->SetURL(MakeSchemeURL("redirect.html"));
    settings_.redirect_request->SetMethod("GET");

    settings_.redirect_response = CefResponse::Create();
    settings_.redirect_response->SetMimeType("text/html");
    settings_.redirect_response->SetStatus(302);
    settings_.redirect_response->SetStatusText("Found");

    CefResponse::HeaderMap headerMap;
    headerMap.insert(std::make_pair("Location", settings_.request->GetURL()));
    settings_.redirect_response->SetHeaderMap(headerMap);

    complete_callback.Run();
  }

  void SetupGetReferrerTest(const base::Closure& complete_callback) {
    settings_.request = CefRequest::Create();
    settings_.request->SetURL(MakeSchemeURL("GetTest.html"));
    settings_.request->SetMethod("GET");

    // The referrer URL must be HTTP or HTTPS. This is enforced by
    // GURL::GetAsReferrer() called from URLRequest::SetReferrer().
    settings_.request->SetReferrer(
        "http://tests.com/referrer.html",
        REFERRER_POLICY_NO_REFERRER_WHEN_DOWNGRADE);

    settings_.response = CefResponse::Create();
    settings_.response->SetMimeType("text/html");
    settings_.response->SetStatus(200);
    settings_.response->SetStatusText("OK");

    settings_.response_data = "GET TEST SUCCESS";

    complete_callback.Run();
  }

  void SetupPostTestShared() {
    settings_.request = CefRequest::Create();
    settings_.request->SetURL(MakeSchemeURL("PostTest.html"));
    settings_.request->SetMethod("POST");
    SetUploadData(settings_.request, "the_post_data");

    settings_.response = CefResponse::Create();
    settings_.response->SetMimeType("text/html");
    settings_.response->SetStatus(200);
    settings_.response->SetStatusText("OK");

    settings_.response_data = "POST TEST SUCCESS";
  }

  void SetupPostTest(const base::Closure& complete_callback) {
    SetupPostTestShared();
    complete_callback.Run();
  }

  void SetupPostFileTest(const base::Closure& complete_callback) {
    // This test is only supported in the browser process.
    EXPECT_TRUE(is_browser_process_);

    settings_.request = CefRequest::Create();
    settings_.request->SetURL(MakeSchemeURL("PostFileTest.html"));
    settings_.request->SetMethod("POST");

    settings_.response = CefResponse::Create();
    settings_.response->SetMimeType("text/html");
    settings_.response->SetStatus(200);
    settings_.response->SetStatusText("OK");

    settings_.response_data = "POST TEST SUCCESS";

    CefPostTask(TID_FILE,
        base::Bind(&RequestTestRunner::SetupPostFileTestContinue, this,
                   complete_callback));
  }

  void SetupPostFileTestContinue(const base::Closure& complete_callback) {
    EXPECT_TRUE(CefCurrentlyOn(TID_FILE));

    EXPECT_TRUE(post_file_tmpdir_.CreateUniqueTempDir());
    const std::string& path =
        file_util::JoinPath(post_file_tmpdir_.GetPath(), "example.txt");
    const char content[] = "HELLO FRIEND!";
    int write_ct = file_util::WriteFile(path, content, sizeof(content) - 1);
    EXPECT_EQ(static_cast<int>(sizeof(content) - 1), write_ct);
    SetUploadFile(settings_.request, path);

    complete_callback.Run();
  }

  void SetupPostWithProgressTest(const base::Closure& complete_callback) {
    // Start with the normal post test.
    SetupPostTestShared();

    // Enable upload progress notifications.
    settings_.request->SetFlags(UR_FLAG_REPORT_UPLOAD_PROGRESS);

    settings_.expect_upload_progress = true;

    complete_callback.Run();
  }

  void SetupHeadTest(const base::Closure& complete_callback) {
    settings_.request = CefRequest::Create();
    settings_.request->SetURL(MakeSchemeURL("HeadTest.html"));
    settings_.request->SetMethod("HEAD");

    settings_.response = CefResponse::Create();
    settings_.response->SetMimeType("text/html");
    settings_.response->SetStatus(200);
    settings_.response->SetStatusText("OK");

    // The scheme handler will disregard this value when it returns the result.
    settings_.response_data = "HEAD TEST SUCCESS";

    settings_.expect_download_progress = false;
    settings_.expect_download_data = false;

    complete_callback.Run();
  }

  // Generic test runner.
  void GenericRunTest(const base::Closure& complete_callback) {
    class Test : public RequestClient::Delegate {
     public:
      Test(const RequestRunSettings& settings,
           const base::Closure& complete_callback)
        : settings_(settings),
          complete_callback_(complete_callback) {
        EXPECT_FALSE(complete_callback_.is_null());
      }

      void OnRequestComplete(CefRefPtr<RequestClient> client) override {
        CefRefPtr<CefRequest> expected_request;
        CefRefPtr<CefResponse> expected_response;

        if (settings_.redirect_request.get())
          expected_request = settings_.redirect_request;
        else
          expected_request = settings_.request;

        if (settings_.redirect_response.get() &&
            !settings_.expect_follow_redirect) {
          // A redirect response was sent but the redirect is not expected to be
          // followed.
          expected_response = settings_.redirect_response;
        } else {
          expected_response = settings_.response;
        }
        
        TestRequestEqual(expected_request, client->request_, false);

        EXPECT_EQ(settings_.expected_status, client->status_);
        EXPECT_EQ(settings_.expected_error_code, client->error_code_);
        TestResponseEqual(expected_response, client->response_, true);

        EXPECT_EQ(1, client->request_complete_ct_);

        if (settings_.expect_upload_progress) {
          EXPECT_LE(1, client->upload_progress_ct_);

          std::string upload_data;
          GetUploadData(expected_request, upload_data);
          EXPECT_EQ(upload_data.size(), client->upload_total_);
        } else {
          EXPECT_EQ(0, client->upload_progress_ct_);
          EXPECT_EQ((uint64)0, client->upload_total_);
        }

        if (settings_.expect_download_progress) {
          EXPECT_LE(1, client->download_progress_ct_);
          EXPECT_EQ(settings_.response_data.size(),
                    client->download_total_);
        } else {
          EXPECT_EQ(0, client->download_progress_ct_);
          EXPECT_EQ((uint64)0, client->download_total_);
        }

        if (settings_.expect_download_data) {
          EXPECT_LE(1, client->download_data_ct_);
          EXPECT_STREQ(settings_.response_data.c_str(),
                       client->download_data_.c_str());
        } else {
          EXPECT_EQ(0, client->download_data_ct_);
          EXPECT_TRUE(client->download_data_.empty());
        }

        complete_callback_.Run();
        complete_callback_.Reset();
      }

     private:
      RequestRunSettings settings_;
      base::Closure complete_callback_;
    };

    CefRefPtr<CefRequest> request;
    if (settings_.redirect_request.get())
      request = settings_.redirect_request;
    else
      request = settings_.request;
    EXPECT_TRUE(request.get());

    CefRefPtr<RequestClient> client =
        new RequestClient(new Test(settings_, complete_callback));

    CefURLRequest::Create(request, client.get(), request_context_);
  }

  // Register a test. Called in the constructor.
  void RegisterTest(RequestTestMode test_mode,
                    TestCallback setup,
                    TestCallback run) {
    TestEntry entry = {setup, run};
    test_map_.insert(std::make_pair(test_mode, entry));
  }

  void SetupComplete() {
    if (!owner_task_runner_->BelongsToCurrentThread()) {
      owner_task_runner_->PostTask(CefCreateClosureTask(
          base::Bind(&RequestTestRunner::SetupComplete, this)));
      return;
    }

    delegate_->OnRunnerSetupComplete();
  }

  // Destroy the current test. Called when the test is complete.
  void RunComplete() {
    if (!post_file_tmpdir_.IsEmpty()) {
      EXPECT_TRUE(is_browser_process_);
      CefPostTask(TID_FILE,
          base::Bind(&RequestTestRunner::RunCompleteDeleteTempDirectory, this));
      return;
    }

    // Continue with test completion.
    RunCompleteContinue();
  }

  void RunCompleteDeleteTempDirectory() {
    EXPECT_TRUE(CefCurrentlyOn(TID_FILE));

    EXPECT_TRUE(post_file_tmpdir_.Delete());
    EXPECT_TRUE(post_file_tmpdir_.IsEmpty());

    // Continue with test completion.
    RunCompleteContinue();
  }

  void RunCompleteContinue() {
    if (!owner_task_runner_->BelongsToCurrentThread()) {
      owner_task_runner_->PostTask(CefCreateClosureTask(
          base::Bind(&RequestTestRunner::RunCompleteContinue, this)));
      return;
    }

    if (scheme_factory_.get()) {
      EXPECT_TRUE(is_browser_process_);

      // Remove the factory registration.
      request_context_->RegisterSchemeHandlerFactory(
          kRequestScheme, kRequestHost, NULL);
      scheme_factory_ = NULL;
    }

    delegate_->OnRunnerRunComplete();
  }

  // Return an appropriate scheme URL for the specified |path|.
  std::string MakeSchemeURL(const std::string& path) {
    std::stringstream ss;
    ss << kRequestOrigin << "/" << path;
    return ss.str();
  }

  // Add a scheme handler for the current test. Called during test setup.
  void AddSchemeHandler() {
    // Scheme handlers are only registered in the browser process.
    EXPECT_TRUE(is_browser_process_);

    if (!scheme_factory_.get()) {
      // Add the factory registration.
      scheme_factory_ = new RequestSchemeHandlerFactory();
      request_context_->RegisterSchemeHandlerFactory(
          kRequestScheme, kRequestHost, scheme_factory_.get());
    }

    EXPECT_TRUE(settings_.request.get());
    EXPECT_TRUE(settings_.response.get());

    scheme_factory_->AddSchemeHandler(settings_);

    if (settings_.redirect_request.get()) {
      scheme_factory_->AddRedirectSchemeHandler(settings_.redirect_request,
                                                settings_.redirect_response);
    }
  }

  Delegate* delegate_;
  bool is_browser_process_;

  // Primary thread runner for the object that owns us. In the browser process
  // this will be the UI thread and in the renderer process this will be the
  // RENDERER thread.
  CefRefPtr<CefTaskRunner> owner_task_runner_;

  CefRefPtr<CefRequestContext> request_context_;

  struct TestEntry {
    TestCallback setup;
    TestCallback run;
  };
  typedef std::map<RequestTestMode, TestEntry> TestMap;
  TestMap test_map_;

  std::string scheme_name_;
  CefRefPtr<RequestSchemeHandlerFactory> scheme_factory_;

  CefScopedTempDir post_file_tmpdir_;

 public:
  RequestRunSettings settings_;
};

// Renderer side.
class RequestRendererTest : public ClientAppRenderer::Delegate,
                            public RequestTestRunner::Delegate {
 public:
  RequestRendererTest() {
  }

  bool OnProcessMessageReceived(
      CefRefPtr<ClientAppRenderer> app,
      CefRefPtr<CefBrowser> browser,
      CefProcessId source_process,
      CefRefPtr<CefProcessMessage> message) override {
    if (message->GetName() == kRequestTestMsg) {
      EXPECT_TRUE(CefCurrentlyOn(TID_RENDERER));

      app_ = app;
      browser_ = browser;

      test_mode_ =
          static_cast<RequestTestMode>(message->GetArgumentList()->GetInt(0));

      test_runner_ = new RequestTestRunner(this, false);

      // Setup the test. This will create the objects that we test against but
      // not register any scheme handlers (because we're in the render process).
      test_runner_->SetupTest(test_mode_);

      return true;
    }

    // Message not handled.
    return false;
  }

 private:
  void OnRunnerSetupComplete() override {
    EXPECT_TRUE(CefCurrentlyOn(TID_RENDERER));

    // Run the test.
    test_runner_->RunTest(test_mode_);
  }

  // Return from the test.
  void OnRunnerRunComplete() override {
    EXPECT_TRUE(CefCurrentlyOn(TID_RENDERER));

    // Check if the test has failed.
    bool result = !TestFailed();

    // Return the result to the browser process.
    CefRefPtr<CefProcessMessage> return_msg =
        CefProcessMessage::Create(kRequestTestMsg);
    EXPECT_TRUE(return_msg->GetArgumentList()->SetBool(0, result));
    EXPECT_TRUE(browser_->SendProcessMessage(PID_BROWSER, return_msg));

    app_ = NULL;
    browser_ = NULL;
  }

  CefRefPtr<ClientAppRenderer> app_;
  CefRefPtr<CefBrowser> browser_;
  RequestTestMode test_mode_;

  scoped_refptr<RequestTestRunner> test_runner_;

  IMPLEMENT_REFCOUNTING(RequestRendererTest);
};

// Browser side.
class RequestTestHandler : public TestHandler,
                           public RequestTestRunner::Delegate {
 public:
  RequestTestHandler(RequestTestMode test_mode,
                     ContextTestMode context_mode,
                     bool test_in_browser,
                     const char* test_url)
    : test_mode_(test_mode),
      context_mode_(context_mode),
      test_in_browser_(test_in_browser),
      test_url_(test_url) {
  }

  void RunTest() override {
    // Time out the test after a reasonable period of time.
    SetTestTimeout();

    // Start pre-setup actions.
    PreSetupStart();
  }

  void PreSetupStart() {
    CefPostTask(TID_FILE,
        base::Bind(&RequestTestHandler::PreSetupFileTasks, this));
  }

  void PreSetupFileTasks() {
    EXPECT_TRUE(CefCurrentlyOn(TID_FILE));

    if (context_mode_ == CONTEXT_ONDISK) {
      EXPECT_TRUE(context_tmpdir_.CreateUniqueTempDir());
      context_tmpdir_path_ = context_tmpdir_.GetPath();
      EXPECT_FALSE(context_tmpdir_path_.empty());
    }

    CefPostTask(TID_UI,
        base::Bind(&RequestTestHandler::PreSetupContinue, this));
  }

  void PreSetupContinue() {
    EXPECT_TRUE(CefCurrentlyOn(TID_UI));

    test_runner_ = new RequestTestRunner(this, true);

    // Get or create the request context.
    if (context_mode_ == CONTEXT_GLOBAL) {
      CefRefPtr<CefRequestContext> request_context =
          CefRequestContext::GetGlobalContext();
      EXPECT_TRUE(request_context.get());
      test_runner_->SetRequestContext(request_context);

      PreSetupComplete();
    } else {
      // Don't end the test until the temporary request context has been
      // destroyed.
      SetSignalCompletionWhenAllBrowsersClose(false);

      CefRequestContextSettings settings;

      if (context_mode_ == CONTEXT_ONDISK) {
        EXPECT_FALSE(context_tmpdir_.IsEmpty());
        CefString(&settings.cache_path) = context_tmpdir_path_;
      }

      // Create a new temporary request context.
      CefRefPtr<CefRequestContext> request_context =
          CefRequestContext::CreateContext(settings,
                                           new RequestContextHandler(this));
      EXPECT_TRUE(request_context.get());
      test_runner_->SetRequestContext(request_context);

      // Set the schemes that are allowed to store cookies.
      std::vector<CefString> supported_schemes;
      supported_schemes.push_back(kRequestScheme);

      // Continue the test once supported schemes has been set.
      request_context->GetDefaultCookieManager(NULL)->SetSupportedSchemes(
          supported_schemes,
          new SupportedSchemesCompletionCallback(
              base::Bind(&RequestTestHandler::PreSetupComplete, this)));
    }
  }

  void PreSetupComplete() {
    if (!CefCurrentlyOn(TID_UI)) {
      CefPostTask(TID_UI,
          base::Bind(&RequestTestHandler::PreSetupComplete, this));
      return;
    }

    // Setup the test. This will create the objects that we test against and
    // register any scheme handlers.
    test_runner_->SetupTest(test_mode_);
  }

  // Browser process setup is complete.
  void OnRunnerSetupComplete() override {
    // Start post-setup actions.
    PostSetupStart();
  }

  void PostSetupStart() {
    CefPostTask(TID_FILE,
        base::Bind(&RequestTestHandler::PostSetupFileTasks, this));
  }

  void PostSetupFileTasks() {
    EXPECT_TRUE(CefCurrentlyOn(TID_FILE));

    // Don't use WaitableEvent on the UI thread.
    SetTestCookie(test_runner_->GetRequestContext());

    CefPostTask(TID_UI,
        base::Bind(&RequestTestHandler::PostSetupComplete, this));
  }

  void PostSetupComplete() {
    EXPECT_TRUE(CefCurrentlyOn(TID_UI));

    if (test_in_browser_) {
      // Run the test now.
      test_runner_->RunTest(test_mode_);
    } else {
      EXPECT_TRUE(test_url_ != NULL);
      AddResource(test_url_, "<html><body>TEST</body></html>", "text/html");

      // Create a browser to run the test in the renderer process.
      CreateBrowser(test_url_, test_runner_->GetRequestContext());
    }
  }

  void OnLoadEnd(CefRefPtr<CefBrowser> browser,
                 CefRefPtr<CefFrame> frame,
                 int httpStatusCode) override {
    EXPECT_FALSE(test_in_browser_);
    if (frame->IsMain()) {
      CefRefPtr<CefProcessMessage> test_message =
          CefProcessMessage::Create(kRequestTestMsg);
      EXPECT_TRUE(test_message->GetArgumentList()->SetInt(0, test_mode_));

      // Send a message to the renderer process to run the test.
      EXPECT_TRUE(browser->SendProcessMessage(PID_RENDERER, test_message));
    }
  }

  bool OnProcessMessageReceived(
      CefRefPtr<CefBrowser> browser,
      CefProcessId source_process,
      CefRefPtr<CefProcessMessage> message) override {
    EXPECT_TRUE(browser.get());
    EXPECT_EQ(PID_RENDERER, source_process);
    EXPECT_TRUE(message.get());
    EXPECT_TRUE(message->IsReadOnly());
    EXPECT_FALSE(test_in_browser_);

    got_message_.yes();

    if (message->GetArgumentList()->GetBool(0))
      got_success_.yes();

    // Renderer process test is complete.
    PostRunStart();

    return true;
  }

  // Browser process test is complete.
  void OnRunnerRunComplete() override {
    PostRunStart();
  }

  void PostRunStart() {
    CefPostTask(TID_FILE,
        base::Bind(&RequestTestHandler::PostRunFileTasks, this));
  }

  void PostRunFileTasks() {
    EXPECT_TRUE(CefCurrentlyOn(TID_FILE));

    // Don't use WaitableEvent on the UI thread.
    bool has_save_cookie = false;
    GetTestCookie(test_runner_->GetRequestContext(), &has_save_cookie);
    EXPECT_EQ(test_runner_->settings_.expect_save_cookie, has_save_cookie);

    CefPostTask(TID_UI, base::Bind(&RequestTestHandler::PostRunComplete, this));
  }

  void PostRunComplete() {
    EXPECT_TRUE(CefCurrentlyOn(TID_UI));
    DestroyTest();
  }

  void DestroyTest() override {
    TestHandler::DestroyTest();

    // Need to call TestComplete() explicitly if testing in the browser and
    // using the global context. Otherwise, TestComplete() will be called when
    // the browser is destroyed (for render test + global context) or when the
    // temporary context is destroyed.
    const bool call_test_complete =
        (test_in_browser_ && context_mode_ == CONTEXT_GLOBAL);

    // Release our reference to the context. Do not access any object members
    // after this call because |this| might be deleted.
    test_runner_->Destroy();

    if (call_test_complete)
      OnTestComplete();
  }

  void OnTestComplete() {
    if (!CefCurrentlyOn(TID_UI)) {
      CefPostTask(TID_UI,
          base::Bind(&RequestTestHandler::OnTestComplete, this));
      return;
    }

    if (!context_tmpdir_.IsEmpty()) {
      // Wait a bit for cache file handles to close after browser or request
      // context destruction.
      CefPostDelayedTask(TID_FILE,
          base::Bind(&RequestTestHandler::PostTestCompleteFileTasks, this),
                     100);
    } else {
      TestComplete();
    }
  }

  void PostTestCompleteFileTasks() {
    EXPECT_TRUE(CefCurrentlyOn(TID_FILE));

    EXPECT_TRUE(context_tmpdir_.Delete());
    EXPECT_TRUE(context_tmpdir_.IsEmpty());

    CefPostTask(TID_UI, base::Bind(&RequestTestHandler::TestComplete, this));
  }

 private:
  // Used with temporary request contexts to signal test completion once the
  // temporary context has been destroyed.
  class RequestContextHandler : public CefRequestContextHandler {
   public:
    explicit RequestContextHandler(CefRefPtr<RequestTestHandler> test_handler)
        : test_handler_(test_handler) {
    }
    ~RequestContextHandler() override {
      test_handler_->OnTestComplete();
    }

   private:
    CefRefPtr<RequestTestHandler> test_handler_;

    IMPLEMENT_REFCOUNTING(RequestContextHandler);
  };

  // Continue the rest once supported schemes have been set.
  class SupportedSchemesCompletionCallback : public CefCompletionCallback {
   public:
    explicit SupportedSchemesCompletionCallback(
        const base::Closure& complete_callback)
        : complete_callback_(complete_callback) {
      EXPECT_FALSE(complete_callback_.is_null());
    }

    void OnComplete() override {
      complete_callback_.Run();
      complete_callback_.Reset();
    }

   private:
    base::Closure complete_callback_;

    IMPLEMENT_REFCOUNTING(SupportedSchemesCompletionCallback);
  };

  RequestTestMode test_mode_;
  ContextTestMode context_mode_;
  bool test_in_browser_;
  const char* test_url_;

  scoped_refptr<RequestTestRunner> test_runner_;

  CefScopedTempDir context_tmpdir_;
  CefString context_tmpdir_path_;

 public:
  // Only used when the test runs in the render process.
  TrackCallback got_message_;
  TrackCallback got_success_;

  IMPLEMENT_REFCOUNTING(RequestTestHandler);
};

}  // namespace


// Entry point for creating URLRequest renderer test objects.
// Called from client_app_delegates.cc.
void CreateURLRequestRendererTests(ClientAppRenderer::DelegateSet& delegates) {
  delegates.insert(new RequestRendererTest);
}

// Entry point for registering custom schemes.
// Called from client_app_delegates.cc.
void RegisterURLRequestCustomSchemes(
      CefRawPtr<CefSchemeRegistrar> registrar,
      std::vector<CefString>& cookiable_schemes) {
  registrar->AddCustomScheme(kRequestScheme, true, false, false, false, true,
                             false);
  cookiable_schemes.push_back(kRequestScheme);
}


// Helpers for defining URLRequest tests.
#define REQ_TEST_EX(name, test_mode, context_mode, test_in_browser, test_url) \
  TEST(URLRequestTest, name) { \
    CefRefPtr<RequestTestHandler> handler = \
        new RequestTestHandler(test_mode, context_mode, test_in_browser, \
                               test_url); \
    handler->ExecuteTest(); \
    if (!test_in_browser) { \
      EXPECT_TRUE(handler->got_message_); \
      EXPECT_TRUE(handler->got_success_); \
    } \
    ReleaseAndWaitForDestructor(handler); \
  }

#define REQ_TEST(name, test_mode, context_mode, test_in_browser) \
  REQ_TEST_EX(name, test_mode, context_mode, test_in_browser, kRequestTestUrl)


// Define the tests.
#define REQ_TEST_SET(suffix, context_mode) \
  REQ_TEST(BrowserGET##suffix, REQTEST_GET, context_mode, true); \
  REQ_TEST(BrowserGETNoData##suffix, REQTEST_GET_NODATA, context_mode, true); \
  REQ_TEST(BrowserGETAllowCookies##suffix, \
           REQTEST_GET_ALLOWCOOKIES, context_mode, true); \
  REQ_TEST(BrowserGETRedirect##suffix, \
           REQTEST_GET_REDIRECT, context_mode, true); \
  REQ_TEST(BrowserGETReferrer##suffix, \
           REQTEST_GET_REFERRER, context_mode, true); \
  REQ_TEST(BrowserPOST##suffix, REQTEST_POST, context_mode, true); \
  REQ_TEST(BrowserPOSTFile##suffix, REQTEST_POST_FILE, context_mode, true); \
  REQ_TEST(BrowserPOSTWithProgress##suffix, \
           REQTEST_POST_WITHPROGRESS, context_mode, true); \
  REQ_TEST(BrowserHEAD##suffix, REQTEST_HEAD, context_mode, true); \
  REQ_TEST(RendererGET##suffix, REQTEST_GET, context_mode, false); \
  REQ_TEST(RendererGETNoData##suffix, \
           REQTEST_GET_NODATA, context_mode, false); \
  REQ_TEST(RendererGETAllowCookies##suffix, \
           REQTEST_GET_ALLOWCOOKIES, context_mode, false); \
  REQ_TEST(RendererGETRedirect##suffix, \
           REQTEST_GET_REDIRECT, context_mode, false); \
  REQ_TEST(RendererGETReferrer##suffix, \
           REQTEST_GET_REFERRER, context_mode, false); \
  REQ_TEST(RendererPOST##suffix, REQTEST_POST, context_mode, false); \
  REQ_TEST(RendererPOSTWithProgress##suffix, \
           REQTEST_POST_WITHPROGRESS, context_mode, false); \
  REQ_TEST(RendererHEAD##suffix, REQTEST_HEAD, context_mode, false)

REQ_TEST_SET(ContextGlobal, CONTEXT_GLOBAL);
REQ_TEST_SET(ContextInMemory, CONTEXT_INMEMORY);
REQ_TEST_SET(ContextOnDisk, CONTEXT_ONDISK);

namespace {

class InvalidURLTestClient : public CefURLRequestClient {
 public:
  InvalidURLTestClient() {
    event_ = CefWaitableEvent::CreateWaitableEvent(true, false);
  }

  void RunTest() {
    CefPostTask(TID_UI, base::Bind(&InvalidURLTestClient::RunOnUIThread, this));

    // Wait for the test to complete.
    event_->Wait();
  }

  void OnRequestComplete(CefRefPtr<CefURLRequest> client) override {
    EXPECT_EQ(UR_FAILED, client->GetRequestStatus());

    // Let the call stack unwind before signaling completion.
    CefPostTask(TID_UI,
        base::Bind(&InvalidURLTestClient::CompleteOnUIThread, this));
  }

  void OnUploadProgress(CefRefPtr<CefURLRequest> request,
                        int64 current,
                        int64 total) override {
    EXPECT_TRUE(false);  // Not reached.
  }

  void OnDownloadProgress(CefRefPtr<CefURLRequest> request,
                          int64 current,
                          int64 total) override {
    EXPECT_TRUE(false);  // Not reached.
  }

  void OnDownloadData(CefRefPtr<CefURLRequest> request,
                      const void* data,
                      size_t data_length) override {
    EXPECT_TRUE(false);  // Not reached.
  }

  bool GetAuthCredentials(
      bool isProxy,
      const CefString& host,
      int port,
      const CefString& realm,
      const CefString& scheme,
      CefRefPtr<CefAuthCallback> callback) override {
    EXPECT_TRUE(false);  // Not reached.
    return false;
  }

 private:
  void RunOnUIThread() {
    EXPECT_UI_THREAD();
    CefRefPtr<CefRequest> request = CefRequest::Create();
    request->SetMethod("GET");
    request->SetURL("foo://invalidurl");

    CefURLRequest::Create(request, this, NULL);
  }

  void CompleteOnUIThread() {
    EXPECT_UI_THREAD();
    // Signal that the test is complete.
    event_->Signal();
  }

  CefRefPtr<CefWaitableEvent> event_;

  IMPLEMENT_REFCOUNTING(InvalidURLTestClient);
};

}  // namespace

// Verify that failed requests do not leak references.
TEST(URLRequestTest, BrowserInvalidURL) { 
  CefRefPtr<InvalidURLTestClient> client = new InvalidURLTestClient();
  client->RunTest();

  // Verify that there's only one reference to the client.
  EXPECT_TRUE(client->HasOneRef());
}
