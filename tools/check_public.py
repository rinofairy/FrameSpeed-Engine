"""Check public source file hashes without board access or vendor dependencies."""
from pathlib import Path
import hashlib
import json
root=Path(__file__).resolve().parents[1]
expected=json.loads((root/'PUBLIC_FILES_SHA256.json').read_text(encoding='utf-8'))['files']
for name,sha in expected.items():
    path=root/name
    if not path.is_file() or hashlib.sha256(path.read_bytes()).hexdigest()!=sha:
        raise SystemExit('Mismatch: '+name)
print('PUBLIC_SOURCE_FILES_PASS',len(expected))
