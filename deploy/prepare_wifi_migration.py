"""Prepare a private offline migration draft; never applies or restarts networking."""
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import stat
import subprocess
import tempfile
import yaml

def require(condition, message):
    if not condition:
        raise RuntimeError(message)

def main():
    require(os.geteuid() == 0, 'Root required')
    os.umask(0o077)
    source = Path('/etc/netplan/30-wifis-dhcp.yaml')
    info = source.lstat()
    require(stat.S_ISREG(info.st_mode) and info.st_uid == 0 and not info.st_mode & 0o077,
            'Wi-Fi source must be a private root-owned regular file')
    require(info.st_size < 65536, 'Unexpected source size')
    data = source.read_bytes()
    # Reject duplicates rather than silently selecting one credential/profile.
    class Loader(yaml.SafeLoader):
        def construct_mapping(self, node, deep=False):
            pairs = self.construct_pairs(node, deep=deep)
            require(len({key for key, _ in pairs}) == len(pairs), 'Duplicate YAML mapping')
            return dict(pairs)
    network = yaml.load(data, Loader=Loader)['network']
    require(set(network) <= {'version', 'renderer', 'wifis'} and network.get('version') == 2,
            'Unexpected scope in Wi-Fi file')
    require(network.get('renderer', 'networkd') == 'networkd', 'Unsupported renderer')
    require(set(network['wifis']) == {'wlan0'}, 'More than one Wi-Fi interface')
    wifi = network['wifis']['wlan0']
    require(set(wifi) <= {'dhcp4', 'renderer', 'access-points'} and wifi.get('dhcp4') is True,
            'Extra wlan0 settings require review; nothing changed')
    require(wifi.get('renderer', 'networkd') == 'networkd', 'Unsupported interface renderer')
    points = wifi['access-points']
    require(len(points) == 1, 'Expected one saved network')
    ssid, point = next(iter(points.items()))
    require(isinstance(ssid, str) and 1 <= len(ssid.encode('utf-8')) <= 32,
            'Unsupported SSID')
    require(all(ord(c) >= 32 and not 0x7f <= ord(c) <= 0x9f and ord(c) not in (0x2028, 0x2029)
                and not 0xfdd0 <= ord(c) <= 0xfdef and ord(c) & 0xffff not in (0xfffe, 0xffff) for c in ssid),
            'Unsupported SSID characters')
    require(set(point) == {'password'}, 'Non-basic authentication requires review')
    password = point['password']
    require(isinstance(password, str), 'Unsupported password type')
    if re.fullmatch('[0-9a-fA-F]{64}', password):
        psk = password.lower()
    else:
        require(8 <= len(password) <= 63 and all(32 <= ord(c) <= 126 for c in password),
                'Unsupported passphrase encoding')
        psk = hashlib.pbkdf2_hmac('sha1', password.encode('ascii'), ssid.encode('utf-8'), 4096, 32).hex()
    result = subprocess.run(['wpa_cli', '-i', 'wlan0', 'status'], check=True, capture_output=True, text=True)
    status = dict(line.split('=', 1) for line in result.stdout.splitlines() if '=' in line)
    require(status.get('wpa_state') == 'COMPLETED' and status.get('key_mgmt') == 'WPA2-PSK'
            and status.get('pairwise_cipher') == 'CCMP' and status.get('group_cipher') == 'CCMP',
            'Current link is not confirmed WPA2-PSK/CCMP; nothing changed')
    # Compare encoded SSID bytes without displaying the network name.
    require(status.get('ssid') == ssid, 'Saved and connected network differ; review required')
    output = Path(tempfile.mkdtemp(prefix='encoder-wifi-migration-', dir='/root'))
    shadow = output / 'shadow'
    for relative in ('etc/netplan', 'run/netplan', 'lib/netplan'):
        original = Path('/') / relative
        if original.exists():
            require(not original.is_symlink(), 'Redirected Netplan directory')
            for child in original.iterdir():
                require(child.is_file() and not child.is_symlink(), 'Unexpected Netplan entry')
            shutil.copytree(original, shadow / relative)
    shutil.copy2(source, output / 'original-wifi.yaml')
    (shadow / 'etc/netplan/30-wifis-dhcp.yaml').unlink()
    quoted = '"' + ssid.replace('\\', '\\\\').replace('"', '\\"') + '"'
    draft = ('# encoder managed wifi v1\n'
             '# Draft only: requires verified WPA2 enforcement before installation.\n'
             'network:\n  version: 2\n  wifis:\n    wlan0:\n'
             '      renderer: networkd\n      dhcp4: true\n      access-points:\n        '
             + quoted + ':\n          auth:\n            key-management: psk\n            password: "' + psk + '"\n')
    (shadow / 'etc/netplan/90-encoder-wifi.yaml').write_text(draft)
    generated = subprocess.run(['netplan', 'generate', '--root-dir', str(shadow)], capture_output=True)
    require(generated.returncode == 0, 'Offline Netplan generation failed; output suppressed to protect credentials')
    (output / 'source.sha256').write_text(hashlib.sha256(data).hexdigest() + '\n')
    print('OK: current network matches saved profile and uses WPA2-PSK/CCMP')
    print('OK: private canonical migration draft generated; offline Netplan accepted it')
    print('Private preparation directory:', output)
    print('Real /etc/netplan unchanged; no network service restarted; Wi-Fi change still disabled')

if __name__ == '__main__':
    try:
        main()
    except Exception:
        # Do not expose YAML/parser exceptions, source lines, SSID or PSK.
        raise SystemExit('REFUSED: preparation could not be validated. No live network settings changed.') from None
