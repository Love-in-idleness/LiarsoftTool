# RScript Runtime

[中文](README.zh.md) | [English](README.en.md) | [Русский](README.ru.md) | [日本語](README.ja.md)

[Engine evidence](ENGINE_AUDIT.en.md) · [DLL development guide](SDK.en.md)

Experimental Windows DLL extensions for running loose resources and UTF-8 scripts in the original game.
They do not recreate the game, modify the EXE on disk, or require replacing the official `RsComp.dll`.
It targets CodeX RScript games sharing the supported formats and Windows APIs, without game-name checks,
EXE hash allowlists, or fixed addresses. Current builds and game checks use 32-bit Windows;
Forest and SevenBridge are regression samples, not an admission list.

## Getting started

Keep the built `RScriptLauncher.exe`, `RScriptHost.dll`, `kinsoku.json`, and feature DLLs in the same directory.
They do not need to be placed in the game directory. Open the launcher:

1. Select the game EXE in **Game executable**.
2. Select an **Override resources** directory, or leave it empty to use the game directory.
3. Select the DLLs, run **Check compatibility**, then click **Start game**.

Test with a game copy first: even though extensions do not rewrite resources, the game itself may write saves.

| DLL | Purpose |
| --- | --- |
| `RScriptFiles.dll` | Hooks synchronous file imports by API name |
| `RScriptResources.dll` | Gives loose resources and current-format TSC priority |
| `RScriptUnicode.dll` | Enables characters outside CP932 in UTF-8 override scripts |

The launcher loads `RScriptHost.dll` when needed; it is not a separate checkbox.
Checking Resources or Unicode also checks Files. Unchecking Files unchecks both.
With everything unchecked, the original game runs without DLL injection. Changes take effect on the next launch.

**Add DLL...** adds another DLL, initially unchecked. Load trusted files only:
native DLLs can access or modify the entire game process; there is no sandbox.
See the [DLL development guide](SDK.en.md). No Wiki DLL is included, and DLLs cannot be unloaded while playing.

## Preflight checks

**Check compatibility** checks the selected game, DLLs and override scripts in the background,
with progress and a **Cancel check** button. Changing the game, override directory or DLL selection
invalidates the previous result. The check only reads files: it does not run the game, load extension
DLLs, or create or modify a glyph dictionary.

**ERROR** identifies a definite fault that blocks startup. **PENDING** means static analysis cannot
confirm a capability; DLL initialization must still verify it. **INCOMPLETE** and **CANCELLED**
are not successful checks. Glyph statistics separate native reserved slots, historical mappings,
new mappings required by the patch, and remaining capacity. Lossless CP932 characters need no new
mapping. Problems are reported per script, continuing after damaged files.

`--check` performs the full check and exits; it cannot be combined with `--wait`.
See `RScriptLauncher --help` for arguments. Definite errors or incomplete checks return nonzero;
information and pending verification alone return zero. Normal startup repeats only lightweight
checks, without compiling the whole patch.

A standalone build without the built-in game modules provides basic checks only and explicitly
reports that full script checking is unavailable. Static checks cannot verify all font glyphs,
actual import addresses or other DLLs' hook conflicts; native testing remains necessary.

## Resource overrides

Keep the original directory layout, for example `scr/2100.tsc`, `grpe/9001.webp`,
`voice/0001.ogg`, and `grps/tbox01/.meta.xml`. The override directory does not need a complete copy of the game.

Resources with the same logical name use this order:

| Type | Priority |
| --- | --- |
| Audio | `ogg > wav` |
| Scripts | `tsc > gsc` |
| Images | `webp > png > lim > wcg` |
| Image components | Component directory with `.meta.xml` `> lwg` |

Keep `.meta.xml` together with the component images it references: it supplies positions, dimensions, and entry names.
Missing overrides use the original game loading path. A selected override that is damaged or incomplete
reports an error instead of silently using a lower-priority file.

Resources are converted in memory on demand; original resources are not rewritten.
Export TSC using the current LiarsoftTool and save it as UTF-8, keeping generated labels and metadata.
Official compiler source features such as includes and macros are not supported here.

TSC retains its own container format. If an original GSC is found loose or in an XFL entry,
its header layout must match. New scenes without a reference retain their supplied format rather than guessing one.

## UTF-8

Enable Resources, Unicode, and Files together for UTF-8 translations. Original GSC remains CP932.
With Resources alone, all TSC text must still be representable in CP932.

The Unicode adapter currently supports characters in the Basic Multilingual Plane, including common Chinese,
Japanese, and Russian text. Supplementary characters such as emoji and general complex-script shaping are not supported.
Distinct characters outside CP932 share at most 1880 glyph-mapping slots; slots already used by the original game
reduce the available capacity. Install fonts containing the required characters: successful resource loading does not ensure glyph coverage.

