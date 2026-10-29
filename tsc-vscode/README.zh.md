# CodeX TSC

[中文](README.zh.md) | [English](README.en.md) | [Русский](README.ru.md) | [日本語](README.ja.md)

为 VS Code 中的 TSC 文件提供语法高亮、错误检查和 GSC 编译。

## 安装

1. 安装 VS Code 1.74 或更新版，以及本分支的 LiarsoftTool。
2. 在扩展菜单选择“从 VSIX 安装”，打开 VSIX 文件。
3. 在设置中搜索 `liarsofttool.path`，填写 LiarsoftTool 命令行程序的完整路径：Windows 为 `liarsofttool.exe`，Linux 为 `liarsofttool`。

## 使用

打开 TSC 文件即可编辑。保存后自动检查，结果显示在“问题”面板。
命令面板提供 **CodeX TSC: 检查已保存文件**和 **CodeX TSC: 将已保存文件编译为 GSC**。
手动检查或编译前，未保存的修改会提示先保存。工具错误详情在“输出 → LiarsoftTool TSC”。

在设置中搜索 `liarsofttool`：

- `path`：LiarsoftTool 命令行程序的路径。
- `encoding`：目标游戏使用的 GSC 文字编码，默认 CP932。TSC 文件本身保存为 UTF-8。
- `outputDirectory`：GSC 保存目录，相对路径以 TSC 所在文件夹为起点。留空则保存在同一文件夹，可能覆盖已有同名 GSC。

检查和编译需要信任工作区。远程工作区中的 LiarsoftTool 应安装在远程端。
界面跟随 VS Code 的中文、日语、英语或俄语设置，其他语言使用英语。

## 本地构建

需要 Node.js 22 或更新版，以及已构建的本分支 LiarsoftTool：

```bash
cd tsc-vscode
npm ci
npm test
npm run package -- --out liarsofttool-tsc.vsix
```

测试可用 `LIARSOFTTOOL_PATH` 指定编译器。

[TSC 指令参考](../docs/GSC_OPCODE_REFERENCE.zh.md)

插件按 GPLv3 分发。仓库不提供游戏资源。
