# Сборка и упаковка для Windows

[中文 / English](BUILDING.md) · [Главная страница](../README.md)

## MSYS2 UCRT64

Установите зависимости в терминале MSYS2 UCRT64 и выполните из корня репозитория:

```bash
pacman -S mingw-w64-ucrt-x86_64-{gcc,cmake,make,pkgconf,libadwaita,gettext-tools,libwebp,libvorbis,libogg,curl}
cmake -S . -B build -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build -j2
bash tools/bundle_windows.sh build build/windows-bundle /ucrt64
```

Каталог назначения ещё не должен существовать. Запускайте
`build/windows-bundle/bin/liarsofttool-gui.exe`. Передавайте весь каталог
`windows-bundle`, а не отдельный EXE.

Для сборки только CLI добавьте `-DBUILD_GUI=OFF`; GTK4/libadwaita тогда не нужны.
Скрипт упаковки GUI для такой сборки не используется.

## Кросс-компиляция из Linux

Подготовьте Windows-версии WebP, Vorbis/Ogg, libcurl 7.85 или новее и инструменты MinGW.
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

## Проверка миграции на GTK4

Рабочий GUI вызывает C API GTK4/libadwaita из C++. Отдельный тест проверяет API
для проверки создания окна, системной/светлой/тёмной темы и закрытия.
Преобразования файлов в тесте нет. Нужны GTK4 и libadwaita 1.4 или новее.

```bash
cmake -S tests/gtk4 -B build/gtk4-probe
cmake --build build/gtk4-probe -j2
ctest --test-dir build/gtk4-probe --output-on-failure
```

В Windows установите UCRT64-пакеты `gcc`, `cmake`, `pkgconf` и `libadwaita`
в отдельной среде MSYS2 UCRT64, затем выполните те же команды.
Не используйте старые библиотеки GTKmm из MINGW64.
