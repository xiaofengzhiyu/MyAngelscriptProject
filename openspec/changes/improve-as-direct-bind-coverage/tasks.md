# Tasks — improve-as-direct-bind-coverage

> Authoritative implementation plan for this change. `tasks.md` is the only plan; do not write a separate plan file.
>
> Verification entry points (per `Documents/Guides/Build.md` and `Documents/Guides/Test.md`):
> - Build: `Tools\RunBuild.ps1` (never call UBT/Build.bat directly)
> - Tests by group: `Tools\RunTests.ps1 -Group <Bindings|Cpp|Editor|...>`
> - Suite: `Tools\RunTestSuite.ps1 -Suite Default`
> - State dump: console `as.DumpEngineState`,or programmatic `FAngelscriptStateDump::DumpAll()`

## 0. Phase 0 — Day-0 IModularFeatures Probe (STOP-on-fail)

> 这一阶段是本 change 的二元门禁。**probe 任一项不通过即 STOP**,不进入后续阶段。失败时把现象与构建/链接器输出归档到 `Documents/Reports/CrossModuleLinkProbe_<Date>.md` 并把 change 标记为 abandoned。
>
> 本阶段同时验证四件事:(i) UBT 是否自动纳编目标模块 OutputDirectory 下额外的 `AS_FunctionTable_*_LinkProbe.cpp`;(ii) 引擎模块静态构造期 `IModularFeatures::Get()` 已就绪、`RegisterModularFeature` 调用成功;(iii) AS Runtime 端 `EOrder::Late + 60` 调 `GetModularFeatureImplementations(...)` 能拿到 probe feature;(iv) `IModularFeatures::IsAvailable()`(或等价 idiom)在 UE 5.7 的可用性,确定 Shutdown 兜底形态。

- [ ] 0.1 <!-- Non-TDD --> 在 `Plugins/Angelscript/Source/AngelscriptUHTTool/` 增加临时 exporter `AngelscriptCrossModuleLinkProbeExporter.cs`(项目本地、可临时存在),只对 `Engine` 模块 `factory.MakePath(engineModule, "AS_FunctionTable_Engine_LinkProbe", ".cpp")` emit 一份内容固定的最小 cpp。文件内容(全部 anonymous 命名空间):
  - `#include "Features/IModularFeatures.h"`
  - 内嵌一个最小 `struct FProbeEntry { const TCHAR* Tag; uint32 Magic; };` 与一个最小 `struct FProbeFeature : public IModularFeature { const FProbeEntry* Entries; int32 Count; const TCHAR* ModuleName; uint32 LayoutVersion; FProbeFeature(const FProbeEntry* E, int32 C, const TCHAR* M, uint32 V) : Entries(E), Count(C), ModuleName(M), LayoutVersion(V) {} };`(**ctor 实例化,绝不 brace-aggregate-init,因为 IModularFeature 有虚析构,派生类不是 aggregate**)。
  - `static const FProbeEntry GProbeTable[] = { { TEXT("Engine.Probe"), 0xA5C0DE01u } };`
  - `static FProbeFeature GProbeFeature(GProbeTable, 1, TEXT("Engine"), 0xA5C0DE01u);`
  - 一个静态构造对象在 ctor 里 `IModularFeatures::Get().RegisterModularFeature(FName("AngelscriptCrossModuleLinkProbe"), &GProbeFeature)`,在 dtor 里 `Unregister`(注意 dtor 调用前的 Shutdown 兜底,见 0.4)。
  - 文件名前缀沿用 `AS_FunctionTable_*` 以避免被引擎 `CodeGen` exporter 的 `CullOutput` 命中。
  - 验证:无需测试,只看构建产物路径。预期 `Engine/Intermediate/Build/.../Engine/` 下出现该文件;`Tools\RunBuild.ps1 -Target Editor` 能编过(说明 UBT 自动纳编 + ctor 形态合法)。
  - 失败模式:UBT 报"unknown source file"或 link 后该 cpp 不在 Engine.dll/.lib 中 → STOP,记录失败到 `Documents/Reports/CrossModuleLinkProbe_<Date>.md`,abandon change。**brace-aggregate-init 编译错误 (`cannot use a brace-enclosed initializer ...`) → 不 STOP,但说明文档示例有误,改 ctor 形态再试**。

