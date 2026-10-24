# Working agreements

[中文](AGENTS.zh.md) | [English](AGENTS.en.md) | [Русский](AGENTS.ru.md) | [日本語](AGENTS.ja.md)

## Before starting

- Inspect Git status, preserve existing changes and use `rg` for searches.
- Read README, CMakeLists.txt and affected modules; TSC reference: `docs/GSC_OPCODE_REFERENCE.en.md`.
- Use the adjacent rscript2renpy repository's `local.paths.toml`; do not hard-code maintainer paths.

## Implementation boundaries

- This repository owns archives, GSC/TSC, encodings, images, audio and EXE handling. Ren'Py belongs in rscript2renpy.
- CLI and the shared Windows/Linux GTK4 GUI call the common conversion module; do not duplicate conversion paths.
- Verify unknown formats and engine behaviour against original files and EXEs; do not guess opcodes or pixel algorithms.
- Keep originals read-only; use copies or temporary directories for conversion and round-trip tests.
- Preserve local `test/`, `CodeXRScript/`, `res_ft/` and `ubai/`; neither clean nor commit game resources.
- Retain input checks, strict encoding, warnings and failed-output exclusion. Simpler code must not weaken safety.

## Documentation

- README is for users: purpose, installation, GUI operation and necessary limitations.
- Maintain CLI options and examples only in program help; point readers to `liarsofttool -h` rather than duplicating them.
- Follow the user's Chinese wording; English, Russian and Japanese carry the same information and structure.
- Maintain every project-authored document as separate `.zh.md`, `.en.md`, `.ru.md`, `.ja.md` files. The unsuffixed `.md` is navigation only; update all four together.
- Keep original licenses and third-party legal texts unchanged; translate project-authored explanations separately.
- Parser fields, legacy migration, error-handling implementation, test logs and commit histories do not belong in user guides.
- Keep actionable cautions: encoding, backup, version requirements and unsupported features.
- Opcode tables and reverse-engineering evidence belong in developer references; retain hashes, addresses, operand tables and unknowns.
- Check commands, arguments and local links; preserve licensing and attribution meanings.

## Validation

- Reuse configured build directories. Check source path and generator in CMakeCache; do not switch an existing generator.
- After code changes run `cmake --build build -j2`, `ctest --test-dir build --output-on-failure` and `git diff --check`, using actual configured paths.
- Check common conversion changes through CLI and available GUI; Windows changes also require target builds and available Wine regressions.
- GSC/TSC or resource-output changes require rscript2renpy consumption, generation and lint checks.
- Documentation-only changes require links and argument checks, not rebuilding or playing games.
- Distinguish automated tests, static analysis and gameplay; compilation alone does not establish compatibility.

## Git and permissions

- Commit verified independent tasks locally, including only task-owned files. Respect explicit requests not to commit.
- Do not push, tag, release or raise versions without a request.
- New branches use `codex/`; prefer the existing checkout.
- Confirm administrator operations; do not install build outputs globally or bypass directory permissions.
