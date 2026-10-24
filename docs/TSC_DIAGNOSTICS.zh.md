# TSC 检查接口

[中文](TSC_DIAGNOSTICS.zh.md) | [English](TSC_DIAGNOSTICS.en.md) | [Русский](TSC_DIAGNOSTICS.ru.md) | [日本語](TSC_DIAGNOSTICS.ja.md)

面向编辑器和工具开发者。用户命令与参数请运行 `liarsofttool -h` 查看。

`--check-tsc` 调用同一个 TSC 编译器，在内存中检查输入，不写入 GSC。
可检查多个文件；每个文件目前报告首个错误。成功退出 0，有错误退出 1。
检查时必须用 `-e` 指定目标编码（不接受 `auto`），不能同时指定输出、转换或更新检查选项。

`--diagnostics=json` 将诊断写入标准输出，格式如下：

```json
{"diagnostics":[{"file":"scene.tsc","severity":"error","message":"line 3: unknown command: missing","line":3,"column":null}]}
```

没有错误时 `diagnostics` 为空数组。`line` 从 1 开始；文件级错误或无法确定
位置时为 `null`。当前不报告精确列号，`column` 为 `null`，编辑器应标记整行。
参数调用错误写入标准错误并返回 1，不保证 JSON 输出。

C++ 调用 `checkTsc(text, encoding)`，结果见 `include/gsc_decompiler.h`。
检查与编译采用同样的语法、参数布局和编码规则，不应在编辑器中另写编译器。

## 图形编辑器文档接口

`--inspect-tsc` 接收一个文件，或用 `-` 从标准输入读取 UTF-8 文本，固定输出 JSON。
它复用编译器语法检查，不限制目标编码，也不写入 GSC。成功返回 0，错误返回 1。
协议 `version: 1` 返回 `byteFormat`、`schema`、`lines` 与 `diagnostics`。
每行包含 `kind`、`line`、`start`、`end`、`opcode`、`tokens`；参数含解码后的 `value`、
`quoted`、`role` 和源码范围。范围为从零开始、左闭右开的 UTF-8 字节偏移，包含引号；
不是 UTF-16 列号。整行范围包含原换行。错误时可能仅返回部分记录，必须保留原文并禁用图形修改。
`role` 区分命令、标签、目标引用、字符串、表达式和数值。语义未知的已知布局仍保留 opcode。
C++ 接口为 `inspectTsc(text)`；旧工具不提供该协议，编辑器应提示升级，不另写解析器。
