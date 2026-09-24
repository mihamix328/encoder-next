"""Offline systemd verification only. No systemctl/start/reload/network calls."""
import argparse
from pathlib import Path
import shutil
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--fixtures", required=True)
    parser.add_argument("--dropin", required=True)
    args = parser.parse_args()
    netplan = shutil.which("netplan")
    analyze = shutil.which("systemd-analyze")
    if not netplan or not analyze:
        raise RuntimeError("Netplan and systemd-analyze are required")
    dropin = Path(args.dropin).read_text(encoding="utf-8")
    if "ExecStartPre=/opt/encoder/bin/encoder-wifi-policy-guard --stage\n" not in dropin or \
       "ExecStart=\nExecStart=/sbin/wpa_supplicant -c /run/encoder-wifi-policy/wpa-wlan0.conf -iwlan0 -Dnl80211,wext\n" not in dropin:
        raise RuntimeError("Policy staging and supplicant launch must use the same fixed runtime file")
    with tempfile.TemporaryDirectory(prefix="encoder-policy-unit-") as temporary:
        root = Path(temporary)
        config = root / "etc/netplan"
        config.mkdir(parents=True)
        draft = subprocess.check_output([str(Path(args.fixtures).resolve()), "--netplan-fixture"], timeout=10)
        profile = config / "90-encoder-wifi.yaml"
        profile.write_bytes(draft)
        profile.chmod(0o600)
        generated = subprocess.run([netplan, "generate", "--root-dir", str(root)], capture_output=True, timeout=30)
        if generated.returncode:
            raise RuntimeError("Isolated Netplan generation failed")
        units = [p for p in root.rglob("netplan-wpa-wlan0.service") if p.is_file() and not p.is_symlink()]
        if len(units) != 1:
            raise RuntimeError("Expected one generated wlan0 service")
        unit_dir = root / "etc/systemd/system"
        unit_dir.mkdir(parents=True)
        (unit_dir / "netplan-wpa-wlan0.service").write_bytes(units[0].read_bytes())
        overrides = unit_dir / "netplan-wpa-wlan0.service.d"
        overrides.mkdir()
        (overrides / "90-encoder-policy.conf").write_text(dropin, encoding="utf-8")
        # Executable placeholders are ONLY inspected, never executed.
        for name in ("sbin/wpa_supplicant", "sbin/wpa_cli", "opt/encoder/bin/encoder-wifi-policy-guard", "bin/true"):
            executable = root / name
            executable.parent.mkdir(parents=True, exist_ok=True)
            executable.write_text("#!/bin/sh\nexit 0\n", encoding="ascii")
            executable.chmod(0o755)
        for name in ("network.target", "basic.target", "sysinit.target", "shutdown.target", "sockets.target"):
            (unit_dir / name).write_text("[Unit]\nDefaultDependencies=no\n", encoding="ascii")
        (unit_dir / "netplan-configure.service").write_text(
            "[Unit]\nDefaultDependencies=no\n[Service]\nType=oneshot\nExecStart=/bin/true\n", encoding="ascii")
        verified = subprocess.run([analyze, "--root=" + str(root), "verify", "netplan-wpa-wlan0.service"],
                                  capture_output=True, timeout=20)
        if verified.returncode:
            raise RuntimeError("Offline policy service verification failed: " + verified.stderr.decode(errors="replace")[:1500])
        if profile.read_bytes() != draft:
            raise RuntimeError("Unit verification changed the fixture profile")
        print("OK: real generated Netplan unit plus policy override verified offline")
        print("No unit installed or started; boot and hardware behavior remain untested")


if __name__ == "__main__":
    main()
