#!/usr/bin/env bash
# SPDX-License-Identifier: MIT

set -euo pipefail

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
cache_base=${XDG_CACHE_HOME:-${HOME}/.cache}/mochascript/upstream

readarray -t pins < <(python3 - "$repo_root/upstream.lock.json" <<'PY'
import json, sys
lock = json.load(open(sys.argv[1]))
print(lock["custom_godot"]["repository"])
print(lock["custom_godot"]["release"])
print(lock["custom_godot"]["commit"])
print(lock["godot"]["repository"])
print(lock["godot"]["tag"])
print(lock["godot"]["commit"])
PY
)
custom_remote=${pins[0]}
custom_release=${pins[1]}
custom_commit=${pins[2]}
godot_remote=${pins[3]}
godot_tag=${pins[4]}
godot_commit=${pins[5]}
baseline_commit=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["godot_commit"])' "$repo_root/provenance/source_imports.json")
target=${MOCHA_CUSTOM_GODOT_SOURCE:-$cache_base/custom-godot-$custom_commit}

mkdir -p "$cache_base"
target_parent=$(dirname "$target")
mkdir -p "$target_parent"
command -v flock >/dev/null || { echo "flock is required" >&2; exit 1; }
lock_path="$target_parent/.$(basename "$target").lock"
exec {materialize_lock_fd}>"$lock_path"
flock "$materialize_lock_fd"
df -h "$cache_base"

if [[ ! -e "$target/.git" ]]; then
    if [[ -e "$target" ]]; then
        echo "Refusing to replace non-repository path: $target" >&2
        exit 1
    fi
    custom_temp=$(mktemp -d "$target_parent/.custom-godot.materialize.XXXXXX")
    trap 'rm -rf "$custom_temp"' EXIT
    git clone --filter=blob:none --no-checkout "$custom_remote" "$custom_temp"
    git -C "$custom_temp" fetch --depth=1 origin "refs/tags/$custom_release:refs/tags/$custom_release"
    custom_tag_commit=$(git -C "$custom_temp" rev-parse "refs/tags/$custom_release^{commit}")
    if [[ "$custom_tag_commit" != "$custom_commit" ]]; then
        echo "Custom Godot tag $custom_release resolves to $custom_tag_commit, expected $custom_commit" >&2
        exit 1
    fi
    git -C "$custom_temp" checkout --detach "$custom_commit"
    mv -T "$custom_temp" "$target"
    trap - EXIT
elif [[ $(git -C "$target" rev-parse HEAD) != "$custom_commit" ]]; then
    echo "Existing custom-godot checkout is not at pinned commit $custom_commit: $target" >&2
    exit 1
fi

custom_tag_commit=$(git -C "$target" rev-parse "refs/tags/$custom_release^{commit}")
if [[ "$custom_tag_commit" != "$custom_commit" ]]; then
    echo "Custom Godot tag $custom_release resolves to $custom_tag_commit, expected $custom_commit" >&2
    exit 1
fi
custom_godot_gitlink=$(git -C "$target" rev-parse "$custom_commit:godot")
if [[ "$custom_godot_gitlink" != "$godot_commit" ]]; then
    echo "Custom Godot $custom_commit records Godot $custom_godot_gitlink, expected $godot_commit" >&2
    exit 1
fi

godot_root="$target/godot"
if [[ ! -e "$godot_root/.git" ]]; then
    if [[ -e "$godot_root" && -n $(find "$godot_root" -mindepth 1 -maxdepth 1 -print -quit) ]]; then
        echo "Refusing to replace nonempty Godot path: $godot_root" >&2
        exit 1
    fi
    godot_temp=$(mktemp -d "$target/.godot.materialize.XXXXXX")
    trap 'rm -rf "$godot_temp"' EXIT
    git clone --filter=blob:none --no-checkout "$godot_remote" "$godot_temp"
    git -C "$godot_temp" sparse-checkout init --cone
    git -C "$godot_temp" sparse-checkout set modules/gdscript core editor scene/resources servers/text
    git -C "$godot_temp" fetch --depth=1 origin "refs/tags/$godot_tag:refs/tags/$godot_tag"
    godot_tag_commit=$(git -C "$godot_temp" rev-parse "refs/tags/$godot_tag^{commit}")
    if [[ "$godot_tag_commit" != "$godot_commit" ]]; then
        echo "Godot tag $godot_tag resolves to $godot_tag_commit, expected $godot_commit" >&2
        exit 1
    fi
    git -C "$godot_temp" checkout --detach "$godot_commit"
    if [[ -d "$godot_root" ]]; then
        rmdir "$godot_root"
    fi
    mv -T "$godot_temp" "$godot_root"
    trap - EXIT
    git -C "$target" submodule absorbgitdirs godot
elif [[ $(git -C "$godot_root" rev-parse HEAD) != "$godot_commit" ]]; then
    echo "Existing Godot checkout is not at pinned commit $godot_commit: $godot_root" >&2
    exit 1
fi

godot_tag_commit=$(git -C "$godot_root" rev-parse "refs/tags/$godot_tag^{commit}")
if [[ "$godot_tag_commit" != "$godot_commit" ]]; then
    echo "Godot tag $godot_tag resolves to $godot_tag_commit, expected $godot_commit" >&2
    exit 1
fi
if ! git -C "$godot_root" cat-file -e "$baseline_commit^{commit}" 2>/dev/null; then
    git -C "$godot_root" fetch --depth=1 origin "$baseline_commit"
fi
git -C "$godot_root" cat-file -e "$baseline_commit^{commit}" || {
    echo "Godot checkout does not contain provenance baseline $baseline_commit" >&2
    exit 1
}

python3 "$repo_root/scripts/inventory_upstream.py" --custom-godot "$target"
python3 "$repo_root/scripts/import_gdscript.py" verify-source tokenizer --custom-godot "$target"
python3 "$repo_root/scripts/import_gdscript.py" verify-source bootstrap-fixtures --custom-godot "$target"
echo "$target"
