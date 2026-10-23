# CodeX TSC

[中文](README.md) · English · [Русский](README.ru.md)

Highlighting, compiler-backed diagnostics and GSC compilation for the current
LiarsoftTool TSC format. This extension lives on `codex/tsc-vscode` and is not
published to the Marketplace.

## Install and use

1. Install LiarsoftTool with `--check-tsc` support, included on this branch.
   The extension never downloads a compiler.
2. Choose **Extensions: Install from VSIX** in VS Code and select the local VSIX.
3. Search settings for `liarsofttool`. Set the executable path, game encoding and
   optional output folder. Use the actual executable, with a full path if not on PATH.

TSC source is UTF-8; the encoding setting applies to GSC output, usually CP932 for
Japanese. In trusted workspaces, opening or saving a TSC checks it automatically
and reports errors in Problems. Editing clears stale diagnostics. Manual checks
and compilation prompt to save unsaved changes first.

Commands:

- **CodeX TSC: Check saved file**
- **CodeX TSC: Compile saved file to GSC**

An empty output folder writes beside the TSC. Compilation can replace existing
GSC: back up first. For failures, read **Output → LiarsoftTool TSC**. Restricted
Mode provides highlighting only. In remote workspaces, install the compiler on
the remote host as well.

Currently reports the first compiler error per file. No semantic completion or
visual editor. On Windows, the older conversion entry point still requires paths
representable in the system code page; move inputs and outputs if necessary.
The new check entry point supports Unicode paths.

## Build locally

Use Node.js 22 or newer. Run in this directory:

```bash
npm ci
npm test
npm run package -- --out ../build/liarsofttool-tsc-0.1.0.vsix
```

Set `LIARSOFTTOOL_PATH` to select the local test compiler. See the
[diagnostics interface](../docs/TSC_DIAGNOSTICS.md#english) and
[TSC reference](../docs/GSC_OPCODE_REFERENCE.en.md). Licensed under GPLv3.
