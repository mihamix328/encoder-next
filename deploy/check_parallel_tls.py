"""Read-only TLS checks. Does not print credentials, account names or payloads."""
import argparse
from pathlib import Path
import socket
import ssl

def request(context, host, port, operation, token):
    with socket.create_connection((host, port), timeout=8) as raw:
        with context.wrap_socket(raw, server_hostname=host) as stream:
            stream.sendall(f'op: {operation}\nadmin_token: {token}\n\n'.encode())
            header = b''
            while not header.endswith(b'\n\n'):
                chunk = stream.recv(1)
                if not chunk or len(header) >= 65536:
                    raise RuntimeError('Incomplete or oversized reply')
                header += chunk
            fields = dict(line.split(': ', 1) for line in header.decode().strip().splitlines())
            remaining = int(fields.get('payload_size', '0'))
            if not 0 <= remaining <= 8 * 1024 * 1024:
                raise RuntimeError('Oversized payload')
            while remaining:
                chunk = stream.recv(min(65536, remaining))
                if not chunk:
                    raise RuntimeError('Incomplete payload')
                remaining -= len(chunk)
            return fields

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--host', default='127.0.0.1')
    parser.add_argument('--cert', default='/etc/encoder/certs/server.crt')
    parser.add_argument('--config')
    parser.add_argument('--ports', type=int, nargs=2, default=(9443, 9444))
    args = parser.parse_args()
    context = ssl.create_default_context(cafile=args.cert)
    token = None
    if args.config:
        values = dict(line.split('=', 1) for line in Path(args.config).read_text().splitlines()
                      if '=' in line and not line.startswith('#'))
        token = values['admin_token']
    for port in args.ports:
        denied = request(context, args.host, port, 'admin_list_users', 'invalid-probe-token')
        if denied.get('status') != 'error' or denied.get('message') != 'Unauthorized':
            raise RuntimeError('Invalid token was not rejected')
        if token:
            users = request(context, args.host, port, 'admin_list_users', token)
            if users.get('status') != 'ok':
                raise RuntimeError('User database check failed')
            print(f'OK port {port}: verified TLS, token checks, user count {users.get("user_count")}')
        else:
            print(f'OK port {port}: verified certificate/address and rejected invalid token')

if __name__ == '__main__':
    main()
