#include "network_status.h"
#include <sstream>
#include <memory>
#include <cstring>
#include <cstdlib>
#ifdef __linux__
#include <ifaddrs.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <poll.h>
#endif

namespace encoder {
bool wifi_cached_results(const std::string& control_socket, std::string* output, std::string* error) {
  output->clear();
#ifdef __linux__
  sockaddr_un remote{};
  if (control_socket.empty() || control_socket[0] != '/' ||
      control_socket.size() >= sizeof(remote.sun_path) ||
      control_socket.find('\0') != std::string::npos) {
    *error = "Invalid Wi-Fi control socket path";
    return false;
  }
  struct LocalSocket {
    int fd = -1;
    std::string directory, path;
    ~LocalSocket() {
      if (fd >= 0) close(fd);
      if (!path.empty()) unlink(path.c_str());
      if (!directory.empty()) rmdir(directory.c_str());
    }
  } local;
  char directory[] = "/tmp/encoder-wifi-XXXXXX";
  if (!mkdtemp(directory)) { *error = "Cannot create private Wi-Fi socket directory"; return false; }
  local.directory = directory;
  local.path = local.directory + "/control";
  local.fd = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
  sockaddr_un address{};
  address.sun_family = AF_UNIX;
  std::memcpy(address.sun_path, local.path.c_str(), local.path.size() + 1);
  remote.sun_family = AF_UNIX;
  std::memcpy(remote.sun_path, control_socket.c_str(), control_socket.size() + 1);
  if (local.fd < 0 || bind(local.fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 ||
      connect(local.fd, reinterpret_cast<sockaddr*>(&remote), sizeof(remote)) != 0) {
    *error = "Wi-Fi control socket unavailable or permission denied";
    return false;
  }
  // Fixed read-only command. Never forward command text received from a client.
  constexpr char command[] = "SCAN_RESULTS";
  if (send(local.fd, command, sizeof(command) - 1, 0) != sizeof(command) - 1) {
    *error = "Cannot request cached Wi-Fi results"; return false;
  }
  pollfd waiting{local.fd, POLLIN, 0};
  if (poll(&waiting, 1, 2000) <= 0 || !(waiting.revents & POLLIN)) {
    *error = "Wi-Fi control response timed out"; return false;
  }
  char buffer[65536];
  const auto size = recv(local.fd, buffer, sizeof(buffer), MSG_TRUNC);
  if (size <= 0 || size >= static_cast<decltype(size)>(sizeof(buffer))) {
    *error = "Missing or oversized Wi-Fi response"; return false;
  }
  std::string result(buffer, static_cast<size_t>(size));
  if (result.rfind("bssid / frequency / signal level / flags / ssid\n", 0) != 0) {
    *error = "Invalid Wi-Fi scan results response"; return false;
  }
  *output = std::move(result);
  return true;
#else
  (void)control_socket;
  *error = "Wi-Fi results are supported only on Linux servers";
  return false;
#endif
}

}
