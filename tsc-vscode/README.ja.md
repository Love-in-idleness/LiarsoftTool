# CodeX TSC

[中文](README.zh.md) | [English](README.en.md) | [Русский](README.ru.md) | [日本語](README.ja.md)

VS Code の TSC ファイルに構文強調、エラーチェックと GSC コンパイルを提供します。

## インストール

1. VS Code 1.74 以降と、このブランチの LiarsoftTool を用意します。
2. 拡張機能メニューの「VSIX からのインストール」で VSIX ファイルを選びます。
3. 設定で `liarsofttool.path` を検索し、LiarsoftTool コマンドラインプログラムの絶対パスを入力します。Windows は `liarsofttool.exe`、Linux は `liarsofttool` です。

## 使い方

TSC ファイルを開くと編集できます。保存時に自動検証し、結果を「問題」パネルに表示します。
コマンドパレットには **CodeX TSC: 保存済みファイルを検証**と **CodeX TSC: 保存済みファイルを GSC にコンパイル**があります。
手動検証やコンパイルの前に未保存の変更があれば、保存を確認します。ツールのエラー詳細は「出力 → LiarsoftTool TSC」に表示します。

設定で `liarsofttool` を検索してください。

- `path`：LiarsoftTool コマンドラインプログラムのパス。
- `encoding`：対象ゲームの GSC 文字コード。標準は CP932 です。TSC ファイルは UTF-8 で保存します。
- `outputDirectory`：GSC の保存先。相対パスは TSC のあるフォルダーを基準にします。空欄では同じフォルダーに保存し、既存の同名 GSC を上書きする場合があります。

検証とコンパイルにはワークスペースの信頼が必要です。リモート作業ではリモート側に LiarsoftTool を設置してください。
表示言語は VS Code の中国語・日本語・英語・ロシア語設定に従い、それ以外は英語になります。

## ローカルビルド

Node.js 22 以降と、ビルド済みのこのブランチの LiarsoftTool が必要です。

```bash
cd tsc-vscode
npm ci
npm test
npm run package -- --out liarsofttool-tsc.vsix
```

テストのコンパイラーは `LIARSOFTTOOL_PATH` で指定できます。

[TSC 命令資料](../docs/GSC_OPCODE_REFERENCE.ja.md)

GPLv3 で配布します。ゲーム素材は含みません。
