# Repository Guidelines

## Project Structure & Module Organization

MochaScript is a C++17 GDExtension language for Godot. Core implementation lives in `src/`: `language/` owns tokenizing, parsing, analysis, and compilation; `runtime/` contains bytecode and the VM; `integration/` connects resources and script instances to Godot; `platform/` isolates engine-facing types; and `testing/` contains native assertions. The fixture Godot project and GDScript integration tests live under `project/`, with `.mocha` corpus files in `project/tests/`. Architecture, language rules, ADRs, and testing details are in `docs/`. Treat `godot-cpp/` as a pinned submodule, not project-owned source.

## Build, Test, and Development Commands

- `git submodule update --init --recursive` initializes the pinned binding dependency.
- `./scripts/install_scons.sh` installs the exact hash-verified SCons version into a user-local environment.
- `./scripts/setup.sh` checks tool, Godot, API, and submodule pins without modifying the checkout.
- `./scripts/build.sh` builds both debug and release libraries. Set `MOCHA_JOBS=2` to lower parallelism; the script enforces a safe maximum.
- `./scripts/verify.sh` is the canonical pre-push check: it builds both variants, validates resource UIDs, and runs native, frontend, editor, and VM tests headlessly.

## Coding Style & Naming Conventions

Follow `.editorconfig` and `.clang-format`: C++ uses tabs displayed at width four, attached braces, and Godot-style formatting; Python and SCons files use four spaces; YAML uses two. Use `snake_case` for files, functions, and variables, and `PascalCase` for C++ types. Keep engine adaptations behind `src/platform/`. Diagnostics must retain their layer prefix: `M0xxx` integration/tokenizer, `M1xxx` parser, `M2xxx` analyzer, `M3xxx` compiler, and `M4xxx` runtime.

## Testing Guidelines

Begin language changes with a failing Mocha-owned test and update `docs/language/specification.md` or the divergence ledger. Name GDScript tests `*_test.gd`; add native pipeline coverage for internal behavior and `.mocha` fixtures for integration behavior. Commit every generated `.uid` alongside its `.gd`, `.mocha`, or `.gdextension` resource. Run `./scripts/verify.sh` before submission; no separate coverage percentage is enforced.

## Commit & Pull Request Guidelines

Recent commits use concise, imperative subjects such as `Harden nested failures and member inference`; follow that style and keep each commit focused. Pull requests should explain behavior and design impact, link relevant issues, list specification or divergence updates, and report verification results. Include screenshots only for editor-visible changes. Do not modify dependency pins or imported provenance without documenting why and updating the associated lock or provenance records.
