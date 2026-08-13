#pragma once
#include <curl/curl.h>
#include <filesystem>
#include <queue>
#include <wil/com.h>
#include <ShlObj.h>
// path : size in bytes

using ModelPair = std::pair<std::string_view, size_t>;
enum FileType {
  kFileModel,
  kFileLoader,
};
struct FileInfo {
  std::string_view name;
  size_t size;
  FileType type{kFileModel};
};
struct DownloaderFile {
  FileInfo file;
  size_t expected_size;
  size_t downloaded{0};
  size_t real_size{0};
};

constexpr auto kIgnoreDownloadSentinel = ".downloads_ignored"sv;
constexpr auto kResourcesBaseUrl =
    "https://media.githubusercontent.com/media/riftcr/resources/"
    "cae0f6173014d1116a7eb95cb6c23b7dcc6bc179/";
constexpr FileInfo kLoaderFile{"RiftLoaderV3.exe", 88'546'304, kFileLoader};
// The new loaders don't work 😂
// constexpr FileInfo kLoaderFile{"RiftLoaderV3.exe", 71'017'984, kFileLoader};

void PrintIgnoredDownloadMessage();

class Downloader {
 public:
  Downloader();
  ~Downloader();

  auto total_size() const { return total_size_; }
  auto& model_path() const { return model_path_; }
  void AddWantedDownload(FileInfo file) {
    total_size_ += file.size;
    wanted_downloads_.emplace(std::move(file));
  }

  auto WantedDownloadCount() const { return wanted_downloads_.size(); }
  bool HaveWantedDownloads();

  void FindWantedModels();

  void Start();

 private:
  void CreateCurl();
  // returns false if aborted
  bool UpdateDialog();

  static int XferInfoCallback(Downloader* self, curl_off_t dltotal, curl_off_t dlnow,
                              curl_off_t ultotal, curl_off_t ulnow);

 private:
  CURL* curl_{nullptr};
  wil::com_ptr<IProgressDialog> dialog_{nullptr};
  fs::path model_path_{};
  size_t downloaded_size_{0};
  size_t total_size_{0};
  DownloaderFile cur_file_{};
  std::queue<DownloaderFile> wanted_downloads_;
};
