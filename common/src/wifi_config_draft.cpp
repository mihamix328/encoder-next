#include "encoder/wifi_config_draft.h"
#include <cstring>

namespace encoder {
namespace {
constexpr char digits[] = "0123456789abcdef";
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
  const std::string yaml = "# encoder managed wifi v1\n"
      "# Draft only: requires verified WPA2 enforcement before installation.\n"
      "network:\n  version: 2\n  wifis:\n    wlan0:\n"
      "      renderer: networkd\n      dhcp4: true\n      access-points:\n        " + quoted +
      ":\n          auth:\n            key-management: psk\n            password: \"";
  const std::string wpa = "# encoder managed WPA2 draft v1\n"
      "ctrl_interface=/run/wpa_supplicant\nupdate_config=0\nnetwork={\n  ssid=" + hex +
      "\n  proto=RSN\n  key_mgmt=WPA-PSK\n  pairwise=CCMP\n  group=CCMP\n  psk=";
  WifiConfigDraft result;
  result.netplan_yaml = with_psk(yaml, profile.psk(), "\"\n");
  result.supplicant_config = with_psk(wpa, profile.psk(), "\n}\n");
  return std::optional<WifiConfigDraft>(std::move(result));
}
}
