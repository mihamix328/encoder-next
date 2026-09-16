#include "network_status.h"
#include <sys/socket.h>
#include <sys/un.h>
#include <poll.h>
#include <unistd.h>
#include <chrono>
#include <cstring>
#include <cerrno>

namespace encoder {
bool wifi_scan_and_wait(const std::string& path, std::string* output, std::string* error, int timeout_ms) {
  output->clear();
  sockaddr_un remote{}, local{};
  remote.sun_family = local.sun_family = AF_UNIX;
  if (path.empty() || path.front() != '/' || path.size() >= sizeof(remote.sun_path) || path.find('\0') != std::string::npos) {
    *error = "Invalid Wi-Fi control socket path"; return false;
  }
  std::memcpy(remote.sun_path, path.c_str(), path.size() + 1);
  struct Socket {
    int fd; bool attached = false;
    ~Socket() {
      if (fd >= 0) {
        if (attached) send(fd, "DETACH", 6, MSG_DONTWAIT | MSG_NOSIGNAL);
        close(fd);
      }
    }
  } socket{::socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0)};
  if (socket.fd < 0 || bind(socket.fd, reinterpret_cast<sockaddr*>(&local), sizeof(sa_family_t)) ||
      connect(socket.fd, reinterpret_cast<sockaddr*>(&remote), sizeof(remote))) {
    *error = "Wi-Fi control socket unavailable or permission denied"; return false;
  }
  using Clock = std::chrono::steady_clock;
  const auto deadline = Clock::now() + std::chrono::milliseconds(timeout_ms);
  unsigned messages = 0;
  bool scan_sent = false, completed = false, failed = false;
  auto receive = [&](std::string* text) {
    while (Clock::now() < deadline && messages < 512) {
      const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()).count();
      pollfd p{socket.fd, POLLIN, 0};
      const int rc = poll(&p, 1, static_cast<int>(ms + 1));
      if (rc < 0 && errno == EINTR) continue;
      if (rc <= 0 || !(p.revents & POLLIN)) break;
      char buffer[65536];
      const auto n = recv(socket.fd, buffer, sizeof(buffer), MSG_TRUNC);
      if (n < 0 && (errno == EAGAIN || errno == EINTR)) continue;
      if (n <= 0 || static_cast<size_t>(n) >= sizeof(buffer)) break;
      ++messages;
      text->assign(buffer, static_cast<size_t>(n)); return true;
    }
    *error = "Wi-Fi scan timed out or event stream invalid; completion unknown"; return false;
  };
  auto event = [&](const std::string& text) {
    if (text.size() < 3 || text[0] != '<') return false;
    const auto end = text.find('>');
    if (end == std::string::npos) return false;
    const auto value = text.substr(end + 1);
    auto matches = [&](const char* name) {
      const size_t length = std::strlen(name);
      return value.compare(0, length, name) == 0 &&
          (value.size() == length || value[length] == ' ' || value[length] == '\n');
    };
    if (scan_sent && matches("CTRL-EVENT-SCAN-RESULTS")) completed = true;
    if (scan_sent && matches("CTRL-EVENT-SCAN-FAILED")) failed = true;
    return true;
  };
  auto command = [&](const char* text, std::string* reply) {
    const auto length = std::strlen(text);
    if (send(socket.fd, text, length, MSG_NOSIGNAL) != static_cast<ssize_t>(length)) {
      *error = "Cannot send Wi-Fi scan command"; return false;
    }
    while (receive(reply)) if (!event(*reply)) return true;
    return false;
  };
  std::string reply;
  if (!command("ATTACH", &reply)) return false;
  if (reply != "OK\n") { *error = "Cannot monitor Wi-Fi scan completion"; return false; }
  socket.attached = true;
  scan_sent = true;
  if (!command("SCAN", &reply)) return false;
  if (reply != "OK\n") { *error = "Wi-Fi scan rejected or busy; retry later"; return false; }
  while (!completed && !failed) {
    if (!receive(&reply)) return false;
    if (!event(reply)) { *error = "Unexpected Wi-Fi scan response"; return false; }
  }
  if (failed) { *error = "Wi-Fi scan failed"; return false; }
  if (!command("SCAN_RESULTS", &reply)) return false;
  if (reply.rfind("bssid / frequency / signal level / flags / ssid\n", 0) != 0) {
    *error = "Invalid Wi-Fi scan results"; return false;
  }
  *output = std::move(reply); return true;
}
}
