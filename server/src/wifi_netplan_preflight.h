#pragma once
#include "wifi_process.h"
namespace encoder {
// Fixed production action. Read-only scope check, NOT permission to apply Wi-Fi.
// Installed checker and its ancestors must be root-controlled. Packaging is not
// installed automatically by this library. No client commands/paths are accepted.
bool wifi_netplan_preflight(WifiProcessRunner& runner, std::string* error) noexcept;
// Trusted local fixture/staging seam, never expose through RPC. Script ancestors
// must be trusted; file must be owned, regular, single-linked and not writable
// by other users. Python is always /usr/bin/python3, with an isolated environment.
bool wifi_netplan_preflight_at(WifiProcessRunner& runner, const std::string& script,
    const std::string& root, std::string* error) noexcept;
}
