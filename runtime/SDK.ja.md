# DLL 開発ガイド

[中文](SDK.zh.md) | [English](SDK.en.md) | [Русский](SDK.ru.md) | [日本語](SDK.ja.md)

最小の拡張用接口であり、一般ゲームエンジンSDKではありません。公開ヘッダー [rscript_plugin.h](include/rscript_plugin.h)、最小例 [log_plugin.cpp](examples/log_plugin.cpp)、[使い方](README.ja.md)を参照してください。

## export と初期化

宿主・ゲームと同じ32-bit Windowsツールチェーンで、次をexportします。

```c
const RScriptPluginDescriptor* RSCRIPT_CALL
RScriptPlugin_GetDescriptor(uint32_t host_abi);
```

ヘッダーのRSCRIPT_EXPORT、C++ではextern "C"も使用します。現ABIはRSCRIPT_PLUGIN_ABI==1、未対応ABIはnullを返します。descriptorとID/nameはプロセス寿命中有効にします。

- size、ABI、一意ID、UTF-8名、initializeを記入。
- 普通の拡張はfeatures=0。内蔵Files/Resources/Unicode bitを再使用しない。
- initialize(context)の0=成功、他=失敗。context->logで詳細を出す。
- size/versionを先に確認。パスは借用UTF-16、ID/name/logはUTF-8。
- 初期化後も必要なパス・文字列はコピーし、借用pointerを保持しない。

初期化順はFiles、Resources、Unicode、他拡張。他はユーザー選択順で汎用依存解決なし。重複ID、内蔵feature競合、非互換ABIは起動を止めます。

## 寿命と安全

ゲーム入口前にinitializeします。DllMainは最小にし、複雑hook、thread待機、資源装入を行いません。DllMainはABI検査より先に動くため、ABIはsandboxではありません。

失敗DLLは自分の変更を戻してからerrorを返します。宿主は成功済みDLLを逆順shutdown。任意shutdown()は初期化回退/明示終了用で、通常ExitProcessでは保証せず、必須保存を依存させません。実行中unloadは未対応で、終了後のunmapも保証しません。

C ABI/RSCRIPT_CALLを使い、STL、C++例外、別moduleでfreeするmemoryを渡しません。各moduleで確保・解放し、callback内部例外を捕捉します。独自hookは版と競合を検証してください。

game_module、ゲームパス、資源場所、logは普通拡張用。set_engine_api/get_engine_apiは内蔵module私用橋で、安定した公共資源APIではありません。

kinsoku_jsonはABI1の末尾に追加した任意UTF-8 JSONで、未設定はnull。使用前に次の最小field長を検査します。

```c
context->size >= offsetof(RScriptHostContext, kinsoku_json) + sizeof(context->kinsoku_json)
```

同ABIの追加fieldを受け入れるため、構造長の完全一致を要求しません。文字列は借用なので必要ならコピーします。宿主は検証済み設定だけ渡し、形式は [kinsoku.json](kinsoku.json) を参照してください。

## 拡張とテスト

同ディレクトリのCMakeにRScriptLogExample targetがあります。独立pluginはヘッダーと共有ライブラリのみでよく、変換core全体へのlinkは不要。export名は修飾なしRScriptPlugin_GetDescriptorです。

Add DLL...から追加し、明示選択して初めてロードします。ABI不一致、初期化失敗、重複ID、単独装入を先にテストし、次に組合せます。log場所は利用ガイドへ。

将来のWiki DLLは検証済み文字・クリック経路へ接続し、on/off状態を内部で持てます。動的unloadをスイッチにしません。現在は入口とlog例のみで、Wiki、plugin download、hot updateは未実装です。
