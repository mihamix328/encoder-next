#include "encoder/wifi_link_probe.h"
#include <cstdlib>
#include <iostream>
using namespace encoder;
void check(bool ok, const char* text) { if (!ok) { std::cerr << text << '\n'; std::exit(1); } }
std::string status(std::string ssid = "Target", std::string ip = "10.0.0.59", std::string key = "WPA2-PSK",
                   std::string pairwise = "CCMP", std::string group = "CCMP") {
  return "bssid=00:11:22:33:44:55\nssid=" + ssid + "\nwpa_state=COMPLETED\nkey_mgmt=" + key +
      "\npairwise_cipher=" + pairwise + "\ngroup_cipher=" + group + "\nip_address=" + ip + "\n";
}
int main() {
  std::string message;
  auto target = WifiProfile::make("Target", "password", &message);
  const std::vector<std::string> addresses{"10.0.0.59"};
  check(assess_wifi_link(*target, status(), addresses, &message) == WifiLink::Ready, "matching authenticated target with local IP is ready");
  check(assess_wifi_link(*target, status("T\\x61rget"), addresses, nullptr) == WifiLink::Ready, "SSID escapes decoded before comparison");
  check(assess_wifi_link(*target, status("Other"), addresses, &message) == WifiLink::Pending, "old network cannot confirm target");
  check(assess_wifi_link(*target, status(), {}, &message) == WifiLink::Pending, "supplicant IP without interface address not ready");
  check(assess_wifi_link(*target, status(), {"172.10.0.2"}, &message) == WifiLink::Pending, "different interface address cannot confirm");
  for (const auto& key : {"WPA-PSK", "NONE", "SAE", "WPA2-EAP", ""})
    check(assess_wifi_link(*target, status("Target", "10.0.0.59", key), addresses, &message) == WifiLink::Failed, "other security mode rejected");
  check(assess_wifi_link(*target, status("Target", "10.0.0.59", "WPA2-PSK", "TKIP"), addresses, &message) == WifiLink::Failed, "weak pairwise cipher rejected");
  check(assess_wifi_link(*target, status("Target", "10.0.0.59", "WPA2-PSK", "CCMP", "TKIP"), addresses, &message) == WifiLink::Failed, "weak group cipher rejected");
  for (const auto& ip : {"", "0.0.0.0", "0.1.2.3", "127.0.0.1", "169.254.1.2", "224.0.0.1", "255.255.255.255", "240.0.0.1",
                         "10.0.0.256", "10.0.0", "10.0.0.1.2", "010.0.0.1", "10.0.0.1 ", "::1", "123456789.0.0.1"})
    check(assess_wifi_link(*target, status("Target", ip), {ip}, &message) == WifiLink::Pending, "unusable or malformed IP never ready");
  for (const auto& state : {"DISCONNECTED", "INACTIVE", "SCANNING", "AUTHENTICATING", "ASSOCIATING", "ASSOCIATED", "4WAY_HANDSHAKE", "GROUP_HANDSHAKE"})
    check(assess_wifi_link(*target, std::string("wpa_state=") + state + "\n", addresses, &message) == WifiLink::Pending, "transitional state waits");
  for (const auto& raw : {std::string(), std::string(8193, 'x'), std::string("wpa_state=COMPLETED"),
       std::string("wpa_state=UNKNOWN\n"), std::string("wpa_state=INTERFACE_DISABLED\n"),
       status() + "ssid=Target\n", status() + "key_mgmt=WPA2-PSK\n", status() + "extra=bad\r\n",
       status() + std::string("extra=x\0y\n", 10), status("bad\\"), status("bad\\q"), status("\\xgg"), status(std::string(33, 'a'))})
    check(assess_wifi_link(*target, raw, addresses, &message) == WifiLink::Failed, "ambiguous or malformed status rejected");
  auto unicode = WifiProfile::make(" \"\\\xd0\x94\xf0\x9f\x8f\xa0 ", "password", &message);
  check(assess_wifi_link(*unicode, status(" \\\"\\\\\\xd0\\x94\\xf0\\x9f\\x8f\\xa0 "), addresses, &message) == WifiLink::Ready,
        "escaped quotes, slash and Unicode match exact target bytes");
  auto moved = std::move(*target);
  check(assess_wifi_link(*target, status(), addresses, &message) == WifiLink::Failed, "consumed target rejected");
  check(assess_wifi_link(moved, status() + "future_field=value\n", addresses, &message) == WifiLink::Ready, "unknown well-formed extension ignored");
  std::cout << "Wi-Fi readiness checks passed; no network accessed\n";
}