Reservation scanning follows override selection: a loose GSC shadowed by a same-name TSC in the override directory does not consume native reservation slots.
Original game scripts, EXE static data and historical dictionary reservations remain protected; existing glyph assignments are not reused to create space.

### Line-breaking rules

When Unicode is enabled, the launcher reads [kinsoku.json](kinsoku.json) beside its own EXE.
`no_line_start` lists characters forbidden at the start of a line, such as `。）」`;
`no_line_end` lists characters forbidden at the end, such as `（「`.
The supplied rules cover common Chinese/Japanese punctuation, small kana, ASCII punctuation and directional quotes;
edit them to suit the translation. The two sets must not overlap. Use UTF-8, at most 64 KiB, and BMP characters only.

Use `--kinsoku "custom rules.json"` on the command line to select another file; an explicit file is always checked.
Missing or invalid configuration prevents game startup. Without Unicode or this option, no rules file is required.
The rules feed the original engine's line-breaking decisions: they do not insert newlines into scripts
or implement the full Unicode UAX #14 algorithm.

The glyph dictionary is stored at:

```text
%LOCALAPPDATA%\LiarsoftTool\RScriptRuntime\<key>\glyphs.bin
```

Do not delete it while keeping old saves or history. Back it up with saves, and keep the game and override directory paths unchanged
when migrating; another path or changed EXE content uses another dictionary. Story edits can still make old saves incompatible.
Multiple game instances cannot use the same dictionary simultaneously.

Do not mix these generic DLLs with the early Forest-specific DLLs; use one complete build.
Glyph dictionaries from that early prototype are not migrated automatically.

Error details are in `%LOCALAPPDATA%\LiarsoftTool\RScriptRuntime\host-<pid>.log`.
Logs and dictionaries go to the user data directory, not the original resources.

## Compatibility

Files hooks `CreateFileA`, `ReadFile`, `SetFilePointer`, `GetFileSize`, and `CloseHandle`
in the EXE's ordinary import table. Unicode uses ANSI GDI glyph, text, and measurement APIs,
with CP932 character recognition local to the game module; system regional settings are unchanged.
Unicode line-break rules also require a recognized complete shared classifier; otherwise initialization fails.
The 32-bit bridge requires SSE support.

Missing required APIs, damaged imports, and existing hook conflicts prevent initialization with an error.
Delay imports, dynamically obtained functions, other DLLs' own reads, file enumeration, mapping,
and asynchronous reads are not intercepted. MCI video playback is outside the override path.
Engines using other file or text APIs may need additional shared support;
successful DLL initialization alone does not establish full compatibility.

## Building and validation

Build the runtime separately from the repository root; GTK and libcurl are not required.
Use CMake 3.16+, a 32-bit Windows MinGW toolchain, and matching static WebP, Vorbis/Ogg, and uchardet libraries:

```bash
cmake -S runtime -B build/runtime-win32 \
  -DCMAKE_SYSTEM_NAME=Windows \
  -DCMAKE_C_COMPILER=i686-w64-mingw32-gcc-posix \
  -DCMAKE_CXX_COMPILER=i686-w64-mingw32-g++-posix \
  -DCMAKE_FIND_ROOT_PATH=/path/to/windows-x86-dependencies \
  -DCMAKE_FIND_ROOT_PATH_MODE_INCLUDE=ONLY \
  -DCMAKE_FIND_ROOT_PATH_MODE_LIBRARY=ONLY \
  -DCMAKE_FIND_ROOT_PATH_MODE_PROGRAM=NEVER \
  -DBUILD_TESTING=OFF
cmake --build build/runtime-win32 -j2
```

Use the same target architecture and runtime for dependencies and compiler; do not mix Windows and Linux libraries.
Outputs are in `build/runtime-win32/`. `RScriptLogExample.dll` is an additional logging example.

Add `-DBUILD_RUNTIME=ON` to an existing main-project build to enable the portable resource and Unicode tests.
Windows tests also need Python 3 and the Vorbis encoder library; cross-builds can set `RSCRIPT_WINE`.
Only Windows builds produce the launcher and feature DLLs.

This is experimental. Automated tests, Wine checks, and a full playthrough on native Windows are different kinds of validation.
Compatibility with every scene, save, font, or plugin combination is not guaranteed. Supply legally obtained game files yourself.

See the [main guide](../README.en.md#license-and-credits) for licensing and third-party notices, including the statically linked [uchardet licenses](../docs/THIRD_PARTY_UCHARDET_LICENSES.md).
