#include "downloader.h"
#include <ShlObj_core.h>
#include <ShlGuid.h>
#include <absl/container/flat_hash_map.h>
#include <absl/container/flat_hash_set.h>
#include <absl/strings/str_cat.h>
#include <wil/com.h>
#include <wil/result.h>
#include <wil/result_macros.h>
#include <wil/win32_helpers.h>
#include <cstdio>
#include <filesystem>
#include <format>
#include <array>
#include "misc.h"
#include <fstream>
#include <queue>
#include <stdexcept>
#include <curl/curl.h>
#include "globals.h"

constexpr std::array<FileInfo, 21> kModelList{
    FileInfo{"Experimental"sv, 10958032},
    FileInfo{"KusoV1"sv, 18777179},
    FileInfo{"KusoV1.5"sv, 41072093},
    FileInfo{"KusoV2_Lightweight_Version"sv, 30801429},
    FileInfo{"KusoV2_Max_Version"sv, 119745890},
    FileInfo{"KusoV2.5_2v2"sv, 41252295},
    FileInfo{"KusoV2.9_2v2"sv, 119745439},
    FileInfo{"KusoV3_2v2"sv, 119745234},
    FileInfo{"Nyx"sv, 33390133},
    FileInfo{"SkyBot"sv, 30276940},
    FileInfo{"SkyBotV2"sv, 30277454},
    FileInfo{"SkyBotV3"sv, 30277007},
    FileInfo{"SysoBot-Multi"sv, 32677372},
    FileInfo{"SysoBotV1"sv, 15892908},
    FileInfo{"SysoBotV2"sv, 32677784},
    FileInfo{"SysoBotV2_1v1"sv, 29464294},
    FileInfo{"Tromso19.5B"sv, 10959295},
    FileInfo{"TromsoAD-Preflip"sv, 30277311},
    FileInfo{"TromsoBump"sv, 30276606},
    FileInfo{"TromsoGP"sv, 10958102},
    FileInfo{"TromsoNewest"sv, 10958516},
};

