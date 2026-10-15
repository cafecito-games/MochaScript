#!/usr/bin/env bash
# SPDX-License-Identifier: MIT

set -euo pipefail

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
readarray -t pin < <(python3 - "$repo_root/upstream.lock.json" <<'PY'
import json
import sys

with open(sys.argv[1], encoding="utf-8") as stream:
    pin = json.load(stream)["build_tools"]["scons"]
print(pin["version"])
print(pin["wheel_sha256"])
print(sys.implementation.cache_tag)
PY
)
managed_scons=${XDG_DATA_HOME:-${HOME}/.local/share}/mochascript/scons-${pin[0]}-${pin[1]:0:12}-${pin[2]}/bin/scons

if [[ -x "$managed_scons" ]]; then
    printf '%s\n' "$managed_scons"
else
    echo "Pinned SCons is unavailable; run ./scripts/install_scons.sh" >&2
    exit 1
fi
