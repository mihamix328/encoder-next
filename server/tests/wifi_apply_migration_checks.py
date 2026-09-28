"""Private filesystem fixtures and mocked commands only; never touches live networking."""
import hashlib
import importlib.util
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

REPO = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('apply_wifi', REPO / 'deploy/apply_wifi_migration.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)

class Checks(unittest.TestCase):
    def exercise(self, failure=None):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            base = root / 'prepared'
            (base / 'shadow/etc/netplan').mkdir(parents=True)
            old = root / 'etc/netplan/30-wifis-dhcp.yaml'
            old.parent.mkdir(parents=True)
            original = b'network:\n  wifis:\n    wlan0:\n      dhcp4: true\n      dhcp6: true\n'
            old.write_bytes(original)
            (base / 'source.sha256').write_text(hashlib.sha256(original).hexdigest())
            draft = b'network:\n  version: 2\n  wifis:\n    wlan0:\n      dhcp6: true\n      access-points: {Test: {password: fixture-only}}\n'
            (base / 'shadow/etc/netplan/90-encoder-wifi.yaml').write_bytes(draft)
            policy = (REPO / 'deploy/experimental/netplan-wpa-wlan0.service.d/90-encoder-policy.conf').read_bytes().replace(b'\r\n', b'\n')
            (base / '90-encoder-policy.conf.review').write_bytes(policy)
            override = root / 'units/wlan0.d/90-encoder-policy.conf'
            override.parent.parent.mkdir()
            new = old.parent / '90-encoder-wifi.yaml'
            calls = []
            def fake_run(*args):
                calls.append(args)
                if failure == 'guard' and args[0].endswith('encoder-wifi-policy-guard'):
                    raise subprocess.CalledProcessError(1, args)
                if failure == 'timer' and args[0] == 'systemd-run':
                    raise subprocess.CalledProcessError(1, args)
                return subprocess.CompletedProcess(args, 0, stdout=b'', stderr=b'')
            with patch.multiple(module, BASE=base, STATE=base / 'apply', OLD=old, NEW=new,
                                OVERRIDE=override, SELF=Path(module.__file__).resolve(), RUNTIME=root / 'runtime'), \
                    patch.object(module, 'run', fake_run), \
                    patch.object(module, 'ethernet_ready', return_value=True), \
                    patch.object(module, 'link_ready', return_value=True), \
                    patch.object(module.time, 'sleep'):
                if failure:
                    with self.assertRaises(subprocess.CalledProcessError):
                        module.apply()
                    self.assertEqual(old.read_bytes(), original)
                    self.assertFalse(new.exists())
                    self.assertFalse(override.exists())
                else:
                    module.apply()
                    self.assertFalse(old.exists())
                    self.assertEqual(new.read_bytes(), draft)
                    self.assertEqual(module.phase(), 'committed')
                    module.rollback()
                    self.assertTrue(new.exists(), 'late timer must not undo committed state')
                self.assertTrue(any(c[0] == 'systemd-run' for c in calls))

    def test_success(self):
        self.exercise()

    def test_guard_failure_restores_original(self):
        self.exercise('guard')

    def test_timer_failure_changes_no_live_files(self):
        self.exercise('timer')

if __name__ == '__main__':
    unittest.main()
