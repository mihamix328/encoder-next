#include "wifi_runtime_policy.h"
#include "wifi_netplan_preflight.h"
#include <unistd.h>
#include <iostream>
#include <string>
int main(int argc, char** argv) {
  if (argc != 2 || (std::string(argv[1]) != "--check" && std::string(argv[1]) != "--stage")) {
    std::cerr << "Usage: encoder-wifi-policy-guard --check|--stage\n"; return 2;
  }
  if (geteuid() != 0) { std::cerr << "Privileged helper required\n"; return 1; }
  try {
    encoder::WifiProcessRunner runner; std::string error;
    if (!encoder::wifi_netplan_preflight(runner, &error) ||
        !encoder::prepare_wifi_runtime_policy("/etc/netplan", "/run/encoder-wifi-policy",
          std::string(argv[1]) == "--check", &error)) {
      std::cerr << "Wi-Fi runtime policy preparation refused\n"; return 1;
    }
    std::cout << "Wi-Fi policy preparation succeeded; no network service was started\n";
    return 0;
  } catch (...) { std::cerr << "Wi-Fi policy preparation failed\n"; return 1; }
}
