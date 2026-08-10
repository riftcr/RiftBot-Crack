#include "hook_ossl.h"
#include <MinHook.h>
#include <absl/strings/escaping.h>
#include "patternfinder.h"

int (*o_ed25519_verify)(void*, const unsigned char* sig, size_t siglen, const unsigned char* tbs,
                        size_t tbslen){nullptr};

// hooked signature(ED25519)->digest_verify because i believe there is some sort of integrity
// check on EVP_DigestVerify
int ed25519_verify_Hook(void*, const unsigned char* sig, size_t siglen, const unsigned char* tbs,
                        size_t tbslen) {
  VLOGF(1, "passing ed25519 signature {} as valid",
        absl::Base64Escape(std::string_view{(char*)sig, siglen}));
  // Success!!
  return 1;
}

#define SCAN(Out_, Pattern_)                            \
  Out_ = (decltype(Out_))PatternScan(module, Pattern_); \
  CHECK(Out_);                                          \
  VLOGF(1, "Located {} @ {}", #Out_, (void*)Out_);
void ScanForOpenSSLFunctions() {
  void* module = GetModuleHandleA(nullptr);

  // clang-format off
  // for now it matches ed25519_verify and ed448_verify , but ed25519 comes first in the mathc list
  SCAN(o_ed25519_verify, "40 53 55 56 57 41 56 B8 ? ? ? ? E8 ? ? ? ? 48 2B E0 48 8B 05 ? ? ? ? 48 33 C4 48 89 84 24 ? ? ? ? 48 8B 69 ? 49 8B F1 49 8B D8 4C 8B F2 48 8B F9");
  // clang-format on
}
#undef SCAN

void InstallOpenSSLHooks() {
  ScanForOpenSSLFunctions();

  auto ed25519_verify = (void*)o_ed25519_verify;

  MH_Assert(MH_CreateHook(ed25519_verify, (void*)&ed25519_verify_Hook, (void**)&o_ed25519_verify));
  MH_Assert(MH_QueueEnableHook(ed25519_verify));

  MH_Assert(MH_ApplyQueued());
  LOGF(INFO, "Installed OpenSSL hooks");
}
