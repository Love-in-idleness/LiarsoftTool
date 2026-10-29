# Developer reference: CNMI trial resources

[中文](CNMI_TRIAL_ENGINE_AUDIT.zh.md) | [English](CNMI_TRIAL_ENGINE_AUDIT.en.md) | [Русский](CNMI_TRIAL_ENGINE_AUDIT.ru.md) | [日本語](CNMI_TRIAL_ENGINE_AUDIT.ja.md)

[Documentation](README.en.md)

## Sample and scope

`ちんまい吸血姫体験版.exe`, SHA-256:
`21db7a449d329fb6bb030b1c1cf4050b0f519a52bd19167ba0d27dcc4cd6740e`.
These are static format findings, not gameplay verification. EXE editing is an explicitly enabled [experimental feature](EXE_EDITOR.en.md); the editor reports the supported operations.

PE headers, sections, size, compile time, and PDB identity match the [Chinese Evermaiden program](EVERMAIDEN_ENGINE_AUDIT.en.md).
Only ten font-charset bytes differ in `.text`, but strings, initial data, and PE resources also differ; they are not identical files.
Paths such as `0x44dc33–0x44dc35` still call `IsDBCSLeadByteEx(CP_ACP, ...)`; font parameters do not determine all runtime encoding.

## Empty LWG entries

`grps.xfl/compane.lwg` has 32 entries. Empty `slide_lev_f` and `slide` are at `(0,0)`, Flag 8.
Another, nonempty `slide` is at `(690,4)`, size `12×18`. Empty entries must not bind same-named images.

Extraction preserves empty names, coordinates, flags, and order with `empty="1"`, without generating images.
Packing respects the marker rather than looking for files. Archives containing only empty entries are still rejected.
Previously lost metadata requires re-extraction from the original LWG.

- `0x475722–0x47578c` reads all declared entries.
- `0x427af2` looks up `slide_lev`; `0x427b7c` associates the `slide` control.
- These paths establish the names' menu role, not all Flag 8 semantics.
- rscript2renpy must also preserve empty-entry identity so an empty duplicate cannot overwrite a nonempty entry's position.

## Data length

The four bytes after the table hold total payload length, not padding.
In `compane.lwg`, field `0x2fd` holds 30231 (`0x7617`).
File size is 31000; data starts at `0x301` and spans exactly 30231 bytes.

Packing writes the actual concatenated payload length; empty entries contribute zero.
The EXE reads this field at `0x475979–0x475981`, which alone does not prove zero causes a crash.
The decoder accepts older zero-field outputs. Coordinates, order, empty entries, and payload all matter:
successfully generating images is not a complete round-trip check.
