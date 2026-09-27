"""Read-only board inventory for approved Wi-Fi installation; never prints PSKs/SSIDs."""
import json
import os
from pathlib import Path
import stat
import subprocess
import yaml

def main():
    if os.geteuid() != 0:
        raise SystemExit('Run as root to inspect protected Netplan files; nothing is changed')
    reports = []
    for directory in ('/etc/netplan', '/run/netplan', '/lib/netplan'):
        root = Path(directory)
        if not root.exists():
            continue
        for path in sorted(root.glob('*.yaml')):
            info = {'file': str(path), 'mode': oct(stat.S_IMODE(path.lstat().st_mode))}
            if path.is_symlink() or not path.is_file() or path.stat().st_size > 65536:
                info['review'] = 'link, special or oversized file'
                reports.append(info)
                continue
            try:
                document = yaml.safe_load(path.read_bytes())
                network = document.get('network', {})
                info['renderer'] = network.get('renderer', 'default')
                info['interfaces'] = []
                for kind in ('ethernets', 'wifis'):
                    for name, settings in network.get(kind, {}).items():
                        info['interfaces'].append({
                            'kind': kind, 'name': name,
                            'dhcp4': settings.get('dhcp4'),
                            'access_point_count': len(settings.get('access-points', {})),
                            'has_match': 'match' in settings,
                            'has_addresses': bool(settings.get('addresses')),
                        })
            except Exception:
                info = {'file': str(path), 'review': 'Unable to parse; details suppressed to protect credentials'}
            reports.append(info)
    print(json.dumps({'netplan': reports}, ensure_ascii=True, indent=2))
    for unit in ('netplan-wpa-wlan0.service', 'encoder-server.service'):
        result = subprocess.run(['systemctl', 'show', unit, '-p', 'FragmentPath', '-p', 'DropInPaths',
                                 '-p', 'ActiveState', '-p', 'UnitFileState'], capture_output=True, text=True)
        print(unit + '\n' + result.stdout.strip())
    result = subprocess.run(['wpa_cli', '-i', 'wlan0', 'ping'], capture_output=True, text=True)
    print('WPA control reachable:', result.returncode == 0 and result.stdout.strip() == 'PONG')
    for item in ('/etc/netplan/90-encoder-wifi.yaml', '/var/lib/encoder-wifi-recovery',
                 '/etc/systemd/system/netplan-wpa-wlan0.service.d/90-encoder-policy.conf'):
        print('Existing managed path:', item, Path(item).exists())
    print('Read-only inspection complete; no scan or network changes requested')

if __name__ == '__main__':
    main()
