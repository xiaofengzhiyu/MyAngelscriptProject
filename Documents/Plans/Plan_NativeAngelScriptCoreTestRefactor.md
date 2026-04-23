# 原生 AngelScript 单元测试重构计划

## 背景与目标

### 背景

当前 `Plugins/Angelscript/Source/AngelscriptTest/` 下的大量测试虽然验证的是 AngelScript 语言内核能力（编译、执行、参数传递、返回值），但执行路径全部穿过 `FAngelscriptEngine` 封装层：

- `GetSharedTestEngine()` → `FAngelscriptEngine::CreateForTesting()` / `CreateTestingFullEngine()`
- `BuildModule()` → `FAngelscriptPreprocessor` + `FAngelscriptEngine::CompileModules()`
- `ExecuteIntFunction()` → `FScopedTestEngineGlobalScope` + `Engine.CreateContext()`
- `CompileModuleFromMemory()` → `FAngelscriptModuleDesc` + `Engine.CompileModules()`

这导致：

1. **分层不清** — 测试失败时无法判断问题来自 AS 原生层还是 `FAngelscriptEngine` 封装层。
2. **依赖过重** — 本可纯内存完成的语言验证被绑定到 UE 编译管线、预处理器和模块管理。
3. **基线缺失** — 没有一组独立的原生测试可以在 UE 集成层出问题时快速确认 AS 核心是否正常。

### 目标

1. 建立纯原生 AngelScript 测试层（`Native/`），只依赖 `asIScriptEngine` / `asIScriptModule` / `asIScriptContext` 原生 API，不依赖 `FAngelscriptEngine`。
2. 保留现有 Runtime / Scenario 测试，明确归类为 `FAngelscriptEngine` 封装测试和 UE 场景测试。
3. 建立最小可信基线：基础编译、执行、参数传递、返回值、注册、错误回调。
4. 在计划中显式固定“外部 include 能力”和“跨模块导出能力”的当前事实，避免把不存在的问题当阻塞项，或把真实的导出缺口漏掉。

## 范围与边界

- 本次只新增 `Native/` 测试层和迁移明确属于语言内核的测试用例。
- 不修改 `Shared/AngelscriptTestUtilities.h` 的现有 helper 逻辑。
- 不迁移类生成、热重载、Blueprint / Actor / UClass 相关测试。
- 不大规模搬迁现有 `Angelscript/`、`Bindings/`、`AngelScriptSDK/` 目录文件。
- `Native/` 默认定位为 **原生公共 API 测试层**，优先只使用 `asIScriptEngine` / `asIScriptModule` / `asIScriptContext` 与 `angelscript.h` 公共表面；不默认扩成 `source/as_*.h` 内部实现测试层。
- 若未来需要把 `Parser` / `Bytecode` / `GC` 一类 internal tests 抽成新的外部模块层，必须单独审计 `source/as_*.h` 的 include 方式与导出符号，不在本计划中默认隐含完成。

## 当前事实状态

