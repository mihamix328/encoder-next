#include "scan_ipc.h"
#ifdef __linux__
#include <sys/socket.h>
#include <sys/un.h>
#include <poll.h>
#include <unistd.h>
#include <chrono>
#include <cstring>
#include <cerrno>
namespace {
using Clock = std::chrono::steady_clock;
bool wait_io(int fd, short events, Clock::time_point deadline) {
  while (Clock::now() < deadline) {
    const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()).count();
    pollfd p{fd, events, 0};
    const int rc = poll(&p, 1, static_cast<int>(remaining + 1));
    if (rc < 0 && errno == EINTR) continue;
    return rc > 0 && (p.revents & (events | POLLHUP));
  }
  return false;
}
bool receive(int fd, std::string* out, size_t limit, Clock::time_point deadline) {
  out->clear();
  char buffer[4096];
  while (wait_io(fd, POLLIN, deadline)) {
    const auto n = recv(fd, buffer, sizeof(buffer), MSG_DONTWAIT);
    if (n == 0) return true;
    if (n < 0) { if (errno == EINTR || errno == EAGAIN) continue; return false; }
    if (out->size() + static_cast<size_t>(n) > limit) return false;
    out->append(buffer, static_cast<size_t>(n));
  }
  return false;
}
bool transmit(int fd, const std::string& text, Clock::time_point deadline) {
  size_t sent = 0;
  while (sent < text.size() && wait_io(fd, POLLOUT, deadline)) {
    const auto n = send(fd, text.data() + sent, text.size() - sent, MSG_NOSIGNAL | MSG_DONTWAIT);
    if (n < 0 && (errno == EINTR || errno == EAGAIN)) continue;
    if (n <= 0) return false;
    sent += static_cast<size_t>(n);
  }
  return sent == text.size();
}
}
#endif
namespace encoder {
bool request_wifi_scan(const std::string& path, std::string* output, std::string* error) {
  output->clear();
#ifdef __linux__
  sockaddr_un address{};
  address.sun_family = AF_UNIX;
  if (path.empty() || path.front() != '/' || path.size() >= sizeof(address.sun_path) || path.find('\0') != std::string::npos) {
    *error = "Invalid scan helper socket path"; return false;
  }
  std::memcpy(address.sun_path, path.c_str(), path.size() + 1);
  struct Fd { int value; ~Fd() { if (value >= 0) close(value); } };
  Fd fd{socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0)};
  // UNIX nonblocking connect fails immediately if the bounded listener queue is full.
  if (fd.value < 0 || connect(fd.value, reinterpret_cast<sockaddr*>(&address), sizeof(address))) {
    *error = "Scan helper unavailable or busy"; return false;
  }
  const auto deadline = Clock::now() + std::chrono::seconds(28);
  std::string response;
  if (!transmit(fd.value, "SCAN\n", deadline) || shutdown(fd.value, SHUT_WR) ||
      !receive(fd.value, &response, 65536, deadline)) {
    *error = "Scan helper timed out or returned an invalid response; completion unknown"; return false;
  }
  if (response.rfind("OK\nbssid / frequency / signal level / flags / ssid\n", 0) == 0) {
    *output = response.substr(3); return true;
  }
  *error = response.rfind("ERROR\n", 0) == 0 ? response.substr(6, 256) : "Invalid scan helper response";
  return false;
#else
  (void)path;
  *error = "Wi-Fi scanning is supported only on Linux servers"; return false;
#endif
}
#ifdef __linux__
bool serve_wifi_scan(int fd, const ScanAction& action) {
  std::string request, results, error;
  // EOF is mandatory: SCAN plus any extra bytes is rejected before executing anything.
  if (!receive(fd, &request, 8, Clock::now() + std::chrono::seconds(2)) || request != "SCAN\n")
    return transmit(fd, "ERROR\nInvalid scan request", Clock::now() + std::chrono::seconds(1));
  const bool ok = action(&results, &error);
  if (ok && (results.size() > 65500 || results.rfind("bssid / frequency / signal level / flags / ssid\n", 0) != 0))
    return transmit(fd, "ERROR\nInvalid scan results", Clock::now() + std::chrono::seconds(1));
  return transmit(fd, ok ? "OK\n" + results : "ERROR\n" + error.substr(0, 256),
                  Clock::now() + std::chrono::seconds(2));
}
#endif
}
