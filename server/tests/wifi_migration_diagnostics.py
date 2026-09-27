"""Diagnostic redaction checks; no board access or network changes."""
import importlib.util
from pathlib import Path
import yaml

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
