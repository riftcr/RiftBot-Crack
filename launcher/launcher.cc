#include "launcher.h"
#include <absl/status/status.h>
#include <handleapi.h>
#include <libloaderapi.h>
#include <processthreadsapi.h>
#include <wil/registry.h>
#include <wil/result_macros.h>
#include <winnt.h>
#include <winuser.h>
#include <shellapi.h>
#include "globals.h"
#include "ntpsapi.h"
#include "phnt.h"
#include "wil/registry_helpers.h"
#include <thread>

wil::unique_hkey ifeo_key;
wil::unique_hkey riftldr_key;

void NeedAdmin() {
  MessageBoxA(nullptr, "RiftCrack needs to be Run as Administrator to install.",
              "[RiftCrack] Missing permissions", MB_OK | MB_ICONEXCLAMATION);
  std::exit(0);
}

void InstallLauncher() {
  auto basename = g_RiftLoaderPath.filename().wstring();

  riftldr_key = wil::reg::create_unique_key(ifeo_key.get(), basename.c_str(),
                                            wil::reg::key_access::readwrite);
  auto filter_key =
      wil::reg::create_unique_key(riftldr_key.get(), L"0", wil::reg::key_access::readwrite);
  wil::reg::set_value_string(filter_key.get(), L"FilterFullPath", g_RiftLoaderPath.c_str());

  std::wstring debugger_value = L"\""s + g_SelfPath.wstring() + L"\" /launch";

  wil::reg::set_value_string(filter_key.get(), L"Debugger", debugger_value.c_str());

  wil::reg::set_value_dword(riftldr_key.get(), L"UseFilter", 1);

  auto msg = std::format(
      "RiftCrack is setup! Open {} to get started using Rift!\n\nWould you like to open it now?",
      WideStringToUTF8(basename));
  if (auto result =
          MessageBoxA(nullptr, msg.c_str(), "[RiftCrack] Crack setup!", MB_YESNO | MB_ICONQUESTION);
      result == IDNO) {
    std::exit(0);
  }

  Launch("");
}

void UninstallLauncher() {
  if (auto result = MessageBoxA(nullptr, "RiftCrack is already setup. Would you like to uninstall?",
                                "[RiftCrack] Uninstall?", MB_YESNO | MB_ICONQUESTION);
      result == IDNO) {
    LOGF(INFO, "user cancelled uninstall");
    return;
  }
  wil::reg::delete_tree(ifeo_key.get(), g_RiftLoaderPath.filename().c_str());
  MessageBoxA(nullptr, "RiftCrack has been uninstalled successfully.", "[RiftCrack] Done",
              MB_OK | MB_ICONQUESTION);
}

void CheckLauncherStatus() {
  if (auto hr = wil::reg::open_unique_key_nothrow(
          HKEY_LOCAL_MACHINE,
          L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Image File Execution Options",
          ifeo_key, wil::reg::key_access::readwrite);
      FAILED(hr)) {
    if (hr == E_ACCESSDENIED) {
      NeedAdmin();
    } else {
      THROW_HR(hr);
    }
  }

  auto basename = g_RiftLoaderPath.filename().wstring();
  VLOGF(1, "Loader basename: {}", WideStringToUTF8(basename));

  if (auto hr = wil::reg::open_unique_key_nothrow(ifeo_key.get(), basename.c_str(), riftldr_key,
                                                  wil::reg::key_access::readwrite);
      FAILED(hr)) {
    if (hr == 0x80070002) {  // Not found
      InstallLauncher();
      return;
    } else {
      THROW_HR(hr);
    }
  }

  UninstallLauncher();
}

struct ExternalAlloc {
  ExternalAlloc(HANDLE proc, size_t sz, DWORD protection = PAGE_READWRITE,
                void* desired_address = nullptr)
      : process{proc}, size{sz} {
    address = VirtualAllocEx(process, desired_address, size, MEM_COMMIT | MEM_RESERVE, protection);
    THROW_LAST_ERROR_IF_NULL(address);
    VLOGF(1, "Allocated {} bytes in external process @ {}", size, address);
  }
  ~ExternalAlloc() {
    if (address) {
      VirtualFreeEx(process, address, size, MEM_RELEASE);
      address = nullptr;
    }
  }

