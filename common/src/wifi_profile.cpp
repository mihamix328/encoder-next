#include "encoder/wifi_profile.h"
#include <openssl/evp.h>
namespace encoder {
std::optional<WifiProfile> WifiProfile::make(std::string_view ssid, std::string_view password, std::string* error) {
  auto fail = [&](const char* message) -> std::optional<WifiProfile> { if (error) *error = message; return std::nullopt; };
  if (error) error->clear();
  if (ssid.empty() || ssid.size() > 32) return fail("SSID must contain 1 to 32 bytes");
  // The UI supports text names; reject controls instead of silently trimming them.
  for (unsigned char c : ssid) if (c < 32 || c == 127) return fail("SSID contains unsupported control characters");
  auto hex = [](unsigned char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
  };
  if (password.size() == 64) {
    for (unsigned char c : password) if (hex(c) < 0) return fail("A 64-character PSK must be hexadecimal");
  } else {
    if (password.size() < 8 || password.size() > 63) return fail("WPA2 password must contain 8 to 63 ASCII characters or 64 hexadecimal digits");
    for (unsigned char c : password) if (c < 32 || c > 126) return fail("WPA2 password contains unsupported characters");
  }
  WifiProfile profile;
  constexpr char digits[] = "0123456789abcdef";
  for (unsigned char c : ssid) { profile.ssid_hex_ += digits[c >> 4]; profile.ssid_hex_ += digits[c & 15]; }
  profile.psk_.resize(32);
  if (password.size() == 64) {
    for (size_t i = 0; i < 32; ++i) profile.psk_.data()[i] = static_cast<uint8_t>((hex(password[2*i]) << 4) | hex(password[2*i+1]));
  } else if (PKCS5_PBKDF2_HMAC_SHA1(password.data(), static_cast<int>(password.size()),
        reinterpret_cast<const unsigned char*>(ssid.data()), static_cast<int>(ssid.size()),
        4096, 32, profile.psk_.data()) != 1) return fail("Cannot derive Wi-Fi key");
  return std::optional<WifiProfile>(std::move(profile));
}
}
