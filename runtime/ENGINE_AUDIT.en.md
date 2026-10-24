# RScript Runtime engine evidence

[中文](ENGINE_AUDIT.zh.md) | [English](ENGINE_AUDIT.en.md) | [Русский](ENGINE_AUDIT.ru.md)

[User guide](README.en.md) · [DLL interface](SDK.en.md)

## Scope

The launcher and host are not Forest-specific; file and text adapters target a verified EXE.
The first adapter supports only the Forest build below. Addresses are VAs at its preferred
image base unless marked as import-slot RVAs. No proprietary DLL code or game assets are included.

## Inspected files

| File | SHA-256 |
| --- | --- |
| Forest `Forest.exe` | `8eb71d07304d362ec26cc47d93c6ef5854b5acadd7c9fe941755ad50820a3e56` |
| CodeXRScript / OC patch `RsComp.dll` | `6c65c3545a559b6e4a01d54fdb2f57ad4e605b27dc73959cde18bd3468f3cb30` |
| UBAI `RsComp.dll` | `0b1c3cdfc1587f576c30c09b3faa329e88e18fdaddfc0655d1ad398c1654a6a9` |
| Albatross `rsinit.dll` | `43499ada89a18735707c0e33d9a9bb585022e8e1b9b93e562a5e41bbb8340eb6` |

Both RsComp files are 32-bit PEs exporting `Compile`, at RVAs `0x124e` and `0x7090` respectively.
Forest loads its compiler at `0x412eb0`; `0x4135f0` wraps a four-argument call.
The result is delivered to an engine object, not an independent input/output filename API.
Neither compiler alone controls images, audio, or Unicode drawing.
The new implementation therefore uses a separate host and optional DLLs, rather than claiming
that a replacement `RsComp.dll` works with every game.

## Forest file paths

| Location | Observation and constraint |
| --- | --- |
| `0x4247c0` | Tries a loose file through CFile before the archive path |
| `0x42a970` | CFile calls read-only `CreateFileA`, `OPEN_EXISTING` |
| `0x42ab20` / `0x42ab80` / `0x42abe0` | CFile read, seek, and size methods |
| `0x413200` | Opens `%s\\%04d.gsc`, reads a 28-byte header and subsequent sections |
| `0x428260` / `0x4288d0` | Forms LIM / WCG paths; the request determines the decoder |
| `0x424b50` | Opens an LWG component |

The adapter replaces only five import slots in the game EXE:

| Windows API | RVA |
| --- | --- |
| `CreateFileA` | `0x59098` |
| `ReadFile` | `0x5907c` |
| `SetFilePointer` | `0x590c8` |
| `GetFileSize` | `0x590cc` |
| `CloseHandle` | `0x5909c` |

A matching override becomes an in-memory file. Otherwise the original API handles loose files
and XFL through the game. There are no process-wide NT file hooks.
A LIM request must receive LIM bytes, not a renamed WCG. Script overrides must retain Forest's
legacy-28 format; another game's modern-36 layout is rejected.
Image, audio, LWG, and TSC conversion reuse the existing core, including the shared `.meta.xml` reader.

## Unicode integration

The parser at `0x438790` walks single- and double-byte characters; `0x438836–0x438842` builds
a 16-bit character value, and `0x438866` advances two bytes. Glyph paths near `0x4453e0`,
`0x445760`, `0x445ab0`, and `0x445e70` call ANSI GDI.
Passing raw UTF-8 into these paths splits characters incorrectly; changing file encoding alone is insufficient.

`0x44880c` reads the static CRT classification table at `0x48fa00`; `0x44bfb8` initializes it,
using `0x44c165` to query the system `GetACP`. The Unicode module therefore also makes the game's
own `GetACP` import slot (RVA `0x59184`) return 932, so internal double-byte IDs work on non-Japanese systems.
This changes neither Windows regional settings nor other modules' `GetACP` calls.
REA evidence: `ev_fec16cd0f7502d87c643ce4be10a0f513fed452271d3d9e2470c33a9fc4ff9a2`
and `ev_17e895a8bdb39082341c3f9bdb782e5e4ef09b18431096f0819b17088726f365`.

The first version preserves instructions and text controls. Characters that round-trip losslessly
through CP932 retain their original bytes; other BMP characters receive stable IDs in CP932's
`F0–F9` user-defined range. Drawing resolves each ID back to its actual Unicode character:

| Original import | RVA | API for mapped characters |
| --- | --- | --- |
| `GetGlyphOutlineA` | `0x59038` | `GetGlyphOutlineW` |
| `TextOutA` | `0x59024` | `TextOutW` |
| `GetTextExtentPoint32A` | `0x5903c` | `GetTextExtentPoint32W` |

Unmapped text retains the original ANSI path. IDs already used by native scripts or the EXE are reserved.
The dictionary is safely written to user data before a script is delivered, keeping IDs stable across
restarts for saves and history. This is not replacement of unencodable characters with `?`.
It is also not a full Unicode layout engine: at most 1880 extra BMP characters, minus native usage,
with no supplementary-plane support or general complex shaping.

## What Albatross demonstrates

The inspected `rsinit.dll` has 361 OpenGL exports, embedded resources, and runtime hooks.
Initialization at `0x623af440` reaches NT file APIs; `0x623b15e0` also handles windows, fonts,
and module lifetime. Font replacements at `0x623af370` / `0x623af750` force charset `0x86`:
a GB2312 path, not existing UTF-8 support. `0x623b45f0` changes memory protection and builds trampolines.

This demonstrates that DLLs can alter resource, window, and text paths without rewriting the EXE
on disk. It does not establish cross-version compatibility. Each new engine still requires verified
calling conventions, data structures, and drawing paths. These observations informed the integration
choice; its proprietary implementation was neither copied nor made a dependency.

## Verification boundaries

Portable tests cover override priorities, invalid resources without fallback, path boundaries,
component layout, unchanged inputs, and glyph dictionaries. Windows / Wine tests additionally
cover pre-entry loading, ABI and failure rollback, file APIs, and Unicode GDI drawing.
Synthetic import-table tests and actual game startup checks are separate; startup does not verify
full gameplay, old saves, or every font. Native Windows and different EXE versions need separate acceptance.

On an independent game copy, Wine checks reached the opening and title and loaded actual TSC / PNG overrides.
Japanese, simplified Chinese, Latin characters outside CP932, and text color controls in the test script
were confirmed in the original engine's rendered frame.
The manual helper [game_smoke.cpp](tests/game_smoke.cpp) terminates only its own verified child game process.
It is not a default CTest test, and game fixtures are not included.
