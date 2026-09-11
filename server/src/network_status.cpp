#include "network_status.h"
#include <sstream>
#include <memory>
#ifdef __linux__
#include <ifaddrs.h>
#include <net/if.h>
#include <arpa/inet.h>
#endif

namespace encoder {
bool network_status(std::string* output, std::string* error) {
  output->clear();
#ifdef __linux__
  ifaddrs* raw = nullptr;
  if (getifaddrs(&raw) != 0) {
    *error = "Cannot read network interfaces";
    return false;
  }
  std::unique_ptr<ifaddrs, decltype(&freeifaddrs)> addresses(raw, freeifaddrs);
  std::ostringstream text;
  text << "Interface | Link | IPv4\n";
  for (auto* entry = raw; entry; entry = entry->ifa_next) {
    if (!entry->ifa_addr || entry->ifa_addr->sa_family != AF_INET) continue;
    char address[INET_ADDRSTRLEN] = {};
    const auto* ipv4 = reinterpret_cast<const sockaddr_in*>(entry->ifa_addr);
    if (!inet_ntop(AF_INET, &ipv4->sin_addr, address, sizeof(address))) continue;
    text << entry->ifa_name << " | "
         << ((entry->ifa_flags & IFF_RUNNING) ? "carrier" : "no carrier")
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
