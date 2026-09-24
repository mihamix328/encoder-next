#include "wifi_netplan_preflight.h"
#include "wifi_managed_backend.h"
#include "encoder/wifi_config_draft.h"
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
  Fixture() { char name[] = "/tmp/encoder-preflight-process-XXXXXX"; check(mkdtemp(name), "private fixture"); root = name; }
  ~Fixture() { std::filesystem::remove_all(root); }
};
std::string read(const std::string& path) { std::ifstream in(path, std::ios::binary); return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()); }
void write(const std::string& path, const std::string& text) {
  std::ofstream out(path, std::ios::binary); out << text; out.close(); check(!chmod(path.c_str(), 0600), "private fixture file");
}
struct Platform final : WifiManagedPlatform {
  WifiProcessRunner& runner; std::string script, root, error; int applied = 0, restored = 0;
  Platform(WifiProcessRunner& r, std::string s, std::string p) : runner(r), script(std::move(s)), root(std::move(p)) {}
  bool ethernet_ready() noexcept override { return true; }
  bool preflight(const WifiProfile&) noexcept override { return wifi_netplan_preflight_at(runner, script, root, &error); }
  bool watchdog_ready(const std::string&) noexcept override { return true; }
  bool apply_target(const WifiProfile&) noexcept override { ++applied; return true; }
  WifiLink probe_target(const WifiProfile&) noexcept override { return WifiLink::Ready; }
  bool restore_network() noexcept override { ++restored; return true; }
};
int main(int argc, char** argv) {
  try {
    check(argc == 2, "expected trusted checker source path");
    Fixture f; WifiProcessRunner runner; std::string error;
    const auto config = f.root + "/etc/netplan";
    std::filesystem::create_directories(config);
    const auto ethernet = config + "/10-ethernet.yaml";
    const std::string original = "network:\n  version: 2\n  ethernets:\n    end1:\n      addresses: [172.10.0.2/24]\n";
    write(ethernet, original);
    check(wifi_netplan_preflight_at(runner, argv[1], f.root, &error), "real checker accepts isolated Ethernet fixture");
    auto profile = WifiProfile::make("Fixture", std::string(64, 'a'), &error);
    auto draft = make_wifi_config_draft(*profile, &error);
    const auto managed = config + "/90-encoder-wifi.yaml";
    const std::string yaml(reinterpret_cast<const char*>(draft->netplan_yaml.data()), draft->netplan_yaml.size());
    write(managed, yaml);
    check(wifi_netplan_preflight_at(runner, argv[1], f.root, &error), "real checker accepts generated managed profile");
    const auto conflict = config + "/30-old-wifi.yaml";
    const std::string old = "network:\n  version: 2\n  wifis:\n    wlan0:\n      access-points:\n        private-fixture-name:\n          password: private-fixture-password\n";
    write(conflict, old);
    check(!wifi_netplan_preflight_at(runner, argv[1], f.root, &error), "real checker refuses conflicting old Wi-Fi");
    check(error.find("private-fixture") == std::string::npos, "no credentials returned in diagnostic");
    check(read(ethernet) == original && read(managed) == yaml && read(conflict) == old, "scope check never changes input files");
    const auto state = f.root + "/state"; check(!mkdir(state.c_str(), 0700), "journal fixture directory");
    Platform platform(runner, argv[1], f.root);
    WifiManagedBackend backend(state, config, platform); WifiChange change(backend);
    check(!change.start(*profile) && platform.applied == 0 && platform.restored == 0,
      "real preflight blocks transaction before any network action");
    check(!std::filesystem::exists(state + "/journal"), "rejected scope creates no pending transaction");
    const auto alias = f.root + "/checker.py";
    check(!symlink(argv[1], alias.c_str()), "checker symlink fixture");
    check(!wifi_netplan_preflight_at(runner, alias, f.root, &error), "checker symlink refused");
    check(!wifi_netplan_preflight_at(runner, "relative.py", f.root, &error), "relative checker refused");
    check(!wifi_netplan_preflight_at(runner, argv[1], std::string("/tmp\0suffix", 11), &error), "embedded NUL refused");
    const auto unsafe = f.root + "/unsafe.py"; write(unsafe, "raise SystemExit(0)\n"); chmod(unsafe.c_str(), 0666);
    check(!wifi_netplan_preflight_at(runner, unsafe, f.root, &error), "writable checker refused before execution");
    std::cout << "Bounded real Netplan preflight passed; temporary fixtures only, no network changes\n";
    return 0;
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