  void Copy(const void* data, size_t len, size_t offset = 0) {
    DCHECK(offset + len <= size);
    size_t wrote{0};
    THROW_LAST_ERROR_IF(
        !WriteProcessMemory(process, (void*)((uintp)address + offset), data, len, &wrote));
    THROW_LAST_ERROR_IF(wrote != len);
    VLOGF(1, "Copied {} bytes to external process", wrote);
  }
  template <typename T>
  void Copy(const T& data, size_t offset = 0) {
    Copy(data.data(), data.size() * sizeof(typename T::value_type), offset);
  }

  HANDLE process{nullptr};
  void* address{nullptr};
  size_t size{0};
};

constexpr std::array<byte, 2> kInfiniteLoopCode = {
    0xeb, 0xfe  // jmp $
};

auto CreateProcParameters(UNICODE_STRING& nt_image_path_name) {
  PRTL_USER_PROCESS_PARAMETERS ptr;
  THROW_IF_NTSTATUS_FAILED(RtlCreateProcessParametersEx(&ptr, &nt_image_path_name, nullptr, nullptr,
                                                        nullptr, nullptr, nullptr, nullptr, nullptr,
                                                        nullptr, RTL_USER_PROC_PARAMS_NORMALIZED));
  return std::unique_ptr<RTL_USER_PROCESS_PARAMETERS, void (*)(RTL_USER_PROCESS_PARAMETERS*)>(
      ptr, +[](RTL_USER_PROCESS_PARAMETERS* a) -> void { RtlDestroyProcessParameters(a); });
}

void WrappedWriteProcessMemory(HANDLE process, void* remote_address, const void* bytes, size_t len,
                               DWORD new_protect = PAGE_READWRITE) {
  DWORD old_protect;
  THROW_LAST_ERROR_IF(!VirtualProtectEx(process, remote_address, len, new_protect, &old_protect));

  SIZE_T wrote;
  THROW_LAST_ERROR_IF(!WriteProcessMemory(process, remote_address, bytes, len, &wrote));
  THROW_LAST_ERROR_IF(wrote != len);

  DWORD tmp;
  THROW_LAST_ERROR_IF(!VirtualProtectEx(process, remote_address, len, old_protect, &tmp));
}

