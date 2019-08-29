// Copyright (c) 2012 The Chromium Embedded Framework Authors. All rights
// reserved. Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE file.

#include "libcef/renderer/render_urlrequest_impl.h"

#include <stdint.h>

#include "libcef/common/request_impl.h"
#include "libcef/common/response_impl.h"

#include "base/logging.h"
#include "base/message_loop/message_loop.h"
#include "third_party/WebKit/public/platform/Platform.h"
#include "third_party/WebKit/public/platform/WebString.h"
#include "third_party/WebKit/public/platform/WebURL.h"
#include "third_party/WebKit/public/platform/WebURLError.h"
#include "third_party/WebKit/public/platform/WebURLLoader.h"
#include "third_party/WebKit/public/platform/WebURLLoaderClient.h"
#include "third_party/WebKit/public/platform/WebURLRequest.h"
#include "third_party/WebKit/public/platform/WebURLResponse.h"

using blink::WebString;
using blink::WebURL;
using blink::WebURLError;
using blink::WebURLLoader;
using blink::WebURLRequest;
using blink::WebURLResponse;


namespace {

class CefWebURLLoaderClient : public blink::WebURLLoaderClient {
 public:
  CefWebURLLoaderClient(CefRenderURLRequest::Context* context,
                        int request_flags);
  ~CefWebURLLoaderClient() override;

  // blink::WebURLLoaderClient methods.
  bool WillFollowRedirect(
      WebURLRequest& newRequest,
      const WebURLResponse& redirectResponse) override;
  void DidSendData(
      unsigned long long bytesSent,
      unsigned long long totalBytesToBeSent) override;
  void DidReceiveResponse(
      const WebURLResponse& response) override;
  void DidDownloadData(int dataLength,
                       int encodedDataLength) override;
  void DidReceiveData(const char* data,
                      int dataLength) override;
  void DidReceiveCachedMetadata(const char* data,
                                int dataLength) override;
  void DidFinishLoading(double finishTime,
                        int64_t totalEncodedDataLength,
                        int64_t totalEncodedBodyLength) override;
  void DidFail(const WebURLError& error,
               int64_t totalEncodedDataLength,
               int64_t totalEncodedBodyLength) override;

 protected:
  // The context_ pointer will outlive this object.
  CefRenderURLRequest::Context* context_;
  int request_flags_;
};

}  // namespace


// CefRenderURLRequest::Context -----------------------------------------------

class CefRenderURLRequest::Context
    : public base::RefCountedThreadSafe<CefRenderURLRequest::Context> {
 public:
  Context(CefRefPtr<CefRenderURLRequest> url_request,
          CefRefPtr<CefRequest> request,
          CefRefPtr<CefURLRequestClient> client)
    : url_request_(url_request),
      request_(request),
      client_(client),
      task_runner_(base::MessageLoop::current()->task_runner()),
      status_(UR_IO_PENDING),
      error_code_(ERR_NONE),
      upload_data_size_(0),
      got_upload_progress_complete_(false),
      download_data_received_(0),
      download_data_total_(-1) {
    // Mark the request as read-only.
    static_cast<CefRequestImpl*>(request_.get())->SetReadOnly(true);
  }

  inline bool CalledOnValidThread() {
    return task_runner_->RunsTasksOnCurrentThread();
  }

  bool Start() {
    DCHECK(CalledOnValidThread());

    GURL url = GURL(request_->GetURL().ToString());
    if (!url.is_valid())
      return false;

    loader_.reset(blink::Platform::Current()->CreateURLLoader());
    url_client_.reset(new CefWebURLLoaderClient(this, request_->GetFlags()));

    WebURLRequest urlRequest;
    static_cast<CefRequestImpl*>(request_.get())->Get(urlRequest,
                                                      upload_data_size_);

    loader_->LoadAsynchronously(urlRequest, url_client_.get());
    return true;
  }

  void Cancel() {
    DCHECK(CalledOnValidThread());

    // The request may already be complete.
    if (!loader_.get() || status_ != UR_IO_PENDING)
      return;

    status_ = UR_CANCELED;
    error_code_ = ERR_ABORTED;

    // Will result in a call to OnError().
    loader_->Cancel();
  }

  void OnResponse(const WebURLResponse& response) {
    DCHECK(CalledOnValidThread());

    response_ = CefResponse::Create();
    CefResponseImpl* responseImpl =
        static_cast<CefResponseImpl*>(response_.get());
    responseImpl->Set(response);
    responseImpl->SetReadOnly(true);

    download_data_total_ = response.ExpectedContentLength();
  }

  void OnError(const WebURLError& error) {
    DCHECK(CalledOnValidThread());

    if (status_ == UR_IO_PENDING) {
      status_ = UR_FAILED;
      error_code_ = static_cast<CefURLRequest::ErrorCode>(error.reason);
    }

    OnComplete();
  }

  void OnComplete() {
    DCHECK(CalledOnValidThread());

    if (status_ == UR_IO_PENDING) {
      status_ = UR_SUCCESS;
      NotifyUploadProgressIfNecessary();
    }

    if (loader_.get())
      loader_.reset(NULL);

    DCHECK(url_request_.get());
    client_->OnRequestComplete(url_request_.get());

    // This may result in the Context object being deleted.
    url_request_ = NULL;
  }

  void OnDownloadProgress(int64_t current) {
    DCHECK(CalledOnValidThread());
    DCHECK(url_request_.get());

    NotifyUploadProgressIfNecessary();

    download_data_received_ += current;
    client_->OnDownloadProgress(url_request_.get(), download_data_received_,
        download_data_total_);
  }

  void OnDownloadData(const char* data, int dataLength) {
    DCHECK(CalledOnValidThread());
    DCHECK(url_request_.get());
    client_->OnDownloadData(url_request_.get(), data, dataLength);
  }

  void OnUploadProgress(int64_t current, int64_t total) {
    DCHECK(CalledOnValidThread());
    DCHECK(url_request_.get());
    if (current == total)
      got_upload_progress_complete_ = true;
    client_->OnUploadProgress(url_request_.get(), current, total);
  }

  CefRefPtr<CefRequest> request() { return request_; }
  CefRefPtr<CefURLRequestClient> client() { return client_; }
  CefURLRequest::Status status() { return status_; }
  CefURLRequest::ErrorCode error_code() { return error_code_; }
  CefRefPtr<CefResponse> response() { return response_; }

 private:
  friend class base::RefCountedThreadSafe<CefRenderURLRequest::Context>;

  virtual ~Context() {}

  void NotifyUploadProgressIfNecessary() {
    if (!got_upload_progress_complete_ && upload_data_size_ > 0) {
      // URLFetcher sends upload notifications using a timer and will not send
      // a notification if the request completes too quickly. We therefore
      // send the notification here if necessary.
      client_->OnUploadProgress(url_request_.get(), upload_data_size_,
                                upload_data_size_);
      got_upload_progress_complete_ = true;
    }
  }

  // Members only accessed on the initialization thread.
  CefRefPtr<CefRenderURLRequest> url_request_;
  CefRefPtr<CefRequest> request_;
  CefRefPtr<CefURLRequestClient> client_;
  scoped_refptr<base::SequencedTaskRunner> task_runner_;
  CefURLRequest::Status status_;
  CefURLRequest::ErrorCode error_code_;
  CefRefPtr<CefResponse> response_;
  std::unique_ptr<blink::WebURLLoader> loader_;
  std::unique_ptr<CefWebURLLoaderClient> url_client_;
  int64_t upload_data_size_;
  bool got_upload_progress_complete_;
  int64_t download_data_received_;
  int64_t download_data_total_;
};


