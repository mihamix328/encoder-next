#include "encoder/wifi_rpc.h"
#include <cstdlib>
#include <iostream>
using namespace encoder;
void check(bool ok) { if (!ok) { std::cerr << "RPC check failed\n"; std::exit(1); } }
struct Fake : WifiChangeBackend {
  bool ethernet_recovery_available() noexcept override { return true; }
  bool prepare(const WifiProfile&, std::chrono::seconds) noexcept override { return true; }
  bool activate() noexcept override { return true; }
  WifiLink probe() noexcept override { return WifiLink::Ready; }
  bool commit() noexcept override { return true; }
  bool rollback() noexcept override { return true; }
};
int main() {
  int created = 0;
  WifiRpc rpc([&] { ++created; return std::make_unique<Fake>(); });
  check(rpc.handle("start|00|" + std::string(64, 'a')) == "rejected");
  check(rpc.handle("start|41|" + std::string(64, 'A')) == "rejected");
  check(created == 0);
  const auto started = rpc.handle("start|486f6d65|" + std::string(64, 'a'));
  check(started.size() == 75 && started.substr(0,11) == "connecting|");
  const auto ticket = started.substr(11);
  check(rpc.handle("cancel|" + std::string(64, '0')) == "error");
  check(rpc.handle("status|" + ticket) == "ready");
  check(rpc.handle("start|41|" + std::string(64, 'a')) == "rejected");
  check(rpc.handle("confirm|" + ticket) == "committed");
  check(rpc.handle("confirm|" + ticket) == "committed");
  check(rpc.handle("cancel|" + ticket) == "committed");
  check(rpc.handle("start|41|" + std::string(64, 'a')).size() == 75);
  check(rpc.handle("confirm|" + ticket) == "error");
  check(created == 2);
  check(rpc.handle(std::string(1025, 'x')) == "error");
  check(rpc.handle("status|" + ticket + "|extra") == "error");
  std::cout << "Strict Wi-Fi RPC lifecycle passed\n";
}
