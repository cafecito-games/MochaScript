#!/usr/bin/env python3
"""Verify the pinned upstream checkouts and summarize the GDScript extraction surface."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import subprocess
from collections import Counter
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
LOCK_PATH = ROOT / "upstream.lock.json"

LAYERS = {
    "tokenizer": (
        "gdscript_tokenizer.cpp",
        "gdscript_tokenizer.h",
        "gdscript_tokenizer_buffer.cpp",
        "gdscript_tokenizer_buffer.h",
    ),
    "parser": ("gdscript_parser.cpp", "gdscript_parser.h"),
    "analyzer": ("gdscript_analyzer.cpp", "gdscript_analyzer.h"),
    "compiler": (
        "gdscript_compiler.cpp",
        "gdscript_compiler.h",
        "gdscript_codegen.h",
        "gdscript_byte_codegen.cpp",
        "gdscript_byte_codegen.h",
    ),
    "runtime": (
        "gdscript_function.cpp",
        "gdscript_function.h",
        "gdscript_vm.cpp",
        "gdscript_lambda_callable.cpp",
        "gdscript_lambda_callable.h",
        "gdscript_rpc_callable.cpp",
        "gdscript_rpc_callable.h",
        "gdscript_utility_callable.cpp",
        "gdscript_utility_callable.h",
        "gdscript_utility_functions.cpp",
        "gdscript_utility_functions.h",
    ),
    "cache": ("gdscript_cache.cpp", "gdscript_cache.h"),
    "integration": (
        "gdscript.cpp",
        "gdscript.h",
        "gdscript_resource_format.cpp",
        "gdscript_resource_format.h",
        "register_types.cpp",
        "register_types.h",
    ),
    "editor_debug": (
        "gdscript_editor.cpp",
        "gdscript_disassembler.cpp",
        "gdscript_warning.cpp",
        "gdscript_warning.h",
    ),
}

INCLUDE_RE = re.compile(r'^#include "([^\"]+)"', re.MULTILINE)
ENGINE_PREFIXES = ("core/", "editor/", "main/", "modules/", "scene/", "servers/")


def git_head(path: Path) -> str:
    return subprocess.run(
        ["git", "-C", str(path), "rev-parse", "HEAD"],
        check=True,
        text=True,
        stdout=subprocess.PIPE,
    ).stdout.strip()


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def line_count(path: Path) -> int:
    with path.open("rb") as stream:
        return sum(1 for _ in stream)


def require_equal(label: str, actual: str, expected: str) -> None:
    if actual != expected:
        raise SystemExit(f"{label}: expected {expected}, actual {actual}")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--custom-godot", type=Path, required=True)
    parser.add_argument("--api", type=Path)
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args()

    lock = json.loads(LOCK_PATH.read_text(encoding="utf-8"))
    outer = args.custom_godot.resolve()
    godot = outer / "godot"
    module = godot / lock["godot"]["gdscript_root"]

    require_equal("custom-godot commit", git_head(outer), lock["custom_godot"]["commit"])
    require_equal("Godot commit", git_head(godot), lock["godot"]["commit"])
    require_equal(
        "patcher config SHA-256",
        sha256(outer / "config/patchers.yaml"),
        lock["custom_godot"]["patcher_config_sha256"],
    )
    for patch in lock["custom_godot"]["enabled_gdscript_patchers"]:
        require_equal(
            f"{patch['name']} SHA-256",
            sha256(outer / patch["patch"]),
            patch["sha256"],
        )
    if args.api:
        require_equal(
            "extension API SHA-256",
            sha256(args.api),
            lock["installed_runtime"]["extension_api_sha256"],
        )

    layer_lines = {
        name: sum(line_count(module / relative) for relative in files)
        for name, files in LAYERS.items()
    }
    include_counts: Counter[str] = Counter()
    unique_headers: set[str] = set()
    for path in sorted(module.rglob("*")):
        if path.suffix not in {".cpp", ".h"} or "tests" in path.parts:
            continue
        for header in INCLUDE_RE.findall(path.read_text(encoding="utf-8")):
            if header.startswith(ENGINE_PREFIXES):
                prefix = "/".join(header.split("/")[:2])
                include_counts[prefix] += 1
                unique_headers.add(header)

    result = {
        "custom_godot_commit": git_head(outer),
        "godot_commit": git_head(godot),
        "layer_lines": layer_lines,
        "direct_engine_include_counts": dict(include_counts.most_common()),
        "direct_engine_headers": sorted(unique_headers),
    }
    if args.json:
        print(json.dumps(result, indent=2, sort_keys=True))
        return

    print("Pinned source verified.")
    print("\nLayer lines:")
    for name, count in layer_lines.items():
        print(f"  {name:14} {count:7}")
    print("\nDirect engine include groups:")
    for prefix, count in include_counts.most_common():
        print(f"  {prefix:24} {count:4}")


if __name__ == "__main__":
    main()
