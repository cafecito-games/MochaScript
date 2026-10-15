#!/usr/bin/env bash
# SPDX-License-Identifier: MIT

set -euo pipefail

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
cache_root=${XDG_CACHE_HOME:-${HOME}/.cache}/mochascript
mkdir -p "$cache_root"
log_dir=$(mktemp -d "$cache_root/verify.XXXXXX")

"$repo_root/scripts/setup.sh"
"$repo_root/scripts/build.sh"

for library in \
    "$repo_root/project/bin/linux/libmocha_script.linux.template_debug.x86_64.so" \
    "$repo_root/project/bin/linux/libmocha_script.linux.template_release.x86_64.so"; do
    if [[ ! -f "$library" ]]; then
        echo "Required extension artifact was not built: $library" >&2
        exit 1
    fi
done

python3 - "$repo_root/project/bin/linux/libmocha_script.linux.template_release.x86_64.so" <<'PY'
import ctypes
import sys

library = ctypes.CDLL(sys.argv[1])
getattr(library, "mocha_script_library_init")
PY

validate_uid_state() {
    local script_resource
    while IFS= read -r script_resource; do
        case "$script_resource" in
            *.gd|*.mocha|*.gdextension)
                if ! git -C "$repo_root" ls-files --error-unmatch "${script_resource}.uid" >/dev/null 2>&1; then
                    echo "Tracked script resource is missing its committed UID sidecar: ${script_resource}.uid" >&2
                    return 1
                fi
                ;;
        esac
    done < <(git -C "$repo_root" ls-files project)

    local untracked_uids
    untracked_uids=$(git -C "$repo_root" ls-files --others --exclude-standard -- '*.uid')
    if [[ -n "$untracked_uids" ]]; then
        echo "Godot generated untracked UID sidecars; commit them for stable resource identity:" >&2
        printf '%s\n' "$untracked_uids" >&2
        return 1
    fi
}

snapshot_tracked_uids() {
    local uid_path
    while IFS= read -r uid_path; do
        sha256sum "$repo_root/$uid_path"
    done < <(git -C "$repo_root" ls-files 'project/*.uid' 'project/**/*.uid')
}

validate_uid_state
uid_snapshot_before="$log_dir/uids-before.sha256"
uid_snapshot_after="$log_dir/uids-after.sha256"
snapshot_tracked_uids >"$uid_snapshot_before"

editor_log="$log_dir/editor-import.log"
editor_test_log="$log_dir/editor-test.log"
recognition_log="$log_dir/recognition.log"
tokenizer_log="$log_dir/tokenizer.log"
frontend_log="$log_dir/frontend.log"
execution_log="$log_dir/execution.log"
native_log="$log_dir/native.log"
bootstrap_corpus_log="$log_dir/bootstrap-corpus.log"
release_editor_log="$log_dir/release-editor-import.log"
release_editor_test_log="$log_dir/release-editor-test.log"
release_recognition_log="$log_dir/release-recognition.log"
release_execution_log="$log_dir/release-execution.log"

run_godot_in_project() {
    local label=$1
    local log=$2
    local project_path=$3
    local sandbox=$4
    shift 4
    set +e
    XDG_DATA_HOME="$log_dir/$sandbox/data" XDG_CONFIG_HOME="$log_dir/$sandbox/config" \
        XDG_CACHE_HOME="$log_dir/$sandbox/cache" \
        timeout 60s godot --headless --path "$project_path" "$@" >"$log" 2>&1
    local status=$?
    set -e
    if (( status != 0 )); then
        echo "Godot $label failed with status $status" >&2
        echo "Verification logs: $log_dir" >&2
        cat "$log" >&2
        exit 1
    fi
}

run_godot() {
    local label=$1
    local log=$2
    shift 2
    run_godot_in_project "$label" "$log" "$repo_root/project" debug "$@"
}

run_godot "editor import" "$editor_log" --editor --quit
run_godot "editor behavior test" "$editor_test_log" --editor --quit-after 2 --script res://tests/editor_test.gd
run_godot "recognition test" "$recognition_log" --script res://tests/recognition_test.gd
run_godot "tokenizer test" "$tokenizer_log" --script res://tests/tokenizer_test.gd
run_godot "frontend test" "$frontend_log" --script res://tests/frontend_test.gd
run_godot "execution test" "$execution_log" --script res://tests/vertical_slice_test.gd
run_godot "native test" "$native_log" --script res://tests/native_test.gd
run_godot "bootstrap corpus test" "$bootstrap_corpus_log" --script res://tests/bootstrap_corpus_test.gd

release_project="$log_dir/release-project"
python3 - "$repo_root/project" "$release_project" <<'PY'
from pathlib import Path
import configparser
import json
import shutil
import sys

source = Path(sys.argv[1])
target = Path(sys.argv[2])
shutil.copytree(source, target, ignore=shutil.ignore_patterns(".godot"))
manifest = target / "bin" / "mocha_script.gdextension"
configuration = configparser.ConfigParser(interpolation=None)
configuration.optionxform = str
with manifest.open(encoding="utf-8") as stream:
    configuration.read_file(stream)
libraries = configuration["libraries"]
debug_key = "linux.x86_64.single.debug"
release_key = "linux.x86_64.single.release"

