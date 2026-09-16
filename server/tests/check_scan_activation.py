"""Manual root-only test: temporary systemd socket activation and one real scan.

Requires the previously verified root-owned helper. Does not install or enable
production services. Only this test's uniquely named runtime units are removed.
"""
import hashlib
import os
import pathlib
import stat
import socket
import subprocess
import sys
import tempfile
import threading
import uuid

HELPER = pathlib.Path('/opt/encoder-scan-check.7B0K5n/helper')
EXPECTED = '81656af0edae0a09117de0fac730a1c394a7d484c094641a2145fb89d1dd55a7'
SERVER = '/home/encoder-diag/encoder-v2-check-ao3M9r/build/server/encoder-server'
SERVER_HASH = '4fae0d79cc4c614d9977041c8a3d4bca6dad91cfef528e3259f5a9c5df8278fa'

TLS_CLIENT = r'''
import os, pathlib, secrets, socket, ssl, subprocess, sys, tempfile, time
if os.geteuid() == 0:
    raise SystemExit('FAILED: test server must not run as root')
server, endpoint = sys.argv[1:]
def free_port():
    with socket.socket() as sock:
        sock.bind(('127.0.0.1', 0))
        return sock.getsockname()[1]
with tempfile.TemporaryDirectory(prefix='encoder-live-tls-') as temporary:
    root = pathlib.Path(temporary)
    cert, key = root / 'server.crt', root / 'server.key'
    subprocess.run(['/usr/bin/openssl', 'req', '-x509', '-newkey', 'rsa:2048', '-nodes',
        '-keyout', str(key), '-out', str(cert), '-days', '1', '-subj', '/CN=localhost',
        '-addext', 'subjectAltName=DNS:localhost'], check=True, timeout=15,
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    port, admin_port = free_port(), free_port()
    while port == admin_port:
        admin_port = free_port()
    token = secrets.token_hex(24)
    (root / 'config').mkdir()
    config = root / 'config/server.conf'
    config.write_text(f'listen_host=127.0.0.1\nlisten_port={port}\n'
        f'admin_host=127.0.0.1\nadmin_port={admin_port}\nadmin_token={token}\n'
        f'cert_file={cert}\nkey_file={key}\nstorage_dir={root / "storage"}\n'
        f'wifi_scan_enabled=true\nwifi_scan_socket={endpoint}\n', encoding='utf-8')
    config.chmod(0o600)
    key.chmod(0o600)
    subprocess.run([server, '--config', str(config), '--init-user', 'scan-test', 'temporary-test-password'],
        cwd=root, check=True, timeout=10, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    context = ssl.create_default_context(cafile=str(cert))
    def request(operation, credential):
        with socket.create_connection(('127.0.0.1', admin_port), timeout=3) as raw:
            raw.settimeout(35)
            with context.wrap_socket(raw, server_hostname='localhost') as stream:
                stream.sendall(f'op: {operation}\nadmin_token: {credential}\n\n'.encode())
                header = bytearray()
                while not header.endswith(b'\n\n'):
                    chunk = stream.recv(1)
                    if not chunk or len(header) >= 65536:
                        raise RuntimeError('Invalid TLS response header')
                    header.extend(chunk)
                fields = dict(line.split(': ', 1) for line in header.decode().strip().splitlines())
                size = int(fields.get('payload_size', '0'))
                if not 0 <= size <= 65536:
                    raise RuntimeError('Invalid TLS payload size')
                payload = bytearray()
                while len(payload) < size:
                    chunk = stream.recv(size - len(payload))
                    if not chunk:
                        raise RuntimeError('Truncated TLS payload')
                    payload.extend(chunk)
                return fields, payload
    process = subprocess.Popen([server, '--config', str(config)], cwd=root,
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        deadline = time.monotonic() + 10
        while True:
            try:
                fields, _ = request('admin_get_stats', token)
                if fields.get('status') != 'ok':
                    raise RuntimeError('Test server readiness failed')
                break
            except OSError:
                if time.monotonic() >= deadline or process.poll() is not None:
                    raise
                time.sleep(0.1)
        fields, _ = request('admin_wifi_scan', 'wrong-token')
        if fields.get('message') != 'Unauthorized':
            raise RuntimeError('Invalid admin token was not rejected')
        print('OK: TLS certificate verified; wrong admin token rejected', flush=True)
        fields, payload = request('admin_wifi_scan', token)
        heading = b'bssid / frequency / signal level / flags / ssid\n'
        if fields.get('status') != 'ok' or not payload.startswith(heading):
            raise RuntimeError('Scan through TLS failed: ' + fields.get('message', 'invalid response')[:256])
        rows = sum(bool(line) for line in payload[len(heading):].splitlines())
        print(f'OK: TLS -> unprivileged test server -> scan helper; {rows} result rows', flush=True)
        fields, _ = request('admin_wifi_scan', token)
        if fields.get('status') != 'error' or 'cooldown' not in fields.get('message', '').lower():
            raise RuntimeError('Immediate repeat was not rejected by cooldown')
        print('OK: immediate repeat rejected by scan cooldown; no second scan requested', flush=True)
    finally:
        process.terminate()
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=3)
    print('Test server stopped; temporary certificate, token and database removed', flush=True)
    print('Production server unchanged; Windows GUI is NOT tested by this script', flush=True)
'''

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
    if sys.argv[1:] not in ([], ['--tls']):
        raise RuntimeError('Usage: check_scan_activation.py [--tls]')
    tls = sys.argv[1:] == ['--tls']
    if os.geteuid() != 0:
        raise RuntimeError('Root required for this manual hardware test')
    for path in (HELPER.parent, HELPER):
        info = path.lstat()
        if info.st_uid != 0 or info.st_mode & 0o022 or stat.S_ISLNK(info.st_mode):
            raise RuntimeError('Unsafe helper ownership or permissions')
    if not stat.S_ISREG(HELPER.stat().st_mode) or hashlib.sha256(HELPER.read_bytes()).hexdigest() != EXPECTED:
        raise RuntimeError('Helper hash mismatch')
    if tls and hashlib.sha256(pathlib.Path(SERVER).read_bytes()).hexdigest() != SERVER_HASH:
        raise RuntimeError('Test server hash mismatch')
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
                        TLS_CLIENT if tls else CLIENT, *([SERVER] if tls else []), endpoint],
                       check=True, timeout=100 if tls else 35)
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
        if sys.argv[1:] == ['--mock-tls']:
            # Unprivileged dry run: no systemd changes, root, or real radio.
            if os.geteuid() == 0:
                raise RuntimeError('Run mock test without root')
            with tempfile.TemporaryDirectory(prefix='encoder-tls-mock-') as temporary:
                path = temporary + '/helper.sock'
                commands = []
                with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as listener:
                    listener.bind(path)
                    listener.listen(2)
                    listener.settimeout(30)
                    def fake():
                        for response in (b'OK\nbssid / frequency / signal level / flags / ssid\n',
                                         b'ERROR\nScan cooldown'):
                            with listener.accept()[0] as connection:
                                connection.settimeout(3)
                                data = bytearray()
                                while chunk := connection.recv(64):
                                    data.extend(chunk)
                                commands.append(bytes(data))
                                connection.sendall(response)
                    thread = threading.Thread(target=fake, daemon=True)
                    thread.start()
                    subprocess.run(['/usr/bin/python3', '-I', '-c', TLS_CLIENT, SERVER, path], check=True, timeout=60)
                    thread.join(timeout=5)
                    if thread.is_alive() or commands != [b'SCAN\n', b'SCAN\n']:
                        raise RuntimeError('Unexpected helper requests')
                print('Mock TLS scenario passed; no radio or systemd accessed')
        else:
            main()
    except subprocess.CalledProcessError as error:
        raise SystemExit('FAILED: ' + (getattr(error, 'stderr', '') or str(error)).strip())
    except Exception as error:
        raise SystemExit(f'FAILED: {error}')
