#include "wifi_change_ipc.h"
#include "wifi_linux_platform.h"
#include "encoder/wifi_rpc.h"
#include <sys/socket.h>
#include <sys/stat.h>
#include <poll.h>
#include <unistd.h>
#include <pwd.h>
#include <cstdlib>
#include <string>
// socket-activated persistent owner. Trusted root-controlled command line only.
int main(int argc, char** argv) {
  if (argc == 2 && std::string(argv[1]) == "--version") return 0;
  if (geteuid() != 0 || argc != 3 || std::string(argv[1]) != "--ethernet-address") return 2;
  const char* pid = getenv("LISTEN_PID"); const char* fds = getenv("LISTEN_FDS");
  if (!pid || std::string(pid) != std::to_string(getpid()) || !fds || std::string(fds) != "1") return 2;
  int type = 0, listening = 0; socklen_t size = sizeof(int);
  if (getsockopt(3, SOL_SOCKET, SO_TYPE, &type, &size) || type != SOCK_SEQPACKET ||
      getsockopt(3, SOL_SOCKET, SO_ACCEPTCONN, &listening, &size) || !listening) return 2;
  const auto* user = getpwnam("encoder"); if (!user || !user->pw_uid) return 2;
  const unsigned uid = user->pw_uid;
  encoder::WifiLinuxPlatform platform(argv[2]);
  if (!platform.ethernet_ready()) return 2;
  encoder::WifiRpc rpc([&] {
    return std::make_unique<encoder::WifiManagedBackend>(
        encoder::WifiLinuxPlatform::state_directory, encoder::WifiLinuxPlatform::netplan_directory, platform);
  });
  for (;;) {
    rpc.tick();
    pollfd listener{3, POLLIN, 0};
    if (poll(&listener, 1, 250) <= 0) continue;
    if (!(listener.revents & POLLIN)) return 1;
    const int fd = accept4(3, nullptr, nullptr, SOCK_CLOEXEC | SOCK_NONBLOCK);
    if (fd < 0) continue;
    encoder::serve_wifi_change_packet(fd, uid, [&](const std::string& packet) { return rpc.handle(packet); });
    close(fd);
  }
}
