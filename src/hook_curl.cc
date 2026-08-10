#include "hook_curl.h"
#include <MinHook.h>
#include <absl/container/flat_hash_map.h>
#include "curlopts.h"
#include "patternfinder.h"
#include "rift_server_reimpl.h"
#include "riftcr_resources.h"

int curl_handle_count{0};

// #define CALLORIG(F_) F_->FlagCallOrig();
#define CALLORIG(...)

decltype(curl_easy_init)* o_init{nullptr};
CURLcode (*o_open)(CURL**){nullptr};
decltype(curl_easy_cleanup)* o_cleanup{nullptr};
CURLcode (*o_close)(CURL**){nullptr};
decltype(curl_easy_setopt)* o_setopt{nullptr};
CURLcode (*o_perform)(CURL*, bool){nullptr};
decltype(curl_easy_getinfo)* o_getinfo{nullptr};
absl::flat_hash_map<CURL*, std::unique_ptr<InterceptorData>> interceptor_data;

InterceptorData::InterceptorData(CURL* curl) : curl_{curl}, index_{curl_handle_count++} {
  // VLOGF(1, "curl_easy_init() -> {} (#{})", (void*)curl_, index_);
}
InterceptorData::~InterceptorData() {
  // VLOGF(1, "curl_easy_cleanup(#{})", index_);
  if (req_header_list_) {
    curl_slist_free_all(req_header_list_);
  }
}
void InterceptorData::AddWriteCallback() {
  CALLORIG(hook_setopt);
  o_setopt(curl_, CURLOPT_WRITEFUNCTION, &InterceptorData::WriteCallback);
  CALLORIG(hook_setopt);
  o_setopt(curl_, CURLOPT_WRITEDATA, this);
}
size_t InterceptorData::WriteCallback(char* buffer, size_t size, size_t nitems, void* outstream) {
  auto real_size = size * nitems;
  auto inte = reinterpret_cast<InterceptorData*>(outstream);
  inte->response_.append(buffer, real_size);

  if (!inte->full_res_replacement_ && inte->write_callback_) {
    auto n = inte->write_callback_(buffer, size, nitems, inte->write_data_);
    CHECKF(n == real_size, "We Have A Fucking Problem!!!");
  }

  return real_size;
}

void InterceptorData::AddReqHeaders() {
  CHECK(req_header_list_ == nullptr);
  for (const auto& line : req_header_lines_) {
    req_header_list_ = curl_slist_append(req_header_list_, line.c_str());
  }
  CALLORIG(hook_setopt);
  o_setopt(curl_, CURLOPT_HTTPHEADER, req_header_list_);
}

CURLcode InterceptorData::SetOptDetour(CURLoption option, void* para, CURLcode& out) {
  switch (option) {
    case CURLOPT_URL:
      url_.assign(reinterpret_cast<const char*>(para));
      para = url_.data();
      break;
    case CURLOPT_POSTFIELDS:
      post_data_.assign(reinterpret_cast<const char*>(para));
      para = post_data_.data();
      break;
    case CURLOPT_USERAGENT:
      user_agent_.assign(reinterpret_cast<const char*>(para));
      para = user_agent_.data();
      break;

    case CURLOPT_SSL_VERIFYPEER:
    case CURLOPT_SSL_VERIFYHOST:
      *reinterpret_cast<long*>(&para) = 0L;
      break;

    case CURLOPT_HTTPHEADER: {
      auto headers = reinterpret_cast<curl_slist*>(para);
      if (headers) {
        do {
          req_header_lines_.emplace(headers->data);
        } while ((headers = headers->next));
      } else {
        req_header_lines_.clear();
        if (req_header_list_) {
          curl_slist_free_all(req_header_list_);
          req_header_list_ = nullptr;
        }
      }
      // we will recreate them before calling perform
      out = CURLE_OK;
      return CURLE_ABORTED_BY_CALLBACK;
    } break;

    case CURLOPT_WRITEFUNCTION:
      write_callback_ = (curl_write_callback)para;
      // para = (void*)&InterceptorData::WriteCallback;
      break;
    case CURLOPT_WRITEDATA:
      write_data_ = para;
      // para = this;
      break;

    default:
      // DLOGF(WARNING, "UNHANDLED OPTION -> curl_easy_setopt(#{}, {}", index_,
      //      CurlOptToString(option));
      break;
  }
  CALLORIG(hook_setopt);
  out = o_setopt(curl_, option, para);
  return CURLE_ABORTED_BY_CALLBACK;
}

