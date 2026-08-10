#include "hook_winhttp.h"
#include <MinHook.h>
#include <absl/strings/escaping.h>
#include <winhttp.h>
#include <absl/container/flat_hash_map.h>
#include <filesystem>
#include <string>
#include "pch.h"
#include "riftcr_resources.h"
#include <fstream>

// winhttp was made by the devil
decltype(WinHttpOpen)* o_winhttpopen;
decltype(WinHttpConnect)* o_winhttpconnect;
decltype(WinHttpOpenRequest)* o_winhttpopenrequest;
decltype(WinHttpSendRequest)* o_winhttpsendrequest;
decltype(WinHttpReceiveResponse)* o_winhttpreceiveresponse;
decltype(WinHttpQueryDataAvailable)* o_winhttpquerydataavailable;
decltype(WinHttpReadData)* o_winhttpreaddata;
decltype(WinHttpQueryHeaders)* o_winhttpqueryheaders;
decltype(WinHttpCloseHandle)* o_winhttpclosehandle;

struct IdataInfile {
  IdataInfile(const fs::path& path) {
    stream.open(path, std::ios::binary | std::ios::ate);
    total = remaining = stream.tellg();
    stream.seekg(0, std::ios::beg);
  }
  std::ifstream stream;
  size_t remaining{0};
  size_t total{0};
};

class WinHttpIdata {
 public:
  WinHttpIdata(HINTERNET session);
  ~WinHttpIdata();

  static constexpr auto kIdataTag = 0x1128ULL << 48;
  static HINTERNET GetNextHandle() {
    static_assert(sizeof(uintp) == 8);
    uintp val = counter_++;
    val |= kIdataTag;
    return (HINTERNET)val;
  }
  static bool IsIdataHandle(HINTERNET hdl) { return ((uintp)hdl & kIdataTag) == kIdataTag; }

  auto self() const { return self_; }

