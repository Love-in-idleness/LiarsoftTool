# 构建与打包

[中文](BUILDING.zh.md) | [English](BUILDING.en.md) | [Русский](BUILDING.ru.md) | [日本語](BUILDING.ja.md)

[主说明](../README.zh.md)

所有命令在仓库根目录执行。需要 CMake 3.10+、C++17 编译器、WebP、Vorbis/Ogg、
uchardet 静态库及 libcurl 7.85+；Unix 还需 iconv。
GUI 另需 GTK4 4.10+、libadwaita 1.5+、gettext 和 pkg-config。
音频预览需要 GTK 媒体后端和 GStreamer 的基础／常用插件。

## Linux

Debian / Ubuntu（软件源须满足上述最低版本）：

```bash
sudo apt install build-essential cmake pkg-config libwebp-dev libvorbis-dev libogg-dev libuchardet-dev libcurl4-openssl-dev libgtk-4-dev libadwaita-1-dev gettext gstreamer1.0-plugins-base gstreamer1.0-plugins-good
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j2
ctest --test-dir build --output-on-failure
```

程序为 `build/liarsofttool` 和 `build/liarsofttool-gui`。
仅构建 CLI 时，配置阶段加 `-DBUILD_GUI=OFF`，可省去 GUI 和媒体预览依赖。
编译依赖属于构建机，不必要求下载发布程序的用户安装整套开发环境。

## MSYS2 UCRT64

在 MSYS2 的 UCRT64 终端中安装依赖，并在仓库根目录执行：

```bash
pacman -S mingw-w64-ucrt-x86_64-{gcc,cmake,make,pkgconf,libadwaita,gettext-tools,libwebp,libvorbis,libogg,uchardet,curl,gstreamer,gst-plugins-base,gst-plugins-good}
cmake -S . -B build -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build -j2
bash tools/bundle_windows.sh build build/windows-bundle /ucrt64
```

打包目标目录必须尚不存在。`windows-bundle` 是中间运行库目录，
可运行其中的 `bin/liarsofttool-gui.exe` 检查构建。
GStreamer 插件和扫描器用于音频预览，须与 GTK 一同保留。

只需要 CLI 时，配置阶段添加 `-DBUILD_GUI=OFF`，无需 GTK4/libadwaita；
GUI 打包脚本不用于 CLI-only 构建。

## Windows 单文件发布

安装 [Enigma Virtual Box](https://www.enigmaprotector.com/en/downloads.html)，
使用 Python 3.9+ 与 GNU objdump 将上述中间目录分别封装为 CLI、GUI。
封装工具只用于构建，不需要安装在用户电脑上。

```bash
python tools/pack_windows.py build/windows-bundle build/windows-single "C:/Program Files/Enigma Virtual Box/enigmavbconsole.exe"
python tests/test_windows_pack.py
```

输出目录必须尚不存在。分别发布其中的 `liarsofttool.exe`、
`liarsofttool-gui.exe`，不再把两个程序合成 ZIP。
脚本开启压缩并保留 GUI 的媒体子进程支持；CLI 仅包含自己的运行库。
封装后仍须验证转换、四语界面、图片与音频预览，不能只检查 EXE 是否生成。

Linux 构建机也可使用本脚本，需在当前环境准备 Wine；路径由 `winepath` 转换。

## Linux 交叉编译

先准备 Windows 目标的 WebP、Vorbis/Ogg、uchardet 静态库、libcurl 7.85 及以上和 MinGW 工具链。
通过 `CMAKE_PREFIX_PATH` 或对应的 CMake 库路径变量指定目标库，
不要链接 Linux 主机上的库。

CLI 示例：

```bash
cmake -S . -B build/win -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_SYSTEM_NAME=Windows -DBUILD_GUI=OFF \
    -DCMAKE_CXX_COMPILER=x86_64-w64-mingw32-g++-posix \
    -DCMAKE_PREFIX_PATH=/path/to/windows-dependencies
cmake --build build/win -j2
```

GUI 还需匹配的 GTK4/libadwaita 开发库和 pkg-config 配置。
所有编译器和库应使用同一目标及运行时；不要混用 UCRT64/MINGW64 或不同线程模型。
