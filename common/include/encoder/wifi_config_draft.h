#pragma once
#include "encoder/wifi_profile.h"

namespace encoder {
// Serialization only; neither document is installed or applied by this API.
// Netplan YAML alone does NOT enforce WPA2-only operation. A future adapter must
// ensure the strict supplicant policy is used, including after reboot, and must
// reject conflicting existing wlan0 definitions before installing any draft.
struct WifiConfigDraft {
  SecureBuffer netplan_yaml;
  SecureBuffer supplicant_config;
};
std::optional<WifiConfigDraft> make_wifi_config_draft(const WifiProfile& profile, std::string* error);
}
