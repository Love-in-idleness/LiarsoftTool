# LiarsoftTool 2.1

发布日期：2026-09-10

## 主要变化

- GSC↔TSC 已转为正式支持功能，不再标记为实验性。
- TSC→GSC 从结构化指令、字符串和数据块重新构建；正文文本及 `*font` 字符串可以直接修改。
- 补充并命名更多经过 DLL 分派或实际脚本验证的 RScript opcode；语义尚未确认的指令继续以数值 `*opcode` 表示。
- 修复早期 28 字节头 GSC 的声明长度，解决 CannonBall 赛车部分在往返转换后的运行错误。
- EXE 转换支持将已识别的 CP932、GBK、CP1251 字体 charset 相互转换，并统一混合状态的两种机器码模式。
- 修复 Windows GUI 的 CP1251 选项识别，以及 Linux/Windows GUI 的输出文件名和类型刷新。
- WAV 解包会区分 Liar-soft 嵌入式 Ogg 与标准 PCM WAV；标准 WAV 原样保留。
- 清理旧 Python 原型、研究样本和专有 DLL；这些资料不进入源码及发布包。

## 兼容性与限制

- 支持 pre-CodeX、早期 CodeX、RScript 1.8、RScript 1.9 和现代指令布局。
- 已识别布局按结构和执行语义重建；调试表、字符串编号和字节排列不保证与原文件完全相同。
- 无法识别指令边界的方言会明确标注不可反编译，并保留 `gsc-raw-v1` 无损回退。
- 旧版结构化 TSC 不再兼容，须从原始 GSC 重新生成。

## 发布前验证

- Linux CLI 与 GTK GUI 构建通过。
- Windows x86_64 CLI 与原生 GUI 交叉编译通过。
- CTest：5/5 通过。
- 本地兼容性资料的 GSC 结构与执行语义往返验证通过；CannonBall 赛车部分已完成实际游戏验证。
- 混合 charset 的实际游戏 EXE 已验证可完整转换为 CP932、GBK 或 CP1251。
