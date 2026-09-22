// Private Unix datagram fixture. No radio, system configuration or real socket.
#include "wifi_target_probe.h"
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <cstring>
#include <cstdlib>
#include <thread>
#include <iostream>
using namespace encoder;
void check(bool ok, const char* text) { if (!ok) { std::cerr << text << '\n'; std::exit(1); } }
int main() {
  char temporary[] = "/tmp/encoder-probe-XXXXXX";
  check(mkdtemp(temporary), "private fixture");
  const std::string path = std::string(temporary) + "/fake";
  const int fd = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);
  check(fd >= 0, "fixture socket");
  sockaddr_un address{}; address.sun_family = AF_UNIX;
  std::memcpy(address.sun_path, path.c_str(), path.size() + 1);
  check(!bind(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)), "bind fixture");
  timeval timeout{5, 0};
  check(!setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)), "bounded fixture");
  const std::string ready = "wpa_state=COMPLETED\nssid=Target\nkey_mgmt=WPA2-PSK\npairwise_cipher=CCMP\ngroup_cipher=CCMP\nip_address=10.0.0.59\n";
  bool commands_ok = true;
  std::thread fake([&]() {
    for (const auto& reply : {ready, ready, ready, ready, std::string("wpa_state=SCANNING\n"), std::string("FAIL\n")}) {
      char command[64]; sockaddr_un peer{}; socklen_t length = sizeof(peer);
      const auto size = recvfrom(fd, command, sizeof(command), 0, reinterpret_cast<sockaddr*>(&peer), &length);
      if (size != 6 || std::memcmp(command, "STATUS", 6)) { commands_ok = false; return; }
      if (sendto(fd, reply.data(), reply.size(), 0, reinterpret_cast<sockaddr*>(&peer), length) != static_cast<ssize_t>(reply.size())) {
        commands_ok = false; return;
      }
    }
  });
  std::string message;
  auto target = WifiProfile::make("Target", "password", &message);
  auto valid = [](std::vector<std::string>* ips, std::string*) { *ips = {"10.0.0.59"}; return true; };
  check(probe_wifi_target_with(*target, path, valid, &message) == WifiLink::Ready, "fresh STATUS plus interface IP is ready");
  check(probe_wifi_target_with(*target, path, [](auto* ips, auto*) { ips->clear(); return true; }, &message) == WifiLink::Pending, "missing interface IP waits");
  check(probe_wifi_target_with(*target, path, [](auto*, auto*) { return false; }, &message) == WifiLink::Failed, "address reader failure blocks confirmation");
  check(probe_wifi_target_with(*target, path, [](auto*, auto*) -> bool { throw 1; }, &message) == WifiLink::Failed, "reader exception contained");
  check(probe_wifi_target_with(*target, path, valid, &message) == WifiLink::Pending, "transitional live status waits");
  check(probe_wifi_target_with(*target, path, valid, &message) == WifiLink::Failed, "invalid live response fails");
  fake.join(); check(commands_ok, "only fixed STATUS commands sent");
  close(fd); unlink(path.c_str());
  bool called = false;
  check(probe_wifi_target_with(*target, path, [&](auto*, auto*) { called = true; return true; }, &message) == WifiLink::Failed && !called,
        "missing socket fails without consuming stale address data");
  check(!rmdir(temporary), "fixture removed");
  std::cout << "Fresh target probe passed with mock supplicant and interface reader\n";
}
