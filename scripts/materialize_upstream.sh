#!/usr/bin/env bash
# SPDX-License-Identifier: MIT

set -euo pipefail

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
cache_base=${XDG_CACHE_HOME:-${HOME}/.cache}/mochascript/upstream
target=${MOCHA_CUSTOM_GODOT_SOURCE:-$cache_base/custom-godot}

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

mkdir -p "$cache_base"
df -h "$cache_base"

if [[ ! -d "$target/.git" ]]; then
    if [[ -e "$target" ]]; then
        echo "Refusing to replace non-repository path: $target" >&2
        exit 1
    fi
    git clone --filter=blob:none --no-checkout "$custom_remote" "$target"
    git -C "$target" fetch --depth=1 origin "refs/tags/$custom_release:refs/tags/$custom_release"
    git -C "$target" checkout --detach "$custom_commit"
elif [[ $(git -C "$target" rev-parse HEAD) != "$custom_commit" ]]; then
    echo "Existing custom-godot checkout is not at pinned commit $custom_commit: $target" >&2
    exit 1
fi

godot_root="$target/godot"
if [[ ! -e "$godot_root/.git" ]]; then
    if [[ -e "$godot_root" && -n $(find "$godot_root" -mindepth 1 -maxdepth 1 -print -quit) ]]; then
        echo "Refusing to replace nonempty Godot path: $godot_root" >&2
        exit 1
    fi
    git clone --filter=blob:none --no-checkout "$godot_remote" "$godot_root"
    git -C "$godot_root" sparse-checkout init --cone
    git -C "$godot_root" sparse-checkout set modules/gdscript core editor scene/resources servers/text
    git -C "$godot_root" fetch --depth=1 origin "refs/tags/$godot_tag:refs/tags/$godot_tag"
    git -C "$godot_root" checkout --detach "$godot_commit"
    git -C "$target" submodule absorbgitdirs godot
elif [[ $(git -C "$godot_root" rev-parse HEAD) != "$godot_commit" ]]; then
    echo "Existing Godot checkout is not at pinned commit $godot_commit: $godot_root" >&2
    exit 1
fi

python3 "$repo_root/scripts/inventory_upstream.py" --custom-godot "$target"
python3 "$repo_root/scripts/import_gdscript.py" verify-source tokenizer --custom-godot "$target"
python3 "$repo_root/scripts/import_gdscript.py" verify-source bootstrap-fixtures --custom-godot "$target"
echo "$target"
