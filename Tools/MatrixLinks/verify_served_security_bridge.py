"""Verify the HTTP assets UE actually loads, then test the served bridge code."""
from pathlib import Path
import hashlib
import json
import subprocess
import tempfile
import urllib.request

source = Path('D:/0bay/NetworkSecurity')
base = 'http://127.0.0.1:5000/network-security/'
report = {}
with tempfile.TemporaryDirectory(prefix='security-bridge-') as temp:
    for filename in ('index.html', 'prototype.js', 'ue-bridge.js'):
        data = urllib.request.urlopen(base + filename, timeout=10).read()
        assert data == (source / filename).read_bytes(), f'HTTP serves a different {filename}'
        (Path(temp) / filename).write_bytes(data)
        report[filename] = hashlib.sha256(data).hexdigest()
    subprocess.run(['node', str(Path(__file__).with_name('test_security_bridge.cjs')),
                    str(Path(temp) / 'ue-bridge.js')], check=True)
print(json.dumps({'http_matches_source': True, 'sha256': report}, indent=2))
