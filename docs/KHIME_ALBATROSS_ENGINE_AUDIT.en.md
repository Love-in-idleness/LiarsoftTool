# Developer reference: Khime / Albatross resources

[中文](KHIME_ALBATROSS_ENGINE_AUDIT.zh.md) | [English](KHIME_ALBATROSS_ENGINE_AUDIT.en.md) | [Русский](KHIME_ALBATROSS_ENGINE_AUDIT.ru.md) | [日本語](KHIME_ALBATROSS_ENGINE_AUDIT.ja.md)

This developer reference describes file formats and the identified native samples. EXE editing is an explicitly enabled [experimental feature](EXE_EDITOR.en.md); the editor reports the supported operations.

[Documentation](README.en.md) · [User guide](../README.en.md)

Static evidence for fonts, archives, images and audio. Originals were read, not run or modified;
conversion results went to temporary directories. Conclusions apply only to the specified builds.

## File identity

| File | SHA-256 | PE FileVersion |
| --- | --- | --- |
| `KhimeKusaritop/KhimeDL.exe` | `2dfc7b78d32867716ab0406ccbf176b55a76faea1cb9c40feac25c90775222fe` | 1, 8, 5, 0 |
| `Albatross/信天翁航海録.exe` | `f04c9729c0f60269f6ffdc63ea71c587a2431c501ade698e374978050fd156c4` | 1, 9, 0, 0 |

Both PE32/i386, ImageBase `0x400000`; addresses are VAs. Resource versions are not release dates.
Chinese patch EXEs in the same directories were outside this comparison.
Albatross .text (`0x72000` bytes) and .rdata (`0xb000`) match kagerou.exe byte-for-byte at identical section addresses.
Font, line-break, channel and audio evidence in the [Kagerou audit](JEANNE_KAGEROU_ENGINE_AUDIT.en.md) therefore applies;
this does not make game resources or scripts identical.

## Khime font charset

KhimeDL.exe has eleven direct CreateFontA IAT calls (IAT `0x46302c`). Old conversion covered nine;
two retained Japanese charset `0x80` because argument-push forms were missed, not because of a new encoding.

| Charset operand | CreateFontA call | Difference |
| --- | --- | --- |
| `0x44f8de` | `0x44f8fb` | SBB EDI,EDI before charset PUSH; EBX height, EBP underline |
| `0x44fff0` | `0x450009` | EAX weight path pushes zeroed EBP, not EBX |

The second function clears EBP at `0x44ff4b`. Shared recognition matches full observed bytes, varying only the four-byte IAT address.
Do not patch arbitrary PUSH 0x80, weight, size, underline, branches or call targets. New shapes serve detection and conversion,
including new-shape-only fixtures.

- CP932 → GBK sets all eleven Khime / twelve Albatross charsets to `0x86`; CP1251 sets `0xcc`.
- CP932 → GBK → CP1251 → CP932 restores both real EXEs byte-for-byte.
- Khime's classic 50-entry table is at `0x441c54`; Albatross 50/53-entry tables at `0x4499d4 / 0x455126`.
  Existing mappings apply, no new restriction semantics.
- A matching Windows code page or Locale Emulator remains necessary; font charset is not all ANSI APIs.

## Images, archives and audio

Khime checks LIM at `0x42ee04`; 32-bit decoding at `0x42ef10–0x42ef64` reads A first and RGB with flag `0x10`.
WCG signature: `0x42f474`; channels: `0x42f520–0x42f5a2`.
`(flags & 0x1c0) == 0x40` selects paired channels, otherwise separate Alpha/RGB.
Checked paths match implementation; actual WCGs in both games are `0x0271`. No LIM samples were found,
so not every LIM variant is resource-tested.

Compressed audio from `0x45cf10` finds WAVE/fmt and checks PCM; non-PCM at `0x45d063` calls acmStreamOpen through a thunk.
No new audio-container defect was confirmed. Exported PNG/Ogg does not imply native engine support.
Every normally named top-level XFL was scanned recursively, including embedded XFL/LWG.
Khime's actual script archive is `scr.xfl.` (trailing dot); it was inspected as XFL by content,
without renaming the user's file or changing CLI extension rules.

Albatross includes `</Layer set>` names; prior XML escaping preserves names, empty entries, coordinates, flags, order and payload.
Extracted Ogg from KhimeKusaritop/voice.xfl/0001.wav and Albatross/voice.xfl/0015.wav fully decoded with FFmpeg.
Container checks do not validate in-game playback length, sound or ACM-driver compatibility.
