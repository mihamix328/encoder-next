"""Independent parser check. Uses public fixtures only; never installs a profile.

Requires PyYAML 6.x. Usage: python wifi_config_yaml_check.py <encoder-wifi-config-tests>
"""
import pathlib
import subprocess
import sys

import yaml


def main():
    executable = str(pathlib.Path(sys.argv[1]).resolve(strict=True))
    name = " \"\\: #{}[]&*!|>@`'\u0414\U0001f3e0 "
    key = "a" * 64
    output = subprocess.check_output([executable, "--yaml-fixture"], timeout=10).decode("utf-8")
    # Exact whole-tree equality also excludes global renderer, Ethernet, routes,
    # extra access points and injected configuration fields.
    expected = {"network": {"version": 2, "wifis": {"wlan0": {
        "renderer": "networkd", "dhcp4": True,
        "access-points": {name: {"auth": {"key-management": "psk", "password": key}}}
    }}}}
    if yaml.safe_load(output) != expected:
        raise RuntimeError("YAML round trip or exact configuration scope failed")
    # The supplicant format is checked separately, not treated as YAML.
    wpa = subprocess.check_output([executable, "--wpa-fixture"], timeout=10).decode("ascii")
    lines = [line.strip() for line in wpa.splitlines() if line and not line.startswith("#")]
    if lines != ["ctrl_interface=/run/wpa_supplicant", "update_config=0", "network={",
                 "ssid=" + name.encode("utf-8").hex(), "proto=RSN", "key_mgmt=WPA-PSK",
                 "pairwise=CCMP", "group=CCMP", "psk=" + key, "}"]:
        raise RuntimeError("Supplicant fixture scope or security policy failed")
    print("OK: independent YAML round trip, exact wlan0 scope and strict WPA2 fixture")
    print("No configuration installed; Netplan and live supplicant were NOT invoked")


if __name__ == "__main__":
    main()
