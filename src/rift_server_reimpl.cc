#include "rift_server_reimpl.h"
#include <absl/log/log.h>
#include <simdjson.h>
#include <sodium.h>
#include <sodium/crypto_aead_aes256gcm.h>
#include <sodium/crypto_sign.h>
#include <sodium/utils.h>
#include <absl/strings/escaping.h>
#include "rift_server_json.h"
#include "riftcr_resources.h"

constexpr Key kRiftCdnInitKey{
    0xc9, 0xb9, 0x6f, 0x97, 0xec, 0x8c, 0x5d, 0x60, 0x25, 0xb0, 0x07, 0x15, 0x6a, 0xba, 0xc3, 0xd2,
    0x7b, 0xeb, 0x18, 0x39, 0x5b, 0x8f, 0x0b, 0x96, 0x3f, 0x40, 0x60, 0x44, 0xe1, 0x12, 0x44, 0x8d,
};
Key rift_session_key;

template <size_t N>
void Base64Escape(const std::array<byte, N>& arr, std::string* out) {
  return absl::Base64Escape(std::string_view{(char*)arr.data(), N}, out);
}
void Base64Escape(const std::string& arr, std::string* out) { return absl::Base64Escape(arr, out); }

std::string Base64Escape(const auto& arr) {
  std::string v;
  Base64Escape(arr, &v);
  return v;
}

/*
 * Script to generate list
 * x = decrypted /auth/bot-list response
   const list = []
   for (const k in x.bot_list) {
       const v = x.bot_list[k]

       list.push(`{"${k}", BotSpec{
         .policy_layersizes = {${v.POLICY_LAYERSIZES.join(', ')}},
         .shared_head_layersizes = {${v.SHARED_HEAD_LAYERSIZES.join(', ')}},
         .observer_type = ${v.OBS_TYPE},
         .action_type = ${v.ACTION_TYPE},
         .model_2url = "yeah"}}`)
   }
   console.log(list.join(','))
   copy(list.join(','))
 */
