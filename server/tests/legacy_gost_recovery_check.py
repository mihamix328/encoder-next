"""Real libakrypt legacy format tests; synthetic files only, no secret output."""
import hashlib
import importlib.util
from pathlib import Path
import subprocess
import sys
import tempfile

spec = importlib.util.spec_from_file_location("recovery", Path(__file__).resolve().parents[1] / "tools/recover_legacy_gost.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
encoder, decoder = map(lambda value: Path(value).resolve(), sys.argv[1:3])
count = 0
with tempfile.TemporaryDirectory(prefix="encoder-legacy-recovery-test-") as directory:
    root = Path(directory)
    for size in (0, 1, 15, 16, 17, 31, 32, 64, 257):
        for ending in (1, 2, 15, 16, 255):
            data = bytes([ending]) * size
            source, cipher, key, output = (root / name for name in ("in", "cipher", "key", "out"))
            source.write_bytes(data)
            subprocess.run([str(encoder), str(source), str(cipher), str(key)], check=True,
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=10)
            before = cipher.read_bytes(), key.read_bytes()
            digest = hashlib.sha256(data).hexdigest()
            module.recover(decoder, cipher, key, output, "sha256", digest)
            assert output.read_bytes() == data
            if len(sys.argv) == 4:
                subprocess.run([str(Path(sys.argv[3]).resolve()), str(cipher), str(key), str(source)],
                               check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=10)
            try:
                module.recover(decoder, cipher, key, output, "sha256", digest)
            except ValueError:
                pass
            else:
                raise AssertionError("Existing output overwritten")
            output.unlink()
            try:
                module.recover(decoder, cipher, key, output, "sha256", "0" * 64)
            except ValueError:
                pass
            else:
                raise AssertionError("Wrong digest accepted")
            assert not output.exists() and before == (cipher.read_bytes(), key.read_bytes())
            count += 1
print(f"Legacy verified recovery: {count} cases passed; wrong digests and overwrite rejected")
