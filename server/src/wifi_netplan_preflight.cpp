#include "wifi_netplan_preflight.h"
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
namespace encoder {
namespace {
bool fail(std::string* error, const char* message) noexcept {
  try { if (error) *error = message; } catch (...) {}
  return false;
}
bool absolute(const std::string& path) {
  return !path.empty() && path[0] == '/' && path.find('\0') == std::string::npos;
}
}
bool wifi_netplan_preflight_at(WifiProcessRunner& runner, const std::string& script,
    const std::string& root, std::string* error) noexcept {
  try {
    if (!absolute(script) || !absolute(root)) return fail(error, "Invalid trusted Netplan preflight paths");
    const int fd = open(script.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    if (fd < 0) return fail(error, "Netplan preflight checker is unavailable");
    struct stat info{};
    const bool safe = !fstat(fd, &info) && S_ISREG(info.st_mode) && info.st_uid == geteuid() &&
      !(info.st_mode & 0022) && info.st_nlink == 1 && info.st_size > 0 && info.st_size <= 65536;
    close(fd);
    if (!safe) return fail(error, "Netplan preflight checker ownership or permissions are unsafe");
    // No output is retained: Python/parser diagnostics can contain source lines.
    const auto result = runner.run({"/usr/bin/python3", "-I", "-B", script, "--root-dir", root}, std::chrono::seconds(5));
    if (result.outcome == WifiProcessOutcome::Success) { if (error) error->clear(); return true; }
    if (result.outcome == WifiProcessOutcome::TimedOut) return fail(error, "Netplan preflight timed out");
    if (result.outcome == WifiProcessOutcome::Busy) return fail(error, "Previous system check has not terminated");
    return fail(error, "Netplan scope check failed; configuration was not approved for staging");
  } catch (...) { return fail(error, "Netplan preflight could not run safely"); }
}
bool wifi_netplan_preflight(WifiProcessRunner& runner, std::string* error) noexcept {
  if (geteuid() != 0) return fail(error, "Installed Netplan preflight requires the privileged helper");
  return wifi_netplan_preflight_at(runner, "/opt/encoder/libexec/netplan_preflight.py", "/", error);
}
}
