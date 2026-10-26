#!/usr/bin/env bash
# Burnout Revenge. GPL-3.0-or-later (see COPYING).
set -euo pipefail
ROOT=$(cd "$(dirname "$(readlink -f "$0")")" && pwd)
exec "$ROOT/install.sh" --uninstall "$@"
