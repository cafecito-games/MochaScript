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
import os
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


def transformed_provenance(custom_godot: Path, component: str, lock: dict) -> dict[str, str]:
    provenance = json.loads(PROVENANCE_PATH.read_text(encoding="utf-8"))
    if provenance["transform_version"] != lock["imports"]["transform_version"]:
        raise SystemExit("provenance and candidate use different transform versions")
    entries = provenance.get("components", {}).get(component)
    if not entries:
        raise SystemExit(f"component {component!r} has no immutable import provenance")

    godot_root = resolve_godot_root(custom_godot)
    baseline_commit = provenance["godot_commit"]
    generated: dict[str, str] = {}
    expected = COMPONENTS[component]
    if {entry["source"] for entry in entries} != set(expected):
        raise SystemExit(f"component {component!r} provenance does not match the import map")
    for entry in entries:
        relative = entry["source"]
        data = subprocess.run(
            ["git", "-C", str(godot_root), "show", f"{baseline_commit}:{relative}"],
            check=True,
            stdout=subprocess.PIPE,
        ).stdout
        if sha256(data) != entry["source_sha256"]:
            raise SystemExit(f"provenance source hash mismatch for {relative}")
        destination = expected[relative]
        if entry["destination"] != destination:
            raise SystemExit(f"provenance destination mismatch for {relative}")
        transformed = transform(relative, data)
        if sha256(transformed.encode("utf-8")) != entry["initial_transformed_sha256"]:
            raise SystemExit(f"provenance transformed hash mismatch for {relative}")
        generated[destination] = transformed
    return generated


def initial_import(custom_godot: Path, component: str, lock: dict) -> None:
    generated = transformed_component(custom_godot, component, lock)
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
        if provenance.get("godot_commit") != lock["godot"]["commit"]:
            raise SystemExit("existing provenance belongs to a different Godot commit")
        if provenance.get("transform_version") != lock["imports"]["transform_version"]:
            raise SystemExit("existing provenance uses a different transform version")
    if component in provenance.get("components", {}):
        raise SystemExit(f"component {component!r} already has immutable import provenance")

    collisions = [relative for relative in generated if (ROOT / relative).exists()]
    if collisions:
        raise SystemExit(
            "refusing to overwrite Mocha-owned files:\n  " + "\n  ".join(collisions)
        )

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
    staged: list[tuple[Path, Path]] = []
    promoted: list[Path] = []
    provenance_temp = PROVENANCE_PATH.with_name(f".{PROVENANCE_PATH.name}.{os.getpid()}.tmp")
    try:
        for relative, contents in generated.items():
            destination = ROOT / relative
            destination.parent.mkdir(parents=True, exist_ok=True)
            temporary = destination.with_name(f".{destination.name}.{os.getpid()}.tmp")
            descriptor = os.open(temporary, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o644)
            staged.append((temporary, destination))
            with os.fdopen(descriptor, "w", encoding="utf-8") as stream:
                stream.write(contents)

        for temporary, destination in staged:
            os.link(temporary, destination)
            promoted.append(destination)

        descriptor = os.open(provenance_temp, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o644)
        with os.fdopen(descriptor, "w", encoding="utf-8") as stream:
            stream.write(json.dumps(provenance, indent=2) + "\n")
        os.replace(provenance_temp, PROVENANCE_PATH)
    except BaseException:
        for destination in reversed(promoted):
            destination.unlink(missing_ok=True)
        raise
    finally:
        provenance_temp.unlink(missing_ok=True)
        for temporary, _destination in staged:
            temporary.unlink(missing_ok=True)


def write_report(output: Path | None, report: str, label: str) -> None:
    if output:
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(report, encoding="utf-8")
        print(f"wrote {label} to {output}")
    else:
        sys.stdout.write(report)


def review(custom_godot: Path, component: str, lock: dict, output: Path | None,
        mocha_output: Path | None) -> int:
    baseline = transformed_provenance(custom_godot, component, lock)
    candidate = transformed_component(custom_godot, component, lock)
    provenance = json.loads(PROVENANCE_PATH.read_text(encoding="utf-8"))
    upstream_chunks: list[str] = []
    mocha_chunks: list[str] = []
    for relative, candidate_text in candidate.items():
        baseline_text = baseline[relative]
        upstream_chunks.extend(
            difflib.unified_diff(
                baseline_text.splitlines(keepends=True),
                candidate_text.splitlines(keepends=True),
                fromfile=f"upstream-{provenance['godot_commit']}/{relative}",
                tofile=f"upstream-{lock['godot']['commit']}/{relative}",
            )
        )
        destination = ROOT / relative
        current = destination.read_text(encoding="utf-8") if destination.exists() else ""
        mocha_chunks.extend(
            difflib.unified_diff(
                candidate_text.splitlines(keepends=True),
                current.splitlines(keepends=True),
                fromfile=f"candidate-upstream/{relative}",
                tofile=f"mocha-owned/{relative}",
            )
        )
    write_report(output, "".join(upstream_chunks), "upstream-only delta")
    if mocha_output:
        write_report(mocha_output, "".join(mocha_chunks), "Mocha-owned divergence delta")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=("initial-import", "verify-source", "review"))
    parser.add_argument("component", choices=tuple(COMPONENTS))
    parser.add_argument("--custom-godot", type=Path, required=True)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--mocha-output", type=Path)
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
    return review(args.custom_godot, args.component, lock, args.output, args.mocha_output)


if __name__ == "__main__":
    raise SystemExit(main())
