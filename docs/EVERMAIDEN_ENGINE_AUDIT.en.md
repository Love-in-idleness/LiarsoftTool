# Developer reference: Evermaiden resources and engine

[中文](EVERMAIDEN_ENGINE_AUDIT.zh.md) | [English](EVERMAIDEN_ENGINE_AUDIT.en.md) | [Русский](EVERMAIDEN_ENGINE_AUDIT.ru.md) | [日本語](EVERMAIDEN_ENGINE_AUDIT.ja.md)

This developer reference describes file formats and the identified native samples. EXE editing is an explicitly enabled [experimental feature](EXE_EDITOR.en.md); the editor reports the supported operations.

[Documentation](README.en.md) · [User guide](../README.en.md)

Historical static records for audio, fonts, punctuation and GSC loading. Conclusions are build-specific.
The sample is the Chinese patch `Evermaiden/startup_CHS.exe`, not the official Japanese EXE.
Do not generalize patch behaviour to all official programs.

- PE32/x86, 1,016,832 bytes, ImageBase `0x400000`.
- SHA-256: `2dd8004933ae3943485f825c4b88ccea53301e0f68f331a1440a17aeb6106ee5`.
- Addresses are PE VAs. No original game launch or game-directory writes; conversion copies were temporary.

## 1. Missing Vorbis mode 3 WAV detection

Scanning all 13 XFL archives found `wav.xfl/2559.wav` unsupported. Like supported compressed WAV, it stores
OGG after a 66-byte wrapper, but format tag is `0x6751`, not `0x6771`: Vorbis mode 3 / mode 3 plus,
per [VLC definitions](https://github.com/videolan/vlc/blob/master/include/vlc_codecs.h).
The sample has fmt length 26, extension length 8, stereo 44100 Hz, fact count 843757, data length 250668;
OggS begins at offset 66.

The fix retains header/page/parameter checks and adds only the verified tag. Writing keeps the template tag
and codec extension, rebuilding lengths, sample count and padding from OGG. No unverified modes were enabled.
CLI extraction → write-back → extraction yields identical OGG. FFmpeg fully decodes it to PCM with SHA-256
`44f63c38a8646a87b4dbc65148c0f3db2bdcc6822d905d7e191a013fcd0ff1ce`.
Tests cover tag preservation and rejection of unsupported `0x6750`.

## 2. Optimized line-break tables not recognized

Old detection only matched CMP SI, imm16. Both tables instead use MOV EAX, imm32 / CMP SI, AX / JE:
38 line-start restrictions and 12 line-end restrictions, still CP932 Japanese data.

- Table 1 starts at `0x457961`, followed by `0x213f`, `0x3f21`, CMP SI, BX extensions;
  line-end table starts at `0x457b9a`. `0x45786b–0x457875` sets BX=`0x2121`.
- Table 2 starts at `0x45d248`, without those three extensions; line-end starts at `0x45d45c`.
- Table 1's first two line-end JEs are near jumps; table 2 only its first. Others are short.
- Table 1 targets `0x457c54 / 0x457c37`, table 2 `0x45d508 / 0x45d4f0`, assigning the restriction class.

Recognition matches the complete known character sequence, zero-extended constants, comparison and JE structures.
Only 50 character operands change; ASCII prefix, extensions, instructions and jumps remain untouched.
CP932 tables are checked even when font charset is already GBK, so a partial patch is not mistaken for complete conversion.

## 3. Interleaved font parameters not recognized

At `0x46d738`, `0x46da28`, `0x46e4eb`, CreateFontA charset was still `0x80` alongside ten `0x86` calls.
Pointer loads and weight calculations interrupt the argument pushes, defeating contiguous-byte matching.
For example `0x46e4df` loads CreateFontA from `0x4c8024`; `0x46e507` calls ESI.
Match complete observed call shapes, not arbitrary PUSH 0x80, and change only charset operands.

Both EXE fixes: `1d6f88e`. GBK → CP1251 → CP932 on a real copy independently confirms both complete tables
and 13 font operands match targets, with all other bytes unchanged. Differences from the original are 201 / 213 / 10 bytes.
The final ten were originally GBK font parameters: uniform CP932 is not identical to the original mixed-charset file.

## Other paths

- WCG: header `0x478150`, version dispatch `0x4781ff`, paired channels `0x478222 / 0x478233`,
  alpha first `0x478244`, optional RGB `0x478250`; matches implementation.
- Palette: the 4096-entry boundary at `0x47b84d–0x47b85b` and indices from `0x47a700` agree with earlier fixes.
  Actual WCG samples were all `0x0271`; this does not test every layout.
- LIM: version dispatch `0x4786df` matches CannonBall; no actual LIM sample in this analysis.
- XFL: `0x47629b–0x4762f8` uses lstrcmpiA binary search. ASCII case-folded lexical order agrees,
  but arbitrary non-ASCII names are not guaranteed equivalent to Windows NLS sorting.
- GSC loads near `0x404faf`; serialization at `0x405458` includes the 36-byte header.

## Limits

IsDBCSLeadByteEx still uses CP_ACP, e.g. `0x44dc33–0x44dc35`. Font/table patches do not set the system code page;
use matching Windows locale or Locale Emulator. Cross-code-page execution needs further character/API analysis,
not just font replacement or blindly changing all PUSH 0. Original visuals, line breaking, playback and native Windows remain user checks.

## official Japanese GSC loading and execution

Separate target: `Evermaiden/startup_jp.exe`, SHA-256
`88820f28b8f770b486407e55dda28266c6fec42145192d5bc8ad5be21ab895af`.
PE32, ImageBase `0x400000`; VAs below. Not run or modified.

| Path | Binary evidence | Fix |
| --- | --- | --- |
| GSC loader | `0x404ee0–0x40526a` reads nine header words; after code, string index/text and data index/16-bit words, `0x4051ab–0x4051dd` reads two tables of header[7] bytes and names of header[8] bytes | Runtime named entries, not just debug data |
| Named entries | `0x404e00–0x404e51` skips slot 0, matches first-table name offsets, returns second-table code offsets; jump uses `0x43df90` | Export entry labels, relocate trailer after edits, reject missing labels |
| Local calls | opcode 200 at `0x43613a–0x4361ab` reads D target and ten expressions, saves return position; `0x43dea0` calls `0x405760`, rejecting target ≥ code size and setting code base + target | insub first operand becomes a checked, relocated label |
| Data blocks | `0x4361f3 → 0x43d4e0 → 0x404d50`: zero index returns; others use index table; MOVSX at `0x404d6e` sign-extends count as int16 before copying | Reject oversized counts/out-of-table references; allow zero no-copy |

Japanese 0001.gsc entries TOP1/click/TOP2 are at `0xa6 / 0x25c / 0x7da`; 2190.gsc REP002 is at `0xd9c`.
Unedited round-trips hide stale offsets: edit instruction length before an entry to expose the bug.
Names, slot 0 and extra padding remain; no full code/string backup is added. Re-export old TSC with nonempty entries or numeric insub operands.
