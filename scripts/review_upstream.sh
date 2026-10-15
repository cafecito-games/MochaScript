#!/usr/bin/env bash
# SPDX-License-Identifier: MIT

set -euo pipefail

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
cache_root=${XDG_CACHE_HOME:-${HOME}/.cache}/mochascript
custom_commit=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["custom_godot"]["commit"])' "$repo_root/upstream.lock.json")
godot_commit=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["godot"]["commit"])' "$repo_root/upstream.lock.json")
if (( $# > 0 )); then
    report_root=$1
    if ! mkdir "$report_root"; then
        echo "Explicit review directory must not already exist: $report_root" >&2
        exit 1
    fi
else
    mkdir -p "$cache_root/upstream-review"
    report_root=$(mktemp -d "$cache_root/upstream-review/$godot_commit.XXXXXX")
fi
source_root=${MOCHA_CUSTOM_GODOT_SOURCE:-$cache_root/upstream/custom-godot-$custom_commit}

materialize_status=0
"$repo_root/scripts/materialize_upstream.sh" >"$report_root/materialize.log" 2>&1 || materialize_status=$?
if (( materialize_status != 0 )); then
    printf 'Attempted upstream review for Godot %s.\nMaterialization status: %s (failed; component reviews not run)\nNo MochaScript-owned source was overwritten.\n' \
        "$godot_commit" "$materialize_status" >"$report_root/README.txt"
    cat "$report_root/materialize.log" >&2
    echo "Upstream materialization failed; review evidence: $report_root" >&2
    exit "$materialize_status"
fi

tokenizer_status=0
python3 "$repo_root/scripts/import_gdscript.py" review tokenizer \
    --custom-godot "$source_root" \
    --output "$report_root/tokenizer-upstream.patch" \
    --mocha-output "$report_root/tokenizer-mocha.patch" \
    2>"$report_root/tokenizer-review.log" || tokenizer_status=$?
fixtures_status=0
python3 "$repo_root/scripts/import_gdscript.py" review bootstrap-fixtures \
    --custom-godot "$source_root" \
    --output "$report_root/bootstrap-fixtures-upstream.patch" \
    --mocha-output "$report_root/bootstrap-fixtures-mocha.patch" \
    2>"$report_root/bootstrap-fixtures-review.log" || fixtures_status=$?

printf 'Attempted upstream-only and Mocha-owned reviews for Godot %s.\nMaterialization status: %s\nTokenizer review status: %s\nBootstrap fixture review status: %s\nStatus legend: 0 complete; 2 candidate/transform-incomplete with saved markers; any other nonzero value failed before complete reports.\nComponent logs record warnings or pre-report failures.\nNo MochaScript-owned source was overwritten.\n' \
    "$godot_commit" "$materialize_status" "$tokenizer_status" "$fixtures_status" >"$report_root/README.txt"

echo "Review reports: $report_root"
if (( tokenizer_status != 0 || fixtures_status != 0 )); then
    cat "$report_root/tokenizer-review.log" "$report_root/bootstrap-fixtures-review.log" >&2
    if (( (tokenizer_status == 0 || tokenizer_status == 2) &&
            (fixtures_status == 0 || fixtures_status == 2) )); then
        echo "One or more component reviews were incomplete; see the saved markers and component logs." >&2
        exit 2
    else
        echo "One or more component reviews failed before complete reports; see the component logs." >&2
        exit 1
    fi
fi