void InterceptorData::PrePerform() {
  if (url_ == "https://api.ipify.org") {
    fast_error_ = CURLE_COULDNT_CONNECT;
  } else if (url_.starts_with(kRiftCrProtocol)) {
    RiftCr_PrePerform(*this);
  } else if (url_.starts_with("https://auth.riftbot.org")) {
    RiftAuth_PrePerform(*this);
  }
  // yo fr
  // response_ = "yo fr";
}

CURLcode InterceptorData::PerformDetour(CURLcode& out) {
  AddWriteCallback();
  if (!req_header_list_) {
    AddReqHeaders();
  }
  PrePerform();

  VLOGF(1, "curl_easy_perform(#{})", index_);
  VLOGF(1, "  URL: {}", url_);
  if (!user_agent_.empty()) VLOGF(1, "  User-Agent: {}", user_agent_);
  // if (!post_data_.empty()) VLOGF(1, "  Request: {}", post_data_);
  if (!req_header_lines_.empty()) {
    for (const auto& line : req_header_lines_) {
      VLOGF(1, "  {}", line);
    }
  }
  // Don't give the client a response body just error the perform func
  if (fast_error_ != CURLE_OK) {
    out = fast_error_;
    return CURLE_ABORTED_BY_CALLBACK;
  }

  full_res_replacement_ = response_.size();
  if (full_res_replacement_) {
    VLOGF(1, "  - request handled by PrePerform hook");

    if (url_.starts_with("https://auth.riftbot.org")) {
      RiftAuth_PostPerform(*this);
    }

    if (write_callback_) {
      auto sz = write_callback_(response_.data(), 1, response_.size(), write_data_);
      CHECKF(sz == response_.size(), "Theres a FUCKING PROBLEM");
    }

    out = CURLE_OK;
    return CURLE_ABORTED_BY_CALLBACK;
  } else {
    // Forward to curl
    CALLORIG(hook_perform);
    out = o_perform(curl_, false);

    if (url_.starts_with("https://auth.riftbot.org")) {
      RiftAuth_PostPerform(*this);
    }

    return CURLE_ABORTED_BY_CALLBACK;
  }
}
CURLcode InterceptorData::GetInfoDetour(CURLINFO info, void* para, CURLcode& out) {
  VLOGF(1, "curl_easy_getinfo(#{}, {}", index_, CurlInfoToString(info));
  if (info == CURLINFO_RESPONSE_CODE && full_res_replacement_) {
    *reinterpret_cast<long*>(para) = 200L;
    out = CURLE_OK;
    return CURLE_ABORTED_BY_CALLBACK;
  }

  CALLORIG(hook_getinfo);
  out = o_getinfo(curl_, info, para);
  return CURLE_ABORTED_BY_CALLBACK;
}

namespace hooks {
CURL* curl_easy_init() {
  CALLORIG(hook_init);
  auto curl = o_init();
  interceptor_data.emplace(curl, std::make_unique<InterceptorData>(curl));
  return curl;
}
CURLcode Curl_open(CURL** curl) {
  CALLORIG(hook_open);
  auto code = o_open(curl);
  if (code == CURLE_OK) {
    interceptor_data.emplace(*curl, std::make_unique<InterceptorData>(*curl));
  }
  return code;
}
// CURL* curl_easy_duphandle(CURL* old_curl) {
// hook_duphandle->FlagCallOrig();
// auto new_curl = ::curl_easy_duphandle(old_curl);
// TODO: clone original?
// interceptor_data.emplace(new_curl, std::make_unique<InterceptorData>(new_curl));
// return new_curl;
// }
void curl_easy_cleanup(CURL* curl) {
  interceptor_data.erase(curl);
  CALLORIG(hook_cleanup);
  o_cleanup(curl);
}
CURLcode Curl_close(CURL** curl) {
  if (curl) {
    interceptor_data.erase(*curl);
  }
  CALLORIG(hook_close);
  return o_close(curl);
}
CURLcode curl_easy_setopt(CURL* curl, CURLoption option, void* para) {
  auto& interceptor = interceptor_data[curl];
  CURLcode code = CURLE_OK;
  if (interceptor->SetOptDetour(option, para, code) == CURLE_ABORTED_BY_CALLBACK) {
    // BLOCK
    return code;
  }

  CALLORIG(hook_setopt);
  code = o_setopt(curl, option, para);
  return code;
}
CURLcode easy_perform(CURL* curl, bool events) {
  auto& interceptor = interceptor_data[curl];
  CURLcode code = CURLE_OK;
  if (interceptor->PerformDetour(code) == CURLE_ABORTED_BY_CALLBACK) {
    // BLOCK
    return code;
  }

  CALLORIG(hook_perform);
  code = o_perform(curl, events);
  return code;
}
CURLcode curl_easy_getinfo(CURL* curl, CURLINFO info, void* para) {
  auto& interceptor = interceptor_data[curl];
  CURLcode code = CURLE_OK;
  if (interceptor->GetInfoDetour(info, para, code) == CURLE_ABORTED_BY_CALLBACK) {
    // BLOCK
    return code;
  }
  CALLORIG(hook_getinfo);
  code = o_getinfo(curl, info, para);
  return code;
}

}  // namespace hooks

