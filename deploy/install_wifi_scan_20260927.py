"""Install only cached Wi-Fi status and scan endpoint; never change Netplan/Wi-Fi profiles."""
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

SOURCE = Path('/home/encoder-diag/wifi-install.tlzU2A')
FILES = (
    ('build/server/encoder-wifi-collector', '/opt/encoder/bin/encoder-wifi-collector', 'f0e8f0864493f5aaac366e843bb1525b7f6c6ec173036ab77f4cf4dbd73dec46', 0o755),
    ('build/server/encoder-wifi-scan-helper', '/opt/encoder/bin/encoder-wifi-scan-helper', '81656af0edae0a09117de0fac730a1c394a7d484c094641a2145fb89d1dd55a7', 0o755),
    ('deploy/encoder-wifi-collector.service', '/etc/systemd/system/encoder-wifi-collector.service', '5f09d93ddaca0f5397304c14b81859118b249d00a7c6156c510ce9fe135f7043', 0o644),
    ('deploy/encoder-wifi-collector.timer', '/etc/systemd/system/encoder-wifi-collector.timer', 'dcfa035178f55f3f47422a5ddb5077b9726f9faab5e6b1055fcb786e034010da', 0o644),
    ('deploy/encoder-wifi-scan.socket', '/etc/systemd/system/encoder-wifi-scan.socket', '83ec497af90d028667eb608a9bce96d6a14ba961304e6947fffb2add1bdf5781', 0o644),
    ('deploy/encoder-wifi-scan@.service', '/etc/systemd/system/encoder-wifi-scan@.service', 'cc1c7703c81ffda03b65534df272a2560988f808da04612b49e09e2bcdbb90ad', 0o644),
)

def run(*args):
    return subprocess.run(args, check=True, capture_output=True)

def main():
    if os.geteuid() != 0:
        raise RuntimeError('Root required for this one-time installation')
    os.umask(0o077)
    run('systemctl', 'is-active', '--quiet', 'encoder-server.service')
    run('systemctl', 'is-active', '--quiet', 'netplan-wpa-wlan0.service')
    config = Path('/etc/encoder/server.conf')
    if config.is_symlink():
        raise RuntimeError('Unexpected configuration symlink')
    original = config.read_bytes()
    # Freeze and verify bytes before placing them in executable, root-owned paths.
    verified = []
    for source, destination, expected, mode in FILES:
        target = Path(destination)
        if target.exists() or target.is_symlink() or target.parent.resolve() != target.parent:
            raise RuntimeError('Destination exists or parent is redirected: ' + destination)
        data = (SOURCE / source).read_bytes()
        if hashlib.sha256(data).hexdigest() != expected:
            raise RuntimeError('Source checksum mismatch: ' + source)
        verified.append((target, data, mode))
    backup = Path(tempfile.mkdtemp(prefix='encoder-wifi-scan-backup-', dir='/root'))
    shutil.copy2(config, backup / 'server.conf')
    created = []
    success = False
    try:
        for target, data, mode in verified:
            with target.open('xb') as output:
                output.write(data)
            created.append(target)
            target.chmod(mode)
        run('systemd-analyze', 'verify', *[str(target) for target in created if target.suffix in ('.service', '.timer', '.socket')])
        run('systemctl', 'daemon-reload')
        run('systemctl', 'start', 'encoder-wifi-collector.service')
        run('runuser', '-u', 'encoder', '--', 'test', '-r', '/run/encoder-network/wifi.txt')
        run('runuser', '-u', 'encoder', '--', 'test', '!', '-w', '/run/encoder-network/wifi.txt')
        run('runuser', '-u', 'encoder', '--', 'test', '!', '-w', '/run/encoder-network')
        run('systemctl', 'start', 'encoder-wifi-scan.socket')
        run('runuser', '-u', 'nobody', '--', 'test', '!', '-w', '/run/encoder-wifi-scan.sock')
        keys = {'wifi_read_enabled', 'wifi_scan_enabled', 'wifi_change_enabled', 'wifi_snapshot_file', 'wifi_scan_socket'}
        lines = [line for line in original.decode().splitlines() if line.split('=', 1)[0].strip() not in keys]
        lines += ['wifi_read_enabled=true', 'wifi_scan_enabled=true', 'wifi_change_enabled=false',
                  'wifi_snapshot_file=/run/encoder-network/wifi.txt', 'wifi_scan_socket=/run/encoder-wifi-scan.sock']
        config.write_text('\n'.join(lines) + '\n')
        run('systemctl', 'restart', 'encoder-server.service')
        run('systemctl', 'is-active', '--quiet', 'encoder-server.service')
        run('systemctl', 'enable', '--now', 'encoder-wifi-collector.timer', 'encoder-wifi-scan.socket')
        success = True
        print('OK: cached Wi-Fi status and scan endpoint installed; access permissions checked')
        print('Wi-Fi change remains DISABLED; Netplan and connection unchanged; no active scan requested')
        print('Private configuration backup:', backup)
    finally:
        if not success:
            subprocess.run(['systemctl', 'disable', '--now', 'encoder-wifi-collector.timer', 'encoder-wifi-scan.socket'], capture_output=True)
            subprocess.run(['systemctl', 'stop', 'encoder-wifi-collector.service'], capture_output=True)
            config.write_bytes(original)
            for target in created:
                target.unlink()
            run('systemctl', 'daemon-reload')
            run('systemctl', 'restart', 'encoder-server.service')
            print('Installation rolled back; private backup:', backup)

if __name__ == '__main__':
    main()
