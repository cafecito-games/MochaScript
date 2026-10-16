#!/usr/bin/env python3
"""Materialize or review the pinned GDScript bootstrap source.

Initial import is deliberately create-only. Once files exist under src/language,
MochaScript owns them; `review` writes a report without changing those files.
"""

from __future__ import annotations

import argparse
import difflib
import fcntl
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
        if not source.is_file():
            raise SystemExit(f"Pinned source file is absent: {relative}")
        data = source.read_bytes()
        actual_hash = sha256(data)
        expected_hash = expected_hashes.get(relative)
        if expected_hash is None:
            raise SystemExit(f"Pinned source hash is absent from the lock: {relative}")
        if actual_hash != expected_hash:
            raise SystemExit(
                f"Pinned source hash mismatch for {relative}: "
                f"expected {expected_hash}, got {actual_hash}"
            )
        result[relative] = data
    return result


def review_candidate_sources(custom_godot: Path, component: str,
        lock: dict) -> tuple[dict[str, bytes | None], dict[str, str]]:
    godot_root = resolve_godot_root(custom_godot)
    expected_commit = lock["godot"]["commit"]
    actual_commit = git_head(godot_root)
    if actual_commit != expected_commit:
        raise SystemExit(
            f"Godot source mismatch: expected {expected_commit}, got {actual_commit}"
        )

    result: dict[str, bytes | None] = {}
    issues: dict[str, str] = {}
    expected_hashes = lock["imports"]["files"]
    for relative in COMPONENTS[component]:
        source = godot_root / relative
        if not source.is_file():
            result[relative] = None
            issues[relative] = "candidate file is absent"
            continue
        data = source.read_bytes()
        result[relative] = data
        actual_hash = sha256(data)
        expected_hash = expected_hashes.get(relative)
        if expected_hash is None:
            issues[relative] = f"candidate hash is not pinned (actual {actual_hash})"
        elif actual_hash != expected_hash:
            issues[relative] = (
                f"candidate hash mismatch: expected {expected_hash}, got {actual_hash}"
            )
    return result, issues


def transform(relative: str, data: bytes, version: int) -> str:
    if version != 1:
        raise SystemExit(f"unsupported import transform version {version}")
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
        COMPONENTS[component][relative]: transform(
            relative, data, lock["imports"]["transform_version"]
        )
        for relative, data in sources.items()
    }


def component_provenance(provenance: dict, component: str) -> tuple[str, int, list[dict]]:
    record = provenance.get("components", {}).get(component)
    if not record:
        raise SystemExit(f"component {component!r} has no immutable import provenance")
    if isinstance(record, list):
        return provenance["godot_commit"], provenance["transform_version"], record
    return record["godot_commit"], record["transform_version"], record["entries"]


def provenance_sources(custom_godot: Path, component: str, lock: dict) -> dict[str, bytes]:
    provenance = json.loads(PROVENANCE_PATH.read_text(encoding="utf-8"))
    baseline_commit, transform_version, entries = component_provenance(provenance, component)

    godot_root = resolve_godot_root(custom_godot)
    sources: dict[str, bytes] = {}
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
        transformed = transform(relative, data, transform_version)
        if sha256(transformed.encode("utf-8")) != entry["initial_transformed_sha256"]:
            raise SystemExit(f"provenance transformed hash mismatch for {relative}")
        sources[relative] = data
    return sources


def initial_import(custom_godot: Path, component: str, lock: dict) -> None:
    PROVENANCE_PATH.parent.mkdir(parents=True, exist_ok=True)
    lock_path = PROVENANCE_PATH.with_name(f".{PROVENANCE_PATH.name}.lock")
    with lock_path.open("a+", encoding="utf-8") as lock_stream:
        fcntl.flock(lock_stream.fileno(), fcntl.LOCK_EX)
        initial_import_locked(custom_godot, component, lock)


