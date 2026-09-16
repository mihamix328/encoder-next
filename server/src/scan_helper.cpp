#include "scan_ipc.h"
#include "scan_limit.h"
#include "network_status.h"
#include <unistd.h>
// Invoked only by socket-activated systemd service, one accepted socket on stdin.
int main() {
  return encoder::serve_wifi_scan(STDIN_FILENO, [](std::string* output, std::string* error) {
    return encoder::reserve_scan_slot("/run/encoder-network", error) &&
        encoder::wifi_scan_and_wait("/run/wpa_supplicant/wlan0", output, error);
  }) ? 0 : 1;
}
