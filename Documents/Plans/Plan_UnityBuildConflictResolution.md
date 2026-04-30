# Unity Build 切回 ON 后的同名符号冲突清理计划

## 背景与目标

`Plugins/Angelscript/Source/AngelscriptTest/AngelscriptTest.Build.cs` 与
`Plugins/Angelscript/Source/AngelscriptEditor/AngelscriptEditor.Build.cs`
长期写着 `bUseUnity = false;`，把 unity build 关掉。这条历史 workaround 是为了
绕过当时大量 `_Private` namespace 同名 helper / 局部常量被 UBT auto-wrap +
`using namespace <File>_Private;` 注入后产生的二义性问题。

目前两个测试模块的源文件数量已经显著增长，关闭 unity build 直接拖慢全量构建
（每个 `.cpp` 都独立预处理一次 + 大量重复模板实例化），且这层"逃避"也阻挡了
后续把测试模块产物纳入正常 release 构建管道。

本 Plan 的目标是：**把 `bUseUnity = false` 移除（已完成），按主题逐批清掉
unity ON 后暴露的冲突，最终让 `RunBuild.ps1` 在 unity 默认行为下零编译错误**，
并在 `AGENTS.md` / `TestConventions.md` 里固化"不允许多文件同名局部 helper /
局部常量"的约束，避免未来再次回流。

冲突主要来自两类源头：

1. **真正的 unity 引起的同名冲突**：每个 `.cpp` 内 anonymous / `_Private`
   namespace 里的 helper 函数、constexpr 常量、本地 struct 类型，被 UBT 在
   unity 合并时通过 `using namespace <File>_Private;` 暴露到 file-scope，
   多文件同名时直接二义性 (C2668 / C2872)。
2. **被 unity 切换"顺带暴露"的 pre-existing bug**：`AngelScriptSDK/` 下 10 个
   CQTest 风格文件用了裸调 `TestNotNull / TestTrue / TestEqual / TestFalse`，
   而 CQTest 的 `TEST_METHOD` 体内这些方法不在裸命名空间可见，必须写
   `TestRunner->TestNotNull(...)`。这部分 commit `4f5f41b`（CQTest 改造）
   就遗留下来了，原本被前面的编译错误挡住没暴露，现在前面错误清掉后浮出来。

## 范围与边界

- **范围内**
  - `Plugins/Angelscript/Source/AngelscriptTest/` 全部 unity 冲突冷修
  - `Plugins/Angelscript/Source/AngelscriptEditor/Tests/` 全部 unity 冲突冷修
  - `AngelScriptSDK/` 10 个 CQTest 风格文件的 `TestNotNull → TestRunner->TestNotNull` 修复
  - `AngelscriptTest.Build.cs` / `AngelscriptEditor.Build.cs` 中保持
    `bUseUnity = false;` 已移除的状态；不再以注释形式回流
  - `AGENTS.md` 与 `TestConventions.md` 同步收口"局部 helper / 常量必须唯一化或抽到 Shared"约束
- **范围外**
  - 不顺手做新功能；不把现有测试断言强度扩大
  - 不改动 `AngelscriptRuntime` / `AngelscriptLoader` / `AngelscriptUHTTool` 模块（这两个模块本来就是 unity ON）
  - 不动 SDK 文件夹里跟 unity 无关的逻辑（仅做 `TestNotNull` → `TestRunner->TestNotNull` 这种最小 surgical 改动）
  - 不重命名/拆分仍用 `_Private` 的命名 namespace，除非具体冲突要求
- **执行边界**
  - 每个 Phase 结束后必须跑一次 `Tools/RunBuild.ps1`，比对错误数下降趋势
  - 不允许"先全改再统一构建"——每个 Phase 改动幅度都应可独立 build 验证
  - 不允许通过 `bUseUnity = false` / `MinSourceFilesForUnityBuildOverride` /
    `bUseAdaptiveUnityBuild = false` 等 build option 回退；冲突必须从源头解决

## 当前事实状态快照

> 数据来自 2026-04-30 最近一次 unity ON build（`Saved/Build/agent-build/<RunId>/` 与
> `terminals/501959.txt`）。

### 错误码分布（共 490 个错误）

| 错误码 | 数量 | 含义 | 主要来源 |
| --- | --- | --- | --- |
| C3861 | 186 | 找不到标识符 | `AngelScriptSDK/` 10 个 CQTest 文件裸调 `TestNotNull/TestTrue/TestEqual` |
| C2668 | 151 | 重载二义性 | 多文件同名 `_Private` helper 函数 |
| C2872 | 52 | 同名全局二义性 | 多文件同名 `_Private` constexpr / FName |
| C2664 | 50 | 无法转换参数 | 上述二义性的级联 |
| C2665 | 12 | 重载选择失败 | 同上级联 |
| C4459 | 10 | 局部声明遮蔽 | `using namespace _Private` 引入的 shadow |
| 其他 | 29 | C2661/C2760/C2562/C3878/C2039/C2672/C2374/C2086/C2737/C2059/C3536/C1003 各 1-7 | 多为级联 |

### Top 错误文件（按错误数）

