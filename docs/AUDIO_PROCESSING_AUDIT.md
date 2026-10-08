# CannonBall 音频处理核对

核对日期：2026-10-04。原程序只做静态分析，未启动游戏。

## 原程序证据

分析文件：`res_ft/exe/Cannonball.exe`，SHA-256：
`df4b5b6dffc7002370d0772869baaba551b08fca970e92609ca64b08174f89ba`。
以下均为该 PE 的虚拟地址，不是文件偏移。

| 地址 | 已确认行为 |
| --- | --- |
| `0x41257a` 附近 | 加载 `vorbis.acm`、取得 `DriverProc` 并注册 ACM 驱动 |
| `0x4411fe–0x441221` | PCM 路径要求 `fmt.wFormatTag == 1`，查找 `data` 块 |
| `0x4414b0–0x44153e` | 压缩路径排除 PCM，从源 `fmt` 复制声道、采样率、位深以构造目标 PCM 格式 |
| `0x441573` | 将源/目标格式传给 `acmStreamOpen` |
| `0x441678–0x441682` | 查找压缩音频的 `data` 块 |
| `0x441862` | 调用 `acmStreamConvert` |

因此 WAV 扩展名不能决定编码方式：普通 PCM 和 Ogg-in-WAV 必须分开处理，
源 `fmt` 也不能与内部 Ogg 的声道、采样率不一致。

## 已复现并修复

1. **PCM 模板误用。** 旧实现无条件复制参考文件前 66 字节，再追加 Ogg。
   使用 CannonBall 的 PCM `bgm/Track01.wav` 作模板，输出被识别成
   44.1 kHz 双声道 PCM，但实际音频数据是 Ogg 字节。
   现在只接受已验证的 `0x6771`、26 字节 `fmt` / 4 字节 `fact` 封装头；
   不符合条件先报错，不覆盖目标。递归封包沿用警告、跳过并排除旧目标的规则。
2. **长度未随替换音频更新。** 原 `voice/0001.wav` 的 `fact` 是 176060，
   替换为 0.4 秒、44.1 kHz 的 Ogg 后仍为旧值。现在从新流的 EOS granule
   重建采样数（该例为 17640）。**这证明字段错误，不证明原引擎因此截断播放**；
   上述 EXE 路径未见 `fact` 决定播放长度。
3. **RIFF 总长度漏算奇数数据的对齐字节。** 现在 RIFF 长度包含末尾填充，
   `data` 长度只计 Ogg 本身。
4. **输入缺乏校验。** 封装时检查 Vorbis identification、声道/采样率匹配、
   页边界、CRC、页序列和 EOS；拒绝空输入、截断、非 Vorbis、串接/复用流，
   不自动重采样，也不实现新的音频编码器。
5. **签名误判和特殊序列号。** 不再只凭偏移 66 的 `OggS` 识别封装，避免
   PCM 样本巧合匹配；去除 `0xffffffff` 空填充页时，保留同序列号的真实音频页。

## 验证边界

- CannonBall：13303 个 Ogg-in-WAV、31 个 PCM WAV；Forest：6307 个 Ogg-in-WAV。
  共 19610 个嵌入音频完成提取→封装→提取，Ogg 字节完全一致，`fact` 与原资源一致；
  PCM 正确识别为无需提取。原游戏资源未改写。
- 抽查 CannonBall 语音 `0001`、`1238`、`3823` 和音效 `0001`、`0002`、`0003`：
  FFmpeg 完整解码成功，往返前后 PCM 长度和 SHA-256 一致。
- 覆盖包含 `LIST`、`cue ` 尾部块的资源：这些编辑元数据不进入提取的 Ogg；
  封装仍只继承 66 字节格式头，不承诺 WAV 容器逐字节还原。
- Linux CTest 7 项通过；音频测试通过 ASan/UBSan；Windows CLI/GUI 交叉编译，
  Windows 音频测试在隔离 Wine 环境通过。
- 以上不等于原游戏内播放、听感或原版 32 位 ACM 驱动的运行验收，后者仍由使用者确认。

日常回归：`cmake --build build -j 4`，然后 `ctest --test-dir build --output-on-failure`。
音频单元测试同时为目录递归测试生成合成容器，避免继续用无效 WAV/Ogg 数据充当成功样本。

## 2026-10-08：实验性回写开关

两版 `RsComp.dll` 的媒体相关路径属于脚本编译，没有找到新的音频/图像编解码实现，
见 [DLL 核对](RSCOMP_MEDIA_AUDIT.md)。OGG→WAV 暂改为默认关闭的实验性功能：
GUI 勾选 `OGG → WAV (experimental)`，CLI 使用 `--experimental-ogg-to-wav`。
开关传递至递归封包的每一层；关闭时警告并跳过 OGG，但保留、收录已有 WAV；
开启后的参数校验及失败旧目标排除规则不变。WAV→OGG 和普通 PCM 保留不受影响。
这不是新的编码器，也不承诺容器逐字节还原；原游戏播放仍需用户验收。
