#pragma once
#include <absl/strings/escaping.h>
#include <sodium/crypto_aead_aes256gcm.h>
#include <sodium/crypto_sign.h>
#include <sodium/randombytes.h>
#include <sodium/utils.h>
#include <simdjson.h>
#include <map>

using Iv = std::array<byte, crypto_aead_aes256gcm_NPUBBYTES>;
using Tag = std::array<byte, crypto_aead_aes256gcm_ABYTES>;
using Signature = std::array<byte, 64 /* crypto_sign_BYTES */>;
using Key = std::array<byte, crypto_aead_aes256gcm_KEYBYTES>;
using Nonce = std::string;

constexpr auto kAddonTier = 1;
constexpr auto kIsStaff = false;

struct RequestBag {
  Iv iv;
  Tag mac;
  std::string payload;
  std::optional<std::string> session_id;
  simdjson::padded_string decrypted_payload;
  void Encrypt(const Key& key) {
    payload.resize(decrypted_payload.size());
    if (auto x = crypto_aead_aes256gcm_encrypt_detached(
            (byte*)payload.data(), mac.data(), nullptr, (byte*)decrypted_payload.data(),
            decrypted_payload.size(), nullptr, 0, nullptr, iv.data(), key.data());
        x != 0) {
      LOGF(ERROR, "FAILED TO ENCRYPT DATA!!!!");
      throw std::runtime_error("[RiftCrack] Failed to encrypt request bag");
    }
  }
  void Decrypt(const Key& key) {
    std::string bin(payload.size(), 0);
    if (auto x = crypto_aead_aes256gcm_decrypt_detached(
            (uint8_t*)bin.data(), nullptr, (const uint8_t*)payload.data(), payload.size(),
            (const uint8_t*)mac.data(), nullptr, 0, (const uint8_t*)iv.data(), key.data());
        x != 0) {
      LOGF(ERROR, "FAILED TO DECRYPT DATA!!!!");
      throw std::runtime_error("[RiftCrack] Failed to decrypt request bag");
    }
    decrypted_payload = bin;
  }
};
struct ResponseBag {
  Iv iv;
  std::string payload;
  Tag mac;
  Signature signature;
  simdjson::padded_string decrypted_payload;
  void RandomizeIv() { randombytes_buf(iv.data(), iv.size()); }
  void Encrypt(const Key& key) {
    payload.resize(decrypted_payload.size());
    if (auto x = crypto_aead_aes256gcm_encrypt_detached(
            (byte*)payload.data(), mac.data(), nullptr, (byte*)decrypted_payload.data(),
            decrypted_payload.size(), nullptr, 0, nullptr, iv.data(), key.data());
        x != 0) {
      LOGF(ERROR, "FAILED TO ENCRYPT DATA!!!!");
      throw std::runtime_error("[RiftCrack] Failed to encrypt response bag");
    }
  }
  void Decrypt(const Key& key) {
    std::string bin(payload.size(), 0);
    if (auto x = crypto_aead_aes256gcm_decrypt_detached(
            (uint8_t*)bin.data(), nullptr, (const uint8_t*)payload.data(), payload.size(),
            (const uint8_t*)mac.data(), nullptr, 0, (const uint8_t*)iv.data(), key.data());
        x != 0) {
      LOGF(ERROR, "FAILED TO DECRYPT DATA!!!!");
      throw std::runtime_error("[RiftCrack] Failed to decrypt response bag");
    }
    decrypted_payload = bin;
  }
};

struct VersionPayload {
  std::string latest_version{"1.1.0"};
  std::string download_link{"https://skydash.lol/clips/version.txt"};
};

struct AuthInitRequest {
  std::string session_id;
};

struct AuthInitResponse {
  std::string status{"session_initialized"};
  ResponseBag session_key;
};

struct AuthStartRequest {
  std::string ci_loader_crc32;
  uint ci_loader_pe_timestamp;
  std::string ci_loader_sha256;
  uint ci_loader_size;

  std::string hw_board_serial;
  int hw_version{2};
  std::string hw_ioctl_disk_serial{"NO_SERIAL"};
  std::string hw_machine_guid;
  std::string hw_physical_mac{"NOT_FOUND"};
  std::string hw_product_id;
  std::string hw_secure_hwid_hash;
  std::string hw_smbios_uuid;
  std::string hw_volume_serial;

