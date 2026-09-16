"""Manual root-only test: temporary systemd socket activation and one real scan.

Requires the previously verified root-owned helper. Does not install or enable
production services. Only this test's uniquely named runtime units are removed.
"""
import hashlib
import os
import pathlib
import stat
import subprocess
import uuid

HELPER = pathlib.Path('/opt/encoder-scan-check.7B0K5n/helper')
EXPECTED = '81656af0edae0a09117de0fac730a1c394a7d484c094641a2145fb89d1dd55a7'

CLIENT = r'''
import socket, sys
with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as connection:
    connection.settimeout(30)
    connection.connect(sys.argv[1])
    connection.sendall(b'SCAN\n')
    connection.shutdown(socket.SHUT_WR)
    response = bytearray()
    while True:
        chunk = connection.recv(4096)
        if not chunk:
            break
        response.extend(chunk)
        if len(response) > 65536:
            raise SystemExit('FAILED: oversized response')
heading = b'OK\nbssid / frequency / signal level / flags / ssid\n'
if not response.startswith(heading):
    if response.startswith(b'ERROR\n'):
        raise SystemExit('FAILED: ' + response[6:262].decode('utf-8', errors='replace'))
    raise SystemExit('FAILED: missing or invalid response')
rows = sum(bool(line) for line in response[len(heading):].splitlines())
print(f'OK: socket activation and real scan passed as encoder-diag; {rows} result rows')
print('SSID/BSSID not printed; connection configuration unchanged')
'''


def control(*arguments, check=True):
    return subprocess.run(['/usr/bin/systemctl', *arguments], check=check,
                          text=True, capture_output=True, timeout=20)


def main():
    if os.geteuid() != 0:
        raise RuntimeError('Root required for this manual hardware test')
    for path in (HELPER.parent, HELPER):
        info = path.lstat()
        if info.st_uid != 0 or info.st_mode & 0o022 or stat.S_ISLNK(info.st_mode):
            raise RuntimeError('Unsafe helper ownership or permissions')
    if not stat.S_ISREG(HELPER.stat().st_mode) or hashlib.sha256(HELPER.read_bytes()).hexdigest() != EXPECTED:
        raise RuntimeError('Helper hash mismatch')
    prefix = 'encoder-scan-check-' + uuid.uuid4().hex[:12]
    endpoint = '/run/' + prefix + '.sock'
    socket_name = prefix + '.socket'
    socket_unit = pathlib.Path('/run/systemd/system') / socket_name
    service_unit = pathlib.Path('/run/systemd/system') / (prefix + '@.service')
    socket_text = f'''[Unit]
Description=Temporary encoder scan socket test
[Socket]
ListenStream={endpoint}
SocketUser=root
SocketGroup=encoder-diag
SocketMode=0660
Accept=yes
MaxConnections=4
Backlog=4
RemoveOnStop=yes
'''
    service_text = f'''[Unit]
Description=Temporary encoder scan helper test
[Service]
Type=exec
User=root
Group=encoder-diag
ExecStart={HELPER}
StandardInput=socket
StandardOutput=null
StandardError=journal
RuntimeDirectory=encoder-network
RuntimeDirectoryMode=0750
RuntimeDirectoryPreserve=yes
UMask=0077
RuntimeMaxSec=25
TimeoutStopSec=2
NoNewPrivileges=true
CapabilityBoundingSet=
PrivateTmp=true
PrivateDevices=true
ProtectHome=true
ProtectSystem=strict
ReadWritePaths=/run/encoder-network
RestrictAddressFamilies=AF_UNIX
MemoryMax=32M
TasksMax=8
'''
    created = []
    print('Temporary test unit:', prefix, flush=True)
    try:
        for path, text in ((service_unit, service_text), (socket_unit, socket_text)):
            with path.open('x', encoding='utf-8') as stream:
                created.append(path)
                stream.write(text)
            path.chmod(0o644)
        control('daemon-reload')
        control('start', socket_name)
        permissions = subprocess.run(['/usr/sbin/runuser', '-u', 'nobody', '--', '/usr/bin/python3', '-I', '-c',
            'import os,sys; sys.exit(1 if os.access(sys.argv[1], os.W_OK) else 0)', endpoint], timeout=5)
        if permissions.returncode:
            raise RuntimeError('Unexpected socket access for nobody')
        print('OK: socket is not writable by nobody', flush=True)
        subprocess.run(['/usr/sbin/runuser', '-u', 'encoder-diag', '--', '/usr/bin/python3', '-I', '-c',
                        CLIENT, endpoint], check=True, timeout=35)
    finally:
        # Stop only this invocation's socket and its uniquely named instances.
        control('stop', socket_name, check=False)
        listed = control('list-units', '--all', '--plain', '--no-legend', prefix + '@*.service', check=False)
        instances = [line.split()[0] for line in listed.stdout.splitlines() if line.split()
                     and line.split()[0].startswith(prefix + '@') and line.split()[0].endswith('.service')]
        if instances:
            control('stop', *instances)
            control('reset-failed', *instances, check=False)
        for path in created:
            path.unlink()
        control('daemon-reload')
        print('Temporary unit files removed; no production service installed or enabled', flush=True)


if __name__ == '__main__':
    try:
        main()
    except subprocess.CalledProcessError as error:
        raise SystemExit('FAILED: ' + (getattr(error, 'stderr', '') or str(error)).strip())
    except Exception as error:
        raise SystemExit(f'FAILED: {error}')
