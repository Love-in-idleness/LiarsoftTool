# CodeX TSC

[中文](#中文) · [English](README.en.md) · [Русский](README.ru.md)

## 中文

为 LiarsoftTool 当前 TSC 格式提供高亮、错误检查和 GSC 编译。
插件位于 `codex/tsc-vscode` 分支，尚未发布到扩展市场。

## 安装

1. 准备支持 `--check-tsc` 的 LiarsoftTool，本分支附带该实现。插件不会下载编译器。
2. 在 VS Code 的扩展菜单选择 **从 VSIX 安装**，打开本地生成的 VSIX。
3. 打开设置，搜索 `liarsofttool`，填写程序路径、游戏编码和可选输出目录。
   不在 PATH 中时，程序路径须指向实际可执行文件。

TSC 源码使用 UTF-8。编码选项指定目标 GSC 的编码，日文通常用 CP932。
信任工作区后，打开或保存 TSC 会自动检查，错误显示在“问题”面板。
修改但未保存时会清除旧诊断，手动检查或编译会先询问是否保存。

命令面板提供：

- **CodeX TSC: 检查已保存文件**
- **CodeX TSC: 将已保存文件编译为 GSC**

输出目录留空时，GSC 写在 TSC 旁。编译可能覆盖已有 GSC，请先备份。
调用失败时查看 **输出 → LiarsoftTool TSC**。受限模式只提供高亮，不运行工具。
使用远程工作区时，LiarsoftTool 也应安装在远程端。

当前每个文件报告首个编译错误，不提供语义补全或可视化编辑器。
Windows 旧转换入口仍受系统代码页的路径限制；若编译失败，请将脚本和输出移到
可用系统编码表示的路径。新的检查入口支持 Unicode 路径。

## 本地构建

需要 Node.js 22 或更新版本，先在此目录执行：

```bash
npm ci
npm test
npm run package -- --out ../build/liarsofttool-tsc-0.1.0.vsix
```

开发测试可用 `LIARSOFTTOOL_PATH` 指定本机编译器。
检查接口见 [开发说明](../docs/TSC_DIAGNOSTICS.md)，TSC 命令见
[指令参考](../docs/GSC_OPCODE_REFERENCE.md)。插件按仓库的 GPLv3 许可分发。
