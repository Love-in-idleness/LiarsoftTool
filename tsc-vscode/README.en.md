# CodeX TSC

[中文](README.zh.md) | [English](README.en.md) | [Русский](README.ru.md) | [日本語](README.ja.md)

Create TSC stories in VS Code with paragraph nodes, events and connections, then compile GSC for the original engine.
Profiles are available for **Japanese SevenBridge** and Forest.
The extension lives on `codex/tsc-vscode`, not the Marketplace. It does not generate Ren’Py projects or create, copy or patch game executables.

## Install

1. Install VS Code 1.74 or newer and LiarsoftTool from this branch; the editor requires its `--inspect-tsc` interface.
2. Choose **Install from VSIX** in Extensions and open the locally built VSIX.
3. Search settings for `liarsofttool.path` and enter the full path to the LiarsoftTool command-line program: `liarsofttool.exe` on Windows or `liarsofttool` on Linux.

Operations require Workspace Trust. In a remote workspace, install the compiler on the remote host too.
Restricted Mode provides source highlighting only. Menus, editor panels and extension messages follow VS Code's Chinese, Japanese, English or Russian setting; other languages fall back to English.
The interface language does not change project encoding or existing story text. Compiler diagnostics and system errors retain their original wording.

## Start a story

1. Prepare a separate, unpacked working copy of legally owned game resources. Keep originals read-only; keep the resource copy outside the authoring project.
2. Run **CodeX TSC: New TSC project**, choose SevenBridge, the resource copy and a new project folder. The first scene defaults to `2001`.
3. Add dialogue, backgrounds, sprites, music, sound, voice or waits inside the Start paragraph.
4. Add paragraph, condition, choice or end nodes. Drag an output port onto another node's input port.
5. Save and click Build. Output goes to the project's `build/`; it is never installed into a game automatically.

Story sources are in `scr/*.tsc`; imported resource copies go in `assets/`.
`tsc.project.json` holds the engine profile, resources, characters, variables, asset mappings, output encoding and relative output directory. Open it with Settings.
TSC is always UTF-8. GSC defaults to CP932; the target game must support the selected encoding.

## Editing and resources

- Move events up/down or delete them; edit dialogue directly. Raw parameters remain available for existing scripts and commands.
- Conditions compare integer variables; choices have 1–5 options. Add named variables before using conditions or set/add/subtract events.
  Choose registers that are not used by the original scripts; the extension does not reserve a free register range.
- Characters refer to native `gfNNN` nameplate images; `999` means narration. Import a nameplate asset when adding a new name image.
- Import PNG/JPEG/BMP images and OGG/WAV audio, assigning a name and number. Resource-number inputs suggest imported asset names.
  Reusing an original number only creates a replacement in exported output.
- Build converts images to WCG and OGG to PCM WAV, leaving game resources untouched.
  SevenBridge voice IDs are grouped by 10000, e.g. `10001 → voice/1/0001.wav`.

Use Source to return to the text editor, or **Reopen Editor With → Open story graph** to return to the graph.
Renaming updates local label references; referenced nodes cannot be deleted directly. Invalid scripts are view-only until fixed.
Opening a graph does not rewrite unknown commands, original comments or GSC metadata.
Positions and event groups are TSC comments, not a second story database.

## Preview and testing in the game

Static preview shows known backgrounds, sprites, nameplates and dialogue. Audio playback is manual.
LWG frame coordinates come from `.meta.xml`; only the first frame is previewed.
Choose an incoming context for merge nodes. Cycles, dynamic expressions, non-default anchoring and unimplemented commands produce limitation notices.
Preview is not engine execution and does not verify typography, effects, menus or complete story flow.

New SevenBridge sources use `modern-36 / rscript18`; the original entry path is `0100 → 0110 → 2001`.
Forest uses `legacy-28 / early`, with the first story normally entered at `1000`.
Imported TSC keeps its dialect; it is not silently normalized.
Exported scenes still need original startup/menu scripts and resources. Repack or place `build/` files at the resource locations actually read by a
**separate native-engine test copy**. Never overwrite originals. The extension does not deploy or launch a game.
Run `liarsofttool -h` for conversion options.

The standalone Check saved file / Compile saved file commands still work without a project, using VS Code's encoding/output settings.
Project Build uses `tsc.project.json`. Empty single-file output writes beside the TSC and may replace GSC.
Diagnostics appear in Problems; tool failures are detailed in Output → LiarsoftTool TSC.
The older Windows conversion entry point still has system-code-page path limits; use representable paths if compilation fails.

## Build locally

Use Node.js 22 or newer and a built LiarsoftTool from this branch:

```bash
cd tsc-vscode
npm ci
npm test
npm run package -- --out liarsofttool-tsc.vsix
```

Tests accept `LIARSOFTTOOL_PATH`. `npm run build` bundles the offline frontend.

[Interfaces and engine evidence](AUTHORING.en.md) · [TSC reference](../docs/GSC_OPCODE_REFERENCE.en.md)

The extension is GPLv3. The frontend uses React, React Flow and their dependencies.
The VSIX includes their full licenses in `dist/THIRD_PARTY_LICENSES.txt`; build tools are not shipped. No game resources are provided.
