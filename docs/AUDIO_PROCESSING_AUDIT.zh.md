# 开发参考：音频封装

[中文](AUDIO_PROCESSING_AUDIT.zh.md) | [English](AUDIO_PROCESSING_AUDIT.en.md) | [Русский](AUDIO_PROCESSING_AUDIT.ru.md)

[返回文档目录](README.zh.md)

说明 WAV/OGG 的处理规则及 CannonBall 原程序依据。
日常转换用法请运行 `liarsofttool -h` 查看软件内帮助。

## 转换规则

| 输入 | 输出 |
| --- | --- |
| 含完整 Vorbis 流的 WAV | 提取 OGG |
| 标准 PCM WAV | 原样保留 |
| OGG | 默认解码为 16 位 PCM WAV |
| OGG 加 `--vorbis-in-wav` | 保留压缩流，生成 WAV 容器 |

PCM 输出保留采样率及单/双声道，不重采样。
Vorbis-in-WAV 按源音频参数重建容器，不需要参考 WAV。
PCM 不能还原原压缩字节，重建 WAV 也不要求与原容器字节一致。

读取按 RIFF 块定位，不能假定 Ogg 永远从第 66 字节开始。
完整流支持 Vorbis mode 1/1+/3/3+；
mode 2 的分离头不作为完整 Ogg 处理。
检查页边界、序列、CRC 和 EOS，编辑元数据不混入提取音频。

## 原程序依据

`Cannonball.exe`，SHA-256：
`df4b5b6dffc7002370d0772869baaba551b08fca970e92609ca64b08174f89ba`。
以下为该 PE 的虚拟地址。

| 地址 | 行为 |
| --- | --- |
| `0x41257a` | 加载 vorbis.acm 并注册 ACM 驱动 |
| `0x4411fe–0x441221` | 识别 PCM，定位 data 块 |
| `0x4414b0–0x44153e` | 读取压缩源格式，构造目标 PCM 参数 |
| `0x441573` | 打开 ACM 转换流 |
| `0x441678–0x441682` | 定位压缩 data 块 |
| `0x441862` | 执行音频转换 |

同名 WAV 扩展名可能包含不同音频格式，必须依据实际容器处理。
RsComp.dll 的媒体指令分析见 [DLL 参考](RSCOMP_MEDIA_AUDIT.zh.md)。

## 维护与检查

实现位于 `src/wav_ogg.cpp`，测试位于 `tests/wav_ogg_test.cpp`。
检查提取与封装、PCM 解码、异常输入及递归处理。
原游戏对生成容器的接受程度仍需游戏内确认。
