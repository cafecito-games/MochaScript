#!/usr/bin/env python3
"""Materialize or review the pinned GDScript bootstrap source.

Initial import is deliberately create-only. Once files exist under src/language,
MochaScript owns them; `review` writes a report without changing those files.
"""

from __future__ import annotations

import argparse
import difflib
import hashlib
import json
import subprocess
import sys
from datetime import date
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
LOCK_PATH = ROOT / "upstream.lock.json"
PROVENANCE_PATH = ROOT / "provenance" / "source_imports.json"

COMPONENTS = {
    "tokenizer": {
        "modules/gdscript/gdscript_tokenizer.h": "src/language/mocha_tokenizer.h",
        "modules/gdscript/gdscript_tokenizer.cpp": "src/language/mocha_tokenizer.cpp",
    },
    "bootstrap-fixtures": {
        "modules/gdscript/tests/scripts/parser/features/array.gd":
            "project/tests/upstream/parser/array.mocha",
        "modules/gdscript/tests/scripts/parser/features/while.gd":
            "project/tests/upstream/parser/while.mocha",
        "modules/gdscript/tests/scripts/analyzer/errors/incompatible_assignment.gd":
            "project/tests/upstream/analyzer/incompatible_assignment.mocha",
    },
}


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def load_lock() -> dict:
    return json.loads(LOCK_PATH.read_text(encoding="utf-8"))


def resolve_godot_root(custom_godot: Path) -> Path:
    candidate = custom_godot / "godot"
    return candidate if candidate.is_dir() else custom_godot


def git_head(path: Path) -> str:
    return subprocess.run(
        ["git", "-C", str(path), "rev-parse", "HEAD"],
        check=True,
        text=True,
        stdout=subprocess.PIPE,
    ).stdout.strip()


def checked_sources(custom_godot: Path, component: str, lock: dict) -> dict[str, bytes]:
    godot_root = resolve_godot_root(custom_godot)
    expected_commit = lock["godot"]["commit"]
    actual_commit = git_head(godot_root)
    if actual_commit != expected_commit:
        raise SystemExit(
            f"Godot source mismatch: expected {expected_commit}, got {actual_commit}"
        )

    result: dict[str, bytes] = {}
    expected_hashes = lock["imports"]["files"]
    for relative in COMPONENTS[component]:
        source = godot_root / relative
        data = source.read_bytes()
        actual_hash = sha256(data)
        expected_hash = expected_hashes[relative]
        if actual_hash != expected_hash:
            raise SystemExit(
                f"Pinned source hash mismatch for {relative}: "
                f"expected {expected_hash}, got {actual_hash}"
            )
        result[relative] = data
    return result


def transform(relative: str, data: bytes) -> str:
    text = data.decode("utf-8")
    if relative.endswith(".gd"):
        return text
    text = text.replace("GDScriptTokenizer", "MochaTokenizer")
    text = text.replace("gdscript_tokenizer", "mocha_tokenizer")

    if relative.endswith(".h"):
        old = (
            '#include "core/templates/hash_map.h"\n'
            '#include "core/templates/list.h"\n'
            '#include "core/templates/vector.h"\n'
            '#include "core/variant/variant.h"\n'
        )
        new = '#include "platform/mocha_platform.h"\n\nnamespace mocha_script {\n'
    else:
        old = (
            '#include "mocha_tokenizer.h"\n\n'
            '#include "core/error/error_macros.h"\n'
            '#include "core/string/char_utils.h"\n\n'
            '#ifdef DEBUG_ENABLED\n'
            '#include "servers/text/text_server.h"\n'
            '#endif\n\n'
            '#ifdef TOOLS_ENABLED\n'
            '#include "editor/settings/editor_settings.h"\n'
            '#endif\n'
        )
        new = (
            '#include "mocha_tokenizer.h"\n\n'
            '#include "platform/mocha_platform.h"\n\n'
            'namespace mocha_script {\n'
        )
    if old not in text:
        raise SystemExit(f"transform v1 no longer applies cleanly to {relative}")
    return text.replace(old, new, 1).rstrip() + "\n\n} // namespace mocha_script\n"


def transformed_component(custom_godot: Path, component: str, lock: dict) -> dict[str, str]:
    sources = checked_sources(custom_godot, component, lock)
    return {
        COMPONENTS[component][relative]: transform(relative, data)
        for relative, data in sources.items()
    }


def initial_import(custom_godot: Path, component: str, lock: dict) -> None:
    generated = transformed_component(custom_godot, component, lock)
    collisions = [relative for relative in generated if (ROOT / relative).exists()]
    if collisions:
        raise SystemExit(
            "refusing to overwrite Mocha-owned files:\n  " + "\n  ".join(collisions)
        )

    for relative, contents in generated.items():
        destination = ROOT / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_text(contents, encoding="utf-8")

    provenance = {
        "schema": 1,
        "policy": "create-only; future upstream changes are review inputs",
        "godot_commit": lock["godot"]["commit"],
        "transform_version": lock["imports"]["transform_version"],
        "initial_import_date": date.today().isoformat(),
        "components": {},
    }
    if PROVENANCE_PATH.exists():
        provenance = json.loads(PROVENANCE_PATH.read_text(encoding="utf-8"))
    provenance["components"][component] = [
        {
            "source": source,
            "source_sha256": lock["imports"]["files"][source],
            "destination": destination,
            "initial_transformed_sha256": sha256(generated[destination].encode("utf-8")),
        }
        for source, destination in COMPONENTS[component].items()
    ]
    PROVENANCE_PATH.parent.mkdir(parents=True, exist_ok=True)
    PROVENANCE_PATH.write_text(json.dumps(provenance, indent=2) + "\n", encoding="utf-8")


def review(custom_godot: Path, component: str, lock: dict, output: Path | None) -> int:
    generated = transformed_component(custom_godot, component, lock)
    chunks: list[str] = []
    for relative, baseline in generated.items():
        destination = ROOT / relative
        current = destination.read_text(encoding="utf-8") if destination.exists() else ""
        chunks.extend(
            difflib.unified_diff(
                baseline.splitlines(keepends=True),
                current.splitlines(keepends=True),
                fromfile=f"pinned-bootstrap/{relative}",
                tofile=f"mocha-owned/{relative}",
            )
        )
    report = "".join(chunks)
    if output:
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(report, encoding="utf-8")
        print(f"wrote review delta to {output}")
    else:
        sys.stdout.write(report)
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=("initial-import", "verify-source", "review"))
    parser.add_argument("component", choices=tuple(COMPONENTS))
    parser.add_argument("--custom-godot", type=Path, required=True)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()

    lock = load_lock()
    if args.command == "verify-source":
        checked_sources(args.custom_godot, args.component, lock)
        print(f"{args.component}: pinned source and commit verified")
        return 0
    if args.command == "initial-import":
        initial_import(args.custom_godot, args.component, lock)
        print(f"{args.component}: initial import complete; files are now Mocha-owned")
        return 0
    return review(args.custom_godot, args.component, lock, args.output)


if __name__ == "__main__":
    raise SystemExit(main())
