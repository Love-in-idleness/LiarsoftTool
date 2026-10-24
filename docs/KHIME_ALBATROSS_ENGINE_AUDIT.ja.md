# 開発資料: Khime / Albatross のリソース

[中文](KHIME_ALBATROSS_ENGINE_AUDIT.zh.md) | [English](KHIME_ALBATROSS_ENGINE_AUDIT.en.md) | [Русский](KHIME_ALBATROSS_ENGINE_AUDIT.ru.md) | [日本語](KHIME_ALBATROSS_ENGINE_AUDIT.ja.md)

[文書一覧](README.ja.md) · [利用ガイド](../README.ja.md)

2026-10-04 の字体、アーカイブ、画像、音声の静的解析です。原本は読み取りのみで起動・変更せず、変換は一時ディレクトリで行いました。結論は以下のビルドに限定します。

## ファイル識別

| ファイル | SHA-256 | PE FileVersion |
| --- | --- | --- |
| `KhimeKusaritop/KhimeDL.exe` | `2dfc7b78d32867716ab0406ccbf176b55a76faea1cb9c40feac25c90775222fe` | 1, 8, 5, 0 |
| `Albatross/信天翁航海録.exe` | `f04c9729c0f60269f6ffdc63ea71c587a2431c501ade698e374978050fd156c4` | 1, 9, 0, 0 |

両方 PE32/i386、ImageBase `0x400000`。アドレスは VA。リソース版は発売日を証明しません。同ディレクトリの中国語パッチ EXE は対象外です。Albatross の .text (`0x72000` bytes) と .rdata (`0xb000`) は res_ft/exe/kagerou.exe と同位置・同バイトです。[Kagerou の字体・禁則・音声資料](JEANNE_KAGEROU_ENGINE_AUDIT.ja.md)を適用できますが、素材やスクリプトが同一という意味ではありません。

## Khime の字体 charset

KhimeDL.exe は直接 CreateFontA IAT call が 11 箇所 (IAT `0x46302c`)。旧検出は 9 箇所のみで、二つの `0x80` が残りました。新符号化ではなく push 形状の差です。

| charset オペランド | call | 差異 |
| --- | --- | --- |
| `0x44f8de` | `0x44f8fb` | charset PUSH 前に SBB EDI,EDI。EBX height、EBP underline |
| `0x44fff0` | `0x450009` | EAX weight 経路で、EBX でなくゼロ化した EBP を push |

後者は `0x44ff4b` で EBP を消去。観測した完全な形状を照合し、4-byte IAT アドレスだけ可変にします。任意の PUSH 0x80、weight、size、underline、分岐、call target は変更しません。新形状のみの fixture でも検出と変換を検証しました。

- CP932→GBK は Khime 11 / Albatross 12 箇所すべてを `0x86`、CP1251 は `0xcc` にします。
- CP932→GBK→CP1251→CP932 で両実 EXE を完全復元。
- 既存禁則表: Khime 50 項目 `0x441c54`、Albatross 50/53 項目 `0x4499d4 / 0x455126`。新意味の追加なし。
- フォント charset はすべての ANSI API を変えません。適合するコードページか Locale Emulator が必要です。

## 画像、アーカイブ、音声

Khime LIM 検査は `0x42ee04`、32-bit decode `0x42ef10–0x42ef64` は A を先に、flag `0x10` で RGB を読みます。WCG signature `0x42f474`、channel `0x42f520–0x42f5a2`。`(flags & 0x1c0) == 0x40` は組チャンネル、それ以外は分離 Alpha/RGB。実装と一致しますが、実 WCG は全て `0x0271`、LIM サンプルなしで全形式の実検証ではありません。

圧縮音声は `0x45cf10` から WAVE/fmt と PCM を検査し、非 PCM は `0x45d063` から thunk 経由で acmStreamOpen。新しい音声不具合は確認しませんでした。PNG/Ogg 出力成功は原エンジン対応を意味しません。通常名の全トップ XFL と内部 XFL/LWG を再帰調査。Khime の実 script は末尾ドット付き `scr.xfl.` で、内容で XFL と判定して調査し、原名や CLI 拡張子規則は変更していません。

| 検査 | Khime | Albatross |
| --- | ---: | ---: |
| 通常名のトップ XFL | 10 | 10 |
| WCG デコード | 1845 | 1484 |
| LWG デコード・抽出・完全往復 | 15 | 20 |
| 空 LWG 項目保持 | 0 | 36 |
| ElementTree で読める .meta.xml | 15 | 20 |
| Ogg-in-WAV 抽出・格納・再抽出一致 | 3084 | 3961 |
| PCM 原状保持 | 0 | 140 |
| 失敗 | 0 | 0 |

Albatross の `</Layer set>` 名は XML escape で名前、空項目、座標、flags、順番、payload を保持します。KhimeKusaritop/voice.xfl/0001.wav と Albatross/voice.xfl/0015.wav の Ogg は FFmpeg 全デコード済み。ゲーム内の再生長、音、ACM 互換性は別途検証が必要です。
