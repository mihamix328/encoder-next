#include "wifi_linux_platform.h"
#include <sys/stat.h>
#include <unistd.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
using namespace encoder;
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
struct Fixture {
  std::string root;
  Fixture() { char path[] = "/tmp/encoder-platform-XXXXXX"; check(mkdtemp(path), "private fixture"); root = path; }
  ~Fixture() { std::filesystem::remove_all(root); }
};
int main(int argc, char** argv) {
  try {
    check(argc == 2, "trusted template source required"); Fixture f;
    int calls = 0;
    auto executor = [&](const std::vector<std::string>& arguments, std::chrono::milliseconds timeout) {
      ++calls;
      check(arguments.size() == 3 && arguments[0] == "/usr/bin/systemctl" &&
        arguments[2] == "netplan-wpa-wlan0.service", "only fixed wlan0 service can be selected");
      check(arguments[1] == (calls == 1 ? "restart" : "stop"), "fixed service verb");
      check(timeout == std::chrono::seconds(20), "service action has bounded timeout");
      return WifiProcessResult{WifiProcessOutcome::Success, 0};
    };
    check(wifi_service_action_with(WifiServiceAction::Restart, executor), "restart maps correctly");
    check(wifi_service_action_with(WifiServiceAction::Stop, executor), "stop maps correctly");
    check(!wifi_service_action_with(static_cast<WifiServiceAction>(99), executor) && calls == 2, "unknown operation cannot execute");
    for (auto outcome : {WifiProcessOutcome::Failed, WifiProcessOutcome::TimedOut, WifiProcessOutcome::Busy})
      check(!wifi_service_action_with(WifiServiceAction::Restart, [=](const auto&, auto) { return WifiProcessResult{outcome, -1}; }), "system failure is not connection success");
    check(!wifi_service_action_with(WifiServiceAction::Restart, [](const auto&, auto) -> WifiProcessResult { throw std::runtime_error("fixture"); }), "executor exception contained");
    const auto installed = f.root + "/policy.conf";
    std::filesystem::copy_file(argv[1], installed); chmod(installed.c_str(), 0644);
    check(wifi_policy_override_matches(installed), "expected owned policy recognized");
    chmod(installed.c_str(), 0666); check(!wifi_policy_override_matches(installed), "writable policy refused"); chmod(installed.c_str(), 0644);
    const auto alias = f.root + "/alias"; check(!link(installed.c_str(), alias.c_str()), "hardlink fixture");
    check(!wifi_policy_override_matches(installed), "multiply-linked policy refused"); unlink(alias.c_str());
    check(!symlink(installed.c_str(), alias.c_str()), "symlink fixture"); check(!wifi_policy_override_matches(alias), "policy symlink refused");
    { std::ofstream out(installed, std::ios::app); out << "ExecStart=/bin/true\n"; }
    check(!wifi_policy_override_matches(installed), "modified policy refused");
    for (const auto* invalid : {"", "127.0.0.1", "0.0.0.0", "169.254.1.1", "224.1.1.1", "not-an-address"}) {
      WifiLinuxPlatform platform(invalid);
      check(!platform.ethernet_ready(), "invalid recovery address refused without interface checks");
    }
    std::cout << "Linux adapter mapping checks passed; no system commands or network changes\n";
    return 0;
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
