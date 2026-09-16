// Read-only manual probe, using the production parser; never prints network identities.
#include "network_status.h"
#include <iostream>

int main(int argc, char** argv) {
  if (argc != 2) {
    std::cerr << "Usage: read-live-snapshot FILE\n";
    return 1;
  }
  std::string networks, connection, error;
  if (!encoder::wifi_snapshot(argv[1], &networks, &error) ||
      !encoder::wifi_snapshot(argv[1], &connection, &error, true)) {
    std::cerr << error << '\n';
    return 1;
  }
  std::cout << "Production parser OK: Wi-Fi list and connection status readable; identities not printed\n";
  return 0;
}