  HINTERNET OpenRequestDetour(std::wstring_view verb, std::wstring_view object_name, uint32 flags) {
    VLOGF(1, "OpenRequest DETOUR!");

    constexpr auto kPrefix = L"/models/"sv;
    constexpr auto kSuffix = L"/MODEL.bin"sv;

    CHECK(object_name.starts_with(kPrefix));
    CHECK(object_name.ends_with(kSuffix));
    const auto model_name =
        object_name.substr(kPrefix.size(), object_name.size() - kPrefix.size() - kSuffix.size());
    CHECK(!model_name.contains(L'/'));
    CHECK(!model_name.contains(L".."));

    const auto model_dir = fs::current_path() / "models" / model_name;
    if (!fs::exists(model_dir)) {
      fs::create_directories(model_dir);
    }

    const auto model_path = model_dir / "MODEL.bin";
    if (fs::exists(model_path)) {
      input_file_.emplace(model_path);
      VLOGF(1, "open local replay request to {} (len={})", model_path.string(), input_file_->total);
    } else {
      output_file_.emplace(model_path, std::ios::binary);
      connect_fwd_ =
          o_winhttpconnect(session_, kRiftCrResourcesDomain.data(), INTERNET_DEFAULT_PORT, 0);
      THROW_LAST_ERROR_IF_NULL(connect_fwd_);

      std::wstring new_pathname = std::wstring(kRiftCrResourcesPath);
      new_pathname.append(object_name);

      VLOGF(1, "open forwarding request to path {}", WideStringToUTF8(new_pathname));

      request_fwd_ = o_winhttpopenrequest(connect_fwd_, verb.data(), new_pathname.c_str(), nullptr,
                                          nullptr, nullptr, flags);
      THROW_LAST_ERROR_IF_NULL(request_fwd_);
    }

    refs_++;
    return self_;
  }
  BOOL SendRequestDetour(_In_reads_opt_(dwHeadersLength) LPCWSTR lpszHeaders,
                         IN DWORD dwHeadersLength,
                         _In_reads_bytes_opt_(dwOptionalLength) LPVOID lpOptional,
                         IN DWORD dwOptionalLength, IN DWORD dwTotalLength,
                         IN DWORD_PTR dwContext) {
    if (request_fwd_) {
      auto retn = o_winhttpsendrequest(request_fwd_, lpszHeaders, dwHeadersLength, lpOptional,
                                       dwOptionalLength, dwTotalLength, dwContext);
      VLOGF(1, "SendRequest returns {}", retn);
      return retn;
    }
    // VLOGF(1, "SendRequest detour! ");
    return TRUE;
  }
  BOOL ReceiveResponseDetour() {
    if (request_fwd_) {
      auto retn = o_winhttpreceiveresponse(request_fwd_, nullptr);
      VLOGF(1, "ReceiveResponse returns {}", retn);
      return retn;
    }
    VLOGF(1, "ReceiveResponse detour! ");
    return TRUE;
  }
  BOOL QueryDataAvailableDetour(DWORD* bytes_available) {
    if (request_fwd_) {
      auto retn = o_winhttpquerydataavailable(request_fwd_, bytes_available);
      // VLOGF(1, "QDA fowrard: {} returns {}", *bytes_available, retn);
      return retn;
    }
    CHECK(bytes_available);
    CHECK(input_file_.has_value());
    *bytes_available = input_file_->remaining;
    return TRUE;
  }
  BOOL ReadDataDetour(void* buf, DWORD buf_len, DWORD* bytes_read) {
    if (request_fwd_) {
      auto retn = o_winhttpreaddata(request_fwd_, buf, buf_len, bytes_read);

      if (retn && bytes_read && *bytes_read) {
        // VLOGF(1, "ReadData on {}({}) read {} bytes, returns {}", buf, buf_len, *bytes_read,
        // retn); write it to disk now
        output_file_->write((char*)buf, *bytes_read);
      }
      return retn;
    }
    CHECK(buf);
    CHECK(bytes_read);
    CHECK(input_file_.has_value());

    auto try_read_count = std::min<size_t>(buf_len, input_file_->remaining);
    input_file_->stream.read((char*)buf, try_read_count);
    auto actual_read_count = (size_t)input_file_->stream.gcount();
    input_file_->remaining -= actual_read_count;
    *bytes_read = static_cast<DWORD>(actual_read_count);
    return TRUE;
  }
  BOOL QueryHeadersDetour(DWORD dwInfoLevel, LPCWSTR pwszName, LPVOID lpBuffer,
                          LPDWORD lpdwBufferLength, LPDWORD lpdwIndex) {
    if (request_fwd_) {
      // rift dwInfoLevel = 0x20000013
      // WINHTTP_QUERY_FLAG_NUMBER | WINHTTP_QUERY_STATUS_CODE
      auto retn = o_winhttpqueryheaders(request_fwd_, dwInfoLevel, pwszName, lpBuffer,
                                        lpdwBufferLength, lpdwIndex);
      // VLOGF(1, "QueryHeaders!! dwInfoLevel = {:x}", dwInfoLevel);
      // VLOGF(1, "  pwszName = {}", !pwszName ? std::string("empty") :
      // WideStringToUTF8(pwszName)); if (lpBuffer && lpdwBufferLength) {
      //   VLOGF(1, "  lpBuffer (len={}) = {}", *lpdwBufferLength,
      //        absl::BytesToHexString(std::string_view{(char*)lpBuffer,
      //        (size_t)*lpdwBufferLength}));
      // }
      return retn;
    }
    CHECK(input_file_.has_value());
    CHECK(lpdwBufferLength);
    if (dwInfoLevel == (WINHTTP_QUERY_FLAG_NUMBER | WINHTTP_QUERY_STATUS_CODE) &&
        *lpdwBufferLength == 4) {
      *reinterpret_cast<uint32*>(lpBuffer) = 200;
      return TRUE;
    }

    CHECKF(false, "WinHttpQueryHeaders hook is bad");
  }

  bool CloseHandleDetour() { return --refs_ <= 0; }

 private:
  inline static uint counter_{0x11223344};
  int refs_{0};
  HINTERNET self_{nullptr};

  HINTERNET session_{nullptr};
  HINTERNET connect_fwd_{nullptr};
  HINTERNET request_fwd_{nullptr};
  std::optional<std::ofstream> output_file_;
  std::optional<IdataInfile> input_file_;
};

