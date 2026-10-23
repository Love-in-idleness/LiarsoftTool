# 开发规划 · Roadmap

[主说明](../README.md) · [Русский](ROADMAP.ru.md)

## 本轮顺序

1. 配合 rscript2renpy 完成 SevenBridge 日文移植所需的转换修复。
2. GUI 改用 C++ 调用 GTK4/libadwaita C API。先验证 Linux、Windows 最小窗口和打包，
   再替换旧界面；提供系统／浅色／深色主题与英语、俄语、简体中文。
3. 增加不预先解包的资源浏览：普通目录、XFL、LWG 和嵌套归档，按需读取，
   在内存中预览图像、音频、文本、GSC/TSC 和原始字节。
4. 在独立的 `codex/tsc-vscode` 分支开发 VS Code 插件：高亮、检查与调用本机编译器。
   检查复用 C++ 核心，提供只检查、不输出 GSC 的命令行接口。

界面保留公共转换入口、现有选项和默认值。资源预览不修改输入，导出和转换
必须由使用者触发。新功能和使用说明同步提供俄语，不自动推送、发布或提高版本。
本页是规划，不是已实现功能清单。

## 长期规划：图形化 TSC 编辑器

未来按对白、资源、条件、选项和流程等语义提供创作界面，复用 LiarsoftTool
的检查和编译能力。具体交互和支持范围另行确定，本轮不实现、不预建编辑器框架。

实验性 `--pure-renpy` 属于 rscript2renpy 的长期目标，见
[对应规划](https://github.com/Love-in-idleness/rscript2renpy/blob/main/docs/ROADMAP.md)。
本轮不添加该开关，也不实现原生 Ren'Py 生成后端。

## English

Work proceeds from SevenBridge conversion fixes to a shared GTK4/libadwaita GUI,
read-only resource browsing and a VS Code TSC extension. The GUI keeps the C++ core,
offers system/light/dark themes and English/Russian/Simplified Chinese, and must
pass Linux and Windows window/package checks before replacing the old frontend.

Previews read directory, XFL, LWG and nested entries on demand without modifying
inputs. The VS Code extension lives on `codex/tsc-vscode`, uses the installed
compiler and shares its checker; it is not automatically merged or published.

The semantic graphical TSC editor is a long-term goal only. Its interaction and
supported scope remain undecided. The experimental `--pure-renpy` backend belongs
to rscript2renpy and is also deferred. This phase adds neither implementation nor
placeholder flags for these two goals. This page describes plans, not shipped features.
