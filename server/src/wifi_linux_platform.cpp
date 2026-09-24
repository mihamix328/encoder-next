#include "wifi_linux_platform.h"
#include "wifi_policy_template.h"
#include "wifi_netplan_preflight.h"
#include "wifi_recovery_supervisor.h"
#include "wifi_target_probe.h"
#include "netplan_files.h"
#include "encoder/wifi_config_draft.h"
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <openssl/crypto.h>
#include <array>
#include <cstring>
namespace encoder {
bool wifi_service_action_with(WifiServiceAction action, const WifiCommandExecutor& execute) noexcept {
  try {
    if (!execute || (action != WifiServiceAction::Restart && action != WifiServiceAction::Stop)) return false;
    return execute({"/usr/bin/systemctl", action == WifiServiceAction::Restart ? "restart" : "stop",
        "netplan-wpa-wlan0.service"}, std::chrono::seconds(20)).outcome == WifiProcessOutcome::Success;
  } catch (...) { return false; }
}
bool wifi_policy_override_matches(const std::string& path) noexcept {
  if (path.empty() || path.front() != '/' || path.find('\0') != std::string::npos) return false;
  const int fd = open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
  if (fd < 0) return false;
  struct stat info{}; std::array<char, 4096> contents{};
  const bool metadata = !fstat(fd, &info) && S_ISREG(info.st_mode) && info.st_uid == geteuid() &&
    !(info.st_mode & 0022) && info.st_nlink == 1 && info.st_size == static_cast<off_t>(expected_wifi_policy_override.size());
  const auto size = metadata ? read(fd, contents.data(), contents.size()) : -1;
  close(fd);
  return metadata && size == static_cast<ssize_t>(expected_wifi_policy_override.size()) &&
    !std::memcmp(contents.data(), expected_wifi_policy_override.data(), expected_wifi_policy_override.size());
}
bool WifiLinuxPlatform::installation_ready() noexcept {
  return geteuid() == 0 && wifi_policy_override_matches("/etc/systemd/system/netplan-wpa-wlan0.service.d/90-encoder-policy.conf");
}
bool WifiLinuxPlatform::ethernet_ready() noexcept {
  in_addr expected{};
  if (recovery_address_.size() > 15 || recovery_address_.find('\0') != std::string::npos) return false;
  if (inet_pton(AF_INET, recovery_address_.c_str(), &expected) != 1) return false;
  const auto host = ntohl(expected.s_addr);
  if ((host >> 24) == 0 || (host >> 24) == 127 || (host >> 24) >= 224 || (host >> 16) == 0xa9fe) return false;
  ifaddrs* addresses = nullptr;
  if (getifaddrs(&addresses)) return false;
  bool ready = false;
  for (auto* item = addresses; item; item = item->ifa_next) {
    if (!item->ifa_addr || item->ifa_addr->sa_family != AF_INET || !item->ifa_name ||
        std::strcmp(item->ifa_name, "end1") || (item->ifa_flags & (IFF_UP | IFF_RUNNING)) != (IFF_UP | IFF_RUNNING)) continue;
    ready = reinterpret_cast<sockaddr_in*>(item->ifa_addr)->sin_addr.s_addr == expected.s_addr;
    if (ready) break;
  }
  freeifaddrs(addresses); return ready;
}
bool WifiLinuxPlatform::preflight(const WifiProfile& target) noexcept {
  std::string error;
  return target.psk().size() == 32 && installation_ready() && wifi_netplan_preflight(runner_, &error);
}
bool WifiLinuxPlatform::watchdog_ready(const std::string& state) noexcept {
  return state == state_directory && wifi_recovery_supervisor_ready(state_directory, netplan_directory);
}
bool WifiLinuxPlatform::service_action(WifiServiceAction action) noexcept {
  if (!installation_ready()) return false;
  return wifi_service_action_with(action, [&](const auto& args, auto timeout) { return runner_.run(args, timeout); });
}
bool WifiLinuxPlatform::apply_target(const WifiProfile& target) noexcept {
  try {
    // Coordinator has durably stored the candidate and holds the journal lock.
    std::string error; bool exists; SecureBuffer current;
    auto files = NetplanFiles::open(netplan_directory, &error);
    auto expected = make_wifi_config_draft(target, &error);
    if (!files || !expected || !files->backup(&exists, &current, &error) || !exists ||
        current.size() != expected->netplan_yaml.size() ||
        CRYPTO_memcmp(current.data(), expected->netplan_yaml.data(), current.size())) return false;
    return service_action(WifiServiceAction::Restart);
  } catch (...) { return false; }
}
WifiLink WifiLinuxPlatform::probe_target(const WifiProfile& target) noexcept {
  try { std::string error; return probe_wifi_target(target, &error); }
  catch (...) { return WifiLink::Failed; }
}
bool WifiLinuxPlatform::restore_network() noexcept {
  try {
    std::string error; bool exists; SecureBuffer restored;
    if (!installation_ready() || !wifi_netplan_preflight(runner_, &error)) return false;
    auto files = NetplanFiles::open(netplan_directory, &error);
    if (!files || !files->backup(&exists, &restored, &error)) return false;
    return service_action(exists ? WifiServiceAction::Restart : WifiServiceAction::Stop);
  } catch (...) { return false; }
}
}
