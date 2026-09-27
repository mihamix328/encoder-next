"""Diagnostic redaction checks; no board access or network changes."""
import importlib.util
from pathlib import Path
import yaml
import tempfile

source = Path(__file__).resolve().parents[2] / 'deploy/prepare_wifi_migration.py'
spec = importlib.util.spec_from_file_location('migration', source)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
try:
    module.require(False, 'Unsupported renderer')
except module.SafeRefusal as error:
    assert 'Unsupported renderer' in module.failure_message(error)
else:
    raise AssertionError('Expected refusal')
module.STAGE = 'parse-and-check-profile'
for error in (RuntimeError('SECRET_PASSWORD'), yaml.YAMLError('SECRET_PASSWORD'),
              KeyError('SECRET_PASSWORD')):
    message = module.failure_message(error)
    assert 'SECRET_PASSWORD' not in message
    assert 'parse-and-check-profile' in message
print('OK: fixed refusal reason visible; unexpected exception details redacted')
for network in ({'wifis': {}}, {'version': 2, 'wifis': {}}):
    assert module.validate_network_scope({'network': network}) == network
for document in (
    {'network': {'version': None, 'wifis': {}}},
    {'network': {'version': 1, 'wifis': {}}},
    {'network': {'version': '2', 'wifis': {}}},
    {'network': {'wifis': {}, 'ethernets': {}}},
    {'network': {'wifis': {}}, 'extra': {}},
    {'network': None}, {},
):
    try:
        module.validate_network_scope(document)
    except module.SafeRefusal:
        pass
    else:
        raise AssertionError('Invalid migration scope accepted')
print('OK: omitted version accepted; explicit invalid version and extra scope rejected')
with tempfile.TemporaryDirectory() as temporary:
    root = Path(temporary)
    original = root / 'source'
    original.mkdir()
    (original / '10-config.yaml').write_text('network: {version: 2}\n')
    (original / 'generate').symlink_to('/usr/libexec/netplan/generate')
    (original / 'wpa-wlan0.conf').write_text('PRIVATE_FIXTURE')
    (original / '.hidden.yaml').write_text('PRIVATE_FIXTURE')
    (original / 'backup.yaml.bak').write_text('PRIVATE_FIXTURE')
    destination = root / 'copy'
    module.copy_netplan_configs(original, destination)
    assert sorted(p.name for p in destination.iterdir()) == ['10-config.yaml']
    assert (destination / '10-config.yaml').read_bytes() == (original / '10-config.yaml').read_bytes()
    (original / '20-link.yaml').symlink_to(original / '10-config.yaml')
    try:
        module.copy_netplan_configs(original, root / 'refused')
    except module.SafeRefusal:
        pass
    else:
        raise AssertionError('YAML symlink accepted')
print('OK: only active YAML copied; generator link ignored; YAML symlink rejected')
