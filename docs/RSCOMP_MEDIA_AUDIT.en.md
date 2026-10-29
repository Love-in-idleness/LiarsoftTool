# Developer reference: RsComp.dll and media commands

[中文](RSCOMP_MEDIA_AUDIT.zh.md) | [English](RSCOMP_MEDIA_AUDIT.en.md) | [Русский](RSCOMP_MEDIA_AUDIT.ru.md) | [日本語](RSCOMP_MEDIA_AUDIT.ja.md)

This developer reference describes file formats and the identified native samples. EXE editing is an explicitly enabled [experimental feature](EXE_EDITOR.en.md); the editor reports the supported operations.

[Documentation](README.en.md) · [User guide](../README.en.md)

Two compiler versions' command layouts, distinct from media decoding. Static checks of these builds:
hashes, PE imports/exports, strings and targeted disassembly; DLLs were not loaded or executed.

## Samples

| Path | Bytes | PE timestamp | Compile RVA | SHA-256 |
| --- | ---: | --- | --- | --- |
| `CodeXRScript/OC用パッチ/data/RsComp.dll` | 364611 | 2008-07-22 | `0x124e` | `6c65c3545a559b6e4a01d54fdb2f57ad4e605b27dc73959cde18bd3468f3cb30` |
| `RsComp.dll (2012)` | 90112 | 2012-10-14 | `0x7090` | `0b1c3cdfc1587f576c30c09b3faa329e88e18fdaddfc0655d1ad398c1654a6a9` |

PE timestamps alone do not prove release dates.
Both x86 PE32, export only Compile, statically import only KERNEL32.dll and USER32.dll.

## Positive evidence: command compilation, not codecs

All addresses are VAs at ImageBase `0x10000000`.

- Old entry `0x1000124e` jumps to `0x10012220` then compilation. New `0x10007090` calls
  `0x10005270`, allocates buffers, calls `0x10001b60 / 0x10002510`, frees them and returns.
- Old `0x10008bc3` matches bgm_on and emits `0x3c`; `0x10008d85` matches voice,
  emits `0x42` and compiles four expressions.
- New `0x100039b7` matches bgm_on and emits `0x3c`; `0x10003ab7` matches voice,
  emits `0x42`, processing the first operand as a string and the rest as expressions.
  Thus identical names across generations do not establish identical layouts.
- New load string reference: `0x10003bc7`, draw: `0x10003e06`, both in parser `0x10002850`.
  Shared `0x10001000` writes a 16-bit value and adds 2 to code length; it does not decode images.

## Avoiding overclaims

Absence of static audio-DLL imports does not rule out dynamic calls. LoadLibraryA / GetProcAddress were also checked:

- New `0x1000c650` loads user32.dll and window helpers GetActiveWindow/GetLastActivePopup.
- Old `0x10022040` is the same window helper path; `0x100019b4` loads RsComp.dll and
  `0x100036da` gets Compile, a compiler wrapper. Other name-driven module loading does not identify external modules' capabilities.
- Neither contains OggS, RIFF, vorbis, .acm, .wcg, .lim, .png codec signatures/strings.
  No ACM, WinMM, GDI-image or codec imports were found. Combined with located compiler paths,
  no new WAV/WCG/LIM codec implementation was established.

This is a failure to find a new implementation, not proof that every unidentified DLL function is unrelated to media.
Use the engine EXE ACM path / vorbis.acm for audio and its WCG/LIM functions for images.
These DLLs help validate GSC/TSC command layouts, not byte-exact WAV container restoration.
See [audio boundaries](AUDIO_PROCESSING_AUDIT.en.md).

## Is oc_reference/bgmsv MP3 a game format?

Files are ID3-tagged MPEG Layer III, but oc_reference/bgmlist.html places bgmsv/TrackNN.mp3 links in the audition column.
CodeXRScript/readme.txt (CP932) describes browser reference material; the installation list contains images, scripts,
RsComp.dll and version.dat, not bgmsv. This does not establish native MP3 playback.
All three {Cannonball,Forest,kagerou}.exe contain `%s\\\\Track%02d.wav` and `%s\\\\%04d.wav`,
not .mp3 or bgmsv signatures. Cannonball references the former at `0x0042c71c`, the latter including `0x004136ae`;
mpegvideo at `0x00431f8f` belongs to a different media path, not evidence for MP3 in BGM/WAV loading.
For these samples MP3 is documentation audition material, not a supported same-name WAV replacement.
Do not expand archive whitelists or merely rename extensions. Support other engines' MP3/raw Ogg paths only with separate call evidence;
the goal is playback, not byte-exact WAV restoration.
