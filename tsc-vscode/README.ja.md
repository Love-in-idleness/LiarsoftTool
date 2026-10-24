# CodeX TSC

[中文](README.zh.md) | [English](README.en.md) | [Русский](README.ru.md) | [日本語](README.ja.md)

現行 LiarsoftTool TSC のハイライト、エラー検査、GSC コンパイルに対応します。`codex/tsc-vscode` ブランチで開発し、拡張マーケットには公開していません。

## 導入

1. `--check-tsc` 対応の LiarsoftTool を用意します。このブランチには実装を含みます。拡張自身はコンパイラーをダウンロードしません。
2. VS Code の拡張メニューで **VSIX からのインストール** を選び、ローカル VSIX を開きます。
3. 設定で `liarsofttool` を検索し、実行ファイル、ゲームの文字コード、任意の出力先を指定します。PATH にない場合は実際の実行ファイルのパスを設定してください。

TSC は UTF-8 です。文字コード設定は出力 GSC 用で、日本語は通常 CP932。信頼済みワークスペースで、TSC の表示・保存時に自動検査し「問題」へ表示します。未保存変更がある間は旧診断を消し、手動検査・コンパイル時は先に保存するか確認します。

コマンドパレット:

- **CodeX TSC: Check saved file**
- **CodeX TSC: Compile saved file to GSC**

出力先が空なら TSC と同じ場所に書きます。既存 GSC を上書きする可能性があるためバックアップしてください。失敗時は **出力 → LiarsoftTool TSC** を確認します。制限モードはハイライトのみでツールを実行しません。リモートワークスペースではツールもリモート側へ導入します。

現在はファイルごとに最初のエラーを返し、意味補完やビジュアルエディターはありません。Windows の従来変換入口はシステムコードページで表現できるパスが必要です。失敗時は入力・出力を対応する場所へ移してください。新しい検査入口は Unicode パス対応です。

## ローカルビルド

Node.js 22 以降が必要です。このディレクトリで実行します。

```bash
npm ci
npm test
npm run package -- --out ../build/liarsofttool-tsc-0.1.0.vsix
```

開発テストは `LIARSOFTTOOL_PATH` でローカルコンパイラーを指定できます。[検査 API](https://github.com/Love-in-idleness/LiarsoftTool/blob/master/docs/TSC_DIAGNOSTICS.ja.md)と [命令リファレンス](https://github.com/Love-in-idleness/LiarsoftTool/blob/master/docs/GSC_OPCODE_REFERENCE.ja.md)も参照してください。拡張はリポジトリの GPLv3 で配布します。
