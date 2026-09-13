"""Isolated TLS integration test; never connects to an Orange Pi or real user database."""
import argparse
import sys
import hashlib
import pathlib
import socket
import ssl
import subprocess
import tempfile
import time

parser = argparse.ArgumentParser()
parser.add_argument('--server', required=True)
parser.add_argument('--openssl', required=True)
args = parser.parse_args()
server = str(pathlib.Path(args.server).resolve())

def free_port():
    with socket.socket() as sock:
        sock.bind(('127.0.0.1', 0))
        return sock.getsockname()[1]

with tempfile.TemporaryDirectory(prefix='encoder-test-') as temporary:
    root = pathlib.Path(temporary)
    (root / 'config').mkdir()
    cert, key = root / 'server.crt', root / 'server.key'
    subprocess.run([args.openssl, 'req', '-x509', '-newkey', 'rsa:2048', '-nodes',
                    '-keyout', str(key), '-out', str(cert), '-days', '1',
                    '-subj', '/CN=localhost', '-addext', 'subjectAltName=DNS:localhost'],
                   check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    client_port, admin_port = free_port(), free_port()
    while admin_port == client_port:
        admin_port = free_port()
    (root / 'config/server.conf').write_text(
        f'listen_host=127.0.0.1\nlisten_port={client_port}\n'
        f'admin_host=127.0.0.1\nadmin_port={admin_port}\nadmin_token=test-token\n'
        f'cert_file={cert.as_posix()}\nkey_file={key.as_posix()}\nstorage_dir=storage\n', encoding='utf-8')
    subprocess.run([server, '--init-user', 'tester', 'old-test-password'], cwd=root,
                   check=True, stdout=subprocess.DEVNULL)
    # Exercise compatibility with the original three-field user database.
    assert subprocess.run([server, '--help'], cwd=root, capture_output=True).returncode == 0
    for arguments in (['--config'], ['--unknown'], ['--config', 'missing.conf']):
        assert subprocess.run([server, *arguments], cwd=root, capture_output=True).returncode != 0
    # Explicit configuration must work without the default config file.
    explicit_config = root / 'explicit-server.conf'
    (root / 'config/server.conf').rename(explicit_config)
    database = root / 'storage/users.db'
    database.write_text(':'.join(database.read_text().strip().split(':')[:3]) + '\n')
    context = ssl.create_default_context(cafile=str(cert))
    def request(port, fields, body=b'', include_payload=False):
        with socket.create_connection(('127.0.0.1', port), timeout=3) as raw:
            with context.wrap_socket(raw, server_hostname='localhost') as stream:
                stream.sendall((''.join(f'{k}: {v}\n' for k, v in fields.items()) + '\n').encode())
                if body:
                    stream.sendall(body)
                header = b''
                while not header.endswith(b'\n\n'):
                    chunk = stream.recv(1)
                    if not chunk:
                        raise RuntimeError('incomplete response')
                    header += chunk
                    if len(header) > 65536:
                        raise RuntimeError('oversized response')
                values = dict(line.split(': ', 1) for line in header.decode().strip().splitlines())
                remaining = int(values.get('payload_size', values.get('enc_size', values.get('plain_size', '0'))))
                if remaining < 0 or remaining > 8 * 1024 * 1024:
                    raise RuntimeError('oversized payload')
                payload = bytearray()
                while remaining:
                    chunk = stream.recv(remaining)
                    if not chunk:
                        raise RuntimeError('incomplete payload')
                    remaining -= len(chunk)
                    payload.extend(chunk)
                return (values, bytes(payload)) if include_payload else values
    with (root / 'server-output.log').open('w') as output:
        process = subprocess.Popen([server, '--config', str(explicit_config)], cwd=root, stdout=output, stderr=output)
        try:
            deadline = time.monotonic() + 10
            while True:
                try:
                    reply = request(admin_port, {'op': 'admin_get_stats', 'admin_token': 'test-token'})
                    break
                except OSError:
                    if time.monotonic() > deadline or process.poll() is not None:
                        raise
                    time.sleep(0.1)
            assert reply['status'] == 'ok'
            for port in (client_port, admin_port):
                for operation in ('admin_wifi_results', 'admin_wifi_status'):
                    wifi = request(port, dict(op=operation, admin_token='wrong'))
                    assert wifi['status'] == 'error' and wifi['message'] == 'Unauthorized'
                    wifi = request(port, dict(op=operation, admin_token='test-token'))
                    assert wifi['status'] == 'error' and 'disabled' in wifi['message']
                denied = request(port, dict(op='admin_network_status', admin_token='wrong'))
                assert denied['status'] == 'error' and denied['message'] == 'Unauthorized'
                network = request(port, dict(op='admin_network_status', admin_token='test-token'))
                if sys.platform.startswith('linux'):
                    assert network['status'] == 'ok' and int(network['payload_size']) > 0
                else:
                    assert network['status'] == 'error'
                    assert 'only on Linux' in network['message']
            for port in (client_port, admin_port):
                for operation in ('admin_get_alerts', 'admin_get_logs', 'admin_get_stats', 'admin_get_locks', 'admin_get_binding'):
                    assert request(port, {'op': operation, 'admin_token': 'test-token'})['status'] == 'ok', operation
                assert request(port, {'op': 'admin_get_binding', 'admin_token': 'wrong'})['message'] == 'Unauthorized'
            assert request(admin_port, {'op': 'auth_check'})['status'] == 'error'
            auth = {'op': 'auth_check', 'username': 'tester', 'password': 'old-test-password', 'client_id': 'integration-test'}
            assert request(client_port, auth)['status'] == 'ok'
            assert request(client_port, dict(auth, op='change_password', new_password='new-test-password'))['status'] == 'ok'
            assert request(client_port, dict(auth, password='new-test-password'))['status'] == 'ok'
            assert request(client_port, auth)['status'] == 'error'
            for storage in ('client', 'server'):
                for plaintext in (b'', b'encoder round trip\x00\xff' * 31):
                    fields = dict(auth, password='new-test-password', op='encrypt',
                                  cipher='aes-256-gcm', hash='sha256', file_name='test.bin',
                                  key_storage=storage, file_size=str(len(plaintext)))
                    encrypted, ciphertext = request(client_port, fields, plaintext, True)
                    assert encrypted['status'] == 'ok', encrypted
                    assert encrypted['hash_value'] == hashlib.sha256(plaintext).hexdigest()
                    fields.update(op='decrypt', file_size=str(len(ciphertext)), file_id=encrypted['file_id'])
                    for field in ('key', 'key_id', 'iv', 'tag'):
                        if field in encrypted:
                            fields[field] = encrypted[field]
                    decrypted, restored = request(client_port, fields, ciphertext, True)
                    assert decrypted['status'] == 'ok' and restored == plaintext, decrypted
                    if ciphertext:
                        damaged = bytes([ciphertext[0] ^ 1]) + ciphertext[1:]
                        assert request(client_port, fields, damaged)['status'] == 'error'
            print('AES-256-GCM round trips passed: client/server keys, empty/binary data, tampering rejection')
            def manage(operation, **fields):
                return request(admin_port, dict(op=operation, admin_token='test-token', **fields))
            assert manage('admin_create_user', username='second', new_password='second-password')['status'] == 'ok'
            assert manage('admin_create_user', username='second', new_password='other-password')['status'] == 'error'
            assert manage('admin_create_user', username='invalid:user', new_password='second-password')['status'] == 'error'
            assert request(admin_port, dict(op='admin_create_user', admin_token='wrong', username='intruder', new_password='second-password'))['status'] == 'error'
            second = dict(op='auth_check', username='second', password='second-password', client_id='integration-test')
            assert request(client_port, second)['status'] == 'ok'
            assert manage('admin_block_user', username='second', blocked='1')['status'] == 'ok'
            assert request(client_port, second)['status'] == 'error'
            assert manage('admin_reset_password', username='second', new_password='reset-password')['status'] == 'ok'
            assert request(client_port, dict(second, password='reset-password'))['status'] == 'error'
            assert manage('admin_block_user', username='second', blocked='0')['status'] == 'ok'
            assert request(client_port, dict(second, password='reset-password'))['status'] == 'ok'
            assert request(client_port, second)['status'] == 'error'
            assert manage('admin_list_users')['user_count'] == '2'
            for rights in range(4):
                assert manage('admin_set_permissions', username='second', permissions=str(rights))['status'] == 'ok'
                assert request(client_port, dict(second, password='reset-password'))['permissions'] == str(rights)
                for operation, bit, denied in (('encrypt', 1, 'Encryption'), ('decrypt', 2, 'Decryption')):
                    response = request(client_port, dict(second, password='reset-password', op=operation, file_size='0', cipher='aes-256-gcm', hash='sha256'))
                    if not rights & bit:
                        assert response['message'] == f'{denied} is not allowed for this account', response
                    else:
                        assert 'not allowed for this account' not in response.get('message', ''), response
            assert manage('admin_set_permissions', username='second', permissions='4')['status'] == 'error'
            assert request(admin_port, dict(op='admin_set_permissions', admin_token='wrong', username='second', permissions='3'))['status'] == 'error'
            assert manage('admin_set_permissions', username='second', permissions='1')['status'] == 'ok'
            assert manage('admin_block_user', username='second', blocked='1')['status'] == 'ok'
            process.terminate()
            process.wait(timeout=10)
            snapshot = root / 'wifi.txt'
            with explicit_config.open('a', encoding='utf-8') as config:
                config.write(f'wifi_read_enabled=true\nwifi_snapshot_file={snapshot.as_posix()}\n')
            process = subprocess.Popen([server, '--config', str(explicit_config)], cwd=root, stdout=output, stderr=output)
            deadline = time.monotonic() + 10
            while True:
                try:
                    assert manage('admin_list_users')['user_count'] == '2'
                    break
                except OSError:
                    if time.monotonic() > deadline: raise
                    time.sleep(0.1)
            assert request(client_port, dict(second, password='reset-password'))['status'] == 'error'
            assert manage('admin_wifi_results')['status'] == 'error'  # Missing snapshot.
            heading = 'bssid / frequency / signal level / flags / ssid\n'
            for body in ('invalid', 'x' * 65536,
                         f'encoder-wifi-v1 {int(time.time()) - 120}\n{heading}',
                         f'encoder-wifi-v1 {int(time.time()) + 3600}\n{heading}',
                         f'encoder-wifi-v1 {int(time.time())}\nFAIL\n'):
                snapshot.write_text(body, encoding='utf-8', newline='\n')
                assert manage('admin_wifi_results')['status'] == 'error'
            snapshot.write_text(f'encoder-wifi-v1 {int(time.time())}\n{heading}', encoding='utf-8', newline='\n')
            assert manage('admin_wifi_results')['status'] == 'ok'
            assert manage('admin_wifi_status')['status'] == 'error'  # Legacy v1 has no connection status.
            for status in ('wpa_state=COMPLETED\nssid=Test network\npassword=must-not-leak\n',
                           'wpa_state=DISCONNECTED\n'):
                snapshot.write_text(f'encoder-wifi-v2 {int(time.time())}\n{status}\n{heading}',
                                    encoding='utf-8', newline='\n')
                response, data = request(admin_port, dict(op='admin_wifi_status', admin_token='test-token'), include_payload=True)
                assert response['status'] == 'ok' and b'wpa_state=' in data
                assert b'password' not in data and b'must-not-leak' not in data
                assert manage('admin_wifi_results')['status'] == 'ok'
            for status in ('wpa_state=INVALID\n', 'wpa_state=COMPLETED\nwpa_state=DISCONNECTED\n',
                           'wpa_state=COMPLETED\nssid=bad\tvalue\n'):
                snapshot.write_text(f'encoder-wifi-v2 {int(time.time())}\n{status}\n{heading}',
                                    encoding='utf-8', newline='\n')
                assert manage('admin_wifi_status')['status'] == 'error'
            assert request(admin_port, dict(op='admin_wifi_status', admin_token='wrong'))['message'] == 'Unauthorized'
            print('Wi-Fi v2 status passed: connected/disconnected, whitelist, malformed fields and v1 compatibility')
            print('Wi-Fi snapshots passed: missing, malformed, oversized, stale, future and valid')
            assert manage('admin_block_user', username='second', blocked='0')['status'] == 'ok'
            assert request(client_port, dict(second, password='reset-password'))['status'] == 'ok'
            assert request(client_port, dict(second, password='reset-password', op='decrypt', file_size='0'))['message'] == 'Decryption is not allowed for this account'
            print('Permissions passed: four permission combinations, token rejection, invalid mask and persistence')
            print('User management passed: legacy DB, create, duplicate rejection, token checks, reset, block and restart persistence')
            print('TLS integration passed: both admin ports, token rejection, password change and authentication')
        finally:
            process.terminate()
            process.wait(timeout=10)