| 计数 | 文件 |
| --- | --- |
| 50 | `AngelScriptSDK/AngelscriptDebuggerValueTests.cpp`（CQTest 裸调） |
| 45 | `AngelScriptSDK/AngelscriptCompilerTests.cpp`（CQTest 裸调） |
| 42 | `AngelScriptSDK/AngelscriptASSDKTypeTests.cpp`（CQTest 裸调） |
| 33 | `AngelScriptSDK/AngelscriptBuilderTests.cpp`（CQTest 裸调） |
| 31 | `AngelscriptEditor/Tests/AngelscriptDirectoryWatcherTests.cpp`（unity 同名 helper） |
| 27 | `AngelScriptSDK/AngelscriptASSDKOperatorTests.cpp`（CQTest 裸调） |
| 27 | `Bindings/AngelscriptMathOrientationFunctionLibraryTests.cpp`（unity 同名 helper） |
| 18 | `AngelscriptEditor/Tests/AngelscriptBlueprintImpactScannerTests.cpp` |
| 17 | `AngelScriptSDK/AngelscriptBytecodeTests.cpp`（CQTest 裸调） |
| 16 | `Core/AngelscriptAbilityTaskLibraryTests.cpp`（unity 同名 helper） |
| 15 | `AngelscriptEditor/Tests/AngelscriptEditorModuleOnPostEngineInitTests.cpp` |
| 14 | `AngelScriptSDK/AngelscriptCallingConvTests.cpp`（CQTest 裸调） |
| 12 | `AngelScriptSDK/AngelscriptASSDKFunctionTests.cpp`（CQTest 裸调） |
| 9 | `Bindings/AngelscriptFStringBindingsTests.cpp` |
| 8 | `Core/AngelscriptGASAbilitySystemCallbackTests.cpp` |

### 已完成（不要重做）

| 内容 | 落点 |
| --- | --- |
| `Read*PropertyChecked` helper 抽取 | `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptFunctionalTestUtils.h`（新增 `ReadIntPropertyChecked` / `ReadUInt64PropertyChecked` / `ReadStringPropertyChecked` inline 包装） |
| `AddCollisionBox` helper 抽取 | `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptCollisionTestHelpers.h`（新建文件，含 `AddQueryOnlyCollisionBox` / `AddBlockAllDynamicCollisionBox` 两个 inline 函数） |
| Debugger session/breakpoint helper 抽取 | `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptDebuggerTestHelpers.h`（新建文件，含 `StartDebuggerSession` / `StartDebuggerSessionWithVersionHandshake` / `WaitForBreakpointCount` / `WaitForSpecificBreakpoint`） |
| 同名 `.cpp` 文件去重（UBT adaptive non-unity 阻塞） | `AngelScriptSDK/Angelscript{Function,Operator,Type}Tests.cpp` 已用 `git mv` 改名为 `AngelscriptASSDK{Function,Operator,Type}Tests.cpp` |
| `bUseUnity = false;` 移除 | `Plugins/Angelscript/Source/AngelscriptTest/AngelscriptTest.Build.cs` 与 `Plugins/Angelscript/Source/AngelscriptEditor/AngelscriptEditor.Build.cs` 已删除该行；不要回流 |

## 影响范围

### 操作类型定义

本次冲突清理涉及以下操作（按需组合）：

- **抽 helper 到 Shared 头**：把多文件同名的 helper 函数 / 常量提到
  `Plugins/Angelscript/Source/AngelscriptTest/Shared/<NewHeader>.h` 里以
  `inline` / `constexpr` 形式落地，调用方加 `#include` 并删除本地副本。
- **抽 helper 到 Editor Shared 目录**：Editor 模块独立的共享头落到
  `Plugins/Angelscript/Source/AngelscriptEditor/Tests/Shared/<NewHeader>.h`
  （目录可能需要新建）；不要把 Editor 测试 helper 倒灌到
  `AngelscriptTest/Shared/`，避免跨模块循环依赖。
- **唯一化命名**：仅当 helper 高度文件特化、不值得抽到共享头时，把局部
  helper 改名加文件前缀（例如 `MakeFileChange` → `MakeDirectoryWatcherFileChange`），
  或把它从 anonymous / `_Private` namespace 挪到唯一命名 namespace 里，并
  避免再 `using namespace`。
- **删除本地副本**：抽到共享头后，原文件中的本地定义必须删除，并保留
  只读式调用（不要保留 `inline` 定义但加 `// kept for backward compat`
  这种回流性写法）。
- **CQTest 调用形式批量替换**：`AngelScriptSDK/` 下 10 个文件中
  `TestNotNull(...)` / `TestTrue(...)` / `TestEqual(...)` / `TestFalse(...)` /
  `TestNotEqual(...)` 全部替换为 `TestRunner->TestNotNull(...)` /
  `TestRunner->TestTrue(...)` / `TestRunner->TestEqual(...)` /
  `TestRunner->TestFalse(...)` / `TestRunner->TestNotEqual(...)`。
  注意 `TestRunner` 是 CQTest 暴露的静态指针；调用必须用 `->`，不要写成 `.`。
- **AddInfo / AddError 等同类形式同步替换**：同一文件内若也存在裸调
  `AddInfo / AddError / AddWarning`，按相同规则改成 `TestRunner->AddInfo(...)` 等。

### 按目录分组的文件清单

> 每个文件在每个 Phase 内只动一次，避免跨 Phase 来回 touch。

**Phase 2 — `AngelscriptTest/Bindings/`（1 个文件）**

- `Bindings/AngelscriptMathOrientationFunctionLibraryTests.cpp` — 抽 helper：`ExecuteValueFunction` / `VerifyVector` / `VerifyTransform` 提到新建 `Shared/AngelscriptMathOrientationTestHelpers.h`（或 `Shared/AngelscriptBindingsAssertions.h` 复用）

