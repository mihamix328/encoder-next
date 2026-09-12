// Linux-only isolated test. Uses a fake supplicant, never the real network.
#include "network_status.h"
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <thread>
#include <chrono>

int main() {
  char directory[] = "/tmp/encoder-wifi-test-XXXXXX";
  if (!mkdtemp(directory)) return 1;
  const std::string path = std::string(directory) + "/fake";
  const int fd = socket(AF_UNIX, SOCK_DGRAM, 0);
  sockaddr_un address{};
  address.sun_family = AF_UNIX;
  std::memcpy(address.sun_path, path.c_str(), path.size() + 1);
  if (fd < 0 || bind(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address))) {
    if (fd >= 0) close(fd);
    rmdir(directory);
    return 1;
  }
  timeval timeout{5, 0};
  setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
  bool commands_ok = true;
  const std::string valid = "bssid / frequency / signal level / flags / ssid\n"
      "54:83:3a:70:05:61\t5180\t-66\t[WPA2-PSK-CCMP][ESS]\tTest network\n";
  std::thread fake([&]() {
    for (const auto& response : {valid, std::string("FAIL\n"), std::string(65536, 'x')}) {
      char command[128];
      sockaddr_un peer{};
      socklen_t length = sizeof(peer);
      const auto count = recvfrom(fd, command, sizeof(command), 0,
          reinterpret_cast<sockaddr*>(&peer), &length);
      if (count != 12 || std::string(command, count > 0 ? count : 0) != "SCAN_RESULTS") {
        commands_ok = false;
        return;
      }
      sendto(fd, response.data(), response.size(), 0, reinterpret_cast<sockaddr*>(&peer), length);
    }
  });
  std::string result, error;
  bool ok = encoder::wifi_cached_results(path, &result, &error) && result == valid;
  ok = !encoder::wifi_cached_results(path, &result, &error) && result.empty() && ok;
  ok = !encoder::wifi_cached_results(path, &result, &error) &&
       error.find("oversized") != std::string::npos && ok;
  fake.join();
  const auto started = std::chrono::steady_clock::now();
  ok = !encoder::wifi_cached_results(path, &result, &error) &&
       error.find("timed out") != std::string::npos && ok;
  const auto elapsed = std::chrono::steady_clock::now() - started;
  ok = elapsed >= std::chrono::seconds(1) && elapsed < std::chrono::seconds(5) && ok;
  close(fd);
  unlink(path.c_str());
  rmdir(directory);
  ok = !encoder::wifi_cached_results(path, &result, &error) && ok;
  ok = !encoder::wifi_cached_results("relative/path", &result, &error) && ok;
  ok = !encoder::wifi_cached_results(std::string(200, '/'), &result, &error) && ok;
  if (!ok || !commands_ok) { std::cerr << "Wi-Fi socket checks failed\n"; return 1; }
  std::cout << "Wi-Fi socket checks passed\n";
}
