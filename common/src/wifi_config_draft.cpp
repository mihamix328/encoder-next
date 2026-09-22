#include "encoder/wifi_config_draft.h"
#include <cstring>
#include <openssl/crypto.h>

namespace encoder {
namespace {
constexpr char digits[] = "0123456789abcdef";
constexpr std::string_view yaml_prefix = "# encoder managed wifi v1\n"
    "# Draft only: requires verified WPA2 enforcement before installation.\n"
    "network:\n  version: 2\n  wifis:\n    wlan0:\n"
    "      renderer: networkd\n      dhcp4: true\n      access-points:\n        ";
constexpr std::string_view yaml_auth = ":\n          auth:\n            key-management: psk\n            password: \"";
// The only secret-bearing output is allocated once in an automatically wiped
// buffer. Never format the PSK into a std::string, exception, or log message.
SecureBuffer with_psk(const std::string& prefix, const SecureBuffer& psk, std::string_view suffix) {
  SecureBuffer result(prefix.size() + 64 + suffix.size());
  std::memcpy(result.data(), prefix.data(), prefix.size());
  auto* target = result.data() + prefix.size();
  for (size_t i = 0; i < 32; ++i) {
    target[2*i] = digits[psk.data()[i] >> 4];
    target[2*i+1] = digits[psk.data()[i] & 15];
  }
  std::memcpy(target + 64, suffix.data(), suffix.size());
  return result;
}
int unhex(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return -1;
}
}
std::optional<WifiConfigDraft> make_wifi_config_draft(const WifiProfile& profile, std::string* error) {
  if (error) error->clear();
  auto fail = [&]() -> std::optional<WifiConfigDraft> {
    if (error) *error = "Cannot generate configuration from an invalid or consumed Wi-Fi profile";
    return std::nullopt;
  };
  const auto& hex = profile.ssid_hex();
  if (profile.psk().size() != 32 || hex.empty() || hex.size() > 64 || hex.size() % 2) return fail();
  // WifiProfile has already validated UTF-8. Preserve its bytes, including
  // whitespace; escape YAML metacharacters inside one double-quoted scalar.
  std::string quoted = "\"";
  for (size_t i = 0; i < hex.size(); i += 2) {
    const int high = unhex(hex[i]), low = unhex(hex[i+1]);
    if (high < 0 || low < 0) return fail();
    const char c = static_cast<char>((high << 4) | low);
    if (c == '\\' || c == '"') quoted += '\\';
    quoted += c;
  }
  quoted += '"';
  // Renderer is deliberately scoped to wlan0, never the whole machine.
  const std::string yaml = std::string(yaml_prefix) + quoted + std::string(yaml_auth);
  const std::string wpa = "# encoder managed WPA2 draft v1\n"
      "ctrl_interface=/run/wpa_supplicant\nupdate_config=0\nnetwork={\n  ssid=" + hex +
      "\n  proto=RSN\n  key_mgmt=WPA-PSK\n  pairwise=CCMP\n  group=CCMP\n  psk=";
  WifiConfigDraft result;
  result.netplan_yaml = with_psk(yaml, profile.psk(), "\"\n");
  result.supplicant_config = with_psk(wpa, profile.psk(), "\n}\n");
  return std::optional<WifiConfigDraft>(std::move(result));
}
std::optional<WifiProfile> read_wifi_config_draft(std::string_view input, std::string* error) {
  if (error) error->clear();
  auto fail = [&]() -> std::optional<WifiProfile> {
    if (error) *error = "Wi-Fi configuration is not an exact supported encoder-generated draft";
    return std::nullopt;
  };
  if (input.size() > 4096 || input.substr(0, yaml_prefix.size()) != yaml_prefix) return fail();
  auto remaining = input.substr(yaml_prefix.size());
  if (remaining.empty() || remaining.front() != '"') return fail();
  remaining.remove_prefix(1);
  std::string ssid;
  size_t position = 0;
  bool closed = false;
  while (position < remaining.size()) {
    char c = remaining[position++];
    if (c == '"') { closed = true; break; }
    if (c == '\\') {
      if (position == remaining.size()) return fail();
      c = remaining[position++];
      if (c != '\\' && c != '"') return fail();
    }
    ssid += c;
    if (ssid.size() > 32) return fail();
  }
  if (!closed) return fail();
  remaining.remove_prefix(position);
  if (remaining.size() != yaml_auth.size() + 64 + 2 || remaining.substr(0, yaml_auth.size()) != yaml_auth ||
      remaining.substr(remaining.size() - 2) != "\"\n") return fail();
  const auto key = remaining.substr(yaml_auth.size(), 64);
  for (char c : key) if (unhex(c) < 0) return fail();
  auto profile = WifiProfile::make(ssid, key, nullptr);
  if (!profile) return fail();
  auto canonical = make_wifi_config_draft(*profile, nullptr);
  if (!canonical || canonical->netplan_yaml.size() != input.size() ||
      CRYPTO_memcmp(canonical->netplan_yaml.data(), input.data(), input.size())) return fail();
  return profile;
}
}
