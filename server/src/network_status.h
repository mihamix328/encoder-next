#pragma once
#include <string>

namespace encoder {
// Read-only snapshot. No shell commands, configuration changes or privileges.
bool network_status(std::string* output, std::string* error);
bool wifi_snapshot(const std::string& path, std::string* output, std::string* error, bool current_status = false);
bool sanitize_wifi_status(const std::string& raw, std::string* output, std::string* error);
// Collector only; not linked into the network-facing server.
bool wifi_cached_results(const std::string& control_socket, std::string* output, std::string* error);
bool wifi_connection_status(const std::string& control_socket, std::string* output, std::string* error);
}
