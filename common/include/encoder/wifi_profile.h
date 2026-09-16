#pragma once
#include "encoder/secure_memory.h"
#include <optional>
#include <string>
#include <string_view>
namespace encoder {
// First supported mode is WPA2-Personal only. No open/enterprise/WPA3 downgrade.
class WifiProfile {
 public:
  static std::optional<WifiProfile> make(std::string_view ssid, std::string_view password, std::string* error);
  WifiProfile(WifiProfile&&) noexcept = default;
  WifiProfile& operator=(WifiProfile&&) noexcept = default;
  WifiProfile(const WifiProfile&) = delete;
  WifiProfile& operator=(const WifiProfile&) = delete;
  const std::string& ssid_hex() const { return ssid_hex_; }
  const SecureBuffer& psk() const { return psk_; }
 private:
  WifiProfile() = default;
  std::string ssid_hex_;
  SecureBuffer psk_;
};
}
