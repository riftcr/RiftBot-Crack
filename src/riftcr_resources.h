#pragma once
#include "hook_curl.h"

#define RIFT_CR_PROTOCOL "http://riftcr/"
#define CONCAT(A, B) A##B
#define SV(A) CONCAT(A, sv)
constexpr auto kRiftCrProtocol = SV(RIFT_CR_PROTOCOL);
constexpr auto kRiftCrResourcesDomain = L"media.githubusercontent.com"sv;
constexpr auto kRiftCrResourcesPath =
    L"/media/riftcr/resources/cae0f6173014d1116a7eb95cb6c23b7dcc6bc179"sv;
#undef CONCAT
#undef SV

void RiftCr_PrePerform(InterceptorData& c);