```text
AngelscriptTest/
  AngelscriptTest.Build.cs          ← 模块定义，依赖 AngelscriptRuntime
  Angelscript/                       ← 语言层测试（实际依赖 FAngelscriptEngine）
    AngelscriptTestSupport.h         ← 仅 include AngelscriptTestUtilities.h
    AngelscriptExecutionTests.cpp    ← 9 个测试，全部通过 GetSharedTestEngine() + BuildModule()
    AngelscriptCoreExecutionTests.cpp← 10+ 个测试，同上 + CompileModuleFromMemory()
    AngelscriptControlFlowTests.cpp
    AngelscriptFunctionTests.cpp
    AngelscriptHandleTests.cpp
    AngelscriptInheritanceTests.cpp
    AngelscriptMiscTests.cpp
    AngelscriptNativeScriptHotReloadTests.cpp
    AngelscriptObjectModelTests.cpp
    AngelscriptOperatorTests.cpp
    AngelscriptTypeTests.cpp
  Shared/
    AngelscriptTestUtilities.h       ← 核心 helper：GetSharedTestEngine(), BuildModule(), ExecuteIntFunction()
    AngelscriptTestEngineHelper.h    ← 高级 helper：CompileModuleFromMemory(), FindGeneratedClass()
    AngelscriptTestEngineHelper.cpp
    AngelscriptTestEngineHelperTests.cpp
    AngelscriptNativeScriptTestObject.h
  Core/
    AngelscriptTestModule.h          ← IModuleInterface
    AngelscriptEngineCoreTests.cpp   ← Engine lifecycle / compile / execute 测试
  AngelScriptSDK/                         ← Parser / Bytecode / Compiler / GC 等
  Bindings/                          ← UE 类型绑定测试
  Compiler/                          ← 编译管线测试
  Preprocessor/                      ← 预处理器测试
  HotReload/                         ← 热重载测试
  Actor/ Blueprint/ Interface/ ...  ← UE 主题化集成测试
  Editor/                            ← 编辑器测试
  FileSystem/                        ← 文件系统测试
  Examples/                          ← 示例测试支持
```

关键依赖链：

- `AngelscriptTestUtilities.h` → `#include "AngelscriptEngine.h"` / `AngelscriptGameInstanceSubsystem.h` / `AngelscriptPreprocessor.h`
- `BuildModule()` 写磁盘文件 → 预处理 → `Engine.CompileModules()` → 返回 `asIScriptModule*`
- `ExecuteIntFunction()` → `FScopedTestEngineGlobalScope` → `Engine.CreateContext()`
- 所有 `Angelscript/*.cpp` 测试通过 `AngelscriptTestSupport.h` → `AngelscriptTestUtilities.h` 间接依赖 `FAngelscriptEngine`

### 外部 include / 导出状态快照

当前仓库对“外部模块访问 AngelScript third-party 头文件”的支持，并不是空白状态，而是已经存在一套可工作的边界：

- `Plugins/Angelscript/Source/AngelscriptRuntime/AngelscriptRuntime.Build.cs`
  - 已将 `ModuleDirectory` 本身加入 `PublicIncludePaths`
  - 已将 `Core/` 目录加入 `PublicIncludePaths`
  - 已将 `ThirdParty/angelscript/source` 与 `ThirdParty/angelscript` 加入 `PublicIncludePaths`
  - 已通过 `ANGELSCRIPT_EXPORT` / `ANGELSCRIPT_DLL_LIBRARY_IMPORT` 构建定义让 stock public C API（例如 `asCreateScriptEngine()`）可在外部模块中链接
- `Plugins/Angelscript/Source/AngelscriptTest/AngelscriptTest.Build.cs`
  - 只声明依赖 `AngelscriptRuntime`
  - 但当前测试已经可以直接 `#include "source/as_*.h"`，说明 include path 已通过依赖传播生效
- 当前跨模块 internal-header 使用不只存在于 `AngelScriptSDK/`，`Core/` 下测试也已直接 include `source/as_*.h`
- `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptInclude.h`
  - 已封装 `StartAngelscriptHeaders.h` + `angelscript.h` + `EndAngelscriptHeaders.h`
  - 适合作为新的 `Native/` 公共 API 测试层统一 include 入口

这意味着要把问题拆成两类：

1. **头文件可见性（include visibility）**
   - 对 `angelscript.h` 与 `source/as_*.h` 来说，当前已经基本成立
   - 本计划不应再把“ThirdParty 头不可见”作为默认前提
2. **符号导出（symbol export）**
   - 当前大量 internal class 已通过 `ANGELSCRIPTRUNTIME_API` 跨模块导出
   - 真正需要细化的是：哪些类已经足够，哪些类若未来要被新的外部模块/测试层直接实例化或调用，仍需按需补导出

### 已验证可跨模块使用的 internal class（代表性清单，非穷举）

以下类已经带有 `ANGELSCRIPTRUNTIME_API`，且当前 `AngelscriptTest` 现有测试已跨模块包含或使用了其中的一部分：

