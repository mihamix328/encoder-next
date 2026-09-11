#pragma once
#include <string>

namespace encoder {
// Read-only snapshot. No shell commands, configuration changes or privileges.
bool network_status(std::string* output, std::string* error);
}
