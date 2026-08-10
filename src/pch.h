#pragma once

#include <magic_enum/magic_enum_all.hpp>

#include <phnt_windows.h>
#include <phnt.h>

#include <string>
#include <string_view>
#include <chrono>
#include <functional>
#include <algorithm>
#include <concepts>
#include <type_traits>

#include <absl/log/log.h>
#include <absl/log/check.h>
#include <format>
#include <filesystem>
#include "wil/result_macros.h"

using namespace std::literals;
using namespace std::placeholders;

typedef unsigned char byte;
typedef unsigned int uint;
typedef unsigned char uint8;
typedef signed char int8;
typedef short int16;
typedef unsigned short uint16;
typedef int int32;
typedef unsigned int uint32;
typedef long long int64;
typedef unsigned long long uint64;

typedef int64 intp;
typedef uint64 uintp;

// What The fuck windows?
#undef ERROR
#undef SetFlag

#define LOGF(Level_, Fmt_, ...) LOG(Level_) << std::format(Fmt_, __VA_ARGS__)
#define VLOGF(Level_, Fmt_, ...) VLOG(Level_) << std::format(Fmt_, __VA_ARGS__)
#define PLOGF(Level_, Fmt_, ...) PLOG(Level_) << std::format(Fmt_, __VA_ARGS__)
#define DLOGF(Level_, Fmt_, ...) DLOG(Level_) << std::format(Fmt_, __VA_ARGS__)
#define CHECKF(Expr_, Fmt_, ...) CHECK(Expr_) << std::format(Fmt_, __VA_ARGS__)
#define DCHECKF(Expr_, Fmt_, ...) DCHECK(Expr_) << std::format(Fmt_, __VA_ARGS__)

constexpr size_t AlignUp(size_t offset, size_t align) {
  return (offset + align - 1) & (~align + 1);
}

#define MH_Assert(Expr_)                           \
  if (auto status = (Expr_); status != MH_OK) {    \
    LOGF(ERROR, "{} returned {}", #Expr_, status); \
  }

#define UGetLastError() static_cast<uint32>(GetLastError())

inline std::wstring UTF8ToWideString(const std::string& in) {
  const auto size = MultiByteToWideChar(CP_UTF8, 0, in.c_str(), in.size(), nullptr, 0);
  std::wstring out(size, 0);
  auto retn = MultiByteToWideChar(CP_UTF8, 0, in.c_str(), in.size(), out.data(), out.size());
  THROW_LAST_ERROR_IF(retn == 0);
  out.resize(retn);
  return out;
}
inline std::string WideStringToUTF8(const std::wstring& in) {
  const auto size =
      WideCharToMultiByte(CP_UTF8, 0, in.c_str(), in.size(), nullptr, 0, nullptr, nullptr);
  std::string out(size, 0);
  auto retn = WideCharToMultiByte(CP_UTF8, 0, in.c_str(), in.size(), out.data(), out.size(),
                                  nullptr, nullptr);
  THROW_LAST_ERROR_IF(retn == 0);
  out.resize(retn);
  return out;
}

namespace fs = std::filesystem;
