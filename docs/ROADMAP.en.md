# Development roadmap

[中文](ROADMAP.zh.md) | [English](ROADMAP.en.md) | [Русский](ROADMAP.ru.md)

[Project guide](../README.en.md)

## Current sequence

1. Make converter fixes required for the Japanese SevenBridge port in rscript2renpy.
2. Replace the GUI with C++ calling GTK4/libadwaita's C API. Verify Linux and Windows minimal windows and packages first; provide system/light/dark themes and English, Russian and Simplified Chinese.
3. Add browsing without prior extraction: directories, XFL, LWG and nested archives, reading on demand and previewing images, audio, text, GSC/TSC and raw bytes in memory.
4. Develop highlighting, checking and local-compiler invocation on the independent `codex/tsc-vscode` branch. Reuse the C++ checker and expose a check-only CLI that writes no GSC.

Keep common conversion entry points, options and defaults. Browsing does not change inputs; export and conversion require explicit user action.
Provide Chinese, English and Russian documentation together. Do not automatically push, release or raise versions.
This is a plan, not a list of shipped features.

## Long-term: graphical TSC editor

A future editor will support dialogue, resources, conditions, choices and flow using LiarsoftTool checking and compilation.
Interaction and supported scope remain undecided. This phase adds neither an implementation nor an editor framework.

Experimental `--pure-renpy` is a long-term rscript2renpy goal: see its
[roadmap](https://github.com/Love-in-idleness/rscript2renpy/blob/main/docs/ROADMAP.en.md).
Do not implement that backend or add a placeholder option in this phase.
