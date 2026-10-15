#!/usr/bin/env bash
# SPDX-License-Identifier: MIT

set -euo pipefail

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
cache_root=${XDG_CACHE_HOME:-${HOME}/.cache}/mochascript
report_root=${1:-$cache_root/upstream-review}
source_root=${MOCHA_CUSTOM_GODOT_SOURCE:-$cache_root/upstream/custom-godot}

"$repo_root/scripts/materialize_upstream.sh" >/dev/null
mkdir -p "$report_root"
python3 "$repo_root/scripts/import_gdscript.py" review tokenizer \
    --custom-godot "$source_root" --output "$report_root/tokenizer.patch"
python3 "$repo_root/scripts/import_gdscript.py" review bootstrap-fixtures \
    --custom-godot "$source_root" --output "$report_root/bootstrap-fixtures.patch"

godot_commit=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["godot"]["commit"])' "$repo_root/upstream.lock.json")
printf 'Generated review-only deltas for Godot %s.\nNo MochaScript-owned source was overwritten.\n' \
    "$godot_commit" >"$report_root/README.txt"

echo "Review reports: $report_root"