**Phase 3 — `AngelscriptTest/Core/` GAS 测试常量（6 个文件）**

- `Core/AngelscriptGASAttributeSetTests.cpp` — 抽常量 + 删本地副本
- `Core/AngelscriptGASAttributeSetRuntimeTests.cpp` — 抽常量 + 删本地副本
- `Core/AngelscriptGASAttributeChangedDataMixinTests.cpp` — 抽常量 + 删本地副本
- `Core/AngelscriptGASAbilitySystemCallbackTests.cpp` — 抽常量 + 删本地副本
- `Core/AngelscriptGASAsyncLibraryTests.cpp` — 抽常量 + 删本地副本
- `Core/AngelscriptGameplayEffectUtilsTests.cpp` — 抽常量 + 删本地副本

新建：`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptGASTestConstants.h`，
里面用 `inline const FName` 暴露 `HealthAttributeName` / `ManaAttributeName`，
以及 `inline constexpr float` 暴露 `InitialHealthValue` / `UpdatedHealthValue`。

**Phase 4 — `AngelscriptTest/Core/AngelscriptAbilityTaskLibraryTests.cpp`（1 个文件 + 同主题 1 个）**

- `Core/AngelscriptAbilityTaskLibraryTests.cpp` — 抽 helper：`ExpectTaskOwnership` / `ExpectTaskOwnershipWithoutInstanceName` / `GetPrimaryTestAbilityInstance` / `FindAbilitySpec` 抽到新建 `Shared/AngelscriptAbilityTaskTestHelpers.h`
- `Core/AngelscriptAbilityTaskLibraryObserverTests.cpp` — 调用方更新为 include 新头并删除本地副本（如果它有同名 helper）

**Phase 5 — `AngelscriptTest/Debugger/`（1 个文件）**

- `Debugger/AngelscriptDebuggerStepOverInFunctionTests.cpp` — 把 `AssertFrameMatches` / `AssertStopReason` 追加进已存在的 `Shared/AngelscriptDebuggerTestHelpers.h`

**Phase 6 — `AngelscriptEditor/Tests/`（12 个文件）**

新建目录：`Plugins/Angelscript/Source/AngelscriptEditor/Tests/Shared/`

新建头：
- `Tests/Shared/AngelscriptDirectoryWatcherTestHelpers.h` — `ContainsFilenamePair` / `MakeFileChange` / `MakeTempWatcherRoot` / `FMockDirectoryWatcher`
- `Tests/Shared/AngelscriptBlueprintImpactTestHelpers.h` — `CleanupBlueprint` / `CreateTransientBlueprintChild`
- `Tests/Shared/AngelscriptEditorModuleTestHelpers.h` — `EnsureClassReloadHelperInitialized` / `CleanupTransientAngelscriptDataSources` / `CollectTransientAngelscriptDataSources` / `CountActiveDataSourceName`

下列文件全部按"include 共享头 + 删本地副本"操作：

- `Tests/AngelscriptDirectoryWatcherTests.cpp`（C2668: `ContainsFilenamePair` 13 处 / `MakeFileChange` 10 处）
- `Tests/AngelscriptDirectoryWatcherRootResolutionTests.cpp`（C2668: `MakeTempWatcherRoot` 8 处）
- `Tests/AngelscriptEditorModuleDirectoryWatcherTests.cpp`（C2668: `ContainsFilenamePair` 3 处）
- `Tests/AngelscriptBlueprintImpactScannerTests.cpp`（C2668: `CleanupBlueprint` 7 / `CreateTransientBlueprintChild` 6）
- `Tests/AngelscriptBlueprintImpactScanTests.cpp`
- `Tests/AngelscriptBlueprintImpactScanNoImpactTests.cpp`
- `Tests/AngelscriptBlueprintImpactAnalyzeLoadedBlueprintTests.cpp`
- `Tests/AngelscriptBlueprintImpactAssetDiscoveryTests.cpp`
- `Tests/AngelscriptEditorModuleLifecycleTests.cpp`（C2668: `CollectTransientAngelscriptDataSources` 4）
- `Tests/AngelscriptEditorModuleOnPostEngineInitTests.cpp`（C2668: `CleanupTransientAngelscriptDataSources` 3 / `CountActiveDataSourceName` 3）
- `Tests/AngelscriptClassReloadHelperClassReloadTests.cpp`（C2668: `EnsureClassReloadHelperInitialized` 3）
- `Tests/AngelscriptClassReloadHelperPostReloadTests.cpp`
- `Tests/AngelscriptClassReloadHelperDelegateTests.cpp`
- `Tests/AngelscriptClassReloadHelperStructTests.cpp`
- `Tests/AngelscriptEditorModuleSettingsTests.cpp`
- `Tests/AngelscriptEditorModulePopupTests.cpp`
- `Tests/AngelscriptContentBrowserDataSourceTests.cpp`

**Phase 7 — `AngelscriptTest/AngelScriptSDK/` CQTest 裸调修复（10 个文件）**

> 与 unity 无关，但与"切回 unity ON 后暴露的下游错误"耦合，必须在
> Phase 1-6 收完后才能验证零错误。所以放在这里串行收尾。

