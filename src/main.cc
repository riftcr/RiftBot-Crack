#include <absl/flags/parse.h>
#include <absl/debugging/symbolize.h>
#include "enumthreads.h"
#include "hook_gui.h"
#include "hook_initial.h"
#include "hook_winhttp.h"
#include "syscalls.h"
#include <absl/log/globals.h>
#include <minwinbase.h>
#include "hook_curl.h"
#include "hook_ossl.h"
#include "console.h"
#include <MinHook.h>
#include <ntstatus.h>
#include <winuser.h>
#include <memory>
#include <wil/win32_helpers.h>

std::unique_ptr<Console> console;

BOOL EnumFunc(HWND hwnd, bool* param) {
  char class_name[32];
  auto class_name_len = RealGetWindowClassA(hwnd, class_name, sizeof(class_name) - 1);
  if (class_name_len > 0) {
    const std::string_view class_name_sv{class_name, class_name_len};
    if (class_name_sv == "licsvc_main" || class_name_sv == "LicensingServicesHud") {
      VLOGF(1, "found window class {}; already unpacked", class_name_sv);
      *param = true;
      return FALSE;
    }
  }
  return TRUE;
}

// Naive method of detecting if Rift was already unpacked before injecting
void CheckAlreadyUnpacked(bool& out_already_unpacked) {
  std::ignore = EnumWindows(reinterpret_cast<WNDENUMPROC>(EnumFunc),
                            reinterpret_cast<LPARAM>(&out_already_unpacked));
}

void AfterUnpacked();

// Check for suspended threads and resume them (crack launcher keeps the main thread suspended)
void ResumeAllThreads() {
  EnumerateThreads([](SYSTEM_THREAD_INFORMATION& thread) {
    if (thread.ThreadState == Waiting &&
        (thread.WaitReason == Suspended || thread.WaitReason == WrSuspended)) {
      VLOGF(1, "resuming thread {}", (uintp)thread.ClientId.UniqueThread);
      wil::unique_handle thread_handle;
      OBJECT_ATTRIBUTES oa;
      InitializeObjectAttributes(&oa, nullptr, 0, nullptr, nullptr);
      THROW_IF_NTSTATUS_FAILED(
          NtOpenThread(&thread_handle, THREAD_ALL_ACCESS, &oa, &thread.ClientId));
      NtResumeThread(thread_handle.get(), nullptr);
    }
  });
}

uintp g_RiftLoaderBase{0};
uintp g_RiftLoaderSizeOfImage{0};

DWORD MainThread(HMODULE mod) {
  console = std::make_unique<Console>();
  // https://youtu.be/U0f_4cBXjSY
  LOGF(INFO, "started");
  InitSyscalls();
  MH_Initialize();

  {
    g_RiftLoaderBase = (uintp)GetModuleHandleA(nullptr);

    auto dos = (IMAGE_DOS_HEADER*)g_RiftLoaderBase;
    auto nt = (IMAGE_NT_HEADERS*)(g_RiftLoaderBase + dos->e_lfanew);

    g_RiftLoaderSizeOfImage = nt->OptionalHeader.SizeOfImage;

    LOGF(INFO, "RiftLoader base/size = {}/{:x}h", (void*)g_RiftLoaderBase, g_RiftLoaderSizeOfImage);
  }

#ifndef NDEBUG
  absl::SetGlobalVLogLevel(100);
#endif

  LOGF(INFO,
       "RiftBot Crack is licensed under the Anyone But the RiftBot Staff (ABRS) License. If you "
       "believe you may fall within the license's restricted class, please review the full "
       "license at https://github.com/riftcr/RiftBot-Crack/blob/master/COPYING.md to understand "
       "the rights afforded to you.");

  bool already_unpacked{false};
  CheckAlreadyUnpacked(already_unpacked);

  InstallInitialHooks(already_unpacked);
  if (already_unpacked) {
    AfterUnpacked();
  }

  try {
    ResumeAllThreads();
  } catch (std::exception& ex) {
    LOGF(INFO, "error: {}", ex.what());
  }

  return 0;
}

void AfterUnpacked() {
  LOGF(INFO, "should be unpacked now");
  InstallOpenSSLHooks();
  InstallCurlHooks();
  InstallWinHttpHooks();
  InstallGuiHooks();
}

BOOL WINAPI DllMain(HMODULE mod, DWORD reason, LPVOID reserved) {
  if (reason == DLL_PROCESS_ATTACH) {
    CreateThread(nullptr, 0, reinterpret_cast<LPTHREAD_START_ROUTINE>(MainThread), mod, 0, nullptr);
  } else if (reason == DLL_PROCESS_DETACH) {
    MH_DisableHook(MH_ALL_HOOKS);
    MH_Uninitialize();
    console.reset();
  }
  return TRUE;
}
