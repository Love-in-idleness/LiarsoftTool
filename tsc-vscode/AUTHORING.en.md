# TSC authoring: developer reference

[中文](AUTHORING.zh.md) | [English](AUTHORING.en.md) | [Русский](AUTHORING.ru.md) | [日本語](AUTHORING.ja.md)

Graphical editing is disabled. This reference describes the retained implementation; restore the editor entry point and frontend build before using graphical test tools. See the [README](README.en.md) for available extension features.

[User guide](README.en.md)

## Source contract

C++ `inspectTsc` shares the compiler's tokenizer, layouts and label validation.
`--inspect-tsc <file>` or `--inspect-tsc -` returns protocol 1 JSON without writing GSC.
See [TSC inspection API](../docs/TSC_DIAGNOSTICS.en.md). Ranges are UTF-8 byte offsets;
the extension checks boundaries and converts them to VS Code UTF-16 positions.

`graph.js` projects compiler records, not a second TSC grammar. Labels are nodes and branches/choices are edges;
implicit sequential fallthrough is retained. Commands after return/end/jump stay in source but do not create executable local edges.
Edits replace selected tokens/event ranges only. Candidate source must pass C++ checks before an undoable WorkspaceEdit.
Stale document versions and syntactically invalid sources cannot be mutated. Unrelated comments/unknown commands are not reordered.

Project JSON is not a second story source. `;@editor-node` stores titles/positions and
`;@editor-event` / `;@editor-event-end` group native commands into an event. Other comments remain ordinary comments.
Build preflights all scripts, target encoding and resolvable scene calls before producing files.
Resolve dynamic calls in source first. Output must not overlap scr, assets or resources; symlinks inside these paths are rejected.
Files use sibling temporary replacements. There is no whole-project transaction: fix resource conversion failures and rebuild.

## SevenBridge profile

Target: Japanese `SBridge_jp.exe`, SHA-256:

```text
c7d5fc38cf0c354a61d056b607cf1367b2529557e9e0407d0b0fdf12771b35e3
```

[Original-program evidence](https://github.com/Love-in-idleness/rscript2renpy/blob/main/sevenbridge/NATIVE_EVIDENCE.en.md):
dispatch `0x41cc40`, three-operand locmode `0x420220`, folder `0x4204e0`,
SE cases `0x3e–0x40`, voice path builder `0x41b5b0`.
Defaults are checked against this game's scripts, not inferred from another title. These are static findings, not native-engine runtime verification.

| Event | SevenBridge output shape | Script evidence |
| --- | --- | --- |
| Coordinates/folder | `locmode obj 0 0`, `folder obj "grpo"` | 0000 |
| Dialogue | `TXT 0 voice 0 0 "" "^gNNNtext" 1` | 2001 |
| Image | queue, folder, locmode, `load layer id x y 0 0`, `update 0 0 0` | 0000, 2001 |
| Clear | queue, `cls layer 0`, update | 2001 |
| Music | `bgm_on track 1 0`, `bgm_off 0 0` | 2001 |
| SE/voice | `se id` + `se_on 0 0 0`; `voice id 0 0 0` | 2001 |
| Call/return | `gosub scene ""` + ten zeros; `return 0` | 2001 |
| Choice | five target slots, five text slots, trailing `0 0 0` | native select layout, 1309 |
| Variables | `0x1800` set; `0xa800/b800` + `0x1900` writeback; `0x4800–9800` compare | 0000 and shared VM |

New sources use `modern-36 / rscript18`: three-operand locmode and single-channel SE.
Existing rscript19/modern files remain unchanged; incompatible event insertion fails rather than changing dialect.
Forest retains `legacy-28 / early`: gosub takes just the scene, bgm_on has a single trailing `1`, select ends in `2 0 0`.
Evidence: Forest's original 0000/2100 and the core early layout.

## Verification boundary

Node tests cover source preservation, Unicode, conditions/choices, events, path protection, failed builds and preview offsets.
`test/host.js` checks diagnostics and GSC compilation in the real VS Code extension host.
`test/browser.js` drives the real provider/compiler with isolated test data.
These checks are not game playthroughs. Native Windows paths, actual typography and complete story behavior still need manual acceptance.

Run `npm test` in `tsc-vscode/` for shared editor checks; set `LIARSOFTTOOL_PATH` to select the compiler.
`node test/browser.js` serves an isolated temporary SevenBridge project on localhost and cleans it on exit; no game assets are required.
