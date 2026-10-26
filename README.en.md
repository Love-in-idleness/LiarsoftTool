# LiarsoftTool

[中文](README.zh.md) | [English](README.en.md) | [Русский](README.ru.md) | [日本語](README.ja.md)

![LiarsoftTool](image.png)

A resource tool for CodeX RScript games: pack and unpack archives, convert images and audio,
and extract, edit, and rebuild GSC scripts. It targets works by Liar-soft, raiL-soft, and related creators;
support varies by game. Game EXE conversion is not available.

To generate Ren'Py projects, use the separate [rscript2renpy](https://github.com/Love-in-idleness/rscript2renpy) project.

## Installation and first use

Download the program for your system from [Releases](https://github.com/Love-in-idleness/LiarsoftTool/releases).
GUI and CLI are separate downloads: compressed single-file EXEs on Windows, standalone binaries on Linux.
Grant execute permission on Linux. The GUI needs GTK4 4.10+ and libadwaita 1.5+;
audio previews also need a GTK media backend and GStreamer Ogg/Vorbis plugins. The Windows GUI bundles media runtimes.
For source builds, see [Building](docs/BUILDING.en.md).

Work on copies of game files. Output defaults to the input's directory and may overwrite files with the same name.

1. Add files or folders to Conversion tasks, or drag them into the window.
2. Check encoding and output paths; select recursion, pack-only, or unpack-only as needed.
3. Start conversion. Copy the full error details if something fails.

GUI and CLI both export GSC to TSC by default. Choose **GSC→TXT** in the GUI for dialogue-only extraction.
Resource encoding defaults to `auto`, detecting GSC encoding. Short text or mixed encodings may need a manual choice.
Japanese usually uses CP932 and Chinese patches GBK. GSC compilation, packing, and encoding checks require an explicit encoding;
`auto` does not detect archive filenames or other legacy text, which still use CP932.

## Browsing and previews

Resource browsing opens folders, XFL, LWG, and nested archives without unpacking them first.
Select an entry to view images, play audio, read text, display GSC as read-only TSC, or inspect paged hexadecimal bytes.
Images support zoom and transparent backgrounds; audio supports pause, seeking, and volume.
Change encoding for garbled text; TSC always uses UTF-8.

Browsing does not modify inputs. Export raw bytes does not convert formats.
To convert an archive entry, export it first and add it to Conversion tasks.

Preferences hold conversion options, system/light/dark themes, and interface language.
The UI supports Chinese, English, Russian, and Japanese, defaulting to the system language.
A manual language change takes effect after restart.
Update checks are manual and open the release page; no automatic download or installation occurs.

## Editing scripts

- TXT: enter translations after `>` and rebuild using the original GSC as a reference.
- TSC: edit complete commands and text, then compile with the target game's encoding. TSC itself is UTF-8.

GSC→TSC is experimental. Keep the original GSC and generated labels and metadata.
Successful conversion does not establish compatibility with every engine version.
See the [TSC reference](docs/GSC_OPCODE_REFERENCE.en.md) for commands.

## VS Code extension

This branch includes the experimental [CodeX TSC extension](tsc-vscode/README.en.md) for highlighting, checking, compiling, and graphical story authoring.
It provides SevenBridge and Forest profiles. Build its VSIX separately; it is not installed with the main program or merged into `master`.

## Help and documentation

Program help is the source for options, supported formats, and conversion examples:

```bash
liarsofttool -h
```

[Documentation](docs/README.en.md) · [Building](docs/BUILDING.en.md) · [Roadmap](docs/ROADMAP.en.md)

For reports, include program version, input type, encoding, and full error details.
Use legally owned resources; do not upload game files publicly.

## License and credits

LiarsoftTool is licensed under GPLv3. Third-party components retain their own licenses.

- [RaiLTools](https://github.com/EusthEnoptEron/RaiLTools) — archive and script format research.
- [arc_unpacker](https://github.com/vn-tools/arc_unpacker) — GPLv3 image decompression code.
- [GARbro](https://github.com/crskycode/GARbro) — WCG encoder reference.
- [stb](https://github.com/nothings/stb) — image loading and writing.
- [WebP](https://chromium.googlesource.com/webm/libwebp/) — lossless WebP support.
- [Ogg / Vorbis](https://xiph.org/vorbis/) — audio support; [license notices](docs/THIRD_PARTY_AUDIO_LICENSES.md).
- [GTK4 / libadwaita](https://www.gtk.org/) — shared desktop interface.
- [GStreamer](https://gstreamer.freedesktop.org/) — audio previews through GTK's media backend.
- [libcurl](https://curl.se/libcurl/) — HTTPS update checks.
- [uchardet](https://www.freedesktop.org/wiki/Software/uchardet/) — GSC encoding detection; [license notices](docs/THIRD_PARTY_UCHARDET_LICENSES.md). Statically linked, unmodified 0.0.8 [source](https://deb.debian.org/debian/pool/main/u/uchardet/uchardet_0.0.8.orig.tar.xz), used under GPLv2 or later.
