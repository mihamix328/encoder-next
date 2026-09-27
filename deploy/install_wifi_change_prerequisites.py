"""Install verified, disabled Wi-Fi change prerequisites; do not apply a profile."""
import hashlib
import os
from pathlib import Path
import subprocess

SOURCE = Path('/home/encoder-diag/wifi-install.tlzU2A')
PREPARATION = Path('/root/encoder-wifi-migration-bia2kngj')
FILES = (
    ('build/server/encoder-wifi-change-helper', '/opt/encoder/bin/encoder-wifi-change-helper', '87668c7840fd5de743006cd8c1f42ee5a898d8eb8d1f3a3ce6a69474b8fffb9b', 0o755),
    ('build/server/encoder-wifi-policy-guard', '/opt/encoder/bin/encoder-wifi-policy-guard', 'ce1ab4c3cdcd7a2e6a45674ec9aa0cd6e3f8bb0a6f67e8e7a2a528b643c60519', 0o755),
    ('build/server/encoder-wifi-recovery-supervisor', '/opt/encoder/bin/encoder-wifi-recovery-supervisor', '5abf7be4d0d7e799db9a47377d250cce0b9f8663b9eb534142853cca1234c97f', 0o755),
    ('server/tools/netplan_preflight.py', '/opt/encoder/libexec/netplan_preflight.py', '6ab55ee47e5869c2027f6b3025587595e389645d0c3a50cb47a88445e96c8695', 0o644),
    ('deploy/experimental/encoder-wifi-change.service', '/etc/systemd/system/encoder-wifi-change.service', 'bd2c53835f33eedce535f2aa445e4bd907d70b17b73c1e431451d99bd37a3720', 0o644),
    ('deploy/experimental/encoder-wifi-change.socket', '/etc/systemd/system/encoder-wifi-change.socket', '92fdff7183cabda8450e36ef49a4df828a07823f4fa9634ffbd680bb18ecb209', 0o644),
    ('deploy/experimental/encoder-wifi-recovery.service', '/etc/systemd/system/encoder-wifi-recovery.service', 'fafab56a92e7da5d0900ea06c0c8ecc97dd7fc939f7203d6bbe77d7d1842ee83', 0o644),
    ('deploy/experimental/netplan-wpa-wlan0.service.d/90-encoder-policy.conf', str(PREPARATION / '90-encoder-policy.conf.review'), '88569f8603c6d99c07d4ce40237b1348b22ee2442e7784bd95e94aaa6b43d295', 0o600),
)

def run(*args):
    return subprocess.run(args, check=True, capture_output=True)

def require(value, reason):
    if not value:
        raise RuntimeError(reason)

def main():
    require(os.geteuid() == 0, 'Root required')
    os.umask(0o077)
    require(PREPARATION.is_dir() and not PREPARATION.is_symlink(), 'Preparation directory missing')
    source = Path('/etc/netplan/30-wifis-dhcp.yaml')
    require(hashlib.sha256(source.read_bytes()).hexdigest() == (PREPARATION / 'source.sha256').read_text().strip(),
            'Live source changed since preparation; refusing installation')
    run('systemctl', 'is-active', '--quiet', 'encoder-server.service')
    for unit in ('encoder-wifi-change.socket', 'encoder-wifi-change.service', 'encoder-wifi-recovery.service'):
        active = run('systemctl', 'show', unit, '-p', 'ActiveState', '--value').stdout.strip()
        require(active == b'inactive', 'A change/recovery unit is already active')
    verified = []
    for relative, destination, expected, mode in FILES:
        target = Path(destination)
        require(not target.exists() and not target.is_symlink(), 'Destination already exists; refusing overwrite')
        require(target.parent.resolve() == target.parent, 'Redirected destination parent')
        data = (SOURCE / relative).read_bytes()
        require(hashlib.sha256(data).hexdigest() == expected, 'Source checksum mismatch')
        verified.append((target, data, mode))
    created = []
    libexec = Path('/opt/encoder/libexec')
    created_libexec = not libexec.exists()
    try:
        libexec.mkdir(mode=0o755, exist_ok=True)
        if created_libexec:
            libexec.chmod(0o755)
        for target, data, mode in verified:
            with target.open('xb') as stream:
                stream.write(data)
            created.append(target)
            target.chmod(mode)
        run('/usr/bin/python3', '-I', '-B', str(libexec / 'netplan_preflight.py'),
            '--root-dir', str(PREPARATION / 'shadow'))
        run('systemd-analyze', 'verify', '/etc/systemd/system/encoder-wifi-change.service',
            '/etc/systemd/system/encoder-wifi-change.socket', '/etc/systemd/system/encoder-wifi-recovery.service')
        run('/opt/encoder/bin/encoder-wifi-recovery-supervisor', '--version')
        run('systemctl', 'daemon-reload')
    except Exception:
        for target in reversed(created):
            target.unlink()
        if created_libexec:
            libexec.rmdir()
        run('systemctl', 'daemon-reload')
        raise
    print('OK: checksummed Wi-Fi helpers and disabled units installed; isolated Netplan scope validated')
    print('Policy override remains a private review copy, NOT installed into the Wi-Fi service')
    print('No units started or enabled; no profile changed; server and Wi-Fi not restarted')

if __name__ == '__main__':
    try:
        main()
    except Exception:
        raise SystemExit('REFUSED: prerequisite installation did not complete; no network transition requested') from None