- `asCScriptEngine` — `source/as_scriptengine.h`
- `asCModule` — `source/as_module.h`
- `asCContext` — `source/as_context.h`
- `asCScriptFunction` — `source/as_scriptfunction.h`
- `asCTypeInfo` — `source/as_typeinfo.h`
- `asCObjectType` — `source/as_objecttype.h`
- `asCDataType` — `source/as_datatype.h`
- `asCBuilder` — `source/as_builder.h`
- `asCCompiler` — `source/as_compiler.h`
- `asCParser` — `source/as_parser.h`
- `asCScriptNode` — `source/as_scriptnode.h`
- `asCScriptCode` — `source/as_scriptcode.h`
- `asCByteCode` — `source/as_bytecode.h`
- `asCTokenizer` — `source/as_tokenizer.h`
- `asCGarbageCollector` — `source/as_gc.h`
- `asCMemoryMgr` — `source/as_memory.h`
- `asCReader` / `asCWriter` — `source/as_restore.h`
- `asCGlobalProperty` — `source/as_property.h`
- `asCString` — `source/as_string.h`
- `asCScriptObject` — `source/as_scriptobject.h`

### 当前仍需按需评估的潜在导出缺口（代表性清单，非穷举）

以下类型当前**不是本计划的 Native 公共 API 层必需项**，但如果未来要把更多 internal tests 拆到新的外部模块层，需要单独评估是否补导出：

- `asCVariableScope` — `source/as_variablescope.h`
  - 当前更适合继续视为 compiler 内部实现细节
- `asCExprValue` / `asCExprContext` — `source/as_compiler.h`
  - 若未来 external tests 要直接验证 compiler 内部表达式状态机，而不是通过 `asCCompiler` 更高层入口，需要单独评估导出策略
- `asCLockableSharedBool` — `source/as_scriptobject.h`
  - 若未来外部测试要直接验证 weak ref flag 内部实现，再考虑导出
- `asCObjectProperty` — `source/as_property.h`
  - 当前以 header-only 数据结构为主；若未来出现跨模块非 inline 使用，再评估导出
- `asCEnumType` / `asCFuncdefType` / `asCTypedefType`
  - 当前通过 `asCTypeInfo` 基类接口足以覆盖多数测试；若未来 external tests 必须直接调用其派生类专有成员，再补导出

## 分阶段执行计划

### Phase 0：固定 Native 层的 include / 导出边界

> 目标：先明确这次重构到底依赖“公开 API”还是“internal class”，避免 Phase 1 开始后继续混淆 include path 与导出符号问题。

