#include "encoder/wifi_config_draft.h"
#include <cstdlib>
#include <iostream>
#include <string_view>
#include <type_traits>
#include <utility>
using namespace encoder;
static_assert(!std::is_copy_constructible_v<WifiConfigDraft>);
static_assert(std::is_move_constructible_v<WifiConfigDraft>);
void check(bool ok, const char* message) { if (!ok) { std::cerr << message << '\n'; std::exit(1); } }
std::string_view view(const SecureBuffer& value) { return {reinterpret_cast<const char*>(value.data()), value.size()}; }
int main(int argc, char** argv) {
  std::string error;
  // Public fixture PSK, never a real credential. Fixture mode supports an
  // independent YAML-parser check without accepting user input or credentials.
  const std::string key(64, 'a');
  if (argc == 2 && std::string_view(argv[1]) == "--netplan-fixture") {
    auto p = WifiProfile::make("Encoder test", key, &error);
    auto d = make_wifi_config_draft(*p, &error);
    check(d.has_value(), "Netplan fixture generated");
    std::cout.write(reinterpret_cast<const char*>(d->netplan_yaml.data()), d->netplan_yaml.size());
    return 0;
  }
  const std::string name = " \"\\: #{}[]&*!|>@`'\xd0\x94\xf0\x9f\x8f\xa0 ";
  auto profile = WifiProfile::make(name, key, &error);
  check(profile.has_value(), "fixture profile accepted");
  auto draft = make_wifi_config_draft(*profile, &error);
  check(draft && error.empty(), "draft generated");
  if (argc == 2 && std::string_view(argv[1]) == "--yaml-fixture") {
    std::cout.write(reinterpret_cast<const char*>(draft->netplan_yaml.data()), draft->netplan_yaml.size());
    return 0;
  }
  if (argc == 2 && std::string_view(argv[1]) == "--wpa-fixture") {
    std::cout.write(reinterpret_cast<const char*>(draft->supplicant_config.data()), draft->supplicant_config.size());
    return 0;
  }
  check(argc == 1, "unsupported arguments");
  const auto yaml = view(draft->netplan_yaml), wpa = view(draft->supplicant_config);
  const std::string expected = "# encoder managed wifi v1\n"
      "# Draft only: requires verified WPA2 enforcement before installation.\n"
      "network:\n  version: 2\n  wifis:\n    wlan0:\n"
      "      renderer: networkd\n      dhcp4: true\n      access-points:\n"
      "        \" \\\"\\\\: #{}[]&*!|>@`'\xd0\x94\xf0\x9f\x8f\xa0 \":\n"
      "          auth:\n            key-management: psk\n            password: \"" + key + "\"\n";
  check(yaml == expected, "exact YAML including escaped metacharacters and Unicode");
  auto parsed = read_wifi_config_draft(yaml, &error);
  check(parsed && error.empty() && parsed->ssid_hex() == profile->ssid_hex(), "canonical draft round trip preserves exact SSID");
  for (size_t i = 0; i < 32; ++i) check(parsed->psk().data()[i] == profile->psk().data()[i], "canonical draft round trip preserves PSK");
  check(read_wifi_config_draft(yaml, nullptr).has_value(), "canonical reader supports null error output");
  for (size_t length = 0; length < yaml.size(); ++length)
    check(!read_wifi_config_draft(yaml.substr(0, length), &error), "every truncated canonical draft rejected");
  for (const auto& bad : {std::string(yaml) + "network: {}\n", std::string(yaml) + "# added comment\n",
                         std::string(4097, 'x'), std::string("network: {version: 2}\n")})
    check(!read_wifi_config_draft(bad, &error), "noncanonical or extended YAML rejected");
  for (const auto& pair : {std::pair<std::string, std::string>{"wlan0:", "end1:"}, {"dhcp4: true", "dhcp4: false"},
       {"key-management: psk", "key-management: none"}, {key, std::string(64, 'A')}, {"access-points:", "ethernets:"}}) {
    std::string bad(yaml); const auto at = bad.find(pair.first);
    check(at != std::string::npos, "mutation fixture found"); bad.replace(at, pair.first.size(), pair.second);
    check(!read_wifi_config_draft(bad, &error) && error.find(key) == std::string::npos, "changed scope or noncanonical value rejected without echoing secret");
  }
  check(wpa == "# encoder managed WPA2 draft v1\nctrl_interface=/run/wpa_supplicant\nupdate_config=0\nnetwork={\n  ssid=" +
      profile->ssid_hex() + "\n  proto=RSN\n  key_mgmt=WPA-PSK\n  pairwise=CCMP\n  group=CCMP\n  psk=" + key + "\n}\n",
      "exact strict WPA2 configuration, SSID and PSK are unquoted hex");
  auto derived = WifiProfile::make("IEEE", "password", &error);
  auto derived_draft = make_wifi_config_draft(*derived, &error);
  check(derived_draft.has_value(), "passphrase draft generated");
  check(view(derived_draft->supplicant_config).find("psk=f42c6fc52df0ebef9ebb4b90b38a5f902e83fe1b135a70e23aed762e9710a12e\n") != std::string_view::npos,
      "known derived PSK rendered correctly");
  check(view(derived_draft->supplicant_config).find("password") == std::string_view::npos, "input passphrase not retained");
  auto moved = std::move(*derived);
  check(!make_wifi_config_draft(*derived, &error) && !error.empty(), "consumed profile rejected");
  check(make_wifi_config_draft(moved, nullptr).has_value(), "null error output supported");
  check(!make_wifi_config_draft(*derived, nullptr), "consumed profile safe without error output");
  for (const auto& ssid : {std::string("true"), std::string("null"), std::string("0123"), std::string(32, 'x')}) {
    auto p = WifiProfile::make(ssid, key, &error);
    auto d = make_wifi_config_draft(*p, &error);
    check(d && view(d->netplan_yaml).find("        \"" + ssid + "\":\n") != std::string_view::npos,
        "scalar-like SSIDs always quoted");
    check(read_wifi_config_draft(view(d->netplan_yaml), &error).has_value(), "scalar-like and maximum SSIDs round trip");
  }
  std::cout << "Wi-Fi configuration serialization checks passed; no files or network changed\n";
}
