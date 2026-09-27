#include "wifi_change_ipc.h"
#include <algorithm>
#ifdef __linux__
#include <sys/socket.h>
#include <sys/un.h>
#include <poll.h>
#include <unistd.h>
#include <cstring>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <net/if.h>
namespace {
bool peer(int fd, unsigned expected) {
  ucred credentials{}; socklen_t size = sizeof(credentials);
  return getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &credentials, &size) == 0 &&
      size == sizeof(credentials) && credentials.uid == expected;
}
bool read_packet(int fd, std::string* text, int timeout) {
  pollfd p{fd, POLLIN, 0};
  if (poll(&p, 1, timeout) <= 0 || !(p.revents & POLLIN)) return false;
  char buffer[1024];
  const auto n = recv(fd, buffer, sizeof(buffer), MSG_TRUNC | MSG_DONTWAIT);
  if (n <= 0 || n > static_cast<ssize_t>(sizeof(buffer))) return false;
  text->assign(buffer, static_cast<size_t>(n));
  return true;
}
}
#endif
namespace encoder {
bool wifi_change_recovery_connection(const Socket& socket) {
#ifdef __linux__
  sockaddr_in local{}, remote{}; socklen_t size = sizeof(local);
  if (getsockname(socket.native(), reinterpret_cast<sockaddr*>(&local), &size) || local.sin_family != AF_INET) return false;
  size = sizeof(remote);
  if (getpeername(socket.native(), reinterpret_cast<sockaddr*>(&remote), &size) || remote.sin_family != AF_INET) return false;
  ifaddrs* interfaces = nullptr;
  if (getifaddrs(&interfaces)) return false;
  bool ok = false;
  for (auto* p = interfaces; p; p = p->ifa_next) {
    if (!p->ifa_addr || !p->ifa_netmask || std::string(p->ifa_name) != "end1" ||
        p->ifa_addr->sa_family != AF_INET || !(p->ifa_flags & IFF_UP) || !(p->ifa_flags & IFF_RUNNING)) continue;
    const auto addr = reinterpret_cast<sockaddr_in*>(p->ifa_addr)->sin_addr.s_addr;
    const auto mask = reinterpret_cast<sockaddr_in*>(p->ifa_netmask)->sin_addr.s_addr;
    ok = addr == local.sin_addr.s_addr && mask != 0 &&
        (remote.sin_addr.s_addr & mask) == (addr & mask) && remote.sin_addr.s_addr != addr;
    if (ok) break;
  }
  freeifaddrs(interfaces); return ok;
#else
  (void)socket; return false;
#endif
}
bool request_wifi_change(const std::string& packet, std::string* response) {
  if (!response || packet.empty() || packet.size() > 1024) return false;
  response->clear();
#ifdef __linux__
  struct Fd { int value; ~Fd() { if (value >= 0) close(value); } };
  Fd fd{socket(AF_UNIX, SOCK_SEQPACKET | SOCK_NONBLOCK | SOCK_CLOEXEC, 0)};
  sockaddr_un addr{}; addr.sun_family = AF_UNIX;
  std::strcpy(addr.sun_path, "/run/encoder-wifi-change.sock");
  if (fd.value < 0 || connect(fd.value, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) ||
      !peer(fd.value, 0)) return false;
  if (send(fd.value, packet.data(), packet.size(), MSG_NOSIGNAL) != static_cast<ssize_t>(packet.size())) return false;
  return read_packet(fd.value, response, 30000);
#else
  return false;
#endif
}
#ifdef __linux__
bool serve_wifi_change_packet(int fd, unsigned allowed_uid,
    const std::function<std::string(const std::string&)>& handler) {
  if (!peer(fd, allowed_uid)) return false;
  std::string request;
  if (!read_packet(fd, &request, 1000)) return false;
  std::string response;
  try { response = handler(request); } catch (...) { response = "error"; }
  std::fill(request.begin(), request.end(), '\0');
  if (response.empty() || response.size() > 1024) return false;
  const bool ok = send(fd, response.data(), response.size(), MSG_NOSIGNAL | MSG_DONTWAIT) ==
      static_cast<ssize_t>(response.size());
  std::fill(response.begin(), response.end(), '\0');
  return ok;
}
#endif
}
