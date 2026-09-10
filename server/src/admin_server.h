#pragma once

#include "audit.h"
#include "encoder/net.h"
#include <atomic>
#include <functional>
#include <string>
#include <thread>

namespace encoder {
// Dedicated admin listener; protocol handling is shared with the main server.
class AdminServer {
 public:
  using Handler = std::function<void(Socket)>;
  AdminServer(std::string host, int port, AuditService* audit, Handler handler);
  ~AdminServer();
  void start();
 private:
  void run();
  std::string host_;
  int port_;
  AuditService* audit_;
  Handler handler_;
  std::thread thread_;
  std::atomic<bool> running_{false};
};
} // namespace encoder
