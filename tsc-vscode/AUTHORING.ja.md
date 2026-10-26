# TSC 制作：開発者向け資料

[中文](AUTHORING.zh.md) | [English](AUTHORING.en.md) | [Русский](AUTHORING.ru.md) | [日本語](AUTHORING.ja.md)

[利用案内](README.ja.md)

## ソースの扱い

C++ の `inspectTsc` はコンパイラーのトークナイザー、引数表、ラベル検証を共有します。
`--inspect-tsc <file>` または `--inspect-tsc -` は GSC を書かずにプロトコル 1 の JSON を返します。
[インターフェース](../docs/TSC_DIAGNOSTICS.ja.md)。位置は UTF-8 のバイト範囲で、境界を確認して VS Code の UTF-16 位置へ変換します。

`graph.js` はコンパイラーの結果を図にするだけで、別の TSC 文法を実装しません。
ラベルがノード、分岐と選択肢が接続となり、暗黙の順次実行も保持します。
return/end/jump 後の命令はソースに残しますが、実行可能なローカル接続にはしません。
選択したトークンまたはイベント範囲のみを書き換え、C++ 検証後に取り消し可能な WorkspaceEdit を適用します。
古い文書バージョンや構文エラーでは変更を拒否し、無関係なコメントと未知の命令を並べ替えません。

プロジェクト JSON は物語の複製ではありません。`;@editor-node` はタイトルと座標、
`;@editor-event` / `;@editor-event-end` は複数の元命令をまとめる印です。他のコメントは通常のコメントです。
ビルドは全ソース、出力文字コード、確定できるシーン呼び出しを事前確認します。動的呼び出しは先にソースで解決してください。
出力先と scr・assets・素材を重複させず、内部のシンボリックリンクも拒否します。
同じディレクトリの一時ファイルで置換しますが、プロジェクト全体のトランザクションではありません。素材の失敗は修正して再ビルドしてください。

## SevenBridge の設定

対象は日本語版 `SBridge_jp.exe`、SHA-256：

```text
c7d5fc38cf0c354a61d056b607cf1367b2529557e9e0407d0b0fdf12771b35e3
```

[元プログラムの根拠](https://github.com/Love-in-idleness/rscript2renpy/blob/main/sevenbridge/NATIVE_EVIDENCE.ja.md)：
分岐処理 `0x41cc40`、3 引数 locmode `0x420220`、folder `0x4204e0`、
効果音 cases `0x3e–0x40`、音声パス構築 `0x41b5b0`。
初期値は本作のソースで確認し、他作品から推測しません。静的な根拠であり、原エンジンの実動作検証ではありません。

| イベント | SevenBridge 出力形式 | 根拠 |
| --- | --- | --- |
| 座標・フォルダー | `locmode obj 0 0`、`folder obj "grpo"` | 0000 |
| 台詞 | `TXT 0 voice 0 0 "" "^gNNNtext" 1` | 2001 |
| 画像 | queue、folder、locmode、`load layer id x y 0 0`、`update 0 0 0` | 0000、2001 |
| 消去 | queue、`cls layer 0`、update | 2001 |
| 音楽 | `bgm_on track 1 0`、`bgm_off 0 0` | 2001 |
| 効果音・音声 | `se id` + `se_on 0 0 0`、`voice id 0 0 0` | 2001 |
| 呼び出し・復帰 | `gosub scene ""` + 10 個のゼロ、`return 0` | 2001 |
| 選択肢 | 5 個の飛び先、5 個の本文、末尾 `0 0 0` | select 引数表、1309 |
| 変数 | 代入 `0x1800`、演算 `0xa800/b800` と書戻し `0x1900`、比較 `0x4800–9800` | 0000 と共通 VM |

新規ソースは `modern-36 / rscript18` とし、3 引数 locmode と単一効果音チャンネルに合わせます。
既存 rscript19/modern は変更せず、互換性のない挿入は形式変更ではなくエラーにします。
Forest は `legacy-28 / early` を保持し、gosub はシーン番号のみ、bgm_on の末尾は `1`、select の末尾は `2 0 0` です。
根拠は元の 0000/2100 とコアの early 引数表です。

## 検証の範囲

Node テストはソース保持、Unicode、条件・選択、イベント、パス保護、失敗時のビルドとプレビュー座標を確認します。
`test/host.js` は実際の VS Code で診断、グラフ、SevenBridge ビルド、ソースへの切り替えを検証します。
`test/browser.js` は本物の provider とコンパイラーを隔離したテストデータで動かします。
ゲームの通しプレイではありません。ネイティブ Windows のパス、実際の文字組みと物語全体は手動確認が必要です。

`tsc-vscode/` で `npm test` を実行し、コンパイラーは `LIARSOFTTOOL_PATH` で指定できます。
`node test/browser.js` は localhost 限定の一時 SevenBridge プロジェクトを提供し、終了時に消去します。ゲーム素材は不要です。
