#include "admin_server.h"
#include <utility>

namespace encoder {
AdminServer::AdminServer(std::string host, int port, AuditService* audit, Handler handler)
    : host_(std::move(host)), port_(port), audit_(audit), handler_(std::move(handler)) {}
AdminServer::~AdminServer() {
  running_ = false;
  if (thread_.joinable()) thread_.join();
}
void AdminServer::start() {
  if (running_) return;
  running_ = true;
  thread_ = std::thread([this]() { run(); });
}
void AdminServer::run() {
  NetInit init;
  if (!init.ok()) return;
  std::string error;
  auto server = Socket::listen_on(host_, port_, &error);
  if (!server.valid()) {
    running_ = false;
    if (audit_) audit_->log_event("admin_listen_error", "system", error);
    return;
  }
  while (running_) {
    if (!server.wait_readable(200)) continue;
    auto client = server.accept(&error);
    if (!running_) break;
    if (!client.valid()) continue;
    std::thread([handler = handler_, connection = std::move(client)]() mutable {
      handler(std::move(connection));
    }).detach();
  }
}
} // namespace encoder
