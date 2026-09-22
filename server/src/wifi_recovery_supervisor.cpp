#include "wifi_recovery_supervisor.h"
#include "wifi_recovery_worker.h"
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
namespace encoder {
namespace {
constexpr char socket_name[] = "watchdog.sock", lock_name[] = "watchdog.lock";
constexpr char query[] = "ENCODER-WATCHDOG-PING-1";
struct Fd {
  int value = -1;
  ~Fd() { if (value >= 0) close(value); }
  Fd() = default;
  explicit Fd(int fd) : value(fd) {}
  Fd(const Fd&) = delete;
  Fd& operator=(const Fd&) = delete;
};
bool directory(const std::string& path, bool private_mode, Fd& fd, struct stat& info) {
  if (path.empty() || path[0] != '/' || path.find('\0') != std::string::npos) return false;
  fd.value = open(path.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
  return fd.value >= 0 && !fstat(fd.value, &info) && info.st_uid == geteuid() &&
    !(info.st_mode & (private_mode ? 0077 : 0022));
}
bool address(const std::string& state, sockaddr_un& addr) {
  const auto path = state + "/" + socket_name;
  if (path.size() >= sizeof(addr.sun_path)) return false;
  addr.sun_family = AF_UNIX;
  std::memcpy(addr.sun_path, path.c_str(), path.size() + 1);
  return true;
}
std::string identity(const struct stat& state, const struct stat& netplan) {
  char value[160];
  const int length = std::snprintf(value, sizeof(value), "ENCODER-WATCHDOG-READY-1 %llu %llu %llu %llu",
    static_cast<unsigned long long>(state.st_dev), static_cast<unsigned long long>(state.st_ino),
    static_cast<unsigned long long>(netplan.st_dev), static_cast<unsigned long long>(netplan.st_ino));
  return length > 0 && static_cast<size_t>(length) < sizeof(value) ? std::string(value, length) : std::string{};
}
bool same_uid(int fd) {
  ucred peer{}; socklen_t length = sizeof(peer);
  return !getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &peer, &length) && length == sizeof(peer) && peer.uid == geteuid();
}
bool private_socket(const struct stat& info) {
  return S_ISSOCK(info.st_mode) && info.st_uid == geteuid() && !(info.st_mode & 0077) && info.st_nlink == 1;
}
struct SocketCleanup {
  int directory;
  struct stat owned{};
  bool active = false;
  ~SocketCleanup() {
    struct stat current{};
    if (active && !fstatat(directory, socket_name, &current, AT_SYMLINK_NOFOLLOW) &&
        current.st_dev == owned.st_dev && current.st_ino == owned.st_ino)
      unlinkat(directory, socket_name, 0);
  }
};
bool wait_for(int fd, short events, int timeout) {
  pollfd item{fd, events, 0};
  return poll(&item, 1, timeout) > 0 && (item.revents & events) && !(item.revents & (POLLERR | POLLNVAL));
}
}
bool run_wifi_recovery_supervisor(const std::string& state_path, const std::string& netplan_path,
    const std::function<bool()>& reconfigure, const std::function<bool()>& should_stop, std::string* error,
    const std::function<void(RecoveryOutcome)>& observe) {
  auto fail = [&](const char* message) { if (error) *error = message; return false; };
  try {
    if (!reconfigure || !should_stop) return fail("Recovery supervisor callbacks are required");
    Fd state, netplan; struct stat state_info{}, netplan_info{};
    if (!directory(state_path, true, state, state_info) || !directory(netplan_path, false, netplan, netplan_info))
      return fail("Unsafe recovery supervisor directories");
    Fd lock(openat(state.value, lock_name, O_RDWR | O_CREAT | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK, 0600));
    struct stat lock_info{};
    if (lock.value < 0 || fstat(lock.value, &lock_info) || !S_ISREG(lock_info.st_mode) ||
        lock_info.st_uid != geteuid() || (lock_info.st_mode & 0077) || lock_info.st_nlink != 1 ||
        flock(lock.value, LOCK_EX | LOCK_NB)) return fail("Recovery supervisor already active or lock unsafe");
    sockaddr_un addr{};
    if (!address(state_path, addr)) return fail("Recovery supervisor socket path too long");
    struct stat previous{};
    if (!fstatat(state.value, socket_name, &previous, AT_SYMLINK_NOFOLLOW)) {
      if (!private_socket(previous) || unlinkat(state.value, socket_name, 0))
        return fail("Unsafe recovery supervisor socket entry");
    } else if (errno != ENOENT) return fail("Cannot inspect recovery supervisor socket");
    Fd listener(socket(AF_UNIX, SOCK_SEQPACKET | SOCK_NONBLOCK | SOCK_CLOEXEC, 0));
    if (listener.value < 0 || bind(listener.value, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)))
      return fail("Cannot bind recovery supervisor socket");
    SocketCleanup cleanup{state.value};
    if (fstatat(state.value, socket_name, &cleanup.owned, AT_SYMLINK_NOFOLLOW))
      return fail("Cannot identify recovery supervisor socket");
    cleanup.active = true;
    if (fchmodat(state.value, socket_name, 0600, 0) || listen(listener.value, 8))
      return fail("Cannot protect recovery supervisor socket");
    const auto response = identity(state_info, netplan_info);
    if (response.empty()) return fail("Cannot identify recovery scope");
    auto next_tick = std::chrono::steady_clock::now();
    int last_outcome = -1;
    while (!should_stop()) {
      if (std::chrono::steady_clock::now() >= next_tick) {
        Fd current_state, current_netplan; struct stat checked_state{}, checked_netplan{};
        if (!directory(state_path, true, current_state, checked_state) ||
            !directory(netplan_path, false, current_netplan, checked_netplan) ||
            checked_state.st_dev != state_info.st_dev || checked_state.st_ino != state_info.st_ino ||
            checked_netplan.st_dev != netplan_info.st_dev || checked_netplan.st_ino != netplan_info.st_ino)
          return fail("Recovery supervisor directory identity changed");
        std::string recovery_error;
        auto outcome = RecoveryOutcome::Failed;
        try { outcome = wifi_managed_recovery_tick(state_path, netplan_path, reconfigure, &recovery_error); }
        catch (...) { /* Keep trying: never erase unresolved recovery evidence. */ }
        if (observe && static_cast<int>(outcome) != last_outcome) observe(outcome);
        last_outcome = static_cast<int>(outcome);
        next_tick = std::chrono::steady_clock::now() + std::chrono::milliseconds(250);
      }
      // Bounded client batch: a busy local client must not starve recovery ticks.
      pollfd incoming{listener.value, POLLIN, 0};
      const int polled = poll(&incoming, 1, 50);
      if (polled < 0 && errno != EINTR) return fail("Recovery supervisor polling failed");
      if (polled <= 0) continue;
      if (incoming.revents & (POLLERR | POLLHUP | POLLNVAL)) return fail("Recovery supervisor listener failed");
      for (int count = 0; count < 8; ++count) {
        Fd client(accept4(listener.value, nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC));
        if (client.value < 0) break;
        if (!same_uid(client.value) || !wait_for(client.value, POLLIN, 10)) continue;
        char request[sizeof(query)]{};
        const auto n = recv(client.value, request, sizeof(request), MSG_TRUNC);
        if (n != static_cast<ssize_t>(sizeof(query) - 1) || std::memcmp(request, query, sizeof(query) - 1)) continue;
        send(client.value, response.data(), response.size(), MSG_NOSIGNAL | MSG_DONTWAIT);
      }
    }
    return true;
  } catch (...) { return fail("Recovery supervisor failed"); }
}
bool wifi_recovery_supervisor_ready(const std::string& state_path, const std::string& netplan_path) noexcept {
  try {
    Fd state, netplan; struct stat state_info{}, netplan_info{}, socket_info{};
    if (!directory(state_path, true, state, state_info) || !directory(netplan_path, false, netplan, netplan_info) ||
        fstatat(state.value, socket_name, &socket_info, AT_SYMLINK_NOFOLLOW) || !private_socket(socket_info)) return false;
    sockaddr_un addr{}; if (!address(state_path, addr)) return false;
    Fd client(socket(AF_UNIX, SOCK_SEQPACKET | SOCK_NONBLOCK | SOCK_CLOEXEC, 0));
    if (client.value < 0 || connect(client.value, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) || !same_uid(client.value)) return false;
    if (send(client.value, query, sizeof(query) - 1, MSG_NOSIGNAL | MSG_DONTWAIT) != sizeof(query) - 1 ||
        !wait_for(client.value, POLLIN, 300)) return false;
    char reply[160]; const auto size = recv(client.value, reply, sizeof(reply), MSG_TRUNC);
    const auto expected = identity(state_info, netplan_info);
    return !expected.empty() && size == static_cast<ssize_t>(expected.size()) && !std::memcmp(reply, expected.data(), expected.size());
  } catch (...) { return false; }
}
}
