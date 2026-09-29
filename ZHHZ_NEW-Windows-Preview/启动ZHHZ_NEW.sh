#!/usr/bin/env bash
set -Eeuo pipefail

PACKAGE_DIR="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
WEB_DIR="$PACKAGE_DIR/web"
DATA_DIR="$PACKAGE_DIR/data"
PORT_VALUE="${ZHHZ_PORT:-4192}"
BACKEND_VALUE="${TWIN_BACKEND:-http://localhost:5000}"

HOST=127.0.0.1 PORT="$PORT_VALUE" DATA_ROOT="$DATA_DIR" TWIN_BACKEND="$BACKEND_VALUE" \
  node "$WEB_DIR/server.mjs" &
SERVER_PID=$!
cleanup() {
  kill "$SERVER_PID" 2>/dev/null || true
}
trap cleanup EXIT INT TERM

URL="http://127.0.0.1:$PORT_VALUE/zhhz-new.html?lightmaps=full&twinDetail=all&roam=1&collision=0&show=xinchuang-v71"
sleep 1
if command -v xdg-open >/dev/null 2>&1; then
  xdg-open "$URL" >/dev/null 2>&1 || true
fi
printf 'ZHHZ_NEW 信创网页包已启动：%s\n' "$URL"
wait "$SERVER_PID"
