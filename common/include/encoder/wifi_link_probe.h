#pragma once
#include "encoder/wifi_change.h"
#include <vector>
namespace encoder {
// Pure assessment, no socket calls. The privileged adapter must supply a FRESH
// raw STATUS from wlan0 (not the cached UI snapshot) and CURRENT addresses read
// from that same interface. This checks local readiness, not Internet access,
// TLS reachability, watchdog health, or exclusive ownership of configuration.
WifiLink assess_wifi_link(const WifiProfile& target, std::string_view status,
    const std::vector<std::string>& interface_ipv4, std::string* message);
}
