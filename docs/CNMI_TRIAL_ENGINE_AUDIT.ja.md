# 開発資料: CNMI 体験版のリソース

[中文](CNMI_TRIAL_ENGINE_AUDIT.zh.md) | [English](CNMI_TRIAL_ENGINE_AUDIT.en.md) | [Русский](CNMI_TRIAL_ENGINE_AUDIT.ru.md) | [日本語](CNMI_TRIAL_ENGINE_AUDIT.ja.md)

[文書一覧](README.ja.md) · [利用ガイド](../README.ja.md)

2026-10-04 の空 LWG 項目、データ長、EXE 文字コードの記録です。対象は `0_Package/ちんまい吸血姫体験版.exe`、SHA-256 `21db7a449d329fb6bb030b1c1cf4050b0f519a52bd19167ba0d27dcc4cd6740e`。`res_ft/CNMI/` と `CNMI_trial/ちんまい吸血姫_体験版/` の EXE とバイト単位で同一です。ゲームは起動せず、原 EXE、素材、セーブを変更せず、一時ディレクトリへ出力しました。

## Evermaiden との関係

`startup_CHS.exe` とサイズ、PE ヘッダー、セクション、コンパイル時刻、PDB 識別子が一致します。.text は字体 charset `0x86/0x80` の 10 bytes だけ異なりますが、文字列、初期データ、PE リソースも異なるためファイル全体は同一ではありません。禁則、WCG/LIM、XFL、GSC、音声に新しいコード分岐はなく、最適化された禁則表とフォント引数の検出を適用できます。

CP932→GBK→CP1251→CP932 で原 EXE に完全復元します。別途確認した差分は禁則表二つとフォント引数 13 箇所だけで、GBK/CP1251 コピーは 211/213 bytes 異なります。

`0x44dc33–0x44dc35` などで `IsDBCSLeadByteEx(CP_ACP, ...)` を引き続き使用します。EXE 変換はシステムコードページや内蔵 UI 翻訳を変更しません。適合する環境が必要です。[Evermaiden の資料](EVERMAIDEN_ENGINE_AUDIT.ja.md)も参照してください。

## 1. データ長ゼロの LWG 項目が消える問題

`grps.xfl/compane.lwg` は 32 項目です。空の `slide_lev_f` と `slide` は `(0,0)`、Flag 8。別の非空 `slide` は `(690,4)`、`12×18` です。同名画像で空項目を埋めてはいけません。

旧抽出は空項目を .meta.xml に書かず、再作成で 30 項目になりました。共通抽出は `empty="1"` で名前、座標、flags、順番をすべて保持し、画像を捏造しません。作成側もこの印を読み、画像を探しません。空項目だけのアーカイブは依然として拒否し、空パッケージは生成しません。

`0x475722–0x47578c` は宣言された全レコードを読み、`0x427af2` は `slide_lev` を検索、`0x427b7c` は `slide` をコントロールに関連付けます。メニューデータとしての役割は分かりますが、Flag=8 全般や空項目の画面効果は確定しません。

rscript2renpy の `port_template/grps_layout.py` も `empty="1"` を尊重し、空の同名項目で `(690,4)` が `(0,0)` に置き換わる問題を共通層で防ぎます。失われたメタデータは原 LWG から再展開が必要です。コミット: LiarsoftTool `3d8d4e8`、rscript2renpy `633eb4c`。

## 2. データ総長をゼロで書く問題

テーブル直後の 4 bytes は余白ではなく総データ長です。compane.lwg では `0x2fd` に 30231 (`0x7617`)、ファイル長 31000、データ開始 `0x301` から正確に 30231 bytes あります。

旧作成器はゼロを固定で書いていました。修正後は結合 payload の長さを書き、空項目の寄与はゼロです。ローカル GARbro `ArcFormats/Liar/ArcLWG.cs` もこの値を使い、EXE は `0x475979–0x475981` で読みます。これだけでゼロがクラッシュ原因とは証明できません。デコーダーは従来のゼロ値も受け入れます。体験版の全 12 LWG は元 payload で完全往復し、compane の空二項目も保持しました。コミット: `81f48f2`。
