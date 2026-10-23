# Документация

[中文](README.md) · [Главная](../README.ru.md)

## Использование и сборка

- [Обзор и работа с программой](../README.ru.md); справка CLI — `liarsofttool -h`.
- [Сборка и упаковка Windows](BUILDING.ru.md)
- [Справочник TSC, английский](GSC_OPCODE_REFERENCE.en.md)
- [Лицензии аудиобиблиотек](THIRD_PARTY_AUDIO_LICENSES.md)

## Разработка

- [План развития](ROADMAP.ru.md)
- [Интерфейс проверки TSC](TSC_DIAGNOSTICS.ru.md)
- [Правила работы](../AGENTS.ru.md)

Ниже технические материалы, пока на китайском. Для обычной работы они не нужны.
Адреса и выводы относятся только к указанным сборкам.

- [Аудиоконтейнеры](AUDIO_PROCESSING_AUDIT.md)
- [CannonBall](CANNONBALL_ENGINE_AUDIT.md)
- [Evermaiden](EVERMAIDEN_ENGINE_AUDIT.md)
- [Khime / Albatross](KHIME_ALBATROSS_ENGINE_AUDIT.md)
- [Jeanne / Kagerou](JEANNE_KAGEROU_ENGINE_AUDIT.md)
- [Пробная версия CNMI](CNMI_TRIAL_ENGINE_AUDIT.md)
- [RsComp.dll и медиакоманды](RSCOMP_MEDIA_AUDIT.md)

## Проверки

```bash
cmake --build build -j2
ctest --test-dir build --output-on-failure
git diff --check
```

После изменения GSC/TSC или медиавывода проверьте чтение и генерацию в
rscript2renpy. Тест преобразования не заменяет проверку оригинальной игрой.
