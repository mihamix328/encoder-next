#include "wifi_runtime_policy.h"
#include "netplan_files.h"
#include "encoder/wifi_config_draft.h"
#include <sys/stat.h>
#include <unistd.h>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace encoder;
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
struct Fixture {
  std::string root;
  Fixture() { char path[] = "/tmp/encoder-policy-XXXXXX"; check(mkdtemp(path), "private fixture"); root = path; }
  ~Fixture() { std::filesystem::remove_all(root); }
};
int main() {
  try {
    Fixture f; std::string error;
    const auto netplan = f.root + "/netplan", runtime = f.root + "/runtime";
    check(!mkdir(netplan.c_str(), 0700) && !mkdir(runtime.c_str(), 0700), "fixture directories");
    auto source = NetplanFiles::open(netplan, &error);
    auto target = NetplanFiles::open_supplicant(runtime, &error);
    check(source && target, "open separate policy scopes");
    check(!prepare_wifi_runtime_policy(netplan, runtime, false, &error), "missing profile refused");
    auto profile = WifiProfile::make("Fixture", std::string(64, 'a'), &error);
    auto draft = make_wifi_config_draft(*profile, &error);
    check(source->replace(draft->netplan_yaml, &error), "save source fixture");
    check(!source->replace(draft->supplicant_config, &error) && !target->replace(draft->netplan_yaml, &error), "formats cannot cross file scopes");
    check(prepare_wifi_runtime_policy(netplan, runtime, true, &error), "read-only check accepted");
    check(!std::filesystem::exists(runtime + "/wpa-wlan0.conf"), "check mode does not create policy");
    check(prepare_wifi_runtime_policy(netplan, runtime, false, &error), "runtime policy staged");
    bool exists; SecureBuffer bytes;
    check(target->backup(&exists, &bytes, &error) && exists && bytes.size() == draft->supplicant_config.size() &&
      !std::memcmp(bytes.data(), draft->supplicant_config.data(), bytes.size()), "exact strict policy persisted");
    struct stat info{}; check(!stat((runtime + "/wpa-wlan0.conf").c_str(), &info) && (info.st_mode & 0777) == 0600, "private runtime key file");
    check(!unlink((runtime + "/wpa-wlan0.conf").c_str()), "simulate volatile runtime file loss");
    check(prepare_wifi_runtime_policy(netplan, runtime, false, &error), "policy regenerated from persistent profile");
    auto next = WifiProfile::make("Changed fixture", std::string(64, 'b'), &error);
    auto next_draft = make_wifi_config_draft(*next, &error);
    check(source->replace(next_draft->netplan_yaml, &error) && prepare_wifi_runtime_policy(netplan, runtime, false, &error), "managed runtime policy updated");
    check(target->backup(&exists, &bytes, &error), "read updated runtime");
    auto decoded = read_wifi_supplicant_draft({reinterpret_cast<const char*>(bytes.data()), bytes.size()}, &error);
    check(decoded && decoded->ssid_hex() == next->ssid_hex(), "updated runtime target identity");
    { std::ofstream out(runtime + "/wpa-wlan0.conf"); out << "# encoder managed WPA2 draft v1\nnetwork={key_mgmt=NONE}\n"; }
    check(!prepare_wifi_runtime_policy(netplan, runtime, false, &error), "marker alone does not authorize replacing foreign policy");
    unlink((runtime + "/wpa-wlan0.conf").c_str());
    check(!symlink((netplan + "/90-encoder-wifi.yaml").c_str(), (runtime + "/wpa-wlan0.conf").c_str()), "symlink fixture");
    check(!prepare_wifi_runtime_policy(netplan, runtime, false, &error), "runtime symlink refused");
    unlink((runtime + "/wpa-wlan0.conf").c_str()); chmod(runtime.c_str(), 0755);
    check(!prepare_wifi_runtime_policy(netplan, runtime, false, &error), "public runtime directory refused");
    std::cout << "Strict runtime policy checks passed; no service or network changes\n";
    return 0;
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