WinHttpIdata::WinHttpIdata(HINTERNET session) : session_(session) {
  VLOGF(1, "WinHttpIdata(session={})", session);
  self_ = GetNextHandle();
  refs_++;
}
WinHttpIdata::~WinHttpIdata() {
  VLOGF(1, "~WinHttpIdata(connect_fwd_={}  request_fwd={})", connect_fwd_, request_fwd_);
  if (connect_fwd_) {
    o_winhttpclosehandle(connect_fwd_);
    connect_fwd_ = nullptr;
  }
  if (request_fwd_) {
    o_winhttpclosehandle(request_fwd_);
    request_fwd_ = nullptr;
  }
}

absl::flat_hash_map<HINTERNET, std::unique_ptr<WinHttpIdata>> winhttp_handles;

#define WARN_INVALID_HANDLE()             \
  LOGF(ERROR, "idata handle is invalid"); \
  SetLastError(ERROR_WINHTTP_INTERNAL_ERROR);

HINTERNET WinHttpOpen_Hook(_In_opt_z_ LPCWSTR pszAgentW, _In_ DWORD dwAccessType,
                           _In_opt_z_ LPCWSTR pszProxyW, _In_opt_z_ LPCWSTR pszProxyBypassW,
                           _In_ DWORD dwFlags) {
  auto hdl = o_winhttpopen(pszAgentW, dwAccessType, pszProxyW, pszProxyBypassW, dwFlags);
  VLOGF(1, "WinHttpOpen: {}", hdl);
  return hdl;
}
HINTERNET WinHttpConnect_Hook(IN HINTERNET hSession, IN LPCWSTR pswzServerName,
                              IN INTERNET_PORT nServerPort, IN DWORD dwReserved) {
  if (std::wstring_view{pswzServerName} == L"riftcr") {
    auto idata = new WinHttpIdata(hSession);
    winhttp_handles.emplace(idata->self(), idata);
    VLOGF(1, "created winhttp handle for connect");
    return idata->self();
  }

  auto hdl = o_winhttpconnect(hSession, pswzServerName, nServerPort, dwReserved);
  VLOGF(1, "WinHttpConnect: server={}  port={}", WideStringToUTF8(pswzServerName), nServerPort);
  return hdl;
}