- [ ] 0.2 <!-- TDD --> 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Probe/AngelscriptCrossModuleLinkProbe.cpp` 中:
  - `#include "Features/IModularFeatures.h"`
  - 写一个 `IMPLEMENT_SIMPLE_AUTOMATION_TEST` 单元 `Angelscript.CppTests.UHTToolResolver.LinkProbe.IModularFeaturesRoundtrip`,在测试体内调 `IModularFeatures::Get().GetModularFeatureImplementations(FName("AngelscriptCrossModuleLinkProbe"))`,断言:(a) 数组非空;(b) 第一个 `IModularFeature*` 通过 reinterpret_cast 到本测试中重定义的 reader struct(layout 与 0.1 完全一致),`Reader->LayoutVersion == 0xA5C0DE01u`、`Reader->Count == 1`、`Reader->Entries[0].Magic == 0xA5C0DE01u`、`FString(Reader->ModuleName) == TEXT("Engine")`。
  - 把测试纳入 `Group=Cpp`。
  - **关键失败模式**(任一即 STOP):
    - 0.1 的 cpp link 失败(`unresolved external` / "no source file" 等)→ STOP。
    - 测试 PASS 但数组为空 → IModularFeatures 注册时序异常,STOP 调研根因。
    - reinterpret_cast 后 LayoutVersion 错乱 → POD layout 跨 DLL 不稳定,STOP。
  - 命令:`Tools\RunTests.ps1 -Group Cpp -Filter "Angelscript.CppTests.UHTToolResolver.LinkProbe.IModularFeaturesRoundtrip"`。
  - 通过条件:测试 PASS 且 `Reader->LayoutVersion == 0xA5C0DE01u`。这是后续阶段的前置条件。

- [ ] 0.3 <!-- Non-TDD --> 把 0.1/0.2 的 probe 留作日后 regression(不删,让它一直跟着 UHT exporter 跑),以保证 UE 升级 / Core 重构 / IModularFeature 形态变化不会突然破坏跨模块纳编与注册机制。

- [ ] 0.4 <!-- Non-TDD --> 调研 `IModularFeatures` 的可用性 idiom(对应 spec 的 Open Question `Q-IsAvailable`):
  - 在 UE 5.7 的 `Features/IModularFeatures.h` 与相关源中查找 `IsAvailable` / `Get()` 的析构防御机制。
  - 若 UE 暴露 `IModularFeatures::IsAvailable()`,Shutdown 路径直接用之。
  - 若不暴露,选定一个 try-pattern 形态(优先级:`FCoreDelegates::OnPreExit` 设置 `static thread_local bool bShuttingDown = true;` flag → `~FAutoReg` 看 flag → 若 `true` 则 no-op;若 false 则正常 `Get().Unregister`)。
  - 选定形态记录到 `Documents/Reports/CrossModuleShutdownIdiom_<Date>.md`,并在 1.x 实施时一致使用。
  - 验证:不进测试,只输出文档与决策。

## 1. Phase 1 — ABI 与公共头骨架(端到端最小手工示例)

> 本阶段不动 UHT 自动化,先用一个手写 cross-module shard 端到端跑通,确认 `IModularFeatures` + raw thunk + reinterpret_cast + ctor 实例化 + Shutdown 兜底 五件套在工程中正确落地。

- [ ] 1.0 <!-- Non-TDD --> 新增 `Plugins/Angelscript/Source/AngelscriptUHTTool/cross-module-layout-version.txt`,内容仅一行 `0xA5C0DE01`(以及顶部多行注释列出"何时必须 bump"的规则)。bump 触发条件清单:
  - 增删 `FAngelscriptCrossModuleEntry` POD 字段
  - 调整字段顺序
  - 改变字段宽度(int32 ↔ int64 / uint16 ↔ uint32)
  - 改变字段语义(同名同类型但 `Flags` 含义变化)
  - AS Runtime reader 与 emit cpp 任一端单方面修改 layout
  - 验证:跑 `Tools\RunTests.ps1 -Group Cpp -Filter "Angelscript.CppTests.UHTToolResolver.LayoutVersionFile_SingleSource_*"`(下 1.6 测试)。

- [ ] 1.1 <!-- Non-TDD --> 新增公共头 `Plugins/Angelscript/Source/AngelscriptRuntime/Public/UHT/AngelscriptCrossModuleBindings.h`。仅包含:
  - `#include "Features/IModularFeatures.h"`(由 AS Runtime 端 `Bind_CrossModuleDirect.cpp` include 时也用)
  - `class UObject;` 前向声明(避免头依赖膨胀)
  - `struct FAngelscriptCrossModuleEntry { const TCHAR* ClassName; const TCHAR* FunctionName; void (*Thunk)(class UObject* Self, void** Args, void* Ret); uint16 ArgCount; uint16 RetSize; uint32 Flags; };`
  - `struct FAngelscriptCrossModuleFeatureReader { void* VTablePadding; const FAngelscriptCrossModuleEntry* Table; int32 Count; const TCHAR* ModuleName; uint32 LayoutVersion; };`
  - 命名空间 `FAngelscriptCrossModuleBindings`(或类似)的 `static constexpr uint32 LayoutVersionExpected = 0xA5C0DE01u;`(值由 build/UHT 阶段从 1.0 的 `cross-module-layout-version.txt` 同步;实现可走 generator emit 一个 `AngelscriptCrossModuleLayoutVersion.gen.h` 由本头 `#include` 的形式)、`static constexpr FName FeatureName = "AngelscriptCrossModuleBindings";`(若 FName 不能 constexpr 则用函数返回)。
  - 编译期 `static_assert(sizeof(FAngelscriptCrossModuleEntry) == /*expected bytes*/, ...)`、`static_assert(sizeof(FAngelscriptCrossModuleFeatureReader) == /*expected bytes*/, ...)`(具体字节数 1.1 实现时计算并写入)。
  - **`Flags` 位定义**:`bit0 Static`、`bit1 Const`、`bit2 WorldContext`、`bit3 HasOutParams`、`bit4 ReturnByRef`,其余预留。位定义写在头注释里。
  - **不**包含 `Core/AngelscriptBinds.h` / `Core/FunctionCallers.h` / `FAngelscriptBinds` / `ASAutoCaller` / `FGenericFuncPtr` / `angelscript.h` 任何引用。
  - 验证:对该头独立做一个最小 includer test(headless 单测)`Angelscript.CppTests.UHTToolResolver.PublicHeader.NoASRuntimeOrSDKDeps`,断言:include 该头后 `FAngelscriptCrossModuleEntry`/`FAngelscriptCrossModuleFeatureReader` 可声明、且 `_HAS_ASRUNTIME_BINDS_H` / `_HAS_AS_SDK` 等 sentinel 不被定义(具体 sentinel 由 1.1 实现时选定)。
  - 命令:`Tools\RunTests.ps1 -Group Cpp -Filter "Angelscript.CppTests.UHTToolResolver.PublicHeader.*"`。

