#pragma once
#include <functional>
#include <string>
namespace encoder {
// Linux-only event loop for a SEPARATE process. Trusted private state directory
// and trusted Netplan directory; never client-supplied paths. One supervisor per
// state directory. No network command is provided by this experimental library.
// reconfigure must be bounded/idempotent and restore the actual network, not
// merely return true. should_stop must be bounded. Callbacks may throw: recovery
// exceptions are retried; a stop callback exception fails the loop.
// Journal failures/lock contention are retried every 250 ms; they never resolve
// a pending transaction. A production service still needs restart supervision.
bool run_wifi_recovery_supervisor(const std::string& state_directory,
    const std::string& netplan_directory, const std::function<bool()>& reconfigure,
    const std::function<bool()>& should_stop, std::string* error);
// Bounded local liveness exchange with a same-uid supervisor watching the exact
// directory inodes. This is NOT proof of successful rollback or WPA2 enforcement.
// No credentials, arbitrary commands, or file contents cross this socket.
bool wifi_recovery_supervisor_ready(const std::string& state_directory,
    const std::string& netplan_directory) noexcept;
}
