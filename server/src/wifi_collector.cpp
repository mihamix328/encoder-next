#include "network_status.h"
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <cstdio>
#include <ctime>
#include <iostream>

// No listener and no client-controlled arguments. Invoked only by a local timer.
int main(int argc, char**) {
  if (argc != 1) return 1;
  std::string results, error;
  if (!encoder::wifi_cached_results("/run/wpa_supplicant/wlan0", &results, &error)) {
    std::cerr << error << '\n';
    return 1;
  }
  const std::string data = "encoder-wifi-v1 " + std::to_string(std::time(nullptr)) + "\n" + results;
  if (data.size() >= 65536) return 1;
  // Directory is owned by root:encoder and not writable by the server group.
  const std::string temporary = "/run/encoder-network/.wifi-" + std::to_string(getpid());
  const int fd = open(temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0640);
  if (fd < 0) return 1;
  size_t written = 0;
  while (written < data.size()) {
    const auto count = write(fd, data.data() + written, data.size() - written);
    if (count < 0 && errno == EINTR) continue;
    if (count <= 0) break;
    written += static_cast<size_t>(count);
  }
  const bool synced = fsync(fd) == 0;
  const bool closed = close(fd) == 0;
  if (written != data.size() || !synced || !closed ||
      rename(temporary.c_str(), "/run/encoder-network/wifi.txt") != 0) {
    unlink(temporary.c_str());
    return 1;
  }
  return 0;
}
