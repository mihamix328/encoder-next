"""Approved one-time cutover. Preserve both installations and rollback on failure."""
import hashlib
import os
from pathlib import Path
import subprocess
import tarfile
import tempfile
import time

def run(*args):
    return subprocess.run(args, check=True, capture_output=True)

def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()

def inventory(root):
    result = {}
    for path in root.rglob('*'):
        if path.is_symlink() or not (path.is_dir() or path.is_file()):
            raise RuntimeError('Unexpected storage entry')
        if path.is_file():
            result[path.relative_to(root).as_posix()] = digest(path)
    return result

def main():
    if os.geteuid() != 0:
        raise RuntimeError('Root required')
    os.umask(0o077)
    old = Path('/home/server/encoder/storage')
    new = Path('/var/lib/encoder')
    config = Path('/etc/encoder/server.conf')
    original = config.read_bytes()
    if b'listen_port=9443' not in original or b'admin_port=9444' not in original:
        raise RuntimeError('Unexpected port configuration')
    run('systemctl', 'is-active', '--quiet', 'cipheator.service')
    run('systemctl', 'is-active', '--quiet', 'encoder-server.service')
    backup = Path(tempfile.mkdtemp(prefix='encoder-cutover-', dir='/root'))
    success = False
    try:
        run('systemctl', 'stop', 'cipheator.service', 'encoder-server.service')
        for name, paths in (
            ('old.tar.gz', ['/home/server/encoder', '/etc/systemd/system/cipheator.service']),
            ('new.tar.gz', ['/opt/encoder', '/etc/encoder', '/var/lib/encoder', '/etc/systemd/system/encoder-server.service'])):
            archive = backup / name
            with tarfile.open(archive, 'w:gz') as output:
                for item in paths:
                    output.add(item, arcname=item.lstrip('/'))
            with tarfile.open(archive, 'r:gz') as verify:
                for member in verify:
                    if member.isfile():
                        source = verify.extractfile(member)
                        while source.read(1024 * 1024):
                            pass
            (backup / (name + '.sha256')).write_text(digest(archive) + '  ' + name + '\n')
        a, b = inventory(old), inventory(new)
        # Never overwrite keys or silently resolve account/authorization conflicts.
        for name, value in a.items():
            if name.startswith(('keys/', 'hashes/')) or name == 'users.db':
                if b.get(name) != value:
                    raise RuntimeError('Old key/hash/account missing or changed; manual reconciliation required')
            elif name not in ('client_binding.db', 'user_stats.db') and not name.startswith('logs/'):
                if b.get(name) != value:
                    raise RuntimeError('Unknown storage divergence')
        def bindings(path):
            rows = []
            for line in path.read_text().splitlines():
                fields = line.split('|')
                if len(fields) >= 5:
                    fields[4] = ''  # last-seen timestamp is not an authorization change
                rows.append('|'.join(fields))
            return sorted(rows)
        if bindings(old / 'client_binding.db') != bindings(new / 'client_binding.db'):
            raise RuntimeError('Client binding authorization differs; manual review required')
        config.write_bytes(original.replace(b'listen_port=9443', b'listen_port=7443')
                           .replace(b'admin_port=9444', b'admin_port=7444'))
        run('systemctl', 'start', 'encoder-server.service')
        for attempt in range(20):
            probe = subprocess.run(['python3', '-I', '/root/encoder-migration-20260927/check_parallel_tls.py',
                                    '--config', str(config), '--ports', '7443', '7444'], capture_output=True)
            if probe.returncode == 0:
                print(probe.stdout.decode().strip())
                break
            time.sleep(0.25)
        else:
            raise RuntimeError('Post-cutover TLS verification failed')
        run('systemctl', 'enable', 'encoder-server.service')
        run('systemctl', 'disable', 'cipheator.service')
        success = True
        print('OK: all old keys, hashes and accounts retained; new-only files:', len(set(b) - set(a)))
        print('Independent statistics/log histories preserved in both backups; new history remains active')
        print('Verified private backups:', backup)
        print('New server enabled on 7443/7444; old server stopped and disabled, NOT deleted')
    finally:
        if not success:
            run('systemctl', 'stop', 'encoder-server.service')
            config.write_bytes(original)
            run('systemctl', 'enable', 'cipheator.service')
            run('systemctl', 'disable', 'encoder-server.service')
            run('systemctl', 'start', 'cipheator.service', 'encoder-server.service')
            print('Rollback: original ports and both services restored; backups:', backup)

if __name__ == '__main__':
    main()