const std::map<std::string, BotSpec> kBotList{
    {"TromsoFR-Newest", BotSpec{.policy_layersizes = {1024, 512, 512, 512, 512},
                                .shared_head_layersizes = {512, 512, 512, 512},
                                .observer_type = 0,
                                .action_type = 0,
                                .model_url = RIFT_CR_PROTOCOL "models/TromsoNewest/MODEL.bin"}},
    {"TromsoFR-19.5B (Arsenal Resets)",
     BotSpec{.policy_layersizes = {1024, 512, 512, 512, 512},
             .shared_head_layersizes = {512, 512, 512, 512},
             .observer_type = 0,
             .action_type = 0,
             .model_url = RIFT_CR_PROTOCOL "models/Tromso19.5B/MODEL.bin"}},
    {"TromsoAD-Preflip",
     BotSpec{.policy_layersizes = {1024, 1024, 1024, 1024},
             .shared_head_layersizes = {1024, 1024, 1024, 1024},
             .observer_type = 0,
             .action_type = 0,
             .model_url = RIFT_CR_PROTOCOL "models/TromsoAD-Preflip/MODEL.bin"}},
    {"TromsoGP (Airrol-No Resets)",
     BotSpec{.policy_layersizes = {1024, 512, 512, 512, 512},
             .shared_head_layersizes = {512, 512, 512, 512},
             .observer_type = 0,
             .action_type = 0,
             .model_url = RIFT_CR_PROTOCOL "models/TromsoGP/MODEL.bin"}},
    {"TromsoFR-Experimental",
     BotSpec{.policy_layersizes = {1024, 512, 512, 512, 512},
             .shared_head_layersizes = {512, 512, 512, 512},
             .observer_type = 0,
             .action_type = 0,
             .model_url = RIFT_CR_PROTOCOL "models/Experimental/MODEL.bin"}},
    {"KusoBot-V1", BotSpec{.policy_layersizes = {1024, 1024, 512, 512, 512},
                           .shared_head_layersizes = {1024, 1024, 512, 512},
                           .observer_type = 1,
                           .action_type = 0,
                           .model_url = RIFT_CR_PROTOCOL "models/KusoV1/MODEL.bin"}},
    {"KusoBot-V2 (Lightweight-version)",
     BotSpec{.policy_layersizes = {1024, 1024, 1024, 1024},
             .shared_head_layersizes = {1024, 1024, 1024, 1024},
             .observer_type = 8,
             .action_type = 0,
             .model_url = RIFT_CR_PROTOCOL "models/KusoV2_Lightweight_Version/MODEL.bin"}},
    {"KusoBot-V2 Max (Heavy)",
     BotSpec{.policy_layersizes = {2048, 2048, 2048, 2048},
             .shared_head_layersizes = {2048, 2048, 2048, 2048},
             .observer_type = 4,
             .action_type = 0,
             .model_url = RIFT_CR_PROTOCOL "models/KusoV2_Max_Version/MODEL.bin"}},
    {"SysoBot-1v1 (V1)", BotSpec{.policy_layersizes = {1024, 1024, 512},
                                 .shared_head_layersizes = {1024, 1024},
                                 .observer_type = 3,
                                 .action_type = 0,
                                 .model_url = RIFT_CR_PROTOCOL "models/SysoBotV1/MODEL.bin"}},
    {"SysoBot-1v1 (V2)", BotSpec{.policy_layersizes = {1024, 512, 512},
                                 .shared_head_layersizes = {2048, 2048},
                                 .observer_type = 7,
                                 .action_type = 0,
                                 .model_url = RIFT_CR_PROTOCOL "models/SysoBotV2_1v1/MODEL.bin"}},
    {"SysoBot-2v2 (V1)", BotSpec{.policy_layersizes = {1024, 1024, 1024, 1024, 512},
                                 .shared_head_layersizes = {1024, 1024, 1024, 1024, 512},
                                 .observer_type = 6,
                                 .action_type = 0,
                                 .model_url = RIFT_CR_PROTOCOL "models/SysoBotV2/MODEL.bin"}},
    {"SysoBot-2v2 (V2)", BotSpec{.policy_layersizes = {1024, 1024, 1024, 1024, 512},
                                 .shared_head_layersizes = {1024, 1024, 1024, 1024, 512},
                                 .observer_type = 6,
                                 .action_type = 0,
                                 .model_url = RIFT_CR_PROTOCOL "models/SysoBot-Multi/MODEL.bin"}},
    {"SkyBot-V1 (1v1-Multi-FlipReset)",
     BotSpec{.policy_layersizes = {1024, 1024, 1024, 1024},
             .shared_head_layersizes = {1024, 1024, 1024, 1024},
             .observer_type = 0,
             .action_type = 0,
             .model_url = RIFT_CR_PROTOCOL "models/SkyBot/MODEL.bin"}},
    {"SkyBot-V2 (1v1-Musty)", BotSpec{.policy_layersizes = {1024, 1024, 1024, 1024},
                                      .shared_head_layersizes = {1024, 1024, 1024, 1024},
                                      .observer_type = 0,
                                      .action_type = 0,
                                      .model_url = RIFT_CR_PROTOCOL "models/SkyBotV2/MODEL.bin"}},
    {"Tromso-Bump-320b", BotSpec{.policy_layersizes = {1024, 1024, 1024, 1024},
                                 .shared_head_layersizes = {1024, 1024, 1024, 1024},
                                 .observer_type = 0,
                                 .action_type = 0,
                                 .model_url = RIFT_CR_PROTOCOL "models/TromsoBump/MODEL.bin"}},
    {"KusoBot-V1.5(3v3)", BotSpec{.policy_layersizes = {1024, 1024, 1024, 512},
                                  .shared_head_layersizes = {1024, 1024, 1024, 1024, 2048},
                                  .observer_type = 5,
                                  .action_type = 0,
                                  .model_url = RIFT_CR_PROTOCOL "models/KusoV1.5/MODEL.bin"}},
    {"Nyx", BotSpec{.policy_layersizes = {1024, 1024, 1024, 1024, 512},
                    .shared_head_layersizes = {1024, 1024, 1024, 1024, 512},
                    .observer_type = 12,
                    .action_type = 0,
                    .model_url = RIFT_CR_PROTOCOL "models/Nyx/MODEL.bin"}},
    // Tier 1
    {"KusoBot-V3(2v2)", BotSpec{.policy_layersizes = {2048, 2048, 2048, 2048},
                                .shared_head_layersizes = {2048, 2048, 2048, 2048},
                                .observer_type = 4,
                                .action_type = 0,
                                .model_url = RIFT_CR_PROTOCOL "models/KusoV3_2v2/MODEL.bin"}},
    {"KusoBot-V2.9(2v2)", BotSpec{.policy_layersizes = {2048, 2048, 2048, 2048},
                                  .shared_head_layersizes = {2048, 2048, 2048, 2048},
                                  .observer_type = 4,
                                  .action_type = 0,
                                  .model_url = RIFT_CR_PROTOCOL "models/KusoV2.9_2v2/MODEL.bin"}},
    {"KusoBot-V2.5(2v2)", BotSpec{.policy_layersizes = {1024, 1024, 1024, 512},
                                  .shared_head_layersizes = {1024, 1024, 1024, 1024, 2048},
                                  .observer_type = 5,
                                  .action_type = 0,
                                  .model_url = RIFT_CR_PROTOCOL "models/KusoV2.5_2v2/MODEL.bin"}},
    {"SkyBot-V3(1v1)", BotSpec{.policy_layersizes = {1024, 1024, 1024, 1024},
                               .shared_head_layersizes = {1024, 1024, 1024, 1024},
                               .observer_type = 0,
                               .action_type = 0,
                               .model_url = RIFT_CR_PROTOCOL "models/SkyBotV3/MODEL.bin"}},
};

