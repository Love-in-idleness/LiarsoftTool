# Developer reference: CannonBall resources and engine

[中文](CANNONBALL_ENGINE_AUDIT.zh.md) | [English](CANNONBALL_ENGINE_AUDIT.en.md) | [Русский](CANNONBALL_ENGINE_AUDIT.ru.md) | [日本語](CANNONBALL_ENGINE_AUDIT.ja.md)

This developer reference describes file formats and the identified native samples. Font and line-break analysis of EXEs does not imply EXE conversion is available; that production entry point is disabled.

[Documentation](README.en.md) · [User guide](../README.en.md)

Static evidence for LIM, archives, and encodings, not a game-engine implementation.
Target: `Cannonball.exe`, SHA-256
`df4b5b6dffc7002370d0772869baaba551b08fca970e92609ca64b08174f89ba`.
All addresses are PE VAs. The original program was not started and its resources were not rewritten.
Conclusions apply to this build only.

## Format rules and fixes

| Item | Evidence or reproduction | Fix |
| --- | --- | --- |
| Alpha-only LIM | `0x43bab6–0x43bac9`: decode A first, RGB only with flag `0x10` | Do not read three absent colour blocks; retain alpha with zero RGB |
| LIM RGB565 | `0x43bd56–0x43bd89`: shift 5/6-bit components, not proportional expansion to 255 | Match engine output, e.g. white `(248,252,248)` |
| RGB565 colour key | Same path recognizes `0x07e0`; separate alpha overrides after `0x43be10` | Transparent green key; independent alpha remains authoritative |
| XFL ordering | `0x41ab05–0x41ab43`: binary search with `lstrcmpiA` | ASCII case-insensitive lexical order, not natural numeric order, including serialization of existing entries |
| Long XFL names | 32-byte name field; old writer silently truncated | Reject encoded names over 31 bytes and case-folded duplicates; never split CP932 characters |
| XFL/LWG bounds | Truncated fixtures were accepted; short LWG headers could read out of bounds | Validate headers, tables, entries, names, offsets and lengths; reject negative XFL offsets, unsafe output paths and LWG names beyond one-byte length |
| Windows encoding API | UTF-8 output used forbidden flags and default-character pointers | Use UTF-8-specific parameters, reject invalid strict input and check actual conversion calls |

Required ASCII order is `1.gsc, 10.gsc, 2.gsc`, not `1.gsc, 2.gsc, 10.gsc`.
Fixed four-digit filenames usually hide the difference. API requirements:
[WideCharToMultiByte](https://learn.microsoft.com/en-us/windows/win32/api/stringapiset/nf-stringapiset-widechartomultibyte),
[MultiByteToWideChar](https://learn.microsoft.com/en-us/windows/win32/api/stringapiset/nf-stringapiset-multibytetowidechar).
Finding an API-contract bug does not establish it as the sole cause of an existing user's Windows failure.
Earlier audio checks: [audio audit](AUDIO_PROCESSING_AUDIT.en.md).

## Compatibility limits

| Feature | Boundary in this record | Evidence |
| --- | --- | --- |
| Original LIM layout | LIM ↔ lossless WebP supported; writing uses version 3 separate A/R/G/B, not original depth/compression | `0x43ba30` dispatches versions; `0x43ba70 / 0x43bab0` handle 16-bit/separate channels. Preserve decoded RGBA, verify other engines in-game |
| Editable MSK round-trip | Preserve/unpack/pack .msk, no dedicated MSK → PNG → MSK conversion | `0x43ac25` appends .msk, `0x43aca9` checks BM, `0x43ace7–0x43acf7` accepts 1/8/24-bit BMP. Renaming does not reproduce mask semantics |
| Official high-level TSC | Structured bytecode compiler, not a full RsComp source replacement | `0x40e38f–0x40e39d` resolves Compile; `0x44f274/0x44f280` are TSC filename formats. Official sources use #include and %title; this parser expects commands or labels |
| Project-wide dependencies | Per-file conversion does not check all referenced images/audio/scripts/video | No project reference graph, missing-resource list or dynamic-reference uncertainty markers |
| Native non-ASCII sorting | ASCII fixed; full Windows locale NLS comparison not reproduced | Engine uses lstrcmpiA; UTF-8 names use ASCII case folding, not guaranteed equivalent for Japanese/Chinese/Russian |
| PCM/Ogg transcoding and resampling | PCM retained; Ogg decoded to PCM or wrapped in WAV; no Vorbis encoding or resampling | PCM and ACM use distinct engine paths. See the updated audio audit; wrapping is not transcoding and incompatible template parameters must fail |
| Video conversion | No MPG → WebM entry point; video not in the established archive resource whitelist | `0x431f67` gets mciSendCommandA; `0x431f8f–0x431f9b` opens mpegvideo. Ren'Py playback belongs to the port |

The first four are resource-tool development opportunities. Playback, effects, menu and save execution belong to rscript2renpy or the engine,
not converter bugs. A 2003 EXE does not establish every later opcode's semantics.
