#pragma once
#include <functional>
#include <string>
namespace encoder {
// Fixed request; no caller-supplied shell commands, paths or network settings.
bool request_wifi_scan(const std::string& socket_path, std::string* output, std::string* error);
#ifdef __linux__
using ScanAction = std::function<bool(std::string*, std::string*)>;
bool serve_wifi_scan(int fd, const ScanAction& action);
#endif
}
