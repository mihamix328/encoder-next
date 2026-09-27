#include "encoder/wifi_session.h"
#include <openssl/crypto.h>
#include <cstring>
namespace encoder {
bool WifiSession::authorized(std::string_view ticket) const {
  return ticket_.size() == 64 && ticket.size() == ticket_.size() &&
      CRYPTO_memcmp(ticket.data(), ticket_.data(), ticket_.size()) == 0;
}
bool WifiSession::start(const WifiProfile& profile, SecureBuffer* ticket) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!ticket || change_.state() != WifiChangeState::Idle || ticket_.size()) return false;
  // Allocate before changing network state. Failed allocation cannot strand a start.
  SecureBuffer retained(64), outgoing(64);
  if (!change_.start(profile)) return false;
  const auto& value = change_.ticket();
  std::memcpy(retained.data(), value.data(), 64);
  std::memcpy(outgoing.data(), value.data(), 64);
  ticket_ = std::move(retained);
  *ticket = std::move(outgoing);
  return true;
}
bool WifiSession::status(std::string_view ticket, WifiChangeState* state) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!state || !authorized(ticket)) return false;
  change_.tick(); *state = change_.state(); return true;
}
bool WifiSession::confirm(std::string_view ticket, WifiChangeState* state) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!state || !authorized(ticket)) return false;
  if (change_.state() == WifiChangeState::Committed) { *state = change_.state(); return true; }
  const bool ok = change_.confirm(ticket);
  *state = change_.state(); return ok;
}
bool WifiSession::cancel(std::string_view ticket, WifiChangeState* state) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!state || !authorized(ticket)) return false;
  change_.cancel(); *state = change_.state(); return true;
}
void WifiSession::tick() {
  std::lock_guard<std::mutex> lock(mutex_);
  change_.tick();
}
}
