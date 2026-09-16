#include "network_status.h"
#include <cstring>
#include <cstdlib>
#ifdef __linux__
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <poll.h>
#endif

namespace encoder {
enum class WifiCommand { Results, Status, Scan };
static bool wifi_control_read(const std::string& control_socket, WifiCommand operation, std::string* output, std::string* error) {
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
    ~LocalSocket() {
      if (fd >= 0) close(fd);
    }
  } local;
  local.fd = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
  sockaddr_un address{};
  address.sun_family = AF_UNIX;
  remote.sun_family = AF_UNIX;
  std::memcpy(remote.sun_path, control_socket.c_str(), control_socket.size() + 1);
  // Linux autobind creates an abstract return address. A pathname in PrivateTmp
  // would be invisible to the external supplicant when it sends its reply.
  if (local.fd < 0 || bind(local.fd, reinterpret_cast<sockaddr*>(&address), sizeof(sa_family_t)) != 0 ||
      connect(local.fd, reinterpret_cast<sockaddr*>(&remote), sizeof(remote)) != 0) {
    *error = "Wi-Fi control socket unavailable or permission denied";
    return false;
  }
  // Fixed allowlist. Never forward command text received from a client.
  const std::string command = operation == WifiCommand::Status ? "STATUS" :
      operation == WifiCommand::Scan ? "SCAN" : "SCAN_RESULTS";
  if (send(local.fd, command.data(), command.size(), 0) != static_cast<ssize_t>(command.size())) {
    *error = "Cannot send Wi-Fi control request"; return false;
  }
  pollfd waiting{local.fd, POLLIN, 0};
  if (poll(&waiting, 1, 2000) <= 0 || !(waiting.revents & POLLIN)) {
    *error = "Wi-Fi control response timed out"; return false;
  }
  char buffer[65536];
  const auto size = recv(local.fd, buffer, sizeof(buffer), MSG_TRUNC);
  if (size <= 0 || static_cast<size_t>(size) >= sizeof(buffer)) {
    *error = "Missing or oversized Wi-Fi response"; return false;
  }
  std::string result(buffer, static_cast<size_t>(size));
  if (operation == WifiCommand::Results && result.rfind("bssid / frequency / signal level / flags / ssid\n", 0) != 0) {
    *error = "Invalid Wi-Fi scan results response"; return false;
  }
  *output = std::move(result);
  return true;
#else
  (void)control_socket;
  (void)operation;
  *error = "Wi-Fi results are supported only on Linux servers";
  return false;
#endif
}

bool wifi_cached_results(const std::string& path, std::string* output, std::string* error) {
  return wifi_control_read(path, WifiCommand::Results, output, error);
}
bool wifi_connection_status(const std::string& path, std::string* output, std::string* error) {
  return wifi_control_read(path, WifiCommand::Status, output, error);
}
bool wifi_request_scan(const std::string& path, std::string* error) {
  std::string response;
  if (!wifi_control_read(path, WifiCommand::Scan, &response, error)) return false;
  if (response == "OK\n") return true;
  *error = response == "FAIL-BUSY\n" ? "Wi-Fi scan is busy; retry later" : "Wi-Fi scan request rejected";
  return false;
}

}
