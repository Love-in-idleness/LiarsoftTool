# ビルドとパッケージ

[中文](BUILDING.zh.md) | [English](BUILDING.en.md) | [Русский](BUILDING.ru.md) | [日本語](BUILDING.ja.md)

[メインガイド](../README.ja.md)

コマンドはリポジトリ直下で実行します。CMake 3.10+、C++17 コンパイラー、WebP、
Vorbis/Ogg、uchardet 静的ライブラリ、libcurl 7.85+ が必要で、Unix では iconv も使います。
GUI には GTK4 4.10+、libadwaita 1.5+、gettext、pkg-config、
音声プレビューには GTK メディアバックエンドと GStreamer base/good プラグインが必要です。

## Linux

Debian / Ubuntu（パッケージの版が上記の要件を満たすこと）：

```bash
sudo apt install build-essential cmake pkg-config libwebp-dev libvorbis-dev libogg-dev libuchardet-dev libcurl4-openssl-dev libgtk-4-dev libadwaita-1-dev gettext gstreamer1.0-plugins-base gstreamer1.0-plugins-good
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j2
ctest --test-dir build --output-on-failure
```

プログラムは `build/liarsofttool` と `build/liarsofttool-gui` です。
CLI のみなら設定時に `-DBUILD_GUI=OFF` を追加し、GUI とプレビューの依存を省けます。
開発環境はビルド側に必要なもので、配布バイナリの利用者に一式を要求するものではありません。

## MSYS2 UCRT64

MSYS2 の UCRT64 ターミナルで依存ライブラリを導入し、リポジトリのルートで実行します。

```bash
pacman -S mingw-w64-ucrt-x86_64-{gcc,cmake,make,pkgconf,libadwaita,gettext-tools,libwebp,libvorbis,libogg,uchardet,curl,gstreamer,gst-plugins-base,gst-plugins-good}
cmake -S . -B build -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build -j2
bash tools/bundle_windows.sh build build/windows-bundle /ucrt64
```

パッケージの出力先は存在しないディレクトリにしてください。`windows-bundle` はランタイムをまとめる中間ディレクトリです。`bin/liarsofttool-gui.exe` で動作を確認できます。音声プレビュー用の GStreamer プラグインとスキャナーを GTK と一緒に残してください。

CLI のみなら構成時に `-DBUILD_GUI=OFF` を指定します。GTK4/libadwaita は不要で、GUI 用パッケージスクリプトは使用しません。

## Windows の単一 EXE 配布

[Enigma Virtual Box](https://www.enigmaprotector.com/en/downloads.html) を導入し、Python 3.9+ と GNU objdump で中間ディレクトリを CLI・GUI 別々にパッケージ化します。利用者側に封入ツールは不要です。

```bash
python tools/pack_windows.py build/windows-bundle build/windows-single "C:/Program Files/Enigma Virtual Box/enigmavbconsole.exe"
python tests/test_windows_pack.py
```

出力先は新規ディレクトリにします。`liarsofttool.exe` と `liarsofttool-gui.exe` を個別に配布し、両方をまとめた ZIP は作りません。圧縮と GUI のメディア子プロセス対応を有効にし、CLI は自分の依存ライブラリのみ封入します。

EXE の生成確認だけでなく、変換、GUI の四言語、設定保存、画像・音声プレビューを検証してください。Linux からの実行には Wine が必要です。Windows パスは `winepath` で変換します。

## Linux からクロスビルドする

Windows 用 WebP、Vorbis/Ogg、uchardet の静的ライブラリ、libcurl 7.85 以降と MinGW を用意します。`CMAKE_PREFIX_PATH` または CMake の各ライブラリ変数で対象ライブラリを指定し、Linux ホスト用ライブラリをリンクしないでください。

CLI の例:

```bash
cmake -S . -B build/win -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_SYSTEM_NAME=Windows -DBUILD_GUI=OFF \
    -DCMAKE_CXX_COMPILER=x86_64-w64-mingw32-g++-posix \
    -DCMAKE_PREFIX_PATH=/path/to/windows-dependencies
cmake --build build/win -j2
```

GUI には同じ ABI の GTK4/libadwaita 開発ファイルと pkg-config 設定も必要です。UCRT64 と MINGW64、異なるスレッドモデルなどを混在させないでください。