- `AngelScriptSDK/AngelscriptDebuggerValueTests.cpp`（50）
- `AngelScriptSDK/AngelscriptCompilerTests.cpp`（45）
- `AngelScriptSDK/AngelscriptASSDKTypeTests.cpp`（42）
- `AngelScriptSDK/AngelscriptBuilderTests.cpp`（33）
- `AngelScriptSDK/AngelscriptASSDKOperatorTests.cpp`（27）
- `AngelScriptSDK/AngelscriptBytecodeTests.cpp`（17）
- `AngelScriptSDK/AngelscriptCallingConvTests.cpp`（14）
- `AngelScriptSDK/AngelscriptASSDKFunctionTests.cpp`（12）
- `AngelScriptSDK/AngelscriptConfigGroupTests.cpp`（约 6）
- `AngelScriptSDK/AngelscriptDefaultTraitTests.cpp`（约 2）

**Phase 8 — 零散冲突（按构建日志补漏，约 10 个文件）**

- 重复 `ModuleName` / `SourceFilename` / `ScriptSource` constexpr / inline const：
  按文件唯一化命名，例如 `ModuleName` → `ModuleName_ExecutionArgumentSlotOrderMatrix`，
  或把它放进 `static constexpr ANSICHAR ModuleName_<TestNameSuffix>[]` 形式。
- `FExpectedGlobalInt` 在多个 Bindings 文件里重复声明 → 提到
  `Shared/AngelscriptBindingsAssertions.h`（已存在），删本地 struct。

涉及（按 build log 提及，可能需要按实际再扫一次）：

- `Bindings/AngelscriptTArrayBindingsTests.cpp`
- `Bindings/AngelscriptTArraySyntaxCompatBindingsTests.cpp`
- `Bindings/AngelscriptUILayoutBindingsTests.cpp`
- `Bindings/AngelscriptUObjectBindingsTests.cpp`
- `Bindings/AngelscriptUserWidgetBindingsTests.cpp`
- `Bindings/AngelscriptWorldFunctionLibraryTests.cpp`
- `Bindings/AngelscriptFStringBindingsTests.cpp`
- `Compiler/AngelscriptCompilerPipelineNamingTests.cpp`
- `Compiler/AngelscriptCompilerPipelinePropertyDefaultTests.cpp`
- `Compiler/AngelscriptCompilerPipelinePropertyMetadataTests.cpp`
- `Compiler/AngelscriptCompilerPipelinePropertyReplicationConditionTests.cpp`
- `Compiler/AngelscriptCompilerPipelineRangeForTests.cpp`
- `Compiler/AngelscriptCompilerPipelineSpecifierMetadataTests.cpp`
- `Compiler/AngelscriptCompilerPipelineStructTests.cpp`
- `Functional/Execution/AngelscriptExecutionArgumentMarshallingTests.cpp`
- `Functional/Execution/AngelscriptExecutionNestedCallTests.cpp`
- `Functional/Execution/AngelscriptExecutionScriptRangeTests.cpp`
- `Functional/Interface/AngelscriptInterfaceNativePointerOffsetTests.cpp`
- `StaticJIT/AngelscriptPrecompiledDataArchiveTests.cpp`
- `StaticJIT/AngelscriptStaticJITGeneratedOutputTests.cpp`
- `StaticJIT/AngelscriptStaticJITNativeFormTests.cpp`

## 分阶段执行计划

> 通用约定：每个 Phase 结束都跑一次 `Tools/RunBuild.ps1 -Label unity-conflict-<phase> -TimeoutMs 600000`，把错误数对比放到 commit message 里。

### Phase 1：扫漏并补齐基线快照

> 目标：在动手具体修复前，把当前 unity ON build 的"完整冲突地图"扫一次，得到比 P0 更细的 phase 划分依据；不动任何源码。

- [ ] **P1.1** 跑一次完整 unity ON build 并把日志归档
  - 走 `powershell.exe -NoProfile -ExecutionPolicy Bypass -File Tools\RunBuild.ps1 -Label unity-conflict-baseline -TimeoutMs 600000`，让产物落到 `Saved/Build/unity-conflict-baseline/<RunId>/`
  - 把 `compile.log` 里所有 `error C\d+` 抽出来按 (错误码, 符号, 文件) 聚合成 CSV，命名 `Documents/Plans/Plan_UnityBuildConflictResolution/baseline-<date>.csv`
  - 这一步只是 ingest，本身不 commit 源码改动；CSV 跟 Plan 一起 commit 即可
- [ ] **P1.1** 📦 Git 提交：`[Docs/Plans] Docs: ingest unity build conflict baseline csv`

- [ ] **P1.2** 校对本 Plan 的 Phase 6 / Phase 8 文件清单
  - 用 P1.1 产出的 CSV 重新校对 Phase 6 / Phase 8 的文件列表，必要时补充新文件，删除已不报错的旧条目
  - 同步更新本 Plan 中"按目录分组的文件清单"和顶部"Top 错误文件"表格
  - 不允许只修文档不更新数字
- [ ] **P1.2** 📦 Git 提交：`[Docs/Plans] Docs: refresh unity conflict file list to match latest baseline`

### Phase 2：MathOrientationFunctionLibrary helper

> 目标：把 `Bindings/AngelscriptMathOrientationFunctionLibraryTests.cpp` 中重复定义的执行/校验 helper 抽到共享头，去掉文件内的本地副本，验证该文件零错误后再进 Phase 3。这一支独立、风险小，先吃软的稳定方法论。

