#include "syscalls.h"

#include <algorithm>
#include <array>
#include <span>

std::vector<SyscallEntry> g_Syscalls;

constexpr auto kSyscallRegionSize = 0x1000U;

struct Region {
  std::span<uint8_t, kSyscallRegionSize> span{static_cast<uint8_t*>(nullptr), kSyscallRegionSize};
  size_t limit{0};
} syscall_region;

// clang-format off
constexpr auto kSyscallShellcodeTpl = std::to_array<uint8_t>({
    0x4c, 0x8b, 0xd1,              // mov r10, rcx
    0xb8, 0x6d, 0x65, 0x6f, 0x77,  // mov eax, imm32
    0x0f, 0x05,                    // syscall
    0xc3,                          // ret
});
// clang-format on
constexpr auto kSyscallShellcodeTplImmOffset = 4U;
void CollectSyscalls();  // Forward declaration

void InitSyscalls() {
  const auto base =
      VirtualAlloc(nullptr, kSyscallRegionSize, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
  if (base == nullptr) {
    LOGF(ERROR, "VirtualAlloc failed: {:08x}", (uint32)GetLastError());
  }
  syscall_region.span =
      std::span<uint8_t, kSyscallRegionSize>(static_cast<uint8_t*>(base), kSyscallRegionSize);

  VLOGF(1, "Created syscall region of {} bytes at {}", kSyscallRegionSize, base);

  CollectSyscalls();
}

void* GetSyscall(SyscallEntry& entry) {
  CHECK(entry.ssn != -1);
  // already setup!
  if (entry.offset_into_region != -1) {
    goto return_syscall_addr;
  }
  entry.offset_into_region = syscall_region.limit;

  CHECK(!(syscall_region.limit + kSyscallShellcodeTpl.size() > syscall_region.span.size()));

  {  // scoped because of the goto/label
    const auto region_addr = syscall_region.span.data() + entry.offset_into_region;
    std::ranges::copy(kSyscallShellcodeTpl, region_addr);
    *reinterpret_cast<uint32_t*>(region_addr + kSyscallShellcodeTplImmOffset) = entry.ssn;
    VLOGF(1, "Created shellcode for syscall {} ({:#x})", entry.name, entry.ssn);
  }

  syscall_region.limit += kSyscallShellcodeTpl.size();
return_syscall_addr:
  return syscall_region.span.data() + entry.offset_into_region;
}

void* GetSyscall(std::string_view str) {
  for (SyscallEntry& ent : g_Syscalls) {
    // Get rid of the function name prefix
    if (str.starts_with("Zw") || str.starts_with("Nt")) {
      str = str.substr(2);
    }

    if (ent.name.ends_with(str)) {
      return GetSyscall(ent);
    }
  }
  LOGF(FATAL, "Couldn't find syscall for {}", str);
  return nullptr;
}

bool IsSyscall(const uintptr_t func_addr, const uintptr_t next_func_addr) {
  const auto func_length = next_func_addr - func_addr;
  const std::span data(reinterpret_cast<const uint8_t*>(func_addr), func_length);
  for (auto i = 0; i < data.size(); ++i) {
    if (data[i] == 0xcc) return false;  // Common windows func padding (int3/bp)
    if (data[i] == 0xc3) return false;  // ret

    if (data[i] == 0x0f && data[i + 1] == 0x05  // 0f05 -> syscall
        && data[i + 2] == 0xc3                  //   c3 -> ret
    ) {
      return true;
    }
  }
  return false;
}

// This won't work if NTDLL is missing syscall instructions (see IsSyscall)
void CollectSyscalls() {
  // There are more syscalls in 'win32u', 'gdi32full', and 'kernelbase', but we are really only
  // doing this for NtProtectVirtualMemory which is located in NTDLL.
  const auto ntdll = reinterpret_cast<uintptr_t>(GetModuleHandleA("ntdll.dll"));
  const auto dos_hdr = reinterpret_cast<IMAGE_DOS_HEADER*>(ntdll);
  const auto nt_hdr = reinterpret_cast<IMAGE_NT_HEADERS64*>(ntdll + dos_hdr->e_lfanew);
  const auto& export_data_dir = nt_hdr->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
  CHECK(export_data_dir.Size && export_data_dir.VirtualAddress);

  const auto export_dir =
      reinterpret_cast<IMAGE_EXPORT_DIRECTORY*>(ntdll + export_data_dir.VirtualAddress);
  const auto name_rvas_array = reinterpret_cast<uint32_t*>(ntdll + export_dir->AddressOfNames);
  const auto func_rvas_array = reinterpret_cast<uint32_t*>(ntdll + export_dir->AddressOfFunctions);

  // So we skip ordinals (They are at the beginning of the func_rvas_array)
  const auto funcs_delta = export_dir->NumberOfFunctions - export_dir->NumberOfNames;

  // Save all the exports to this so we can check if they are syscalls
  // (we need the next function address to determine the func length, and it needs to be sorted.)
  std::vector<SyscallEntry> temp;
  for (auto i = 0; i < export_dir->NumberOfNames; ++i) {
    const std::string_view name{reinterpret_cast<char*>(ntdll + name_rvas_array[i])};
    const auto func_rva = func_rvas_array[i + funcs_delta];
    temp.emplace_back(SyscallEntry{name, ntdll + func_rva, 0});
  }

  // Now we sort by the func address cuz the ssns are linear
  std::ranges::sort(temp,
                    [](const SyscallEntry& a, const SyscallEntry& b) { return a.func < b.func; });

  // Now we can check for syscall instruction, populate the SSN, and push it to g_Syscalls
  auto syscall_index = 0;
  for (auto it = temp.begin(); it != temp.end(); ++it) {
    auto next = it + 1;
    uintptr_t next_func_addr = it->func + 0x20;  // 0x20 is how big the syscall functions are and is
                                                 // a reasonable length for the last entry (maybe?)
    if (next != temp.end()) {
      next_func_addr = next->func;
    }

    if (IsSyscall(it->func, next_func_addr)) {
      it->ssn = syscall_index++;
      g_Syscalls.emplace_back(*it);
    }
  }
  // Log the service numbers
  // for (auto& ent : g_Syscalls) {
  //   LOGF(INFO, "ssn for {} (addr={}) = {:#x}", ent.name, reinterpret_cast<void*>(ent.func),
  //        ent.ssn);
  // }
}
