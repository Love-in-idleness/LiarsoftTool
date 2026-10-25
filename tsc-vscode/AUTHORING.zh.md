# TSC 创作：开发参考

[中文](AUTHORING.zh.md) | [English](AUTHORING.en.md) | [Русский](AUTHORING.ru.md) | [日本語](AUTHORING.ja.md)

[使用说明](README.zh.md)

## 源码契约

C++ 的 `inspectTsc` 与编译器共享 tokenizer、参数表和标签检查；CLI 的
`--inspect-tsc <file>` 或 `--inspect-tsc -` 返回协议 1 JSON，不写 GSC。
接口字段见 [TSC 检查接口](../docs/TSC_DIAGNOSTICS.zh.md)。位置为 UTF-8 字节区间，
插件先校验边界再转换成 VS Code 的 UTF-16 位置。

`graph.js` 只将已有检查结果投影为节点，不再实现 TSC 语法解析器。
标签构成节点，跳转与选择构成边。没有跳转的段落保留原来的顺序落入关系。
`return/end/jump` 后的命令仍保留在源码中，但不产生可执行的本地连线。
编辑只替换选定 token 或事件区间；候选源码通过 C++ 检查后才提交 WorkspaceEdit，支持撤销。
文档版本改变后拒绝旧消息；语法错误时禁用图形修改。注释与未知命令不参与无关重排。

工程 JSON 不是剧情副本。`;@editor-node` 记录标题与位置，`;@editor-event` / `;@editor-event-end`
记录多条原生命令组成的事件；其他注释不视为编辑器元数据。
构建先检查全部脚本的语法、目标编码和可确定的场景调用，再逐文件输出。
动态场景调用须先在源码中解决。输出与 scr、assets、资源目录不得重叠；目录中的符号链接不接受。
写入采用同目录临时文件替换。构建不是整工程事务，素材转换中途失败后应修复并重新构建。

## SevenBridge 优先配置

目标为日文 `SBridge_jp.exe`，SHA-256：

```text
c7d5fc38cf0c354a61d056b607cf1367b2529557e9e0407d0b0fdf12771b35e3
```

原程序静态依据位于
[SevenBridge 原程序依据](https://github.com/Love-in-idleness/rscript2renpy/blob/main/sevenbridge/NATIVE_EVIDENCE.zh.md)。
分派为 `0x41cc40`，三参数 locmode 为 `0x420220`，folder 为 `0x4204e0`，
音效为 cases `0x3e–0x40`，语音路径组成为 `0x41b5b0`。
当前使用原始脚本核对参数，不从其他游戏猜默认值；未为本任务运行原游戏 EXE。

| 事件 | SevenBridge 输出形状 | 原始脚本依据 |
| --- | --- | --- |
| 初始化坐标／资源目录 | `locmode obj 0 0`、`folder obj "grpo"` | 0000 |
| 对白 | `TXT 0 voice 0 0 "" "^gNNNtext" 1` | 2001 |
| 图像 | queue、folder、locmode、`load layer id x y 0 0`、`update 0 0 0` | 0000、2001 |
| 清图 | queue、`cls layer 0`、update | 2001 |
| 音乐 | `bgm_on track 1 0`、`bgm_off 0 0` | 2001 |
| 音效 | `se id`、`se_on 0 0 0` | 2001 |
| 语音 | `voice id 0 0 0` | 2001 |
| 场景调用 | `gosub scene ""` 后跟十个零；`return 0` | 2001 |
| 选择 | 五个目标槽、五个文本槽、末尾 `0 0 0` | 原 select 参数表、1309 |
| 变量 | `0x1800` 赋值；`0xa800/b800` 运算后用 `0x1900` 写回；`0x4800–9800` 比较 | 0000 与公共 VM |

新脚本固定 `modern-36 / rscript18`，因为本作 locmode 接收三个参数，SE 为单通道。
已有 rscript19/modern 文件保持原样；不能插入的事件会报错，不暗改参数表。
Forest 配置保留 `legacy-28 / early`；gosub 只有场景号，bgm_on 末尾只有 `1`，select 末尾为 `2 0 0`。
依据为 Forest 原始 0000/2100 与核心 early 参数表。

## 验证边界

Node 测试覆盖源码保留、Unicode、条件/选择、原生事件、路径保护、构建失败和预览偏移；
`test/host.js` 用实际 VS Code 扩展宿主检查诊断、图形页、SevenBridge 构建与源码切换。
`test/browser.js` 通过真实 provider 与本机编译器驱动前端，素材和脚本均为隔离测试数据。
不把这些检查等同原游戏游玩；原生 Windows 路径、实际文字效果和完整故事仍需人工验收。
