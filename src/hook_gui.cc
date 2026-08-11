#include "hook_gui.h"
#include <MinHook.h>
#include "patternfinder.h"

extern uintp g_RiftLoaderBase;

// the function declarations are incorrect but they work until it tries rendering the bot search
// so just disable them after you pass the verification screen
void* button_tgt{nullptr};
void* inputtext_tgt{nullptr};
bool (*o_button)(char* a, size_t b){nullptr};
bool (*o_inputtext)(char* a, char* b, char* buf, size_t len){nullptr};

// sub_14028df80
bool Button_Hook(char* a, size_t b) {
  if (a && std::string_view{a} == "Verify") {
    VLOG_EVERY_N_SEC(1, 5) << "Verify button called";
    static bool ran = false;
    if (!ran) {
      MH_Assert(MH_QueueDisableHook(button_tgt));
      MH_Assert(MH_QueueDisableHook(inputtext_tgt));
      MH_Assert(MH_ApplyQueued());
      MH_Assert(MH_RemoveHook(button_tgt));
      MH_Assert(MH_RemoveHook(inputtext_tgt));
      ran = true;
      return ran;
    }
  }
  return o_button(a, b);
}

// sub_14024b670
bool InputText_Hook(char* a, char* b, char* buf, size_t len) {
  if (a && std::string_view{a} == "License Key" && buf && len == 0x100) {
    constexpr auto kFakeKey = "RIFT-FREEFREEFREEFREE\0"sv;
    std::memcpy(buf, kFakeKey.data(), kFakeKey.size());
    VLOG_EVERY_N_SEC(1, 5) << "InputText replaced";
    return false;
  }

  return o_inputtext(a, b, buf, len);
}

// clangd 22.1.8 cant handle concepts 🖕
// template <typename StringViewType>
// concept AnyStringView =
//     std::is_same_v<StringViewType, std::basic_string_view<typename StringViewType::value_type>>;

template </* AnyStringView */ typename StringViewType, bool NullTerminate = true>
auto* FindString(StringViewType str) {
  constexpr auto kElemSize = sizeof(typename StringViewType::value_type);
  std::string pattern;
  for (const auto c : str) {
    if constexpr (kElemSize == 2) {
      const auto lol = reinterpret_cast<const uint8_t*>(&c);
      pattern.append(std::format("{:02X} {:02X} ", lol[0], lol[1]));
    } else if constexpr (kElemSize == 1) {
      pattern.append(std::format("{:02X} ", (uint8_t)c));
    } else {
      static_assert(false, "The fuck is wrong with u");
    }
  }
  if constexpr (NullTerminate) {
    for (auto i = 0; i < kElemSize; ++i) {
      pattern.append("00 ");
    }
  } else {
    // remove trailing whitespace
    pattern.resize(pattern.size() - 1);
  }
  VLOGF(1, "Finding \"{}\"", pattern);
  return reinterpret_cast<StringViewType::value_type*>(
      PatternScan((void*)g_RiftLoaderBase, pattern.c_str()));
}
template </* AnyStringView */ typename StringViewType>
void ReplaceString(StringViewType original, StringViewType replacement) {
  constexpr auto kElemSize = sizeof(typename StringViewType::value_type);
  const auto orig_address = FindString(original);
  if (!orig_address) {
    LOGF(WARNING, "couldn't locate string to replace");
    return;
  }

  const auto byte_size = original.size() * kElemSize;
  DWORD old_prot;
  THROW_LAST_ERROR_IF(!VirtualProtect(orig_address, byte_size, PAGE_READWRITE, &old_prot));
  std::ranges::copy(replacement, orig_address);
  if (replacement.size() < original.size()) {
    orig_address[replacement.size()] = 0;
  }
  THROW_LAST_ERROR_IF(!VirtualProtect(orig_address, byte_size, old_prot, &old_prot));
}

void PatchStrings() {
  {  // clang-format off
    constexpr auto kOriginal = "RiftSDK - Select Bot"sv;
    constexpr auto kNew      = "Rift  t.me/riftcrack"sv;
    static_assert(kNew.size() <= kOriginal.size());
    ReplaceString(kOriginal, kNew);
  }  // clang-format on

  {  // clang-format off
    constexpr auto kOriginal = "RiftSDK - License Verification"sv;
    constexpr auto kNew      = "RiftSDK -    t.me/riftcrack   "sv;
    static_assert(kNew.size() <= kOriginal.size());
    ReplaceString(kOriginal, kNew);
  }  // clang-format on

  {  // clang-format off
    constexpr auto kOriginal = "Enter your license key to continue."sv;
    constexpr auto kNew      = "t.me/riftcrack                     "sv;
    static_assert(kNew.size() <= kOriginal.size());
    ReplaceString(kOriginal, kNew);
  }  // clang-format on
}

#define SCAN(Out_, Pattern_)                            \
  Out_ = (decltype(Out_))PatternScan(module, Pattern_); \
  CHECK(Out_);                                          \
  VLOGF(1, "Located {} @ {}", #Out_, (void*)Out_);
void ScanForGuiFunctions() {
  void* module = GetModuleHandleA(nullptr);

  // clang-format off
  // both of these sigs match multiple funcs but the imgui ones seem to be first
  SCAN(button_tgt, "48 8B C4 55 53 56 57 41 56 48 8D 68 ? 48 81 EC ? ? ? ? 0F 29 70 ? 48 8B F9"); // Button
  SCAN(inputtext_tgt, "48 8B C4 55 53 56 57 41 54 41 55 41 56 41 57 48 8D A8 ? ? ? ? 48 81 EC ? ? ? ? 0F 29 70 ? 0F 29 78 ?"); // InputText
  // clang-format on
}
#undef SCAN

void InstallGuiHooks() {
  ScanForGuiFunctions();

  auto button = (void*)button_tgt;
  auto inputtext = (void*)inputtext_tgt;

  MH_Assert(MH_CreateHook(button, (void*)&Button_Hook, (void**)&o_button));
  MH_Assert(MH_QueueEnableHook(button));

  MH_Assert(MH_CreateHook(inputtext, (void*)&InputText_Hook, (void**)&o_inputtext));
  MH_Assert(MH_QueueEnableHook(inputtext));

  MH_Assert(MH_ApplyQueued());
  LOGF(INFO, "Installed GUI hooks");

  PatchStrings();
}
