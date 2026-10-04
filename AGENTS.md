# 项目工作约定

## 开始工作与本机配置

- 使用 `rg` 搜索源码，先检查 `git status --short`，保留已有修改及未跟踪资源。
- 阅读 `README.md` 与 `CMakeLists.txt`；GSC/TSC 改动参考 `docs/GSC_OPCODE_REFERENCE.md`，音频改动参考 `docs/AUDIO_PROCESSING_AUDIT.md`。
- 两仓库本机路径集中在 `/home/idleness/Source/rscript2renpy/local.paths.toml`，不另维护一份。格式及读取方式见该仓库的 `local.paths.example.toml`、`docs/LOCAL_WORKFLOW.md`。先检查路径存在与用途，其他机器按其实际 checkout 定位配置。
- 本项目的 `.codex/config.toml` 保存跨仓库和 Ren'Py 输出目录权限。文件保存不代表当前聊天已加载新权限，以实际权限上下文为准。

## 实现与资源边界

- 本工具负责 XFL/LWG、GSC/TSC、WCG/LIM、WAV/OGG 和 EXE 处理；Ren'Py lowering、运行时与移植界面放在 rscript2renpy。
- 公共转换逻辑放在 `src/`、`include/`；CLI、GTK 和 Win32 GUI 通过公共逻辑保持行为一致，避免在不同入口重复修复。
- 原版游戏与补丁保留只读副本；转换、递归处理及往返测试使用指定工作副本或临时目录，确认输出位置，避免默认同名输出改写原件。
- 保留 `test/`、`CodeXRScript/`、`res_ft/`、`ubai/` 等本机资源，不清理或提交它们。测试产物只写入构建目录或临时目录。
- 不凭空补全 opcode、DLL 或二进制语义；记录文件哈希、布局、地址和调用证据，明确标注未确认部分。静态分析不等于引擎运行验证。
- 维持现有编码、元数据和失败处理约定；错误应明确报告，不能悄悄替换无法编码的文本或把失败项的旧产物收入封包。

## 构建与验证

- 使用本机配置的 `liarsofttool_build`；已有构建目录先检查 `CMakeCache.txt` 的源码目录与生成器，不擅自切换生成器。
- 首次配置：`cmake -S <源码目录> -B <构建目录> -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON`。
- 代码修改后：`cmake --build <构建目录> -j2`、`ctest --test-dir <构建目录> --output-on-failure`、`git diff --check`。
- 单项调试可用 `ctest --test-dir <构建目录> -R <测试名> --output-on-failure`。当前测试名：`encoding`、`wav_ogg`、`exe_patch`、`gsc_decompiler`、`gui_common`、`archive`、`cg_decompress`、`directory_packing`。
- 公共转换改动检查 CLI 和可用的 GUI 目标。Windows/交叉编译仅在任务涉及该平台或用户要求时执行；编译成功不等于 GUI 交互或原引擎行为已验证。
- 纯文档/路径配置改动只检查文档、路径、配置格式和 diff，不要求重建全部目标。
- GSC/TSC 输出格式变化必须检查 rscript2renpy 的解析与生成入口，使用工作副本完成转换，再运行相关生成器测试和 Ren'Py lint；两仓库均修复及验证后再完成任务。
- 原游戏画面、音频、路线及实际游玩由用户验证，分开报告自动测试与实际运行证据，注明未运行检查和既有失败。

## Git 与权限

- 每项独立任务实现并验证后及时本地提交，只暂存任务文件，不混入已有修改。
- 未明确要求不推送，不创建 tag、发布包或 GitHub Release；若用户明确要求发布，再执行构建、打包与远端核验。
- 新分支默认使用 `codex/` 前缀，优先复用合适的 checkout。
- 管理员权限操作先说明目的及具体命令并获得授权，不绕过目录权限。使用构建目录中的工具，不因 README 有安装示例而自动执行 `sudo` 或全局安装。