HINTERNET WinHttpOpenRequest_Hook(IN HINTERNET hConnect, IN LPCWSTR pwszVerb,
                                  IN LPCWSTR pwszObjectName, IN LPCWSTR pwszVersion,
                                  IN LPCWSTR pwszReferrer OPTIONAL,
                                  IN LPCWSTR FAR* ppwszAcceptTypes OPTIONAL, IN DWORD dwFlags) {
  if (WinHttpIdata::IsIdataHandle(hConnect)) {
    CHECK(std::wstring_view{pwszVerb} == L"GET");
    if (auto it = winhttp_handles.find(hConnect); it != winhttp_handles.end()) {
      return it->second->OpenRequestDetour(pwszVerb, pwszObjectName, dwFlags);
    } else {
      WARN_INVALID_HANDLE();
      return nullptr;
    }
  }
  auto hdl = o_winhttpopenrequest(hConnect, pwszVerb, pwszObjectName, pwszVersion, pwszReferrer,
                                  ppwszAcceptTypes, dwFlags);
  VLOGF(1, "WinHttpOpenRequest: method={}  pathname={}", WideStringToUTF8(pwszVerb),
        WideStringToUTF8(pwszObjectName));
  return hdl;
}
BOOL WinHttpSendRequest_Hook(IN HINTERNET hRequest,
                             _In_reads_opt_(dwHeadersLength) LPCWSTR lpszHeaders,
                             IN DWORD dwHeadersLength,
                             _In_reads_bytes_opt_(dwOptionalLength) LPVOID lpOptional,
                             IN DWORD dwOptionalLength, IN DWORD dwTotalLength,
                             IN DWORD_PTR dwContext) {
  if (WinHttpIdata::IsIdataHandle(hRequest)) {
    if (auto it = winhttp_handles.find(hRequest); it != winhttp_handles.end()) {
      return it->second->SendRequestDetour(lpszHeaders, dwHeadersLength, lpOptional,
                                           dwOptionalLength, dwTotalLength, dwContext);
    } else {
      WARN_INVALID_HANDLE();
      return FALSE;
    }
  }
  VLOGF(1, "WinHttpSendRequest");
  return o_winhttpsendrequest(hRequest, lpszHeaders, dwHeadersLength, lpOptional, dwOptionalLength,
                              dwTotalLength, dwContext);
}
BOOL WinHttpReceiveResponse_Hook(IN HINTERNET hRequest, IN LPVOID lpReserved) {
  if (WinHttpIdata::IsIdataHandle(hRequest)) {
    if (auto it = winhttp_handles.find(hRequest); it != winhttp_handles.end()) {
      return it->second->ReceiveResponseDetour();
    } else {
      WARN_INVALID_HANDLE();
      return FALSE;
    }
  }
  VLOGF(1, "WinHttpReceiveResponse");
  return o_winhttpreceiveresponse(hRequest, lpReserved);
}
BOOL WinHttpQueryDataAvailable_Hook(IN HINTERNET hRequest,
                                    __out_data_source(NETWORK) LPDWORD lpdwNumberOfBytesAvailable) {
  if (WinHttpIdata::IsIdataHandle(hRequest)) {
    if (auto it = winhttp_handles.find(hRequest); it != winhttp_handles.end()) {
      return it->second->QueryDataAvailableDetour(lpdwNumberOfBytesAvailable);
    } else {
      WARN_INVALID_HANDLE();
      return FALSE;
    }
  }
  auto retn = o_winhttpquerydataavailable(hRequest, lpdwNumberOfBytesAvailable);
  LOG_EVERY_N_SEC(INFO, 4) << std::format(
      "WinHttpQueryDataAvailable returns {} (bytes available={})", (bool)retn,
      *lpdwNumberOfBytesAvailable);
  return retn;
}
BOOL WinHttpReadData_Hook(IN HINTERNET hRequest,
                          _Out_writes_bytes_to_(dwNumberOfBytesToRead, *lpdwNumberOfBytesRead)
                              __out_data_source(NETWORK) LPVOID lpBuffer,
                          IN DWORD dwNumberOfBytesToRead, OUT LPDWORD lpdwNumberOfBytesRead) {
  if (WinHttpIdata::IsIdataHandle(hRequest)) {
    if (auto it = winhttp_handles.find(hRequest); it != winhttp_handles.end()) {
      return it->second->ReadDataDetour(lpBuffer, dwNumberOfBytesToRead, lpdwNumberOfBytesRead);
    } else {
      WARN_INVALID_HANDLE();
      return FALSE;
    }
  }
  // VLOGF(1, "WinHttpReadData");
  return o_winhttpreaddata(hRequest, lpBuffer, dwNumberOfBytesToRead, lpdwNumberOfBytesRead);
}
BOOL WinHttpQueryHeaders_Hook(HINTERNET hRequest, DWORD dwInfoLevel, LPCWSTR pwszName,
                              LPVOID lpBuffer, LPDWORD lpdwBufferLength, LPDWORD lpdwIndex) {
  if (WinHttpIdata::IsIdataHandle(hRequest)) {
    if (auto it = winhttp_handles.find(hRequest); it != winhttp_handles.end()) {
      return it->second->QueryHeadersDetour(dwInfoLevel, pwszName, lpBuffer, lpdwBufferLength,
                                            lpdwIndex);
    } else {
      WARN_INVALID_HANDLE();
      return FALSE;
    }
  }
  VLOGF(1, "WinHttpQueryHeaders");
  return o_winhttpqueryheaders(hRequest, dwInfoLevel, pwszName, lpBuffer, lpdwBufferLength,
                               lpdwIndex);
}
BOOL WinHttpCloseHandle_Hook(IN HINTERNET hInternet) {
  if (WinHttpIdata::IsIdataHandle(hInternet)) {
    if (auto it = winhttp_handles.find(hInternet); it != winhttp_handles.end()) {
      if (it->second->CloseHandleDetour()) {
        winhttp_handles.erase(hInternet);
      }
      return TRUE;
    } else {
      WARN_INVALID_HANDLE();
      return FALSE;
    }
  }
  VLOGF(1, "WinHttpCloseHandle: {}", hInternet);
  return o_winhttpclosehandle(hInternet);
}

