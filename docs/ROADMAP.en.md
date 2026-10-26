# Roadmap and experimental branches

[中文](ROADMAP.zh.md) | [English](ROADMAP.en.md) | [Русский](ROADMAP.ru.md) | [日本語](ROADMAP.ja.md)

[Main guide](../README.en.md)

## Main program

The main program provides resource conversion, a GTK4/libadwaita GUI, and browsing/previews without prior unpacking.
Further work focuses on format compatibility, cross-platform usability, and shared interfaces for editing tools.
See the main guide for current usage; this page is not an operation manual.

## Experimental branches

| Branch | Contents |
| --- | --- |
| `codex/tsc-vscode` | [VS Code extension and graphical TSC authoring](https://github.com/Love-in-idleness/LiarsoftTool/blob/codex/tsc-vscode/tsc-vscode/README.en.md): highlighting, checking, compilation, and story editing; SevenBridge and Forest profiles |
| `codex/rscript-runtime` | [Windows runtime loader and optional DLLs](https://github.com/Love-in-idleness/LiarsoftTool/blob/codex/rscript-runtime/runtime/README.en.md): loose-resource overrides and experimental UTF-8 support |

These branches are not merged into the main program and may not share every main-branch feature or resource-format update.
Read the relevant branch guide and test against your target game.

## Long-term goals

Improve semantic TSC authoring and resource management. Full engine emulation and a universal game runner are not current editor features.

The native Ren'Py backend `--pure-renpy` is tracked in the separate
[rscript2renpy roadmap](https://github.com/Love-in-idleness/rscript2renpy/blob/main/docs/ROADMAP.en.md) and is not implemented.
