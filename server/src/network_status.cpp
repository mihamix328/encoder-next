#include "network_status.h"
#include <sstream>
#include <memory>
#include <fstream>
#include <ctime>
#include <charconv>
#ifdef __linux__
#include <ifaddrs.h>
#include <net/if.h>
#include <arpa/inet.h>
#endif

namespace encoder {
bool wifi_snapshot(const std::string& path, std::string* output, std::string* error) {
  output->clear();
  std::ifstream file(path, std::ios::binary);
  if (!file) { *error = "Wi-Fi snapshot unavailable; collector is not installed or not running"; return false; }
  char buffer[65536];
  file.read(buffer, sizeof(buffer));
  const auto size = file.gcount();
  if (file.bad() || size == sizeof(buffer)) { *error = "Wi-Fi snapshot is oversized or unreadable"; return false; }
  std::string data(buffer, static_cast<size_t>(size));
  const auto newline = data.find('\n');
  const std::string prefix = "encoder-wifi-v1 ";
  long long timestamp = 0;
  if (data.rfind(prefix, 0) != 0 || newline == std::string::npos || newline <= prefix.size()) {
    *error = "Invalid Wi-Fi snapshot format"; return false;
  }
  const auto parsed = std::from_chars(data.data() + prefix.size(), data.data() + newline, timestamp);
  const auto now = static_cast<long long>(std::time(nullptr));
  if (parsed.ec != std::errc{} || parsed.ptr != data.data() + newline || timestamp < 0 ||
      timestamp > now || now - timestamp > 90) {
    *error = "Wi-Fi snapshot expired or has invalid time"; return false;
  }
  const auto results = data.substr(newline + 1);
  if (results.rfind("bssid / frequency / signal level / flags / ssid\n", 0) != 0) {
    *error = "Invalid Wi-Fi snapshot results"; return false;
  }
  *output = results;
  return true;
}

bool network_status(std::string* output, std::string* error) {
  output->clear();
#ifdef __linux__
  ifaddrs* raw = nullptr;
  if (getifaddrs(&raw) != 0) { *error = "Cannot read network interfaces"; return false; }
  std::unique_ptr<ifaddrs, decltype(&freeifaddrs)> addresses(raw, freeifaddrs);
  std::ostringstream text;
  text << "Interface | Link | IPv4\n";
  for (auto* entry = raw; entry; entry = entry->ifa_next) {
    if (!entry->ifa_addr || entry->ifa_addr->sa_family != AF_INET) continue;
    char address[INET_ADDRSTRLEN] = {};
    const auto* ipv4 = reinterpret_cast<const sockaddr_in*>(entry->ifa_addr);
    if (!inet_ntop(AF_INET, &ipv4->sin_addr, address, sizeof(address))) continue;
    text << entry->ifa_name << " | " << ((entry->ifa_flags & IFF_RUNNING) ? "carrier" : "no carrier")
         << " | " << address << '\n';
  }
  *output = text.str();
  return true;
#else
  *error = "Network status is supported only on Linux servers";
  return false;
#endif
}
}