void InstallWinHttpHooks() {
  auto winhttp = LoadLibraryA("winhttp.dll");

  const auto winhttp_WinHttpOpen = reinterpret_cast<void*>(GetProcAddress(winhttp, "WinHttpOpen"));
  const auto winhttp_WinHttpConnect =
      reinterpret_cast<void*>(GetProcAddress(winhttp, "WinHttpConnect"));
  const auto winhttp_WinHttpOpenRequest =
      reinterpret_cast<void*>(GetProcAddress(winhttp, "WinHttpOpenRequest"));
  const auto winhttp_WinHttpSendRequest =
      reinterpret_cast<void*>(GetProcAddress(winhttp, "WinHttpSendRequest"));
  const auto winhttp_WinHttpReceiveResponse =
      reinterpret_cast<void*>(GetProcAddress(winhttp, "WinHttpReceiveResponse"));
  const auto winhttp_WinHttpQueryDataAvailable =
      reinterpret_cast<void*>(GetProcAddress(winhttp, "WinHttpQueryDataAvailable"));
  const auto winhttp_WinHttpReadData =
      reinterpret_cast<void*>(GetProcAddress(winhttp, "WinHttpReadData"));
  const auto winhttp_WinHttpQueryHeaders =
      reinterpret_cast<void*>(GetProcAddress(winhttp, "WinHttpQueryHeaders"));
  const auto winhttp_WinHttpCloseHandle =
      reinterpret_cast<void*>(GetProcAddress(winhttp, "WinHttpCloseHandle"));

  MH_Assert(MH_CreateHook(winhttp_WinHttpOpen, reinterpret_cast<void*>(WinHttpOpen_Hook),
                          reinterpret_cast<void**>(&o_winhttpopen)));
  MH_Assert(MH_QueueEnableHook(winhttp_WinHttpOpen));
  MH_Assert(MH_CreateHook(winhttp_WinHttpConnect, reinterpret_cast<void*>(WinHttpConnect_Hook),
                          reinterpret_cast<void**>(&o_winhttpconnect)));
  MH_Assert(MH_QueueEnableHook(winhttp_WinHttpConnect));
  MH_Assert(MH_CreateHook(winhttp_WinHttpOpenRequest,
                          reinterpret_cast<void*>(WinHttpOpenRequest_Hook),
                          reinterpret_cast<void**>(&o_winhttpopenrequest)));
  MH_Assert(MH_QueueEnableHook(winhttp_WinHttpOpenRequest));
  MH_Assert(MH_CreateHook(winhttp_WinHttpSendRequest,
                          reinterpret_cast<void*>(WinHttpSendRequest_Hook),
                          reinterpret_cast<void**>(&o_winhttpsendrequest)));
  MH_Assert(MH_QueueEnableHook(winhttp_WinHttpSendRequest));
  MH_Assert(MH_CreateHook(winhttp_WinHttpReceiveResponse,
                          reinterpret_cast<void*>(WinHttpReceiveResponse_Hook),
                          reinterpret_cast<void**>(&o_winhttpreceiveresponse)));
  MH_Assert(MH_QueueEnableHook(winhttp_WinHttpReceiveResponse));
  MH_Assert(MH_CreateHook(winhttp_WinHttpQueryDataAvailable,
                          reinterpret_cast<void*>(WinHttpQueryDataAvailable_Hook),
                          reinterpret_cast<void**>(&o_winhttpquerydataavailable)));
  MH_Assert(MH_QueueEnableHook(winhttp_WinHttpQueryDataAvailable));
  MH_Assert(MH_CreateHook(winhttp_WinHttpReadData, reinterpret_cast<void*>(WinHttpReadData_Hook),
                          reinterpret_cast<void**>(&o_winhttpreaddata)));
  MH_Assert(MH_QueueEnableHook(winhttp_WinHttpReadData));
  MH_Assert(MH_CreateHook(winhttp_WinHttpQueryHeaders,
                          reinterpret_cast<void*>(WinHttpQueryHeaders_Hook),
                          reinterpret_cast<void**>(&o_winhttpqueryheaders)));
  MH_Assert(MH_QueueEnableHook(winhttp_WinHttpQueryHeaders));
  MH_Assert(MH_CreateHook(winhttp_WinHttpCloseHandle,
                          reinterpret_cast<void*>(WinHttpCloseHandle_Hook),
                          reinterpret_cast<void**>(&o_winhttpclosehandle)));
  MH_Assert(MH_QueueEnableHook(winhttp_WinHttpCloseHandle));

  MH_Assert(MH_ApplyQueued());
  LOGF(INFO, "Installed WinHTTP hooks");
}