bool FillResponseBag(ResponseBag& bag, const Key& key, const auto&& obj) {
  simdjson::builder::string_builder sb;
  sb.append(std::move(obj));
  bag.decrypted_payload = sb.view().value();

  bag.RandomizeIv();
  bag.Encrypt(key);
  return true;
}

void RiftAuth_PrePerform(InterceptorData& c) {
  simdjson::padded_string json = c.post_data();
  simdjson::ondemand::parser parser;
  simdjson::ondemand::document doc = parser.iterate(json);
  RequestBag bag;
  std::ignore = doc.get<RequestBag>(bag);

  const auto& url = c.url();
  ResponseBag response_bag;

  if (url.contains("/auth/init")) {
    bag.Decrypt(kRiftCdnInitKey);
    LOGF(INFO, "Handling init request");
    VLOGF(1, "Decrypted /auth/init request: {}", bag.decrypted_payload.data());
    doc = parser.iterate(bag.decrypted_payload);
    AuthInitRequest req;
    std::ignore = doc.get(req);

    AuthInitResponse res;

    randombytes_buf(rift_session_key.data(), rift_session_key.size());
    res.session_key.decrypted_payload.append((char*)rift_session_key.data(),
                                             rift_session_key.size());
    res.session_key.RandomizeIv();
    res.session_key.Encrypt(kRiftCdnInitKey);

    VLOGF(1, "Rift session key: {}",
          absl::Base64Escape(
              std::string_view{(char*)rift_session_key.data(), rift_session_key.size()}));

    FillResponseBag(response_bag, kRiftCdnInitKey, std::move(res));
  } else if (url.contains("/auth/start")) {
    bag.Decrypt(rift_session_key);
    LOGF(INFO, "Handling start request");
    VLOGF(1, "Decrypted /auth/start request: {}", bag.decrypted_payload.data());
    doc = parser.iterate(bag.decrypted_payload);
    AuthStartRequest req;
    std::ignore = doc.get(req);

    AuthStartResponse res;
    res.nonce = req.nonce;
    FillResponseBag(res.version_payload, kRiftCdnInitKey, VersionPayload{});
    res.user_info_license_key = req.license_key;

    FillResponseBag(response_bag, rift_session_key, std::move(res));
  } else if (url.contains("/auth/heartbeat")) {
    bag.Decrypt(rift_session_key);
    LOG_EVERY_N_SEC(INFO, 60) << "Handling heartbeat request";
    VLOGF(1, "Decrypted /auth/heartbeat request: {}", bag.decrypted_payload.data());
    doc = parser.iterate(bag.decrypted_payload);
    AuthHeartbeatRequest req;
    std::ignore = doc.get(req);

    AuthHeartbeatResponse res;
    // byte ftdata[8]{0, 0, 0, 0, 0, 0, 0, 0};
    res.feature_ttl = std::numeric_limits<int32>::max();
    // *reinterpret_cast<uint32*>(ftdata) =
    //     std::byteswap((uint32)absl::ToUnixSeconds(absl::Now()) - 86400);
    // *reinterpret_cast<uint32*>(ftdata + 4) = std::byteswap(res.feature_ttl);
    // res.feature_token_data = absl::Base64Escape(std::string_view{(char*)ftdata, 8});
    res.nonce = req.nonce;
    FillResponseBag(response_bag, rift_session_key, std::move(res));
  } else if (url.contains("/auth/bot-list")) {
    bag.Decrypt(rift_session_key);
    LOGF(INFO, "Handling bot-list request");
    VLOGF(1, "Decrypted /auth/bot-list request: {}", bag.decrypted_payload.data());
    doc = parser.iterate(bag.decrypted_payload);
    AuthBotListRequest req;
    std::ignore = doc.get(req);

    AuthBotListResponse res;
    res.nonce = req.nonce;
    res.bot_list = kBotList;
    FillResponseBag(response_bag, rift_session_key, std::move(res));
  }

  CHECKF(!response_bag.payload.empty(), "WHATTTTT?? response bag is EMPTY?!?!?!?!?!?!");

  {
    simdjson::builder::string_builder sb;
    sb.append(response_bag);
    c.response() = sb.view().value();
    VLOGF(1, "overwrote response for {}", url);
    VLOGF(1, "  {}", c.response());
  }
}
void RiftAuth_PostPerform(InterceptorData& c) {
  simdjson::padded_string json = c.response();
  simdjson::ondemand::parser parser;
  simdjson::ondemand::document doc = parser.iterate(json);
  ResponseBag bag;
  std::ignore = doc.get<ResponseBag>(bag);

  if (c.url().contains("/auth/init")) {
    bag.Decrypt(kRiftCdnInitKey);
    VLOGF(1, "Decrypted /auth/init response: {}", bag.decrypted_payload.data());

    doc = parser.iterate(bag.decrypted_payload);
    ResponseBag session_key;
    doc["session_key"].get<ResponseBag>(session_key);
    session_key.Decrypt(kRiftCdnInitKey);
    CHECK(session_key.decrypted_payload.size() == rift_session_key.size());
    std::memcpy(rift_session_key.data(), session_key.decrypted_payload.data(),
                session_key.decrypted_payload.size());
    VLOGF(1, "Decrypted session key: {}",
          absl::Base64Escape(
              std::string_view{(char*)rift_session_key.data(), rift_session_key.size()}));
  } else if (c.url().contains("/auth/start")) {
    bag.Decrypt(rift_session_key);
    VLOGF(1, "Decrypted /auth/start response: {}", bag.decrypted_payload.data());

    doc = parser.iterate(bag.decrypted_payload);
    ResponseBag version_payload;
    doc["version_payload"].get<ResponseBag>(version_payload);
    version_payload.Decrypt(kRiftCdnInitKey);

    VLOGF(1, "Decrypted version payload: {}", version_payload.decrypted_payload.data());
  } else {
    try {
      bag.Decrypt(rift_session_key);
      VLOGF(1, "Decrypted {} response: {}", c.url(), bag.decrypted_payload.data());
    } catch (std::exception& ex) {
      LOGF(ERROR, "error decrypting {} response", c.url());
    }
  }
}