def declared_library(key: str, expected_name: str) -> str:
    if key not in libraries:
        raise SystemExit(f"extension manifest is missing {key}")
    raw_value = libraries[key]
    try:
        relative_path = json.loads(raw_value)
    except (TypeError, json.JSONDecodeError) as error:
        raise SystemExit(f"extension manifest has an invalid {key} value: {raw_value}") from error
    resolved = (manifest.parent / relative_path).resolve()
    expected = (manifest.parent / "linux" / expected_name).resolve()
    if resolved != expected or not resolved.is_file():
        raise SystemExit(f"extension manifest {key} does not resolve to {expected}")
    return raw_value

declared_library(debug_key, "libmocha_script.linux.template_debug.x86_64.so")
release_library = declared_library(release_key, "libmocha_script.linux.template_release.x86_64.so")
libraries[debug_key] = release_library
with manifest.open("w", encoding="utf-8") as stream:
    configuration.write(stream, space_around_delimiters=True)
PY

run_godot_in_project "release editor import" "$release_editor_log" "$release_project" release --editor --quit
run_godot_in_project "release editor behavior test" "$release_editor_test_log" "$release_project" release \
    --editor --quit-after 2 --script res://tests/editor_test.gd
run_godot_in_project "release recognition test" "$release_recognition_log" "$release_project" release \
    --script res://tests/recognition_test.gd -- --release-smoke
run_godot_in_project "release execution test" "$release_execution_log" "$release_project" release \
    --script res://tests/vertical_slice_test.gd

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

release_editor_sentinel_count=$(grep -c '^MOCHA_EDITOR_OK$' "$release_editor_test_log" || true)
release_recognition_sentinel_count=$(grep -c '^MOCHA_RECOGNITION_OK$' "$release_recognition_log" || true)
release_execution_sentinel_count=$(grep -c '^MOCHA_EXECUTION_OK$' "$release_execution_log" || true)
if [[ "$release_editor_sentinel_count" != "1" || "$release_recognition_sentinel_count" != "1" ||
        "$release_execution_sentinel_count" != "1" ]]; then
    echo "Release extension smoke tests did not emit every expected sentinel" >&2
    cat "$release_editor_test_log" "$release_recognition_log" "$release_execution_log" >&2
    exit 1
fi

editor_test_errors=$(grep -Ei 'SCRIPT ERROR|ERROR:|crash|segmentation fault' "$editor_test_log" |
    grep -Ev "^ERROR: [0-9]+ RID allocations of type '(N16RendererViewport8ViewportE|PN13RendererDummy14TextureStorage12DummyTextureE|N17RendererSceneCull8ScenarioE|PN18TextServerAdvanced22ShapedTextDataAdvancedE|PN18TextServerAdvanced12FontAdvancedE)' were leaked at exit\.$" || true)
if [[ -n "$editor_test_errors" ]]; then
    echo "Godot emitted an unexpected error during editor-context verification" >&2
    printf '%s\n' "$editor_test_errors" >&2
    exit 1
fi


release_editor_test_errors=$(grep -Ei 'SCRIPT ERROR|ERROR:|crash|segmentation fault' "$release_editor_test_log" |
    grep -Ev "^ERROR: [0-9]+ RID allocations of type '(N16RendererViewport8ViewportE|PN13RendererDummy14TextureStorage12DummyTextureE|N17RendererSceneCull8ScenarioE|PN18TextServerAdvanced22ShapedTextDataAdvancedE|PN18TextServerAdvanced12FontAdvancedE)' were leaked at exit\.$" || true)
if [[ -n "$release_editor_test_errors" ]]; then
    echo "Godot emitted an unexpected error during release editor-context verification" >&2
    printf '%s\n' "$release_editor_test_errors" >&2
    exit 1
fi

if grep -Eiq 'SCRIPT ERROR|ERROR:|crash|segmentation fault' "$editor_log" "$recognition_log" "$tokenizer_log" "$frontend_log" "$execution_log" "$native_log" "$bootstrap_corpus_log" "$release_editor_log" "$release_recognition_log" "$release_execution_log"; then
    echo "Godot emitted an unexpected error during verification" >&2
    grep -Ein 'SCRIPT ERROR|ERROR:|crash|segmentation fault' "$editor_log" "$recognition_log" "$tokenizer_log" "$frontend_log" "$execution_log" "$native_log" "$bootstrap_corpus_log" "$release_editor_log" "$release_recognition_log" "$release_execution_log" >&2
    exit 1
fi

validate_uid_state
snapshot_tracked_uids >"$uid_snapshot_after"
if ! cmp -s "$uid_snapshot_before" "$uid_snapshot_after"; then
    echo "Godot rewrote a tracked UID sidecar during editor import:" >&2
    diff -u "$uid_snapshot_before" "$uid_snapshot_after" >&2 || true
    exit 1
fi

rm -f -- "$editor_log" "$editor_test_log" "$recognition_log" "$tokenizer_log" "$frontend_log" \
    "$execution_log" "$native_log" "$bootstrap_corpus_log" "$release_editor_log" \
    "$release_editor_test_log" "$release_recognition_log" "$release_execution_log" \
    "$uid_snapshot_before" "$uid_snapshot_after"
rm -rf -- "$log_dir/debug" "$log_dir/release" "$release_project"
rmdir -- "$log_dir"
echo "MOCHA_VERIFY_OK"
