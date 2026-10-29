# RScript Runtime engine evidence

[中文](ENGINE_AUDIT.zh.md) | [English](ENGINE_AUDIT.en.md) | [Русский](ENGINE_AUDIT.ru.md) | [日本語](ENGINE_AUDIT.ja.md)

[User guide](README.en.md) · [DLL interface](SDK.en.md)

## Scope

File and GDI integration resolves PE import names; line-break integration recognizes a complete shared classifier.
Neither uses game names, EXE allowlists, or fixed addresses.
Forest and SevenBridge validate shared file and text paths. Addresses below are evidence, not runtime
selection conditions: VAs use the preferred image base unless marked as import-slot RVAs.
No proprietary DLL code or game assets are included.

## Inspected files

| File | SHA-256 |
| --- | --- |
| Forest `Forest.exe` | `8eb71d07304d362ec26cc47d93c6ef5854b5acadd7c9fe941755ad50820a3e56` |
| SevenBridge `SBridge_jp.exe` | `c7d5fc38cf0c354a61d056b607cf1367b2529557e9e0407d0b0fdf12771b35e3` |
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

The five file import slots in this Forest sample are:

| Windows API | RVA |
| --- | --- |
| `CreateFileA` | `0x59098` |
| `ReadFile` | `0x5907c` |
| `SetFilePointer` | `0x590c8` |
| `GetFileSize` | `0x590cc` |
| `CloseHandle` | `0x5909c` |

A matching override becomes an in-memory file. Otherwise the original API handles loose files
and XFL through the game. There are no process-wide NT file hooks.
A LIM request must receive LIM bytes, not a renamed WCG. Forest uses
legacy-28 scripts, but this does not justify rejecting modern-36 from another engine.
The runtime retains TSC metadata and checks the original loose or archived GSC header when available;
it does not choose the header size from a game name.
Image, audio, LWG, and TSC conversion reuse the existing core, including the shared `.meta.xml` reader.

## SevenBridge comparison

| Behavior | VA / import RVA |
| --- | --- |
| Loose-file first, then archive | `0x427c30 → 0x42e020 / 0x42e760` |
| GSC loading | `0x415a60`, nine DWORDs, modern-36 |
| LIM / WCG / LWG request | `0x42b910` / `0x42bf80` / `0x4281f0` |
| File imports | open `0x5f098`, read `0x5f080`, seek `0x5f0e0`, size `0x5f0e4`, close `0x5f09c` |
| DBCS parser | `0x43dad0`; `0x43db76` forms lead<<8\|trail |
| GDI glyph path | `0x44adf0`, calls `GetGlyphOutlineA` |
| CRT codepage initialization | `0x44d444 → 0x44d5f1 → GetACP` |

Addresses and GSC headers differ, but the synchronous file protocol is shared.
The same Files / Resources / Unicode DLLs handle both samples; no SevenBridge-specific DLL is added.
`imports.cpp` parses ordinary PE imports and rejects missing APIs or existing hook conflicts.
SevenBridge's `FindFirstFileA` concerns saves/directories, not resource preflight; MCI video bypasses this override path.

## Unicode integration

The parser at `0x438790` walks single- and double-byte characters; `0x438836–0x438842` builds
a 16-bit character value, and `0x438866` advances two bytes. Glyph paths near `0x4453e0`,
`0x445760`, `0x445ab0`, and `0x445e70` call ANSI GDI.
Passing raw UTF-8 into these paths splits characters incorrectly; changing file encoding alone is insufficient.

`0x44880c` reads the static CRT classification table at `0x48fa00`; `0x44bfb8` initializes it,
using `0x44c165` to query the system `GetACP`. The Unicode module therefore also makes the game's
own named `GetACP` import return 932, so internal double-byte IDs work on non-Japanese systems.
System-default requests to `IsDBCSLeadByteEx` also use 932; other explicit codepages retain their meaning.
This changes neither Windows regional settings nor other modules' `GetACP` calls.

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

## UTF-8 line-break rules

SevenBridge's `0x43e450` writes character classes to the layout array after drawing:
`0` ordinary, `1` no line start, `2` no line end. `0x43e540–0x43e798` compares `SI`;
branches converge at `0x43e7cc` and `0x43e7b3`, with the ordinary path at `0x43e79a`.

[kinsoku_match.cpp](../src/kinsoku_match.cpp) validates full ASCII/CP932 comparison groups,
conditional jumps, and common targets, for immediate and `MOV EAX / CMP SI, AX` forms.
The in-process bridge aligns the stack, saves registers, flags and FPU/SIMD state, restores the Unicode character from its glyph token,
and uses [kinsoku.json](kinsoku.json). Native packed values that do not decode as one character retain
their original classification. Unicode initialization fails if no classifier is recognized.
No EXE on disk or TSC line breaks are changed.

These are customizable character-based start/end restrictions, not full [Unicode UAX #14](https://www.unicode.org/reports/tr14/).
New instruction forms require extending the shared recognizer and tests, not game address tables.

## What Albatross demonstrates

The inspected `rsinit.dll` has 361 OpenGL exports, embedded resources, and runtime hooks.
Initialization at `0x623af440` reaches NT file APIs; `0x623b15e0` also handles windows, fonts,
and module lifetime. Font replacements at `0x623af370` / `0x623af750` force charset `0x86`:
a GB2312 path, not existing UTF-8 support. `0x623b45f0` changes memory protection and builds trampolines.

This demonstrates that DLLs can alter resource, window, and text paths without rewriting the EXE
on disk. It does not establish cross-version compatibility. Each new engine still requires verified
calling conventions, data structures, and drawing paths. These observations informed the integration
choice; its proprietary implementation was neither copied nor made a dependency.

## Checks and limits

Portable tests cover resource priority, invalid overrides without fallback, path boundaries, component layout,
unchanged inputs, and glyph dictionaries. Enable tests using the [build guide](README.en.md#building-and-validation),
then run CTest in your build directory.

Windows/Wine checks also need pre-entry loading, ABI, initialization rollback, file APIs, and Unicode GDI drawing.
Synthetic import tables do not replace real engine checks. For a new game, use an independent copy to check
startup, title, overrides, and text, then full routes, old saves, fonts, and DLL combinations.

The manual entry point is [game_smoke.cpp](tests/game_smoke.cpp). It terminates only its own verified child process,
is not a default CTest test, and includes no game resources.
Static analysis, automated checks, and Wine do not replace native Windows acceptance.