- [ ] 1.2 <!-- Non-TDD --> 选 1 个 `Engine` 模块的 `unexported-symbol` 候选(从当前 `AS_FunctionTable_SkippedEntries.csv` 中挑一个签名简单、无 out-param、**非 RPC**(不带 Server/Client/NetMulticast)的成员函数 — 优先 `void(void)` 或 `<primitive>(void)`)。在 `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptCrossModuleHandwrittenExporter.cs` 临时硬编码,只为这一个函数 emit `AS_FunctionTable_Engine_CrossModule_Manual.cpp` 到 Engine 模块 OutputDirectory。文件内容(全部 anonymous 命名空间):
  - `#include "Features/IModularFeatures.h"`
  - `#include "<TargetClass>.h"`
  - 内嵌 `FAngelscriptCrossModuleEntry` 与 `FAngelscriptCrossModuleFeature : public IModularFeature { ... 显式 ctor ... }` 的完整 layout(与 1.1 公共头中 reader 字段顺序与类型一一对应;**ctor 实例化,禁止 brace-aggregate-init**)
  - `static void Thunk_<Class>_<Func>(UObject* Self, void** /*Args*/, void* /*Ret*/) { static_cast<TargetClass*>(Self)-><Func>(); }`
  - `static const FAngelscriptCrossModuleEntry GTable[] = { { TEXT("<Class>"), TEXT("<Func>"), &Thunk_<Class>_<Func>, 0, 0, 0 } };`
  - `static FAngelscriptCrossModuleFeature GFeature(GTable, UE_ARRAY_COUNT(GTable), TEXT("Engine"), 0xA5C0DE01u);`
  - `static struct FAutoReg { FAutoReg() { IModularFeatures::Get().RegisterModularFeature(FName("AngelscriptCrossModuleBindings"), &GFeature); } ~FAutoReg() { /* Shutdown 兜底见 0.4 决策;通常形如:if (!GShuttingDown) IModularFeatures::Get().UnregisterModularFeature(FName("AngelscriptCrossModuleBindings"), &GFeature); */ } } GAutoReg;`
  - 验证:`Tools\RunBuild.ps1` 通过;最终二进制中无新增导出符号(本方案不依赖导出表)。
  - **关键不变量**:本 cpp 内 **不出现** `extern <ENGINE>_API ...`、**不出现** `Get_AS_Bindings_*` 形态的导出函数、**不出现** `static .* = { GTable,` 形态的 brace-aggregate-init — 与方案 D-IMF / D-Aggregate-Init 一致。

