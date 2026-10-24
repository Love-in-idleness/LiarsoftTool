# Windows 构建与打包

[中文](BUILDING.zh.md) | [English](BUILDING.en.md) | [Русский](BUILDING.ru.md) | [日本語](BUILDING.ja.md)

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

## GTK4 迁移检查（开发用）

正式 GUI 使用 C++ 调用 GTK4/libadwaita 的 C API。这个独立的最小窗口用于
验证窗口创建、浅色／深色／系统主题切换及关闭；不提供转换功能。
需要 GTK4 和 libadwaita 1.4 及以上。

```bash
cmake -S tests/gtk4 -B build/gtk4-probe
cmake --build build/gtk4-probe -j2
ctest --test-dir build/gtk4-probe --output-on-failure
```

Windows 在独立的 MSYS2 UCRT64 环境中安装 `gcc`、`cmake`、`pkgconf` 和
`libadwaita` 对应的 UCRT64 包，再执行上述命令。不要复用 MINGW64 的旧 GTKmm 库。
