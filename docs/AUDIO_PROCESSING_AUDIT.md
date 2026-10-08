# CannonBall 音频处理核对

初次核对：2026-10-04；最新实现核对：2026-10-08。原程序只做静态分析，未启动游戏。

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
   当时先限制参考头为已验证的 `0x6771`；目前已完全取消音频参考文件，
   根据 Ogg 自身参数生成 PCM 或 Vorbis-in-WAV，见下文。
   转换失败不覆盖目标；递归封包沿用警告、跳过并排除旧目标的规则。
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
- 覆盖包含 `LIST`、`cue ` 尾部块的资源：这些编辑元数据不进入提取的 Ogg。
  目前重新生成容器而非继承原头，不承诺 WAV 容器逐字节还原。
- 初次修复时 Linux CTest 7 项通过；音频测试通过 ASan/UBSan；Windows CLI/GUI 交叉编译，
  Windows 音频测试在隔离 Wine 环境通过。
- 以上不等于原游戏内播放、听感或原版 32 位 ACM 驱动的运行验收，后者仍由使用者确认。

日常回归：`cmake --build build -j 4`，然后 `ctest --test-dir build --output-on-failure`。
音频单元测试同时为目录递归测试生成合成容器，避免继续用无效 WAV/Ogg 数据充当成功样本。

## 2026-10-08：实验性回写开关（已被下述实现替代）

两版 `RsComp.dll` 的媒体相关路径属于脚本编译，没有找到新的音频/图像编解码实现，
见 [DLL 核对](RSCOMP_MEDIA_AUDIT.md)。曾将 OGG→WAV 暂改为默认关闭的实验性功能：
GUI 勾选 `OGG → WAV (experimental)`，CLI 使用 `--experimental-ogg-to-wav`。
开关传递至递归封包的每一层；关闭时警告并跳过 OGG，但保留、收录已有 WAV；
开启后的参数校验及失败旧目标排除规则不变。WAV→OGG 和普通 PCM 保留不受影响。
这不是新的编码器，也不承诺容器逐字节还原；原游戏播放仍需用户验收。

## 当前实现：默认 PCM，可选 Vorbis-in-WAV

- 默认通过 Xiph `libvorbisfile` 真正解码为普通 WAV：`fmt` 标签 1、16 位有符号
  小端交错 PCM，保留源采样率和单/双声道，不重采样、不再次有损编码。
  多于两声道明确报错，避免原游戏不支持的声道布局或静默混音。
  用 EOS 采样数校验解码长度，拒绝错误页和不完整流，且先完成转换再写入目标。
- GUI 的 `Vorbis-in-WAV` / CLI 的 `--vorbis-in-wav` 改为可选压缩封装。
  **两种模式都不读取、不需要原 WAV**，`-r` 只用于 TXT→GSC。
  原 `--experimental-ogg-to-wav` 开关已移除。
- 新封装使用完整 Ogg 模式 1（`0x674f`），携带源流的头和码本，重新计算
  `fmt`、`fact`、`data`、RIFF 长度及对齐。不能随意给新 Ogg 标成原资源常见的
  模式 3：模式 3 可能依赖 ACM 驱动内建码本，不能保证与外部编码器一致。
  OGGWAVEFORMAT 的版本标识取自原 ACM 格式定义；模式 1 不用它选择内建码本。
  依据：[原 ACM 源码与格式说明](https://github.com/kunitsyn/ogg-acm-codec)、
  [Xiph PCM 解码接口](https://xiph.org/vorbis/doc/vorbisfile/ov_read.html)。
- 输入不再假定固定 66 字节头。按 RIFF 块长度、偶数字节对齐查找 `fmt` / `data`，
  支持额外 `JUNK` / `LIST` / `cue `、可选 `fact`、变长 `fmt` 和先 `data` 后 `fmt`。
  可提取完整 Ogg 的格式标签为 `0x674f/0x676f/0x6751/0x6771`；
  头另存于 `fmt` 的模式 2（`0x6750/0x6770`）尚不支持，明确不按完整 Ogg 提取。
- 保留旧资源兼容：误填解码后长度的 `data` 只恢复到真正 Ogg EOS，
  不把尾部编辑元数据当音频；容许省略最后一个对齐字节，以及已发现的 Sound Forge
  RIFF 总长度恰少 1 字节写法。不会任意忽略超界的 RIFF 块。
- 默认 PCM 文件更大，且不能从解码后的 PCM 逐字节恢复原 Ogg。
  压缩模式保留源 Ogg 字节（剔除独立空填充流），但新 WAV 容器不等同于原容器。
  游戏实际是否接受模式 1、PCM 的播放/寻址表现仍需原游戏验收。

### 本次验证

- Linux CLI / GTK GUI 与 Windows CLI / GUI 均编译成功。
- Linux 8 项 CTest 回归；音频单测通过 ASan/UBSan。
  回归覆盖无参考文件的两种模式、真实合成 Vorbis 解码、声道/采样率、RIFF 变长块、
  坏输入不覆盖输出、递归转换警告及旧产物排除。
- Windows 7 项二进制回归在隔离 Wine 环境通过；依赖宿主直接启动程序的 CMake
  `directory_packing` 测试仅在 Linux 运行。当前 Windows 交叉构建静态链接音频库，
  CLI / GUI 不需额外部署音频 DLL；未进行 Windows 原生 GUI 交互验收。
- 原游戏资源只读复核：CannonBall 和 Forest 共 19610 个嵌入 Vorbis 文件，
  提取→无模板新封装→提取后 Ogg 字节一致；31 个 PCM 正确识别保留，0 失败。
- 真实 PCM 抽查：CannonBall `voice/0001.wav`、`voice/1/0000.wav` 和 Forest
  `bgm/Track01.ogg` 解码后由 FFmpeg 完整读取，均识别为 `pcm_s16le`、44.1 kHz；
  单声道语音分别为 176060 / 223068 帧，双声道配乐为 8115652 帧，与源 Ogg 一致。
- 本节与前面的静态地址证据均不等于原游戏内播放测试；游戏资源未改写。
