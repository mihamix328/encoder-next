#pragma once
#include "encoder/wifi_change.h"
#include <mutex>

namespace encoder {
// One session per privileged owner. Outer transport must authenticate its peer,
// enforce Ethernet recovery, and retain this object between requests. No wire API.
class WifiSession {
 public:
  explicit WifiSession(WifiChangeBackend& backend) : change_(backend) {}
  bool start(const WifiProfile& profile, SecureBuffer* ticket);
  bool status(std::string_view ticket, WifiChangeState* state);
  bool confirm(std::string_view ticket, WifiChangeState* state);
  bool cancel(std::string_view ticket, WifiChangeState* state);
  void tick();
  bool finished(); // Trusted owner only; never exposed as an unauthenticated RPC.
 private:
  bool authorized(std::string_view ticket) const;
  std::mutex mutex_;
  WifiChange change_;
  // Retained for terminal status queries; WifiChange wipes its own ticket on end.
  SecureBuffer ticket_;
};
}