void Launch(std::string_view unused_path) {
  fs::path rc_dll_path = fs::current_path() / "rc.dll";
  if (!fs::exists(rc_dll_path)) {
    MessageBoxA(nullptr,
                "\"rc.dll\" is missing. Please redownload the "
                "crack\n\nhttps://github.com/riftcr/RiftBot-Crack | https://t.me/riftcrack",
                "[RiftCrack] Error", MB_OK | MB_ICONHAND);
    if (auto thing = (INT_PTR)ShellExecuteA(nullptr, "open", "https://t.me/riftcrack", nullptr,
                                            nullptr, SW_SHOWDEFAULT);
        32 <= thing) {
      THROW_LAST_ERROR();
    }
    std::exit(0);
  }
  VLOGF(1, "rc.dll path: {}", rc_dll_path.string());
  auto rc_dll_wpath = rc_dll_path.wstring();

  VLOGF(1, "RiftLoader path: {}", g_RiftLoaderPath.string());

  LOGF(INFO, "Launching Rift . . .");

  wil::unique_process_handle process;
  wil::unique_handle main_thread;

  UNICODE_STRING nt_path_name;
  if (!RtlDosPathNameToNtPathName_U(g_RiftLoaderPath.c_str(), &nt_path_name, 0, 0)) {
    THROW_WIN32(ERROR_PATH_NOT_FOUND);
  }

  VLOG(1) << "RiftLoader NtImagePath: "
          << std::wstring_view{nt_path_name.Buffer, static_cast<size_t>(nt_path_name.Length / 2)};

  auto proc_params = CreateProcParameters(nt_path_name);

  PS_CREATE_INFO psi{0};
  std::memset(&psi, 0, sizeof(psi));
  psi.Size = sizeof(psi);
  psi.State = PsCreateInitialState;
  psi.InitState.WriteOutputOnExit = TRUE;
  psi.InitState.IFEOSkipDebugger = TRUE;
  psi.InitState.IFEODoNotPropagateKeyState = TRUE;

  auto list_length = 1 * sizeof(PS_ATTRIBUTE) + sizeof(SIZE_T);
  auto list_ptr = std::make_unique_for_overwrite<char[]>(list_length);
  auto list = reinterpret_cast<PPS_ATTRIBUTE_LIST>(list_ptr.get());
  list->TotalLength = list_length;
  list->Attributes[0].Attribute = PS_ATTRIBUTE_IMAGE_NAME;
  list->Attributes[0].ReturnLength = 0;
  list->Attributes[0].Size = nt_path_name.Length;
  list->Attributes[0].ValuePtr = nt_path_name.Buffer;
  // maybe later PS_ATTRIBUTE_PARENT_PROCESS

  // unfortunately this the only way to set IFEOSkipDebugger (besides supplying DEBUG_PROCESS in
  // CreateProcess flags) and the win32 debugger api seems to crash rift after a few seconds of
  // being open? probably user error but this works!!
  THROW_IF_NTSTATUS_FAILED(
      NtCreateUserProcess(&process, &main_thread, PROCESS_ALL_ACCESS, THREAD_ALL_ACCESS, nullptr,
                          nullptr, PROCESS_CREATE_FLAGS_CREATE_SUSPENDED,
                          THREAD_CREATE_FLAGS_CREATE_SUSPENDED, proc_params.get(), &psi, list));
  if (psi.State != PsCreateSuccess) {
    LOGF(ERROR, "NtCreateUserProcess failed state={}", std::to_underlying(psi.State));
    throw std::runtime_error("NtCreateUserProcess failed: state != PsCreateSuccess");
  }
  VLOGF(1, "spawned RiftLoader process  pid={:x}  tid={:x}", GetProcessId(process.get()),
        GetThreadId(main_thread.get()));

  // rift has kernel debuger detection
  // just tell the user to attach debuger to the process and fix it yourself
  if (USER_SHARED_DATA->KdDebuggerEnabled & 1 || USER_SHARED_DATA->KdDebuggerEnabled & 2) {
    LOGF(INFO, "attach your debugger and hit enter");
    system("pause");
  }

  {
    ExternalAlloc loop_alloc(process.get(), kInfiniteLoopCode.size(), PAGE_EXECUTE_READWRITE);
    loop_alloc.Copy(kInfiniteLoopCode);
    wil::unique_handle loop_thread(CreateRemoteThread(
        process.get(), nullptr, 0, reinterpret_cast<LPTHREAD_START_ROUTINE>(loop_alloc.address),
        nullptr, 0, nullptr));
    THROW_LAST_ERROR_IF_NULL(loop_thread);
    VLOGF(1, "spawned loop thread in RiftLoader");

    {
      ExternalAlloc dll_path_alloc(process.get(), (rc_dll_wpath.size() + 1) * 2, PAGE_READWRITE);
      dll_path_alloc.Copy(rc_dll_wpath);

      // kernel32.dll _should_ be in the same place in all programs
      const auto k32_LoadLibraryW =
          (void*)GetProcAddress(GetModuleHandleA("kernel32.dll"), "LoadLibraryW");
      VLOGF(1, "kernel32!LoadLibraryW @ {}", k32_LoadLibraryW);
      wil::unique_handle inject_thread(CreateRemoteThread(
          process.get(), nullptr, 0, reinterpret_cast<LPTHREAD_START_ROUTINE>(k32_LoadLibraryW),
          dll_path_alloc.address, 0, nullptr));
      VLOGF(1, "spawned LoadLibraryW thread");
      if (!wil::handle_wait(inject_thread.get(), 60'000)) {
        NtTerminateProcess(process.get(), 0);
        throw std::runtime_error("Inject thread timed out (60 seconds)");
      }
    }

    std::this_thread::sleep_for(1000ms);
    NtTerminateThread(loop_thread.get(), 0);
  }

  LOGF(INFO, "Launcher done!");
}

void Launch2(std::string_view path) {
  fs::path rc_dll_path = fs::current_path() / "rc.dll";
  if (!fs::exists(rc_dll_path)) {
    MessageBoxA(nullptr, "\"rc.dll\" is missing. Please redownload the crack", "[RiftCrack] Error",
                MB_OK | MB_ICONHAND);
    std::exit(0);
  }
  VLOGF(1, "rc.dll path: {}", rc_dll_path.string());
  auto rc_dll_wpath = rc_dll_path.wstring();

  VLOGF(1, "RiftLoader path: {}", g_RiftLoaderPath.string());
  STARTUPINFOEXW si{0};
  si.StartupInfo.cb = sizeof(si);
  // wil::unique_process_information pi;
  PROCESS_INFORMATION pi;
  HANDLE debug_object;
  LOGF(INFO, "v2");
  THROW_LAST_ERROR_IF(!CreateProcessW(
      g_RiftLoaderPath.c_str(), nullptr, nullptr, nullptr, FALSE,
      CREATE_SUSPENDED | DEBUG_PROCESS | DEBUG_ONLY_THIS_PROCESS | EXTENDED_STARTUPINFO_PRESENT,
      nullptr, nullptr, (LPSTARTUPINFOW)&si, &pi));
  VLOGF(1, "spawned RiftLoader process");

  wil::unique_process_handle process(OpenProcess(
      PROCESS_VM_READ | PROCESS_VM_WRITE | PROCESS_VM_OPERATION | PROCESS_QUERY_INFORMATION, FALSE,
      pi.dwProcessId));
  THROW_LAST_ERROR_IF_NULL(process);
  DebugActiveProcessStop(pi.dwProcessId);

  // THROW_IF_NTSTATUS_FAILED(NtQueryInformationProcess(process.get(), ProcessDebugObjectHandle,
  //                                                    &debug_object, sizeof(debug_object),
  //                                                    nullptr));

  // NtRemoveProcessDebug(pi.hProcess, debug_object);
  // CloseHandle(debug_object);
  VLOGF(1, "closed debug handle");

  system("pause");

  ExternalAlloc loop_alloc(process.get(), kInfiniteLoopCode.size(), PAGE_EXECUTE_READWRITE);
  loop_alloc.Copy(kInfiniteLoopCode);
  wil::unique_handle loop_thread(CreateRemoteThread(
      process.get(), nullptr, 0, reinterpret_cast<LPTHREAD_START_ROUTINE>(loop_alloc.address),
      nullptr, 0, nullptr));
  THROW_LAST_ERROR_IF_NULL(loop_thread);
  VLOGF(1, "spawned loop thread in RiftLoader");

  {
    ExternalAlloc dll_path_alloc(process.get(), (rc_dll_wpath.size() + 1) * 4, PAGE_READWRITE);
    dll_path_alloc.Copy(rc_dll_wpath);

    // kernel32.dll _should_ be in the same place in all programs
    const auto k32_LoadLibraryW =
        (void*)GetProcAddress(GetModuleHandleA("kernel32.dll"), "LoadLibraryW");
    VLOGF(1, "kernel32!LoadLibraryW @ {}", k32_LoadLibraryW);
    wil::unique_handle inject_thread(CreateRemoteThread(
        process.get(), nullptr, 0, reinterpret_cast<LPTHREAD_START_ROUTINE>(k32_LoadLibraryW),
        dll_path_alloc.address, 0, nullptr));
    THROW_LAST_ERROR_IF_NULL(inject_thread);
    if (!wil::handle_wait(inject_thread.get(), 60'000)) {
      NtTerminateProcess(process.get(), 0);
      throw std::runtime_error("Inject thread timed out (10 seconds)");
    }
  }
  NtTerminateThread(loop_thread.get(), 0);
  LOGF(INFO, "Launcher done!");

  // The crack DLL will resume the main thread
  std::this_thread::sleep_for(1000ms);
}
