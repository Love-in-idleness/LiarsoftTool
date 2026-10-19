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

GUI 默认将 GSC 转成 TSC；若只需要提取译文，勾选 **GSC→TXT**。
CLI 默认提取 TXT，使用 `--gsc-to-tsc` 才生成 TSC。

### 常用命令

以下示例在资源工作副本中执行。省略 `-e` 时默认使用 CP932；
补丁的编码应按实际文件选择，不由语言名称决定。

| 需求 | 命令 |
| --- | --- |
| 提取脚本文本 | `liarsofttool scenario.gsc` |
| 导出 TSC | `liarsofttool --gsc-to-tsc scenario.gsc` |
| 将中文 TSC 编译为 GSC | `liarsofttool -e gbk scenario.tsc` |
| 将 TXT 译文写回 GSC | `liarsofttool -e gbk -r original.gsc trans.txt` |
| 解包 XFL / LWG | `liarsofttool archive.xfl` / `liarsofttool scene.lwg` |
| 打包文件夹 | `liarsofttool ./resources` |
| WCG → PNG | `liarsofttool image.wcg` |
| PNG / JPG / BMP → WCG | `liarsofttool image.png` |
| LIM → 无损 WebP | `liarsofttool image.lim` |
| WebP → LIM | `liarsofttool image.webp` |
| 提取 WAV 内的 OGG | `liarsofttool audio.wav` |
| OGG → PCM WAV | `liarsofttool audio.ogg` |
| 将 OGG 压缩封装为 WAV | `liarsofttool --vorbis-in-wav audio.ogg` |
| 规范化 CodeX BMP 的透明度 | `liarsofttool --unpack-only image.bmp` |
| EXE 转为 GBK 字体与标点设置 | `liarsofttool -e gbk game.exe` |

标准 PCM WAV 可以直接播放，解包时会原样保留，不会另外生成 OGG。
EXE 转换生成另一个文件；运行游戏时仍需使用与资源匹配的系统区域设置或 Locale Emulator。

递归解包整个工作目录，并准备供 rscript2renpy 使用的资源：

```bash
liarsofttool -R --unpack-only --gsc-to-tsc -e cp932 ./Game
```

递归转换并封包：

```bash
liarsofttool -R --pack-only -e gbk ./resources
```

递归封包先处理子目录。带 `.meta.xml` 的目录生成 LWG，其余生成 XFL。
封包只收录 LIM、WCG、GSC、WAV、XML、LWG、XFL、MSK。启用递归时会先转换
PNG、TSC 等编辑文件，不会直接收入封包。请保留解包生成的 `.meta.xml`。

同时启用“仅封包”和“仅解包”时不执行操作。递归处理中遇到单个文件失败会给出
警告并继续；空封包会报错。

### 脚本翻译

只改对话时可使用 TXT：在 `>` 后填写译文，再以原 GSC 为参考写回。
需要编辑完整脚本时使用 TSC，修改其中的命令和文字后重新编译。
TSC 用 UTF-8 保存，生成中文 GSC 时使用 `-e gbk`。

GSC→TSC 是实验性功能，不是所有游戏都已适配。编辑前保留原 GSC，
不要删除 TSC 中自动生成的标签和元数据。
命令含义见 [TSC 指令参考](docs/GSC_OPCODE_REFERENCE.md)。

### 命令行参数

| 参数 | 用途 |
| --- | --- |
| `-e, --encoding` | CP932（日文，默认）、GBK 或 CP1251 |
| `-o, --output` | 指定输出文件或目录 |
| `-r, --reference` | TXT 写回时使用的原 GSC |
| `-R, --recursive` | 递归处理子目录和封包，同时转换资源 |
| `--pack-only` / `--unpack-only` | 只处理选定方向 |
| `--gsc-to-tsc` | GSC 导出为 TSC |
| `--vorbis-in-wav` | OGG→WAV 时保留压缩音频 |
| `-j, --jobs` | 递归转换线程数，0 自动，1 串行，最大 64 |
| `-h, --help` | 查看帮助 |

可以传入多个文件，也可以使用通配符，例如 `liarsofttool *.wcg`。

### 从源码编译

需要 C++17 编译器、CMake、WebP、Vorbis/Ogg；图形界面还需要 GTKmm 3。
Windows 和 Linux 使用同一套 GUI。

Debian / Ubuntu：

```bash
sudo apt install build-essential cmake pkg-config libwebp-dev libvorbis-dev libogg-dev libgtkmm-3.0-dev
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j2
```

