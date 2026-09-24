#pragma once
#include <string>
namespace encoder {
// Trusted operator directories. Derive strict WPA2/CCMP policy from the saved
// canonical profile, never accept credentials/commands from CLI or RPC here.
// Caller serializes with profile updates. Does NOT start/restart any service.
bool prepare_wifi_runtime_policy(const std::string& netplan_directory,
    const std::string& runtime_directory, bool check_only, std::string* error);
}
