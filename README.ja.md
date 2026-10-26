# LiarsoftTool

[中文](README.zh.md) | [English](README.en.md) | [Русский](README.ru.md) | [日本語](README.ja.md)

CodeX RScript ゲームのリソースツールです。アーカイブの展開・再梱包、画像・音声の変換、
GSC スクリプトの抽出・編集・再構築に対応します。Liar-soft、raiL-soft などの作品を対象とし、
対応範囲はゲームによって異なります。ゲーム EXE の変換機能はありません。

Ren'Py プロジェクトの生成には、独立した [rscript2renpy](https://github.com/Love-in-idleness/rscript2renpy) を使います。

## 導入と使い始め

[Releases](https://github.com/Love-in-idleness/LiarsoftTool/releases) から対応するプログラムを取得します。
GUI と CLI は別配布で、Windows は圧縮された単一 EXE、Linux は個別のバイナリです。
Linux では実行権限を付けてください。GUI には GTK4 4.10+ と libadwaita 1.5+、
音声プレビューには GTK メディアバックエンドと GStreamer Ogg/Vorbis プラグインが必要です。
Windows GUI にはメディア実行環境を同梱しています。[ソースビルド](docs/BUILDING.ja.md)も参照してください。

操作はゲームのコピーで行ってください。既定の出力先は入力と同じ場所で、同名ファイルを上書きする場合があります。

1. 変換タスクへファイルやフォルダーを追加するか、ウィンドウへドラッグします。
2. 文字コードと出力先を確認し、必要に応じて再帰、梱包のみ、展開のみを選びます。
3. 変換を開始します。失敗時はエラーの詳細をすべてコピーしてください。

GUI と CLI はどちらも既定で GSC を TSC に出力します。会話抽出のみなら GUI の **GSC→TXT** を選びます。
文字コードの既定は CP932 です。中国語パッチなどは実際の文字コードを手動指定します。変換とプレビューは同じ設定を使います。

## リソースの閲覧

フォルダー、XFL、LWG、入れ子のアーカイブを事前展開なしで開けます。
項目を選ぶと画像、音声、テキスト、読み取り専用の GSC→TSC、ページ式の十六進バイトを確認できます。
画像は拡大縮小・透明背景、音声は一時停止・シーク・音量に対応します。
文字化け時は文字コードを変更します。TSC は常に UTF-8 です。

閲覧は入力を変更しません。生バイトの書き出しは形式を変換しません。
アーカイブ項目を変換するには、まず書き出して変換タスクへ追加します。

設定では変換オプション、システム／明色／暗色テーマ、表示言語を選べます。
中国語・英語・ロシア語・日本語に対応し、既定はシステム言語です。手動の言語変更は再起動後に反映されます。
更新確認は手動操作で配布ページを開くだけで、自動ダウンロードやインストールは行いません。

## スクリプトの編集

- TXT：`>` の後ろに訳文を書き、原 GSC を参照して再構築します。
- TSC：命令と文字列を編集し、対象ゲームの文字コードでコンパイルします。TSC 自体は UTF-8 で保存します。

GSC→TSC は実験的機能です。原 GSC と生成されたラベル・メタデータを残してください。
変換成功は全エンジン版での互換性を保証しません。命令は [TSC 資料](docs/GSC_OPCODE_REFERENCE.ja.md)を参照してください。

## 実験ランタイム

このブランチには Windows ローダー、任意 DLL、非梱包素材の優先読込、UTF-8 対応の [RScript Runtime](runtime/README.ja.md)があります。
別途ビルドするか対応するテストパッケージを使います。主プログラムの配布にはこれらの DLL は含まれません。
本ブランチの変換器の既定は CP932 で、主ブランチの文字コード自動判定はありません。以下はこのブランチのソースについての説明です。

## ヘルプと資料

引数、対応形式、変換例はプログラム内のヘルプを参照してください。

```bash
liarsofttool -h
```

[文書一覧](docs/README.ja.md) · [ビルド](docs/BUILDING.ja.md) · [開発計画](docs/ROADMAP.ja.md)

報告にはプログラムの版、入力形式、文字コード、完全なエラー詳細を添えてください。
正規に所有する素材を使い、ゲームファイルは公開しないでください。

## ライセンスと謝辞

LiarsoftTool は GPLv3 です。第三者のコンポーネントには、それぞれのライセンスが適用されます。

- [RaiLTools](https://github.com/EusthEnoptEron/RaiLTools): アーカイブ・スクリプト形式の研究。
- [arc_unpacker](https://github.com/vn-tools/arc_unpacker): GPLv3 の画像展開コード。
- [GARbro](https://github.com/crskycode/GARbro): WCG エンコードの参考。
- [stb](https://github.com/nothings/stb): 画像の読み書き。
- [WebP](https://chromium.googlesource.com/webm/libwebp/): 可逆 WebP。
- [Ogg / Vorbis](https://xiph.org/vorbis/): 音声。[ライセンス原文](docs/THIRD_PARTY_AUDIO_LICENSES.md)。
- [GTK4 / libadwaita](https://www.gtk.org/): 共通デスクトップ GUI。
- [GStreamer](https://gstreamer.freedesktop.org/): GTK メディアバックエンドの音声プレビュー。
- [libcurl](https://curl.se/libcurl/): HTTPS による更新確認。
