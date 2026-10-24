# 開発資料: 音声コンテナー

[中文](AUDIO_PROCESSING_AUDIT.zh.md) | [English](AUDIO_PROCESSING_AUDIT.en.md) | [Русский](AUDIO_PROCESSING_AUDIT.ru.md) | [日本語](AUDIO_PROCESSING_AUDIT.ja.md)

[文書一覧](README.ja.md)

WAV/OGG の処理規則と CannonBall における根拠です。通常の変換は `liarsofttool -h` を参照してください。

## 変換規則

| 入力 | 出力 |
| --- | --- |
| 完全な Vorbis ストリームを含む WAV | OGG を抽出 |
| 標準 PCM WAV | 変更せず保持 |
| OGG | 既定では 16-bit PCM WAV にデコード |
| OGG と `--vorbis-in-wav` | 圧縮データを WAV に格納 |

PCM のサンプルレートとモノラル／ステレオを保持し、リサンプリングはしません。Vorbis-in-WAV は参照 WAV なしで入力音声のパラメーターから作成します。PCM から元の圧縮バイト列は復元できず、再生成 WAV が元のコンテナーと完全一致するとは限りません。

Ogg が常にオフセット 66 にあると仮定せず、RIFF チャンクをたどります。完全なストリームを持つ Vorbis mode 1/1+/3/3+ に対応し、ヘッダーが分離した mode 2 は完全な Ogg として扱いません。ページ境界、連番、CRC、EOS を検査し、編集ソフトのメタデータを音声に混入させません。

## 原エンジンの根拠

`Cannonball.exe`、SHA-256:
`df4b5b6dffc7002370d0772869baaba551b08fca970e92609ca64b08174f89ba`。
アドレスは PE 仮想アドレスです。

| アドレス | 動作 |
| --- | --- |
| `0x41257a` | vorbis.acm のロードと ACM ドライバー登録 |
| `0x4411fe–0x441221` | PCM 判定と data 検索 |
| `0x4414b0–0x44153e` | 圧縮形式の読み取りと PCM 出力設定 |
| `0x441573` | ACM 変換ストリームを開く |
| `0x441678–0x441682` | 圧縮データの位置取得 |
| `0x441862` | 音声変換 |

同じ WAV 拡張子でも内部形式は異なります。[RsComp.dll の資料](RSCOMP_MEDIA_AUDIT.ja.md)はメディア命令の根拠です。

## 保守と確認

実装は `src/wav_ogg.cpp`、テストは `tests/wav_ogg_test.cpp`。抽出・格納、PCM デコード、破損入力、再帰処理を確認します。生成コンテナーを原ゲームが受け入れるかはゲーム内で別途確認してください。
