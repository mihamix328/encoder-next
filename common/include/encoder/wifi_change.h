#pragma once
#include "encoder/wifi_profile.h"
#include <chrono>
#include <string>
#include <string_view>
namespace encoder {
enum class WifiChangeState { Idle, Connecting, AwaitingConfirmation, Committed, RolledBack, RecoveryRequired };
enum class WifiLink { Pending, Ready, Failed };
// Adapter contract: methods are bounded and catch internal exceptions. prepare()
// must DURABLY save recovery state and arm an INDEPENDENT rollback watchdog before
// activate() can change anything. rollback() is idempotent, including after a
// partial prepare. commit() atomically persists the new config and disarms recovery.
// No real adapter is shipped yet: this core alone cannot survive process death.
class WifiChangeBackend {
 public:
  virtual ~WifiChangeBackend() = default;
  virtual bool ethernet_recovery_available() noexcept = 0;
  virtual bool prepare(const WifiProfile&, std::chrono::seconds lifetime) noexcept = 0;
  virtual bool activate() noexcept = 0;
  // Ready requires target identity + a usable IP, not association alone.
  virtual WifiLink probe() noexcept = 0;
  virtual bool commit() noexcept = 0;
  virtual bool rollback() noexcept = 0;
};
class WifiChange {
 public:
  using Clock = std::chrono::steady_clock;
  explicit WifiChange(WifiChangeBackend& backend) : backend_(backend) {}
  ~WifiChange();
  WifiChange(const WifiChange&) = delete;
  WifiChange& operator=(const WifiChange&) = delete;
  // Single-use transaction; only one owner/thread may call it. The privileged
  // adapter must serialize transaction ownership using durable journal state.
  // Hold its interprocess lock only around bounded operations, never throughout
  // the confirmation window: an independent watchdog must still acquire it.
  bool start(const WifiProfile&, Clock::time_point now = Clock::now());
  void tick(Clock::time_point now = Clock::now());
  bool confirm(std::string_view ticket, Clock::time_point now = Clock::now());
  void cancel();
  WifiChangeState state() const { return state_; }
  const std::string& ticket() const { return ticket_; } // bearer secret: never log
  const std::string& message() const { return message_; } // fixed, no credentials
 private:
  void revert(const char* reason);
  bool pending() const;
  WifiChangeBackend& backend_;
  WifiChangeState state_ = WifiChangeState::Idle;
  Clock::time_point deadline_{};
  std::string ticket_, message_;
};
}
