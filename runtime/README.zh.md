# RScript Runtime

[中文](README.zh.md) | [English](README.en.md) | [Русский](README.ru.md) | [日本語](README.ja.md)

[逆向依据](ENGINE_AUDIT.zh.md) · [DLL 开发说明](SDK.zh.md)

实验性的 Windows DLL 扩展，用原游戏运行散装资源和 UTF-8 脚本。
不重新实现游戏，不修改磁盘上的 EXE，不要求替换官方 `RsComp.dll`。
面向使用相同文件格式和 Windows 接口的 CodeX RScript 游戏，不使用游戏名、EXE 哈希白名单或固定地址。
目前构建和游戏检查使用 32 位 Windows 版本；Forest、SevenBridge 是回归样本，不是准入名单。

## 开始使用

把构建生成的 `RScriptLauncher.exe`、`RScriptHost.dll`、`kinsoku.json` 和功能 DLL 放在同一目录，
无需放进游戏目录。打开加载器后：

1. 在 **Game executable** 选择游戏 EXE。
2. 在 **Override resources** 选择覆盖资源目录；留空则使用游戏目录。
3. 勾选所需 DLL，点击 **Start game**。

请先用游戏副本测试；即使扩展不改写资源，游戏本身仍可能写入存档。

| DLL | 用途 |
| --- | --- |
| `RScriptFiles.dll` | 按接口名称接入游戏的同步文件读取 |
| `RScriptResources.dll` | 优先加载散装资源及当前格式的 TSC |
| `RScriptUnicode.dll` | 在 UTF-8 覆盖脚本中使用原 CP932 之外的文字 |

宿主 `RScriptHost.dll` 由加载器按需加载，不单独勾选。
勾选 Resources 或 Unicode 会自动勾选 Files；取消 Files 也会取消这两项。
全部取消时直接运行原游戏，不注入 DLL。开关在下次启动游戏时生效。

**Add DLL...** 可以添加额外 DLL，新增项默认关闭。仅加载可信文件：
DLL 可以读取或修改整个游戏进程，加载器不提供沙箱。
接入方式见 [DLL 开发说明](SDK.zh.md)。当前没有附带 Wiki DLL，也不支持运行中卸载 DLL。

## 覆盖资源

覆盖目录沿用原游戏的目录结构，例如 `scr/2100.tsc`、`grpe/9001.webp`、
`voice/0001.ogg`、`grps/tbox01/.meta.xml`。不需要把整套游戏复制到覆盖目录。

同名资源按以下顺序选择：

| 类型 | 优先级 |
| --- | --- |
| 音频 | `ogg > wav` |
| 脚本 | `tsc > gsc` |
| 图片 | `webp > png > lim > wcg` |
| 图片组件 | 含 `.meta.xml` 的组件目录 `> lwg` |

`.meta.xml` 必须与其引用的组件图片一起保留，用于确定位置、尺寸和条目名称。
覆盖文件不存在时使用原游戏的加载路径；已选中的覆盖文件损坏或缺少必要内容时，
报告错误，不悄悄改用低优先级文件。

资源按需在内存中转换，原始资源不被改写。
TSC 需要由当前 LiarsoftTool 导出并以 UTF-8 保存；保留生成的标签和元数据。
这里不支持官方编译器的 include、宏等高层源文件。

TSC 保留自身的容器格式。能找到原 GSC 时，会核对散装文件或 XFL 条目的头格式，
不一致时报错；新增场景找不到参考时，不替用户猜测目标格式。

## UTF-8

使用 UTF-8 译文时同时启用 Resources、Unicode 和 Files。原 GSC 仍按 CP932 处理。
只启用 Resources 时，TSC 中的文字仍须能用 CP932 表示。

当前 Unicode 适配支持基本多文种平面内的文字，例如常用中、日、俄文；
不支持 emoji 等补充平面字符，也不提供所有复杂文字的排版。
原 CP932 之外的不同字符最多占用 1880 个字形映射位，原游戏已使用的映射位会减少可用数量。
仍需要安装能显示这些文字的字体；资源能加载不代表字体一定有相应字形。

### 行首与行末禁则

