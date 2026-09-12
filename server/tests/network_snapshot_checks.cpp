// Linux-only test: private temporary files and read-only interface enumeration.
#include "network_status.h"
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>

int main() {
  char temporary[] = "/tmp/encoder-snapshot-test-XXXXXX";
  if (!mkdtemp(temporary)) return 1;
  const auto path = std::filesystem::path(temporary) / "wifi.txt";
  std::string result, error;
  bool ok = !encoder::wifi_snapshot(path.string(), &result, &error);
  const std::string heading = "bssid / frequency / signal level / flags / ssid\n";
  const auto now = std::time(nullptr);
  auto check = [&](const std::string& data, bool expected) {
    { std::ofstream file(path, std::ios::binary); file << data; }
    const bool accepted = encoder::wifi_snapshot(path.string(), &result, &error);
    ok = (accepted == expected) && (accepted || result.empty()) && ok;
  };
  check("encoder-wifi-v1 " + std::to_string(now) + "\n" + heading, true);
  ok = result == heading && ok;
  check("encoder-wifi-v1 " + std::to_string(now - 120) + "\n" + heading, false);
  check("encoder-wifi-v1 " + std::to_string(now + 3600) + "\n" + heading, false);
  check("encoder-wifi-v1 999999999999999999999999999\n" + heading, false);
  check("encoder-wifi-v1 " + std::to_string(now) + "\nFAIL\n", false);
  check(std::string(65536, 'x'), false);
  check("", false);
  ok = encoder::network_status(&result, &error) && result.find("127.0.0.1") != std::string::npos && ok;
  std::filesystem::remove(path);
  std::filesystem::remove(temporary);
  if (!ok) { std::cerr << "Network snapshot checks failed\n"; return 1; }
  std::cout << "Network snapshot and loopback checks passed\n";
}
