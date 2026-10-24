# DLL development guide

[中文](SDK.zh.md) | [English](SDK.en.md) | [Русский](SDK.ru.md)

This is a minimal DLL interface for future extensions, not a general game engine SDK.
The public interface is [rscript_plugin.h](include/rscript_plugin.h); the complete minimal example is
[log_plugin.cpp](examples/log_plugin.cpp). See the [user guide](README.en.md) for running it.

## Export and initialization

Use a 32-bit Windows toolchain matching the host and game. Export:

```c
const RScriptPluginDescriptor* RSCRIPT_CALL
RScriptPlugin_GetDescriptor(uint32_t host_abi);
```

Apply `RSCRIPT_EXPORT` from the header and `extern "C"` in C++.
The current ABI is `RSCRIPT_PLUGIN_ABI == 1`; return a null pointer for unsupported versions.
The descriptor, ID, and name must remain valid for the lifetime of the process.

- Supply the structure size, ABI version, unique ID, UTF-8 name, and initialization function.
- Set `features` to `0` for ordinary extensions. Built-in file integration, resource, and Unicode feature bits cannot be registered twice.
- `initialize(context)` returns `0` on success and any other value on failure. Send details through `context->log`.
- Check the context size and version first. Paths are borrowed UTF-16; IDs, names, and logs use UTF-8.
- Copy any data needed after initialization, including paths and strings; do not retain borrowed pointers.

The host initializes file integration first, followed by resources, Unicode, and other extensions.
Ordinary extensions run in the user's selected order; there is no general dependency resolver.
Duplicate IDs, conflicting built-in features, or incompatible ABIs prevent startup.

## Lifecycle and safety

Initialization runs before the game entry point. Keep `DllMain` minimal: do not install complex hooks,
wait for threads, or load resources there. `DllMain` executes before ABI validation, so that validation is not a sandbox.

A DLL whose initialization fails must undo its own partial changes before returning an error.
The host shuts down successfully initialized DLLs in reverse order. The optional `shutdown()` is used for
initialization rollback or explicit shutdown, not guaranteed during normal `ExitProcess`.
Do not depend on it to save essential data. Runtime unloading is unsupported, and shutdown does not promise that the DLL is unmapped.

Use the C ABI and `RSCRIPT_CALL`. Do not pass STL objects, C++ exceptions, or memory that another module must free across DLL boundaries.
Each module owns its allocations and releases; callbacks must catch their own exceptions.
Extensions installing hooks must verify the target version and coordinate conflicts with other DLLs.

Ordinary extensions can use `game_module`, the game path, resource directory, and logging.
`set_engine_api` / `get_engine_api` are a private bridge between built-ins, not a stable public resource interface;
third-party extensions must not depend on them.

`kinsoku_json` is an optional UTF-8 JSON string appended to the ABI 1 context; it is null when no configuration was loaded.
Before reading it, check that `context->size` covers the field, for example
`context->size >= offsetof(RScriptHostContext, kinsoku_json) + sizeof(context->kinsoku_json)`.
Check the minimum size you need, not exact equality, so later appended fields in the same ABI remain compatible.
The string is borrowed; copy it if needed later. The host validates it before passing it to plugins;
see [kinsoku.json](kinsoku.json) for its format.

## Extending and testing

The runtime CMake project includes the `RScriptLogExample` target.
An independent plugin needs only the public header and a shared-library target, not the entire conversion core.
Ensure its export is named exactly `RScriptPlugin_GetDescriptor`, without C++ name mangling.

Use **Add DLL...** in the launcher and explicitly check the added item.
Test ABI mismatch, initialization failure, duplicate IDs, and running with that DLL alone before combining modules.
The user guide lists the log location.

A future Wiki DLL can connect to verified text and click paths during initialization and manage Wiki on/off internally;
the switch should not depend on unloading the DLL. Only the extension entry point and logging example are provided today,
not Wiki functionality, plugin downloads, or hot updates.
