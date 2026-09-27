"""Build a fresh offline Linux installation image. Never starts services or changes /etc.

This is deliberately staging, not an upgrade of a live installation. The output
is reviewed and transferred during the separately approved board migration.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil


def stage(build: Path, output: Path, templates: Path):
    if output.exists() or output.is_symlink():
        raise ValueError("Output must not exist; no overwrite is permitted")
    names = ("encoder-server", "encoder-wifi-collector", "encoder-wifi-scan-helper",
             "encoder-wifi-policy-guard", "encoder-wifi-recovery-supervisor", "encoder-wifi-change-helper")
    sources = []
    for name in names:
        source = build / "server" / name
        if source.is_symlink() or not source.is_file():
            raise ValueError("Missing regular Linux binary: " + name)
        with source.open("rb") as stream:
            header = stream.read(20)
        if len(header) != 20 or header[:6] != b"\x7fELF\x02\x01":
            raise ValueError("Expected 64-bit little-endian ELF: " + name)
        machine = int.from_bytes(header[18:20], "little")
        if machine not in (62, 183):
            raise ValueError("Unsupported ELF architecture")
        sources.append((source, name, machine))
    if len({item[2] for item in sources}) != 1:
        raise ValueError("Mixed binary architectures")
    # Validate every input before producing an output directory.
    required = [templates / "server.conf.example", templates / "encoder-server.service",
                templates.parent / "server/tools/netplan_preflight.py"]
    for source in required:
        if source.is_symlink() or not source.is_file():
            raise ValueError("Missing regular installation template")
    output.mkdir(parents=True, mode=0o700)
    binary_dir = output / "opt/encoder/bin"
    binary_dir.mkdir(parents=True)
    for source, name, _ in sources:
        shutil.copyfile(source, binary_dir / name)
        (binary_dir / name).chmod(0o755)
    libexec = output / "opt/encoder/libexec"
    libexec.mkdir(parents=True)
    shutil.copyfile(required[2], libexec / "netplan_preflight.py")
    (libexec / "netplan_preflight.py").chmod(0o644)
    config = output / "etc/encoder"
    config.mkdir(parents=True, mode=0o750)
    shutil.copyfile(required[0], config / "server.conf.example")
    (config / "server.conf.example").chmod(0o640)
    units = output / "usr/lib/systemd/system"
    units.mkdir(parents=True)
    shutil.copyfile(required[1], units / "encoder-server.service")
    # Experimental networking units are reference material, not installed units.
    shutil.copytree(templates / "experimental", output / "review-only-network-units")
    manifest = {str(path.relative_to(output)): hashlib.sha256(path.read_bytes()).hexdigest()
                for path in sorted(output.rglob("*")) if path.is_file()}
    (output / "MANIFEST.json").write_text(json.dumps({"architecture": sources[0][2],
        "files": manifest, "ready_for_board": False}, indent=2), encoding="utf-8")
    return manifest


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    try:
        stage(args.build.resolve(), args.output.absolute(), Path(__file__).resolve().parent)
    except (ValueError, OSError) as error:
        parser.exit(1, str(error) + "\n")
    print("Offline image prepared. No accounts, certificates, services or networks changed.")
