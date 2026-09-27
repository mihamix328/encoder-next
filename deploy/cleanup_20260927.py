"""Approved exact-target cleanup after successful cutover and user acceptance."""
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import tarfile
import tempfile

TARGETS = (
    '/home/server/encoder',
    '/home/encoder-diag/encoder-check-ECm1Ll',
    '/home/encoder-diag/encoder-v2-check-ao3M9r',
    '/home/encoder-diag/encoder-migration-c91a8ce',
    '/opt/encoder-activation-check.kSFsNZ',
    '/opt/encoder-gui-check.shPGCR',
    '/opt/encoder-scan-check.7B0K5n',
    '/opt/encoder-tls-check.7NxAel',
    '/opt/encoder-tls-check.nP7aXI',
    '/opt/encoder-wifi-check.2ThWTS',
    '/opt/encoder-wifi-publish.oVS3qp',
    '/etc/systemd/system/cipheator.service',
)

def run(*args):
    return subprocess.run(args, check=True, capture_output=True)

def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()

def main():
    if os.geteuid() != 0:
        raise RuntimeError('Root required')
    os.umask(0o077)
    run('systemctl', 'is-active', '--quiet', 'encoder-server.service')
    run('systemctl', 'is-enabled', '--quiet', 'encoder-server.service')
    if run('systemctl', 'show', 'cipheator.service', '-p', 'ActiveState', '--value').stdout.strip() != b'inactive':
        raise RuntimeError('Old service must be inactive')
    for name in ('old.tar.gz', 'new.tar.gz'):
        path = Path('/root/encoder-cutover-ktl1rbn4') / name
        if digest(path) != Path(str(path) + '.sha256').read_text().split()[0]:
            raise RuntimeError('Cutover backup checksum mismatch')
    targets = [Path(value) for value in TARGETS]
    for path in targets:
        if not path.exists() or path.is_symlink() or path.resolve() != path:
            raise RuntimeError('Unexpected cleanup target: ' + str(path))
        if path.is_dir() and os.path.ismount(path):
            raise RuntimeError('Refusing mount point')
        if path.is_dir():
            for child in path.rglob('*'):
                if os.path.ismount(child):
                    raise RuntimeError('Refusing nested mount')
    backup = Path(tempfile.mkdtemp(prefix='encoder-retired-', dir='/root'))
    archive = backup / 'retired-files.tar.gz'
    with tarfile.open(archive, 'w:gz') as output:
        for path in targets:
            output.add(path, arcname=str(path).lstrip('/'))
    count = 0
    with tarfile.open(archive, 'r:gz') as verify:
        for member in verify:
            if member.isfile():
                source = verify.extractfile(member)
                archived = hashlib.file_digest(source, 'sha256').hexdigest()
                original = Path('/') / member.name
                if archived != digest(original):
                    raise RuntimeError('Retired archive verification mismatch')
                count += 1
    (backup / 'retired-files.tar.gz.sha256').write_text(digest(archive) + '  retired-files.tar.gz\n')
    # Exact validated directories only; do not remove any parent directory or account.
    for path in targets:
        if path.resolve() != path or path.is_symlink():
            raise RuntimeError('Target changed during cleanup')
        if path.is_dir():
            shutil.rmtree(path)
        else:
            path.unlink()
    run('systemctl', 'daemon-reload')
    run('systemctl', 'is-active', '--quiet', 'encoder-server.service')
    run('python3', '-I', '/root/encoder-migration-20260927/check_parallel_tls.py',
        '--config', '/etc/encoder/server.conf', '--ports', '7443', '7444')
    print(f'OK: {len(targets)} exact targets removed; {count} archived regular files verified')
    print('Recoverable backup:', backup)
    print('Production server/TLS verified; SSH account and networking unchanged')
    # Last privileged change: revoke only the exact temporary grant we installed.
    grant = Path('/etc/sudoers.d/encoder-diag')
    if grant.is_symlink() or grant.read_text().strip() != 'encoder-diag ALL=(ALL:ALL) NOPASSWD: ALL':
        raise RuntimeError('Unexpected sudo grant; not removed')
    shutil.copyfile(grant, backup / 'encoder-diag-sudo-grant.disabled')
    grant.unlink()
    run('visudo', '-c')
    print('Temporary unrestricted sudo grant revoked; sudo configuration validated')

if __name__ == '__main__':
    main()
