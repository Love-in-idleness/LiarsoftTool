# Developer reference: Jeanne / Kagerou resources

[中文](JEANNE_KAGEROU_ENGINE_AUDIT.zh.md) | [English](JEANNE_KAGEROU_ENGINE_AUDIT.en.md) | [Русский](JEANNE_KAGEROU_ENGINE_AUDIT.ru.md) | [日本語](JEANNE_KAGEROU_ENGINE_AUDIT.ja.md)

[Documentation](README.en.md) · [User guide](../README.en.md)

2026-10-04 static evidence for fonts, LWG names and media. Originals were neither run nor rewritten.
Only confirmed font machine-code patterns and LWG-name handling were added to the shared converter.

## Files

| File | SHA-256 | PE FileVersion |
| --- | --- | --- |
| `res_ft/exe/j.exe` | `7cb856bdbc5c61d9606734b30fd83bac880a3eff9829112f0ddf203e3026c4a6` | 1, 11, 0, 3 |
| `res_ft/exe/kagerou.exe` | `f02998fae350945f4de4dc7eab47997e1e2d9b8aa43d49740a80aad893dcde9b` | 1, 9, 0, 0 |

Both are apparently unpacked PE32/i386, ImageBase `0x400000`. Addresses are VAs, not file offsets.
Resource versions are not release dates.

## Font charset

Each EXE has 12 direct IAT CreateFontA paths; old detection converted six, leaving six `0x80` after GBK conversion.
These are compiler argument-push variations, not new encodings: weight/height calculations are interleaved,
or zeroed EBP/EBX supplies zero parameters.

| Shape | j.exe charset operand | kagerou.exe charset operand |
| --- | --- | --- |
| EBP zeros, fixed weight | `0x45c173` | `0x457e83` |
| Interleaved EDI weight, EBP height | `0x45ec5e` | `0x45c61e` |
| Interleaved EDI weight, EBX height | `0x45efeb, 0x45f33e` | `0x45c9ab, 0x45ccfe` |
| EBX zeros, EAX weight | `0x45fa6f` | `0x45d42f` |
| Immediate zeros, EAX weight | `0x45fcd6` | `0x45d696` |

Match full observed shapes with only IAT addresses variable; change one charset byte, not arbitrary PUSH 0x80,
branches, function addresses, weights or sizes. Shapes serve both detection and conversion, including fixtures containing only new shapes.
CP932 → GBK yields all twelve `0x86`; CP932 → GBK → CP1251 → CP932 restores both EXEs byte-for-byte.
Existing line-break detection finds one 50- and one 53-comparison table each:
j.exe `0x44d104 / 0x45aca6`, kagerou.exe `0x4499d4 / 0x455126`.
The three ASCII extensions are unchanged; no new restriction semantics. Runtime system code page must still match the script:
a font patch does not change all ANSI APIs.

## LWG names and XML

Kagerou's grps.xfl has 46 empty entries; 23 names are Photoshop-style `</Layer set>`.
Empty entries were retained with `empty="1"`, but unescaped names broke .meta.xml in
cgview, excompane, extrmenu, fontcompane and scview, including ElementTree consumers.
The common extractor escapes `& < > " '`; the packer decodes the five XML entities once, not recursively
(e.g. literal &lt; must not be decoded twice). File lookup uses the extractor's filesystem-character replacement;
nonempty reserved-character layers rebind their image while LWG stores the original name.
All 32 LWG archives across both games extract/repack identically, preserving empty names, coordinates, flags and order.
All 32 XML files parse with ElementTree. Re-extract old malformed XML from original archives;
production Ren'Py projects were not overwritten in this analysis.

## Image and audio paths

| Behaviour | j.exe | kagerou.exe |
| --- | --- | --- |
| LIM low-nibble version 2/3 | `0x43b770–0x43b79b` | `0x4375e0–0x43760b` |
| Version 3 A first, RGB with 0x10 | `0x43b7f0–0x43b844` | `0x437660–0x4376b4` |
| Paired/separate WCG channels | `0x43be00–0x43be82` | `0x437c70–0x437cf2` |
| BGR565 green key 0x07e0 | `0x43bab7` | `0x437927` |
| RIFF/WAVE + fmt | `0x46f100` onward | `0x46c790` onward |
| Compressed audio ACM open | `0x46f253` | `0x46c8e3` |

Checked dispatch agrees with implementation. Real WCGs were all `0x0271`; no new LIM sample was found.
Static dispatch is not all-variant resource testing, and output PNG does not prove native engine PNG support.
Scanning included every top-level XFL and nested XFL/LWG, including seven Jeanne voice subarchives,
five Kagerou voice subarchives and thumbnail subarchives, not just the first files.

| Check | Jeanne, 16 top-level XFL | Kagerou, 9 top-level XFL |
| --- | ---: | ---: |
| WCG decoded | 2431 | 1462 |
| LWG decoded and round-tripped | 10 | 22 |
| Ogg-in-WAV extract/wrap/extract, identical Ogg | 4551 | 3698 |
| PCM correctly retained | 115 | 0 |
| Failures | 0 | 0 |

Jeanne voice.xfl/0001.wav extracted Ogg fully decoded with FFmpeg. Container checks and byte round-trips do not validate
in-game duration, sound or ACM-driver compatibility. No new audio conversion defect was established.
