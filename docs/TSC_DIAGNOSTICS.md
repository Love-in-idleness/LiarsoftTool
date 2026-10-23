# TSC 检查接口

[文档目录](README.md) · [English](#english) · [Русский](TSC_DIAGNOSTICS.ru.md)

面向编辑器和工具开发者。用户命令与参数请运行 `liarsofttool -h` 查看。

`--check-tsc` 调用同一个 TSC 编译器，在内存中检查输入，不写入 GSC。
可检查多个文件；每个文件目前报告首个错误。成功退出 0，有错误退出 1。
检查时可以指定目标编码，不能同时指定输出、转换或更新检查选项。

`--diagnostics=json` 将诊断写入标准输出，格式如下：

```json
{"diagnostics":[{"file":"scene.tsc","severity":"error","message":"line 3: unknown command: missing","line":3,"column":null}]}
```

没有错误时 `diagnostics` 为空数组。`line` 从 1 开始；文件级错误或无法确定
位置时为 `null`。当前不报告精确列号，`column` 为 `null`，编辑器应标记整行。
参数调用错误写入标准错误并返回 1，不保证 JSON 输出。

C++ 调用 `checkTsc(text, encoding)`，结果见 `include/gsc_decompiler.h`。
检查与编译采用同样的语法、参数布局和编码规则，不应在编辑器中另写编译器。

## English

This interface is for editor and tool authors. User commands are documented by
`liarsofttool -h`.

`--check-tsc` runs the existing compiler in memory without writing GSC. It accepts
multiple inputs and currently reports the first error in each file. Exit status
is 0 on success or 1 on error. The target encoding is configurable; conversion,
output and update options cannot be combined with checks.

`--diagnostics=json` writes the object shown above to stdout. Valid inputs yield
an empty `diagnostics` array. Lines are one-based; unknown or file-level locations
are `null`. Columns are currently `null`: mark the whole line rather than inventing
a column. Invalid command-line arguments use stderr and exit 1, not necessarily JSON.

C++ callers use `checkTsc(text, encoding)` from `include/gsc_decompiler.h`.
Editors should reuse this checker rather than implementing another compiler.
