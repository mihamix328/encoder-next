#include "encoder/wifi_profile.h"
#include <openssl/evp.h>
namespace encoder {
namespace {
// Accept text SSIDs only. Validate before deriving a key: replacing malformed
// UTF-8 later would silently select a different SSID and a different PSK salt.
bool valid_text_ssid(std::string_view text) {
  for (size_t i = 0; i < text.size();) {
    const auto first = static_cast<unsigned char>(text[i++]);
    uint32_t point = first;
    unsigned remaining = 0;
    uint32_t minimum = 0;
    if (first >= 0xc2 && first <= 0xdf) { point = first & 0x1f; remaining = 1; minimum = 0x80; }
    else if (first >= 0xe0 && first <= 0xef) { point = first & 0x0f; remaining = 2; minimum = 0x800; }
    else if (first >= 0xf0 && first <= 0xf4) { point = first & 0x07; remaining = 3; minimum = 0x10000; }
    else if (first >= 0x80) return false;
    if (text.size() - i < remaining) return false;
    for (unsigned n = 0; n < remaining; ++n) {
      const auto next = static_cast<unsigned char>(text[i++]);
      if ((next & 0xc0) != 0x80) return false;
      point = (point << 6) | (next & 0x3f);
    }
    if (point < minimum || point > 0x10ffff || (point >= 0xd800 && point <= 0xdfff)) return false;
    if (point < 0x20 || (point >= 0x7f && point <= 0x9f) || point == 0x2028 || point == 0x2029) return false;
  }
  return true;
}
}
std::optional<WifiProfile> WifiProfile::make(std::string_view ssid, std::string_view password, std::string* error) {
  auto fail = [&](const char* message) -> std::optional<WifiProfile> { if (error) *error = message; return std::nullopt; };
  if (error) error->clear();
  if (ssid.empty() || ssid.size() > 32) return fail("SSID must contain 1 to 32 bytes");
  if (!valid_text_ssid(ssid)) return fail("SSID must be valid UTF-8 text without control characters or line separators");
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
