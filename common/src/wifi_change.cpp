#include "encoder/wifi_change.h"
#include <openssl/crypto.h>
#include <openssl/rand.h>
namespace encoder {
bool WifiChange::pending() const {
  return state_ == WifiChangeState::Connecting || state_ == WifiChangeState::AwaitingConfirmation;
}
WifiChange::~WifiChange() {
  if (pending()) revert("Connection owner closed");
  if (!ticket_.empty()) OPENSSL_cleanse(ticket_.data(), ticket_.size());
}
void WifiChange::revert(const char* reason) {
  state_ = backend_.rollback() ? WifiChangeState::RolledBack : WifiChangeState::RecoveryRequired;
  message_ = state_ == WifiChangeState::RolledBack ? reason : "Rollback failed; recovery required";
  if (!ticket_.empty()) OPENSSL_cleanse(ticket_.data(), ticket_.size());
  ticket_.clear();
}
bool WifiChange::start(const WifiProfile& profile, Clock::time_point now) {
  if (state_ != WifiChangeState::Idle) return false;
  if (profile.psk().size() != 32 || profile.ssid_hex().empty()) {
    message_ = "Invalid or consumed Wi-Fi profile"; return false;
  }
  if (!backend_.ethernet_recovery_available()) { message_ = "Ethernet recovery connection required"; return false; }
  unsigned char random[32];
  if (RAND_bytes(random, sizeof(random)) != 1) {
    OPENSSL_cleanse(random, sizeof(random)); message_ = "Cannot create confirmation ticket"; return false;
  }
  ticket_.reserve(64);
  constexpr char hex[] = "0123456789abcdef";
  for (unsigned char c : random) { ticket_ += hex[c >> 4]; ticket_ += hex[c & 15]; }
  OPENSSL_cleanse(random, sizeof(random));
  deadline_ = now + std::chrono::seconds(90);
  state_ = WifiChangeState::Connecting;
  if (!backend_.prepare(profile, std::chrono::seconds(90))) { revert("Recovery preparation failed"); return false; }
  if (!backend_.activate()) { revert("Connection activation failed"); return false; }
  message_ = "Connecting; explicit confirmation required within 90 seconds";
  return true;
}
void WifiChange::tick(Clock::time_point now) {
  if (!pending()) return;
  if (now >= deadline_) { revert("Confirmation timed out"); return; }
  if (!backend_.ethernet_recovery_available()) { revert("Ethernet recovery connection lost"); return; }
  const auto link = backend_.probe();
  if (link == WifiLink::Failed) { revert("Target connection failed"); return; }
  state_ = link == WifiLink::Ready ? WifiChangeState::AwaitingConfirmation : WifiChangeState::Connecting;
  message_ = link == WifiLink::Ready ? "Target connected; awaiting confirmation" : "Connecting";
}
bool WifiChange::confirm(std::string_view ticket, Clock::time_point now) {
  tick(now);
  if (state_ != WifiChangeState::AwaitingConfirmation || ticket.size() != ticket_.size() ||
      CRYPTO_memcmp(ticket.data(), ticket_.data(), ticket_.size()) != 0) return false;
  if (!backend_.commit()) { revert("Cannot persist target connection"); return false; }
  state_ = WifiChangeState::Committed;
  message_ = "Connection confirmed";
  OPENSSL_cleanse(ticket_.data(), ticket_.size()); ticket_.clear();
  return true;
}
void WifiChange::cancel() { if (pending()) revert("Connection cancelled"); }
}
