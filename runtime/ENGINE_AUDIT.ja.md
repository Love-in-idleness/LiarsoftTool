# RScript Runtime エンジンの根拠

[中文](ENGINE_AUDIT.zh.md) | [English](ENGINE_AUDIT.en.md) | [Русский](ENGINE_AUDIT.ru.md) | [日本語](ENGINE_AUDIT.ja.md)

[利用ガイド](README.ja.md) · [DLL インターフェース](SDK.ja.md)

## 範囲

ファイルとGDIはPE import名、禁則は完全な共通分類器で接続します。ゲーム名・EXE whitelist・固定アドレスは使いません。Forest/SevenBridgeは共通経路の検証サンプルです。以下のアドレスは選択条件でなく根拠で、import-slot RVA以外はpreferred baseのVAです。専有DLLコードやゲーム資産は含みません。

## 調査ファイル

| ファイル | SHA-256 |
| --- | --- |
| Forest.exe | `8eb71d07304d362ec26cc47d93c6ef5854b5acadd7c9fe941755ad50820a3e56` |
| SBridge_jp.exe | `c7d5fc38cf0c354a61d056b607cf1367b2529557e9e0407d0b0fdf12771b35e3` |
| CodeXRScript/OC patch RsComp.dll | `6c65c3545a559b6e4a01d54fdb2f57ad4e605b27dc73959cde18bd3468f3cb30` |
| UBAI RsComp.dll | `0b1c3cdfc1587f576c30c09b3faa329e88e18fdaddfc0655d1ad398c1654a6a9` |
| Albatross rsinit.dll | `43499ada89a18735707c0e33d9a9bb585022e8e1b9b93e562a5e41bbb8340eb6` |

RsComp二版は32-bit、Compile exportのRVAは0x124e/0x7090。Forestは0x412eb0で装入、0x4135f0で四引数call。結果はengine objectへ渡り、独立input/outputファイルAPIではありません。コンパイラーだけで画像・音声・Unicodeを制御できないため、万能RsComp交換とせず別hostと任意DLLを採用します。

## Forest ファイル経路

| 位置 | 動作・条件 |
| --- | --- |
| 0x4247c0 | CFileでlooseを先に試し、次にarchive |
| 0x42a970 | read-only CreateFileA、OPEN_EXISTING |
| 0x42ab20 /0x42ab80 /0x42abe0 | read/seek/size |
| 0x413200 | %s/%04d.gsc、28-byte headerと各section |
| 0x428260 /0x4288d0 | LIM/WCG名を作り、要求でdecoderを選ぶ |
| 0x424b50 | LWG部品 |

五importのRVA:

| API | RVA |
| --- | --- |
| CreateFileA | 0x59098 |
| ReadFile | 0x5907c |
| SetFilePointer | 0x590c8 |
| GetFileSize | 0x590cc |
| CloseHandle | 0x5909c |

対応overrideはメモリーファイル、それ以外は原APIでloose/XFLを処理。process全体のNT hookではありません。LIM要求にWCGを改名して返してはいけません。Forestはlegacy-28ですが、他engineのmodern-36を拒否する理由にはなりません。TSC metadataを保持し、参照GSC headerがあれば比較します。ゲーム名からheader長を選びません。画像・音声・LWG・TSCは既存coreと共通.meta.xml readerを再利用します。

## SevenBridge 比較

| 動作 | VA / import RVA |
| --- | --- |
| loose優先、次archive | 0x427c30→0x42e020/0x42e760 |
| GSC | 0x415a60、9DWORD、modern-36 |
| LIM/WCG/LWG | 0x42b910 /0x42bf80 /0x4281f0 |
| file import | open0x5f098/read0x5f080/seek0x5f0e0/size0x5f0e4/close0x5f09c |
| DBCS parser | 0x43dad0、0x43db76でlead<<8 OR trail |
| GDI glyph | 0x44adf0、GetGlyphOutlineA |
| CRT codepage | 0x44d444→0x44d5f1→GetACP |

アドレスとheaderは違っても同期file protocolは共通です。同じFiles/Resources/Unicodeを使い、専用DLLを追加しません。imports.cppは普通importを解析し、API不足とhook競合を拒否。FindFirstFileAはセーブ/ディレクトリ用途で資源事前探索ではなく、MCI動画は迂回します。

