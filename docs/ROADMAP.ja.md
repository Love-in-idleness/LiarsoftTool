# 開発計画

[中文](ROADMAP.zh.md) | [English](ROADMAP.en.md) | [Русский](ROADMAP.ru.md) | [日本語](ROADMAP.ja.md)

[プロジェクトの説明](../README.ja.md)

## 開発順序

1. rscript2renpy の SevenBridge 日本語版移植に必要な変換修正。
2. C++ から GTK4/libadwaita の C API を呼ぶ共通 GUI。最小ウィンドウとパッケージを Linux/Windows で先に確認し、システム連動・ライト・ダーク、中国語・英語・ロシア語・日本語を提供。
3. 事前展開なしの閲覧。フォルダー、XFL、LWG、入れ子のアーカイブを必要時に読み、画像、音声、テキスト、GSC/TSC、バイト列をメモリー内でプレビュー。
4. 独立ブランチ `codex/tsc-vscode` でハイライト、検査、ローカルコンパイラー呼び出しを開発。C++ の検査を共有し、GSC を出力しない CLI を提供。

共通変換入口、引数、既定値は維持します。閲覧は入力を変更せず、書き出しと変換は利用者の明示操作で行います。文書は中国語・英語・ロシア語・日本語を分けて同期します。自動 push、公開、バージョン変更は行いません。これは計画であり、実装済み機能の一覧ではありません。

## グラフィカル TSC エディター：独立ブランチの初版

最新の方針により `codex/tsc-vscode` で段落・イベント・接続の編集、ソースへの切り替え、静止画プレビューと元のエンジン用出力を提供しています。
日本語版 SevenBridge を優先し、Forest 設定も保持します。他作品の拡張は行いません。
元のゲームでの手動確認が必要な初版であり、エンジンのシミュレーターでも master への統合済み機能でもありません。[案内](../tsc-vscode/README.ja.md)を参照してください。

実験的 `--pure-renpy` は rscript2renpy の[長期計画](https://github.com/Love-in-idleness/rscript2renpy/blob/main/docs/ROADMAP.ja.md)です。本段階ではバックエンドや仮のオプションを実装しません。
