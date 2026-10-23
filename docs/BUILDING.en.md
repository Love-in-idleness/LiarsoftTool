# Windows builds and packaging

[中文](BUILDING.zh.md) | [English](BUILDING.en.md) | [Русский](BUILDING.ru.md)

[Project guide](../README.en.md)

## MSYS2 UCRT64

Install dependencies in an MSYS2 UCRT64 terminal and run from the repository root:

```bash
pacman -S mingw-w64-ucrt-x86_64-{gcc,cmake,make,pkgconf,libadwaita,gettext-tools,libwebp,libvorbis,libogg,curl,gstreamer,gst-plugins-base,gst-plugins-good}
cmake -S . -B build -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build -j2
bash tools/bundle_windows.sh build build/windows-bundle /ucrt64
```

The bundle destination must not exist. This is an intermediate runtime directory;
launch `build/windows-bundle/bin/liarsofttool-gui.exe` to check the build.
Keep GStreamer plugins and its scanner with GTK for audio previews.
For CLI-only builds add `-DBUILD_GUI=OFF`; GTK4/libadwaita and the GUI bundler are not needed.

## Single-file Windows releases

Install [Enigma Virtual Box](https://www.enigmaprotector.com/en/downloads.html).
Use Python 3.9+ and GNU objdump to package the intermediate directory into separate CLI and GUI executables.
The packer is a build tool; users do not need it installed.

```bash
python tools/pack_windows.py build/windows-bundle build/windows-single "C:/Program Files/Enigma Virtual Box/enigmavbconsole.exe"
python tests/test_windows_pack.py
```

The output directory must not exist. Publish `liarsofttool.exe` and `liarsofttool-gui.exe`
separately, not together in a ZIP. Compression and GUI media subprocess support are enabled;
the CLI includes only its own runtime libraries. Test conversion, all three languages, images and audio
after packing; producing an EXE alone is not enough.

Linux build hosts can use the same script with Wine installed; paths are converted with `winepath`.

## Cross-building from Linux

Prepare Windows-target WebP, Vorbis/Ogg, libcurl 7.85 or newer and a MinGW toolchain.
Select target libraries through `CMAKE_PREFIX_PATH` or corresponding CMake variables; do not link host Linux libraries.

CLI example:

```bash
cmake -S . -B build/win -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_SYSTEM_NAME=Windows -DBUILD_GUI=OFF \
    -DCMAKE_CXX_COMPILER=x86_64-w64-mingw32-g++-posix \
    -DCMAKE_PREFIX_PATH=/path/to/windows-dependencies
cmake --build build/win -j2
```

GUI builds additionally need matching GTK4/libadwaita development libraries and pkg-config files.
Use one target, runtime and thread model throughout; do not mix UCRT64 and MINGW64 libraries.

## GTK4 migration probe for developers

The production GUI calls GTK4/libadwaita's C API from C++. This independent minimal window verifies creation,
system/light/dark themes and closing, not conversion. It requires GTK4 and libadwaita 1.4 or newer.

```bash
cmake -S tests/gtk4 -B build/gtk4-probe
cmake --build build/gtk4-probe -j2
ctest --test-dir build/gtk4-probe --output-on-failure
```

On Windows install the UCRT64 packages for `gcc`, `cmake`, `pkgconf` and `libadwaita`
in a separate MSYS2 UCRT64 environment, then run these commands. Do not reuse the old MINGW64 GTKmm stack.
