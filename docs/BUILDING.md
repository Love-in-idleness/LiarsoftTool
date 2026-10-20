# Windows 构建与打包

[返回主说明](../README.md) · [English](#english)

## MSYS2 UCRT64

在 MSYS2 的 UCRT64 终端中安装依赖，并在仓库根目录执行：

```bash
pacman -S mingw-w64-ucrt-x86_64-{gcc,cmake,make,pkgconf,gtkmm3,libwebp,libvorbis,libogg,curl}
cmake -S . -B build -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build -j2
bash tools/bundle_windows.sh build build/windows-bundle /ucrt64
```

打包目标目录必须尚不存在。运行
`build/windows-bundle/bin/liarsofttool-gui.exe`。
分发完整的 `windows-bundle` 目录，不要只发送 EXE。

只需要 CLI 时，配置阶段添加 `-DBUILD_GUI=OFF`，无需 GTKmm；
GUI 打包脚本不用于 CLI-only 构建。

## Linux 交叉编译

先准备 Windows 目标的 WebP、Vorbis/Ogg、libcurl 7.85 及以上和 MinGW 工具链。
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

GUI 还需匹配的 GTKmm 开发库和 pkg-config 配置。
所有编译器和库应使用同一目标及运行时；不要混用 UCRT64/MINGW64 或不同线程模型。

## English

Use the commands above in an MSYS2 UCRT64 terminal. The GUI bundle directory
must not already exist. Run `bin/liarsofttool-gui.exe` from the generated bundle
and distribute the complete directory, including its libraries and resources.

For CLI-only builds, add `-DBUILD_GUI=OFF`; the GUI packaging script is not needed.
Cross-building from Linux requires Windows-target WebP, Vorbis/Ogg, libcurl 7.85 or newer and, for
the GUI, GTKmm. Use a matching compiler, library stack and runtime throughout.
