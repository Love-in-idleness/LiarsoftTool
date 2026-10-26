# 文档目录

[中文](README.zh.md) | [English](README.en.md) | [Русский](README.ru.md) | [日本語](README.ja.md)

[返回主说明](../README.zh.md)

## 使用与构建

- [项目介绍与使用](../README.zh.md)；命令行帮助运行 `liarsofttool -h`。
- [源码构建与打包](BUILDING.zh.md)
- [TSC 指令参考](GSC_OPCODE_REFERENCE.zh.md)
- [VS Code TSC 插件](../tsc-vscode/README.zh.md)
- [音频依赖许可证](THIRD_PARTY_AUDIO_LICENSES.md)

## 开发参考

- [开发规划](ROADMAP.zh.md)
- [TSC 检查接口](TSC_DIAGNOSTICS.zh.md)
- [工作约定](../AGENTS.zh.md)

以下为引擎和文件格式的分析依据，普通使用无需阅读。
地址和结论只适用于文中标明的构建。

- [音频封装](AUDIO_PROCESSING_AUDIT.zh.md)
- [CannonBall](CANNONBALL_ENGINE_AUDIT.zh.md)
- [Evermaiden](EVERMAIDEN_ENGINE_AUDIT.zh.md)
- [Khime / Albatross](KHIME_ALBATROSS_ENGINE_AUDIT.zh.md)
- [Jeanne / Kagerou](JEANNE_KAGEROU_ENGINE_AUDIT.zh.md)
- [CNMI 体验版](CNMI_TRIAL_ENGINE_AUDIT.zh.md)
- [RsComp.dll 与媒体指令](RSCOMP_MEDIA_AUDIT.zh.md)

## 开发检查

```bash
cmake --build build -j2
ctest --test-dir build --output-on-failure
git diff --check
```

修改 GSC/TSC 或媒体输出时，同时检查 rscript2renpy 的读取和生成。
转换测试不替代原游戏内验证。
