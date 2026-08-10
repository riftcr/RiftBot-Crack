#include <absl/flags/parse.h>
#include <absl/debugging/symbolize.h>
#include <absl/log/flags.h>
#include <absl/log/globals.h>
#include <absl/log/initialize.h>
#include "downloader.h"
#include "launcher.h"
#include "misc.h"
#include "pch.h"
#include <absl/strings/str_cat.h>
#include <wil/com.h>
#include <wil/result.h>
#include <wil/result_macros.h>
#include <wil/win32_helpers.h>
#include <winuser.h>
#include <memory>
#include <print>

fs::path g_SelfPath;
fs::path g_RiftLoaderPath;

bool FindLoader() {
  if (!g_RiftLoaderPath.empty()) return true;

  for (const auto& dir_entry : fs::directory_iterator(fs::current_path())) {
    if (dir_entry.is_regular_file() && dir_entry.file_size() == kLoaderFile.size &&
        dir_entry.path().extension() == ".exe") {
      g_RiftLoaderPath = fs::absolute(dir_entry.path());
      VLOGF(1, "Found loader: {}", g_RiftLoaderPath.string());
      return true;
    }
  }
  return false;
}

void CheckNotRunningFromTemp() {
  wchar_t temp_dir[MAX_PATH];
  THROW_LAST_ERROR_IF(!GetTempPathW(MAX_PATH, temp_dir));

  auto module_file_name = wil::GetModuleFileNameW();
  g_SelfPath = module_file_name.get();

  std::wstring lower_temp = temp_dir;
  std::wstring lower_filename = module_file_name.get();
  std::ranges::transform(lower_temp, lower_temp.begin(), towlower);
  std::ranges::transform(lower_filename, lower_filename.begin(), towlower);

  if (lower_filename.starts_with(lower_temp)) {
    MessageBoxA(nullptr, "Please extract RiftCrack to a permanent folder before running it",
                "[RiftCrack] Error", MB_OK | MB_ICONHAND);
    std::exit(0);
  }
}

void CheckForLoaderDownload() {
  Downloader dl;
  if (!FindLoader()) {
    dl.AddWantedDownload(kLoaderFile);
  }

  const auto wanted_dls = dl.WantedDownloadCount();
  if (wanted_dls) {
    auto msg = std::format(
        "You are missing the correct version of RiftLoaderV3. This "
        "program cannot function without it.\n\n"
        "Total download size: {}\n\n"
        "Select [Yes] to begin the download\n\n"
        "Select [No] to close the program now",
        StringifyFileSize(dl.total_size()));

    if (auto result = MessageBoxA(nullptr, msg.c_str(), "[RiftCrack] Download Rift loader?",
                                  MB_YESNO | MB_ICONEXCLAMATION);
        result == IDNO) {
      LOGF(INFO, "User cancelled loader download");
      std::exit(0);
    }
    dl.Start();
  }
}

void CheckForModelDownloads() {
  Downloader dl;
  dl.FindWantedModels();
  const auto wanted_dls = dl.WantedDownloadCount();
  if (wanted_dls) {
    VLOGF(1, "Want {} models ({})", wanted_dls, StringifyFileSize(dl.total_size()));
    auto msg = std::format(
        "You are missing {} AI bot model(s). It is highly recommended that you download them "
        "now. Depending on your internet connection this may take a long time.\n\n"
        "Total download size: {}\n\n"
        "Select [Yes] to begin the download\n\n"
        "Select [No] to cancel download.",
        wanted_dls, StringifyFileSize(dl.total_size()));

    if (auto result = MessageBoxA(nullptr, msg.c_str(), "[RiftCrack] Download models?",
                                  MB_YESNO | MB_ICONQUESTION);
        result == IDNO) {
      // create an empty file there
      std::fclose(std::fopen((dl.model_path() / kIgnoreDownloadSentinel).string().c_str(), "w"));
      PrintIgnoredDownloadMessage();
      return;
    }
    dl.Start();
  } else {
    LOGF(INFO, "No model downloads needed!");
  }
}

const size_t GetConsoleColumnCount() {
  CONSOLE_SCREEN_BUFFER_INFO csbi;
  THROW_LAST_ERROR_IF(!GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi));
  return csbi.srWindow.Right - csbi.srWindow.Left + 1;
}

void PrintDivider() { std::println("{}", std::string(GetConsoleColumnCount(), '=')); }

void PrintlnNofmtCentered(std::string s) {
  auto gap = (GetConsoleColumnCount() - s.size()) / 2;
  s.insert(s.begin(), gap, ' ');
  s.insert(s.end(), gap, ' ');
  std::println("{}", s);
}

int main(int argc, char** argv) {
  CHECK(argc > 0);
  absl::ParseCommandLine(argc, argv);
  absl::InitializeSymbolizer(argv[0]);
  absl::InitializeLog();
  absl::SetStderrThreshold(absl::LogSeverityAtLeast::kInfo);
#ifndef NDEBUG
  absl::SetGlobalVLogLevel(100);
#endif

  try {
    PrintDivider();
    std::println();
    PrintlnNofmtCentered("Rift 3 crack");
    PrintlnNofmtCentered(
        "[ https://github.com/riftcr/RiftBot-Crack ] | [ https://t.me/riftcrack ]");
    std::println();
    PrintDivider();
    std::println();
    std::println(
        "RiftBot Crack is licensed under the Anyone But the RiftBot Staff (ABRS) License. If you "
        "believe you may fall within the license's restricted class, please review the full "
        "license at https://github.com/riftcr/RiftBot-Crack to understand the rights afforded to "
        "you.");
    std::println();

    auto init = wil::CoInitializeEx(COINIT_MULTITHREADED);

    std::optional<std::string_view> launch_path{};
    for (std::string_view sv : std::span{argv, static_cast<size_t>(argc)}) {
      if (launch_path.has_value()) {
        launch_path = sv;
      } else if (sv == "/launch") {
        launch_path.emplace();
      }
    }

    CheckNotRunningFromTemp();
    CheckForLoaderDownload();
    CheckForModelDownloads();
    if (launch_path.has_value()) {
      Launch(launch_path.value());
    } else {
      // install/uninstall
      CheckLauncherStatus();
    }
  } catch (std::exception& ex) {
    LOGF(ERROR, "error: {}", ex.what());
    MessageBoxA(nullptr,
                absl::StrCat("An unexpected error occurred while running RiftCrack. Check the "
                             "console for more details.\n\n",
                             ex.what())
                    .c_str(),
                "[RiftCrack] Error", MB_OK | MB_ICONHAND);
  }
}