// CURL* curl_easy_init(void) {
//   CURLcode result;
//   struct Curl_easy* data;
//   result = Curl_open(&data);
//   if (result) {
//     return NULL;
//   }
//   return data;
// }
#define SCAN(Out_, Pattern_)                            \
  Out_ = (decltype(Out_))PatternScan(module, Pattern_); \
  CHECK(Out_);                                          \
  VLOGF(1, "Located {} @ {}", #Out_, (void*)Out_);
void ScanForCurlFunctions() {
  void* module = GetModuleHandleA(nullptr);

  // clang-format off
  SCAN(o_open,    "48 89 5C 24 ? 57 48 83 EC ? 48 8B F9 BA ? ? ? ? B9 ? ? ? ?"              ); // Curl_open        GOOD? 7-aug-26 - Matches many but first match is correct
  SCAN(o_close,   "40 53 48 83 EC ? 48 85 C9 0F 84 ? 03 00 00 48 8B 19 48 85 DB"            ); // Curl_close       GOOD  7-aug-26
  SCAN(o_setopt,  "89 54 24 ? 4C 89 44 24 ? 4C 89 4C 24 ? 53 48 83 EC ? 48 8B D9 48 85 C9"  ); // curl_easy_setopt GOOD  7-aug-26
  SCAN(o_perform, "40 53 55 56 48 83 EC ? 0F B6 DA 48 8B F1 48 85 C9"                       ); // easy_perform     GOOD  7-aug-26
  SCAN(o_getinfo, "89 54 24 ? 4C 89 44 24 ? 4C 89 4C 24 ? 53 48 83 EC ? 4C 8B D1 BB ? ? ? ?"); // Curl_getinfo     GOOD  7-aug-26
  // clang-format on
}
#undef SCAN

void InstallCurlHooks() {
  ScanForCurlFunctions();

  auto open = (void*)o_open;
  auto close = (void*)o_close;
  auto setopt = (void*)o_setopt;
  auto perform = (void*)o_perform;
  auto getinfo = (void*)o_getinfo;

  MH_Assert(MH_CreateHook(open, (void*)&hooks::Curl_open, (void**)&o_open));
  MH_Assert(MH_QueueEnableHook(open));
  MH_Assert(MH_CreateHook(close, (void*)&hooks::Curl_close, (void**)&o_close));
  MH_Assert(MH_QueueEnableHook(close));
  MH_Assert(MH_CreateHook(setopt, (void*)&hooks::curl_easy_setopt, (void**)&o_setopt));
  MH_Assert(MH_QueueEnableHook(setopt));
  MH_Assert(MH_CreateHook(perform, (void*)&hooks::easy_perform, (void**)&o_perform));
  MH_Assert(MH_QueueEnableHook(perform));
  MH_Assert(MH_CreateHook(getinfo, (void*)&hooks::curl_easy_getinfo, (void**)&o_getinfo));
  MH_Assert(MH_QueueEnableHook(getinfo));

  MH_Assert(MH_ApplyQueued());
  LOGF(INFO, "Installed cURL hooks");
}
