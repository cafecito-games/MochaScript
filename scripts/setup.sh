#!/usr/bin/env bash
# SPDX-License-Identifier: MIT

set -euo pipefail

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
cd "$repo_root"

command -v python3 >/dev/null || { echo "python3 is required" >&2; exit 1; }
command -v scons >/dev/null || { echo "scons is required" >&2; exit 1; }
command -v godot >/dev/null || { echo "godot is required" >&2; exit 1; }

git submodule update --init --recursive

python3 - <<'PY'
import hashlib
import json
import subprocess
from pathlib import Path

root = Path.cwd()
lock = json.loads((root / "upstream.lock.json").read_text())

expected_scons = lock["build_tools"]["scons"]["version"]
scons_output = subprocess.run(
    ["scons", "--version"],
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
PY

expected_version=$(python3 -c 'import json; print(json.load(open("upstream.lock.json"))["installed_runtime"]["version"])')
actual_version=$(godot --version)
if [[ "$actual_version" != "$expected_version" ]]; then
    echo "Godot mismatch: expected $expected_version, got $actual_version" >&2
    exit 1
fi

echo "MochaScript prerequisites and immutable pins are valid."
