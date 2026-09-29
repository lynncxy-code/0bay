#!/bin/bash
set -euo pipefail
export UV_PROJECT_ENVIRONMENT=/opt/onto-venv
export PATH="/opt/onto-venv/bin:/usr/local/bin:${PATH}"
export PYTHONPATH="/overlay/pypath${PYTHONPATH:+:$PYTHONPATH}"
if ! command -v uv >/dev/null 2>&1; then
  pip install --no-cache-dir uv
  mkdir -p /opt/onto-venv/bin
  cp "$(command -v uv)" /opt/onto-venv/bin/uv
fi
uv sync --frozen --no-dev --no-install-package cx-oracle
exec /opt/onto-venv/bin/python /overlay/lingyz-boot.py
