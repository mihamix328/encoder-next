"""Exercise installed Netplan in an isolated root, never apply real networking.

Only public fixture credentials are used. No /etc/netplan reads or changes,
no root required, no reload/restart/apply/try commands.
"""
import argparse
import pathlib
import shutil
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--fixtures", required=True)
    parser.add_argument("--netplan", default="netplan")
    args = parser.parse_args()
    fixtures = str(pathlib.Path(args.fixtures).resolve(strict=True))
    netplan = shutil.which(args.netplan)
    if not netplan:
        raise RuntimeError("Netplan executable not found")

    with tempfile.TemporaryDirectory(prefix="encoder-netplan-generate-") as temporary:
        root = pathlib.Path(temporary)
        config = root / "etc" / "netplan"
        config.mkdir(parents=True, mode=0o700)

        def write_fixture(name, data):
            target = config / name
            target.touch(mode=0o600, exist_ok=False)
            target.write_bytes(data)

        def generate():
            result = subprocess.run([netplan, "generate", "--root-dir", str(root)],
                                    capture_output=True, timeout=30, check=False)
            if result.returncode:
                # Even with public fixture secrets, avoid adopting a habit of
                # putting tool output (which may echo credentials) in logs.
                raise RuntimeError("Isolated Netplan generation failed")

        write_fixture("10-ethernet.yaml", b"network:\n  version: 2\n  ethernets:\n    end1:\n      renderer: networkd\n      addresses: [172.10.0.2/24]\n")
        generate()
        ethernet = root / "run/systemd/network/10-netplan-end1.network"
        baseline = ethernet.read_bytes()

        draft = subprocess.check_output([fixtures, "--netplan-fixture"], timeout=10)
        write_fixture("90-encoder-wifi.yaml", draft)
        generate()
        if ethernet.read_bytes() != baseline:
            raise RuntimeError("Wi-Fi draft changed the Ethernet fixture output")
        wpa_path = root / "run/netplan/wpa-wlan0.conf"
        wpa = wpa_path.read_text(encoding="utf-8")
        lines = [line.strip() for line in wpa.splitlines()]
        if lines.count("network={") != 1:
            raise RuntimeError("Expected exactly one generated access point")
        if not any(line in ('ssid="Encoder test"', 'ssid=P"Encoder test"') for line in lines):
            raise RuntimeError("Netplan did not preserve the fixture SSID")
        if "psk=" + "a" * 64 not in lines or "key_mgmt=WPA-PSK" not in lines:
            raise RuntimeError("Unexpected generated authentication configuration")
        if wpa_path.stat().st_mode & 0o077:
            raise RuntimeError("Generated key file is accessible to other users")

        write_fixture(".hidden-old.yaml", b"network:\n  version: 2\n  wifis:\n    wlan0:\n      access-points:\n        \"Hidden fixture\":\n          password: \"fixture-only-password\"\n")
        generate()
        if wpa_path.read_text(encoding="utf-8").count("network={") != 1:
            raise RuntimeError("Netplan reads hidden YAML files; update the scope guard")

        # Prove why dropping another YAML file beside an old Wi-Fi definition
        # is insufficient: Netplan merges access points instead of replacing.
        write_fixture("30-old-wifi.yaml", b"network:\n  version: 2\n  wifis:\n    wlan0:\n      access-points:\n        \"Old network\":\n          password: \"fixture-only-password\"\n")
        generate()
        conflict = wpa_path.read_text(encoding="utf-8")
        if conflict.count("network={") != 2:
            raise RuntimeError("Netplan merge behavior changed; review conflict handling")
        if ethernet.read_bytes() != baseline:
            raise RuntimeError("Conflicting Wi-Fi fixture changed Ethernet")
        print("OK: isolated Netplan accepts draft, preserves Ethernet and protects generated key")
        print("OK: existing wlan0 access points merge; migration/conflict guard is required")
        print("No live network commands, service changes or production configuration access")


if __name__ == "__main__":
    main()