- [ ] **P0.1** 在计划与实现约束中固定两条访问路径
  - `Native/` 公共 API 测试层：默认只使用 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptInclude.h` 或 `angelscript.h` 暴露的公开 API
  - `AngelScriptSDK/` 级内部测试：继续允许通过 `StartAngelscriptHeaders.h` / `EndAngelscriptHeaders.h` + `source/as_*.h` 访问已导出的 internal class
  - 明确禁止在 `Native/` 公共 API helper 中默认混入 `source/as_parser.h`、`source/as_builder.h` 这一类内部实现头
- [ ] **P0.1** 📦 Git 提交：`[Test/Native] Docs: fix native-vs-internal header boundary`

- [ ] **P0.2** 校正文档中的 include path 假设
  - 将当前“可能需要额外开放 ThirdParty include path”的表述改为事实化表述：
    - `AngelscriptRuntime.Build.cs` 已公开 `ModuleDirectory`、`Core`、ThirdParty source/root include path
    - `AngelscriptTest` 当前已跨模块直接 include `source/as_*.h`
  - 仅在 `AngelscriptTest.Build.cs` 中保留对本模块自身 `Native/` 目录的 include path 调整；不要为了 third-party 头重复添加已经存在的公共路径
- [ ] **P0.2** 📦 Git 提交：`[Test/Native] Docs: correct include-path assumptions for external as headers`

- [ ] **P0.3** 固定当前导出矩阵与按需扩展规则
  - 在计划中明确记录：现阶段 `Native/` 公共 API 层**不需要新增 internal class 导出符号**；它依赖的是 stock public C API 的模块间导入/导出配置
  - 同时列出按需扩展规则：若未来 external tests 直接使用 `asCVariableScope`、`asCExprValue`、`asCExprContext`、`asCLockableSharedBool`、`asCEnumType`、`asCFuncdefType`、`asCTypedefType` 或需要跨模块调用 `asCObjectProperty` 的非 inline 能力，则按现有 `[UE++]` + `ANGELSCRIPTRUNTIME_API` 模式逐项补导出
- [ ] **P0.3** 📦 Git 提交：`[Test/Native] Docs: capture current export matrix for as AngelScriptSDK`

### Phase 1：建立 Native 测试基础设施

> 目标：新增 `Native/` 目录，提供纯原生 AngelScript helper，代码中不出现 `FAngelscriptEngine`。

- [ ] **P1.1** 创建 `AngelScriptSDK/AngelscriptNativeTestSupport.h`
  - 提供以下 helper 函数（全部 `inline`，位于 `AngelscriptNativeTestSupport` 命名空间）：
    - `asIScriptEngine* CreateNativeEngine()` — 调用 `asCreateScriptEngine()` + 注册默认 message callback
    - `void DestroyNativeEngine(asIScriptEngine*)` — `ShutDownAndRelease()`
    - `asIScriptModule* BuildNativeModule(asIScriptEngine*, const char* ModuleName, const char* Source)` — `GetModule(GM_ALWAYS_CREATE)` + `AddScriptSection()` + `Build()`
    - `asIScriptFunction* GetNativeFunctionByDecl(asIScriptModule*, const char* Declaration)` — `GetFunctionByDecl()`
    - `int PrepareAndExecute(asIScriptContext*, asIScriptFunction*)` — `Prepare()` + `Execute()`
    - `FString CollectMessages(...)` — message callback 收集器
  - 约束：优先 `#include "AngelscriptInclude.h"` 作为公开 API 统一入口；它只包裹 `angelscript.h` 与 warning wrapper，不引入 `FAngelscriptEngine`
  - 不得 `#include` 任何项目封装层运行时头，例如 `AngelscriptEngine.h`、`AngelscriptPreprocessor.h`、`AngelscriptGameInstanceSubsystem.h`
  - 不得在此 helper 中直接 `#include "source/as_*.h"`；若后续确有 internal test helper 需求，必须单独新建并保持与 `Native` 公共 API helper 分离
- [ ] **P1.1** 📦 Git 提交：`[Test/Native] Feat: add AngelscriptNativeTestSupport header`

- [ ] **P1.2** 创建 `AngelScriptSDK/AngelscriptNativeTestSupport.cpp`（如有非 inline 实现）
  - 若 message callback 需要持有状态（收集诊断），则在此实现
  - 若全部 inline 可省略此文件
- [ ] **P1.2** 📦 Git 提交：`[Test/Native] Feat: add NativeTestSupport implementation`

- [ ] **P1.3** 更新 `AngelscriptTest.Build.cs`
  - 在 `PrivateIncludePaths` 中新增 `Path.Combine(ModuleDirectory, "Native")`
  - 验证依赖 `AngelscriptRuntime` 后，`Native/` 文件可直接 include `AngelscriptInclude.h` / `angelscript.h`
  - **不要**为了 third-party 头再次在 `AngelscriptTest.Build.cs` 中手工重复添加 `ThirdParty/angelscript/source`，除非实际编译证明当前公共路径没有向该模块传播
- [ ] **P1.3** 📦 Git 提交：`[Test/Native] Chore: update Build.cs for Native local includes only`

