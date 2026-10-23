# Developer reference: CNMI trial resources

[中文](CNMI_TRIAL_ENGINE_AUDIT.zh.md) | [English](CNMI_TRIAL_ENGINE_AUDIT.en.md) | [Русский](CNMI_TRIAL_ENGINE_AUDIT.ru.md)

[Documentation](README.en.md) · [User guide](../README.en.md)

Historical evidence for empty LWG entries, data length and EXE encodings, dated 2026-10-04.
Target: `0_Package/ちんまい吸血姫体験版.exe`, SHA-256
`21db7a449d329fb6bb030b1c1cf4050b0f519a52bd19167ba0d27dcc4cd6740e`.
It is byte-identical to EXEs in `res_ft/CNMI/` and `CNMI_trial/ちんまい吸血姫_体験版/`.
No game was started or original EXE, resource or save modified; outputs used temporary directories.

## Relationship to Evermaiden

Against `startup_CHS.exe`, file size, PE headers, sections, compile time and PDB identity match.
Only 10 bytes differ in .text, all font charset `0x86/0x80`. Strings, initial data and PE resources differ elsewhere;
these are not identical whole files. Line breaking, WCG/LIM, XFL, GSC and audio add no code branches.
Optimized line-break-table and font-parameter recognition therefore also apply.
CP932 → GBK → CP1251 → CP932 restores the original EXE byte-for-byte. Independent inspection found only
two line-break tables and 13 confirmed font operands changed; GBK/CP1251 copies differ by 211/213 bytes.

The engine still uses `IsDBCSLeadByteEx(CP_ACP, ...)`, e.g. `0x44dc33–0x44dc35`.
EXE conversion neither sets the system code page nor translates built-in UI strings. Use a matching environment.
See [Evermaiden](EVERMAIDEN_ENGINE_AUDIT.en.md).

## 1. Lost zero-data LWG entries

`grps.xfl/compane.lwg` has 32 entries. Empty `slide_lev_f` and `slide` both have coordinates `(0,0)`, Flag 8.
Another, nonempty `slide` is at `(690,4)`, size `12×18`. Do not fill an empty duplicate from the same-named image.
The old extractor omitted empty entries from .meta.xml and repacked only 30. The shared extractor now writes all
metadata with `empty="1"`, preserving names, coordinates, flags and order without inventing images.
The packer honours that marker instead of looking for a file. Archives containing only empty entries still fail:
no empty package is generated.

`0x475722–0x47578c` reads all declared entry records. `0x427af2` looks up `slide_lev`;
`0x427b7c` associates `slide` with a control. This establishes their menu-data role, not all Flag=8 semantics
or the visual role of those empty entries.

The shared rscript2renpy `port_template/grps_layout.py` also honours `empty="1"`:
an empty duplicate no longer replaces the real slide position with `(0,0)`; the sample retains `(690,4)`.
No per-game duplicate fixes. Previously lost metadata requires re-extraction from the original LWG.
Commits: LiarsoftTool `3d8d4e8`; rscript2renpy `633eb4c`.

## 2. Data length incorrectly written as zero

The four bytes after the table give total data length, not padding. For compane.lwg the field is at `0x2fd`,
30231 (`0x7617`); file size is 31000, data begins at `0x301` and spans exactly 30231 bytes.
The old packer always wrote zero; it now writes the concatenated payload length, with empty entries contributing zero.
Local GARbro `ArcFormats/Liar/ArcLWG.cs` also uses the field; the EXE reads it at `0x475979–0x475981`.
This alone does not prove that zero causes a crash. The decoder still accepts older zero-field outputs.
All 12 trial LWG archives then round-tripped byte-for-byte with original payloads, including compane's two empty entries.
Commit: `81f48f2`.