- [ ] **P2.1** 抽 `ExecuteValueFunction` / `VerifyVector` / `VerifyTransform` 到共享头
  - 当前这三个 helper 与同名 helper 在 anonymous / `_Private` namespace 里冲突，不能继续就地保留
  - 优先看 `Shared/AngelscriptBindingsAssertions.h` 是否已经有同名/类似函数；如果有，复用并删本地副本；如果没有，新建 `Shared/AngelscriptMathOrientationTestHelpers.h`，提供 `inline` 实现
  - 共享头里所有函数必须 `inline`，签名保持现有调用兼容；不允许在共享头里 `using namespace`
  - 修改完后 `Tools/RunBuild.ps1 -Label unity-conflict-p2 -TimeoutMs 600000`，单文件错误数应该降到 0
- [ ] **P2.1** 📦 Git 提交：`[AngelscriptTest/Bindings] Refactor: extract math orientation test helpers into shared header`

### Phase 3：GAS 测试共享常量

> 目标：把 6 个 GAS 测试文件里同名的 `HealthAttributeName` / `ManaAttributeName` / `InitialHealthValue` / `UpdatedHealthValue` 抽到共享头，去掉所有本地副本，相当于把 23+4+2+1=30 处 C2872 一口气清掉。

- [ ] **P3.1** 新建 `Shared/AngelscriptGASTestConstants.h`
  - `inline const FName HealthAttributeName = ...;`（`GET_MEMBER_NAME_CHECKED(UAngelscriptHealthAttributeSet, Health)` 等具体值参照原文件的写法搬过来；不要变更属性名）
  - `inline const FName ManaAttributeName = ...;`
  - `inline constexpr float InitialHealthValue = 10.f;`
  - `inline constexpr float UpdatedHealthValue = 25.f;`
  - 头文件必须自包含必要的 include（GAS attribute set 头）；不要让调用方再被迫额外 include
  - 不要 `using namespace`，不要把它们放在新 namespace 里——保持顶层 `inline` 就好
- [ ] **P3.1** 📦 Git 提交：`[AngelscriptTest/Core] Feat: add shared GAS test constants header`

- [ ] **P3.2** 把 6 个 GAS 测试文件的本地副本删除并改 include
  - 文件清单见上方"影响范围"中的 Phase 3 部分
  - 删除范围：本地 `const FName HealthAttributeName = ...` / `const FName ManaAttributeName = ...` / `const float InitialHealthValue = ...` / `const float UpdatedHealthValue = ...`，以及它们所在的 anonymous / `_Private` namespace 包裹（如果包裹里只剩这些常量也一并删，避免空 namespace）
  - 调用点不变（共享头里 `inline` 全局命名一致）
  - 修改完后 `Tools/RunBuild.ps1 -Label unity-conflict-p3 -TimeoutMs 600000`，C2872 中 GAS 常量类应全部消失
- [ ] **P3.2** 📦 Git 提交：`[AngelscriptTest/Core] Refactor: deduplicate GAS attribute constants across tests`

### Phase 4：AbilityTask 测试 helper

> 目标：清掉 `Core/AngelscriptAbilityTaskLibraryTests.cpp` 中 16 个错误，集中在 `ExpectTaskOwnership` / `ExpectTaskOwnershipWithoutInstanceName` / `GetPrimaryTestAbilityInstance` / `FindAbilitySpec` 等 helper。

- [ ] **P4.1** 新建 `Shared/AngelscriptAbilityTaskTestHelpers.h`
  - 把 `ExpectTaskOwnership(FAutomationTestBase&, const TCHAR*, UAbilityTask_*, UAngelscriptGASTestAbility*, UAngelscriptAbilitySystemComponent*, const FGameplayAbilitySpecHandle, FName)` 等签名抽过来
  - 同步抽 `ExpectTaskOwnershipWithoutInstanceName` / `GetPrimaryTestAbilityInstance` / `FindAbilitySpec` / `GetAbilityInstanceCount` 等
  - 必须 `inline`，必须包含完整 include（`AbilityTask_Wait*.h` / GAS spec 头等）
  - 共享头不允许声明任何 UCLASS / USTRUCT
- [ ] **P4.1** 📦 Git 提交：`[AngelscriptTest/Core] Feat: add shared ability task test helpers header`

- [ ] **P4.2** 把 `AngelscriptAbilityTaskLibraryTests.cpp` 与 `AngelscriptAbilityTaskLibraryObserverTests.cpp` 改 include 并删本地副本
  - 删除两文件中本地的 `ExpectTaskOwnership` 等 helper 定义
  - 检查残留 anonymous / `_Private` namespace 是否还有别的内容；如果只剩噪音常量，顺手清掉
  - 修改完后 `Tools/RunBuild.ps1 -Label unity-conflict-p4 -TimeoutMs 600000`，AbilityTask 测试目录应零错误
- [ ] **P4.2** 📦 Git 提交：`[AngelscriptTest/Core] Refactor: switch ability task tests to shared helpers`

### Phase 5：Debugger StepOver helper 补充

> 目标：把 `Debugger/AngelscriptDebuggerStepOverInFunctionTests.cpp` 中 `AssertFrameMatches` / `AssertStopReason` 追加进已存在的 `Shared/AngelscriptDebuggerTestHelpers.h`。注意这个共享头是 Phase 2A 已落地的，不要重新建一个新头。

- [ ] **P5.1** 在 `Shared/AngelscriptDebuggerTestHelpers.h` 里追加 `AssertFrameMatches` / `AssertStopReason`
  - 沿用现有 `inline bool` 风格；签名保持与原本地 helper 完全一致
  - 同时检查别的 debugger 测试文件是否有类似 helper 但还没抽，顺手抽
