# LiarsoftTool

[中文](README.zh.md) | [English](README.en.md) | [Русский](README.ru.md) | [日本語](README.ja.md)

![LiarsoftTool](image.png)

LiarsoftTool handles resources from CodeX RScript games, including titles by
Liar-soft and raiL-soft. It packs and unpacks archives, converts images and audio,
and extracts or rebuilds scripts.
The current version does not convert game EXE files.

To port a game to Ren'Py, use it with
[rscript2renpy](https://github.com/Love-in-idleness/rscript2renpy).

## Getting started

Windows provides separate compressed, single-file GUI and CLI executables.
Download the one you need and run it directly; no extraction is required.
Linux provides separate GUI and CLI binaries. Grant execute permission after downloading; no extraction is needed.
The GUI includes its translations but still requires the system libraries listed below.
Source builds produce `liarsofttool-gui` and the command-line program `liarsofttool`.

Work on a copy of your game resources. Output normally goes beside the input
and rebuilding may overwrite files with the same name.

In the GUI:

1. Add files or folders, or drag them into the window.
2. Encoding defaults to `auto` for GSC detection. Select an encoding manually if the result is ambiguous or looks wrong: usually CP932 for Japanese or GBK for Chinese patches.
3. Set Recursive, Pack only or Unpack only as needed, then check Conversion tasks and output paths.
4. Start conversion. Use **Copy details** to copy a complete error report.

Both the GUI and CLI export GSC to TSC by default.
Enable **GSC→TXT** in the GUI to extract translation text.

Detection also applies to GSC previews. Short text may be inconclusive; ASCII cannot identify the original encoding. Check the result.
Packing, checking or creating GSC requires an explicit encoding. In `auto`, archive names and other legacy text still use CP932.

Click **Check for updates** to check manually and open the GitHub releases page.
The application never connects on startup or downloads and installs updates automatically.

## Resource browsing

Open a folder, XFL or LWG in Resources. Double-click entries to enter folders or
nested archives. Select an entry to preview images, audio or text, or decompile
GSC into a read-only TSC view. Change the encoding if needed; TSC always uses UTF-8.
Unsupported or damaged files remain available in the paginated hex view.

Browsing and previews neither unpack nor modify the input. Export original bytes
saves only the selected entry, without conversion. To convert an embedded entry,
export it first, then add it to Conversion tasks. Standalone files can be added directly.

Images support zoom and transparency. Audio supports playback, pause, seeking and
volume. Audio previews need a GTK media backend and GStreamer Ogg/Vorbis plugins;
the Windows GUI executable includes them.

## Command-line help

For supported formats, options and conversion examples, run:

```bash
liarsofttool -h
```

## Translating scripts

For dialogue-only changes, edit the text after `>` in TXT and import it using
the original GSC as the reference. For full script editing, export TSC, edit
its commands and text, then compile it back. Save TSC as UTF-8 and choose
the encoding used by the target game when compiling GSC.
Available encodings are CP932 / Windows-31J, GBK, GB18030, Big5 / CP950,
Korean CP949, Windows CP1250–CP1258 and UTF-8. Conversion and resource previews
share the same encoding choices.

GSC→TSC is experimental and does not support every game. Keep the original GSC,
and retain generated labels and metadata in TSC.
See the [TSC command reference](docs/GSC_OPCODE_REFERENCE.en.md).

## Building from source

Requires a C++17 compiler, CMake, WebP, Vorbis/Ogg, uchardet (including its static library) and libcurl 7.85 or newer. The GUI also requires
GTK4 4.10, libadwaita 1.5 or newer, and gettext. Windows and Linux share the same frontend.
Preferences contain conversion options, the system/light/dark theme and the interface
language. English, Russian, Simplified Chinese and Japanese are supported; language changes apply after restart.

On Debian / Ubuntu:

```bash
sudo apt install build-essential cmake pkg-config libwebp-dev libvorbis-dev libogg-dev libuchardet-dev libcurl4-openssl-dev libgtk-4-dev libadwaita-1-dev gettext gstreamer1.0-plugins-base gstreamer1.0-plugins-good
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j2
```

Executables are in `build/`. Add `-DBUILD_GUI=OFF` when configuring a CLI-only
build. See [build instructions](docs/BUILDING.en.md) for Windows and packaging.

## Further reading

See the [roadmap](docs/ROADMAP.en.md) for planned work.

[Documentation](docs/README.en.md) · [TSC command reference](docs/GSC_OPCODE_REFERENCE.en.md)

Use game resources you legally own. Successful conversion does not guarantee
compatibility with every engine version; test edited resources in the game.

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