- [ ] 1.3 <!-- TDD --> 新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_CrossModuleDirect.cpp`,`EOrder::Late + 60` 阶段:
  - `#include "Features/IModularFeatures.h"` + `#include "UHT/AngelscriptCrossModuleBindings.h"`
  - 实现单一通用 generic hook:`static void GAngelscriptCrossModuleGenericHook(asIScriptGeneric* G)`,从 `G` 读 Self/Args、按 `Flags` 位决策(bit0/bit2/bit3 等)、为 Ret 分配 `RetSize` 字节缓冲、调 entry 的 raw `Thunk(Self, Args, Ret)`、把 Ret 与 out-param 写回 `G`。entry 指针通过 AS user-data 携带。
  - 在 Late+60 调 `IModularFeatures::Get().GetModularFeatureImplementations(FName("AngelscriptCrossModuleBindings"))`;对每个 `IModularFeature*` 反向 reinterpret_cast 到 `FAngelscriptCrossModuleFeatureReader*`;校验 LayoutVersion magic、`Count >= 0`、`Table != nullptr`、`ModuleName != nullptr`,任一 fail 即 `UE_LOG(Angelscript, Warning, ...)` 并 skip。
  - 遍历 entry,以 `asCALL_GENERIC` 注册到 AS Engine,并 `FAngelscriptBinds::AddFunctionEntry(...)` 写入 `ClassFuncMaps`(仅在空槽时,遵守 D4 优先级)。
  - 订阅 `IModularFeatures::Get().OnModularFeatureRegistered.AddStatic(&OnLateRegistration)`;**回调内 marshal 到 GameThread**:`AsyncTask(ENamedThreads::GameThread, [Feature]{ /* 走相同的 cast + 校验 + 注入路径 */ })`。
  - 在 `FCoreDelegates::OnPreExit.AddStatic(&Unsubscribe)`,Shutdown 时主动移除 `OnModularFeatureRegistered` 订阅(见 0.4 决策)。
  - **绝对禁止**:本 cpp 内出现任何 `extern <MODULE>_API` 或 `Get_AS_Bindings_<Module>` 形态的声明 / 调用 — 这是核心约束,违反即 task 不通过。
  - **失败的红测试先写**:`Angelscript.TestModule.Bindings.CrossModuleDirectBind.IsBoundAfterLate60_Manual`(红 → 实现 → 绿)。断言 `FAngelscriptBinds::GetClassFuncMaps()[OwningClass][FuncName].FuncPtr.IsBound() == true` 且行为等价反射 fallback。
  - 命令:`Tools\RunTests.ps1 -Group Bindings -Filter "Angelscript.TestModule.Bindings.CrossModuleDirectBind.*"`。

- [ ] 1.4 <!-- TDD --> `Angelscript.TestModule.Bindings.CrossModuleDirectBind.BehaviorEquivalent_VsReflectionFallback`:用 1.2 选定的函数,先以 `r.AS.ReflectiveFallback.UseCache=0`(legacy ProcessEvent) 与 `r.AS.ReflectiveFallback.UseCache=1`(cached UFunction::Invoke) 反射路径采样输出,再以本 change 的 cross-module direct 路径采样,断言三者完全一致(返回值、out-param、世界状态副作用)。
  - 命令:`Tools\RunTests.ps1 -Group Bindings -Filter "...BehaviorEquivalent_VsReflectionFallback"`。

- [ ] 1.5 <!-- TDD --> `Angelscript.CppTests.UHTToolResolver.BuildCs_NoEngineModuleAddedAsDependency`:静态扫描 `Plugins/Angelscript/Source/AngelscriptRuntime/AngelscriptRuntime.Build.cs`,断言其 `PublicDependencyModuleNames` / `PrivateDependencyModuleNames` 列表与本 change 起点(可由 baseline 文件比对或 hardcoded 列表)相比,**没有新增任何引擎模块** — 即本 change 不引入反向 link 依赖。
  - 命令:`Tools\RunTests.ps1 -Group Cpp -Filter "Angelscript.CppTests.UHTToolResolver.BuildCs_*"`。

- [ ] 1.6 <!-- TDD --> `Angelscript.CppTests.UHTToolResolver.LayoutVersionFile_SingleSource_GeneratorAndHeaderInSync`:读取 `cross-module-layout-version.txt` 内容,与公共头 `LayoutVersionExpected` 常量、与 1.2 手写 emit cpp 内 `GFeature` ctor 第 4 参数,三处比对一致。
  - 任一处不一致即测试 fail,提示 bump 流程被违反。

- [ ] 1.7 <!-- Non-TDD + TDD --> 端到端覆盖**复杂参数形态各 1 个手工示例**(扩展 1.2 的 emit cpp,选 4 个目标函数分别覆盖):
  - **out-param**(如 `void GetClampedActorBounds(FVector& OutMin, FVector& OutMax)`):TDD `OutParam_WriteBackThroughArgsSlot` 验证 AS 端 out-param 槽收到正确写入。
  - **static 函数**(如 `static void K2_Foo(...)` 在某 BlueprintFunctionLibrary):TDD `StaticFunc_NoSelf_BindsAsGlobal` 验证以 `BindGlobalFunction(<ClassName>::<Func>, ...)` 注册。
  - **WorldContext**(如某带 `meta=(WorldContext="WorldContextObject")` 的函数):TDD `WorldContextObject_InjectedByHook` 验证 hook 自动注入当前 World。
  - **const-qualified**(如 `FVector GetActorLocation() const`):TDD `ConstQualifiedMethod_PreservesConst` 验证 thunk 走 `const T*` 路径,AS 端 method declaration 带 `const`。
  - 命令:`Tools\RunTests.ps1 -Group Bindings -Filter "...OutParam_*|...StaticFunc_*|...WorldContextObject_*|...ConstQualifiedMethod_*"`。

