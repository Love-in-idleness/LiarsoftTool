# 开发规划

[中文](ROADMAP.zh.md) | [English](ROADMAP.en.md) | [Русский](ROADMAP.ru.md) | [日本語](ROADMAP.ja.md)

## 本轮顺序

1. 配合 rscript2renpy 完成 SevenBridge 日文移植所需的转换修复。
2. GUI 改用 C++ 调用 GTK4/libadwaita C API。先验证 Linux、Windows 最小窗口和打包，
   再替换旧界面；提供系统／浅色／深色主题与英语、俄语、简体中文、日语。
3. 增加不预先解包的资源浏览：普通目录、XFL、LWG 和嵌套归档，按需读取，
   在内存中预览图像、音频、文本、GSC/TSC 和原始字节。
4. 在独立的 `codex/tsc-vscode` 分支开发 VS Code 插件：高亮、检查与调用本机编译器。
   检查复用 C++ 核心，提供只检查、不输出 GSC 的命令行接口。

界面保留公共转换入口、现有选项和默认值。资源预览不修改输入，导出和转换
必须由使用者触发。使用说明同步维护中英俄日四语，不自动推送、发布或提高版本。
本页是规划，不是已实现功能清单。

## 图形化 TSC 编辑器：独立分支首版

按最新安排，`codex/tsc-vscode` 已提供段落／事件／连线创作、源码切换、静态资源预览和原引擎导出。
优先核对 SevenBridge 日文版，保留 Forest 配置；其他游戏暂不扩展。
这是待原游戏人工验收的首版，不是引擎模拟器，也未合并至主分支。见[使用说明](../tsc-vscode/README.zh.md)。

实验性 `--pure-renpy` 属于 rscript2renpy 的长期目标，见
[对应规划](https://github.com/Love-in-idleness/rscript2renpy/blob/main/docs/ROADMAP.md)。
本轮不添加该开关，也不实现原生 Ren'Py 生成后端。
