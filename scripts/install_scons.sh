#!/usr/bin/env bash
# SPDX-License-Identifier: MIT

set -euo pipefail

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
cache_root=${XDG_CACHE_HOME:-${HOME}/.cache}/mochascript
mkdir -p "$cache_root"
requirements=$(mktemp "$cache_root/scons-requirements.XXXXXX")
trap 'rm -f -- "$requirements"' EXIT

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
version=${pin[0]}
wheel_sha256=${pin[1]}
printf 'scons==%s --hash=sha256:%s\n' "$version" "$wheel_sha256" >"$requirements"

python_abi=${pin[2]}
tool_parent=${XDG_DATA_HOME:-${HOME}/.local/share}/mochascript
venv_root="$tool_parent/scons-$version-${wheel_sha256:0:12}-$python_abi"
mkdir -p "$tool_parent"
if [[ -L "$venv_root" ]]; then
    echo "Refusing managed SCons virtualenv symlink: $venv_root" >&2
    exit 1
fi
venv_arguments=()
if [[ -d "$venv_root" ]] && { [[ ! -x "$venv_root/bin/python" ]] ||
        ! "$venv_root/bin/python" -c 'import sys' >/dev/null 2>&1; }; then
    venv_arguments+=(--clear)
fi
if ! python3 -m venv "${venv_arguments[@]}" "$venv_root"; then
    echo "Could not create the managed SCons virtualenv; install Python's venv support and retry." >&2
    exit 1
fi

"$venv_root/bin/python" -m pip install --disable-pip-version-check --require-hashes \
    --only-binary=:all: --no-deps --requirement "$requirements"
echo "Installed pinned SCons at $venv_root/bin/scons"
