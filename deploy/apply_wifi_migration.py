"""One-time current-network migration with independent timed rollback; root only."""
import fcntl
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import time
import yaml

BASE = Path('/root/encoder-wifi-migration-bia2kngj')
STATE = BASE / 'apply'
OLD = Path('/etc/netplan/30-wifis-dhcp.yaml')
NEW = Path('/etc/netplan/90-encoder-wifi.yaml')
OVERRIDE = Path('/etc/systemd/system/netplan-wpa-wlan0.service.d/90-encoder-policy.conf')
SELF = Path('/root/encoder-apply-wifi.py')
TIMER = 'encoder-wifi-migration-rollback'
RUNTIME = Path('/run/encoder-wifi-policy')

class Refused(Exception):
    pass

def require(ok, message):
    if not ok:
        raise Refused(message)

def run(*args):
    return subprocess.run(args, check=True, capture_output=True, timeout=25)

def put(path, data):
    temporary = path.with_name(path.name + '.encoder-stage')
    with temporary.open('xb') as stream:
        stream.write(data)
        stream.flush()
        os.fsync(stream.fileno())
    os.replace(temporary, path)
    directory = os.open(path.parent, os.O_RDONLY | os.O_DIRECTORY)
    try:
        os.fsync(directory)
    finally:
        os.close(directory)

def phase(value=None):
    if value is not None:
        put(STATE / 'phase', value.encode())
    return (STATE / 'phase').read_text()

def rollback():
    with (STATE / 'lock').open('a') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        if phase() in ('committed', 'restored'):
            return
        put(OLD, (STATE / 'original.yaml').read_bytes())
        # These exact paths were required absent before applying.
        for path in (NEW, OVERRIDE):
            if path.exists():
                require(not path.is_symlink(), 'Rollback destination redirected')
                path.unlink()
        phase('restoring')
    run('systemctl', 'disable', '--now', 'encoder-wifi-recovery.service')
    run('systemctl', 'daemon-reload')
    run('systemctl', 'restart', 'netplan-wpa-wlan0.service')
    phase('restored')
    print('ROLLBACK: original profile and Wi-Fi service configuration restored')

def ethernet_ready():
    devices = json.loads(run('ip', '-j', '-4', 'addr', 'show', 'dev', 'end1').stdout)
    return any('LOWER_UP' in d.get('flags', []) and any(a.get('local') == '172.10.0.2'
               for a in d.get('addr_info', [])) for d in devices)

def link_ready(ssid):
    try:
        result = run('wpa_cli', '-i', 'wlan0', 'status').stdout.decode()
    except (subprocess.SubprocessError, OSError):
        return False
    values = dict(line.split('=', 1) for line in result.splitlines() if '=' in line)
    return (values.get('ssid') == ssid and values.get('wpa_state') == 'COMPLETED'
            and values.get('key_mgmt') == 'WPA2-PSK' and values.get('pairwise_cipher') == 'CCMP'
            and values.get('group_cipher') == 'CCMP' and bool(values.get('ip_address')))

def apply():
    require(Path(__file__).resolve() == SELF, 'Use the verified root-owned installation path')
    require(not STATE.exists(), 'Migration already attempted; inspect its state before retrying')
    for path in (NEW, OVERRIDE):
        require(not path.exists() and not path.is_symlink(), 'Managed destination already exists')
    require(not OLD.is_symlink() and not BASE.is_symlink(), 'Source redirected')
    require(ethernet_ready(), 'Ethernet recovery link is unavailable')
    run('systemctl', 'is-active', '--quiet', 'encoder-server.service')
    require(not run('systemctl', 'show', 'netplan-wpa-wlan0.service', '-p', 'DropInPaths', '--value').stdout.strip(),
            'Unexpected Wi-Fi service overrides')
    original = OLD.read_bytes()
    require(hashlib.sha256(original).hexdigest() == (BASE / 'source.sha256').read_text().strip(),
            'Source changed since preparation')
    draft = (BASE / 'shadow/etc/netplan/90-encoder-wifi.yaml').read_bytes()
    policy = (BASE / '90-encoder-policy.conf.review').read_bytes()
    require(hashlib.sha256(policy).hexdigest() == '88569f8603c6d99c07d4ce40237b1348b22ee2442e7784bd95e94aaa6b43d295',
            'Policy override checksum mismatch')
    run('python3', '-I', '/opt/encoder/libexec/netplan_preflight.py', '--root-dir', str(BASE / 'shadow'))
    ssid = next(iter(yaml.safe_load(draft)['network']['wifis']['wlan0']['access-points']))
    require(link_ready(ssid), 'Current Wi-Fi connection is not ready or differs from draft')
    STATE.mkdir(mode=0o700)
    put(STATE / 'original.yaml', original)
    phase('pending')
    # Independent systemd timer outlives SSH and this script. Do not reboot during migration.
    run('systemd-run', '--unit=' + TIMER, '--on-active=150s', '--timer-property=AccuracySec=1s',
        '/usr/bin/python3', '-I', str(SELF), '--rollback')
    try:
        with (STATE / 'lock').open('a') as lock:
            fcntl.flock(lock, fcntl.LOCK_EX)
            require(phase() == 'pending', 'Rollback already began')
            OVERRIDE.parent.mkdir(mode=0o755, exist_ok=True)
            OVERRIDE.parent.chmod(0o755)
            put(NEW, draft)
            OLD.unlink()
            put(OVERRIDE, policy)
        # --check validates the canonical profile without requesting a restart.
        runtime = RUNTIME
        require(not runtime.is_symlink(), 'Runtime directory redirected')
        runtime.mkdir(mode=0o700, exist_ok=True)
        run('/opt/encoder/bin/encoder-wifi-policy-guard', '--check')
        run('systemctl', 'daemon-reload')
        run('systemctl', 'restart', 'netplan-wpa-wlan0.service')
        deadline = time.monotonic() + 50
        while time.monotonic() < deadline:
            if link_ready(ssid) and ethernet_ready():
                break
            time.sleep(1)
        else:
            raise Refused('Current network did not return before deadline')
        run('systemctl', 'enable', '--now', 'encoder-wifi-recovery.service')
        time.sleep(2)
        run('systemctl', 'is-active', '--quiet', 'encoder-wifi-recovery.service')
        run('systemctl', 'is-active', '--quiet', 'encoder-server.service')
        with (STATE / 'lock').open('a') as lock:
            fcntl.flock(lock, fcntl.LOCK_EX)
            require(phase() == 'pending' and ethernet_ready() and link_ready(ssid), 'Cannot confirm migrated connection')
            phase('committed')
        run('systemctl', 'stop', TIMER + '.timer')
        print('OK: current Wi-Fi migrated; WPA2/CCMP and IPv4 verified; DHCPv6 preserved in saved profile')
        print('Ethernet and encryption server available; independent recovery service active/enabled')
        print('Wi-Fi change API/socket still disabled. Do not switch to another network yet.')
        print('Private backup and state:', STATE)
    except Exception:
        rollback()
        raise

if __name__ == '__main__':
    os.umask(0o077)
    try:
        require(os.geteuid() == 0, 'Root required')
        if sys.argv[1:] == ['--rollback']:
            rollback()
        else:
            require(not sys.argv[1:], 'Unsupported arguments')
            apply()
    except Refused as error:
        raise SystemExit('REFUSED: ' + str(error)) from None
    except Exception:
        raise SystemExit('ERROR: migration did not finish. Keep Ethernet connected; report this result. Secret details suppressed.') from None
