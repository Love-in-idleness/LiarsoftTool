# RScript Runtime 逆向依据

[中文](ENGINE_AUDIT.zh.md) | [English](ENGINE_AUDIT.en.md) | [Русский](ENGINE_AUDIT.ru.md)

[使用说明](README.zh.md) · [DLL 接口](SDK.zh.md)

## 范围

文件与 GDI 接口按 PE 导入名称接入；禁则识别完整的共享字符分类比较组。
两者都不绑定游戏名、哈希白名单或固定地址。
Forest 与 SevenBridge 用于验证公共文件及文字路径。以下地址只记录分析证据，
不是运行时选择条件；VA 按首选加载基址解释，导入表位置另外标为 RVA。
没有把私有 DLL 的代码或游戏资源加入仓库。

## 检查的文件

| 文件 | SHA-256 |
| --- | --- |
| Forest `Forest.exe` | `8eb71d07304d362ec26cc47d93c6ef5854b5acadd7c9fe941755ad50820a3e56` |
| SevenBridge `SBridge_jp.exe` | `c7d5fc38cf0c354a61d056b607cf1367b2529557e9e0407d0b0fdf12771b35e3` |
| CodeXRScript／OC 补丁 `RsComp.dll` | `6c65c3545a559b6e4a01d54fdb2f57ad4e605b27dc73959cde18bd3468f3cb30` |
| UBAI `RsComp.dll` | `0b1c3cdfc1587f576c30c09b3faa329e88e18fdaddfc0655d1ad398c1654a6a9` |
| Albatross `rsinit.dll` | `43499ada89a18735707c0e33d9a9bb585022e8e1b9b93e562a5e41bbb8340eb6` |

两份 RsComp 都是 32 位 PE，导出 `Compile`，入口 RVA 分别为 `0x124e` 和 `0x7090`。
Forest 在 `0x412eb0` 加载编译器，`0x4135f0` 包装四参数调用；
编译结果交给引擎对象，不是“输入文件名、输出文件名”的独立编译接口。
它们不能单独接管图像、音频或 Unicode 绘制。
因此新实现采用独立宿主与可选 DLL，不伪装成与所有游戏兼容的 `RsComp.dll`。

## Forest 文件路径

| 位置 | 观察及实现约束 |
| --- | --- |
| `0x4247c0` | 先通过 CFile 尝试散装文件，失败后走归档路径 |
| `0x42a970` | CFile 调用 `CreateFileA`，只读、`OPEN_EXISTING` |
| `0x42ab20`／`0x42ab80`／`0x42abe0` | CFile 的读取、定位和长度接口 |
| `0x413200` | 以 `%s\\%04d.gsc` 打开脚本，读取 28 字节头及后续区段 |
| `0x428260`／`0x4288d0` | 分别形成 LIM／WCG 路径，解码器由请求路径决定 |
| `0x424b50` | 打开 LWG 组件 |

在这个 Forest 样本中，五个文件导入槽的位置为：

| Windows 接口 | RVA |
| --- | --- |
| `CreateFileA` | `0x59098` |
| `ReadFile` | `0x5907c` |
| `SetFilePointer` | `0x590c8` |
| `GetFileSize` | `0x590cc` |
| `CloseHandle` | `0x5909c` |

覆盖命中后返回内存文件，未命中仍调用原接口，由原游戏处理散装资源及 XFL。
不拦截全进程的 NT 文件接口。请求 LIM 时必须交付 LIM 字节，不能仅改文件扩展名；
Forest 原脚本为 legacy-28，但不能据此拒绝其他引擎的 modern-36。
运行时保留 TSC 自带格式，并在能找到原散装或归档 GSC 时核对头长；不按游戏名指定头长。
图像、音频、LWG 和 TSC 的转换复用转换核心；组件布局复用同一个 `.meta.xml` 读取器。

## SevenBridge 对照

| 行为 | VA / 导入 RVA |
| --- | --- |
| 散装优先、失败后归档 | `0x427c30 → 0x42e020 / 0x42e760` |
| GSC 加载 | `0x415a60`，连读九个 DWORD，即 modern-36 |
| LIM / WCG / LWG 请求 | `0x42b910` / `0x42bf80` / `0x4281f0` |
| 文件导入 | open `0x5f098`、read `0x5f080`、seek `0x5f0e0`、size `0x5f0e4`、close `0x5f09c` |
| 双字节解析 | `0x43dad0`，`0x43db76` 形成 lead<<8\|trail |
| GDI 字形路径 | `0x44adf0`，调用 `GetGlyphOutlineA` |
| CRT 代码页初始化 | `0x44d444 → 0x44d5f1 → GetACP` |

两个样本的地址和 GSC 头长不同，文件接口的同步读、定位和关闭协议一致。
因此使用同一套 Files／Resources／Unicode DLL，不添加 SevenBridge 专用 DLL。
普通 PE 导入表通过 `imports.cpp` 解析；缺失接口或已有钩子冲突时明确报错。
SevenBridge 的 `FindFirstFileA` 仅用于存档／建目录，不是资源预检；MCI 视频则不经公共覆盖路径。

REA 依据：文件回退 `ev_aa340a6409c9a54a1f26c89c1852b42989d52f080dee22121653bcb1bfd57bba`，
GSC `ev_4b709b26aef633c9dadc5343ecb2fe26276667c62df7e3982a280d53464ee4a3`，
文字解析 `ev_fc6c754911df3df1c51c4883f8919a561b03506db554063b44fbab618c6d3916`，
字形 `ev_3560688f91be6c3ed92c87c94570b236b6514478bc63db0e127f59bf0a61b4b4`，
CRT `ev_c4c5f4527fa369b4af23870e6b73666994f8b60fe2041741d8fa196910bd6c12`。

