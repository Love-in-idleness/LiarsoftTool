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
