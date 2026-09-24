#pragma once
#include "wifi_managed_backend.h"
#include "wifi_process.h"
#include <functional>
#include <utility>
namespace encoder {
enum class WifiServiceAction { Restart, Stop };
using WifiCommandExecutor = std::function<WifiProcessResult(const std::vector<std::string>&, std::chrono::milliseconds)>;
// Trusted adapter seam, not an RPC. The only selectable unit is wlan0's service.
bool wifi_service_action_with(WifiServiceAction action, const WifiCommandExecutor& execute) noexcept;
bool wifi_policy_override_matches(const std::string& trusted_path) noexcept;
// Concrete Linux helper adapter. NOT wired into TLS/API or installed by default.
// Deployment must first validate effective systemd units (including OTHER
// overrides), networkd DHCP on wlan0, migrated Netplan and independent supervisor.
// Caller supplies a trusted Ethernet recovery address, NOT client-supplied data;
// authenticating/confirming that the admin uses this recovery path is separate.
class WifiLinuxPlatform final : public WifiManagedPlatform {
 public:
  // Separate from the non-root encryption server's writable /var/lib/encoder.
  static constexpr const char* state_directory = "/var/lib/encoder-wifi-recovery";
  static constexpr const char* netplan_directory = "/etc/netplan";
  explicit WifiLinuxPlatform(std::string recovery_address) : recovery_address_(std::move(recovery_address)) {}
  bool ethernet_ready() noexcept override;
  bool preflight(const WifiProfile&) noexcept override;
  bool watchdog_ready(const std::string&) noexcept override;
  bool apply_target(const WifiProfile&) noexcept override;
  WifiLink probe_target(const WifiProfile&) noexcept override;
  bool restore_network() noexcept override;
 private:
  bool installation_ready() noexcept;
  bool service_action(WifiServiceAction) noexcept;
  std::string recovery_address_;
  WifiProcessRunner runner_;
};
}
