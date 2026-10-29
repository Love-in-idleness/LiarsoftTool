# CodeX TSC

[中文](README.zh.md) | [English](README.en.md) | [Русский](README.ru.md) | [日本語](README.ja.md)

Syntax highlighting, error checking and GSC compilation for TSC files in VS Code.

## Install

1. Install VS Code 1.74 or newer and LiarsoftTool from this branch.
2. Choose **Install from VSIX** in Extensions and open the VSIX file.
3. Search settings for `liarsofttool.path` and enter the full path to the LiarsoftTool command-line program: `liarsofttool.exe` on Windows or `liarsofttool` on Linux.

## Use

Open a TSC file to edit it. Saving automatically checks the file and shows diagnostics in Problems.
The Command Palette provides **CodeX TSC: Check saved file** and **CodeX TSC: Compile saved file to GSC**.
Manual checks and compilation prompt you to save unsaved changes first. Tool errors appear in Output → LiarsoftTool TSC.

Search settings for `liarsofttool`:

- `path`: path to the LiarsoftTool command-line program.
- `encoding`: GSC text encoding used by the target game; defaults to CP932. Save TSC files as UTF-8.
- `outputDirectory`: folder for GSC output. Relative paths start from the TSC file’s folder. Leave empty to save there; existing GSC files with the same name may be overwritten.

Checking and compilation require Workspace Trust. In a remote workspace, install LiarsoftTool on the remote host.
The interface follows VS Code’s Chinese, Japanese, English or Russian setting, with English for other languages.

## Build locally

Use Node.js 22 or newer and a built LiarsoftTool from this branch:

```bash
cd tsc-vscode
npm ci
npm test
npm run package -- --out liarsofttool-tsc.vsix
```

Tests accept `LIARSOFTTOOL_PATH` to select the compiler.

[TSC reference](../docs/GSC_OPCODE_REFERENCE.en.md)

The extension is GPLv3. No game resources are provided.
