# Сборка и упаковка

[中文](BUILDING.zh.md) | [English](BUILDING.en.md) | [Русский](BUILDING.ru.md) | [日本語](BUILDING.ja.md)

[Основное руководство](../README.ru.md)

Выполняйте команды из корня репозитория. Нужны CMake 3.10+, компилятор C++17, WebP,
Vorbis/Ogg, статическая библиотека uchardet, libcurl 7.85+; в Unix также iconv.
Для GUI дополнительно нужны GTK4 4.10+, libadwaita 1.5+, gettext и pkg-config.
Аудиопросмотр требует медиабэкенд GTK и плагины GStreamer base/good.

## Linux

Debian / Ubuntu с репозиториями, содержащими указанные минимальные версии:

```bash
sudo apt install build-essential cmake pkg-config libwebp-dev libvorbis-dev libogg-dev libuchardet-dev libcurl4-openssl-dev libgtk-4-dev libadwaita-1-dev gettext gstreamer1.0-plugins-base gstreamer1.0-plugins-good
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j2
ctest --test-dir build --output-on-failure
```

Программы находятся в `build/liarsofttool` и `build/liarsofttool-gui`.
Для одного CLI при настройке добавьте `-DBUILD_GUI=OFF`; зависимости GUI и просмотра можно исключить.
Средства разработки нужны машине сборки, а не каждому пользователю готового выпуска.

## MSYS2 UCRT64

Установите зависимости в терминале MSYS2 UCRT64 и выполните из корня репозитория:

```bash
pacman -S mingw-w64-ucrt-x86_64-{gcc,cmake,make,pkgconf,libadwaita,gettext-tools,libwebp,libvorbis,libogg,uchardet,curl,gstreamer,gst-plugins-base,gst-plugins-good}
cmake -S . -B build -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build -j2
bash tools/bundle_windows.sh build build/windows-bundle /ucrt64
```

Каталог назначения ещё не должен существовать. Это промежуточный каталог библиотек;
для проверки запускайте `build/windows-bundle/bin/liarsofttool-gui.exe`.
Сохраняйте плагины GStreamer и их сканер: они нужны для предпросмотра аудио.

Для сборки только CLI добавьте `-DBUILD_GUI=OFF`; GTK4/libadwaita тогда не нужны.
Скрипт упаковки GUI для такой сборки не используется.

## Однофайловые выпуски Windows

Установите [Enigma Virtual Box](https://www.enigmaprotector.com/en/downloads.html).
Python 3.9+ и GNU objdump нужны для упаковки промежуточного каталога в отдельные EXE для CLI и GUI.
Пользователю устанавливать упаковщик не требуется.

```bash
python tools/pack_windows.py build/windows-bundle build/windows-single "C:/Program Files/Enigma Virtual Box/enigmavbconsole.exe"
python tests/test_windows_pack.py
```

Каталог вывода ещё не должен существовать. Публикуйте `liarsofttool.exe` и `liarsofttool-gui.exe`
отдельно, а не одним ZIP. Включены сжатие и поддержка дочерних медиапроцессов GUI;
CLI содержит только собственные библиотеки. После упаковки проверьте преобразование,
четыре языка, изображения и звук: самого появления EXE недостаточно.

В Linux тот же скрипт работает через установленный Wine; пути преобразует `winepath`.

## Кросс-компиляция из Linux

Подготовьте Windows-версии WebP, Vorbis/Ogg, статическую библиотеку uchardet, libcurl 7.85 или новее и инструменты MinGW.
Задайте расположение целевых библиотек через `CMAKE_PREFIX_PATH` либо параметры
поиска библиотек CMake. Не связывайте программу с библиотеками Linux.

Пример для CLI:

```bash
cmake -S . -B build/win -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_SYSTEM_NAME=Windows -DBUILD_GUI=OFF \
    -DCMAKE_CXX_COMPILER=x86_64-w64-mingw32-g++-posix \
    -DCMAKE_PREFIX_PATH=/path/to/windows-dependencies
cmake --build build/win -j2
```

GUI также требует соответствующих библиотек GTK4/libadwaita и конфигурации pkg-config.
Компилятор и библиотеки должны использовать один целевой ABI и CRT.
Не смешивайте UCRT64/MINGW64 или разные модели потоков.