def initial_import_locked(custom_godot: Path, component: str, lock: dict) -> None:
    generated = transformed_component(custom_godot, component, lock)
    provenance = {
        "schema": 2,
        "policy": "create-only; future upstream changes are review inputs",
        "initial_import_date": date.today().isoformat(),
        "components": {},
    }
    if PROVENANCE_PATH.exists():
        provenance = json.loads(PROVENANCE_PATH.read_text(encoding="utf-8"))
        if provenance.get("schema") == 1:
            baseline_commit = provenance.pop("godot_commit")
            transform_version = provenance.pop("transform_version")
            provenance["components"] = {
                name: {
                    "godot_commit": baseline_commit,
                    "transform_version": transform_version,
                    "entries": entries,
                }
                for name, entries in provenance["components"].items()
            }
            provenance["schema"] = 2
        if provenance.get("schema") != 2:
            raise SystemExit("unsupported provenance schema")
    if component in provenance.get("components", {}):
        raise SystemExit(f"component {component!r} already has immutable import provenance")

    collisions = [relative for relative in generated if (ROOT / relative).exists()]
    if collisions:
        raise SystemExit(
            "refusing to overwrite Mocha-owned files:\n  " + "\n  ".join(collisions)
        )

    provenance["components"][component] = {
        "godot_commit": lock["godot"]["commit"],
        "transform_version": lock["imports"]["transform_version"],
        "entries": [
            {
                "source": source,
                "source_sha256": lock["imports"]["files"][source],
                "destination": destination,
                "initial_transformed_sha256": sha256(generated[destination].encode("utf-8")),
            }
            for source, destination in COMPONENTS[component].items()
        ],
    }
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
    baseline_sources = provenance_sources(custom_godot, component, lock)
    candidate_sources, candidate_issues = review_candidate_sources(
        custom_godot, component, lock
    )
    provenance = json.loads(PROVENANCE_PATH.read_text(encoding="utf-8"))
    baseline_commit, _transform_version, _entries = component_provenance(provenance, component)
    upstream_chunks: list[str] = []
    for source, candidate_data in candidate_sources.items():
        baseline_data = baseline_sources[source]
        if source in candidate_issues:
            upstream_chunks.append(f"# CANDIDATE INCOMPLETE for {source}: {candidate_issues[source]}\n")
        if candidate_data is None:
            candidate_data = b""
        upstream_chunks.extend(
            difflib.unified_diff(
                baseline_data.decode("utf-8").splitlines(keepends=True),
                candidate_data.decode("utf-8").splitlines(keepends=True),
                fromfile=f"upstream-{baseline_commit}/{source}",
                tofile=f"upstream-{lock['godot']['commit']}/{source}",
            )
        )
    write_report(output, "".join(upstream_chunks), "upstream-only delta")

    mocha_chunks: list[str] = []
    review_incomplete = bool(candidate_issues)
    for source, candidate_data in candidate_sources.items():
        relative = COMPONENTS[component][source]
        if source in candidate_issues:
            mocha_chunks.append(f"# CANDIDATE INCOMPLETE for {source}: {candidate_issues[source]}\n")
        if candidate_data is None:
            destination = ROOT / relative
            current = destination.read_text(encoding="utf-8") if destination.exists() else ""
            mocha_chunks.extend(
                difflib.unified_diff(
                    [],
                    current.splitlines(keepends=True),
                    fromfile=f"candidate-upstream/{relative} (absent)",
                    tofile=f"mocha-owned/{relative}",
                )
            )
            continue
        try:
            candidate_text = transform(
                source, candidate_data, lock["imports"]["transform_version"]
            )
        except SystemExit as error:
            review_incomplete = True
            mocha_chunks.append(f"# TRANSFORM FAILED for {source}: {error}\n")
            print(
                f"warning: upstream-only report is complete, but the Mocha divergence "
                f"transform failed for {source}: {error}",
                file=sys.stderr,
            )
            continue
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
    if mocha_output:
        write_report(mocha_output, "".join(mocha_chunks), "Mocha-owned divergence delta")
    for source, issue in candidate_issues.items():
        print(f"warning: candidate review is incomplete for {source}: {issue}", file=sys.stderr)
    return 2 if review_incomplete else 0


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
