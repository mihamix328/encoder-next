#pragma once
#include <string>

namespace encoder {
// Read-only snapshot. No shell commands, configuration changes or privileges.
bool network_status(std::string* output, std::string* error);
bool wifi_cached_results(const std::string& control_socket, std::string* output, std::string* error);
}
