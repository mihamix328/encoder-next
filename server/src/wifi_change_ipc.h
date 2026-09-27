#pragma once
#include <string>
#include <functional>
#include "encoder/net.h"
namespace encoder {
bool wifi_change_recovery_connection(const Socket& socket);
// Fixed, bounded packet exchange. Root peer identity is checked on Linux.
bool request_wifi_change(const std::string& packet, std::string* response);
#ifdef __linux__
bool serve_wifi_change_packet(int fd, unsigned allowed_uid,
    const std::function<std::string(const std::string&)>& handler);
#endif
}
