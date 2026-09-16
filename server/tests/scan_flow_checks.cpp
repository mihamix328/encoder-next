// Isolated fake sockets only; no root, real Wi-Fi, or installed services.
#include "network_status.h"
#include "scan_ipc.h"
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <thread>
#include <chrono>

void check(bool value, const char* message) {
  if (!value) { std::cerr << message << '\n'; std::exit(1); }
}
const std::string table = "bssid / frequency / signal level / flags / ssid\n"
    "aa:bb:cc:dd:ee:ff\t2412\t-50\t[WPA2]\tTest\n";
int main() {
  char directory[] = "/tmp/encoder-scan-flow-XXXXXX";
  check(mkdtemp(directory), "temporary directory");
  const std::string path = std::string(directory) + "/socket";
  for (int scenario = 0; scenario < 6; ++scenario) {
    const int fd = socket(AF_UNIX, SOCK_DGRAM, 0);
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    std::memcpy(address.sun_path, path.c_str(), path.size() + 1);
    check(!bind(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)), "fake supplicant bind");
    timeval timeout{2, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    bool valid = true;
    std::thread fake([&]() {
      sockaddr_un peer{};
      auto read = [&](const char* expected) {
        socklen_t size = sizeof(peer);
        char buffer[64];
        const auto n = recvfrom(fd, buffer, sizeof(buffer), 0, reinterpret_cast<sockaddr*>(&peer), &size);
        valid = valid && n > 0 && std::string(buffer, n > 0 ? n : 0) == expected;
        return size;
      };
      auto size = read("ATTACH");
      auto send_text = [&](const std::string& text) {
        sendto(fd, text.data(), text.size(), 0, reinterpret_cast<sockaddr*>(&peer), size);
      };
      send_text(scenario == 5 ? "FAIL\n" : "OK\n");
      if (scenario == 5) return;
      size = read("SCAN");
      if (scenario == 1) send_text("FAIL-BUSY\n");
      else {
        if (scenario == 3) send_text("<3>CTRL-EVENT-SCAN-RESULTS\n"); // Event before ACK.
        send_text("OK\n");
        send_text("<3>CTRL-EVENT-STATE-CHANGE id=0\n");
        if (scenario == 0) send_text("<3>CTRL-EVENT-SCAN-RESULTS\n");
        if (scenario == 2) send_text("<3>CTRL-EVENT-SCAN-FAILED ret=-1\n");
        // scenario 4: accepted, but never completed.
        if (scenario == 0 || scenario == 3) { size = read("SCAN_RESULTS"); send_text(table); }
      }
      read("DETACH");
    });
    std::string result, error;
    const bool ok = encoder::wifi_scan_and_wait(path, &result, &error, 500);
    fake.join();
    check(valid, "fixed scan command sequence");
    check(ok == (scenario == 0 || scenario == 3), "scan completion versus rejection/failure/timeout");
    if (ok) check(result == table, "fresh table returned");
    else check(result.empty() && !error.empty(), "no stale success on failure");
    close(fd); unlink(path.c_str());
  }
  for (const std::string request : {"SCAN\n", "SCAN\nEXTRA", "RECONFIGURE\n", "SCAN"}) {
    int pair[2];
    check(!socketpair(AF_UNIX, SOCK_STREAM, 0, pair), "IPC socket pair");
    int calls = 0;
    std::thread helper([&]() {
      encoder::serve_wifi_scan(pair[1], [&](std::string* out, std::string*) { ++calls; *out = table; return true; });
      close(pair[1]);
    });
    send(pair[0], request.data(), request.size(), MSG_NOSIGNAL);
    shutdown(pair[0], SHUT_WR);
    std::string response;
    char buffer[1024];
    for (ssize_t n; (n = recv(pair[0], buffer, sizeof(buffer), 0)) > 0;) response.append(buffer, n);
    close(pair[0]); helper.join();
    check(calls == (request == "SCAN\n" ? 1 : 0), "only exact framed request executes");
    check(response.rfind(request == "SCAN\n" ? "OK\n" : "ERROR\n", 0) == 0, "IPC result format");
  }
  for (int scenario = 0; scenario < 3; ++scenario) {
    const int listener = socket(AF_UNIX, SOCK_STREAM, 0);
    sockaddr_un address{}; address.sun_family = AF_UNIX;
    std::memcpy(address.sun_path, path.c_str(), path.size() + 1);
    check(!bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) && !listen(listener, 1), "IPC listener");
    std::thread helper([&]() {
      const int fd = accept(listener, nullptr, nullptr);
      encoder::serve_wifi_scan(fd, [&](std::string* out, std::string* error) {
        *out = scenario == 2 ? "broken" : table; *error = "Scan cooldown"; return scenario != 1;
      });
      close(fd);
    });
    std::string result, error;
    check(encoder::request_wifi_scan(path, &result, &error) == (scenario == 0), "server/helper result propagation");
    if (scenario == 0) check(result == table, "IPC table preserved");
    helper.join(); close(listener); unlink(path.c_str());
  }
  std::string result, error;
  check(!encoder::request_wifi_scan(path, &result, &error), "missing helper fails cleanly");
  rmdir(directory);
  std::cout << "Scan flow passed: completion, early event, busy, failure, timeout, attach rejection, IPC allowlist and result propagation\n";
}