- [ ] 1.8 <!-- TDD --> 端到端覆盖**复杂值参数与对齐**:扩展 1.2 选 3 个目标函数:
  - **`FString` / `TArray<X>` 值参数**:TDD `NonTrivialValueParam_FString_TArray_MarshalledByValue`。
  - **`FVector` / `FTransform` SIMD 对齐参数**:TDD `SimdAlignedStruct_FVector_FTransform_HonorAlignment`。
  - **`TSubclassOf<X>` / `TWeakObjectPtr<X>` 模板包装**:TDD `ObjectPtrWrapper_TSubclassOf_TWeakObjectPtr_OpaqueByValue`。
  - 命令:`Tools\RunTests.ps1 -Group Bindings -Filter "...NonTrivialValueParam_*|...SimdAlignedStruct_*|...ObjectPtrWrapper_*"`。

## 2. Phase 2 — UHT 自动化(把 "unexported-symbol" 路径迁移到自动 cross-module emit)

- [ ] 2.0 <!-- Non-TDD --> 调整 `AngelscriptFunctionTableExporter.cs` 的 `[UhtExporter]` 配置:**取消 `ModuleName = "AngelscriptRuntime"` 强制**(现有 same-module shard 仍可通过 generator 内部判别落到 AngelscriptRuntime 自己的 OutputDirectory);保留 `CppFilters = ["AS_FunctionTable_*.cpp"]`。
  - 验证:跑一次 UHT,断言现有 shard 仍按现路径生成(`Intermediate/Build/.../AngelscriptRuntime/UHT/AS_FunctionTable_*_NNN.cpp`),且新增的 cross-module shard 落到目标模块 OutputDirectory。

- [ ] 2.1 <!-- Non-TDD --> 把 1.2/1.7/1.8 的 `AngelscriptCrossModuleHandwrittenExporter.cs` 删除,功能并入 `AngelscriptFunctionTableExporter.cs` / `AngelscriptFunctionTableCodeGenerator.cs`:
  - `AngelscriptFunctionTableCodeGenerator.GenerateModule` 在 entry 类型分流时,新增"CrossModule"类目:`HasLinkableExport==false` 但其它通过的候选,**不再 stub 化**,而是被分流到一份独立的 cross-module shard,落到目标 module 的 OutputDirectory(用 `factory.MakePath(uhtModule, ...)` — 注意 exporter 此时的 `ModuleName` 限制要在 task 2.0 中先放开)。
  - shard 命名:`AS_FunctionTable_<Module>_CrossModule_<NNN>.cpp`,沿用 `MaxEntriesPerShard = 256` 与稳定排序(ClassName/FunctionName Ordinal)。
  - shard 内容(完全对照 1.2 手写形态自动化):
    1. 头文件:仅 `#include "Features/IModularFeatures.h"` + 各目标类头(generator 通过 `factory.GetModuleShortestIncludePath` 收集);**不 include `angelscript.h`、不 include `AngelscriptCrossModuleBindings.h`**(这两条是回归测试要校验的不变量)。
    2. anonymous namespace 内 inline 声明 `FAngelscriptCrossModuleEntry` 与 `FAngelscriptCrossModuleFeature : public IModularFeature` 的完整 layout(与公共头双端一致;字段顺序、宽度、`LayoutVersion` 从 1.0 的 `cross-module-layout-version.txt` 读取;ctor 显式声明)。
    3. **emit 端 `static_assert(sizeof(FAngelscriptCrossModuleEntry) == EXPECTED_BYTES, "...")`**,EXPECTED 与公共头 1.1 同步。
    4. 为每个 entry 按签名 emit `static void Thunk_<Class>_<Func>(UObject* Self, void** Args, void* Ret) { ... }`(generator 按 `UhtFunction` 的参数与返回类型 emit 解包/打包代码,覆盖 D-Param-Marshal 列出的所有形态)。
    5. 静态 `GTable[]` 与 `GFeature(GTable, ...)` ctor 实例化与 `GAutoReg` 三件套(同 1.2)。
  - 同时维护 `AS_FunctionTable_Summary.json` 增加 `crossModuleEntries`(总 + per-module),`AS_FunctionTable_ModuleSummary.csv` 增加 `CrossModuleEntries` 列,`AS_FunctionTable_Entries.csv` `EntryKind` 增加 `CrossModule` 取值。
  - **稳定输出要求**(防 R3):shard 内容按 ClassName / FunctionName Ordinal 排序;不写时间戳、不写 `DateTime.Now`、不写 `Path.GetTempFileName` 类不稳定字段;generator 改动后 `SaveIfChanged` 才能保护引擎模块免重编。
  - 验证:对照 spec `Cross-module emission and registration are observable through metrics` 的两个 scenario。

- [ ] 2.2 <!-- Non-TDD --> `AngelscriptHeaderSignatureResolver.HasLinkableExport` 收敛:删除"非 AngelscriptRuntime + 缺 API 宏 → 不可链接"判定。其余规则不动。失败原因表 `AS_FunctionTable_SkippedReasonSummary.csv` 中 `unexported-symbol` 一行 ⌈预期归零⌋;若 cross-module shard 无法生成的真不可达原因(例如目标模块未启用编译)使用专门的 reason 字符串(实现时新增 `target-module-disabled` 等)。

