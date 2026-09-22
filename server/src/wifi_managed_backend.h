#pragma once
#include "encoder/wifi_change.h"
#include "wifi_journal.h"
namespace encoder {
// Trusted, bounded local operations. No production implementation is supplied.
// Callbacks run with the journal locked and MUST NOT try to acquire it themselves.
class WifiManagedPlatform {
 public:
  virtual ~WifiManagedPlatform() = default;
  virtual bool ethernet_ready() noexcept = 0;
  virtual bool preflight(const WifiProfile&) noexcept = 0;
  // Independent supervisor must watch this SAME directory before prepare succeeds.
  virtual bool watchdog_ready(const std::string& state_directory) noexcept = 0;
  // Must enforce persistent WPA2/CCMP, including reboot, and preserve Ethernet.
  virtual bool apply_target(const WifiProfile&) noexcept = 0;
  virtual WifiLink probe_target(const WifiProfile&) noexcept = 0;
  virtual bool restore_network() noexcept = 0;
};
// Linux-only experimental coordinator. Paths are trusted operator inputs, never
// client inputs. Single use/owner. Platform outlives backend; backend outlives
// WifiChange. Process death is handled by the independent supervisor, not a
// destructor. Confirmation cannot be resumed with a public transaction ID.
class WifiManagedBackend final : public WifiChangeBackend {
 public:
  WifiManagedBackend(std::string state_directory, std::string netplan_directory,
                     WifiManagedPlatform& platform);
  WifiManagedBackend(const WifiManagedBackend&) = delete;
  WifiManagedBackend& operator=(const WifiManagedBackend&) = delete;
  WifiManagedBackend(WifiManagedBackend&&) = delete;
  WifiManagedBackend& operator=(WifiManagedBackend&&) = delete;
  bool ethernet_recovery_available() noexcept override;
  bool prepare(const WifiProfile&, std::chrono::seconds) noexcept override;
  bool activate() noexcept override;
  WifiLink probe() noexcept override;
  bool commit() noexcept override;
  bool rollback() noexcept override;
  const std::string& last_error() const { return error_; }
 private:
  bool fail(const char* message) noexcept;
  bool current(const RecoveryRecord&) noexcept;
  void forget_target() noexcept;
  std::string state_directory_, netplan_directory_, transaction_, error_;
  WifiManagedPlatform& platform_;
  std::optional<WifiProfile> target_;
  SecureBuffer candidate_;
  bool attempted_ = false, prepared_ = false, activated_ = false;
  bool committed_ = false, restored_ = false;
};
}
