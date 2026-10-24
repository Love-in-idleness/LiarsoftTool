# 開発資料: Jeanne / Kagerou のリソース

[中文](JEANNE_KAGEROU_ENGINE_AUDIT.zh.md) | [English](JEANNE_KAGEROU_ENGINE_AUDIT.en.md) | [Русский](JEANNE_KAGEROU_ENGINE_AUDIT.ru.md) | [日本語](JEANNE_KAGEROU_ENGINE_AUDIT.ja.md)

[文書一覧](README.ja.md) · [利用ガイド](../README.ja.md)

2026-10-04 の字体、LWG 名、メディアの静的解析です。原本は起動・変更せず、確認済みの字体機械語形状と LWG 名処理のみ共通変換器に追加しました。

## 対象

| ファイル | SHA-256 | PE FileVersion |
| --- | --- | --- |
| `res_ft/exe/j.exe` | `7cb856bdbc5c61d9606734b30fd83bac880a3eff9829112f0ddf203e3026c4a6` | 1, 11, 0, 3 |
| `res_ft/exe/kagerou.exe` | `f02998fae350945f4de4dc7eab47997e1e2d9b8aa43d49740a80aad893dcde9b` | 1, 9, 0, 0 |

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

## LWG 名と XML

Kagerou の grps.xfl に空項目 46、うち 23 は Photoshop 形式の `</Layer set>` 名です。`empty="1"` は保持されていましたが、未 escape 名が cgview、excompane、extrmenu、fontcompane、scview の .meta.xml を壊し、ElementTree で読めませんでした。

共通抽出は `& < > " '` を escape、作成器は五つの XML entity を一度だけ戻します。文字列 &lt; を二重デコードしません。画像検索には抽出側と同じファイル名置換を使い、LWG には原名を保持します。両ゲーム全 32 LWG は空項目、座標、flags、順を含めて完全往復し、全 XML が読めます。既存の壊れた XML は原アーカイブから再抽出してください。この調査で本番 Ren'Py 工程は上書きしていません。

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

| 検査 | Jeanne (トップ XFL 16) | Kagerou (9) |
| --- | ---: | ---: |
| WCG decode | 2431 | 1462 |
| LWG decode・往復 | 10 | 22 |
| Ogg-in-WAV 抽出・格納・再抽出一致 | 4551 | 3698 |
| PCM 原状保持 | 115 | 0 |
| 失敗 | 0 | 0 |

Jeanne voice.xfl/0001.wav の Ogg は FFmpeg 全デコード済み。コンテナー検査・完全往復はゲーム内再生長、音、ACM 互換性を保証しません。新しい音声変換不具合は確定しませんでした。
