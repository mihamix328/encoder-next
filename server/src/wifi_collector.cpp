#include "network_status.h"
#include "snapshot_publish.h"
#include "scan_limit.h"
#include <ctime>
#include <iostream>

// No listener or client-controlled paths/commands. --check never publishes a file.
int main(int argc, char** argv) {
  const bool check_only = argc == 2 && std::string(argv[1]) == "--check";
  const bool scan_only = argc == 2 && std::string(argv[1]) == "--scan";
  if (argc != 1 && !check_only && !scan_only) {
    std::cerr << "Usage: encoder-wifi-collector [--check|--scan]\n";
    return 1;
  }
  std::string results, error;
  if (scan_only) {
    if (!encoder::reserve_scan_slot("/run/encoder-network", &error) ||
        !encoder::wifi_request_scan("/run/wpa_supplicant/wlan0", &error)) {
      std::cerr << error << '\n'; return 1;
    }
    std::cout << "Scan request accepted; completion and fresh results are not yet confirmed\n";
    return 0;
  }
  if (!encoder::wifi_cached_results("/run/wpa_supplicant/wlan0", &results, &error)) {
    std::cerr << error << '\n';
    return 1;
  }
  std::string raw_status, connection;
  if (!encoder::wifi_connection_status("/run/wpa_supplicant/wlan0", &raw_status, &error) ||
      !encoder::sanitize_wifi_status(raw_status, &connection, &error)) {
    std::cerr << error << '\n'; return 1;
  }
  const std::string data = "encoder-wifi-v2 " + std::to_string(std::time(nullptr)) + "\n" +
                           connection + "\n" + results;
  if (data.size() >= 65536) return 1;
  if (check_only) {
    std::cout << "Wi-Fi cache and connection status OK; v2 snapshot validated (" << data.size()
              << " bytes); no snapshot published, no scan requested\n";
    return 0;
  }
  if (!encoder::publish_wifi_snapshot("/run/encoder-network", data, &error)) {
    std::cerr << error << '\n';
    return 1;
  }
  return 0;
}