- [ ] **P5.1** 📦 Git 提交：`[AngelscriptTest/Debugger] Refactor: extend shared debugger helpers with frame and stop assertions`

- [ ] **P5.2** 删除 `AngelscriptDebuggerStepOverInFunctionTests.cpp` 中本地副本
  - 确认调用点已能解析到共享头
  - 修改完后 `Tools/RunBuild.ps1 -Label unity-conflict-p5 -TimeoutMs 600000`，Debugger 目录零错误
- [ ] **P5.2** 📦 Git 提交：`[AngelscriptTest/Debugger] Refactor: drop local frame and stop helpers in step-over tests`

### Phase 6：AngelscriptEditor/Tests/ helper 大批量抽取

> 目标：Editor 模块测试目录是这一轮冲突最密集的区域（约 80+ 错误），需要新建独立的共享目录与三份共享头。

- [ ] **P6.1** 新建 `AngelscriptEditor/Tests/Shared/` 目录与三份共享头
  - `Tests/Shared/AngelscriptDirectoryWatcherTestHelpers.h` — 提供 `ContainsFilenamePair(const TArray<TPair<FString,FString>>&, const FString&, const FString&)` / `MakeFileChange(const FString&, FFileChangeData::EFileChangeAction)` / `MakeTempWatcherRoot(...)`，`FMockDirectoryWatcher` 类也搬到这里（必须放进唯一 namespace `AngelscriptEditorTestSupport`）
  - `Tests/Shared/AngelscriptBlueprintImpactTestHelpers.h` — 提供 `CleanupBlueprint(UBlueprint*)` / `CreateTransientBlueprintChild(...)`
  - `Tests/Shared/AngelscriptEditorModuleTestHelpers.h` — 提供 `EnsureClassReloadHelperInitialized()` / `CleanupTransientAngelscriptDataSources()` / `CollectTransientAngelscriptDataSources()` / `CountActiveDataSourceName(FName)`
  - 必须保证共享头 `inline` 化、`#pragma once` 双保险；不能引入对 `AngelscriptTest/` 模块的 include 依赖（避免反向耦合）
  - 共享头里如有 `class FMockDirectoryWatcher` 这种重 type，需要确保 IDirectoryWatcher 接口在 Editor 模块的 PublicDependencyModuleNames 已暴露；如未暴露则在 `Tests/Shared/` 中包一层 `inline` 工厂函数
- [ ] **P6.1** 📦 Git 提交：`[AngelscriptEditor/Tests] Feat: introduce shared editor tests helper headers`

- [ ] **P6.2** 把 12 个 Editor/Tests 文件改成 include 共享头并删本地副本
  - 文件清单见上方"影响范围"Phase 6
  - 修改时优先用 grep 在该文件搜 helper 名定位本地副本，避免遗漏
  - 检查 anonymous / `_Private` 包裹——如果包裹里只剩 helper 副本，整个删；如果还有别的，仅删 helper
  - 修改完后 `Tools/RunBuild.ps1 -Label unity-conflict-p6 -TimeoutMs 600000`，Editor 模块零错误
- [ ] **P6.2** 📦 Git 提交：`[AngelscriptEditor/Tests] Refactor: deduplicate watcher and module test helpers across editor tests`

### Phase 7：AngelScriptSDK CQTest 裸调修复

> 目标：把 10 个文件里全部裸调的 `TestNotNull / TestTrue / TestEqual / TestFalse / TestNotEqual / AddInfo / AddError / AddWarning` 等 CQTest 静态指针调用改写为 `TestRunner->...` 形式。这个 Phase 与 unity 无关，但只能在 unity 冲突清完后才能验证零错误。

- [ ] **P7.1** 单文件试点：`AngelScriptSDK/AngelscriptDefaultTraitTests.cpp`
  - 这个文件错误数最少（≈2），先用它做样板，确认替换规则没有副作用
  - 替换规则：所有 file-scope 的 anonymous / `_Private` 中的 helper 调用 `Test.TestNotNull(...)` 形式保持不变；TEST_METHOD 体内裸调 `TestNotNull(...)` 改成 `TestRunner->TestNotNull(...)`
  - 注意 BEFORE_ALL / AFTER_ALL（static）里 `TestRunner` 同样可见；但里面常用的是 `Test.TestNotNull` 形式（如果有 `FAutomationTestBase&` 参数）——具体看上下文
  - 修改完后用 `Tools/RunBuild.ps1 -Label unity-conflict-p7-trait -TimeoutMs 600000` 验证该文件零错误
- [ ] **P7.1** 📦 Git 提交：`[AngelscriptTest/AngelScriptSDK] Fix: route CQTest assertions through TestRunner pointer in default trait tests`

- [ ] **P7.2** 批量推进 9 个剩余 SDK 文件
  - 顺序：`AngelscriptConfigGroupTests` → `AngelscriptASSDKFunctionTests` → `AngelscriptCallingConvTests` → `AngelscriptBytecodeTests` → `AngelscriptASSDKOperatorTests` → `AngelscriptBuilderTests` → `AngelscriptASSDKTypeTests` → `AngelscriptCompilerTests` → `AngelscriptDebuggerValueTests`（按当前错误数从小到大，方便每步验证）
  - 每改完 2-3 个跑一次 `Tools/RunBuild.ps1 -Label unity-conflict-p7-batch<N> -TimeoutMs 600000` 验证错误数下降
  - 最终一轮全量构建必须证明 SDK 这 10 个文件累计 186 个 C3861 全部清零
