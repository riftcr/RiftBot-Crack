#pragma once
#include <string_view>
#include <vector>

struct SyscallEntry {
  std::string_view name;
  uintptr_t func;
  uint32_t ssn = -1;
  size_t offset_into_region = -1;
};

extern std::vector<SyscallEntry> g_Syscalls;

void* GetSyscall(SyscallEntry& entry);
void* GetSyscall(std::string_view str);

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmicrosoft-cast"
template <auto Fn>
  requires std::is_function_v<std::remove_pointer_t<decltype(Fn)>>
auto GetSyscall(SyscallEntry& entry) {
  return static_cast<decltype(Fn)*>(GetSyscall(entry));
}
template <auto Fn>
  requires std::is_function_v<std::remove_pointer_t<decltype(Fn)>>
auto GetSyscall(const std::string_view str) {
  return static_cast<decltype(Fn)>(GetSyscall(str));
}
#pragma clang diagnostic pop

// Creates the syscall memory and collects them
void InitSyscalls();

#define GET_SYSCALL(Arg1) GetSyscall<Arg1>(#Arg1)
