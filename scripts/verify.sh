#!/usr/bin/env bash
# SPDX-License-Identifier: MIT

set -euo pipefail

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
cache_root=${XDG_CACHE_HOME:-${HOME}/.cache}/mochascript
mkdir -p "$cache_root"
log_dir=$(mktemp -d "$cache_root/verify.XXXXXX")

"$repo_root/scripts/setup.sh"
"$repo_root/scripts/build.sh"

editor_log="$log_dir/editor-import.log"
editor_test_log="$log_dir/editor-test.log"
recognition_log="$log_dir/recognition.log"
tokenizer_log="$log_dir/tokenizer.log"
frontend_log="$log_dir/frontend.log"
execution_log="$log_dir/execution.log"
native_log="$log_dir/native.log"
bootstrap_corpus_log="$log_dir/bootstrap-corpus.log"

run_godot() {
    local label=$1
    local log=$2
    shift 2
    set +e
    timeout 60s godot --headless --path "$repo_root/project" "$@" >"$log" 2>&1
    local status=$?
    set -e
    if (( status != 0 )); then
        echo "Godot $label failed with status $status" >&2
        echo "Verification logs: $log_dir" >&2
        cat "$log" >&2
        exit 1
    fi
}

run_godot "editor import" "$editor_log" --editor --quit
run_godot "editor behavior test" "$editor_test_log" --editor --quit-after 2 --script res://tests/editor_test.gd
run_godot "recognition test" "$recognition_log" --script res://tests/recognition_test.gd
run_godot "tokenizer test" "$tokenizer_log" --script res://tests/tokenizer_test.gd
run_godot "frontend test" "$frontend_log" --script res://tests/frontend_test.gd
run_godot "execution test" "$execution_log" --script res://tests/vertical_slice_test.gd
run_godot "native test" "$native_log" --script res://tests/native_test.gd
run_godot "bootstrap corpus test" "$bootstrap_corpus_log" --script res://tests/bootstrap_corpus_test.gd

sentinel_count=$(grep -c '^MOCHA_RECOGNITION_OK$' "$recognition_log" || true)
if [[ "$sentinel_count" != "1" ]]; then
    echo "Expected exactly one MOCHA_RECOGNITION_OK sentinel; got $sentinel_count" >&2
    cat "$recognition_log" >&2
    exit 1
fi

editor_sentinel_count=$(grep -c '^MOCHA_EDITOR_OK$' "$editor_test_log" || true)
if [[ "$editor_sentinel_count" != "1" ]]; then
    echo "Expected exactly one MOCHA_EDITOR_OK sentinel; got $editor_sentinel_count" >&2
    cat "$editor_test_log" >&2
    exit 1
fi

tokenizer_sentinel_count=$(grep -c '^MOCHA_TOKENIZER_OK$' "$tokenizer_log" || true)
if [[ "$tokenizer_sentinel_count" != "1" ]]; then
    echo "Expected exactly one MOCHA_TOKENIZER_OK sentinel; got $tokenizer_sentinel_count" >&2
    cat "$tokenizer_log" >&2
    exit 1
fi

frontend_sentinel_count=$(grep -c '^MOCHA_FRONTEND_OK$' "$frontend_log" || true)
if [[ "$frontend_sentinel_count" != "1" ]]; then
    echo "Expected exactly one MOCHA_FRONTEND_OK sentinel; got $frontend_sentinel_count" >&2
    cat "$frontend_log" >&2
    exit 1
fi

execution_sentinel_count=$(grep -c '^MOCHA_EXECUTION_OK$' "$execution_log" || true)
if [[ "$execution_sentinel_count" != "1" ]]; then
    echo "Expected exactly one MOCHA_EXECUTION_OK sentinel; got $execution_sentinel_count" >&2
    cat "$execution_log" >&2
    exit 1
fi

native_sentinel_count=$(grep -c '^MOCHA_NATIVE_OK$' "$native_log" || true)
if [[ "$native_sentinel_count" != "1" ]]; then
    echo "Expected exactly one MOCHA_NATIVE_OK sentinel; got $native_sentinel_count" >&2
    cat "$native_log" >&2
    exit 1
fi

bootstrap_sentinel_count=$(grep -c '^MOCHA_BOOTSTRAP_CORPUS_OK$' "$bootstrap_corpus_log" || true)
if [[ "$bootstrap_sentinel_count" != "1" ]]; then
    echo "Expected exactly one MOCHA_BOOTSTRAP_CORPUS_OK sentinel; got $bootstrap_sentinel_count" >&2
    cat "$bootstrap_corpus_log" >&2
    exit 1
fi

editor_test_errors=$(grep -Ei 'SCRIPT ERROR|ERROR:|crash|segmentation fault' "$editor_test_log" |
    grep -Ev "^ERROR: [0-9]+ RID allocations of type '(N16RendererViewport8ViewportE|PN13RendererDummy14TextureStorage12DummyTextureE|N17RendererSceneCull8ScenarioE|PN18TextServerAdvanced22ShapedTextDataAdvancedE|PN18TextServerAdvanced12FontAdvancedE)' were leaked at exit\.$" || true)
if [[ -n "$editor_test_errors" ]]; then
    echo "Godot emitted an unexpected error during editor-context verification" >&2
    printf '%s\n' "$editor_test_errors" >&2
    exit 1
fi

if grep -Eiq 'SCRIPT ERROR|ERROR:|crash|segmentation fault' "$editor_log" "$recognition_log" "$tokenizer_log" "$frontend_log" "$execution_log" "$native_log" "$bootstrap_corpus_log"; then
    echo "Godot emitted an unexpected error during verification" >&2
    grep -Ein 'SCRIPT ERROR|ERROR:|crash|segmentation fault' "$editor_log" "$recognition_log" "$tokenizer_log" "$frontend_log" "$execution_log" "$native_log" "$bootstrap_corpus_log" >&2
    exit 1
fi

rm -f -- "$editor_log" "$editor_test_log" "$recognition_log" "$tokenizer_log" "$frontend_log" \
    "$execution_log" "$native_log" "$bootstrap_corpus_log"
rmdir -- "$log_dir"
echo "MOCHA_VERIFY_OK"
