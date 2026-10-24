# RScript Runtime

[中文](README.zh.md) | [English](README.en.md) | [Русский](README.ru.md)

[逆向依据](ENGINE_AUDIT.zh.md) · [DLL 开发说明](SDK.zh.md)

实验性的 Windows DLL 扩展，用原游戏运行散装资源和 UTF-8 脚本。
不重新实现游戏，不修改磁盘上的 EXE，不要求替换官方 `RsComp.dll`。
目前提供 32 位加载器及 Forest 适配，不能直接用于所有 RScript 游戏。

## 开始使用

把构建生成的 `RScriptLauncher.exe`、`RScriptHost.dll` 和功能 DLL 放在同一目录，
无需放进游戏目录。打开加载器后：

1. 在 **Game executable** 选择游戏 EXE。
2. 在 **Override resources** 选择覆盖资源目录；留空则使用游戏目录。
3. 勾选所需 DLL，点击 **Start game**。

请先用游戏副本测试；即使扩展不改写资源，游戏本身仍可能写入存档。

| DLL | 用途 |
| --- | --- |
| `RScriptForest.dll` | 接入已经验证的 Forest 引擎版本 |
| `RScriptResources.dll` | 优先加载散装资源及当前格式的 TSC |
| `RScriptUnicode.dll` | 在 UTF-8 覆盖脚本中使用原 CP932 之外的文字 |

宿主 `RScriptHost.dll` 由加载器按需加载，不单独勾选。
勾选 Resources 或 Unicode 会自动勾选 Forest；取消 Forest 也会取消这两项。
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

## Forest 与 UTF-8

内置适配仅接受以下 `Forest.exe` 的 SHA-256；其他版本会拒绝加载，不能绕过检查使用。

```text
8eb71d07304d362ec26cc47d93c6ef5854b5acadd7c9fe941755ad50820a3e56
```

使用 UTF-8 译文时同时启用 Resources、Unicode 和 Forest。原 GSC 仍按 CP932 处理。
只启用 Resources 时，TSC 中的文字仍须能用 CP932 表示。

当前 Unicode 适配支持基本多文种平面内的文字，例如常用中、日、俄文；
不支持 emoji 等补充平面字符，也不提供所有复杂文字的排版。
原 CP932 之外的不同字符最多占用 1880 个字形映射位，原游戏已使用的映射位会减少可用数量。
仍需要安装能显示这些文字的字体；资源能加载不代表字体一定有相应字形。

字形字典保存于：

```text
%LOCALAPPDATA%\LiarsoftTool\RScriptRuntime\<key>\glyphs.bin
```

保留旧存档或历史记录时，不要删除字典。迁移存档时一并备份字典，并保持游戏与覆盖目录路径不变；
更换路径会使用另一份字典。修改剧情后，旧存档仍可能不兼容。
同一字典不能同时供多个游戏实例使用。

错误详情见 `%LOCALAPPDATA%\LiarsoftTool\RScriptRuntime\host-<pid>.log`。
日志和字典写入用户数据目录，不写进原游戏资源。

## 构建与验证

在仓库根目录单独构建运行库，不需要 GTK 或 libcurl。
准备 32 位 Windows MinGW 工具链，以及同一目标的静态 WebP、Vorbis/Ogg 库：

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
