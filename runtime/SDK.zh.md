# DLL 开发说明

[中文](SDK.zh.md) | [English](SDK.en.md) | [Русский](SDK.ru.md)

这是一个最小 DLL 接口，供后续扩展使用，不是通用游戏引擎 SDK。
公开接口见 [rscript_plugin.h](include/rscript_plugin.h)，完整最小示例见
[log_plugin.cpp](examples/log_plugin.cpp)。运行方法见 [使用说明](README.zh.md)。

## 导出与初始化

使用与宿主、游戏一致的 32 位 Windows 工具链。DLL 导出：

```c
const RScriptPluginDescriptor* RSCRIPT_CALL
RScriptPlugin_GetDescriptor(uint32_t host_abi);
```

按头文件添加 `RSCRIPT_EXPORT`，C++ 实现还需 `extern "C"`。
当前 ABI 为 `RSCRIPT_PLUGIN_ABI == 1`；不支持传入版本时返回空指针。
描述符及其 ID、名称在进程存续期间保持有效。

- 填写结构大小、ABI 版本、唯一 ID、UTF-8 名称及初始化函数。
- 普通扩展的 `features` 设为 `0`；内置功能位用于引擎适配、资源和 Unicode，不能重复占用。
- `initialize(context)` 返回 `0` 表示成功，其他值表示失败；错误详情通过 `context->log` 输出。
- 先检查上下文大小和版本。路径是借用的 UTF-16；ID、名称和日志是 UTF-8。
- 需要在初始化后使用的数据须自行复制，包括路径和字符串；不要保留借用指针。

宿主先初始化引擎，再初始化资源、Unicode，最后初始化其他扩展。
普通扩展按用户选择的顺序执行，不提供通用依赖解析。
重复 ID、内置功能冲突及不兼容的 ABI 会阻止本次启动。

## 生命周期与安全

游戏入口执行前调用初始化。`DllMain` 保持最小，不在其中安装复杂钩子、等待线程或加载资源。
DLL 的 `DllMain` 会早于 ABI 检查执行，因此 ABI 检查不是安全沙箱。

初始化失败的 DLL 应先自行撤销已做的修改再返回错误。
宿主会逆序关闭已经成功初始化的 DLL。可选 `shutdown()` 只用于初始化回退或显式关闭，
正常 `ExitProcess` 不保证调用它；不要依赖它保存必须保留的数据。
不支持运行中卸载，关闭后也不承诺 DLL 已从内存解除映射。

接口使用 C ABI 和 `RSCRIPT_CALL`，不跨 DLL 传递 STL 对象、C++ 异常或需要另一模块释放的内存。
模块各自管理分配与释放；所有回调都必须捕获内部异常。
自行安装钩子的扩展须先核对目标版本，并协调与其他 DLL 的冲突。

`game_module`、游戏路径、资源目录及日志用于普通扩展。
`set_engine_api`／`get_engine_api` 是内置模块的私有桥接，不是稳定的公共资源接口，第三方扩展不要依赖。

## 扩展与测试

在同一目录的 CMake 中已提供 `RScriptLogExample` 目标。
独立插件只需包含公开头文件并构建共享库，不需要链接整个转换核心；
确保导出名恰为 `RScriptPlugin_GetDescriptor`，不能被 C++ 名字修饰改变。

通过加载器 **Add DLL...** 添加，明确勾选后才会加载。
先测试 ABI 不匹配、初始化失败、重复 ID 和只加载该 DLL 的情况，再组合其他模块。
日志路径见使用说明。

未来 Wiki DLL 可以在初始化时接入经过验证的文字与点击路径，
并在 DLL 内管理 Wiki on/off 状态；开关不应依赖动态卸载 DLL。
当前只提供接入入口和日志示例，没有实现 Wiki、插件下载或热更新。
