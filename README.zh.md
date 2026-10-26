# LiarsoftTool

[中文](README.zh.md) | [English](README.en.md) | [Русский](README.ru.md) | [日本語](README.ja.md)

![LiarsoftTool](image.png)

CodeX RScript 游戏资源工具，支持归档封包与解包、图像和音频转换，以及 GSC 脚本的提取、编辑和回写。
适用于 Liar-soft、raiL-soft 等作品；不同游戏的支持范围可能不同。当前不提供游戏 EXE 转换。

需要生成 Ren'Py 工程时，使用独立项目 [rscript2renpy](https://github.com/Love-in-idleness/rscript2renpy)。

## 安装与开始使用

从 [Releases](https://github.com/Love-in-idleness/LiarsoftTool/releases) 下载适合系统的程序。
GUI 与 CLI 分开提供：Windows 为压缩单文件 EXE，Linux 为独立二进制文件。
Linux 下载后需授予执行权限；GUI 需要 GTK4 4.10+、libadwaita 1.5+，
音频预览还需要 GTK 媒体后端及 GStreamer Ogg/Vorbis 插件，Windows GUI 已包含媒体运行库。
源码构建见[构建指南](docs/BUILDING.zh.md)。

操作前复制游戏文件。默认输出在输入旁边，重建时可能覆盖同名文件。

1. 在“转换任务”中添加文件、文件夹，或直接拖入窗口。
2. 检查编码和输出位置，按需启用递归、仅封包或仅解包。
3. 开始转换；遇到问题时复制完整错误详情。

GUI 与 CLI 默认都将 GSC 导出为 TSC；只需对话文本时，在 GUI 选择 **GSC→TXT**。
资源编码默认 `auto`，自动检测 GSC；短文本或混合编码可能判断不准，必要时手动指定。
日文通常为 CP932，中文补丁通常为 GBK。编译 GSC、封包及编码检查须选择具体编码；
`auto` 不检测归档文件名和其他旧式文本，它们仍使用 CP932。

## 浏览与预览

“资源浏览”可以打开文件夹、XFL、LWG 和嵌套归档，无需先解包。
选中条目可查看图片、播放音频、阅读文本、将 GSC 只读显示为 TSC，或查看分页十六进制字节。
图片支持缩放与透明背景，音频支持暂停、进度和音量控制。文本乱码时切换编码；TSC 固定使用 UTF-8。

浏览不修改输入。“导出原始字节”不转换格式；归档条目需要转换时，先导出再加入转换任务。

首选项可设置转换选项、跟随系统／浅色／深色主题和界面语言。
界面支持中文、英文、俄文、日文，默认跟随系统，手动换语言后重启生效。
更新检查由用户手动触发，只打开发布页，不自动下载或安装。

## 编辑脚本

- TXT：在 `>` 后填写译文，以原 GSC 为参考写回。
- TSC：编辑完整命令与文字，再编译为目标游戏使用的编码。TSC 文件本身保存为 UTF-8。

GSC→TSC 为实验性功能。保留原 GSC 和自动生成的标签、元数据，
不要把转换成功等同于所有引擎版本兼容。指令含义见 [TSC 参考](docs/GSC_OPCODE_REFERENCE.zh.md)。

## 帮助与文档

命令参数、支持格式和转换示例以软件内帮助为准：

```bash
liarsofttool -h
```

[文档目录](docs/README.zh.md) · [构建指南](docs/BUILDING.zh.md) · [开发规划](docs/ROADMAP.zh.md)

反馈请附程序版本、输入类型、所选编码与完整错误详情。只使用合法持有的资源，不公开上传游戏文件。

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