启用 Unicode 时，启动器读取自身目录中的 [kinsoku.json](kinsoku.json)。
`no_line_start` 是不能放在行首的字符，例如 `。）」`；`no_line_end` 是不能放在行末的字符，例如 `（「`。
随附规则覆盖常用中日标点、小假名、ASCII 标点及方向引号，可按译文需要修改。
两组不能包含同一个字符；文件须为 UTF-8，最大 64 KiB，目前只接受基本多文种平面字符。

命令行可用 `--kinsoku "自定义规则.json"` 指定另一份文件；该参数指定的文件始终会被检查。
文件缺失或内容无效时不会启动游戏。未启用 Unicode 且未指定此参数时，不要求提供禁则文件。
规则接入原引擎的换行判断，不给剧情文本插入换行，也不是完整的 Unicode UAX #14 排版实现。

字形字典保存于：

```text
%LOCALAPPDATA%\LiarsoftTool\RScriptRuntime\<key>\glyphs.bin
```

保留旧存档或历史记录时，不要删除字典。迁移存档时一并备份字典，并保持游戏与覆盖目录路径不变；
更换路径或 EXE 内容会使用另一份字典。修改剧情后，旧存档仍可能不兼容。
同一字典不能同时供多个游戏实例使用。

本版的通用 DLL 与早期 Forest 专用 DLL 不能混用；请使用同一次构建的完整文件组。
旧试验版的字形字典不自动迁移。

错误详情见 `%LOCALAPPDATA%\LiarsoftTool\RScriptRuntime\host-<pid>.log`。
日志和字典写入用户数据目录，不写进原游戏资源。

## 兼容范围

Files 接入 EXE 普通导入表中的 `CreateFileA`、`ReadFile`、`SetFilePointer`、
`GetFileSize`、`CloseHandle`。Unicode 使用 ANSI GDI 的字形、文本与测量接口，
并在游戏模块内固定 CP932 字符识别，不改变系统区域设置。
Unicode 禁则还须识别共享分类器的完整比较组，未识别时拒绝启用；32 位桥接要求处理器支持 SSE。

缺少必要接口、导入表损坏或已有钩子冲突会阻止加载，并给出错误。
目前不接管延迟导入、动态取得的函数、其他 DLL 自身的读取、文件枚举、映射或异步读取；
MCI 视频播放也不属于覆盖路径。使用其他读取或文字渲染方式的引擎仍可能需要补充公共支持，
不能仅凭 DLL 初始化成功就认为游戏完全兼容。

## 构建与验证

在仓库根目录单独构建运行库，不需要 GTK 或 libcurl。
需要 CMake 3.16+、32 位 Windows MinGW 工具链，以及同一目标的静态 WebP、Vorbis/Ogg 和 uchardet 库：

```bash
cmake -S runtime -B build/runtime-win32 \
  -DCMAKE_SYSTEM_NAME=Windows \
  -DCMAKE_C_COMPILER=i686-w64-mingw32-gcc-posix \
  -DCMAKE_CXX_COMPILER=i686-w64-mingw32-g++-posix \
  -DCMAKE_FIND_ROOT_PATH=/path/to/windows-x86-dependencies \
  -DCMAKE_FIND_ROOT_PATH_MODE_INCLUDE=ONLY \
  -DCMAKE_FIND_ROOT_PATH_MODE_LIBRARY=ONLY \
  -DCMAKE_FIND_ROOT_PATH_MODE_PROGRAM=NEVER \
  -DBUILD_TESTING=OFF
cmake --build build/runtime-win32 -j2
```

依赖与编译器须使用一致的架构和运行时，不要混用 Windows 与 Linux 库。
文件位于 `build/runtime-win32/`。额外日志示例为 `RScriptLogExample.dll`。

在现有主项目构建中添加 `-DBUILD_RUNTIME=ON` 可启用公共资源和 Unicode 测试。
Windows 测试还需要 Python 3 与 Vorbis 编码库；交叉编译时可指定 `RSCRIPT_WINE`。
只有 Windows 构建生成加载器和功能 DLL。

这是实验性功能。自动测试、Wine 检查与原生 Windows 完整游玩是不同的验证；
不保证所有场景、存档、字体或其他插件组合都兼容。游戏文件请自行合法取得。

许可与第三方组件说明见[主说明](../README.zh.md#许可与致谢)。静态链接的 uchardet 许可见[原文](../docs/THIRD_PARTY_UCHARDET_LICENSES.md)。