- [ ] **P7.2** 📦 Git 提交：`[AngelscriptTest/AngelScriptSDK] Fix: route CQTest assertions through TestRunner pointer across SDK tests`

### Phase 8：零散冲突清理与终态构建

> 目标：处理 P1.1 baseline 与上面 Phase 没覆盖的零散冲突（`ModuleName` / `SourceFilename` / `ScriptSource` / `FExpectedGlobalInt` 等），跑全量 build + 全量 RunTestSuite Smoke，把 unity ON 真正定型。

- [ ] **P8.1** `FExpectedGlobalInt` 抽到 `Shared/AngelscriptBindingsAssertions.h`
  - 共享头里以 `struct FExpectedGlobalInt { ... }` 定义；如果已经有同名 struct 但字段不一致，先评估是否合并或在共享头里加成员；不允许在共享头里以 namespace 隔离两份重复 struct
  - 涉及文件按 P1.1 CSV 实际清单收口
- [ ] **P8.1** 📦 Git 提交：`[AngelscriptTest/Shared] Refactor: hoist FExpectedGlobalInt struct into shared bindings assertions`

- [ ] **P8.2** 唯一化局部 `ModuleName` / `SourceFilename` / `ScriptSource` 常量
  - 同一个文件里把 `constexpr ANSICHAR ModuleName[]` 改名加上一个文件特化后缀（用 `<TestSuiteName>` 后缀），避免再被 unity 注入到全局
  - 不要为这种"高度文件特化"的常量再新建共享头；保持就地唯一即可
  - 关注每个文件里同名常量是否真的在多文件之间冲突；只动确实冲突的文件，避免无意义大改
- [ ] **P8.2** 📦 Git 提交：`[AngelscriptTest] Refactor: uniquify per-file ModuleName and ScriptSource constants under unity build`

- [ ] **P8.3** 跑全量 build + Smoke，确认 unity ON 下零错误
  - `Tools/RunBuild.ps1 -Label unity-conflict-final -TimeoutMs 900000`
  - `Tools/RunTestSuite.ps1 -Suite Smoke -LabelPrefix unity-conflict-smoke -TimeoutMs 600000`
  - 把 build/test 总结贴到 commit message 里：`Errors: 0 (was 490). Smoke: <pass>/<total>`
- [ ] **P8.3** 📦 Git 提交：`[AngelscriptTest] Chore: validate unity-on build and smoke pass after conflict cleanup`

### Phase 9：约束固化与文档同步

> 目标：把这一轮踩到的"多文件同名局部 helper / 常量"约束写进 `AGENTS.md` 和 `TestConventions.md`，并在 `Plan_StatusPriorityRoadmap.md` / `Plan_OpportunityIndex.md` 里同步状态，避免再次回流。

- [ ] **P9.1** 更新 `Plugins/Angelscript/AGENTS.md` 与 `Documents/Guides/TestConventions.md`
  - 增补 "测试模块 unity build 约束" 小节：每个测试 `.cpp` 不允许在 anonymous / `_Private` namespace 里定义任何 helper 函数 / 常量 / struct，除非该 helper 仅被本文件使用且名字带文件特化后缀；多文件共用的 helper 必须落到 `Shared/` 或 `Tests/Shared/` 共享头里以 `inline`/`constexpr` 形式提供
  - 同时在文档里记录"`bUseUnity = false` 不允许在测试模块 `.Build.cs` 中使用，需通过抽 helper / 唯一化命名解决冲突"
  - 中文版 `AGENTS_ZH.md` 同步更新
- [ ] **P9.1** 📦 Git 提交：`[Docs] Docs: forbid duplicate test-local helpers and disable bUseUnity=false in test modules`

- [ ] **P9.2** `Plan_StatusPriorityRoadmap.md` / `Plan_OpportunityIndex.md` 同步收口
  - `Plan_StatusPriorityRoadmap.md`：在 "Recently Completed Milestones" 添加一条 "✅ Unity build 切回 ON 并清理同名 helper 冲突"
  - `Plan_OpportunityIndex.md`：将本 Plan 增补到 "三、缺陷修复与重构" 的 3.1 已有 Plan 列表（或建合适位置），并在第二段落 baseline 数字校对一遍
  - 完成后把本 Plan 顶部加 `归档状态: 已完成` `归档日期: <YYYY-MM-DD>` `完成判断` `结果摘要`，并 `git mv` 到 `Documents/Plans/Archives/Plan_UnityBuildConflictResolution.md`
  - `Documents/Plans/Archives/README.md` 加一条索引摘要
- [ ] **P9.2** 📦 Git 提交：`[Docs/Plans] Docs: archive unity build conflict resolution plan`

## 验收标准

每个 Phase 收尾时必须满足以下条件，才能进入下一个 Phase：

1. `powershell.exe -NoProfile -ExecutionPolicy Bypass -File Tools\RunBuild.ps1 -Label unity-conflict-<phase> -TimeoutMs 600000` 运行结果中错误数较前一 Phase 下降，且不引入新的错误码类型
2. 当前 Phase 的影响范围文件清单中，所有该 Phase 应该清的错误均消失（在 build log 中 grep 不到对应符号 / 文件）
3. commit message 必须遵循 `[<Scope>] <Type>: <description>` 规则，并把"前后错误数对比"写进 description（例如 `Reduce errors 490 → 432 by extracting math orientation helpers`）
4. 不引入对 `bUseUnity = false` 的回退、不修改 `MinSourceFilesForUnityBuildOverride`，不在 `RunBuild.ps1` 调用里加 `-DisableAdaptiveUnity / -ForceUnity` 之类绕过参数

