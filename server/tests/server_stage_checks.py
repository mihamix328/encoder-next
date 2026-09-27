"""Exercise staging against real CI binaries, entirely in a temporary directory."""
import importlib.util
from pathlib import Path
import sys
import tempfile

script = Path(__file__).resolve().parents[2] / "deploy/stage_server.py"
spec = importlib.util.spec_from_file_location("stage_server", script)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
with tempfile.TemporaryDirectory(prefix="encoder-stage-test-") as temporary:
    output = Path(temporary) / "image"
    manifest = module.stage(Path(sys.argv[1]).resolve(), output, script.parent)
    assert "opt/encoder/bin/encoder-server" in manifest
    assert not (output / "etc/encoder/server.conf").exists()
    assert not list(output.rglob("*.key"))
    assert not (output / "usr/lib/systemd/system/encoder-wifi-change.service").exists()
    try:
        module.stage(Path(sys.argv[1]).resolve(), output, script.parent)
    except ValueError:
        pass
    else:
        raise AssertionError("Existing destination was accepted")
print("Offline staging, no-overwrite and exclusion of active network units passed")
