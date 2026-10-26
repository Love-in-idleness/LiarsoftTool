# 開発資料: RsComp.dll とメディア命令

[中文](RSCOMP_MEDIA_AUDIT.zh.md) | [English](RSCOMP_MEDIA_AUDIT.en.md) | [Русский](RSCOMP_MEDIA_AUDIT.ru.md) | [日本語](RSCOMP_MEDIA_AUDIT.ja.md)

形式と特定の原プログラムに関する開発資料です。EXE の字体・禁則解析は EXE 変換機能の提供を意味せず、製品側の入口は無効です。

[文書一覧](README.ja.md) · [利用ガイド](../README.ja.md)

二版のコンパイラーの命令配置を扱います。メディアのデコードとは別です。ハッシュ、PE import/export、文字列、対象逆アセンブルを静的検査し、DLL はロード・実行していません。

## サンプル

| パス | bytes | PE timestamp | Compile RVA | SHA-256 |
| --- | ---: | --- | --- | --- |
| `CodeXRScript/OC用パッチ/data/RsComp.dll` | 364611 | 2008-07-22 | `0x124e` | `6c65c3545a559b6e4a01d54fdb2f57ad4e605b27dc73959cde18bd3468f3cb30` |
| `RsComp.dll (2012)` | 90112 | 2012-10-14 | `0x7090` | `0b1c3cdfc1587f576c30c09b3faa329e88e18fdaddfc0655d1ad398c1654a6a9` |

PE 時刻だけで発売日は確定しません。両方 x86 PE32、export は Compile のみ、静的 import は KERNEL32.dll と USER32.dll のみです。

## 確認できること: codec でなく命令コンパイル

ImageBase `0x10000000`、以下は VA です。

- 旧入口 `0x1000124e`→`0x10012220`→コンパイル。新 `0x10007090`→`0x10005270`、buffer 確保、`0x10001b60 / 0x10002510`、解放、return。
- 旧 `0x10008bc3` が bgm_on を照合し `0x3c`、`0x10008d85` が voice を照合し `0x42` と四つの式を出力。
- 新 `0x100039b7` が bgm_on を照合し `0x3c`、`0x10003ab7` は voice、`0x42`、最初は文字列、残りは式。命令名が同じでも世代間の引数配置は同じとは限りません。
- 新 load の文字列参照 `0x10003bc7`、draw `0x10003e06`、両方 parser `0x10002850`。共通 `0x10001000` は 16-bit を書いて code 長を 2 増やすだけで、画像デコードではありません。

## 断定しない範囲

音声 DLL の静的 import がないだけでは動的呼出を除外できないため、LoadLibraryA / GetProcAddress も調べました。

- 新 `0x1000c650` は user32.dll と GetActiveWindow/GetLastActivePopup。
- 旧 `0x10022040` も同じ window helper。`0x100019b4` は RsComp.dll、`0x100036da` は Compile を取得するコンパイラー wrapper。別の名前駆動ロードだけでは外部 module の機能を断定できません。
- OggS、RIFF、vorbis、.acm、.wcg、.lim、.png の codec signature/文字列なし。ACM、WinMM、GDI-image、codec import も見つからず、確認したコンパイル経路と合わせ、新 WAV/WCG/LIM codec は確認できませんでした。

これは未確認関数がすべてメディアと無関係という証明ではありません。音声は原 EXE の ACM/vorbis.acm、画像は WCG/LIM 関数を根拠にします。これら DLL は GSC/TSC の命令配置検証に役立ちますが、WAV コンテナーの完全復元の根拠ではありません。[音声の範囲](AUDIO_PROCESSING_AUDIT.ja.md)も参照してください。

## oc_reference/bgmsv の MP3 はゲーム形式か

実ファイルは ID3 付き MPEG Layer III ですが、oc_reference/bgmlist.html では bgmsv/TrackNN.mp3 は試聴欄に置かれています。CP932 の CodeXRScript/readme.txt はブラウザー用参考資料と説明し、導入一覧は画像、スクリプト、RsComp.dll、version.dat で bgmsv は含みません。原エンジンの MP3 再生対応は証明しません。

{Cannonball,Forest,kagerou}.exe は全て `%s\\\\Track%02d.wav` と `%s\\\\%04d.wav` を持ち、.mp3 や bgmsv はありません。Cannonball の参照は `0x0042c71c` と `0x004136ae` など。`0x00431f8f` の mpegvideo は別経路であり BGM/WAV の MP3 対応根拠ではありません。この資料では MP3 は文書の試聴素材で、同名 WAV 代替として確認したものではありません。拡張子変更や whitelist 拡大はせず、他エンジンは個別の根拠で判断します。
