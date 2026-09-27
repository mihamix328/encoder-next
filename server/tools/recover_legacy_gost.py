"""Recover ambiguous legacy Kuznechik CBC padding using a trusted original digest.

Offline migration utility, not the production decrypt endpoint. The original
ciphertext/key are read-only; output must be new. Never guesses without a digest.
"""
import argparse
import hashlib
import hmac
import os
from pathlib import Path
import subprocess
import tempfile

ALGORITHMS = {"sha256": "sha256", "sha512": "sha512", "sha3-256": "sha3_256",
              "sha3-512": "sha3_512", "blake2b-512": "blake2b"}


def recover(decoder, ciphertext, key, output, algorithm, expected):
    if algorithm not in ALGORITHMS:
        raise ValueError("Unsupported digest; use the actual stored hash algorithm")
    digest_name = ALGORITHMS[algorithm]
    digest_size = hashlib.new(digest_name).digest_size * 2
    if len(expected) != digest_size or any(c not in "0123456789abcdef" for c in expected):
        raise ValueError("Invalid trusted digest")
    if output.exists() or output.is_symlink():
        raise ValueError("Output already exists")
    if key.stat().st_size != 32:
        raise ValueError("Expected a 32-byte legacy key")
    length = ciphertext.stat().st_size
    if length < 16 or (length - 16) % 16 or length > 100 * 1024 * 1024 + 32:
        raise ValueError("Invalid legacy ciphertext length")
    with tempfile.TemporaryDirectory(prefix="encoder-gost-recovery-") as directory:
        raw = Path(directory) / "raw"
        result = subprocess.run([str(decoder.resolve()), str(ciphertext.resolve()), str(raw),
                                 str(key.resolve()), "--raw-blocks"],
                                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=60)
        if result.returncode or not raw.is_file() or raw.stat().st_size != length - 16:
            raise ValueError("Legacy decoder failed")
        data = raw.read_bytes()
        candidates = [len(data)]
        # Legacy encryption adds 1..15 bytes only for a partial block. A whole
        # 16-byte padding block never existed in that writer.
        if data:
            padding = data[-1]
            if 1 <= padding <= 15 and data.endswith(bytes([padding]) * padding):
                candidates.append(len(data) - padding)
        matches = [size for size in candidates
                   if hmac.compare_digest(hashlib.new(digest_name, memoryview(data)[:size]).hexdigest(), expected)]
        if len(matches) != 1:
            raise ValueError("No unique match with trusted digest; no output written")
        # Publish a fully written file atomically and without replacing an
        # existing output. Keep the temporary file on the same filesystem.
        fd, staged_name = tempfile.mkstemp(prefix=".encoder-recovery-", dir=output.parent)
        staged = Path(staged_name)
        try:
            with os.fdopen(fd, "wb") as stream:
                stream.write(memoryview(data)[:matches[0]])
                stream.flush()
                os.fsync(stream.fileno())
            os.link(staged, output)
        finally:
            staged.unlink(missing_ok=True)
    return matches[0]


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("decoder", "ciphertext", "key", "output"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--algorithm", choices=ALGORITHMS, required=True)
    parser.add_argument("--trusted-digest", required=True)
    args = parser.parse_args()
    try:
        recover(args.decoder, args.ciphertext, args.key, args.output, args.algorithm, args.trusted_digest)
    except (ValueError, OSError, subprocess.SubprocessError):
        parser.exit(1, "Recovery failed; check inputs and trusted digest. No unverified data is accepted.\n")
    print("Legacy file recovered and verified against trusted digest.")
