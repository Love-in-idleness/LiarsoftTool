# 项目工作约定

[中文](AGENTS.zh.md) | [English](AGENTS.en.md) | [Русский](AGENTS.ru.md) | [日本語](AGENTS.ja.md)

## 开始工作

- 先检查 Git 状态，保留已有改动；使用 `rg` 搜索。
- 阅读 README、CMakeLists.txt 和相关模块；TSC 参考 `docs/GSC_OPCODE_REFERENCE.md`。
- 本机路径共用相邻 rscript2renpy 仓库的 `local.paths.toml`，不硬编码维护者路径。

## 实现边界

- 本仓库负责归档、GSC/TSC、编码、图像、音频和 EXE；Ren'Py 代码放在 rscript2renpy。
- CLI 和 Windows/Linux GTK4 GUI 调用公共转换模块，不另写一套转换路径。
- 未知格式和引擎行为从原文件与 EXE 验证，不猜 opcode 或像素算法。
- 原件只读，转换与往返测试使用副本或临时目录。
- 保留 `test/`、`CodeXRScript/`、`res_ft/`、`ubai/` 等本机资料，不清理或提交素材。
- 维持输入检查、严格编码、警告和失败产物排除规则，不能以简化代码为由移除保护。

## 文档写法

- README 面向用户，写用途、安装、GUI 操作和必要注意事项。
- CLI 常用命令、参数和转换示例只维护在软件内帮助中，README 引导运行 `liarsofttool -h`，不重复列举。
- 所有项目自写文档均提供完整、同步的 `.zh.md`、`.en.md`、`.ru.md`、`.ja.md`，原 `.md` 只作语言导航。
- 中文以用户的改写为准，英文、俄文、日文同步信息和结构；许可证和第三方原始声明保留原文。
- 不把解析器内部字段、旧数据迁移分支、错误处理实现、测试流水账和提交记录当使用说明。
- 仅保留会影响用户操作的提示，例如编码选择、备份、版本要求和未支持的功能。
- opcode 表和逆向依据放在开发参考，保留哈希、地址、参数表及未确认项。
- 检查参数、命令和本地链接。许可证原文和版权声明不得删改含义。

## 验证

- 使用已配置构建目录，先核对 CMakeCache 的源码路径和生成器，不切换既有生成器。
- 代码修改后执行 `cmake --build build -j2`、`ctest --test-dir build --output-on-failure`
  与 `git diff --check`；实际路径以本机配置为准。
- 公共转换改动检查 CLI 和可用的 GUI。涉及 Windows 时再进行交叉编译与 Wine 回归。
- GSC/TSC 或资源输出变化时同时检查 rscript2renpy 的消费、生成和 lint。
- 纯文档修改只检查文档、链接及参数，不要求重建或运行游戏。
- 自动测试、静态分析、游戏内验证分别报告，不将编译成功写成兼容性已验证。

## Git 与权限

- 独立任务验证后本地提交，只包含当前任务文件；用户要求暂不提交时照做。
- 未明确要求不推送、不创建 tag 或 Release、不提高版本号。
- 新分支使用 `codex/` 前缀，优先复用当前 checkout。
- 管理员操作先确认，不全局安装构建产物，不绕过目录权限。
