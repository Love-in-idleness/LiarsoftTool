# 開発計画と実験ブランチ

[中文](ROADMAP.zh.md) | [English](ROADMAP.en.md) | [Русский](ROADMAP.ru.md) | [日本語](ROADMAP.ja.md)

[メインガイド](../README.ja.md)

## 主プログラム

主プログラムはリソース変換、GTK4/libadwaita GUI、事前展開なしの閲覧とプレビューを提供します。
今後は形式の互換性、各 OS での使いやすさ、編集ツール用の共通 API を改善します。
現在の操作方法はメインガイドを参照してください。

[RScript Runtime](https://github.com/Love-in-idleness/LiarsoftTool/blob/master/runtime/README.ja.md) は主ブランチで実験的な Windows ローダーと任意 DLL を提供し、
非梱包素材の優先読込と UTF-8 対応を行います。既定ではビルドされません。使用前にガイドを確認してください。

## 実験ブランチ

| ブランチ | 内容 |
| --- | --- |
| `codex/tsc-vscode` | [VS Code 拡張と TSC グラフィカル編集](https://github.com/Love-in-idleness/LiarsoftTool/blob/codex/tsc-vscode/tsc-vscode/README.ja.md)：ハイライト、検査、コンパイル、物語編集。SevenBridge と Forest のプロファイル |

主プログラムへは未統合で、主ブランチの全機能や形式更新と同時には進まない場合があります。
対象ブランチのガイドを読み、対象ゲームで確認してください。

## 長期目標

意味に基づく TSC 創作とリソース管理を改善します。エンジンの完全再現や全作品共通のゲーム実行器は現在の編集機能ではありません。

通常の Ren'Py を生成する `--pure-renpy` は独立した
[rscript2renpy の計画](https://github.com/Love-in-idleness/rscript2renpy/blob/main/docs/ROADMAP.ja.md)で管理し、未実装です。
