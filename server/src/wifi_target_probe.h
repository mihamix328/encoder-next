#pragma once
#include "encoder/wifi_link_probe.h"
#include <functional>
namespace encoder {
// Read-only, privileged-helper side only. The main TLS server must not acquire
// access to the supplicant socket. Uses fixed wlan0 and its live IPv4 addresses.
WifiLink probe_wifi_target(const WifiProfile& target, std::string* message);

// Dependency seam for isolated tests and trusted adapters, NEVER client input.
// read_addresses must report only current, up/running wlan0 IPv4 addresses.
using WifiAddressReader = std::function<bool(std::vector<std::string>*, std::string*)>;
WifiLink probe_wifi_target_with(const WifiProfile& target, const std::string& trusted_socket,
    const WifiAddressReader& read_addresses, std::string* message);
}
