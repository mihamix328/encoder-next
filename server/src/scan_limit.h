#pragma once
#include <string>
namespace encoder {
// Reserves a 30-second slot before attempting SCAN; failed attempts also count.
bool reserve_scan_slot(const std::string& directory, std::string* error);
}
