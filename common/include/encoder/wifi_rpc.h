#pragma once
#include "encoder/wifi_session.h"
#include <functional>
#include <memory>
namespace encoder {
// Local helper application protocol. No paths, shell commands or network address
// are accepted here. Outer IPC enforces peer UID; outer TLS enforces admin auth.
class WifiRpc {
 public:
  using Factory = std::function<std::unique_ptr<WifiChangeBackend>()>;
  explicit WifiRpc(Factory factory) : factory_(std::move(factory)) {}
  std::string handle(const std::string& packet);
  void tick();
 private:
  Factory factory_;
  std::unique_ptr<WifiChangeBackend> backend_;
  std::unique_ptr<WifiSession> session_;
};
}
