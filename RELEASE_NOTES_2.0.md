# LiarsoftTool 2.0

发布日期：2026-09-07

## 主要变化

- 实验性 GSC→TSC 反编译器已从 Python 原型迁移到 C++ 主程序。
- TSC 可转换回 GSC；未修改脚本按字节精确重建。
- 已识别脚本改用 `gsc-structure-v1`：逐条记录指令，并分别保存文件头、字符串、数据块和调试区段，不再嵌入完整 `gsc-raw`。
- 支持 pre-CodeX、早期 CodeX、RScript 1.8、RScript 1.9 和现代指令布局。
- 支持修改 36 字节头脚本中的 TXT/TXA 对白及已知定长命令参数。
- 增加完整的 [GSC opcode 与 TSC 命令对照表](docs/GSC_OPCODE_REFERENCE.md)，未知语义 opcode 单独标注。
- 补充 LiarsoftTool 与 `rscript2renpy` 的配合流程；`rscript2renpy` 仍是独立项目。

## 兼容性

- 继续接受 LiarsoftTool 1.5 生成的 `gsc-raw-v1` TSC。
- 无法确定指令边界的未知 GSC 方言会明确标注反编译不可用，并保留 `gsc-raw-v1` 无损回退。
- GSC/TSC 的日文编码边界统一使用 CP932（Windows-31J）。

## 已知限制

- GSC↔TSC 功能仍标记为实验性。
- 尚不支持新增或删除指令、更换 opcode、重组表达式或重排控制流。
- `0x0054`、`0x0071`、`0x008C`、`0x008D`、`0x008E`、`0x00DC` 的参数结构已知，但命令语义尚未验证。

## 发布前验证

- CTest：4/4 通过。
- 本地兼容性矩阵：十套游戏共 1,894 个 GSC，全部生成不含完整 `gsc-raw` 的结构化 TSC，并逐字节重建原文件。
- 样本与游戏 EXE 仅用于本地兼容性验证，不包含在发布包或源码仓库中。

## 升级建议

旧版 TSC 可以继续使用。若要采用结构化重建格式，请使用 2.0 重新执行 `--gsc-to-tsc`。