- [ ] 2.3 <!-- TDD --> headless UHT resolver 单测 `Angelscript.CppTests.UHTToolResolver.NoLongerEmitsUnexportedSymbol_ForCrossModuleCandidate`:用 `AngelscriptNativeTestSupport.h` 起一个 mock UHT session,喂入 1.2 同型签名,断言 `AngelscriptHeaderSignatureResolver.TryBuild` 不再返回 `unexported-symbol`,且生成器把该候选放入 cross-module 类目。
  - 命令:`Tools\RunTests.ps1 -Group Cpp -Filter "Angelscript.CppTests.UHTToolResolver.NoLongerEmitsUnexportedSymbol_*"`。

- [ ] 2.4 <!-- TDD --> `Angelscript.TestModule.Bindings.CrossModuleDirectBind.SameModuleShardWins_When_BothExist`:制造一个"同模块 shard 在 Late+50 已写入 + cross-module shard 在 Late+60 想再次写入"的场景(例如让 AngelscriptRuntime 自身的某个函数同时出现在两表),断言 Late+60 不覆盖,最终 entry 来自 Late+50。

- [ ] 2.5 <!-- TDD --> `Angelscript.TestModule.Bindings.CrossModuleDirectBind.CullOutput_DoesNotDelete_AS_FunctionTable_CrossModule_Files`:跑两次 UHT(模拟无变化的 incremental build),断言两次跑后 cross-module shard 仍然存在(即引擎 `CodeGen` exporter 的 `CullOutput` 不删它)。

- [ ] 2.6 <!-- TDD --> `Angelscript.CppTests.UHTToolResolver.EmittedCpp_DoesNotInclude_AS_SDK_Headers`:对 `Intermediate/Build/.../<Module>/AS_FunctionTable_<Module>_CrossModule_*.cpp` 做静态文本扫描,断言**不包含** `#include "angelscript.h"`、`#include "AngelscriptCrossModuleBindings.h"`、`#include "Core/AngelscriptBinds.h"`、`extern.*Get_AS_Bindings_` 任何字串。

- [ ] 2.7 <!-- TDD --> `Angelscript.CppTests.UHTToolResolver.EmittedCpp_UsesConstructorInstantiation_NoBraceAggregate`:对同一组 emit cpp 做静态扫描,断言**不包含** `static\s+FAngelscriptCrossModuleFeature\s+\w+\s*=\s*\{` 形态(brace-aggregate-init);**包含** `static\s+FAngelscriptCrossModuleFeature\s+\w+\s*\(` 形态(ctor 实例化)。

- [ ] 2.8 <!-- Non-TDD --> Generator 在 `ShouldGenerate` 阶段加 RPC/Net 过滤:`function.FunctionFlags` 含 `Net` / `NetServer` / `NetClient` / `NetMulticast` 任一,直接 skip cross-module 路径,在 `AS_FunctionTable_SkippedReasonSummary.csv` 记录 reason `rpc-net-function`。
  - **实现注意**:也要确保这部分函数仍走原 same-module shard 或 stub 路径,使反射 fallback 仍然能注入并在调用时走 RPC 路由(回归测试 3.x 守住)。

- [ ] 2.9 <!-- TDD --> 三组 RPC 回归测试(用 `Network RPC compilation tests` 已有用例改造或新增):
  - `Angelscript.TestModule.Bindings.CrossModuleDirectBind.RPC_ServerOnly_NotDirectBound_ReflectionRoutesCorrectly`:Server-only RPC 不在 ClassFuncMaps 的 direct-bind 槽,仍走反射 fallback;脚本调用时 marshal 到对端,本地不直接执行函数体。
  - `...RPC_NetMulticast_PreservesMulticast`:多端验证 multicast 仍触发。
  - `...RPC_WithValidation_StillValidates`:验证 `_Validate` 仍被调。
  - 命令:`Tools\RunTests.ps1 -Group Bindings -Filter "...RPC_*"`。

- [ ] 2.10 <!-- Non-TDD --> 扩展 `AngelscriptFunctionTableCodeGenerator.DeleteStaleOutputs`:从只 enumerate AngelscriptRuntime 的 OutputDirectory,改为按 supported module 列表 enumerate **每个目标模块的 OutputDirectory**,删除 `AS_FunctionTable_<Module>_CrossModule_*.cpp` 中不在本次 generated set 的旧文件。AngelscriptRuntime 自己的 stale-cleanup 行为保持不变。
  - **关键不变量**:同次 UHT 运行的 stale-cleanup 仅删本次生成的"模块组"内的旧文件,不动其它模块的 `AS_FunctionTable_*` 文件。

