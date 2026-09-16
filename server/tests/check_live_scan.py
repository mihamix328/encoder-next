"""Manual root-only radio test. Never part of CTest; executes a real Wi-Fi scan.

Copy this file and the reviewed helper into a new root-owned /opt directory,
verify BOTH copied hashes, then run with python3 -I under the documented sandbox.
The helper must be named 'helper' beside this script. No network credentials or
SSID values are printed. This tests the helper over a socketpair, not installation
or systemd socket activation. It consumes the shared 30-second scan cooldown.
"""
import os
import pathlib
import socket
import stat
import subprocess


def main():
    if os.geteuid() != 0:
        raise RuntimeError('This manual hardware check requires root')
    directory = pathlib.Path(__file__).resolve().parent
    helper = directory / 'helper'
    for path in (directory, helper):
        info = path.lstat()
        if info.st_uid != 0 or info.st_mode & 0o022 or stat.S_ISLNK(info.st_mode):
            raise RuntimeError('Unsafe test directory or helper ownership/permissions')
    if not stat.S_ISREG(helper.stat().st_mode):
        raise RuntimeError('Helper is not a regular file')
    parent, child = socket.socketpair()
    process = None
    try:
        parent.settimeout(28)
        process = subprocess.Popen([str(helper)], stdin=child, stdout=subprocess.DEVNULL,
                                   stderr=subprocess.DEVNULL, close_fds=True)
        child.close()
        parent.sendall(b'SCAN\n')
        parent.shutdown(socket.SHUT_WR)
        response = bytearray()
        while True:
            chunk = parent.recv(4096)
            if not chunk:
                break
            response.extend(chunk)
            if len(response) > 65536:
                raise RuntimeError('Oversized helper response')
        code = process.wait(timeout=2)
        heading = b'OK\nbssid / frequency / signal level / flags / ssid\n'
        if code != 0:
            raise RuntimeError(f'Helper exited with code {code}')
        if not response.startswith(heading):
            if response.startswith(b'ERROR\n'):
                raise RuntimeError(response[6:262].decode('utf-8', errors='replace'))
            raise RuntimeError('Invalid helper response')
        rows = len([line for line in response[len(heading):].splitlines() if line])
        print(f'OK: real scan completion observed; {rows} result rows; helper exited 0')
        print('SSID/BSSID not printed; no connection configuration changed')
        print('Socket activation and installed server/UI are NOT tested by this check')
    finally:
        parent.close()
        child.close()
        if process is not None and process.poll() is None:
            process.kill()
            process.wait(timeout=3)


if __name__ == '__main__':
    try:
        main()
    except Exception as error:
        raise SystemExit(f'FAILED: {error}')
