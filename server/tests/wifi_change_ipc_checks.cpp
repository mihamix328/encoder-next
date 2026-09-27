#include "wifi_change_ipc.h"
#include <sys/socket.h>
#include <unistd.h>
#include <thread>
#include <cstdlib>
#include <iostream>
void check(bool ok) { if (!ok) std::exit(1); }
int main() {
  for (bool authorized : {true, false}) {
    int fd[2]; check(socketpair(AF_UNIX, SOCK_SEQPACKET, 0, fd) == 0);
    bool called = false;
    std::thread worker([&] {
      encoder::serve_wifi_change_packet(fd[1], authorized ? getuid() : getuid() + 1,
          [&](const std::string& request) { called = true; check(request == "status|test"); return "ready"; });
      close(fd[1]);
    });
    if (authorized) {
      check(send(fd[0], "status|test", 11, 0) == 11);
      char result[32]; check(recv(fd[0], result, sizeof(result), 0) == 5);
    }
    worker.join(); close(fd[0]); check(called == authorized);
  }
  int fd[2]; check(socketpair(AF_UNIX, SOCK_SEQPACKET, 0, fd) == 0);
  const std::string oversized(1025, 'x'); check(send(fd[0], oversized.data(), oversized.size(), 0) == 1025);
  bool called = false;
  check(!encoder::serve_wifi_change_packet(fd[1], getuid(), [&](auto&) { called = true; return "ready"; }));
  check(!called); close(fd[0]); close(fd[1]);
  std::cout << "Wi-Fi IPC peer identity and size checks passed\n";
}
