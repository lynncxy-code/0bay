#!/bin/bash
set -euo pipefail
export DEBIAN_FRONTEND=noninteractive
if ! apt-get update; then
  find /etc/apt -type f -name '*.list' -exec sed -i '/updates/d' {} +
  apt-get update
fi
apt-get install -y --no-install-recommends bubblewrap
corepack enable
cd /app
if [ ! -f node_modules/.bin/tsc ]; then
  corepack pnpm install --frozen-lockfile
  corepack pnpm -r --if-present run build
fi
export DSH_HOST="${DSH_HOST:-0.0.0.0}"
export PORT="${PORT:-3080}"
export DSH_INTERNAL_PORT="${DSH_INTERNAL_PORT:-3081}"
export DSH_CONTROL_PLANE_URL="${DSH_CONTROL_PLANE_URL:-http://lingyz-api:8080}"
export ORGBRAIN_API_URL="${ORGBRAIN_API_URL:-http://lingyz-api:8080}"
exec node --env-file-if-exists=.env deploy/start.mjs --local-preview
