# TSC diagnostics interface

[中文](TSC_DIAGNOSTICS.zh.md) | [English](TSC_DIAGNOSTICS.en.md) | [Русский](TSC_DIAGNOSTICS.ru.md)

[Documentation](README.en.md)

For editor and tool authors. User commands and options are documented by `liarsofttool -h`.

`--check-tsc` runs the same compiler in memory without writing GSC. It accepts multiple inputs and currently reports
one error per failing file. Exit status is 0 on success, 1 on errors. Target encoding is configurable;
output, conversion and update-check options cannot be combined with checking.

`--diagnostics=json` writes to stdout:

```json
{"diagnostics":[{"file":"scene.tsc","severity":"error","message":"line 3: unknown command: missing","line":3,"column":null}]}
```

Valid inputs produce an empty `diagnostics` array. Lines are one-based; file-level errors or unknown locations use `null`.
Precise columns are not reported: `column` is `null`, so editors should mark the whole line.
Invalid CLI arguments use stderr and exit 1; JSON is not guaranteed for invocation errors.

C++ callers use `checkTsc(text, encoding)`, declared in `include/gsc_decompiler.h`.
Checking and compilation share syntax, operand layouts and encoding rules; do not maintain another compiler in the editor.
