# LiarsoftTool &nbsp; [中文](#中文) | [English](#english)

A comprehensive toolkit for visual novels powered by the **Codex RScript** engine,
developed by circles including **Liar-soft (骗子社)**, **rail-soft**, and
**スタジオ奪トランス (Studio Ubai Trans)**. Handles archive packing/unpacking,
image decoding/encoding, script extraction/injection, and audio extraction.

针对 **Liar-soft（骗子社）**、**rail-soft**、**スタジオ奪トランス**
等社团使用 **Codex RScript** 引擎开发的视觉小说/文字游戏的综合资源处理工具。支持封包解包、图像编解码、脚本提取/注入、音频提取。

**References / 参考项目：**
- [RaiLTools](https://github.com/EusthEnoptEron/RaiLTools) — original C# reverse-engineering (GSC/XFL/LWG/WCG)
- [arc_unpacker](https://github.com/vn-tools/arc_unpacker) — C++ port of CG decompression (WCG/LIM)
- [GARbro](https://github.com/crskycode/GARbro) — WCG encoder reference implementation

---

## 中文

### 速查表

| 需求 | 命令 |
|------|------|
| 提取脚本原文 | `liarsofttool -e gbk scenario.gsc` |
| 翻译后注回 | `liarsofttool -e gbk -r original.gsc trans.txt` |
| 解包资源封包 | `liarsofttool -e shift_jis archive.xfl` |
| 解包场景封包 | `liarsofttool cgview.lwg` |
| WCG 转 PNG | `liarsofttool image.wcg` |
| LIM 转 PNG | `liarsofttool image.lim` |
| PNG 转 WCG | `liarsofttool image.png` |
| 提取内嵌 OGG | `liarsofttool audio.wav` |
| 打包目录→XFL | `liarsofttool -e shift_jis ./dir` |
| 打包目录→LWG | `liarsofttool -e shift_jis ./dir_with_meta` |
| 批量转换 | `liarsofttool *.png` 或 `liarsofttool * -e gbk` |

> **提示**：中文版游戏用 `-e gbk`，日文版用默认 `shift_jis`。省略输出路径时自动推导到输入文件所在目录。

### 编译

**依赖：** CMake ≥ 3.10, GCC ≥ 9（C++17 + `<filesystem>`）, libiconv

```bash
cd LiarsoftTool
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j$(nproc)
sudo cp liarsofttool /usr/local/bin/   # 可选
```

### 命令行参数

| 参数 | 说明 |
|------|------|
| `-e, --encoding <enc>` | 文本编码，默认 `shift_jis`，可选 `gbk` |
| `-r, --reference <path>` | TXT→GSC 时所需的参考 GSC 文件 |
| `-o, --output <path>` | 显式指定输出路径 |
| `-h, --help` | 显示帮助 |

支持多个输入文件及 shell 通配符：`liarsofttool *.wcg`、`liarsofttool * -e gbk`。
两个参数时若扩展名不同则视为 `输入 输出`（向后兼容）。

### 支持格式

| 格式 | 扩展名 | 操作 | 说明 |
|------|--------|------|------|
| XFL | `.xfl` | 解包/打包 | 通用资源封包，Magic: `LB\x01\x00` |
| LWG | `.lwg` | 解包/打包 | 场景合成封包，Magic: `LG\x01\x00`，含图层 X/Y/Flag |
| GSC | `.gsc` | 提取/注回 | 游戏脚本，小端序，9 字段头+命令段+字符串段 |
| WCG | `.wcg` | ↔ PNG | 32-bit BGRA，两次 CG 解压/压缩（有损） |
| LIM | `.lim` | → PNG | 32-bit 四通道 或 16-bit BGR565+Alpha |
| WAV | `.wav` | → OGG | 偏移 66 处嵌入 Ogg Vorbis |

GSC 文本格式：`#` 标记原文，`>` 标记译文，支持 `\t`（全角空格）和多行。

### 典型工作流

```bash
# --- 汉化流程 ---
liarsofttool -e gbk data.xfl unpacked/
liarsofttool -e gbk unpacked/0010.gsc              # → 0010.txt
# 翻译 0010.txt …
liarsofttool -e gbk -r unpacked/0010.gsc 0010.txt  # → 0010.gsc
cp 0010.gsc unpacked/0010.gsc
liarsofttool -e gbk unpacked/                      # → unpacked.xfl

# --- 场景编辑 ---
liarsofttool cgview.lwg cgview/                    # 解包+生成 .meta.xml
liarsofttool cgview/bg.wcg                         # → bg.png
# 编辑 bg.png …
liarsofttool cgview/bg.png                         # → bg.wcg
liarsofttool cgview/                               # → cgview.lwg
```

### 已知限制

- **OGG → WAV**：尚未实现 Ogg 嵌入 WAV 的反向操作。
- **多级目录**：XFL/LWG 中文件均扁平存放。

---

## English

### Quick Reference

| Task | Command |
|------|---------|
| Extract script strings | `liarsofttool -e gbk scenario.gsc` |
| Inject translation | `liarsofttool -e gbk -r original.gsc trans.txt` |
| Unpack resource archive | `liarsofttool -e shift_jis archive.xfl` |
| Unpack scene archive | `liarsofttool cgview.lwg` |
| WCG to PNG | `liarsofttool image.wcg` |
| LIM to PNG | `liarsofttool image.lim` |
| PNG to WCG | `liarsofttool image.png` |
| Extract embedded OGG | `liarsofttool audio.wav` |
| Pack directory → XFL | `liarsofttool -e shift_jis ./dir` |
| Pack directory → LWG | `liarsofttool -e shift_jis ./dir_with_meta` |
| Batch convert | `liarsofttool *.png` or `liarsofttool * -e gbk` |

> **Tip:** Use `-e gbk` for Chinese releases, default `shift_jis` for Japanese.
> Output paths default to the input file's directory when omitted.

### Build

**Requirements:** CMake ≥ 3.10, GCC ≥ 9 (C++17 + `<filesystem>`), libiconv

```bash
cd LiarsoftTool
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j$(nproc)
sudo cp liarsofttool /usr/local/bin/   # optional
```

### CLI Options

| Option | Description |
|--------|-------------|
| `-e, --encoding <enc>` | Text encoding: `shift_jis` (default) or `gbk` |
| `-r, --reference <path>` | Reference GSC for TXT→GSC injection |
| `-o, --output <path>` | Explicit output path |
| `-h, --help` | Show help |

Multiple inputs and shell wildcards are supported: `liarsofttool *.wcg`, `liarsofttool * -e gbk`.
When exactly two args have different extensions, the second is treated as output (backward compat).

### Supported Formats

| Format | Extension | Operation | Notes |
|--------|-----------|-----------|-------|
| XFL | `.xfl` | unpack/pack | Resource archive, Magic: `LB\x01\x00` |
| LWG | `.lwg` | unpack/pack | Scene composition, Magic: `LG\x01\x00`, with layer X/Y/Flag |
| GSC | `.gsc` | extract/inject | Game script, LE binary, 9-field header + command + string sections |
| WCG | `.wcg` | ↔ PNG | 32-bit BGRA, dual-pass CG compress/decompress (lossy) |
| LIM | `.lim` | → PNG | 32-bit 4-channel or 16-bit BGR565+Alpha |
| WAV | `.wav` | → OGG | Embedded Ogg Vorbis at offset 66 |

GSC text format: `#` prefix for original, `>` for translation. Supports `\t` and multi-line.

### Typical Workflows

```bash
# --- Translation ---
liarsofttool -e gbk data.xfl unpacked/
liarsofttool -e gbk unpacked/0010.gsc              # → 0010.txt
# translate 0010.txt …
liarsofttool -e gbk -r unpacked/0010.gsc 0010.txt  # → 0010.gsc
cp 0010.gsc unpacked/0010.gsc
liarsofttool -e gbk unpacked/                      # → unpacked.xfl

# --- Scene editing ---
liarsofttool cgview.lwg cgview/                    # unpack + generate .meta.xml
liarsofttool cgview/bg.wcg                         # → bg.png
# edit bg.png …
liarsofttool cgview/bg.png                         # → bg.wcg
liarsofttool cgview/                               # → cgview.lwg
```

### Known Limitations

- **OGG → WAV**: reverse embedding not yet implemented.
- **Subdirectories**: all files in XFL/LWG archives are flat (no nesting).

---

## License

GNU General Public License v3.0 (inherited from arc_unpacker's CG decompression code).

Third-party code:
- [stb_image](https://github.com/nothings/stb) (public domain) — PNG read/write
- CG decompression algorithm from [arc_unpacker](https://github.com/vn-tools/arc_unpacker) (GPLv3)

