# Development roadmap

[中文](ROADMAP.zh.md) | [English](ROADMAP.en.md) | [Русский](ROADMAP.ru.md) | [日本語](ROADMAP.ja.md)

[Project guide](../README.en.md)

## Current sequence

1. Make converter fixes required for the Japanese SevenBridge port in rscript2renpy.
2. Replace the GUI with C++ calling GTK4/libadwaita's C API. Verify Linux and Windows minimal windows and packages first; provide system/light/dark themes and English, Russian, Simplified Chinese and Japanese.
3. Add browsing without prior extraction: directories, XFL, LWG and nested archives, reading on demand and previewing images, audio, text, GSC/TSC and raw bytes in memory.
4. Develop highlighting, checking and local-compiler invocation on the independent `codex/tsc-vscode` branch. Reuse the C++ checker and expose a check-only CLI that writes no GSC.

Keep common conversion entry points, options and defaults. Browsing does not change inputs; export and conversion require explicit user action.
Provide Chinese, English, Russian and Japanese documentation together. Do not automatically push, release or raise versions.
This is a plan, not a list of shipped features.

## Graphical TSC editor: first version on its own branch

The latest scope adds paragraph/event/connection authoring, source switching, static resource previews and native-engine export on `codex/tsc-vscode`.
Japanese SevenBridge is the priority, with a Forest profile retained; other games are not being added.
This first version still needs original-game acceptance. It is not an engine simulator and is not merged into master. See the [guide](../tsc-vscode/README.en.md).

Experimental `--pure-renpy` is a long-term rscript2renpy goal: see its
[roadmap](https://github.com/Love-in-idleness/rscript2renpy/blob/main/docs/ROADMAP.en.md).
Do not implement that backend or add a placeholder option in this phase.