## Unicode 接入

`0x438790` 的文字解析器以单字节／双字节遍历；`0x438836–0x438842` 形成
16 位字符值，`0x438866` 前移两个字节。`0x4453e0`、`0x445760`、`0x445ab0`、
`0x445e70` 附近的字形路径调用 ANSI GDI。
直接把 UTF-8 字节交给这些路径会拆错字符，不能只改文件编码。

`0x44880c` 查询静态 CRT 的 `0x48fa00` 分类表；`0x44bfb8` 初始化该表，
经 `0x44c165` 读取系统 `GetACP`。因此 Unicode 模块还把游戏自己的 `GetACP`
导入固定为 932，让引擎在非日文系统上也正确识别内部双字节编号。
`IsDBCSLeadByteEx` 的系统默认代码页请求同样使用 932；显式指定的其他代码页保持原义。
不修改 Windows 区域设置，也不替换其他模块的 `GetACP`。
对应 REA 证据为 `ev_fec16cd0f7502d87c643ce4be10a0f513fed452271d3d9e2470c33a9fc4ff9a2`
与 `ev_17e895a8bdb39082341c3f9bdb782e5e4ef09b18431096f0819b17088726f365`。

首版保留原指令和文字控制符；能无损往返 CP932 的字符使用原字节，其余 BMP 字符
映射到 CP932 用户区 `F0–F9` 的稳定编号。绘制时恢复实际 Unicode 字符：

| 原导入 | RVA | 映射字符使用 |
| --- | --- | --- |
| `GetGlyphOutlineA` | `0x59038` | `GetGlyphOutlineW` |
| `TextOutA` | `0x59024` | `TextOutW` |
| `GetTextExtentPoint32A` | `0x5903c` | `GetTextExtentPoint32W` |

非映射文字继续调用原 ANSI 接口。原脚本和 EXE 已使用的用户区编号会被保留；
字典先安全写入用户目录，再交付脚本，避免存档和历史中的编号在重启后变化。
这不是把不能编码的字符替换成 `?`。但它也不是完整 Unicode 排版引擎：
上限为 1880 个额外 BMP 字符，需扣除原生占用，不支持补充平面及通用复杂塑形。

## UTF-8 禁则

SevenBridge 的 `0x43e450` 在绘制后把字符分类写入布局数组：`0` 为普通，`1` 为不可行首，`2` 为不可行末。
`0x43e540–0x43e798` 比较 `SI`，分支分别汇入 `0x43e7cc`、`0x43e7b3`，普通路径为 `0x43e79a`。
REA 依据：`ev_f04c03afdea665d7bdadec40a14e679ddbec0d74637f99ddd47b90b02e821f43`。

[native_kinsoku.cpp](src/native_kinsoku.cpp) 核对完整 ASCII／CP932 比较组、条件跳转及共同目标，
支持直接比较和 `MOV EAX / CMP SI, AX` 形态。进程内桥接对齐调用栈，保存寄存器、标志与 FPU/SIMD 状态，
恢复字形编号对应的 Unicode 字符，再按 [kinsoku.json](kinsoku.json) 分类。
无法作为单个字符解码的原生组合继续使用原判断。没有识别到分类器时拒绝启用 Unicode。
不写磁盘 EXE，不在 TSC 中插入换行。

这是可定制的行首／行末字符禁则，不是完整的 [Unicode UAX #14](https://www.unicode.org/reports/tr14/)。
新增机器码形态须扩展共享识别器及回归，不添加游戏地址表。

## Albatross 的启示与边界

被检查的 `rsinit.dll` 带有 361 个 OpenGL 导出、嵌入资源和运行时钩子。
`0x623af440` 的初始化路径接入 NT 文件接口；`0x623b15e0` 还处理窗口、字体及模块生命周期。
`0x623af370`／`0x623af750` 的字体替换强制字符集 `0x86`，是 GB2312 路径，
不能视作已有 UTF-8 支持。`0x623b45f0` 是改写内存并建立跳板的钩子辅助路径。

这些观察证明 DLL 可以在不改磁盘 EXE 的前提下改变资源、窗口和文字路径；
并不证明一个 DLL 能跨版本通用。每个新引擎仍需核对调用约定、数据布局和绘制路径。
这里只据此选择接入方式，没有复制其实现，也没有依赖这个私有 DLL。

## 验证边界

公共测试检查资源优先级、损坏资源不回退、路径边界、组件布局、输入未修改和字形字典。
Windows／Wine 测试另外检查入口前加载、ABI 与失败回退、文件接口及 Unicode GDI 绘制。
模拟导入表的测试与真实游戏启动检查分开；启动成功不代表完整游玩、旧存档或全部字体已验证。
原生 Windows、其他引擎读取／渲染协议必须另行验收。

在独立游戏副本中，Wine 已检查开场、标题，以及实际加载覆盖 TSC／PNG。
通用实现已分别在 Forest 与 SevenBridge 中检查同一条覆盖及 Unicode 路径；没有逐游戏代码。
测试脚本中的日文、简体中文、CP932 之外的拉丁字符和文字颜色控制已在原引擎画面确认。
手动检查工具为 [game_smoke.cpp](tests/game_smoke.cpp)，只终止它自己创建并核对过的游戏子进程，
不加入默认 CTest，也不附带测试游戏资源。
