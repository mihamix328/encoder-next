#include "encoder/net.h"
#include <iostream>
#include <cstdlib>
#include <chrono>
#if defined(_WIN32)
#include <winsock2.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#endif

void check(bool ok, const char* message) {
  if (!ok) { std::cerr << message << std::endl; std::exit(1); }
}
int main() {
  encoder::NetInit init;
  check(init.ok(), "network initialization");
  std::string error;
  auto listener = encoder::Socket::listen_on("127.0.0.1", 0, &error);
  check(listener.valid(), "ephemeral loopback listener");
  sockaddr_in address{};
#if defined(_WIN32)
  int length = sizeof(address);
#else
  socklen_t length = sizeof(address);
#endif
  check(getsockname(listener.native(), reinterpret_cast<sockaddr*>(&address), &length) == 0, "listener port");
  int port = ntohs(address.sin_port);
  encoder::Socket client;
  check(client.connect_to("127.0.0.1", port, &error, 1000), "nonblocking connect completes");
  auto peer = listener.accept(&error);
  check(peer.valid(), "accept loopback client");
  uint8_t sent = 42, received = 0;
  check(client.write(&sent, 1) == 1 && peer.read(&received, 1) == 1 && received == sent, "send after restoring blocking mode");
  check(peer.write(&sent, 1) == 1 && client.read(&received, 1) == 1 && received == sent, "receive after restoring blocking mode");
  check(!client.set_io_timeout(0), "zero timeout rejected");
  check(client.set_io_timeout(200), "set bounded I/O wait");
  const auto started = std::chrono::steady_clock::now();
  check(client.read(&received, 1) < 0, "idle peer read times out");
  check(std::chrono::steady_clock::now() - started < std::chrono::seconds(3), "read timeout duration");
  client.close(); peer.close(); listener.close();
  check(!client.connect_to("127.0.0.1", port, &error, 1000), "closed port rejected");
  check(!client.valid(), "failed connection does not retain a socket");
  check(!client.connect_to("127.0.0.1", 65536, &error), "invalid port rejected");
  std::cout << "Loopback connection, I/O, refusal and validation passed\n";
}
