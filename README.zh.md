# LiarsoftTool

[中文](README.zh.md) | [English](README.en.md) | [Русский](README.ru.md) | [日本語](README.ja.md)

![LiarsoftTool](image.png)

LiarsoftTool 用于处理 CodeX RScript 引擎游戏的资源，支持封包解包、图像转换、
脚本提取与回写、音频转换，适用于 Liar-soft、raiL-soft 等社团的作品。
当前版本不提供游戏 EXE 转换。

若要将游戏移植到 Ren'Py，请配合
[rscript2renpy](https://github.com/Love-in-idleness/rscript2renpy) 使用。

## 开始使用

Windows 分别提供 GUI、CLI 两个压缩单文件 EXE，下载所需程序即可运行，无需解压。
Linux 分别提供 GUI、CLI 独立二进制文件，下载后赋予执行权限即可运行，无需解压。
GUI 内置语言文件，仍需安装下文列出的系统运行库。
从源码构建时，GUI 程序为 `liarsofttool-gui`，命令行程序为 `liarsofttool`。

先复制一份游戏资源再操作。生成文件默认写在输入文件旁，重建可能覆盖同名文件。

图形界面的操作顺序：

1. 添加文件或文件夹，也可以直接拖入窗口。
2. 资源编码默认为 `auto`，自动检测 GSC；有歧义或文字不正确时手动选择，日文通常用 CP932，中文补丁通常用 GBK。
3. 按需设置递归、仅封包或仅解包，检查“转换任务”中的输出路径。
4. 开始转换。遇到问题时，用 “Copy details” 复制完整提示。

GUI 和 CLI 默认都将 GSC 转成 TSC。只需要提取文本时，GUI 勾选 **GSC→TXT**。

自动检测也用于 GSC 预览。短文本可能无法判断，纯 ASCII 无法区分原编码；请检查转换结果。
封包、检查或生成 GSC 时必须选择具体编码。`auto` 下，归档文件名和其他旧式文本仍按 CP932 读取。

点击 **Check for updates** 可手动检查新版并打开 GitHub 发布页。
程序不会在启动时联网，也不会自动下载或安装更新。

## 资源浏览

在“资源浏览”打开文件夹、XFL 或 LWG，双击条目进入子目录或嵌套归档。
选中条目可预览图片、音频、文本，或将 GSC 只读反编译为 TSC。
文本显示不正确时切换编码；TSC 始终按 UTF-8 读取。
不支持或损坏的文件仍可查看分页十六进制字节。

浏览和预览不解包、不修改原文件。“导出原始字节”只导出当前条目，
不会转换格式。归档中的条目要转换时，先导出，再加入“转换任务”。
独立文件可直接加入转换任务。

图片支持缩放和透明背景。音频可播放、暂停、拖动进度和调节音量。
音频预览需要 GTK 媒体后端及 GStreamer 的 Ogg/Vorbis 插件；Windows GUI 已内置。

## 命令行帮助

支持格式、参数和转换示例请在软件中查看：

```bash
liarsofttool -h
```

## 脚本翻译

只改对话时可使用 TXT：在 `>` 后填写译文，再以原 GSC 为参考写回。
需要编辑完整脚本时使用 TSC，修改其中的命令和文字后重新编译。
TSC 用 UTF-8 保存，生成 GSC 时选择与目标游戏匹配的编码。
可选编码包括 CP932 / Windows-31J、GBK、GB18030、Big5 / CP950、
韩文 CP949、Windows CP1250–CP1258 和 UTF-8；转换与资源预览使用同一份编码列表。

GSC→TSC 是实验性功能，不是所有游戏都已适配。编辑前保留原 GSC，
不要删除 TSC 中自动生成的标签和元数据。
命令含义见 [TSC 指令参考](docs/GSC_OPCODE_REFERENCE.zh.md)。

## 从源码编译

需要 C++17 编译器、CMake、WebP、Vorbis/Ogg、uchardet（含静态库）、libcurl 7.85 及以上；图形界面还需要 GTK4 4.10、libadwaita 1.5 及以上和 gettext。
Windows 和 Linux 使用同一套 GUI。首选项集中设置转换选项、主题和界面语言。
主题可跟随系统或使用浅色、深色；界面支持中、英、俄、日语，手动选择后重启生效。

Debian / Ubuntu：

```bash
sudo apt install build-essential cmake pkg-config libwebp-dev libvorbis-dev libogg-dev libuchardet-dev libcurl4-openssl-dev libgtk-4-dev libadwaita-1-dev gettext gstreamer1.0-plugins-base gstreamer1.0-plugins-good
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j2
```

程序位于 `build/`。只构建命令行时，配置阶段添加 `-DBUILD_GUI=OFF`。
Windows 编译和打包见 [构建说明](docs/BUILDING.zh.md)。

## 更多说明

[VS Code TSC 插件](tsc-vscode/README.zh.md)提供高亮、检查和编译。
插件只在 `codex/tsc-vscode` 分支维护，可在本地构建 VSIX，不发布到扩展市场。

[文档目录](docs/README.zh.md) · [TSC 指令参考](docs/GSC_OPCODE_REFERENCE.zh.md)
· [开发规划](docs/ROADMAP.zh.md)

请使用合法持有的游戏资源。脚本或图像能成功转换，不代表所有引擎版本
都能直接使用，替换回游戏后请自行测试。

## 许可与致谢

LiarsoftTool 采用 GPLv3。第三方组件保留各自许可证。

- [RaiLTools](https://github.com/EusthEnoptEron/RaiLTools)：归档与脚本格式研究。
- [arc_unpacker](https://github.com/vn-tools/arc_unpacker)：GPLv3 图像解压代码。
- [GARbro](https://github.com/crskycode/GARbro)：WCG 编码参考。
- [stb](https://github.com/nothings/stb)：图像读写。
- [WebP](https://chromium.googlesource.com/webm/libwebp/)：无损 WebP。
- [Ogg / Vorbis](https://xiph.org/vorbis/)：音频；[许可证原文](docs/THIRD_PARTY_AUDIO_LICENSES.md)。
- [GTK4 / libadwaita](https://www.gtk.org/)：共用桌面界面。
- [GStreamer](https://gstreamer.freedesktop.org/)：GTK 媒体后端音频预览。
- [libcurl](https://curl.se/libcurl/)：HTTPS 更新检查。
- [uchardet](https://www.freedesktop.org/wiki/Software/uchardet/)：GSC 编码检测；[许可证原文](docs/THIRD_PARTY_UCHARDET_LICENSES.md)。静态链接的 0.0.8 [源码](https://deb.debian.org/debian/pool/main/u/uchardet/uchardet_0.0.8.orig.tar.xz)未修改，按 GPLv2 或更新版本使用。