程序位于 `build/`。只构建命令行时，配置阶段添加 `-DBUILD_GUI=OFF`。
Windows 编译和打包见 [构建说明](docs/BUILDING.md)。

### 更多说明

[文档目录](docs/README.md) · [TSC 指令参考](docs/GSC_OPCODE_REFERENCE.md)

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

The GUI exports GSC to TSC by default; enable **GSC→TXT** for translation text.
The CLI exports TXT by default; add `--gsc-to-tsc` for TSC.

### Common commands

Examples operate on working copies. The default encoding is CP932.
Choose a patch's encoding from its actual files, not its language label.

| Task | Command |
| --- | --- |
| Extract script text | `liarsofttool scenario.gsc` |
| Export TSC | `liarsofttool --gsc-to-tsc scenario.gsc` |
| Compile Chinese TSC to GSC | `liarsofttool -e gbk scenario.tsc` |
| Import translated TXT | `liarsofttool -e gbk -r original.gsc trans.txt` |
| Unpack XFL / LWG | `liarsofttool archive.xfl` / `liarsofttool scene.lwg` |
| Pack a folder | `liarsofttool ./resources` |
| WCG → PNG | `liarsofttool image.wcg` |
| PNG / JPG / BMP → WCG | `liarsofttool image.png` |
| LIM → lossless WebP | `liarsofttool image.lim` |
| WebP → LIM | `liarsofttool image.webp` |
| Extract OGG from WAV | `liarsofttool audio.wav` |
| OGG → PCM WAV | `liarsofttool audio.ogg` |
| Wrap OGG in WAV without decoding | `liarsofttool --vorbis-in-wav audio.ogg` |
| Normalize CodeX BMP transparency | `liarsofttool --unpack-only image.bmp` |
| Set GBK font and punctuation rules in an EXE | `liarsofttool -e gbk game.exe` |

Standard PCM WAV is already playable and remains unchanged during unpacking;
no OGG is created. EXE conversion writes a separate file. The game still needs
a matching system locale or Locale Emulator configuration.

Recursively prepare a working directory for rscript2renpy:

```bash
liarsofttool -R --unpack-only --gsc-to-tsc -e cp932 ./Game
```

Convert resources and pack recursively:

```bash
liarsofttool -R --pack-only -e gbk ./resources
```

Recursive packing handles children first. Folders with `.meta.xml` become LWG;
other folders become XFL. Archives include only LIM, WCG, GSC, WAV, XML, LWG,
XFL and MSK. With recursive packing, editing files such as PNG and TSC are
converted first, not stored directly. Keep the `.meta.xml` files produced by unpacking.

Enabling both direction filters skips every input. Recursive processing warns
and continues when one file fails; an empty archive is an error.

### Translating scripts

For dialogue-only changes, edit the text after `>` in TXT and import it using
the original GSC as the reference. For full script editing, export TSC, edit
its commands and text, then compile it back. Save TSC as UTF-8; use `-e gbk`
when compiling Chinese GSC.

GSC→TSC is experimental and does not support every game. Keep the original GSC,
and retain generated labels and metadata in TSC.
See the [TSC command reference](docs/GSC_OPCODE_REFERENCE.en.md).

### Command-line options

| Option | Purpose |
| --- | --- |
| `-e, --encoding` | CP932 (Japanese, default), GBK or CP1251 |
| `-o, --output` | Output file or directory |
| `-r, --reference` | Original GSC for importing TXT |
| `-R, --recursive` | Process nested folders and archives, converting resources |
| `--pack-only` / `--unpack-only` | Restrict the conversion direction |
| `--gsc-to-tsc` | Export GSC as TSC |
| `--vorbis-in-wav` | Preserve compressed audio when producing WAV |
| `-j, --jobs` | Recursive conversion workers: 0 automatic, 1 serial, up to 64 |
| `-h, --help` | Show help |

Multiple inputs and wildcards are supported, for example `liarsofttool *.wcg`.

### Building from source

Requires a C++17 compiler, CMake, WebP and Vorbis/Ogg. The GUI also requires
GTKmm 3. Windows and Linux share the same frontend.

On Debian / Ubuntu:

```bash
sudo apt install build-essential cmake pkg-config libwebp-dev libvorbis-dev libogg-dev libgtkmm-3.0-dev
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j2
```

Executables are in `build/`. Add `-DBUILD_GUI=OFF` when configuring a CLI-only
build. See [build instructions](docs/BUILDING.md) for Windows and packaging.

### Further reading

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
- [GTK / GTKmm](https://www.gtk.org/) — shared desktop interface.
