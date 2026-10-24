# 開発資料: CannonBall のリソースとエンジン

[中文](CANNONBALL_ENGINE_AUDIT.zh.md) | [English](CANNONBALL_ENGINE_AUDIT.en.md) | [Русский](CANNONBALL_ENGINE_AUDIT.ru.md) | [日本語](CANNONBALL_ENGINE_AUDIT.ja.md)

[文書一覧](README.ja.md) · [利用ガイド](../README.ja.md)

2026-10-04 時点の LIM、アーカイブ、文字コードに関する静的解析記録です。新しいゲームエンジンの実装ではありません。対象は `res_ft/exe/Cannonball.exe`、SHA-256 は `df4b5b6dffc7002370d0772869baaba551b08fca970e92609ca64b08174f89ba`。アドレスは PE VA です。原プログラムは起動せず、原リソースも変更していません。結論はこのビルドに限定します。

## 形式と修正

| 項目 | 根拠・再現 | 修正 |
| --- | --- | --- |
| Alpha のみの LIM | `0x43bab6–0x43bac9`: A を先に読み、flag `0x10` の場合だけ RGB を読む | 存在しない色ブロックを読まず、RGB をゼロにして Alpha を保持 |
| LIM RGB565 | `0x43bd56–0x43bd89`: 5/6-bit 値をシフトし、255 まで比例拡大しない | 白は `(248,252,248)` など原エンジンに合わせる |
| RGB565 カラーキー | 同経路で `0x07e0`、`0x43be10` 以降で別 Alpha が優先 | 緑のキーを透明化し、別 Alpha を優先 |
| XFL 順序 | `0x41ab05–0x41ab43`: `lstrcmpiA` による二分探索 | 数値の自然順ではなく ASCII 大文字小文字を無視した辞書順。既存項目の再保存も同様 |
| 長い XFL 名 | 名前欄は 32 bytes。旧実装は切り捨てた | エンコード後 31 bytes 超、大小文字を無視した重複を拒否し、CP932 文字を分断しない |
| XFL/LWG 境界 | 切断入力を受け入れ、短い LWG ヘッダーで範囲外参照 | ヘッダー、表、項目、名前、位置、長さを検査。負の XFL オフセット、危険な出力パス、1-byte 長を超える LWG 名を拒否 |
| Windows 文字コード API | UTF-8 出力で禁止された flags と既定文字ポインターを使用 | UTF-8 専用の引数を使い、厳密入力と実際の API 成否を確認 |

必要な順序は `1.gsc, 10.gsc, 2.gsc` であり、`1.gsc, 2.gsc, 10.gsc` ではありません。4 桁固定の名前では問題が見えにくくなります。API の契約: [WideCharToMultiByte](https://learn.microsoft.com/en-us/windows/win32/api/stringapiset/nf-stringapiset-widechartomultibyte)、[MultiByteToWideChar](https://learn.microsoft.com/en-us/windows/win32/api/stringapiset/nf-stringapiset-multibytetowidechar)。API の不備が見つかっても、過去の Windows 障害の唯一の原因とは断定できません。[音声資料](AUDIO_PROCESSING_AUDIT.ja.md)も参照してください。

## 記録時点の制限と未検証範囲

| 機能 | 範囲 | 根拠 |
| --- | --- | --- |
| 元の LIM 配置 | LIM ↔ 可逆 WebP。書き戻しは version 3 の A/R/G/B 分離で、元の深度・圧縮方式は保持しない | `0x43ba30` が版分岐、`0x43ba70 / 0x43bab0` が 16-bit/分離経路。RGBA を保持し、他エンジンは実機確認 |
| MSK 編集往復 | .msk の保持・展開・格納は可能。MSK→PNG→MSK 専用変換なし | `0x43ac25` が .msk 追加、`0x43aca9` が BM、`0x43ace7–0x43acf7` が 1/8/24-bit BMP を受理。拡張子変更だけではマスクにならない |
| 公式の高水準 TSC | 構造化バイトコードのコンパイラーで、RsComp ソース完全互換ではない | `0x40e38f–0x40e39d` で Compile、`0x44f274/0x44f280` は TSC 名。公式 #include・%title に対し、本パーサーは命令・ラベルを扱う |
| 工程全体の依存関係 | ファイル変換時に参照先の全画像・音声・スクリプト・動画は検査しない | 依存グラフ、不足一覧、動的参照の不確実性表示はない |
| 非 ASCII の整列 | ASCII は対応。Windows NLS の完全再現ではない | 原作の lstrcmpiA と UTF-8 名の ASCII case folding は日中露で同値とは限らない |
| 音声変換・リサンプリング | 解析当時は PCM 保持と Ogg 抽出・格納のみ。再圧縮・リサンプリング・チャンネル修正なし | PCM と ACM は別経路。現行の音声資料を参照。格納は再圧縮ではなく、不適合な参照パラメーターは失敗させる |
| 動画 | MPG→WebM 入口なし。既存のアーカイブ資源一覧にも含めない | `0x431f67` で mciSendCommandA、`0x431f8f–0x431f9b` で mpegvideo。Ren'Py 再生は移植側 |

最初の四項目はリソースツールの拡張候補です。再生、効果、メニュー、セーブ実行は rscript2renpy または原エンジンの担当で、変換器のバグとは限りません。2003 年の EXE で後年の全 opcode の意味は確定できません。依存検査、編集可能な MSK、元 LIM 配置保持の順を提案します。高水準コンパイラーは別の大きな作業であり、構造化 TSC でも編集と再コンパイルは可能です。