- [ ] 2.11 <!-- TDD --> 两组 stale-cleanup 测试:
  - `Angelscript.CppTests.UHTToolResolver.StaleShard_DeletedOnRebuildAfterFunctionRemoval`:模拟一个候选函数被加 `NotInAngelscript`(或被删除),跑增量 UHT,断言对应 cross-module shard 文件被删除,无残留。
  - `...StaleShard_DeletedOnRebuildAfterModuleRemoval`:模拟一个模块从 supported 列表移除,跑增量 UHT,断言该模块下所有 `AS_FunctionTable_<M>_CrossModule_*.cpp` 被清,且其他模块的 shard 不受影响。

- [ ] 2.12 <!-- TDD --> `Angelscript.TestModule.Bindings.CrossModuleDirectBind.MultipleShardsFromSameModule_AllRegisteredAndIterated_NoModuleNameDedup`:构造或选定一个 cross-module 条目数 > 256 的模块(若实际无,可临时调小 `MaxEntriesPerShard`)产生多 shard,验证 `IModularFeatures::GetModularFeatureImplementations(...)` 返回多条 same-`ModuleName` 的 feature,AS Runtime 全部迭代,所有 entry 都进 `ClassFuncMaps`,无重复 / 无丢失。

## 3. Phase 3 — Modular / Monolithic 双 build 与 OnModularFeatureRegistered 验收

- [ ] 3.1 <!-- TDD --> `Angelscript.TestModule.Bindings.CrossModuleDirectBind.LauncherTargetSimulation_NoFeatureRegistered_FallsBackToReflection`:在测试 setup 阶段临时不注册任何 cross-module feature(或卸载已注册的),跑 1.4 的同一函数,断言:(a) 链接通过(本就无 link 依赖,自然过);(b) `IModularFeatures::Get().GetModularFeatureImplementations(FName("AngelscriptCrossModuleBindings"))` 拉空;(c) `Bind_BlueprintCallable.cpp` 进入反射 fallback 分支;(d) 行为与本 change 之前一致。

- [ ] 3.2 <!-- TDD --> `Angelscript.TestModule.Bindings.CrossModuleDirectBind.OnModularFeatureRegistered_LateLoadedModule`:测试体内手动构造一个独立 `FAngelscriptCrossModuleFeature` 实例(模拟一个"在 Late+60 之后才加载的模块的 self-register"),调 `IModularFeatures::Get().RegisterModularFeature(...)`,断言 AS Runtime 的 `OnModularFeatureRegistered` 回调命中、reader-cast + magic 校验 + injection 全过、最终 entry 写入 `ClassFuncMaps`。
  - 测试结束时调 `UnregisterModularFeature` 清理。

- [ ] 3.3 <!-- TDD --> `Angelscript.TestModule.Bindings.CrossModuleDirectBind.OnModularFeatureRegistered_WorkerThreadInvocation_MarshalsToGameThread`:在测试体的 worker thread(`AsyncTask(ENamedThreads::AnyThread, ...)` 或显式 `FRunnable`)里调 `IModularFeatures::Get().RegisterModularFeature(...)`,断言:(a) AS Runtime 的回调 *没有* 在 worker thread 上直接 mutate `ClassFuncMaps` 或调 `BindMethodDirect`;(b) 一个 `AsyncTask(ENamedThreads::GameThread, ...)` 在 GameThread 上完成实际注入;(c) 注入完成后,GameThread 上后续的脚本调用走直绑路径,无 race condition。

- [ ] 3.4 <!-- TDD --> `Angelscript.TestModule.Bindings.CrossModuleDirectBind.LayoutVersionMismatch_FeatureSkipped_NoCrash`:构造一个 `LayoutVersion` 故意写错的 feature(例如 `0xDEADBEEF`),注册,断言:(a) AS Runtime warn 一行(可通过 `LogAngelscript` capture 测);(b) 该 feature 的 entry 完全没进 `ClassFuncMaps`;(c) 引擎不 crash。

- [ ] 3.5 <!-- TDD --> `Angelscript.TestModule.Bindings.CrossModuleDirectBind.RuntimeNullRangeValidation_RejectsMalformedFeature`:对应 spec 中"runtime null/range validation rejects malformed payloads",分别用 `Count = -1` / `Table = nullptr` / `ModuleName = nullptr` 三种 feature,断言全部跳过、不 crash。

- [ ] 3.6 <!-- TDD --> `Angelscript.CppTests.UHTToolResolver.StaticAssert_SizeofConsistency`:在 headless test 内 `static_assert(sizeof(FAngelscriptCrossModuleEntry) == EXPECTED_BYTES)` 与 `static_assert(sizeof(FAngelscriptCrossModuleFeatureReader) == EXPECTED_BYTES)`,EXPECTED 与 1.1 公共头中的 assert 同步;**generator emit 端的 inline 定义也带相同 assert**,任一不一致编译失败。

- [ ] 3.7 <!-- Non-TDD --> 在真正的 Modular Editor build 与 Monolithic Shipping build 下分别跑一次完整 `Tools\RunTestSuite.ps1 -Suite Default`,确认两种 build 配置下 `Angelscript.TestModule.Bindings.CrossModuleDirectBind.*` 全套 PASS,且对同一组 UFunction `Entry->FuncPtr.IsBound()` 都为 `true`。归档输出到 `Documents/Reports/CrossModuleDualBuild_<Date>.md`(包含两种 build 的 summary 数对比)。