REA: fallback `ev_aa340a6409c9a54a1f26c89c1852b42989d52f080dee22121653bcb1bfd57bba`、
GSC `ev_4b709b26aef633c9dadc5343ecb2fe26276667c62df7e3982a280d53464ee4a3`、
parser `ev_fc6c754911df3df1c51c4883f8919a561b03506db554063b44fbab618c6d3916`、
glyph `ev_3560688f91be6c3ed92c87c94570b236b6514478bc63db0e127f59bf0a61b4b4`、
CRT `ev_c4c5f4527fa369b4af23870e6b73666994f8b60fe2041741d8fa196910bd6c12`。

## Unicode 接続

Forest parser0x438790は一/二byteを歩き、0x438836–0x438842で16-bit字、0x438866で二byte進行。0x4453e0/0x445760/0x445ab0/0x445e70付近はANSI GDIで、UTF-8を直接渡すと分断します。ファイル符号化の変更だけでは不十分です。

0x44880cはCRT表0x48fa00、0x44bfb8は初期化、0x44c165はGetACP。Unicodeはゲーム自身の名前付きGetACPを932にし、日本語外環境でも二byteIDを使います。IsDBCSLeadByteExのシステム既定要求も932、明示された別pageは保持。Windows全体や別moduleのGetACPは変更しません。
REA `ev_fec16cd0f7502d87c643ce4be10a0f513fed452271d3d9e2470c33a9fc4ff9a2`、
`ev_17e895a8bdb39082341c3f9bdb782e5e4ef09b18431096f0819b17088726f365`。

命令・制御符を保持し、CP932で無損失往復できる字は原bytes、他BMP字はCP932 F0–F9の安定IDへ。描画はIDを実Unicodeへ戻します。

| 原import | RVA | mapping後 |
| --- | --- | --- |
| GetGlyphOutlineA | 0x59038 | GetGlyphOutlineW |
| TextOutA | 0x59024 | TextOutW |
| GetTextExtentPoint32A | 0x5903c | GetTextExtentPoint32W |

未mappingは原ANSI。原script/EXE使用IDは予約。script提供前にuser dataへ辞書を安全書込し、再起動・セーブ・履歴で安定させます。文字を?に置換する処理ではありません。ただし追加BMP最大1880から既使用分を引き、補助平面・一般複雑塑形は未対応です。

## UTF-8 禁則

SevenBridge0x43e450は描画後layout配列に0普通/1行頭禁止/2行末禁止を記入。0x43e540–0x43e798がSIを比較し、0x43e7cc/0x43e7b3へ合流、普通0x43e79a。
REA `ev_f04c03afdea665d7bdadec40a14e679ddbec0d74637f99ddd47b90b02e821f43`。

[native_kinsoku.cpp](src/native_kinsoku.cpp)はASCII/CP932全比較群、条件分岐、共通targetを確認し、即値型とMOV EAX/CMP SI,AX型を扱います。process内bridgeはstack整列、register/flags/FPU/SIMD保存、glyph tokenからUnicode復元、[kinsoku.json](kinsoku.json)分類。単字へ戻せないnative packed値は元分類を維持。認識できなければUnicode初期化失敗。ディスクEXEやTSC改行は変えません。

これは文字別の始終禁則で、[Unicode UAX #14](https://www.unicode.org/reports/tr14/)全体ではありません。新形状は共有認識器とテストを拡張し、ゲームアドレス表を増やしません。

## Albatross DLL から分かること

rsinit.dllはOpenGL export361個、埋込resource、runtime hookを持ちます。0x623af440初期化はNT file、0x623b15e0はwindow/font/module寿命も扱います。0x623af370/0x623af750はcharset0x86固定で、GB2312経路であってUTF-8対応ではありません。0x623b45f0は保護変更とtrampolineを作ります。

DLLでディスクEXEを書かず資源・window・textを変更できる証拠ですが、版横断互換を証明しません。新engineにはcall規約、構造、描画の確認が必要。選択の参考にしただけで、専有実装はコピーせず依存もしません。

## 検証範囲

portable testは優先度、不正overrideでfallbackしないこと、パス境界、部品配置、原入力不変、辞書を確認。Windows/Wineは入口前装入、ABI、失敗回退、file API、Unicode GDIを追加検査します。合成import表と実ゲーム起動は別で、起動成功は全プレイ・旧セーブ・全字体を意味しません。Windows実機と別protocolは別途確認します。

独立コピーのWineで冒頭・タイトルまで進み、実TSC/PNGを装入しました。同一汎用経路をForest/SevenBridgeで使用し、原エンジンframeで日本語、簡体字、CP932外ラテン、色制御を確認しました。[game_smoke.cpp](tests/game_smoke.cpp)は自分が確認して作成した子processのみ終了する手動helperで、既定CTestではなく、ゲームfixtureも非同梱です。
