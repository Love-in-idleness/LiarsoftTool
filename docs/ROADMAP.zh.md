# 开发规划与实验分支

[中文](ROADMAP.zh.md) | [English](ROADMAP.en.md) | [Русский](ROADMAP.ru.md) | [日本語](ROADMAP.ja.md)

[主说明](../README.zh.md)

## 主程序

主程序提供资源转换、GTK4/libadwaita 图形界面，以及无需预先解包的资源浏览与预览。
后续工作重点是格式兼容性、跨平台使用体验和编辑工具所需的公共接口。
当前功能用法见主说明，不以本页替代操作指南。

## 实验分支

| 分支 | 内容 |
| --- | --- |
| `codex/tsc-vscode` | [VS Code 插件与图形化 TSC 创作](https://github.com/Love-in-idleness/LiarsoftTool/blob/codex/tsc-vscode/tsc-vscode/README.zh.md)：高亮、检查、编译、剧情编辑；适配 SevenBridge 与 Forest 配置 |
| `codex/rscript-runtime` | [Windows 运行时加载器与可选 DLL](https://github.com/Love-in-idleness/LiarsoftTool/blob/codex/rscript-runtime/runtime/README.zh.md)：优先加载散装资源及实验性 UTF-8 支持 |

这些分支未合入主程序，不保证与主分支的全部功能和资源格式同步。
使用前阅读对应分支说明；兼容性以目标游戏实际测试为准。

## 长期目标

继续完善基于语义的 TSC 创作与资源管理。完整引擎模拟、通用游戏运行器不属于现有编辑器功能。

原生 Ren'Py 生成后端 `--pure-renpy` 由独立的
[rscript2renpy 规划](https://github.com/Love-in-idleness/rscript2renpy/blob/main/docs/ROADMAP.zh.md)维护，尚未实现。