- [ ] 3.8 <!-- Non-TDD --> 在 Launcher engine 安装上跑一次完整 build + `Tools\RunTestSuite.ps1 -Suite Default` 烟测,确认本 change 的存在不破坏现有 Launcher 流程(此时 cross-module shard 不被生成,行为退化到反射 fallback,符合 spec)。归档到 `Documents/Reports/CrossModuleLauncherSmoke_<Date>.md`。

- [ ] 3.9 <!-- TDD --> Shutdown 时序两组:
  - `Angelscript.CppTests.UHTToolResolver.Shutdown_OnPreExit_UnsubscribesOnModularFeatureRegistered_NoCrash`:模拟 `FCoreDelegates::OnPreExit` 触发,断言 AS Runtime 的 `OnModularFeatureRegistered` 订阅被移除;触发后再次模拟一个 `RegisterModularFeature`,AS Runtime 不应再被调进。
  - `Angelscript.CppTests.UHTToolResolver.Shutdown_DllUnload_GuardedUnregister_NoCrash`:模拟"`IModularFeatures` 单例已销毁"的状态(可用 mock / scoped state hack),触发 `~FAutoRegister`(0.4 决策的 idiom 路径),断言不 dereference 已销毁单例。

## 4. Phase 4 — 性能基线、覆盖率与文档

- [ ] 4.1 <!-- TDD --> `Angelscript.TestModule.Bindings.CrossModuleDirectBind.MicroBench_Reflection_VsCrossModuleDirect`:对 1.2 / 2.x 生成的代表性函数(覆盖 void/primitive/object/ref-arg/out-param 形状各 1 个)做 N=100k 调用 micro-bench,采集:(a) 反射 fallback cached、(b) cross-module direct via `asCALL_GENERIC` + raw thunk,记录 per-call ns。
  - 命令:`Tools\RunTests.ps1 -Group Bindings -Filter "...MicroBench_Reflection_VsCrossModuleDirect"`。

- [ ] 4.2 <!-- Non-TDD --> 把 4.1 数据写进 `Documents/Guides/TestPerformance.md` 新增章节"Cross-module direct bind via IModularFeatures vs reflection fallback"。设定 ⌈直绑相对反射 cached 至少减少 30% per-call 耗时⌋ 为本 change 验收门槛;若实测低于此,本 task 转为打开 Open Question Q3,提交后续 change 处理(thiscall 升级路线)。

- [ ] 4.3 <!-- Non-TDD --> 跑全量 UHT,刷新 `AS_FunctionTable_Summary.json` / `AS_FunctionTable_ModuleSummary.csv` / `AS_FunctionTable_Entries.csv` / `AS_FunctionTable_SkippedReasonSummary.csv`。把"`unexported-symbol` 行归零"、"`crossModuleEntries` 总数"、"`rpc-net-function` 计数"作为关键指标补到 `Documents/Guides/BindGapAuditMatrix.md`。

- [ ] 4.4 <!-- Non-TDD --> 更新 `Plugins/Angelscript/AGENTS.md` 中"绑定路径"小节,新增 cross-module direct bind 经由 `IModularFeatures` 的存在与启用条件,显式说明 "AngelscriptRuntime 不 link 任何引擎模块" 是本 change 的硬性约束;并加入"RPC/Net 函数继续走反射 fallback"与"`cross-module-layout-version.txt` bump 流程"两节维护说明。`AGENTS.md`(根)若有相应章节也同步,但**不**修改公开 README。

## 5. Phase 5 — 收尾、Validate、Apply 准备

- [ ] 5.1 <!-- Non-TDD --> 检查 1.2/1.7/1.8 里的临时硬编码已在 2.1 全部清理;`AngelscriptCrossModuleHandwrittenExporter.cs` 已删除;0.x 的 link / IModularFeatures probe 仍然保留为 regression。

- [ ] 5.2 <!-- Non-TDD --> 跑 `openspec validate "improve-as-direct-bind-coverage" --strict --json`,无 error。warnings 在 review 时反馈。

- [ ] 5.3 <!-- Non-TDD --> 跑 `Tools\RunTestSuite.ps1 -Suite Default` 全量套件;断言基线无 regression(`Documents/Guides/TestCatalog.md` 中 baseline 数量不下降,`#ue57-headless` 之外无新增 Disabled)。

- [ ] 5.4 <!-- Non-TDD --> 给 PR 准备就绪:按 `Documents/Rules/GitCommitRule.md` 整理 commit、关联本 change ID;走 `superpowers:requesting-code-review` 流程。**PR description 要明确两条**:(a) `AngelscriptRuntime.Build.cs` 不引入任何引擎模块依赖(给 reviewer 一个直接的回归边界);(b) RPC/Net 函数继续走反射 fallback,网络复制语义不变。
