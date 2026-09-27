"""One-time, explicitly approved parallel migration for the existing board.

Refuses existing destinations. Never deletes old data, changes networking, or
enables the new service. The old service is stopped briefly for a consistent copy.
Run a root-owned verified copy, not directly from an unprivileged home directory.
"""
import hashlib
import os
from pathlib import Path
import pwd
import shutil
import socket
import stat
import subprocess

OLD = Path('/home/server/encoder')
SOURCE = Path('/home/encoder-diag/encoder-migration-c91a8ce/build/server/encoder-server')
EXPECTED = '1b0618d07cffbcec45f166deaa17d3f438a444cb3c9ca50cf7c41442f14e2e61'
BACKUP = Path('/root/encoder-migration-20260927/old-installation.tar.gz')
UNIT = Path('/etc/systemd/system/encoder-server.service')

def run(*args):
    return subprocess.run(args, check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)

def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()

def inventory(root):
    result = {}
    for path in root.rglob('*'):
        mode = path.lstat().st_mode
        if not (stat.S_ISREG(mode) or stat.S_ISDIR(mode)):
            raise RuntimeError('Storage contains a link or special file; manual review needed')
        if stat.S_ISREG(mode):
            result[str(path.relative_to(root))] = digest(path)
    return result

def main():
    if os.geteuid() != 0:
        raise RuntimeError('Root required')
    os.umask(0o077)
    targets = [Path('/opt/encoder'), Path('/etc/encoder'), Path('/var/lib/encoder'), UNIT]
    if any(p.exists() or p.is_symlink() for p in targets):
        raise RuntimeError('Destination already exists; refusing overwrite')
    if digest(BACKUP) != 'c552b2a170193a30ce4684456cf9b57fe4db737934622253f86385f9ef2644d8':
        raise RuntimeError('Backup verification failed')
    if SOURCE.is_symlink() or digest(SOURCE) != EXPECTED:
        raise RuntimeError('Tested server binary mismatch')
    run('systemctl', 'is-active', '--quiet', 'cipheator.service')
    for port in (9443, 9444):
        with socket.socket() as probe:
            probe.bind(('0.0.0.0', port))
    values = {}
    for line in (OLD / 'config/server.conf').read_text().splitlines():
        if line.strip() and not line.lstrip().startswith('#') and '=' in line:
            key, value = line.split('=', 1)
            values[key.strip()] = value.strip()
    storage = Path(values.get('storage_dir', 'storage'))
    if not storage.is_absolute():
        storage = OLD / storage
    if storage.resolve() != OLD / 'storage' or storage.is_symlink():
        raise RuntimeError('Unexpected old storage location')
    if not values.get('admin_token') or values['admin_token'].upper() in ('CHANGE_ME', 'CHANGEME'):
        raise RuntimeError('A real admin token is required; refusing silent credential changes')
    try:
        pwd.getpwnam('encoder')
    except KeyError:
        run('useradd', '--system', '--user-group', '--home-dir', '/var/lib/encoder',
            '--no-create-home', '--shell', '/usr/sbin/nologin', 'encoder')
    account = pwd.getpwnam('encoder')
    if account.pw_shell not in ('/usr/sbin/nologin', '/sbin/nologin', '/bin/false'):
        raise RuntimeError('Existing encoder account is not a service account')
    binaries = Path('/opt/encoder/bin')
    binaries.mkdir(parents=True, mode=0o755)
    binaries.chmod(0o755)
    binaries.parent.chmod(0o755)
    shutil.copyfile(SOURCE, binaries / 'encoder-server')
    if digest(binaries / 'encoder-server') != EXPECTED:
        raise RuntimeError('Installed binary verification failed')
    (binaries / 'encoder-server').chmod(0o755)
    certs = Path('/etc/encoder/certs')
    certs.mkdir(parents=True, mode=0o750)
    for path in (certs.parent, certs):
        os.chown(path, 0, account.pw_gid)
        path.chmod(0o750)
    run('openssl', 'req', '-x509', '-newkey', 'rsa:3072', '-nodes', '-days', '365',
        '-keyout', str(certs / 'server.key'), '-out', str(certs / 'server.crt'),
        '-subj', '/CN=orangepi3b', '-addext',
        'subjectAltName=IP:172.10.0.2,IP:10.0.0.69,IP:127.0.0.1,DNS:orangepi3b,DNS:orangepi3b.local,DNS:localhost')
    for path in certs.iterdir():
        os.chown(path, 0, account.pw_gid)
        path.chmod(0o640 if path.suffix == '.key' else 0o644)
    run('systemctl', 'stop', 'cipheator.service')
    try:
        before = inventory(storage)
        shutil.copytree(storage, '/var/lib/encoder', symlinks=True)
        if inventory(Path('/var/lib/encoder')) != before:
            raise RuntimeError('Copied data verification failed')
    finally:
        run('systemctl', 'start', 'cipheator.service')
    destination = Path('/var/lib/encoder')
    for path in [destination, *destination.rglob('*')]:
        os.chown(path, account.pw_uid, account.pw_gid)
        path.chmod(0o700 if path.is_dir() else 0o600)
    for key in ('enc_magma', 'dec_magma', 'enc_kuznechik', 'dec_kuznechik',
                'gost_enc_suffix', 'gost_key_suffix'):
        values.pop(key, None)
    values.update(listen_host='0.0.0.0', listen_port='9443', admin_host='0.0.0.0',
                  admin_port='9444', storage_dir='/var/lib/encoder',
                  cert_file=str(certs / 'server.crt'), key_file=str(certs / 'server.key'),
                  wifi_read_enabled='false', wifi_scan_enabled='false', wifi_change_enabled='false')
    config = Path('/etc/encoder/server.conf')
    config.write_text('# Parallel migration: independent data, temporary ports.\n' +
                      ''.join(f'{key}={value}\n' for key, value in sorted(values.items())))
    os.chown(config, 0, account.pw_gid)
    config.chmod(0o640)
    UNIT.write_text('''[Unit]
Description=encoder encryption server (parallel migration)
After=network.target
[Service]
Type=simple
User=encoder
Group=encoder
WorkingDirectory=/var/lib/encoder
ExecStart=/opt/encoder/bin/encoder-server --config /etc/encoder/server.conf
Restart=on-failure
RestartSec=5
UMask=0077
NoNewPrivileges=true
PrivateTmp=true
ProtectHome=true
ProtectSystem=strict
ReadWritePaths=/var/lib/encoder
InaccessiblePaths=-/run/wpa_supplicant
[Install]
WantedBy=multi-user.target
''')
    UNIT.chmod(0o644)
    run('systemd-analyze', 'verify', str(UNIT))
    run('systemctl', 'daemon-reload')
    run('systemctl', 'start', 'encoder-server.service')
    run('systemctl', 'is-active', '--quiet', 'cipheator.service')
    run('systemctl', 'is-active', '--quiet', 'encoder-server.service')
    print(f'OK: copied and verified {len(before)} storage files; private service ownership applied')
    print('OK: old server active on 7443/7444; new server started on 9443/9444, NOT enabled')
    print('No old files deleted; networking unchanged; new certificate contains current IP/DNS names')

if __name__ == '__main__':
    main()
