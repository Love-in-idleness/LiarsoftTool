# CannonBall 引擎与工具实现核对

日期：2026-10-04。范围是资源转换、封包和编码，不是实现一个新的游戏引擎。

分析对象：`res_ft/exe/Cannonball.exe`，SHA-256：
`df4b5b6dffc7002370d0772869baaba551b08fca970e92609ca64b08174f89ba`。
本文地址均为原 PE 的虚拟地址。原程序未启动，游戏资源未改写。

## 本轮发现并修复的缺陷

| 项目 | 原程序证据或复现 | 修复 |
| --- | --- | --- |
| LIM 仅透明度图像 | `0x43bab6–0x43bac9`：先解 A，没有 `0x10` 标志就不读 RGB | 不再强行读不存在的三个颜色块；保留透明度，RGB 为零 |
| LIM RGB565 颜色 | `0x43bd56–0x43bd89`：5/6 位颜色直接左移，不是按比例扩展到 255 | 按原引擎输出，例如白色为 `(248,252,248)` |
| LIM RGB565 透明色 | 同一路径识别 `0x07e0`；独立 Alpha 在 `0x43be10` 之后覆盖 | 绿色键输出透明像素；有独立 Alpha 时仍以它为准 |
| XFL 文件排序 | `0x41ab05–0x41ab43` 用 `lstrcmpiA` 做二分查找 | 移除自然数字排序；ASCII 文件名按大小写不敏感字典序排列，包括直接序列化已有 entries 的路径 |
| XFL 长文件名 | 表项只有 32 字节名字区域；旧实现静默截断 | 编码后超过 31 字节报错，不截断 CP932 多字节字符；拒绝大小写折叠后的重名 |
| XFL/LWG 数据边界 | 新增的截断样本在旧实现下被接受；LWG 短头还可能越界读取 | 检查头、表、条目、名字、偏移和长度；拒绝负 XFL 偏移及危险输出路径；LWG 名字超过单字节长度上限时拒绝封包 |
| Windows 编码 API | 由文件名编码检查延伸发现：输出 UTF-8 却使用了不允许的 flags 和默认字符指针 | UTF-8 使用专属参数；严格模式拒绝非法输入；检查实际转换调用结果 |

XFL 例子：`1.gsc, 10.gsc, 2.gsc` 才是原字符串查找所需的 ASCII 顺序，
不能打成自然排序的 `1.gsc, 2.gsc, 10.gsc`。固定四位数字名通常不会暴露这个差异。

Windows 参数要求见微软的
[WideCharToMultiByte](https://learn.microsoft.com/en-us/windows/win32/api/stringapiset/nf-stringapiset-widechartomultibyte)
和 [MultiByteToWideChar](https://learn.microsoft.com/en-us/windows/win32/api/stringapiset/nf-stringapiset-multibytetowidechar)。
这是接口约束缺陷，不据此认定它就是某个已有 Windows 用户故障的唯一原因。

本轮代码提交：`5a85da0`（LIM）、`38fc38a`（归档）、`0df5531`（Windows 编码）。
前一轮音频核对及修复见 [音频报告](AUDIO_PROCESSING_AUDIT.md)。

## 尚未实现或覆盖不完整的功能

| 功能 | 当前边界 | 原程序或源码依据 |
| --- | --- | --- |
| PNG → LIM | 只有 LIM → PNG；图片回写使用 WCG，不能原格式双向重建 LIM | `0x43ba30` 后区分 LIM 版本，`0x43ba70` / `0x43bab0` 分别处理 16 位与独立通道布局；工具没有 LIM 编码器 |
| MSK 的可编辑往返 | `.msk` 可保留、解包、封包，但没有独立 MSK → PNG → MSK 转换入口 | `0x43ac25` 拼接 `.msk`；`0x43aca9` 检查 `BM`；`0x43ace7–0x43acf7` 接受 1/8/24 位 BMP。不能仅改后缀就承诺遮罩语义完整复现 |
| 官方高层 TSC 源语言编译 | 当前是结构化字节码 TSC 编译器，不是 RsComp 源语言的完整替代 | `0x40e38f–0x40e39d` 解析 DLL 的 `Compile`；`0x44f274/0x44f280` 是 TSC 文件名格式。官方示例有 `#include`、`%title` 等语法，当前解析器要求 `*命令` 或标签 |
| 整个游戏的资源依赖检查 | 目前逐文件转换，不检查所有脚本引用的图片、音频、子脚本和视频是否存在 | 转换器能读指令，但没有项目级引用图、缺失资源清单或动态引用的未确认标记 |
| 非 ASCII 文件名的原生 Windows 排序 | 已修复 ASCII 排序；没有完整复刻不同 Windows 区域设置下的 NLS 比较 | 原引擎使用 `lstrcmpiA`；当前排序采用 UTF-8 名字的 ASCII 大小写折叠，不能承诺任意日文、中文、俄文文件名排序等价 |
| 普通 PCM 与 Ogg 的转码、自动重采样 | PCM 保留，Ogg 提取/封装；不编码、不重采样，不自动修正声道 | 原程序 PCM 与 ACM 压缩播放路径不同；详见音频报告。模板与 Ogg 参数不匹配时拒绝，不能用容器包裹代替转码 |
| 视频转换 | LiarsoftTool 没有 MPG → WebM 等转换入口，视频也不属于既定封包资源白名单 | `0x431f67` 取得 `mciSendCommandA`，`0x431f8f–0x431f9b` 以 `mpegvideo` 打开媒体。Ren'Py 视频适配应由移植项目处理 |

其中前四项适合继续发展资源工具；视频播放、场景效果、菜单和存档运行逻辑属于
`rscript2renpy` 或游戏引擎，不应当作为 LiarsoftTool 的缺陷来修。
本报告也不声称通过一个 2003 年 EXE 就能确认所有后期游戏 opcode 的语义。

建议优先做**项目级资源依赖检查**，再做 MSK 可编辑往返和 LIM 编码器。
高层 TSC 编译器工作量大，现有结构化 TSC 已能编辑和重编译，不必先实现全部原始语言。

## 验证与未验证项

- 2,955 个 CannonBall LIM 全部解码；其中两个 RGB565 样本与独立的原引擎颜色/透明键规则
  逐像素比对，覆盖 455 个透明键像素。Alpha-only 等布局另有合成回归样本。
- 归档批量验证：19 个 XFL、196 个 LWG，失败 0；XFL 重打包后条目名字和数据一致。
  批量测试跳过 7 个超过 64 MiB 的归档，不能称为所有大归档均已验证。
- Linux CTest 全部 8 项通过。LIM 和归档边界测试通过 ASan/UBSan；归档测试中仅相关
  解析/编码源码启用检测器，不等于全仓库都做了 sanitizer 验证。
- Windows CLI/GUI 交叉编译成功，LIM、归档、编码测试在隔离 Wine 环境通过。
  编码测试增加 UTF-8 多语言/emoji、GBK、CP1251、非法 UTF-8 和不可表示字符案例。
- 未在原游戏中验收画面、播放和归档查找；未在真实 Windows 上运行。视觉、听感及游戏内
  效果仍需使用者确认。测试资源和临时对照程序没有进入仓库，没有 push 或发布 release。

日常回归：`cmake --build build -j 4`，再执行 `ctest --test-dir build --output-on-failure`。
