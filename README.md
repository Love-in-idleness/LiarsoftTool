# LiarsoftTool · [中文](#中文) | [English](#english)

![LiarsoftTool](image.png)

## 中文

LiarsoftTool 用于处理 CodeX RScript 引擎游戏的资源，支持封包解包、图像转换、
脚本提取与回写、音频转换，适用于 Liar-soft、raiL-soft 等社团的作品。

若要将游戏移植到 Ren'Py，请配合
[rscript2renpy](https://github.com/Love-in-idleness/rscript2renpy) 使用。

### 开始使用

Windows 用户解压完整发行包后运行 `bin/liarsofttool-gui.exe`，不要只复制 EXE。
Linux 用户运行 `liarsofttool-gui`；命令行程序为 `liarsofttool`。

先复制一份游戏资源再操作。生成文件默认写在输入文件旁，重建可能覆盖同名文件。

图形界面的操作顺序：

1. 添加文件或文件夹，也可以直接拖入窗口。
2. 选择资源编码。日文通常用 CP932，中文补丁通常用 GBK。
3. 按需选择递归、仅封包或仅解包，检查列表中的输出路径。
4. 开始转换。遇到问题时，用 “Copy details” 复制完整提示。

GUI 和 CLI 默认都将 GSC 转成 TSC。只需要提取文本时，GUI 勾选 **GSC→TXT**。

点击 **Check for updates** 可手动检查新版并打开 GitHub 发布页。
程序不会在启动时联网，也不会自动下载或安装更新。

### 命令行帮助

支持格式、参数和转换示例请在软件中查看：

```bash
liarsofttool -h
```

### 脚本翻译

只改对话时可使用 TXT：在 `>` 后填写译文，再以原 GSC 为参考写回。
需要编辑完整脚本时使用 TSC，修改其中的命令和文字后重新编译。
TSC 用 UTF-8 保存，生成 GSC 时选择与目标游戏匹配的编码。

GSC→TSC 是实验性功能，不是所有游戏都已适配。编辑前保留原 GSC，
不要删除 TSC 中自动生成的标签和元数据。
命令含义见 [TSC 指令参考](docs/GSC_OPCODE_REFERENCE.md)。

### 从源码编译

需要 C++17 编译器、CMake、WebP、Vorbis/Ogg、libcurl 7.85 及以上；图形界面还需要 GTK4 4.10、libadwaita 1.5 及以上和 gettext。
Windows 和 Linux 使用同一套 GUI。首选项集中设置转换选项、主题和界面语言。
主题可跟随系统或使用浅色、深色；界面支持中、英、俄语，手动选择后重启生效。

Debian / Ubuntu：

```bash
sudo apt install build-essential cmake pkg-config libwebp-dev libvorbis-dev libogg-dev libcurl4-openssl-dev libgtk-4-dev libadwaita-1-dev gettext
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j2
```

程序位于 `build/`。只构建命令行时，配置阶段添加 `-DBUILD_GUI=OFF`。
Windows 编译和打包见 [构建说明](docs/BUILDING.md)。

### 更多说明

[文档目录](docs/README.md) · [TSC 指令参考](docs/GSC_OPCODE_REFERENCE.md)
· [开发规划](docs/ROADMAP.md)

请使用合法持有的游戏资源。脚本、图像或 EXE 能成功转换，不代表所有引擎版本
都能直接使用，替换回游戏后请自行测试。

---

## English

LiarsoftTool handles resources from CodeX RScript games, including titles by
Liar-soft and raiL-soft. It packs and unpacks archives, converts images and audio,
and extracts or rebuilds scripts.

To port a game to Ren'Py, use it with
[rscript2renpy](https://github.com/Love-in-idleness/rscript2renpy).

### Getting started

On Windows, extract the complete release package and run
`bin/liarsofttool-gui.exe`. Keep the bundled files together.
On Linux, run `liarsofttool-gui`. The command-line program is `liarsofttool`.

Work on a copy of your game resources. Output normally goes beside the input
and rebuilding may overwrite files with the same name.

In the GUI:

1. Add files or folders, or drag them into the window.
2. Select the resource encoding: usually CP932 for Japanese or GBK for Chinese patches.
3. Choose Recursive, Pack only or Unpack only as needed, then check the output paths.
4. Start conversion. Use **Copy details** to copy a complete error report.

Both the GUI and CLI export GSC to TSC by default.
Enable **GSC→TXT** in the GUI to extract translation text.

Click **Check for updates** to check manually and open the GitHub releases page.
The application never connects on startup or downloads and installs updates automatically.

### Command-line help

For supported formats, options and conversion examples, run:

```bash
liarsofttool -h
```

### Translating scripts

For dialogue-only changes, edit the text after `>` in TXT and import it using
the original GSC as the reference. For full script editing, export TSC, edit
its commands and text, then compile it back. Save TSC as UTF-8 and choose
the encoding used by the target game when compiling GSC.

GSC→TSC is experimental and does not support every game. Keep the original GSC,
and retain generated labels and metadata in TSC.
See the [TSC command reference](docs/GSC_OPCODE_REFERENCE.en.md).

### Building from source

Requires a C++17 compiler, CMake, WebP, Vorbis/Ogg and libcurl 7.85 or newer. The GUI also requires
GTK4 4.10, libadwaita 1.5 or newer, and gettext. Windows and Linux share the same frontend.
Preferences contain conversion options, the system/light/dark theme and the interface
language. English, Russian and Simplified Chinese are supported; language changes apply after restart.

On Debian / Ubuntu:

```bash
sudo apt install build-essential cmake pkg-config libwebp-dev libvorbis-dev libogg-dev libcurl4-openssl-dev libgtk-4-dev libadwaita-1-dev gettext
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j2
```

Executables are in `build/`. Add `-DBUILD_GUI=OFF` when configuring a CLI-only
build. See [build instructions](docs/BUILDING.md) for Windows and packaging.

### Further reading

See the [roadmap](docs/ROADMAP.md#english) for planned work.

[Documentation](docs/README.md) · [TSC command reference](docs/GSC_OPCODE_REFERENCE.en.md)

Use game resources you legally own. Successful conversion does not guarantee
compatibility with every engine version; test edited resources in the game.

---

## License and credits

LiarsoftTool is licensed under GPLv3. Third-party components retain their own licenses.

- [RaiLTools](https://github.com/EusthEnoptEron/RaiLTools) — archive and script format research.
- [arc_unpacker](https://github.com/vn-tools/arc_unpacker) — GPLv3 image decompression code.
- [GARbro](https://github.com/crskycode/GARbro) — WCG encoder reference.
- [stb](https://github.com/nothings/stb) — image loading and writing.
- [WebP](https://chromium.googlesource.com/webm/libwebp/) — lossless WebP support.
- [Ogg / Vorbis](https://xiph.org/vorbis/) — audio support; [license notices](docs/THIRD_PARTY_AUDIO_LICENSES.md).
- [GTK4 / libadwaita](https://www.gtk.org/) — shared desktop interface.
- [libcurl](https://curl.se/libcurl/) — HTTPS update checks.
