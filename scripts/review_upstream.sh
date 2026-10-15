#!/usr/bin/env bash
# SPDX-License-Identifier: MIT

set -euo pipefail

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
cache_root=${XDG_CACHE_HOME:-${HOME}/.cache}/mochascript
custom_commit=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["custom_godot"]["commit"])' "$repo_root/upstream.lock.json")
godot_commit=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["godot"]["commit"])' "$repo_root/upstream.lock.json")
if (( $# > 0 )); then
    report_root=$1
    mkdir -p "$report_root"
    if [[ -n $(find "$report_root" -mindepth 1 -maxdepth 1 -print -quit) ]]; then
        echo "Explicit review directory must be empty: $report_root" >&2
        exit 1
    fi
else
    mkdir -p "$cache_root/upstream-review"
    report_root=$(mktemp -d "$cache_root/upstream-review/$godot_commit.XXXXXX")
fi
source_root=${MOCHA_CUSTOM_GODOT_SOURCE:-$cache_root/upstream/custom-godot-$custom_commit}

"$repo_root/scripts/materialize_upstream.sh" >/dev/null
mkdir -p "$report_root"
tokenizer_status=0
python3 "$repo_root/scripts/import_gdscript.py" review tokenizer \
    --custom-godot "$source_root" \
    --output "$report_root/tokenizer-upstream.patch" \
    --mocha-output "$report_root/tokenizer-mocha.patch" \
    2> >(tee "$report_root/tokenizer-review.log" >&2) || tokenizer_status=$?
fixtures_status=0
python3 "$repo_root/scripts/import_gdscript.py" review bootstrap-fixtures \
    --custom-godot "$source_root" \
    --output "$report_root/bootstrap-fixtures-upstream.patch" \
    --mocha-output "$report_root/bootstrap-fixtures-mocha.patch" \
    2> >(tee "$report_root/bootstrap-fixtures-review.log" >&2) || fixtures_status=$?

printf 'Attempted upstream-only and Mocha-owned reviews for Godot %s.\nTokenizer review status: %s\nBootstrap fixture review status: %s\nComponent logs record warnings or pre-report failures.\nNo MochaScript-owned source was overwritten.\n' \
    "$godot_commit" "$tokenizer_status" "$fixtures_status" >"$report_root/README.txt"

echo "Review reports: $report_root"
if (( tokenizer_status != 0 || fixtures_status != 0 )); then
    echo "One or more Mocha transforms failed; see the saved report markers." >&2
    exit 2
fi
