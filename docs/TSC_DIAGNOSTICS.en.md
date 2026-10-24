# TSC diagnostics interface

[中文](TSC_DIAGNOSTICS.zh.md) | [English](TSC_DIAGNOSTICS.en.md) | [Русский](TSC_DIAGNOSTICS.ru.md) | [日本語](TSC_DIAGNOSTICS.ja.md)

[Documentation](README.en.md)

For editor and tool authors. User commands and options are documented by `liarsofttool -h`.

`--check-tsc` runs the same compiler in memory without writing GSC. It accepts multiple inputs and currently reports
one error per failing file. Exit status is 0 on success, 1 on errors. Specify the target encoding with `-e` (`auto` is not accepted);
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

## Graph editor document interface

`--inspect-tsc` accepts one file, or `-` for UTF-8 stdin, and always produces JSON.
It shares compiler syntax validation, without target-encoding restrictions or GSC writes. Exit codes are 0/1.
Protocol `version: 1` returns `byteFormat`, `schema`, `lines` and `diagnostics`.
Lines contain `kind`, `line`, `start`, `end`, `opcode`, `tokens`; tokens contain decoded `value`,
`quoted`, `role` and source ranges. Ranges are zero-based, half-open UTF-8 byte offsets, including quotes,
not UTF-16 columns. Line ranges include original line endings. Errors may leave partial records:
keep the source and disable graphical mutation. Roles distinguish commands, labels, targets, strings,
expressions and numbers. Known layouts with unknown semantics retain their opcode.
C++ callers use `inspectTsc(text)`. Report an upgrade requirement for older tools instead of using another parser.