Downloader::Downloader() {
  model_path_ = fs::current_path() / "models";
  // model_path_ = R"(c:\release\riftcr\models)";

  if (!fs::exists(model_path_)) {
    fs::create_directory(model_path_);
    VLOGF(1, "created model directory {}", model_path_.string());
  }
  CreateCurl();
}
Downloader::~Downloader() {}
void Downloader::CreateCurl() {
  curl_ = curl_easy_init();
  if (!curl_) throw std::bad_alloc();

  // the default curl write function is to write to a FILE* in write data.
  curl_easy_setopt(curl_, CURLOPT_WRITEFUNCTION, nullptr);

  curl_easy_setopt(curl_, CURLOPT_XFERINFOFUNCTION, Downloader::XferInfoCallback);
  curl_easy_setopt(curl_, CURLOPT_XFERINFODATA, this);
  curl_easy_setopt(curl_, CURLOPT_NOPROGRESS, 0L);
  curl_easy_setopt(curl_, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(curl_, CURLOPT_VERBOSE, 0L);
}

void PrintIgnoredDownloadMessage() {
  LOGF(INFO,
       "Ignoring download requests. Delete the {} file in the models folder if you would like "
       "to download models later.",
       kIgnoreDownloadSentinel);
}

void Downloader::FindWantedModels() {
  absl::flat_hash_map<std::string, size_t> found_models;
  for (const auto& dir_entry : fs::recursive_directory_iterator(model_path_)) {
    if (!dir_entry.is_regular_file()) continue;
    const auto& model_path = dir_entry.path();

    if (model_path.filename() == kIgnoreDownloadSentinel) {
      PrintIgnoredDownloadMessage();
      return;
    }

    if (model_path.extension() == ".bin") {
      const auto model_name = model_path.parent_path().filename().string();
      found_models.emplace(model_name, dir_entry.file_size());
    }
  }

  for (const auto& model_info : kModelList) {
    total_size_ += model_info.size;
    if (auto it = found_models.find(model_info.name); it != found_models.end()) {
      if (it->second != model_info.size) {
        VLOGF(1, "want download {} - wrong size on disk", model_info.name);
        wanted_downloads_.emplace(DownloaderFile{model_info});
      } else {
        VLOGF(1, "found correct model {}", model_info.name);
        total_size_ -= model_info.size;
      }
    } else {
      VLOGF(1, "want download {} - missing on disk", model_info.name);
      wanted_downloads_.emplace(DownloaderFile{model_info});
    }
  }
}

bool Downloader::HaveWantedDownloads() { return wanted_downloads_.size(); }
void Downloader::Start() {
  dialog_ = wil::CoCreateInstance<IProgressDialog>(CLSID_ProgressDialog, CLSCTX_INPROC_SERVER);
  dialog_->SetTitle(L"Downloading models");
  dialog_->StartProgressDialog(nullptr, nullptr, PROGDLG_NORMAL | PROGDLG_AUTOTIME, nullptr);
  dialog_->SetLine(1, L"Starting download...", false, nullptr);
  dialog_->SetLine(
      2, UTF8ToWideString(std::format("Downloading {} models", wanted_downloads_.size())).c_str(),
      false, nullptr);

  while (wanted_downloads_.size()) {
    cur_file_ = wanted_downloads_.front();
    wanted_downloads_.pop();

    fs::path path;
    std::string url;
    switch (cur_file_.file.type) {
      case kFileModel:
        path = model_path_ / cur_file_.file.name;
        if (!fs::exists(path)) {
          fs::create_directory(path);
        }
        path /= "MODEL.bin";
        url = absl::StrCat(kResourcesBaseUrl, "/models/", cur_file_.file.name, "/MODEL.bin");
        break;
      case kFileLoader:
        path = fs::current_path() / cur_file_.file.name;
        url = absl::StrCat(kResourcesBaseUrl, "/official-loader/", cur_file_.file.name);
        break;
    }

    auto model_file = std::fopen(path.string().c_str(), "wb");
    if (!model_file)
      throw std::runtime_error(
          absl::StrCat("failed to open file '", path.string(), "' for writing"));

    curl_easy_setopt(curl_, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl_, CURLOPT_WRITEDATA, model_file);
    if (auto res = curl_easy_perform(curl_); res != CURLE_OK) {
      LOGF(ERROR, "curl_easy_perform returned {}", std::to_underlying(res));
      if (res == CURLE_ABORTED_BY_CALLBACK) {
        // clear the queue
        wanted_downloads_ = {};
        std::fclose(model_file);
        // delete remnants - or else the crack might load a bad model file
        fs::remove(path);
      }
    }
    std::fclose(model_file);
  }

  dialog_->StopProgressDialog();
}
bool Downloader::UpdateDialog() {
  dialog_->SetProgress(downloaded_size_, total_size_);

  dialog_->SetLine(1, UTF8ToWideString(std::format("Downloading {}", cur_file_.file.name)).c_str(),
                   false, nullptr);
  dialog_->SetLine(
      2,
      UTF8ToWideString(std::format("{} / {} complete", StringifyFileSize(downloaded_size_),
                                   StringifyFileSize(total_size_)))
          .c_str(),
      false, nullptr);

  if (dialog_->HasUserCancelled()) {
    LOGF(INFO, "User cancelled download");
    return false;
  }
  return true;
}
int Downloader::XferInfoCallback(Downloader* self, curl_off_t dltotal, curl_off_t dlnow,
                                 curl_off_t ultotal, curl_off_t ulnow) {
  auto old_dl_size = self->cur_file_.downloaded;
  self->cur_file_.downloaded = dlnow;
  self->cur_file_.real_size = dltotal;
  self->downloaded_size_ += dlnow - old_dl_size;

  if (!self->UpdateDialog()) {
    // ABORT
    return 1;
  }
  return 0;
}
