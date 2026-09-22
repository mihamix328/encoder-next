"""Linux fixtures only: never inspect the machine's real network configuration."""
import importlib.util
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

SCRIPT = Path(__file__).resolve().parents[1] / "tools" / "netplan_preflight.py"
spec = importlib.util.spec_from_file_location("preflight", SCRIPT)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
ETHERNET = b"network:\n  version: 2\n  ethernets:\n    end1:\n      addresses: [172.10.0.2/24]\n"


class Checks(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="encoder-scope-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.config = self.root / "etc/netplan"
        self.config.mkdir(parents=True, mode=0o700)

    def write(self, name, data):
        path = self.config / name
        path.write_bytes(data)
        path.chmod(0o600)
        return path

    def test_ethernet_and_generated_profile(self):
        ethernet = self.write("10-ethernet.yaml", ETHERNET)
        self.assertEqual(module.preflight(self.root), 1)
        draft = subprocess.check_output([FIXTURES, "--netplan-fixture"], timeout=10)
        managed = self.write(module.MANAGED, draft)
        self.assertEqual(module.preflight(self.root), 2)
        self.assertEqual(ethernet.read_bytes(), ETHERNET)
        self.assertEqual(managed.read_bytes(), draft)
        managed.chmod(0o644)
        with self.assertRaises(module.Refused):
            module.preflight(self.root)

    def test_old_wifi_refused_without_disclosing_credentials(self):
        secret = "private-fixture-do-not-echo"
        old = self.write("30-old.yaml", ("network:\n  version: 2\n  wifis:\n    wlan0:\n      access-points:\n        private-name:\n          password: " + secret + "\n").encode())
        original = old.read_bytes()
        run = subprocess.run([sys.executable, "-B", str(SCRIPT), "--root-dir", str(self.root)], capture_output=True, text=True, timeout=10)
        self.assertEqual(run.returncode, 1)
        self.assertNotIn(secret, run.stdout + run.stderr)
        self.assertNotIn("private-name", run.stdout + run.stderr)
        self.assertEqual(old.read_bytes(), original)

    def test_ambiguous_yaml(self):
        for raw in (b"network: {version: 2, version: 2}", b"network: {version: 2}\n---\nnetwork: {version: 2}",
                    b"network: &n {version: 2, ethernets: *n}", b"network: !!str hello", b"network: [broken",
                    b"network: {version: 2, bridges: {br0: {interfaces: [wlan0]}}}",
                    b"network: {version: 2, ethernets: {end1: {match: {name: '*'}}}}",
                    b"network: {version: 2, ethernets: {end1: {'<<': {match: {name: wlan0}}}}}",
                    b"network: {version: 2, renderer: NetworkManager}"):
            self.write("10-bad.yaml", raw)
            with self.subTest(raw=raw), self.assertRaises(module.Refused):
                module.preflight(self.root)

    def test_links_and_permissions(self):
        target = self.write("10-ethernet.yaml", ETHERNET)
        link = self.config / "20-link.yaml"
        link.symlink_to(target)
        with self.assertRaises(module.Refused):
            module.preflight(self.root)
        link.unlink()
        os.link(target, link)
        with self.assertRaises(module.Refused):
            module.preflight(self.root)
        link.unlink()
        target.chmod(0o666)
        with self.assertRaises(module.Refused):
            module.preflight(self.root)
        target.chmod(0o600)
        self.config.chmod(0o777)
        with self.assertRaises(module.Refused):
            module.preflight(self.root)
        self.config.chmod(0o700)

    def test_size_depth_and_shadow(self):
        file = self.write("10-bad.yaml", b"x" * 65537)
        with self.assertRaises(module.Refused):
            module.preflight(self.root)
        file.write_bytes(b"network: " + b"[" * 25 + b"x" + b"]" * 25)
        with self.assertRaises(module.Refused):
            module.preflight(self.root)
        file.unlink()
        shadow = self.root / "run/netplan"
        shadow.mkdir(parents=True, mode=0o700)
        (shadow / module.MANAGED).write_bytes(ETHERNET)
        with self.assertRaises(module.Refused):
            module.preflight(self.root)

    def test_foreign_managed_file(self):
        self.write(module.MANAGED, ETHERNET)
        with self.assertRaises(module.Refused):
            module.preflight(self.root)

    def test_managed_scope_and_identity(self):
        draft = subprocess.check_output([FIXTURES, "--netplan-fixture"], timeout=10)
        for changed in (draft.replace(b"wlan0:", b"end1:"),
                        draft.replace(b"network:\n", b"network:\n  renderer: networkd\n"),
                        draft.replace(b"key-management: psk", b"key-management: none"),
                        draft.replace(b"Encoder test", b"\\uFFFF"),
                        draft.replace(b"Encoder test", b"\\n"),
                        draft.replace(b"a" * 64, b"private-fixture-password")):
            self.write(module.MANAGED, changed)
            with self.assertRaises(module.Refused):
                module.preflight(self.root)

    def test_malformed_yaml_does_not_echo_secret(self):
        secret = "malformed-private-fixture-do-not-echo"
        self.write("10-bad.yaml", ('network: {version: 2, password: "' + secret).encode())
        run = subprocess.run([sys.executable, "-B", str(SCRIPT), "--root-dir", str(self.root)], capture_output=True, text=True, timeout=10)
        self.assertEqual(run.returncode, 1)
        self.assertNotIn(secret, run.stdout + run.stderr)
        self.assertNotIn("Traceback", run.stdout + run.stderr)

    def test_file_and_node_limits(self):
        for number in range(33):
            self.write(f"{number:02}-ethernet.yaml", ETHERNET)
        with self.assertRaises(module.Refused):
            module.preflight(self.root)
        for path in self.config.iterdir():
            path.unlink()
        self.write("10-large-tree.yaml", b"network: {version: 2, ethernets: {end1: {addresses: [" + b"a," * 2100 + b"]}}}")
        with self.assertRaises(module.Refused):
            module.preflight(self.root)


if __name__ == "__main__":
    FIXTURES = str(Path(sys.argv.pop(1)).resolve(strict=True))
    unittest.main()
