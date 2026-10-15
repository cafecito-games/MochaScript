# Source provenance and upstream review

`upstream.lock.json` is the machine-readable authority for every bootstrap input. The current
snapshot is Cafecito custom Godot release `v4.7.2-20260903.1`, outer commit
`e762551293cd7312e0fd6695b76b1050d7e15bdc`, with its Godot source at
`ed1daf0bf001b61586d9930840f2f1394092c079` (`4.7.2-stable`). The exact installed extension API has
SHA-256 `b61fb6208257c8d694463404d93d0b4d39e4d11a772babf012150bd57411d41b`; the Linux editor archive
and extracted executable are independently pinned in the lock.

The initial tokenizer files retain Godot's MIT notice. `provenance/source_imports.json` records the
source path, source hash, destination, transformation version, and initial transformed hash. The
pinned bootstrap corpus similarly retains exact upstream contents and hashes. `NOTICE.md` records
Godot, godot-cpp-template, and BaristaScript attribution.

## Reproduce the source snapshot

```sh
./scripts/materialize_upstream.sh
```

This materializes immutable checkouts under a commit-keyed directory in the persistent user cache,
verifies commits, patcher configuration, relevant patch hashes, and every imported file hash. Set
`MOCHA_CUSTOM_GODOT_SOURCE` to use an existing checkout. Each pin gets a separate default checkout;
the script refuses an explicitly selected checkout at another commit rather than rewriting it.
Concurrent materialization of the same pin is serialized with a checkout-specific file lock.

The original component imports were:

```sh
python3 scripts/import_gdscript.py initial-import tokenizer \
  --custom-godot "$MOCHA_CUSTOM_GODOT_SOURCE"
python3 scripts/import_gdscript.py initial-import bootstrap-fixtures \
  --custom-godot "$MOCHA_CUSTOM_GODOT_SOURCE"
```

Each component import is create-only and may run once. It refuses any existing destination and
serializes provenance updates so parallel component imports cannot lose ledger entries. After that
operation the destination is Mocha-owned forever.

## Review a future upstream change

```sh
./scripts/review_upstream.sh
```

The command materializes both the immutable baseline commit and candidate commit, verifies the
original source and transformed hashes recorded in `source_imports.json`, and verifies the candidate
pinned source. It writes separate raw upstream-only (`*-upstream.patch`) and
candidate-to-Mocha (`*-mocha.patch`) deltas under the user cache, and never modifies `src/` or
fixtures. Each default invocation creates a unique report directory prefixed by the candidate Godot
commit, so concurrent worktrees cannot mix results; an explicitly supplied directory must not
already exist and is claimed atomically. Materialization and component logs plus exit statuses are
saved with the reports. The raw report is written first and
remains available even when new upstream structure no
longer fits the recorded source transform; that condition leaves a per-file failure marker in the
Mocha report, records nonzero component status in `README.txt`, and makes the command fail only after
all available evidence has been saved.
To evaluate a newer Godot revision, update a branch's lock and expected hashes deliberately,
generate both reports, then manually port only changes that fit the current Mocha specification.
Never run a bulk overwrite. A GDScript behavior change is evidence, not a Mocha compatibility
requirement.