- [ ] **P1.4** 创建 `AngelScriptSDK/AngelscriptNativeSmokeTest.cpp`
  - 1 个冒烟测试：创建 engine → 编译 `int Test() { return 1; }` → 执行 → 验证返回值 → 销毁 engine
  - 测试路径：`Angelscript.TestModule.AngelScriptSDK.Smoke`
  - 确认整条 Native helper 链路跑通
- [ ] **P1.4** 📦 Git 提交：`[Test/Native] Feat: add native smoke test`

- [ ] **P1.5** 在编辑器中运行 `Angelscript.TestModule.AngelScriptSDK.Smoke`，确认通过
- [ ] **P1.5** 📦 Git 提交：`[Test/Native] Test: verify native smoke test passes`

### Phase 2：迁移执行类基础测试

> 目标：将 `Angelscript/AngelscriptExecutionTests.cpp` 中的核心执行测试以"纯原生"方式重写到 `Native/`。

- [ ] **P2.1** 创建 `AngelScriptSDK/AngelscriptNativeExecutionTests.cpp`，覆盖以下测试点（每个 IMPLEMENT_SIMPLE_AUTOMATION_TEST 一一对应）：
  - `Native.Execute.VoidFunction` — void 函数编译与执行
  - `Native.Execute.ReturnValue` — int 函数返回值读取
  - `Native.Execute.OneArg` — 单参数 `SetArgDWord()` + 执行
  - `Native.Execute.TwoArgs` — 双参数传递
  - `Native.Execute.ThreeArgs` — 三参数传递
  - 所有测试只用 `AngelscriptNativeTestSupport` 中的 helper，不引用 `FAngelscriptEngine`
- [ ] **P2.1** 📦 Git 提交：`[Test/Native] Feat: add native execution tests (void, return, 1-3 args)`

- [ ] **P2.2** 创建 `AngelScriptSDK/AngelscriptNativeExecutionAdvancedTests.cpp`，覆盖：
  - `Native.Execute.FloatReturn` — float 返回值
  - `Native.Execute.StringReturn` — string 返回值（如适用）
  - `Native.Execute.NegativeValue` — 负数参数传递
  - `Native.Execute.MultipleReturnPaths` — 多分支返回路径
- [ ] **P2.2** 📦 Git 提交：`[Test/Native] Feat: add advanced native execution tests`

- [ ] **P2.3** 运行全部 `Angelscript.TestModule.AngelScriptSDK.Execute.*` 测试，确认通过
- [ ] **P2.3** 📦 Git 提交：`[Test/Native] Test: verify all native execution tests pass`

### Phase 3：补齐 Native 编译与诊断测试

> 目标：覆盖原生编译成功 / 失败路径和 message callback 诊断信息收集。

- [ ] **P3.1** 创建 `AngelScriptSDK/AngelscriptNativeCompileTests.cpp`，覆盖：
  - `Native.Compile.SimpleFunction` — 单函数编译成功
  - `Native.Compile.MultipleFunctions` — 多函数模块编译
  - `Native.Compile.GlobalVariables` — 全局变量声明编译
  - `Native.Compile.SyntaxError` — 语法错误编译失败，`Build()` 返回负值
  - `Native.Compile.ErrorMessage` — message callback 收到错误信息，验证行号和消息文本非空
- [ ] **P3.1** 📦 Git 提交：`[Test/Native] Feat: add native compile and diagnostics tests`

- [ ] **P3.2** 创建 `AngelScriptSDK/AngelscriptNativeRegistrationTests.cpp`，覆盖：
  - `Native.Register.GlobalFunction` — 注册 C++ 全局函数，脚本中调用
  - `Native.Register.GlobalProperty` — 注册全局属性，脚本中读取
  - `Native.Register.SimpleValueType` — 注册 POD value type，脚本中构造使用
- [ ] **P3.2** 📦 Git 提交：`[Test/Native] Feat: add native registration tests`

- [ ] **P3.3** 运行全部 `Angelscript.TestModule.AngelScriptSDK.*` 测试，确认通过
- [ ] **P3.3** 📦 Git 提交：`[Test/Native] Test: verify all Phase 3 native tests pass`

