#pragma once
#include <curl/curl.h>
#include <absl/container/linked_hash_set.h>

void InstallCurlHooks();

typedef void CURL;
class InterceptorData {
 public:
  InterceptorData(CURL* curl);
  ~InterceptorData();

  auto& user_agent() const { return user_agent_; }
  auto& url() { return url_; }
  auto& post_data() { return post_data_; }
  auto& headers() { return req_header_lines_; }
  auto& fast_error() { return fast_error_; }
  auto& response() { return response_; }

  void AddWriteCallback();
  void AddReqHeaders();
  void PrePerform();

  CURLcode SetOptDetour(CURLoption option, void* para, CURLcode& out);
  CURLcode PerformDetour(CURLcode& out);
  CURLcode GetInfoDetour(CURLINFO info, void* para, CURLcode& out);

 private:
  CURL* curl_;
  int index_{0};
  std::string user_agent_;
  std::string url_;

  std::string post_data_;
  absl::linked_hash_set<std::string> req_header_lines_;
  curl_slist* req_header_list_{nullptr};

  static size_t WriteCallback(char* buffer, size_t size, size_t nitems, void* outstream);
  curl_write_callback write_callback_{nullptr};
  void* write_data_{nullptr};

  CURLcode fast_error_{CURLE_OK};
  bool full_res_replacement_{false};
  std::string response_;
};
