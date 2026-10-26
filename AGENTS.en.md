# Repository guidelines

[中文](AGENTS.zh.md) | [English](AGENTS.en.md) | [Русский](AGENTS.ru.md) | [日本語](AGENTS.ja.md)

## Change boundaries

- Check Git status, preserve existing edits, and read README, CMake configuration, and affected modules.
- This repository owns archives, GSC/TSC, encodings, images, and audio conversion. Ren'Py generation and runtime belong in rscript2renpy.
- CLI and Windows/Linux GUI use the common conversion entry points; do not maintain duplicate conversion paths.
- Establish unknown formats and engine behavior from original files, EXEs, and reproducible analysis; do not guess opcodes or pixel algorithms.
- Keep originals read-only; use copies or temporary directories for tests. Do not remove or commit private assets, generated files, or personal configuration.
- Preserve boundary checks, encoding failures, warnings, and file-level safe writes. Simpler code must not weaken data protection.
- Branch documentation must describe features available in that branch; documentation synchronization must not merge feature code.

## Documentation

- READMEs serve users. Keep developer APIs and native evidence in technical references, build steps in the build guide.
- Maintain CLI options and conversion examples in program help only; point README readers to `liarsofttool -h`.
- Keep separate `.zh.md`, `.en.md`, `.ru.md`, and `.ja.md` versions; unsuffixed files only select a language.
- Align facts and structure. Use replaceable paths without maintainer-specific configuration.
- Omit task diaries, chat decisions, commit histories, and obsolete test counts. Keep current behavior, practical limits, and reproducible evidence.
- Preserve original licenses, attribution, provenance, and necessary unknowns. Remove references to deleted documents.

## Checks

See [Building](docs/BUILDING.en.md) for configuration. Run from the repository root:

```bash
cmake --build build -j2
ctest --test-dir build --output-on-failure
git diff --check
```

- Before reusing a build directory, check source path and generator in CMakeCache; do not change its toolchain in place.
- Check shared conversions through CLI and available GUI; cross-platform changes also need Windows builds and available Wine regressions.
- Output-format changes require downstream rscript2renpy reading, generation, and lint checks.
- For documentation, check links, anchors, fences, actual help, and diffs; no rebuild or game run is required.
- Report tests, static analysis, and device verification separately. Do not re-download release packages for verification.

## Git and permissions

- Commit independently verified tasks separately and locally, including only relevant files.
- Pushes, tags, releases, and version changes require an explicit request; new branches use `codex/`.
- Do not execute unknown EXEs, perform administrator operations, install globally, or bypass permissions without authorization.