### Phase 4：标记现有测试分层归属

> 目标：不搬文件，但在现有测试文件头部添加注释说明其归属层级，为后续目录整理做准备。

- [ ] **P4.1** 在以下文件头部（`#if WITH_DEV_AUTOMATION_TESTS` 之前）添加层级注释 `// Test Layer: Runtime Integration`：
  - `Angelscript/AngelscriptExecutionTests.cpp`
  - `Angelscript/AngelscriptCoreExecutionTests.cpp`
  - `Angelscript/AngelscriptControlFlowTests.cpp`
  - `Angelscript/AngelscriptFunctionTests.cpp`
  - `Angelscript/AngelscriptHandleTests.cpp`
  - `Angelscript/AngelscriptMiscTests.cpp`
  - `Angelscript/AngelscriptObjectModelTests.cpp`
  - `Angelscript/AngelscriptOperatorTests.cpp`
  - `Angelscript/AngelscriptTypeTests.cpp`
- [ ] **P4.1** 📦 Git 提交：`[Test] Chore: annotate Angelscript/ tests as Runtime Integration layer`

- [ ] **P4.2** 在以下文件头部添加层级注释 `// Test Layer: Runtime Integration`：
  - `Angelscript/AngelscriptInheritanceTests.cpp`
  - `Angelscript/AngelscriptNativeScriptHotReloadTests.cpp`
  - `Core/AngelscriptEngineCoreTests.cpp`
  - `Shared/AngelscriptTestEngineHelperTests.cpp`
- [ ] **P4.2** 📦 Git 提交：`[Test] Chore: annotate Core/ and inheritance tests as Runtime Integration`

- [ ] **P4.3** 确认主题化集成测试目录（如 `Actor/`、`Blueprint/`、`Interface/`、`Component/`、`GC/`、`Subsystem/`、`HotReload/`）中的文件已带 `// Test Layer: UE Scenario` 或无需标注
- [ ] **P4.3** 📦 Git 提交：`[Test] Chore: annotate themed integration tests as UE Scenario layer`

### Phase 5：文档与测试指南同步

> 目标：更新相关文档，明确三层测试的目标、边界和新增约定。

- [ ] **P5.1** 在 `Documents/Guides/` 下新增或更新 `Test.md`，写明：
  - 三层测试定义（Native Core / Runtime Integration / UE Scenario）
  - 每层测试的职责、允许的依赖、文件位置约定
  - `Native/` 下测试的命名规范：`Angelscript.TestModule.AngelScriptSDK.<Category>.<TestName>`
  - 新增测试时的选层决策流程
- [ ] **P5.1** 📦 Git 提交：`[Docs] Feat: add test layer guide to Documents/Guides/Test.md`

- [ ] **P5.2** 在 `Plugins/Angelscript/AGENTS.md` 中补充 Native 测试层说明（1-2 句简述 + 指向 `Documents/Guides/Test.md`）
- [ ] **P5.2** 📦 Git 提交：`[Docs] Chore: mention Native test layer in plugin AGENTS.md`

## 验收标准

1. `AngelScriptSDK/AngelscriptNativeTestSupport.h` 中不出现任何 `FAngelscriptEngine` / `FAngelscriptPreprocessor` / `UObject` 相关引用。
2. `Native/*.cpp` 中所有测试只通过 `asIScriptEngine` / `asIScriptModule` / `asIScriptContext` 原生 API 运行，不直接依赖 `source/as_*.h` 内部实现头。
3. `Angelscript.TestModule.AngelScriptSDK.Smoke` 冒烟测试通过。
4. `Angelscript.TestModule.AngelScriptSDK.Execute.*`（≥5 个）全部通过。
5. `Angelscript.TestModule.AngelScriptSDK.Compile.*`（≥5 个）全部通过。
6. `Angelscript.TestModule.AngelScriptSDK.Register.*`（≥3 个）全部通过。
7. 现有 Runtime / Scenario 测试回归通过（不因本次重构引入失败）。
8. `Documents/Guides/Test.md` 明确定义三层测试边界，并说明 `Native` 公共 API 层与 `AngelScriptSDK` 内部头测试层的 include 规则。
9. 计划或文档中明确记录：当前 third-party `source/as_*.h` 对外 include 能力已存在；Native 层本次不需要新增 internal class 导出，但需要 stock public C API 在模块间可链接。