终态（Phase 8 末）必须满足：

1. `Tools/RunBuild.ps1 -Label unity-conflict-final -TimeoutMs 900000` 在 unity ON 默认行为下零编译错误（warning 不计入本 Plan 范围，但不允许新增 warning 数量）
2. `Tools/RunTestSuite.ps1 -Suite Smoke -LabelPrefix unity-conflict-smoke -TimeoutMs 600000` 通过率不低于本次工作开始前的 baseline（Smoke 套件不允许出现新的失败）
3. `git diff` 中不包含任何 `bUseUnity = ` 的修改（已删除的应保持已删除状态）
4. `Plugins/Angelscript/Source/AngelscriptTest/AngelscriptTest.Build.cs` 与 `Plugins/Angelscript/Source/AngelscriptEditor/AngelscriptEditor.Build.cs` 无 `bUseUnity` 相关行
5. `AGENTS.md` / `AGENTS_ZH.md` / `TestConventions.md` 已写入"测试模块 unity build 约束"小节，并通过文档查找（grep）能定位到该约束
6. 本 Plan 完成归档（移入 `Archives/` 并补齐归档元信息）

## 风险与注意事项

### 风险

1. **共享头反复 include 引发的循环依赖**
   - 新建的 `Shared/AngelscriptGASTestConstants.h` / `AngelscriptAbilityTaskTestHelpers.h` 等需要 include GAS 模块头；如果 GAS 模块 include 链里反向 include 测试侧头会循环
   - **缓解**：所有共享头只包必要的 GAS public 头（`AbilitySystemComponent.h` / `AttributeSet.h`），不要 include 任何 `AngelscriptTest/Shared/*` 同级头；调用方文件继续按需添加自己的 include
2. **CQTest `TEST_METHOD` 内 `TestRunner->...` 的 nullptr 风险**
   - `TestRunner` 是 CQTest 静态指针；在 BEFORE_ALL/AFTER_ALL 等静态上下文里它可能尚未赋值
   - **缓解**：本 Plan 只替换 TEST_METHOD 内（即非 static 的运行时上下文）的裸调；对 BEFORE_ALL / AFTER_ALL 中的 helper（一般已经接受 `FAutomationTestBase&` 参数）保持不变
3. **Editor 模块对 `IDirectoryWatcher` 依赖在共享头中暴露**
   - `FMockDirectoryWatcher` 抽到共享头后，所有 include 该共享头的 `.cpp` 都会被迫拉进 `IDirectoryWatcher.h`；如 `Build.cs` 没把 `DirectoryWatcher` 模块加到 `PrivateDependencyModuleNames` 会编译失败
   - **缓解**：如果发现链接报错，只在共享头 forward-declare `IDirectoryWatcher`，把 mock 类的实现挪到 `Tests/Shared/AngelscriptDirectoryWatcherTestHelpers.cpp`（非头文件），用 `inline` 工厂函数返回基类指针
4. **adaptive non-unity 重新触发 duplicate basename 报错**
   - 如果未来又新增 `AngelScriptSDK/AngelscriptFooTests.cpp` 与 `Functional/.../AngelscriptFooTests.cpp` 这种同名对，UBT 会在某些修改集大小下重新触发 duplicate-basename block
   - **缓解**：本 Plan 不解决这条；但 `AGENTS.md` / `TestConventions.md` 同步增补"新建测试 .cpp 时 basename 必须在整个测试模块内全局唯一"约束

### 已知行为变化

1. **`AngelscriptTest.Build.cs` 与 `AngelscriptEditor.Build.cs` 不再含 `bUseUnity = false`**
   - 已经在前期工作里删除；本 Plan 期间任何 commit 都不允许把它加回来
   - 影响：增量编译时 `.gen.cpp` 与本模块 `.cpp` 都会进 unity 合并，编译速度提升但单次编译错误信息可读性下降（一个错误的级联可能跨多个文件）
2. **`AngelScriptSDK/Angelscript{Function,Operator,Type}Tests.cpp` 已被改名为 `AngelscriptASSDK{Function,Operator,Type}Tests.cpp`**
   - 影响：直接 `git log -- <旧路径>` 看不到历史；要用 `--follow` 跟踪
   - 影响：旧测试名前缀仍是 `Angelscript.TestModule.AngelScriptSDK.ASSDK.<Theme>`，与文件改名前一致；不需要在 RunTests.ps1 调用层做适配
3. **`Shared/AngelscriptCollisionTestHelpers.h` 与 `Shared/AngelscriptDebuggerTestHelpers.h` 已存在**
   - 影响：本 Plan 不允许重新创建同名头；只能在已有头上追加内容
4. **CQTest `TestRunner->` 替换会改大量行**
   - 影响：单个 SDK 文件的 commit diff 会很大（50 行起），需要 reviewer 注意 diff 集中在 `Test*` / `AddInfo` / `AddError` 这种纯文本批量替换；不要在同一 commit 里掺别的逻辑改动
5. **本 Plan 不修补"warnings"**
   - 影响：unity ON 后某些 warning 数量可能上升（如 `unused-but-set-variable` 跨 .cpp 暴露），不强制本 Plan 处理；但若有 `-Werror` 类配置，需要在 P8.3 终态构建里单独说明
