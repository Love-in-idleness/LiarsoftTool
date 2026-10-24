# RScript Runtime

[中文](README.zh.md) | [English](README.en.md) | [Русский](README.ru.md) | [日本語](README.ja.md)

[逆解析の根拠](ENGINE_AUDIT.ja.md) · [DLL 開発](SDK.ja.md)

原ゲームで展開済みリソースと UTF-8 スクリプトを使う、実験的な Windows DLL 拡張です。ゲームを再実装せず、ディスクの EXE を変更せず、公式RsComp.dllの交換も要求しません。同じ形式・Windows API を使う CodeX RScript 向けで、ゲーム名・EXE hash whitelist・固定アドレスで選別しません。現在は32-bit Windowsを構築・検査し、Forest/SevenBridgeは回帰サンプルであって許可リストではありません。

## 使用開始

同じ場所に `RScriptLauncher.exe`、`RScriptHost.dll`、`kinsoku.json` と機能 DLL を置きます。ゲームディレクトリに置く必要はありません。

1. **Game executable** でゲーム EXE を選択。
2. **Override resources** で上書き資源場所を選択。空欄はゲーム場所。
3. 必要な DLL を選び **Start game**。

コピーでテストしてください。拡張が素材を変えなくてもゲーム自体はセーブを書き得ます。

| DLL | 用途 |
| --- | --- |
| RScriptFiles.dll | 名前付き API による同期ファイル読取 |
| RScriptResources.dll | 展開済み資源・現形式 TSC の優先装入 |
| RScriptUnicode.dll | CP932 外の文字を UTF-8 上書きスクリプトで使う |

Hostは必要時にロードし選択項目はありません。Resources/Unicodeを選ぶとFilesも選択し、Files解除で両者解除。全解除なら注入せず原ゲームを直接起動します。変更は次のゲーム起動から有効です。

**Add DLL...** の追加項目は既定オフ。信頼できるDLLだけ使用してください。全プロセスを読み書きでき、sandboxはありません。[開発説明](SDK.ja.md)を参照してください。Wiki DLLや実行中unloadは提供しません。

## 上書き資源

元の構造で `scr/2100.tsc`、`grpe/9001.webp`、`voice/0001.ogg`、`grps/tbox01/.meta.xml` 等を置きます。全ゲームをコピーする必要はありません。

| 種類 | 優先順 |
| --- | --- |
| 音声 | ogg > wav |
| スクリプト | tsc > gsc |
| 画像 | webp > png > lim > wcg |
| 複合部品 | .meta.xml を持つディレクトリ > lwg |

.meta.xmlと参照画像を一緒に保持し、位置・寸法・名前を決めます。上書きがなければ元の経路へ。選ばれた上位ファイルが壊れている・不足している場合はエラーにし、下位へ黙って戻りません。

必要時にメモリー内で変換し、原資源は書き換えません。TSCは現LiarsoftToolから出力したUTF-8で、ラベル・metadataを保持します。公式コンパイラーのinclude/macros等の高水準ソースには対応しません。

TSC自身のcontainerを保持し、原GSCがあれば単独/XFLからheader形式を比較して不一致は失敗。追加シーンに参照がない場合も対象形式を推測しません。

## UTF-8

UTF-8訳文にはResources・Unicode・Filesを同時に有効化します。原GSCはCP932。ResourcesのみならTSC文字もCP932で表現できる必要があります。

BMP内の通常の日中露文字等に対応し、emoji等の補助平面や一般的な複雑組版は未対応です。追加の異なる文字は最大1880slotで、原作使用slotの分だけ減ります。対応字形を持つインストール済み字体が必要です。

### 行頭・行末禁則

Unicode有効時はランチャー隣の [kinsoku.json](kinsoku.json) を読みます。no_line_startは `。）」` 等の行頭禁止、no_line_endは `（「` 等の行末禁止文字。一般的な日中句読点、小仮名、ASCII記号、方向付き引用符を含み、翻訳に合わせて変更できます。両組の重複は禁止、UTF-8・64KiB以下・BMP内です。

CLIの `--kinsoku "custom.json"` は別ファイルを指定し、明示したファイルは常に検査します。不足・不正ならゲームを起動しません。Unicode無効かつ引数なしならファイル不要。原改行判定へ接続し、本文に改行を挿入せず、Unicode UAX #14 全体の実装でもありません。

字形辞書:

```text
%LOCALAPPDATA%\LiarsoftTool\RScriptRuntime\<key>\glyphs.bin
```

旧セーブ・履歴を保つ間は辞書を消さないでください。移行時は辞書も保存し、ゲームと上書き場所を変えません。パス・EXE内容の変更は別辞書になります。シナリオ変更は旧セーブを壊す場合があります。同じ辞書を複数instanceで同時使用できません。

旧Forest専用DLLと新共通DLLを混ぜず、同じビルドの一式を使います。旧試作辞書は自動移行しません。詳細ログは `%LOCALAPPDATA%\LiarsoftTool\RScriptRuntime\host-<pid>.log`。辞書・ログはユーザーデータで、原ゲーム素材には書きません。

## 互換範囲

FilesはEXEの普通import表のCreateFileA/ReadFile/SetFilePointer/GetFileSize/CloseHandleを接続します。UnicodeはANSI GDIのglyph/text/measureとゲーム内CP932認識を使い、システムロケールを変えません。禁則分類器の完全比較群を識別できなければUnicodeを拒否し、32-bit橋接にはSSEが必要です。

必要API不足、壊れたimport、既存hook競合はエラー。delay import、動的取得関数、他DLL自身の読取、列挙、mapping、非同期、MCI動画は対象外です。別読取・描画protocolは共通対応の追加が必要で、初期化成功は全互換性を意味しません。

## ビルドと確認

ルートから別ビルドします。GTK/libcurl不要で、32-bit MinGWと同じABIの静的WebP/Vorbis/Oggが必要です。

```bash
cmake -S runtime -B build/runtime-win32 \
  -DCMAKE_SYSTEM_NAME=Windows \
  -DCMAKE_C_COMPILER=i686-w64-mingw32-gcc-posix \
  -DCMAKE_CXX_COMPILER=i686-w64-mingw32-g++-posix \
  -DCMAKE_FIND_ROOT_PATH=/path/to/windows-x86-dependencies \
  -DCMAKE_FIND_ROOT_PATH_MODE_INCLUDE=ONLY \
  -DCMAKE_FIND_ROOT_PATH_MODE_LIBRARY=ONLY \
  -DCMAKE_FIND_ROOT_PATH_MODE_PROGRAM=NEVER \
  -DBUILD_TESTING=OFF
cmake --build build/runtime-win32 -j2
```

Windows/Linuxライブラリや異なるarchitecture/runtimeを混ぜません。出力はbuild/runtime-win32、追加ログ例はRScriptLogExample.dll。主プロジェクトに `-DBUILD_RUNTIME=ON` で資源/Unicode共通テストも有効化できます。WindowsテストにはPython3とVorbis encoderが必要で、クロス時RSCRIPT_WINEを指定できます。ランチャー/DLLを作るのはWindowsのみです。

実験的機能です。自動テスト、Wine、Windows実機での完全プレイは別です。全シーン、セーブ、字体、他pluginの組合せは保証しません。ゲームは正当に入手してください。
