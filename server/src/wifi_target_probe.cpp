#include "wifi_target_probe.h"
#include "network_status.h"
#include <ifaddrs.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <memory>
#include <cstring>
namespace encoder {
namespace {
bool read_wlan0_addresses(std::vector<std::string>* output, std::string* error) {
  output->clear();
  ifaddrs* raw = nullptr;
  if (getifaddrs(&raw)) { *error = "Cannot read Wi-Fi interface addresses"; return false; }
  std::unique_ptr<ifaddrs, decltype(&freeifaddrs)> addresses(raw, freeifaddrs);
  for (auto* item = raw; item; item = item->ifa_next) {
    if (!item->ifa_name || std::strcmp(item->ifa_name, "wlan0") || !item->ifa_addr ||
        item->ifa_addr->sa_family != AF_INET || !(item->ifa_flags & IFF_UP) || !(item->ifa_flags & IFF_RUNNING)) continue;
    char text[INET_ADDRSTRLEN];
    const auto* address = reinterpret_cast<const sockaddr_in*>(item->ifa_addr);
    if (!inet_ntop(AF_INET, &address->sin_addr, text, sizeof(text))) {
      *error = "Cannot format Wi-Fi interface address"; return false;
    }
    output->emplace_back(text);
  }
  return true;
}
}
WifiLink probe_wifi_target_with(const WifiProfile& target, const std::string& trusted_socket,
    const WifiAddressReader& read_addresses, std::string* message) {
  auto fail = [&](const char* text) { if (message) *message = text; return WifiLink::Failed; };
  if (target.psk().size() != 32 || target.ssid_hex().empty()) return fail("Invalid or consumed target profile");
  std::string status, error;
  // Never inspect the cached snapshot used by the UI for a commit decision.
  if (!wifi_connection_status(trusted_socket, &status, &error)) return fail("Fresh Wi-Fi status unavailable");
  std::vector<std::string> addresses;
  try {
    if (!read_addresses(&addresses, &error)) return fail("Fresh Wi-Fi addresses unavailable");
  } catch (...) { return fail("Fresh Wi-Fi addresses unavailable"); }
  return assess_wifi_link(target, status, addresses, message);
}
WifiLink probe_wifi_target(const WifiProfile& target, std::string* message) {
  return probe_wifi_target_with(target, "/run/wpa_supplicant/wlan0", read_wlan0_addresses, message);
}
}
