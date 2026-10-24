# TSC 検査インターフェース

[中文](TSC_DIAGNOSTICS.zh.md) | [English](TSC_DIAGNOSTICS.en.md) | [Русский](TSC_DIAGNOSTICS.ru.md) | [日本語](TSC_DIAGNOSTICS.ja.md)

[文書一覧](README.ja.md)

エディター・ツール開発者向けです。利用者向けのコマンドと引数は `liarsofttool -h` にあります。

`--check-tsc` は共通コンパイラーをメモリー内で実行し、GSC を書きません。複数入力に対応し、現在は失敗ファイルごとに最初のエラーを報告します。成功時は 0、エラー時は 1 を返します。`-e` で出力文字コードを指定してください（`auto` は使えません）。出力先、変換、更新確認のオプションとは併用できません。

`--diagnostics=json` は標準出力へ次の形式で返します。

```json
{"diagnostics":[{"file":"scene.tsc","severity":"error","message":"line 3: unknown command: missing","line":3,"column":null}]}
```

正常な入力は空の `diagnostics` 配列です。行番号は 1 始まりで、ファイル全体のエラーや位置不明の場合は `null` を使います。正確な列は返さず、`column` は `null` です。エディターは行全体を指してください。不正な CLI 引数は標準エラーと終了コード 1 で報告し、呼び出し自体のエラーには JSON を保証しません。

C++ からは `include/gsc_decompiler.h` の `checkTsc(text, encoding)` を使います。検査とコンパイルは文法、オペランド配置、文字コード規則を共有します。エディター側に別のコンパイラーを実装しないでください。

## グラフ編集用の文書インターフェース

`--inspect-tsc` はファイル一つ、または `-` で UTF-8 の標準入力を受け取り、JSON を返します。
コンパイラーの構文検査を共有し、出力文字コードの制限や GSC の書き込みはありません。終了コードは 0/1 です。
`version: 1` は `byteFormat`、`schema`、`lines`、`diagnostics` を含みます。
各行は `kind`、`line`、`start`、`end`、`opcode`、`tokens` を持ち、トークンは復号後の `value`、
`quoted`、`role` とソース範囲を持ちます。範囲はゼロ始まり・半開区間の UTF-8 バイト位置で引用符を含み、
UTF-16 の列番号ではありません。行範囲には元の改行を含みます。エラー時は部分的な結果となるため、
原文を保持し、グラフからの変更を無効にしてください。役割は命令、ラベル、参照、文字列、式、数値です。
意味が未確認でも既知のレイアウトは opcode を保持します。C++ API は `inspectTsc(text)` です。
旧ツールには更新を案内し、別のパーサーで代用しないでください。
