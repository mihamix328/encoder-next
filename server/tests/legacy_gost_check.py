"""Diagnostic only: no real files/keys; do not expose legacy CLI secret output."""
import pathlib
import subprocess
import sys
import tempfile

failed = 0
with tempfile.TemporaryDirectory(prefix="encoder-legacy-gost-") as temporary:
    root = pathlib.Path(temporary)
    for size in (0, 1, 15, 16, 17, 31, 32, 64):
        data = bytes((i * 17 + 3) % 256 for i in range(size))
        if size and size % 16 == 0:
            data = data[:-1] + b"\x01"
        source, cipher, key, restored = (root / name for name in ("in", "cipher", "key", "out"))
        source.write_bytes(data)
        results = []
        for command in ([sys.argv[1], str(source), str(cipher), str(key)],
                        [sys.argv[2], str(cipher), str(restored), str(key)]):
            result = subprocess.run(command, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=10)
            results.append(result.returncode)
        ok = results == [0, 0] and restored.exists() and restored.read_bytes() == data
        print(f"legacy GOST size={size}: {'PASS' if ok else 'FAIL'}")
        failed += not ok
        if restored.exists():
            restored.unlink()
sys.exit(1 if failed else 0)
