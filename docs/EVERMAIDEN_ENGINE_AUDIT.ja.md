# 開発資料: Evermaiden のリソースとエンジン

[中文](EVERMAIDEN_ENGINE_AUDIT.zh.md) | [English](EVERMAIDEN_ENGINE_AUDIT.en.md) | [Русский](EVERMAIDEN_ENGINE_AUDIT.ru.md) | [日本語](EVERMAIDEN_ENGINE_AUDIT.ja.md)

[文書一覧](README.ja.md) · [利用ガイド](../README.ja.md)

音声、字体、句読点、GSC ロードの静的解析記録です。結論は各ビルドに限定します。2026-10-04 の対象は公式日本語 EXE ではなく、提供された中国語パッチ `Evermaiden/startup_CHS.exe` です。パッチの挙動を公式全体へ一般化しません。

- PE32/x86、1,016,832 bytes、ImageBase `0x400000`。
- SHA-256: `2dd8004933ae3943485f825c4b88ccea53301e0f68f331a1440a17aeb6106ee5`。
- アドレスは PE VA。原ゲームの起動・原ディレクトリへの書き込みはせず、一時コピーで変換。

## 1. Vorbis mode 3 WAV の検出漏れ

全 13 XFL を調べ、`wav.xfl/2559.wav` が未対応でした。66-byte のラッパー後に OGG を含みますが、tag は `0x6771` でなく `0x6751` です。[VLC 定義](https://github.com/videolan/vlc/blob/master/include/vlc_codecs.h)では Vorbis mode 3 / mode 3 plus。fmt 長 26、拡張長 8、stereo 44100 Hz、fact 843757、data 長 250668、OggS は offset 66 です。

ヘッダー、ページ、パラメーター検査を保持し、確認済み tag のみ追加しました。書き戻しは参照元の tag と codec 拡張を保持し、OGG から長さ・サンプル数・padding を再計算します。未確認 mode は追加しません。抽出→書き戻し→抽出で同一 OGG、FFmpeg 全デコード後の PCM SHA-256 は `44f63c38a8646a87b4dbc65148c0f3db2bdcc6822d905d7e191a013fcd0ff1ce`。tag 保持と未対応 `0x6750` 拒否をテストしました。コミット `2f88429`。

## 2. 最適化された禁則表の検出漏れ

旧検出は CMP SI, imm16 のみ。二つの表は MOV EAX, imm32 / CMP SI, AX / JE を使い、CP932 の行頭禁則 38・行末禁則 12 文字です。

- 表 1: `0x457961`、続いて `0x213f`、`0x3f21`、CMP SI, BX の拡張。行末は `0x457b9a`。`0x45786b–0x457875` で BX=`0x2121`。
- 表 2: `0x45d248`。三つの拡張なし。行末は `0x45d45c`。
- 表 1 の行末最初の二 JE、表 2 の最初の JE は near、残りは short。
- 分類代入先は表 1 `0x457c54 / 0x457c37`、表 2 `0x45d508 / 0x45d4f0`。

既知文字列全体、ゼロ拡張定数、比較、JE 構造を照合します。変更するのは文字オペランド 50 箇所のみで、ASCII 接頭部、拡張、命令、分岐を保持します。font charset が GBK でも CP932 表を検査し、部分パッチを完全変換済みと誤認しません。

## 3. フォント引数の交錯

CreateFontA の 10 箇所は `0x86` でしたが、`0x46d738`、`0x46da28`、`0x46e4eb` は `0x80` のままでした。引数 push の間にポインター読み取り・weight 計算が入り、連続パターンでは検出できません。`0x46e4df` が `0x4c8024` から CreateFontA を読み、`0x46e507` が ESI を call します。観測した呼び出し全体を照合し、任意の PUSH 0x80 を置換しません。

EXE 修正二件は `1d6f88e`。実コピーの GBK→CP1251→CP932 で両表とフォント 13 箇所を独立確認し、他のバイトは不変でした。原本との差は 201 / 213 / 10 bytes。最後の 10 bytes は元から GBK だったため、一様 CP932 と元の混合ファイルは一致しません。

## その他の経路

- WCG: header `0x478150`、版分岐 `0x4781ff`、組チャンネル `0x478222 / 0x478233`、Alpha 先行 `0x478244`、任意 RGB `0x478250`。実装と一致。
- palette: 4096 境界 `0x47b84d–0x47b85b`、索引 `0x47a700` は既存修正と一致。実 WCG はすべて `0x0271` で、全形式の検証ではない。
- LIM: `0x4786df` の版分岐は CannonBall と一致。実 LIM サンプルなし。
- XFL: `0x47629b–0x4762f8` は lstrcmpiA の二分探索。ASCII 辞書順と合うが、非 ASCII と Windows NLS の同値は保証しない。
- GSC: `0x404faf` 付近でロードし、`0x405458` の保存に 36-byte header を含む。GBK 208 本は whole-file gsc-raw なしで構造化往復。

## 制限

`0x44dc33–0x44dc35` などは CP_ACP の IsDBCSLeadByteEx を使用します。字体・禁則表の変更はシステムコードページを変更しません。適合する Windows ロケールまたは Locale Emulator が必要です。コードページをまたぐ実行は文字/API 経路の追加分析が必要で、フォント交換や全 PUSH 0 置換では解決しません。原画面、禁則、再生、Windows 実機は別途確認が必要です。

## 2026-10-09: 公式日本語 GSC のロードと実行

別対象 `Evermaiden/startup_jp.exe`、SHA-256 `88820f28b8f770b486407e55dda28266c6fec42145192d5bc8ad5be21ab895af`。PE32、ImageBase `0x400000`。起動・変更なし。以下は VA です。

| 経路 | バイナリーの根拠 | 修正 |
| --- | --- | --- |
| GSC ローダー | `0x404ee0–0x40526a` が header 9 words、code、文字列索引/本文、data 索引/16-bit words を読む。`0x4051ab–0x4051dd` は header[7] bytes の表二つと header[8] bytes の名前を読む | 名前付き実行入口であり、デバッグ専用ではない |
| 名前付き入口 | `0x404e00–0x404e51` が slot 0 を除き、第一表の名前位置から第二表の code 位置を返す。jump は `0x43df90` | 入口ラベルを出力し、編集後 trailer を再配置。不足ラベルを拒否 |
| ローカル呼出 | opcode 200 の `0x43613a–0x4361ab` が D target と 10 式を読み、戻り位置保存。`0x43dea0`→`0x405760` で code 長以上を拒否し code base+target | insub の先頭引数を検査・再配置するラベルにする |
| data block | `0x4361f3 → 0x43d4e0 → 0x404d50`: 索引ゼロなら戻り、他は索引表を使う。`0x404d6e` の MOVSX で個数を int16 符号拡張 | 過大個数・表外参照を拒否し、ゼロの no-copy を許可 |

日本語 0001.gsc の TOP1/click/TOP2 は `0xa6 / 0x25c / 0x7da`、2190.gsc の REP002 は `0xd9c`。未編集往復では古いオフセットが隠れるため、入口前の命令長を変えて検査します。名前、slot 0、余白を保持し、code/文字列全体のバックアップは追加しません。非空入口や数値 insub を持つ旧 TSC は再出力してください。

日本語 208 本・現中国語 205 本の構造化往復と ASan/UBSan に合格し、実入口前に wait 1 を挿入すると target は 6 増えました。回帰は CannonBall JP 626 / ZH 437、Forest 103、Khime 341、Albatross 91。CLI/GTK を再構築し当時の 8 CTest が通過。合計 413 Evermaiden の各 corpus は入口 10、insub は 6385 / 6103 箇所です。rscript2renpy のラベル読取・変換とテストも更新し、素材・生成工程は変更していません。非 ASCII 入口名の再符号化、不明 opcode の意味、全ルートはこの根拠に含みません。
