"""Audit release archives without extracting or running their content."""
import argparse
import csv
import hashlib
import json
from pathlib import Path, PurePosixPath
import zipfile

def audit_zip(path, personal):
    with zipfile.ZipFile(path) as archive:
        if archive.testzip() is not None:
            raise ValueError('ZIP integrity failed')
        names = archive.namelist()
        if len(names) != len(set(names)):
            raise ValueError('Duplicate archive entry')
        for name in names:
            normalized = PurePosixPath(name.replace('\\', '/'))
            if normalized.is_absolute() or '..' in normalized.parts:
                raise ValueError('Unsafe archive path')
            if normalized.suffix.lower() in ('.key', '.db', '.log', '.pfx', '.p12') or normalized.name == 'admin_devices.conf':
                raise ValueError('Private/local data included: ' + name)
            if normalized.suffix.lower() in ('.crt', '.pem'):
                if not personal or str(normalized) != 'config/server.crt':
                    raise ValueError('Unexpected certificate')
                data = archive.read(name)
                if b'PRIVATE KEY' in data or b'BEGIN CERTIFICATE' not in data:
                    raise ValueError('Invalid public certificate')
            if normalized.suffix.lower() in ('.conf', '.md', '.txt', '.json'):
                if b'PRIVATE KEY-----' in archive.read(name):
                    raise ValueError('Private key marker')
        app = 'admin' if 'encoder-admin' in path.name else 'client'
        required = {f'encoder-{app}.exe', 'Qt6Core.dll', 'Qt6Gui.dll', 'Qt6Widgets.dll',
                    'libcrypto-3-x64.dll', 'libssl-3-x64.dll', 'platforms/qwindows.dll',
                    'vcruntime140.dll', 'START-HERE.md', 'BUILD-INFO.txt', f'config/{app}.conf'}
        if not required.issubset(names) or not any(name.startswith('licenses/') for name in names):
            raise ValueError('Required runtime, configuration or license notices missing')
        if app == 'client':
            config = archive.read('config/client.conf').decode('utf-8-sig')
            if 'verify_peer=true' not in config or 'server_port=7443' not in config:
                raise ValueError('Unexpected client TLS/port defaults')
            host = '172.10.0.2' if personal else '127.0.0.1'
            if 'server_host=' + host not in config:
                raise ValueError('Unexpected client address')
        return len(names)

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('directory', type=Path)
    args = parser.parse_args()
    source = args.directory / 'source'
    with (source / 'EXPORT-MANIFEST.csv').open(encoding='utf-8-sig', newline='') as stream:
        rows = list(csv.DictReader(stream))
    expected = set()
    for row in rows:
        relative = PurePosixPath(row['File'].replace('\\', '/'))
        if relative.is_absolute() or '..' in relative.parts:
            raise ValueError('Unsafe source manifest path')
        path = source.joinpath(*relative.parts)
        if path.is_symlink() or hashlib.sha256(path.read_bytes()).hexdigest().lower() != row['SHA256'].lower():
            raise ValueError('Source manifest mismatch: ' + str(relative))
        if path.suffix.lower() in ('.key', '.crt', '.pem', '.db', '.log', '.exe', '.dll', '.zip'):
            raise ValueError('Forbidden source artifact')
        expected.add(str(relative))
    actual = {p.relative_to(source).as_posix() for p in source.rglob('*') if p.is_file()}
    if actual != expected | {'EXPORT-MANIFEST.csv'}:
        raise ValueError('Unexpected or missing source files')
    print('Source manifest verified:', len(rows), 'files')
    result = []
    for folder, personal in (('public-windows', False), ('your-device', True)):
        for app in ('client', 'admin'):
            path = args.directory / folder / f'encoder-{app}-windows-x64.zip'
            count = audit_zip(path, personal)
            result.append({'file': str(path.relative_to(args.directory)), 'entries': count,
                           'sha256': hashlib.sha256(path.read_bytes()).hexdigest()})
    print(json.dumps(result, indent=2))

if __name__ == '__main__':
    main()
