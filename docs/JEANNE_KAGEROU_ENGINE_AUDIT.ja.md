# 開発資料: Jeanne / Kagerou のリソース

[中文](JEANNE_KAGEROU_ENGINE_AUDIT.zh.md) | [English](JEANNE_KAGEROU_ENGINE_AUDIT.en.md) | [Русский](JEANNE_KAGEROU_ENGINE_AUDIT.ru.md) | [日本語](JEANNE_KAGEROU_ENGINE_AUDIT.ja.md)

形式と特定の原プログラムに関する開発資料です。EXE 編集は明示的に有効にする[実験的機能](EXE_EDITOR.ja.md)です。対応する操作はエディターの解析結果で確認してください。

[文書一覧](README.ja.md) · [利用ガイド](../README.ja.md)

字体、LWG 名、メディアの静的解析です。原本は起動・変更せず、確認済みの字体機械語形状と LWG 名処理のみ共通変換器に追加しました。

## 対象

| ファイル | SHA-256 | PE FileVersion |
| --- | --- | --- |
| `j.exe` | `7cb856bdbc5c61d9606734b30fd83bac880a3eff9829112f0ddf203e3026c4a6` | 1, 11, 0, 3 |
| `kagerou.exe` | `f02998fae350945f4de4dc7eab47997e1e2d9b8aa43d49740a80aad893dcde9b` | 1, 9, 0, 0 |

いずれも未パックと見られる PE32/i386、ImageBase `0x400000`。アドレスはファイル位置でなく VA。リソース版は発売日を証明しません。

## 字体 charset

各 EXE に直接 IAT CreateFontA が 12 箇所。旧検出は 6 箇所しか変換せず、GBK 化後も六つの `0x80` が残りました。weight/height 計算の交錯やゼロ EBP/EBX による引数供給であり、新しい文字コードではありません。

| 形状 | j.exe charset | kagerou.exe charset |
| --- | --- | --- |
| EBP zeros、固定 weight | `0x45c173` | `0x457e83` |
| EDI weight 交錯、EBP height | `0x45ec5e` | `0x45c61e` |
| EDI weight 交錯、EBX height | `0x45efeb, 0x45f33e` | `0x45c9ab, 0x45ccfe` |
| EBX zeros、EAX weight | `0x45fa6f` | `0x45d42f` |
| 即値 zeros、EAX weight | `0x45fcd6` | `0x45d696` |

IAT アドレスだけ可変とし、観測した形状全体を照合して charset 1 byte のみ変更します。任意の PUSH 0x80、分岐、関数アドレス、太さや大きさは触りません。新形状のみの fixture も検査します。GBK 化で全 12 箇所が `0x86`、CP932→GBK→CP1251→CP932 で両 EXE 完全復元。

50/53 比較の禁則表は j.exe `0x44d104 / 0x45aca6`、kagerou.exe `0x4499d4 / 0x455126`。ASCII 拡張三つは不変で、新意味は追加しません。字体変更だけでは ANSI API 全体は変わらず、実行環境のコードページが必要です。

## LWG の名前と XML

試料 `kagerou.exe` の `grps.xfl` には 46 個の空項目があり、23 個は Photoshop 形式の `</Layer set>` という名前です。
名前はアーカイブのデータであり、XML タグとして直接書かず、空項目も捨てません。

展開時は名前の `& < > " '` をエスケープし、梱包時は XML 実体を一段だけ復元します。
文字列としての `&lt;` を二重にデコードしません。画像検索には展開時と同じファイル名置換を使い、
LWG 内には元の名前を保存します。空項目の印、座標、Flag、順序も保持します。
壊れた XML は原アーカイブから再展開してください。

## 画像と音声の経路

| 動作 | j.exe | kagerou.exe |
| --- | --- | --- |
| LIM 下位 nibble version 2/3 | `0x43b770–0x43b79b` | `0x4375e0–0x43760b` |
| version 3 A 先行、0x10 で RGB | `0x43b7f0–0x43b844` | `0x437660–0x4376b4` |
| WCG 組/分離 channel | `0x43be00–0x43be82` | `0x437c70–0x437cf2` |
| BGR565 緑 key 0x07e0 | `0x43bab7` | `0x437927` |
| RIFF/WAVE + fmt | `0x46f100` 以降 | `0x46c790` 以降 |
| 圧縮音声 ACM open | `0x46f253` | `0x46c8e3` |

分岐は実装と一致。実 WCG は全て `0x0271`、新しい LIM サンプルなし。静的分岐確認は全形式テストではなく、PNG 出力成功は原エンジン PNG 対応を意味しません。全トップ XFL と内部 XFL/LWG、Jeanne 音声子アーカイブ七つ、Kagerou 五つ、thumbnail も調査しました。

Jeanne voice.xfl/0001.wav の Ogg は FFmpeg 全デコード済み。コンテナー検査・完全往復はゲーム内再生長、音、ACM 互換性を保証しません。新しい音声変換不具合は確定しませんでした。
