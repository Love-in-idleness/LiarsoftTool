# EXE editor (experimental)

[中文](EXE_EDITOR.zh.md) | [English](EXE_EDITOR.en.md) | [Русский](EXE_EDITOR.ru.md) | [日本語](EXE_EDITOR.ja.md)

Edit font charsets, embedded text and line-break rules in recognized CodeX RScript PE32/x86 games. The feature is off by default and saves a separate EXE.

## Use

Enable **EXE editor (experimental)** in Settings under Experimental features and open the **EXE editor** page.

1. Open an EXE and confirm its text source encoding. Reinspect with an explicit encoding if the text is garbled or uncertain. A `gbk` filename or font charset does not prove that the embedded text was converted.
2. Select the target encoding. Edit identified window titles, control prompts, error messages and scene captions. Originals remain visible; entries with unverified references are read-only.
3. Select **Use target encoding default configuration** to fill line-break rules, recognized stock prompt translations and standard font names. Manually edited translations remain intact. Translate game-specific titles and scene names yourself.
4. Review the rules, optionally edit the sets or load JSON, then choose **Save as**. The output cannot be the input or a hard-link/symbolic-link alias.

Switching files, closing the window or disabling the feature offers save, discard and cancel. Inspection and generation run in the background; cancellation discards stale results. If saving already finished, the interface says so.

The CLI supports inspection without writes, editing by text ID and default configuration. Options and examples are in `liarsofttool -h`. Ordinary resource conversion and recursion do not edit EXEs.

## Encoding and defaults

EXE targets are CP932, GBK, CP950, CP949 and CP1250–CP1258. Font charset, text source encoding and Windows system locale are separate settings. A matching ANSI code page and fonts are still required.

| Target encoding | Default stock prompt language |
| --- | --- |
| CP932 | Japanese |
| GBK | Simplified Chinese |
| CP950 | Traditional Chinese |
| CP949 | Korean |
| CP1251 | Russian |
| Other CP125x | English |

Defaults match known original stock text or its standard translations. They do not replace game-specific or custom text. Encoding changes transcode identified text; translate characters unavailable in the target encoding before saving.

UTF-8 is not a resource encoding option; EXE editing also rejects GB18030. TSC, JSON, the UI and paths retain UTF-8, and the Unicode runtime DLL remains available. Complex layout such as Arabic shaping is not implemented.

Rule JSON follows the [runtime format](../runtime/kinsoku.json): `version` is `1`, with character sets in `no_line_start` and `no_line_end`. Sets must not overlap or contain NUL or unencodable characters. Rule count is not limited by the original table.

## Text coverage and limits

- Confirmation prompts with verified references support long translations by relocation. Game windows are not redesigned; check wrapping in the game.
- APINI titles, scene tables and other fixed fields show their byte capacity. Oversized translations fail without truncation. External `RsInit.cfg` or `RsInit.dll` may override the embedded title.
- Other code-referenced text uses a safe byte capacity. Keep format placeholders intact. Resource keys, file paths and control tokens are not ordinary translation entries.
- Image-based menus need translated image assets. General Windows-resource editing, unpacking protected files, signed EXEs and unrecognized code layouts are unsupported.

Generated EXEs can be reopened for further editing. Inspection does not start games or load their DLLs. Saving rechecks the source and preserves an existing output on failure.

## Inspected native samples

Static checks include SevenBridge's `SBridge.gbk.exe` (SHA-256 `ca0b635abe9861652182df0ed2ccc6854b580b1c8b41aa8ccf4368390944b74d`) and Kagerou's `kagerou.exe` (`f02998fae350945f4de4dc7eab47997e1e2d9b8aa43d49740a80aad893dcde9b`). Neither has standard menu/string resources; menu images need separate translation. The former has GBK font settings but CP932 stock text. The latter contains an APINI title and twelve scene captions.

Recognition uses PE structures, instructions and data layouts, not game names or fixed addresses. Tests and static checks do not establish complete localization or replace native Windows and full gameplay acceptance.