  std::string ip;
  std::string license_key;
  // markers: []
  Nonce nonce;
  std::string session_id;
};

struct AuthStartResponse {
  Nonce nonce;
  std::string latest_version{"1.1.0"};
  std::string download_link{"https://skydash.lol/clips/version.txt"};
  ResponseBag version_payload;
  std::string token{
      "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9."
      "eyJzdWIiOiI4NjkxMzYwODMzNTc3MzY5NiIsImlhdCI6MTc4NjAwMDAwMCwiZXhwIjoxOTI0OTkxOTk5fQ."
      "h7f9by2nPkpugaa5Oje3qxfFxmzC4Z8cgV7TnLqF6zU"};
  std::string user_info_discord_id{"696969696969696969"};
  std::string user_info_discord_name{"cracked"};
  std::string user_info_license_key;
  // uint32 user_info_subscription_expiry_timestamp{1924991999};
  uint32 user_info_subscription_expiry_timestamp{0};
  int addon_tier{kAddonTier};
  bool is_staff{kIsStaff};
  bool plant_markers{false};
  std::string marker_secret{"0000000000000000000000000000000000000000000000000000000000000000"};
};

struct AuthHeartbeatRequest {
  std::string license_key;
  Nonce nonce;
  std::string token;
};

struct AuthHeartbeatResponse {
  std::string status{"ok"};
  Nonce nonce;
  std::string feature_token_data{"anWWaAAAAlg="};
  std::string feature_token_sig{
      "PZshKAAPvUpP3Nesq7HdxpkpqochaVqvoJYKHzAgaEuEA+NJKbj2fjqkHAcNRvdqk0RzrEaTyCjIKCN9vV7TAA=="};
  int32 feature_ttl{600};
  bool is_staff{kIsStaff};
  int addon_tier{kAddonTier};
};

struct BotSpec {
  std::vector<uint16> policy_layersizes;
  std::vector<uint16> shared_head_layersizes;
  int observer_type;
  int action_type;
  std::string model_url;
};
struct AuthBotListRequest {
  std::string nonce;
  std::string token;
};
struct AuthBotListResponse {
  std::string status{"ok"};
  std::string nonce;
  std::map<std::string, BotSpec> bot_list;
  int addon_tier{kAddonTier};
  bool is_staff{kIsStaff};
};

