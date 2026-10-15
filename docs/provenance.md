# Source provenance and upstream review

`upstream.lock.json` is the machine-readable authority for every bootstrap input. The current
snapshot is Cafecito custom Godot release `v4.7.2-20260903.1`, outer commit
`e762551293cd7312e0fd6695b76b1050d7e15bdc`, with its Godot source at
`ed1daf0bf001b61586d9930840f2f1394092c079` (`4.7.2-stable`). The exact installed extension API has
SHA-256 `b61fb6208257c8d694463404d93d0b4d39e4d11a772babf012150bd57411d41b`.

The initial tokenizer files retain Godot's MIT notice. `provenance/source_imports.json` records the
source path, source hash, destination, transformation version, and initial transformed hash. The
pinned bootstrap corpus similarly retains exact upstream contents and hashes. `NOTICE.md` records
Godot, godot-cpp-template, and BaristaScript attribution.

## Reproduce the source snapshot

```sh
./scripts/materialize_upstream.sh
```

This materializes immutable checkouts under the persistent user cache, verifies commits, patcher
configuration, relevant patch hashes, and every imported file hash. Set
`MOCHA_CUSTOM_GODOT_SOURCE` to use an existing checkout. The script refuses an existing checkout at
another commit rather than rewriting it.

The original component imports were:

```sh
python3 scripts/import_gdscript.py initial-import tokenizer \
  --custom-godot "$MOCHA_CUSTOM_GODOT_SOURCE"
python3 scripts/import_gdscript.py initial-import bootstrap-fixtures \
  --custom-godot "$MOCHA_CUSTOM_GODOT_SOURCE"
```

Each component import is create-only and may run once. It refuses any existing destination. After
that operation the destination is Mocha-owned forever.

## Review a future upstream change

```sh
./scripts/review_upstream.sh
```

The command verifies both the immutable original import recorded in `source_imports.json` and the
candidate pinned source. It writes separate raw upstream-only (`*-upstream.patch`) and
candidate-to-Mocha (`*-mocha.patch`) deltas under the user cache, and never modifies `src/` or
fixtures. The raw report is written first and remains available even when new upstream structure no
longer fits the recorded source transform; that condition produces a warning for the Mocha report.
To evaluate a newer Godot revision, update a branch's lock and expected hashes deliberately,
generate both reports, then manually port only changes that fit the current Mocha specification.
Never run a bulk overwrite. A GDScript behavior change is evidence, not a Mocha compatibility
requirement.
