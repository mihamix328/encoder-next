#pragma once
#include <string>

namespace encoder {
// Linux-only. Directory must belong to the caller and not be group/world writable.
bool publish_wifi_snapshot(const std::string& directory, const std::string& data,
                           std::string* error);
}