namespace simdjson {
template <typename simdjson_value>
auto tag_invoke(deserialize_tag, simdjson_value& val, RequestBag& bag) {
  ondemand::object obj;
  auto error = val.get_object().get(obj);
  if (error) return error;

  std::string iv_b64;
  if ((error = obj["iv"].get_string(iv_b64))) {
    return error;
  }
  if (sodium_base642bin(bag.iv.data(), bag.iv.size(), iv_b64.data(), iv_b64.size(), nullptr,
                        nullptr, nullptr, sodium_base64_VARIANT_ORIGINAL) != 0) {
    return simdjson::UNEXPECTED_ERROR;
  }

  std::string mac_b64;
  if ((error = obj["mac"].get_string(mac_b64))) {
    return error;
  }
  if (sodium_base642bin(bag.mac.data(), bag.mac.size(), mac_b64.data(), mac_b64.size(), nullptr,
                        nullptr, nullptr, sodium_base64_VARIANT_ORIGINAL) != 0) {
    return simdjson::UNEXPECTED_ERROR;
  }

  std::string payload_b64;
  if ((error = obj["payload"].get_string(payload_b64))) {
    return error;
  }
  if (!absl::Base64Unescape(payload_b64, &bag.payload)) {
    return simdjson::UNEXPECTED_ERROR;
  }

  if ((error = obj["session_id"].get_string(bag.session_id))) {
    if (error != simdjson::NO_SUCH_FIELD) return error;
  }

  return simdjson::SUCCESS;
}

template <typename simdjson_value>
auto tag_invoke(deserialize_tag, simdjson_value& val, ResponseBag& bag) {
  ondemand::object obj;
  auto error = val.get_object().get(obj);
  if (error) return error;

  std::string iv_b64;
  if ((error = obj["iv"].get_string(iv_b64))) {
    return error;
  }
  if (sodium_base642bin(bag.iv.data(), bag.iv.size(), iv_b64.data(), iv_b64.size(), nullptr,
                        nullptr, nullptr, sodium_base64_VARIANT_ORIGINAL) != 0) {
    return simdjson::UNEXPECTED_ERROR;
  }

  std::string mac_b64;
  if ((error = obj["mac"].get_string(mac_b64))) {
    return error;
  }
  if (sodium_base642bin(bag.mac.data(), bag.mac.size(), mac_b64.data(), mac_b64.size(), nullptr,
                        nullptr, nullptr, sodium_base64_VARIANT_ORIGINAL) != 0) {
    return simdjson::UNEXPECTED_ERROR;
  }

  std::string signature_b64;
  if ((error = obj["signature"].get_string(signature_b64))) {
    return error;
  }
  if (sodium_base642bin(bag.signature.data(), bag.signature.size(), signature_b64.data(),
                        signature_b64.size(), nullptr, nullptr, nullptr,
                        sodium_base64_VARIANT_ORIGINAL) != 0) {
    return simdjson::UNEXPECTED_ERROR;
  }

  std::string payload_b64;
  if ((error = obj["payload"].get_string(payload_b64))) {
    return error;
  }
  if (!absl::Base64Unescape(payload_b64, &bag.payload)) {
    return simdjson::UNEXPECTED_ERROR;
  }
  return simdjson::SUCCESS;
}

using builder_type = simdjson::builder::string_builder;
template <typename builder_type>
void tag_invoke(serialize_tag, builder_type& builder, const RequestBag& bag) {
  builder.start_object();
  builder.template append_key_value<"iv">(
      absl::Base64Escape(std::string_view{(char*)bag.iv.data(), bag.iv.size()}));
  builder.append_comma();
  builder.template append_key_value<"mac">(
      absl::Base64Escape(std::string_view{(char*)bag.mac.data(), bag.mac.size()}));
  builder.append_comma();
  builder.template append_key_value<"payload">(
      absl::Base64Escape(std::string_view{(char*)bag.payload.data(), bag.payload.size()}));
  if (bag.session_id.has_value()) {
    builder.append_comma();
    builder.template append_key_value<"session_id">(bag.session_id.value());
  }
  builder.end_object();
}

template <typename builder_type>
void tag_invoke(serialize_tag, builder_type& builder, const ResponseBag& bag) {
  builder.start_object();
  builder.template append_key_value<"iv">(
      absl::Base64Escape(std::string_view{(char*)bag.iv.data(), bag.iv.size()}));
  builder.append_comma();
  builder.template append_key_value<"payload">(
      absl::Base64Escape(std::string_view{(char*)bag.payload.data(), bag.payload.size()}));
  builder.append_comma();
  builder.template append_key_value<"mac">(
      absl::Base64Escape(std::string_view{(char*)bag.mac.data(), bag.mac.size()}));
  builder.append_comma();
  builder.template append_key_value<"signature">(
      absl::Base64Escape(std::string_view{(char*)bag.signature.data(), bag.signature.size()}));
  builder.end_object();
}

template <typename simdjson_value>
auto tag_invoke(deserialize_tag, simdjson_value& val, VersionPayload& p) {
  ondemand::object obj;
  auto error = val.get_object().get(obj);
  if (error) return error;

  if ((error = obj["latest_version"].get_string(p.latest_version))) {
    return error;
  }
  if ((error = obj["download_link"].get_string(p.download_link))) {
    return error;
  }

  return simdjson::SUCCESS;
}
template <typename builder_type>
void tag_invoke(serialize_tag, builder_type& builder, const VersionPayload& p) {
  builder.start_object();
  builder.template append_key_value<"latest_version">(p.latest_version);
  builder.append_comma();
  builder.template append_key_value<"download_link">(p.download_link);
  builder.end_object();
}

template <typename simdjson_value>
auto tag_invoke(deserialize_tag, simdjson_value& val, AuthInitRequest& p) {
  ondemand::object obj;
  auto error = val.get_object().get(obj);
  if (error) return error;

  if ((error = obj["session_id"].get_string(p.session_id))) {
    return error;
  }

  return simdjson::SUCCESS;
}
template <typename builder_type>
void tag_invoke(serialize_tag, builder_type& builder, const AuthInitRequest& p) {
  builder.start_object();
  builder.template append_key_value<"session_id">(p.session_id);
  builder.end_object();
}
template <typename simdjson_value>
auto tag_invoke(deserialize_tag, simdjson_value& val, AuthInitResponse& p) {
  ondemand::object obj;
  auto error = val.get_object().get(obj);
  if (error) return error;

  if ((error = obj["status"].get_string(p.status))) {
    return error;
  }

  if ((error = obj["session_key"].get(p.session_key))) {
    return error;
  }

  return simdjson::SUCCESS;
}
template <typename builder_type>
void tag_invoke(serialize_tag, builder_type& builder, const AuthInitResponse& p) {
  builder.start_object();
  builder.template append_key_value<"status">(p.status);
  builder.append_comma();
  builder.template append_key_value<"session_key">(std::ref(p.session_key));
  builder.end_object();
}

template <typename simdjson_value>
auto tag_invoke(deserialize_tag, simdjson_value& val, AuthStartRequest& p) {
  ondemand::object obj;
  auto error = val.get_object().get(obj);
  if (error) return error;

  if (auto ci = obj["client_integrity"]->get_object(); !ci.error()) {
    if (auto ldr = ci["loader"]->get_object(); !ldr.error()) {
      if ((error = ldr["crc32"].get_string(p.ci_loader_crc32))) {
        return error;
      }
      p.ci_loader_pe_timestamp = ldr["pe_timestamp"].get_uint32();
      if ((error = ldr["sha256"].get_string(p.ci_loader_sha256))) {
        return error;
      }
      p.ci_loader_size = ldr["size"].get_uint32();
    } else {
      return ldr.error();
    }
  } else {
    return ci.error();
  }

  if (auto hw = obj["hwid_components"]->get_object(); !hw.error()) {
    p.hw_version = hw["hwidVersion"].get_uint32();
    if ((error = hw["board_serial"].get_string(p.hw_board_serial))) {
      return error;
    }
    if ((error = hw["ioctl_disk_serial"].get_string(p.hw_ioctl_disk_serial))) {
      return error;
    }
    if ((error = hw["machine_guid"].get_string(p.hw_machine_guid))) {
      return error;
    }
    if ((error = hw["physical_mac"].get_string(p.hw_physical_mac))) {
      return error;
    }
    if ((error = hw["product_id"].get_string(p.hw_product_id))) {
      return error;
    }
    if ((error = hw["secureHwidHash"].get_string(p.hw_secure_hwid_hash))) {
      return error;
    }
    if ((error = hw["smbios_uuid"].get_string(p.hw_smbios_uuid))) {
      return error;
    }
    if ((error = hw["volume_serial"].get_string(p.hw_volume_serial))) {
      return error;
    }
  } else {
    return hw.error();
  }

  if ((error = obj["ip"].get_string(p.ip))) {
    return error;
  }
  if ((error = obj["licenseKey"].get_string(p.license_key))) {
    return error;
  }
  if ((error = obj["nonce"].get_string(p.nonce))) {
    return error;
  }
  if ((error = obj["session_id"].get_string(p.session_id))) {
    return error;
  }

  return simdjson::SUCCESS;
}
template <typename builder_type>
void tag_invoke(serialize_tag, builder_type& builder, const AuthStartRequest& p) {
  builder.start_object();

  {
    builder.template escape_and_append_with_quotes<"client_integrity">();
    builder.append_colon();
    builder.start_object();
    {
      builder.template escape_and_append_with_quotes<"loader">();
      builder.append_colon();
      builder.start_object();
      builder.template append_key_value<"crc32">(p.ci_loader_crc32);
      builder.append_comma();
      builder.template append_key_value<"pe_timestamp">(p.ci_loader_pe_timestamp);
      builder.append_comma();
      builder.template append_key_value<"sha256">(p.ci_loader_sha256);
      builder.append_comma();
      builder.template append_key_value<"size">(p.ci_loader_size);
      builder.end_object();
    }
    builder.end_object();
  }
  builder.append_comma();

  {
    builder.template escape_and_append_with_quotes<"hwid_components">();
    builder.append_colon();
    builder.start_object();
    builder.template append_key_value<"board_serial">(p.hw_board_serial);
    builder.append_comma();
    builder.template append_key_value<"hwidVersion">(p.hw_version);
    builder.append_comma();
    builder.template append_key_value<"ioctl_disk_serial">(p.hw_ioctl_disk_serial);
    builder.append_comma();
    builder.template append_key_value<"machine_guid">(p.hw_machine_guid);
    builder.append_comma();
    builder.template append_key_value<"physical_mac">(p.hw_physical_mac);
    builder.append_comma();
    builder.template append_key_value<"product_id">(p.hw_product_id);
    builder.append_comma();
    builder.template append_key_value<"secureHwidHash">(p.hw_secure_hwid_hash);
    builder.append_comma();
    builder.template append_key_value<"smbios_uuid">(p.hw_smbios_uuid);
    builder.append_comma();
    builder.template append_key_value<"volume_serial">(p.hw_volume_serial);
    builder.end_object();
  }
  builder.append_comma();

  builder.template append_key_value<"ip">(p.ip);
  builder.append_comma();

  builder.template append_key_value<"licenseKey">(p.license_key);
  builder.append_comma();

  {
    builder.template escape_and_append_with_quotes<"markers">();
    builder.append_colon();
    builder.start_array();
    builder.end_array();
  }
  builder.append_comma();

  builder.template append_key_value<"nonce">(p.nonce);
  builder.append_comma();

  builder.template append_key_value<"session_id">(p.session_id);

  builder.end_object();
}

template <typename builder_type>
void tag_invoke(serialize_tag, builder_type& builder, const AuthStartResponse& p) {
  builder.start_object();

  builder.template append_key_value<"nonce">(p.nonce);
  builder.append_comma();

  builder.template append_key_value<"latest_version">(p.latest_version);
  builder.append_comma();

  builder.template append_key_value<"download_link">(p.download_link);
  builder.append_comma();

  builder.template append_key_value<"version_payload">(std::ref(p.version_payload));
  builder.append_comma();

  builder.template append_key_value<"token">(p.token);
  builder.append_comma();

  {
    builder.template escape_and_append_with_quotes<"user_info">();
    builder.append_colon();
    builder.start_object();

    builder.template append_key_value<"discord_id">(p.user_info_discord_id);
    builder.append_comma();

    builder.template append_key_value<"discord_name">(p.user_info_discord_name);
    builder.append_comma();

    builder.template append_key_value<"licenseKey">(p.user_info_license_key);
    builder.append_comma();

    builder.template append_key_value<"subscription_expiry_timestamp">(
        p.user_info_subscription_expiry_timestamp);
    builder.end_object();
  }
  builder.append_comma();

  builder.template append_key_value<"addon_tier">(p.addon_tier);
  builder.append_comma();

  builder.template append_key_value<"is_staff">(p.is_staff);
  builder.append_comma();

  builder.template append_key_value<"plant_markers">(p.plant_markers);
  builder.append_comma();

  builder.template append_key_value<"marker_secret">(p.marker_secret);

  builder.end_object();
}

template <typename simdjson_value>
auto tag_invoke(deserialize_tag, simdjson_value& val, AuthHeartbeatRequest& p) {
  ondemand::object obj;
  auto error = val.get_object().get(obj);
  if (error) return error;

  if ((error = obj["licenseKey"].get_string(p.license_key))) {
    return error;
  }
  if ((error = obj["nonce"].get_string(p.nonce))) {
    return error;
  }
  if ((error = obj["token"].get_string(p.token))) {
    return error;
  }

  return simdjson::SUCCESS;
}
template <typename builder_type>
void tag_invoke(serialize_tag, builder_type& builder, const AuthHeartbeatRequest& p) {
  builder.start_object();
  builder.template append_key_value<"licenseKey">(p.license_key);
  builder.append_comma();
  builder.template append_key_value<"nonce">(p.nonce);
  builder.append_comma();
  builder.template append_key_value<"token">(p.token);
  builder.end_object();
}
template <typename simdjson_value>
auto tag_invoke(deserialize_tag, simdjson_value& val, AuthHeartbeatResponse& p) {
  ondemand::object obj;
  auto error = val.get_object().get(obj);
  if (error) return error;

  if ((error = obj["status"].get_string(p.status))) {
    return error;
  }

  if ((error = obj["nonce"].get_string(p.nonce))) {
    return error;
  }

  if ((error = obj["feature_token_data"].get_string(p.feature_token_data))) {
    return error;
  }
  if ((error = obj["feature_token_sig"].get_string(p.feature_token_sig))) {
    return error;
  }
  if ((error = obj["feature_ttl"].get_string(p.feature_ttl))) {
    return error;
  }
  if ((error = obj["is_staff"].get_string(p.is_staff))) {
    return error;
  }
  if ((error = obj["addon_tier"].get_string(p.addon_tier))) {
    return error;
  }

  return simdjson::SUCCESS;
}
template <typename builder_type>
void tag_invoke(serialize_tag, builder_type& builder, const AuthHeartbeatResponse& p) {
  builder.start_object();
  builder.template append_key_value<"status">(p.status);
  builder.append_comma();
  builder.template append_key_value<"nonce">(p.nonce);
  builder.append_comma();
  builder.template append_key_value<"feature_token_data">(p.feature_token_data);
  builder.append_comma();
  builder.template append_key_value<"feature_token_sig">(p.feature_token_sig);
  builder.append_comma();
  builder.template append_key_value<"feature_ttl">(p.feature_ttl);
  builder.append_comma();
  builder.template append_key_value<"is_staff">(p.is_staff);
  builder.append_comma();
  builder.template append_key_value<"addon_tier">(p.addon_tier);
  builder.end_object();
}

template <typename simdjson_value>
auto tag_invoke(deserialize_tag, simdjson_value& val, BotSpec& p) {
  ondemand::object obj;
  auto error = val.get_object().get(obj);
  if (error) return error;

  if ((error = obj["POLICY_LAYERSIZES"].get(p.policy_layersizes))) {
    return error;
  }
  if ((error = obj["SHARED_HEAD_LAYERSIZES"].get(p.shared_head_layersizes))) {
    return error;
  }
  if ((error = obj["OBS_TYPE"].get(p.observer_type))) {
    return error;
  }
  if ((error = obj["ACTION_TYPE"].get(p.action_type))) {
    return error;
  }
  if ((error = obj["MODEL"].get_string(p.model_url))) {
    return error;
  }

  return simdjson::SUCCESS;
}
template <typename builder_type>
void tag_invoke(serialize_tag, builder_type& builder, const BotSpec& p) {
  builder.start_object();
  builder.template append_key_value<"POLICY_LAYERSIZES">(p.policy_layersizes);
  builder.append_comma();
  builder.template append_key_value<"SHARED_HEAD_LAYERSIZES">(p.shared_head_layersizes);
  builder.append_comma();
  builder.template append_key_value<"OBS_TYPE">(p.observer_type);
  builder.append_comma();
  builder.template append_key_value<"ACTION_TYPE">(p.action_type);
  builder.append_comma();
  builder.template append_key_value<"MODEL">(p.model_url);
  builder.end_object();
}

template <typename simdjson_value>
auto tag_invoke(deserialize_tag, simdjson_value& val, AuthBotListRequest& p) {
  ondemand::object obj;
  auto error = val.get_object().get(obj);
  if (error) return error;

  if ((error = obj["nonce"].get_string(p.nonce))) {
    return error;
  }
  if ((error = obj["token"].get_string(p.token))) {
    return error;
  }

  return simdjson::SUCCESS;
}
template <typename builder_type>
void tag_invoke(serialize_tag, builder_type& builder, const AuthBotListRequest& p) {
  builder.start_object();
  builder.template append_key_value<"nonce">(p.nonce);
  builder.append_comma();
  builder.template append_key_value<"token">(p.token);
  builder.end_object();
}

template <typename simdjson_value>
auto tag_invoke(deserialize_tag, simdjson_value& val, AuthBotListResponse& p) {
  ondemand::object obj;
  auto error = val.get_object().get(obj);
  if (error) return error;

  if ((error = obj["status"].get_string(p.status))) {
    return error;
  }
  if ((error = obj["nonce"].get_string(p.nonce))) {
    return error;
  }
  if ((error = obj["bot_list"].get(p.bot_list))) {
    return error;
  }
  if ((error = obj["addon_tier"].get(p.addon_tier))) {
    return error;
  }
  if ((error = obj["is_staff"].get(p.is_staff))) {
    return error;
  }

  return simdjson::SUCCESS;
}
template <typename builder_type>
void tag_invoke(serialize_tag, builder_type& builder, const AuthBotListResponse& p) {
  builder.start_object();
  builder.template append_key_value<"status">(p.status);
  builder.append_comma();
  builder.template append_key_value<"nonce">(p.nonce);
  builder.append_comma();
  builder.template append_key_value<"bot_list">(p.bot_list);
  builder.append_comma();
  builder.template append_key_value<"addon_tier">(p.addon_tier);
  builder.append_comma();
  builder.template append_key_value<"is_staff">(p.is_staff);
  builder.end_object();
}

}  // namespace simdjson