## 风险与注意事项

### 风险 1：把 include 可见性与导出符号问题混为一谈

当前仓库对 `angelscript.h`、`source/as_*.h`、`StartAngelscriptHeaders.h` / `EndAngelscriptHeaders.h` 的跨模块 include 能力已经基本成立；真正更容易出错的是未来把某些 internal class 当作“理所当然可跨模块直接调用”时，忘记核对是否已经导出。

**缓解**：Phase 0 先固定两条访问路径；`Native` 公共 API 层只依赖 `AngelscriptInclude.h` / `angelscript.h`，internal tests 才允许使用 `source/as_*.h` + export matrix。

### 风险 2：误在 `AngelscriptTest.Build.cs` 中重复配置 ThirdParty 公共路径

当前 `AngelscriptRuntime.Build.cs` 已经通过依赖传播暴露 `ModuleDirectory`、`Core` 与 ThirdParty include path。若不了解现状，后续很容易在 `AngelscriptTest.Build.cs` 中重复追加相同路径，制造不必要的配置噪音。

**缓解**：P0.2 / P1.3 先验证现有公共路径是否足够；除非编译证据表明路径没有传播，否则只新增 `Native/` 本地目录路径。

### 风险 3：Native helper 能力边界蠕变

如果后续为了"方便"向 `AngelscriptNativeTestSupport.h` 中塞入 `FAngelscriptEngine` 相关逻辑，分层就会失效。

**缓解**：在 `AngelscriptNativeTestSupport.h` 文件头部添加约束注释，明确禁止引入项目封装层依赖。CI 中可补 include 检查。

### 风险 4：把 internal test 需求偷渡进 Native 公共 API 层

当前 `AngelScriptSDK/` 与 `Core/` 测试已经能够跨模块使用 `source/as_builder.h`、`as_parser.h`、`as_module.h` 等头。如果不提前划清边界，后续可能为了省事把 parser / bytecode / GC 一类 internal 断言直接塞进 `Native/`，导致“原生公共 API 测试层”名不副实。

**缓解**：P0.1 明确把 `Native` 与 `AngelScriptSDK` 分成两条访问路径；需要内部实现头时新建独立 helper 或继续留在 `AngelScriptSDK/`，不污染 `Native` helper。

### 风险 5：测试迁移窗口期覆盖面下降

先新增 Native 测试，不删除原有 Runtime 层测试。两层测试暂时共存，直到 Native 层稳定后再评估是否精简 Runtime 层中的重复用例。

**缓解**：采用"先新增 → 并行运行 → 确认覆盖 → 再评估删除"的顺序，不在本计划中删除任何现有测试。

### 风险 6：未来 internal class 扩展时遗漏导出符号

当前 Native 公共 API 层本身不需要新增 internal class 导出符号，但如果未来 external module 级别的 internal tests 要直接调用 `asCVariableScope`、`asCExprValue`、`asCExprContext`、`asCLockableSharedBool`、`asCEnumType`、`asCFuncdefType`、`asCTypedefType` 等未覆盖在当前导出矩阵里的类型，编译或链接边界可能再次暴露。

**缓解**：按需沿用现有 `[UE++]` + `ANGELSCRIPTRUNTIME_API` 模式逐项补导出；不要预防性大面积公开所有 third-party 内部类。

### 风险 7：asIScriptEngine 无默认 message callback 导致静默失败

原生 `asCreateScriptEngine()` 创建的引擎没有 message callback，编译错误不会输出。

**缓解**：P1.1 中的 `CreateNativeEngine()` 必须注册 message callback，将消息转发到 `UE_LOG` 或测试框架输出。
