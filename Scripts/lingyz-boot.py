from pathlib import Path
import os
import sys

sys.path.insert(0, "/overlay/pypath")

import uvicorn


def load_env(path: str) -> None:
    file_path = Path(path)
    if not file_path.exists():
        return
    for raw in file_path.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        key, value = line.split("=", 1)
        os.environ[key] = value


load_env("/app/.env")
load_env("/overlay/lingyz.env.local")

uvicorn.run(
    "onto_platform.app:create_app",
    factory=True,
    host="0.0.0.0",
    port=8080,
    proxy_headers=True,
    forwarded_allow_ips="*",
)
