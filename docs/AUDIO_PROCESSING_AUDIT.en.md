# Developer reference: audio containers

[中文](AUDIO_PROCESSING_AUDIT.zh.md) | [English](AUDIO_PROCESSING_AUDIT.en.md) | [Русский](AUDIO_PROCESSING_AUDIT.ru.md) | [日本語](AUDIO_PROCESSING_AUDIT.ja.md)

[Documentation](README.en.md)

WAV/OGG handling rules and evidence from CannonBall. For everyday conversion, use `liarsofttool -h`.

## Conversion rules

| Input | Output |
| --- | --- |
| WAV containing a complete Vorbis stream | Extract OGG |
| Standard PCM WAV | Retain unchanged |
| OGG | Decode to 16-bit PCM WAV by default |
| OGG with `--vorbis-in-wav` | Keep compression in a WAV container |

PCM keeps sample rate and mono/stereo channels; no resampling. Vorbis-in-WAV builds its container from source parameters without a reference WAV.
PCM cannot restore the original compressed bytes; a rebuilt WAV need not match the original container byte-for-byte.

Find chunks through RIFF, not a fixed Ogg offset of 66. Complete streams support Vorbis modes 1/1+/3/3+;
mode 2's separated headers are not treated as a complete Ogg. Validate page boundaries, sequence, CRC and EOS;
editor metadata must not become extracted audio.

## Original-engine evidence

`Cannonball.exe`, SHA-256:
`df4b5b6dffc7002370d0772869baaba551b08fca970e92609ca64b08174f89ba`.
Addresses below are PE virtual addresses.

| Address | Behaviour |
| --- | --- |
| `0x41257a` | Load vorbis.acm and register the ACM driver |
| `0x4411fe–0x441221` | Identify PCM and find data |
| `0x4414b0–0x44153e` | Read compressed source format and construct target PCM parameters |
| `0x441573` | Open the ACM conversion stream |
| `0x441678–0x441682` | Locate compressed data |
| `0x441862` | Convert audio |

The same WAV suffix can contain different formats: inspect the container.
See [DLL evidence](RSCOMP_MEDIA_AUDIT.en.md) for RsComp.dll media instructions.

## Maintenance and checks

Implementation: `src/wav_ogg.cpp`; tests: `tests/wav_ogg_test.cpp`.
Check extraction/wrapping, PCM decoding, malformed inputs and recursion.
Acceptance of generated containers by the original game still needs in-game checks.
