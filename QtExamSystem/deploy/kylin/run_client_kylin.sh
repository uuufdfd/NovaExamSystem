#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
APP="$ROOT/build-kylin/QtExamSystem"
if [[ ! -x "$APP" ]]; then
  bash "$ROOT/deploy/kylin/build_client_kylin.sh"
fi
# Local camera by default. VM host bridges are opt-in through environment variables.
if [[ -z "${QTEXAM_PYTHON:-}" && -x "$ROOT/.venv/bin/python" ]]; then
  export QTEXAM_PYTHON="$ROOT/.venv/bin/python"
fi
cd "$ROOT"
exec "$APP"
