#include "riftcr_resources.h"

void RiftCr_PrePerform(InterceptorData& c) {
  auto& url = c.url();
  auto url_without_proto = url.substr(kRiftCrProtocol.size());
  auto filepath = R"(file://C:/release/riftcr/)"s + url_without_proto;
  LOGF(INFO, "handling RiftCr resource request: {} -> {}", url_without_proto, filepath);
  url = filepath;
}
