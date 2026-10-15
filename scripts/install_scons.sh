#!/usr/bin/env bash
# SPDX-License-Identifier: MIT

set -euo pipefail

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
cache_root=${XDG_CACHE_HOME:-${HOME}/.cache}/mochascript
mkdir -p "$cache_root"
requirements=$(mktemp "$cache_root/scons-requirements.XXXXXX")
trap 'rm -f -- "$requirements"' EXIT

python3 - "$repo_root/upstream.lock.json" >"$requirements" <<'PY'
import json
import sys

with open(sys.argv[1], encoding="utf-8") as stream:
    pin = json.load(stream)["build_tools"]["scons"]
print(f"scons=={pin['version']} --hash=sha256:{pin['wheel_sha256']}")
PY

python3 -m pip install --user --disable-pip-version-check --require-hashes \
    --only-binary=:all: --requirement "$requirements"
