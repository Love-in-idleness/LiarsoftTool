# 文書一覧

[中文](README.zh.md) | [English](README.en.md) | [Русский](README.ru.md) | [日本語](README.ja.md)

[プロジェクトの説明](../README.ja.md)

## 利用とビルド

- [用途と使い方](../README.ja.md)。CLI のヘルプは `liarsofttool -h`。
- [Windows のビルドとパッケージ作成](BUILDING.ja.md)
- [TSC 命令リファレンス](GSC_OPCODE_REFERENCE.ja.md)
- [RScript Runtime の利用とビルド](../runtime/README.ja.md)
- [音声ライブラリのライセンス原文](THIRD_PARTY_AUDIO_LICENSES.md)

## 開発資料

- [開発計画](ROADMAP.ja.md)
- [TSC 検査インターフェース](TSC_DIAGNOSTICS.ja.md)
- [Runtime DLL インターフェース](../runtime/SDK.ja.md)
- [作業規約](../AGENTS.ja.md)

以下はエンジンとファイル形式の解析資料です。通常の利用では読む必要はありません。アドレスと結論は各文書に明記されたビルドだけに適用します。

- [音声コンテナー](AUDIO_PROCESSING_AUDIT.ja.md)
- [CannonBall](CANNONBALL_ENGINE_AUDIT.ja.md)
- [Evermaiden](EVERMAIDEN_ENGINE_AUDIT.ja.md)
- [Khime / Albatross](KHIME_ALBATROSS_ENGINE_AUDIT.ja.md)
- [Jeanne / Kagerou](JEANNE_KAGEROU_ENGINE_AUDIT.ja.md)
- [CNMI 体験版](CNMI_TRIAL_ENGINE_AUDIT.ja.md)
- [RsComp.dll とメディア命令](RSCOMP_MEDIA_AUDIT.ja.md)
- [Runtime の接続根拠](../runtime/ENGINE_AUDIT.ja.md)

## 開発時の確認

```bash
cmake --build build -j2
ctest --test-dir build --output-on-failure
git diff --check
```

GSC/TSC やメディア出力を変更した場合は rscript2renpy の読み取りと生成も確認してください。変換テストは原ゲーム内の検証を代替しません。
