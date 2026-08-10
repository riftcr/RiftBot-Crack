#include "hook_initial.h"
#include <MinHook.h>
#include "syscalls.h"
#include <absl/base/call_once.h>
#include <absl/log/internal/log_message.h>
#include <sysinfoapi.h>
#include <wil/token_helpers.h>

// so we can place inline hooks in the RiftBot.dll module
// VMP hooks NtProtectVirtualMemory to stop you from setting page protection in the text section
// But MinHook calls VirtualProtect to write inline hooks - so we use syscall and dont call NtPVM
// Of course we could just fork MinHook and add the syscall stuff to it but this is much more
// straightforward
decltype(VirtualProtect)* o_virtual_protect{nullptr};
BOOL WINAPI VirtualProtectHook(_In_ LPVOID lpAddress, _In_ SIZE_T dwSize, _In_ DWORD flNewProtect,
                               _Out_ PDWORD lpflOldProtect) {
  static auto sc_ProtectVirtualMemory = GET_SYSCALL(NtProtectVirtualMemory);

  void* base_address = lpAddress;
  size_t region_size = dwSize;
  if (const auto status = sc_ProtectVirtualMemory(NtCurrentProcess(), &base_address, &region_size,
                                                  flNewProtect, lpflOldProtect);
      NT_SUCCESS(status)) {
    return TRUE;
  } else {
    LOGF(ERROR, "ProtectVirtualMemory failed!!!: {:08x}", static_cast<uint32_t>(status));
    return FALSE;
  }
}

void AfterUnpacked();

// Naive method of detecting when Rift is unpacked.
// Not immediate but good enough; no web requests are sent before this is called.
decltype(CreateWindowExW)* o_create_window_exw{nullptr};
absl::once_flag init_unpacked;
HWND CreateWindowExWHook(DWORD dwExStyle, LPCWSTR lpClassName, LPCWSTR lpWindowName, DWORD dwStyle,
                         int X, int Y, int nWidth, int nHeight, HWND hWndParent, HMENU hMenu,
                         HINSTANCE hInstance, LPVOID lpParam) {
  absl::call_once(init_unpacked, AfterUnpacked);
  return o_create_window_exw(dwExStyle, lpClassName, lpWindowName, dwStyle, X, Y, nWidth, nHeight,
                             hWndParent, hMenu, hInstance, lpParam);
}

extern uintp g_RiftLoaderBase;
extern uintp g_RiftLoaderSizeOfImage;

// heartbeat response contains a "feature_token" (with a ed25519 signature)
//  (2 u32 in BE, [0]=unix ts (sec), [1]=expire ttl (sec))
//  idk how the sig is validated so we just make this hook return the time in the feature token
decltype(GetSystemTimePreciseAsFileTime)* o_getsystemtimepreciseasfiletime{nullptr};
VOID GetSystemTimePreciseAsFileTimeHook(LPFILETIME lpSystemTimeAsFileTime) {
  o_getsystemtimepreciseasfiletime(lpSystemTimeAsFileTime);
  auto retadr = (uintp)_ReturnAddress();
  if (retadr >= g_RiftLoaderBase && retadr <= (g_RiftLoaderBase + g_RiftLoaderSizeOfImage))
    if (lpSystemTimeAsFileTime) {
      DLOG_EVERY_N_SEC(INFO, 5) << "  !! GetSystemTimePreciseAsFileTime hook triggered";
      // timestamp of the "feature_data_token" in /auth/heartbeat
      *reinterpret_cast<uint64*>(lpSystemTimeAsFileTime) = 134305647120000000ULL;
    }
}

void InstallInitialHooks(bool already_unpacked) {
  auto k32 = LoadLibraryA("kernel32.dll");
  auto u32 = LoadLibraryA("user32.dll");

  const auto k32_VirtualProtect = reinterpret_cast<void*>(GetProcAddress(k32, "VirtualProtect"));
  const auto k32_GetSystemTimePreciseAsFileTime =
      reinterpret_cast<void*>(GetProcAddress(k32, "GetSystemTimePreciseAsFileTime"));

  MH_Assert(MH_CreateHook(k32_VirtualProtect, reinterpret_cast<void*>(VirtualProtectHook),
                          reinterpret_cast<void**>(&o_virtual_protect)));
  MH_Assert(MH_QueueEnableHook(k32_VirtualProtect));

  MH_Assert(MH_CreateHook(k32_GetSystemTimePreciseAsFileTime,
                          reinterpret_cast<void*>(GetSystemTimePreciseAsFileTimeHook),
                          reinterpret_cast<void**>(&o_getsystemtimepreciseasfiletime)));
  MH_Assert(MH_QueueEnableHook(k32_GetSystemTimePreciseAsFileTime));

  if (!already_unpacked) {
    const auto u32_CreateWindowExW =
        reinterpret_cast<void*>(GetProcAddress(u32, "CreateWindowExW"));
    MH_Assert(MH_CreateHook(u32_CreateWindowExW, (void*)CreateWindowExWHook,
                            (void**)&o_create_window_exw));
    MH_Assert(MH_QueueEnableHook(u32_CreateWindowExW));
  }

  MH_Assert(MH_ApplyQueued());
  LOGF(INFO, "Installed initial hooks");
}
