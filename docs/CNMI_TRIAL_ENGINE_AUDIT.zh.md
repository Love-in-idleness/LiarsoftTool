# 开发参考：CNMI 体验版资源

[中文](CNMI_TRIAL_ENGINE_AUDIT.zh.md) | [English](CNMI_TRIAL_ENGINE_AUDIT.en.md) | [Русский](CNMI_TRIAL_ENGINE_AUDIT.ru.md) | [日本語](CNMI_TRIAL_ENGINE_AUDIT.ja.md)

[文档目录](README.zh.md)

## 样本与范围

`ちんまい吸血姫体験版.exe`，SHA-256：
`21db7a449d329fb6bb030b1c1cf4050b0f519a52bd19167ba0d27dcc4cd6740e`。
以下是静态格式依据，不是原游戏运行验证。主程序的 EXE 转换入口已禁用。

与 [Evermaiden 中文程序](EVERMAIDEN_ENGINE_AUDIT.zh.md)相比，PE 头、节布局、大小、编译时间及 PDB 标识相同。
`.text` 仅有 10 个字体 charset 字节不同，但字符串、初始化数据和 PE 资源仍有差异，不能视为同一文件。
`0x44dc33–0x44dc35` 等路径仍调用 `IsDBCSLeadByteEx(CP_ACP, ...)`；字体参数不决定全部运行时编码。

## 空 LWG 条目

`grps.xfl/compane.lwg` 有 32 个条目。空的 `slide_lev_f` 与 `slide` 均位于 `(0,0)`、Flag 8；
另一个非空 `slide` 位于 `(690,4)`，尺寸为 `12×18`。空条目不能绑定到同名图片。

提取器用 `empty="1"` 保存空条目的名称、坐标、Flag 和顺序，不生成图片。
封包器读取该标记，不寻找同名文件；仅由空条目组成的归档仍被拒绝。
已经丢失的元数据须从原 LWG 重新提取。

- `0x475722–0x47578c` 读取全部声明条目。
- `0x427af2` 查询 `slide_lev`，`0x427b7c` 关联 `slide` 控件。
- 这些路径确认名称的菜单用途，不确认 Flag 8 的全部含义。
- rscript2renpy 同样须保留空条目身份，不让空的同名项覆盖非空项的坐标。

## 数据区长度

表后的四字节是数据区总长，不是填充。`compane.lwg` 在 `0x2fd` 保存
30231（`0x7617`）；文件总长 31000，数据从 `0x301` 开始，长度恰好为 30231。

封包时写入实际拼接载荷总长，空条目贡献零字节。
EXE 在 `0x475979–0x475981` 读取该字段；此事实本身不能证明零值必然导致崩溃。
解码器仍接受旧的零值产物。坐标、顺序、空项和载荷都属于需要保留的结构，
仅成功生成图片不足以验证完整往返。