// CefWebURLLoaderClient --------------------------------------------------

namespace {

CefWebURLLoaderClient::CefWebURLLoaderClient(
    CefRenderURLRequest::Context* context,
    int request_flags)
  : context_(context),
    request_flags_(request_flags) {
}

CefWebURLLoaderClient::~CefWebURLLoaderClient() {
}

bool CefWebURLLoaderClient::WillFollowRedirect(
    WebURLRequest& newRequest,
    const WebURLResponse& redirectResponse) {
  return true;
}

void CefWebURLLoaderClient::DidSendData(
    unsigned long long bytesSent,
    unsigned long long totalBytesToBeSent) {
  if (request_flags_ & UR_FLAG_REPORT_UPLOAD_PROGRESS)
    context_->OnUploadProgress(bytesSent, totalBytesToBeSent);
}

void CefWebURLLoaderClient::DidReceiveResponse(
    const WebURLResponse& response) {
  context_->OnResponse(response);
}

void CefWebURLLoaderClient::DidDownloadData(int dataLength,
                                            int encodedDataLength) {
}

void CefWebURLLoaderClient::DidReceiveData(const char* data,
                                           int dataLength) {
  context_->OnDownloadProgress(dataLength);

  if (!(request_flags_ & UR_FLAG_NO_DOWNLOAD_DATA))
    context_->OnDownloadData(data, dataLength);
}

void CefWebURLLoaderClient::DidReceiveCachedMetadata(const char* data,
                                                     int dataLength) {
}

void CefWebURLLoaderClient::DidFinishLoading(double finishTime,
                                             int64_t totalEncodedDataLength,
                                             int64_t totalEncodedBodyLength) {
  context_->OnComplete();
}

void CefWebURLLoaderClient::DidFail(const WebURLError& error,
                                    int64_t totalEncodedDataLength,
                                    int64_t totalEncodedBodyLength) {
  context_->OnError(error);
}


}  // namespace


// CefRenderURLRequest --------------------------------------------------------

CefRenderURLRequest::CefRenderURLRequest(
    CefRefPtr<CefRequest> request,
    CefRefPtr<CefURLRequestClient> client) {
  context_ = new Context(this, request, client);
}

CefRenderURLRequest::~CefRenderURLRequest() {
}

bool CefRenderURLRequest::Start() {
  if (!VerifyContext())
    return false;
  return context_->Start();
}

CefRefPtr<CefRequest> CefRenderURLRequest::GetRequest() {
  if (!VerifyContext())
    return NULL;
  return context_->request();
}

CefRefPtr<CefURLRequestClient> CefRenderURLRequest::GetClient() {
  if (!VerifyContext())
    return NULL;
  return context_->client();
}

CefURLRequest::Status CefRenderURLRequest::GetRequestStatus() {
  if (!VerifyContext())
    return UR_UNKNOWN;
  return context_->status();
}

CefURLRequest::ErrorCode CefRenderURLRequest::GetRequestError() {
  if (!VerifyContext())
    return ERR_NONE;
  return context_->error_code();
}

CefRefPtr<CefResponse> CefRenderURLRequest::GetResponse() {
  if (!VerifyContext())
    return NULL;
  return context_->response();
}

void CefRenderURLRequest::Cancel() {
  if (!VerifyContext())
    return;
  return context_->Cancel();
}

bool CefRenderURLRequest::VerifyContext() {
  DCHECK(context_.get());
  if (!context_->CalledOnValidThread()) {
    NOTREACHED() << "called on invalid thread";
    return false;
  }

  return true;
}
