# Documentation

[中文](README.zh.md) | [English](README.en.md) | [Русский](README.ru.md) | [日本語](README.ja.md)

[Project guide](../README.en.md)

## Usage and builds

- [Overview and use](../README.en.md); CLI help: `liarsofttool -h`.
- [Source builds and packaging](BUILDING.en.md)
- [TSC command reference](GSC_OPCODE_REFERENCE.en.md)
- [RScript Runtime usage and build](../runtime/README.en.md)
- [Audio dependency licenses, original text](THIRD_PARTY_AUDIO_LICENSES.md)

## Developer references

- [Roadmap](ROADMAP.en.md)
- [TSC diagnostics API](TSC_DIAGNOSTICS.en.md)
- [Runtime DLL API](../runtime/SDK.en.md)
- [Runtime native evidence](../runtime/ENGINE_AUDIT.en.md)
- [Working agreements](../AGENTS.en.md)

The following records document particular binaries, not a universal engine specification.

- [Audio containers](AUDIO_PROCESSING_AUDIT.en.md)
- [CannonBall](CANNONBALL_ENGINE_AUDIT.en.md)
- [Evermaiden](EVERMAIDEN_ENGINE_AUDIT.en.md)
- [Khime / Albatross](KHIME_ALBATROSS_ENGINE_AUDIT.en.md)
- [Jeanne / Kagerou](JEANNE_KAGEROU_ENGINE_AUDIT.en.md)
- [CNMI trial](CNMI_TRIAL_ENGINE_AUDIT.en.md)
- [RsComp.dll and media commands](RSCOMP_MEDIA_AUDIT.en.md)

## Development checks

```bash
cmake --build build -j2
ctest --test-dir build --output-on-failure
git diff --check
```

Changes to GSC/TSC or media output also require rscript2renpy reading and generation checks.
Conversion tests do not replace checks in the original game.
