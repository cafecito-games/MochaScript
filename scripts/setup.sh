#!/usr/bin/env bash
# SPDX-License-Identifier: MIT

set -euo pipefail

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
cd "$repo_root"

command -v python3 >/dev/null || { echo "python3 is required" >&2; exit 1; }
command -v godot >/dev/null || { echo "godot is required" >&2; exit 1; }
scons_command=$("$repo_root/scripts/resolve_scons.sh")

if [[ ! -e "$repo_root/godot-cpp/.git" ]] || ! git -C "$repo_root/godot-cpp" rev-parse --git-dir >/dev/null 2>&1; then
    echo "godot-cpp is not initialized; run: git submodule update --init --recursive" >&2
    exit 1
fi

MOCHA_SCONS_COMMAND="$scons_command" python3 - <<'PY'
import hashlib
import json
import os
import shutil
import subprocess
from pathlib import Path

root = Path.cwd()
lock = json.loads((root / "upstream.lock.json").read_text())

expected_scons = lock["build_tools"]["scons"]["version"]
scons_output = subprocess.run(
    [os.environ["MOCHA_SCONS_COMMAND"], "--version"],
    check=True,
    text=True,
    stdout=subprocess.PIPE,
).stdout
scons_marker = "SCons: v"
scons_line = next((line.strip() for line in scons_output.splitlines() if scons_marker in line), "")
actual_scons = scons_line.split(scons_marker, 1)[1].split(".", 3)[:3] if scons_line else []
actual_scons = ".".join(actual_scons)
if actual_scons != expected_scons:
    raise SystemExit(f"SCons mismatch: expected {expected_scons}, got {actual_scons or 'unknown'}")

untracked_source = subprocess.run(
    ["git", "ls-files", "--others", "--exclude-standard", "--", "src"],
    check=True,
    text=True,
    stdout=subprocess.PIPE,
).stdout.splitlines()
ignored_source = subprocess.run(
    ["git", "ls-files", "--others", "--ignored", "--exclude-standard", "--", "src"],
    check=True,
    text=True,
    stdout=subprocess.PIPE,
).stdout.splitlines()
unexpected_source = sorted({
    path for path in untracked_source + ignored_source
    if Path(path).suffix in {".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx"}
})
if unexpected_source:
    raise SystemExit(
        "untracked or ignored MochaScript build inputs under src/:\n" +
        "\n".join(unexpected_source)
    )

expected_cpp = lock["godot_cpp"]["commit"]
actual_cpp = subprocess.run(
    ["git", "-C", str(root / "godot-cpp"), "rev-parse", "HEAD"],
    check=True,
    text=True,
    stdout=subprocess.PIPE,
).stdout.strip()
if actual_cpp != expected_cpp:
    raise SystemExit(f"godot-cpp mismatch: expected {expected_cpp}, got {actual_cpp}")
cpp_status = subprocess.run(
    ["git", "-C", str(root / "godot-cpp"), "status", "--porcelain", "--untracked-files=all"],
    check=True,
    text=True,
    stdout=subprocess.PIPE,
).stdout.strip()
if cpp_status:
    raise SystemExit(f"godot-cpp has local source modifications:\n{cpp_status}")
if (root / "godot-cpp" / "custom.py").exists():
    raise SystemExit("godot-cpp/custom.py would override the pinned build configuration")
ignored_inputs = subprocess.run(
    [
        "git", "-C", str(root / "godot-cpp"), "ls-files", "--others", "--ignored",
        "--exclude-standard", "--", "src", "include", "gdextension", "tools",
        "binding_generator.py", "build_profile.py", "SConstruct",
    ],
    check=True,
    text=True,
    stdout=subprocess.PIPE,
).stdout.splitlines()
unexpected_inputs = [
    path for path in ignored_inputs
    if not path.endswith((".o", ".os", ".obj", ".pyc")) and "/__pycache__/" not in path
]
if unexpected_inputs:
    raise SystemExit(
        "godot-cpp has ignored files that can change the build:\n" + "\n".join(unexpected_inputs)
    )

api = root / "api" / "extension_api.json"
actual_api = hashlib.sha256(api.read_bytes()).hexdigest()
expected_api = lock["installed_runtime"]["extension_api_sha256"]
if actual_api != expected_api:
    raise SystemExit(f"extension API mismatch: expected {expected_api}, got {actual_api}")

godot_path = Path(shutil.which("godot")).resolve()
binary_hash = hashlib.sha256()
with godot_path.open("rb") as stream:
    for chunk in iter(lambda: stream.read(1024 * 1024), b""):
        binary_hash.update(chunk)
actual_binary = binary_hash.hexdigest()
expected_binary = lock["installed_runtime"]["linux_editor_binary_sha256"]
if actual_binary != expected_binary:
    raise SystemExit(
        f"Godot binary mismatch: expected {expected_binary}, got {actual_binary} ({godot_path})"
    )
PY

expected_version=$(python3 -c 'import json; print(json.load(open("upstream.lock.json"))["installed_runtime"]["version"])')
actual_version=$(godot --version)
if [[ "$actual_version" != "$expected_version" ]]; then
    echo "Godot mismatch: expected $expected_version, got $actual_version" >&2
    exit 1
fi

echo "MochaScript prerequisites and immutable pins are valid."
