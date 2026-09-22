#include "encoder/wifi_link_probe.h"
#include <algorithm>
#include <array>
#include <map>
namespace encoder {
namespace {
int hex_value(unsigned char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}
bool ssid_hex(std::string_view encoded, std::string* result) {
  result->clear();
  constexpr char hex[] = "0123456789abcdef";
  for (size_t i = 0; i < encoded.size(); ++i) {
    auto c = static_cast<unsigned char>(encoded[i]);
    if (c == '\\') {
      if (++i == encoded.size()) return false;
      switch (encoded[i]) {
        case '\\': c = '\\'; break;
        case '"': c = '"'; break;
        case 'n': c = '\n'; break;
        case 'r': c = '\r'; break;
        case 't': c = '\t'; break;
        case 'e': c = 27; break;
        case 'x': {
          if (i + 2 >= encoded.size()) return false;
          const int high = hex_value(encoded[i+1]), low = hex_value(encoded[i+2]);
          if (high < 0 || low < 0) return false;
          c = static_cast<unsigned char>((high << 4) | low); i += 2; break;
        }
        default: return false;
      }
    }
    *result += hex[c >> 4]; *result += hex[c & 15];
    if (result->size() > 64) return false;
  }
  return !result->empty();
}
bool usable_ipv4(std::string_view ip) {
  std::array<unsigned, 4> parts{};
  size_t position = 0;
  for (size_t i = 0; i < parts.size(); ++i) {
    const size_t start = position;
    while (position < ip.size() && ip[position] >= '0' && ip[position] <= '9') {
      if (position - start >= 3) return false;
      parts[i] = parts[i] * 10 + static_cast<unsigned>(ip[position++] - '0');
    }
    if (position == start || parts[i] > 255 || (position - start > 1 && ip[start] == '0')) return false;
    if (i != 3 && (position == ip.size() || ip[position++] != '.')) return false;
  }
  return position == ip.size() && parts[0] != 0 && parts[0] != 127 && parts[0] < 224 &&
      !(parts[0] == 169 && parts[1] == 254);
}
}
WifiLink assess_wifi_link(const WifiProfile& target, std::string_view status,
    const std::vector<std::string>& interface_ipv4, std::string* message) {
  auto result = [&](WifiLink link, const char* text) { if (message) *message = text; return link; };
  if (target.psk().size() != 32 || target.ssid_hex().empty())
    return result(WifiLink::Failed, "Invalid or consumed target profile");
  if (status.empty() || status.size() > 8192 || status.back() != '\n')
    return result(WifiLink::Failed, "Missing, oversized or incomplete Wi-Fi status");
  std::map<std::string_view, std::string_view> fields;
  while (!status.empty()) {
    const auto end = status.find('\n');
    const auto line = status.substr(0, end);
    status.remove_prefix(end + 1);
    for (unsigned char c : line) if (c < 32 || c == 127)
      return result(WifiLink::Failed, "Invalid Wi-Fi status characters");
    const auto equals = line.find('=');
    if (equals == std::string_view::npos) return result(WifiLink::Failed, "Invalid Wi-Fi status line");
    const auto key = line.substr(0, equals), value = line.substr(equals + 1);
    if (key != "wpa_state" && key != "ssid" && key != "key_mgmt" && key != "pairwise_cipher" &&
        key != "group_cipher" && key != "ip_address") continue;
    if (value.size() > 256 || !fields.emplace(key, value).second)
      return result(WifiLink::Failed, "Duplicate or oversized Wi-Fi status field");
  }
  const auto state = fields["wpa_state"];
  if (state == "DISCONNECTED" || state == "INACTIVE" || state == "SCANNING" || state == "AUTHENTICATING" ||
      state == "ASSOCIATING" || state == "ASSOCIATED" || state == "4WAY_HANDSHAKE" || state == "GROUP_HANDSHAKE")
    return result(WifiLink::Pending, "Waiting for Wi-Fi authentication");
  if (state != "COMPLETED") return result(WifiLink::Failed, "Wi-Fi interface disabled or unknown state");
  std::string actual;
  if (!ssid_hex(fields["ssid"], &actual)) return result(WifiLink::Failed, "Invalid connected network identity");
  if (actual != target.ssid_hex()) return result(WifiLink::Pending, "Waiting for the requested network");
  if (fields["key_mgmt"] != "WPA2-PSK" || fields["pairwise_cipher"] != "CCMP" || fields["group_cipher"] != "CCMP")
    return result(WifiLink::Failed, "Connected network does not meet WPA2-CCMP policy");
  const auto ip = fields["ip_address"];
  if (!usable_ipv4(ip) || std::find(interface_ipv4.begin(), interface_ipv4.end(), ip) == interface_ipv4.end())
    return result(WifiLink::Pending, "Waiting for a usable IPv4 address on wlan0");
  return result(WifiLink::Ready, "Requested WPA2 network and local IPv4 are ready");
}
}
