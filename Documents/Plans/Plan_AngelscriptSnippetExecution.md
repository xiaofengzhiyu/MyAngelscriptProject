# Angelscript 动态片段执行支持计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development（推荐）或 superpowers:executing-plans 按任务执行本计划。步骤使用 checkbox（`- [ ]`）语法跟踪。

**Goal:** 让 `Plugins/Angelscript` 在不破坏现有编译/热重载/测试边界的前提下，支持“动态执行一段 Angelscript 代码片段”，并明确区分快速即时执行的**公开 MVP 能力**与完整动态模块编译的**内部升级路径**。

**Architecture:** 推荐采用 **Hybrid 双通道，但公开面非对称**：公开 MVP 仅围绕 `FastSnippet` 路径，基于 `asIScriptModule::CompileFunction()` + 包装函数完成低开销即时执行；`TransientModule` 路径基于 `FAngelscriptModuleDesc` + `FAngelscriptEngine::CompileModules()`，作为内部升级路径保留给多声明、imports、全局状态或后续更重的扩展场景。两条路径统一收口到一个新的 runtime 执行器层，而不是把测试 helper 或调试器逻辑直接暴露成正式 API。

**Tech Stack:** `AngelscriptRuntime/Core`、`FAngelscriptEngine`、`FAngelscriptModuleDesc`、`FAngelscriptPreprocessor`、`FAngelscriptContext` / `FAngelscriptPooledContextBase`、AngelScript `asIScriptModule::CompileFunction` / `AddScriptSection` / `Build` / `asIScriptContext::Prepare/Execute`、`AngelscriptTest` 自动化测试。

---

## 背景与目标

当前仓库其实已经同时存在两条“动态代码”证据链，但它们还没有被收敛成正式功能：

- `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEngineCoreTests.cpp` 与 `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEngineParityTests.cpp` 已证明当前引擎可以直接通过 `asIScriptModule::CompileFunction()` 编译并执行一段函数级片段，而且这些片段能访问已经绑定进生产引擎的 UE 类型与全局 API。
- `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestUtilities.h`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelper.cpp` 已证明当前插件也能通过 `FAngelscriptModuleDesc` / `FAngelscriptPreprocessor` / `FAngelscriptEngine::CompileModules()` 从内存侧构造完整模块，再按正常模块路径执行与清理。

但这两条路径目前都不是正式对外能力：前者缺少统一的诊断、上下文、模块命名与清理约束；后者是测试 helper，带有 `Saved/Automation` 文件写入、测试全局作用域、模块污染与自动化上下文假设，不能原样抬升到 Runtime API。

本计划的目标不是“再造一套脚本系统”，而是把现有底座整理成一个清晰、可验证、可扩展的动态片段执行能力层，回答下面四个问题：

1. 哪些片段属于公开 MVP 的 `CompileFunction()` 快速路径，哪些只应落在内部 `CompileModules()` 升级路径；
2. 动态片段如何拿到 compile/execute diagnostics，而不是只得到一个失败返回码；
3. 动态片段如何在执行后清理临时模块、上下文与状态，避免污染热重载、测试发现与普通脚本模块；
4. 这项能力应当以什么形式暴露：新的 Runtime C++ API、调试器内部 helper，还是更上层的 Editor/Console 包装。

## 范围与边界

- **纳入范围**
  - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/` 中与上下文、模块、诊断、编译入口相关的基础能力改造。
  - 新增一个专门的 runtime 执行器层，用于统一 `FastSnippet` / `TransientModule` 两条路径。
  - `AngelscriptTest` 中针对快速片段执行、完整动态模块执行、诊断捕获、模块清理与上下文隔离的自动化测试。
  - 必要的文档与使用边界说明。
- **不纳入范围**
  - 直接把 `RequestEvaluate` 调试消息扩展成完整 REPL 或脚本控制台 UI。
  - 第一阶段就支持完整的 `UCLASS` / `USTRUCT` / `UENUM` 动态定义与生命周期管理。
  - 第一阶段就承诺通用返回值反射封送、Blueprint 暴露、远程执行协议或安全沙箱。
  - 把测试 helper 直接挪到 Runtime 并保持其磁盘写入策略不变。
- **关键约束**
  - 新能力优先服务插件本体，而不是宿主工程；逻辑应落在 `Plugins/Angelscript`。
  - 动态片段执行不能破坏现有 `CompileModules()`、热重载、自动化测试发现与多引擎 clone/full 语义。
  - 任何运行期能力都必须明确区分“正式 runtime API”与“调试器/编辑器上的上层包装”，不能继续把核心行为埋在 DebugServer 或 Test helper 里。

## 当前事实状态快照

### 已有底座

- `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`
  - 已暴露 `CompileModules()`、`DiscardModule()`、`GetModuleByModuleName()`、`CreateContext()`、`FAngelscriptPooledContextBase`、`FAngelscriptContext`、`FScopedTestEngineGlobalScope` 等关键原语。
- `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`
  - 已有完整四阶段编译流水线：`CompileModule_Types_Stage1()`、`CompileModule_Functions_Stage2()`、`CompileModule_Code_Stage3()`、`CompileModule_Globals_Stage4()`。
  - 已有诊断缓存 `Diagnostics` 与统一消息回调 `LogAngelscriptError()`，但当前捕获逻辑主要服务常规模块编译，而不是 `CompileFunction()` 这类瞬时片段。
  - 已有上下文池、异常回调、world context 与 DebugServer 集成。
- `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`
  - `RequestEvaluate` 当前只是变量/作用域求值，不会动态编译任意代码片段，因此它不是现成功能，只能算潜在上层调用方。

### 已有动态执行证据

- `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEngineCoreTests.cpp`
  - `CompileSnippet` / `ExecuteSnippet` 证明 `CompileFunction()` + `CreateContext()` + `Prepare/Execute()` 已可工作。
- `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEngineParityTests.cpp`
  - 证明 `CompileFunction()` 形成的片段可访问生产引擎中已绑定的 UE 类型、命名空间与系统函数，而不只是最小原生 AngelScript 引擎。
- `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestUtilities.h`
  - `BuildModule()` 证明可以将动态源码穿过 preprocessor + `CompileModules()`，并在执行后按模块名查找函数、执行与回收。
- `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelper.cpp`
  - `CompileModuleFromMemory()` / `CompileAnnotatedModuleFromMemory()` 证明完整模块编译与注解路径已经存在，但实现中混有测试专用作用域与文件落盘策略。

### 当前 blocker 与边界

- 现有 `CompileFunction()` 使用场景没有统一的 runtime 结果对象，compile error / execute error / exception string / section name / module name 目前都由调用者各自处理。
- 现有测试 helper 为了走 preprocessor，会把脚本写入 `Saved/Automation`；这对自动化测试合理，但作为正式 runtime 功能会引入不必要的磁盘副作用。
- 现有 `CompileModules()` 会进入 `ActiveModules`、类生成、hot reload、test discovery 等完整语义；如果把“每次动态执行一行代码”都强行走这条链，成本太高，也容易污染引擎状态。

## 候选路线对比与推荐

### 路线 A：只做 `CompileFunction()` 快速路径

- **优点**
  - 复用 AngelScript 官方推荐的 ad-hoc code 执行模型，工程量最小。
  - 天然适合“执行一小段语句/表达式/单函数”的场景，和当前测试证据完全一致。
  - 不必碰类生成、完整模块 swap-in、full reload 等重型机制。
- **缺点**
  - 无法覆盖多顶层声明、`import`、模块级全局状态、完整 preprocessor 语义。
  - 若将来需要更接近“动态脚本文件”的能力，会很快撞上边界。

### 路线 B：只做 `CompileModules()` 完整路径

- **优点**
  - 与当前插件主编译链完全一致，语义最统一。
  - 未来接 `imports`、全局变量、模块依赖、类生成更顺。
- **缺点**
  - 对单次即时执行来说过重。
  - 会天然卷入模块 swap-in、类生成、reload requirement、test discovery、discard/cleanup 等一整套状态管理。
  - 把所有片段都当“临时模块”处理，会造成过度复杂与更高副作用。

### 路线 C：Hybrid 双通道（推荐，但公开面非对称）

- **核心判断**
  - 把“动态执行片段”拆成两类问题：
    1. **即时执行**：一小段函数体/单函数，需要低延迟、低副作用；
    2. **动态模块**：需要 imports、多函数、全局状态，甚至未来的注解/类生成扩展。
  - 这两类问题在当前仓库里已经分别有现成证据链，因此最佳方案不是强行二选一，而是把它们在正式 API 中收口到统一执行器，并给出明确边界。
- **推荐结论**
  - MVP 采用 Hybrid，但调用面要明确分层：
    - **公开 runtime façade**：只承诺 `FastSnippet`，基于 `CompileFunction()`，面向“非结构性、非注解、一次性执行”的片段。
    - **内部执行器升级路径**：保留 `TransientModule`，基于 `CompileModules()`，第一阶段主要为多声明、全局状态、imports 或后续工具链预留，不直接包装成默认 public API。
  - DebugServer / Editor Console / 自动化工具后续都调用同一 runtime 执行器，不再各自复制执行逻辑；但它们要通过 façade 消费内部能力，而不是各自拥有编译语义。

## 推荐 API 草案

以下草案不是要求一次性把所有细节做完，而是为了在 Phase 1 先把边界固定下来。这里要明确区分 **公开请求面** 与 **内部执行策略**：

```cpp
enum class EAngelscriptSnippetPublicMode : uint8
{
	Auto,
	FastFunction,
};

enum class EAngelscriptSnippetInternalStrategy : uint8
{
	FastFunction,
	TransientModule,
};

enum class EAngelscriptSnippetPhase : uint8
{
	CompileOnly,
	CompileAndExecute,
};

struct FAngelscriptSnippetOptions
{
	EAngelscriptSnippetPublicMode Mode = EAngelscriptSnippetPublicMode::Auto;
	EAngelscriptSnippetPhase Phase = EAngelscriptSnippetPhase::CompileAndExecute;
	FString ModuleNameOverride;
	FString SectionNameOverride;
	FString EntryDeclaration;
	FString WrapperDeclaration = TEXT("void __ASSnippetEntry()");
	UObject* WorldContext = nullptr;
	bool bDiscardAfterExecution = true;
	bool bAllowAnnotatedModulePath = false;
};

struct FAngelscriptSnippetResult
{
	bool bCompileSucceeded = false;
	bool bExecuteSucceeded = false;
	int32 CompileCode = 0;
	int32 ExecuteCode = 0;
	FString ModuleName;
	FString SectionName;
	FString EntryDeclaration;
	FString Diagnostics;
	FString ExceptionString;
};
```

### Auto 路径选择规则（建议第一版固定）

- 若显式指定 `Mode`，当前 MVP 只允许公开调用 `Auto` / `FastFunction`。
- 若 `Mode == Auto`：
  - 代码明显是单函数或函数体片段时，走公开 `FastFunction`；
  - 代码包含多顶层声明、需要 `EntryDeclaration` 或需要模块级可见性时，**不在公开 façade 中自动升级到 `TransientModule`**，而是返回稳定 diagnostics，提示这是内部升级路径或后续能力；
  - 若检测到 `UCLASS(` / `USTRUCT(` / `UENUM(`，第一阶段直接返回“当前不支持 annotated dynamic snippet”的明确 diagnostics，而不是静默尝试。

### 内部升级路径规则（建议第一版固定）

- `TransientModule` 属于 runtime 执行器的内部策略，不属于 MVP 默认公开接口。
- 只有测试、未来调试器适配层、或后续明确扩 scope 的工具入口，才可以显式请求内部模块级执行能力。
- 这样做的目的是避免把“执行一小段脚本”错误演化成“对外承诺完整动态模块系统”。

### 为什么不直接暴露测试 helper

- `AngelscriptTestSupport::BuildModule()` 与 `CompileAnnotatedModuleFromMemory()` 位于 `AngelscriptTest`，带有测试层约束，不是运行时 API。
- 它们部分逻辑会写入 `Saved/Automation`，这是测试环境能接受的副作用，但不应成为正式 runtime feature 的默认实现。
- 正式实现应该抽取**同样的思想**——构造 `FAngelscriptModuleDesc`、配置 diagnostics、调用 `CompileModules()`、执行、清理——而不是把 helper 原样搬进 Runtime。

## 首批文件归属建议

为避免后续实现时再次纠结“功能到底该挂在哪”，本计划先固定建议归属：

- **新增** `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptSnippetExecutor.h`
- **新增** `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptSnippetExecutor.cpp`
- **修改** `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`
- **修改** `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`
- **新增或修改测试**
  - `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEngineCoreTests.cpp`
  - `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelperTests.cpp`
  - 必要时新增 `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptSnippetExecutionTests.cpp`

设计原则是：`FAngelscriptEngine` 只新增最小的基础 helper，真正的路径分流、包装函数生成、临时模块命名、结果对象拼装都放进单独执行器层；而真正对外暴露的 façade 只承诺快速、非结构性片段执行，避免继续把 `FAngelscriptEngine` 变成更大的“万能桶”或把 `CompileModules()` 误包装成轻量 eval API。

## 分阶段执行计划

### Phase 0：冻结能力边界、推荐架构与 MVP 范围

> 目标：先把“支持动态片段执行”到底指什么、第一版刻意不做什么写死，避免实现时在 `CompileFunction` 与 `CompileModules` 之间反复漂移。

- [ ] **P0.1** 固定动态片段执行的能力分层与非目标
  - 当前仓库已经证明 `CompileFunction()` 与 `CompileModules()` 都能承载某种“动态代码”能力，但二者解决的问题并不相同；本项必须先把“即时片段执行”与“完整动态模块”定义成两个正式层级，而不是一个模糊的大功能。
  - 第一阶段成功标准固定为：非注解 Angelscript 片段可以 compile/execute，能返回 compile/execute 结果与 diagnostics，执行后可按配置清理临时模块，不污染普通脚本模块与测试发现。
  - 第一阶段明确排除：完整 `UCLASS/USTRUCT/UENUM` 动态定义、Blueprint 暴露、任意返回值自动封送、远程控制台协议与安全沙箱。若未来需要这些能力，应在计划中作为 Phase 4 之后的增强，而不是偷渡进 MVP。
- [ ] **P0.1** 📦 Git 提交：`[Runtime/AS] Plan: freeze snippet execution capability boundary`

- [ ] **P0.2** 固定推荐架构为 Hybrid 双通道
  - 结合 `AngelscriptEngineCoreTests.cpp` 的 `CompileFunction()` 证据、`AngelscriptTestUtilities.h` / `AngelscriptTestEngineHelper.cpp` 的完整模块编译证据，以及官方 AngelScript 文档对 ExecuteString / CompileFunction / Build module 的分工，本项必须把推荐架构钉成“FastFunction + TransientModule”。
  - 本项要明确写清楚为什么不能只选其中一条：只做快速路径会失去模块级能力，只做完整路径会让一次性 eval 背上过重状态管理成本；但同时也要钉死一个实现纪律：**公开 MVP 不自动暴露 `TransientModule`**。
  - 同时固定一条负面结论：`DebugServer::RequestEvaluate` 不是现成功能，它只能作为未来调用新的 runtime 执行器的一个上层入口。
- [ ] **P0.2** 📦 Git 提交：`[Runtime/AS] Plan: freeze hybrid architecture for snippet execution`

### Phase 1：建立正式 runtime API、结果对象与诊断收口层

> 目标：先把调用面和结果面做稳定，不要让执行逻辑一开始就散落在测试、调试器和零碎 helper 中。

- [ ] **P1.1** 新建 `AngelscriptSnippetExecutor` 并固定最小公共接口
  - 新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptSnippetExecutor.h/.cpp`，定义 `FAngelscriptSnippetOptions`、`FAngelscriptSnippetResult`、`EAngelscriptSnippetPublicMode`、必要的内部策略枚举，以及一个统一入口，例如 `ExecuteSnippet(FAngelscriptEngine&, const FString&, const FAngelscriptSnippetOptions&)`。
  - 这一层负责路径分流、临时命名、包装函数拼接、结果对象整理，不直接承担脚本引擎初始化或模块持有；也就是说，它是 `FAngelscriptEngine` 之上的能力层，而不是另一个全局 singleton。
  - 与此同时在 `FAngelscriptEngine` 上只暴露最小 helper；真正对外的 façade 只承诺非结构性 snippet 执行，避免把路径判断、字符串包装与返回值拼装塞回 `AngelscriptEngine.cpp`，也避免在第一阶段把内部 `TransientModule` 策略直接变成 public API。
- [ ] **P1.1** 📦 Git 提交：`[Runtime/AS] Feat: add snippet executor API surface`

- [ ] **P1.2** 为瞬时片段执行补齐 diagnostics 捕获 seam
  - 当前 `LogAngelscriptError()` 主要通过 `Diagnostics.Find(Section)` 捕获常规模块编译消息，但 `CompileFunction()` 路径没有现成的“注册瞬时 section 并在执行后取回诊断”的正式接口。
  - 本项应在 `FAngelscriptEngine` 中新增最小 helper，例如“为某个 synthetic section 预注册 diagnostics sink”“执行后提取并清理 diagnostics”，让 `FastFunction` 路径也能像普通模块编译那样给出稳定错误信息。
  - 目标是让调用方拿到结构化结果，而不是只看到 `asSUCCESS` / `asEXECUTION_EXCEPTION` 这种裸返回码。
- [ ] **P1.2** 📦 Git 提交：`[Runtime/AS] Feat: add transient diagnostics capture for snippet execution`

- [ ] **P1.3** 先补红灯测试，锁定正式行为边界
  - 在 `Plugins/Angelscript/Source/AngelscriptTest/Core/` 或独立测试文件中，先写失败测试覆盖：快速片段成功执行、编译失败可返回 diagnostics、执行异常可返回 exception string、执行后模块不留脏状态。
  - 在 `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelperTests.cpp` 或相邻测试中补“不同引擎实例间不串模块、不串上下文”的红灯，确保后续实现不会偷用全局状态。
  - 红灯测试写在 Phase 1，是为了让后续两条执行路径都能对准同一组行为契约，而不是各自临时发散。
- [ ] **P1.3** 📦 Git 提交：`[Test/AS] Test: add failing coverage for runtime snippet execution contract`

### Phase 2：接通 `FastFunction` 即时执行路径

> 目标：先把最小可用、低副作用、最符合“动态执行代码片段”直觉的能力做通。

- [ ] **P2.1** 实现包装函数生成与 `CompileFunction()` 编译入口
  - 在 `AngelscriptSnippetExecutor.cpp` 中实现 `FastFunction` 路径：若输入本身是单个函数定义，则直接编译；若输入是函数体/语句片段，则按 `WrapperDeclaration` 包装成一个临时函数，再调用 `asIScriptModule::CompileFunction()`。
  - 生成的 section / module 名必须使用统一的内部前缀，例如 `__AS_Snippet.Fast.*`，避免与普通脚本模块冲突，同时便于 diagnostics 和后续 cleanup 精确定位。
  - 包装策略要固定：第一版默认支持 `void` 和显式声明的 primitive return，不做“自动推导任意表达式返回类型”的过度设计。
- [ ] **P2.1** 📦 Git 提交：`[Runtime/AS] Feat: implement fast compile-function snippet path`

- [ ] **P2.2** 接通上下文、world context 与异常回传
  - 快速路径执行时应优先使用 `FAngelscriptContext` / `FAngelscriptPooledContextBase` 的既有能力，而不是直接 new / release 原始 context；这样可以自动复用当前引擎的上下文池、loop detection、world context 与异常回调。
  - 若 `Options.WorldContext` 有值，执行时要显式接入 world context scope；若无值，也要保持与当前引擎行为一致，不让片段执行破坏 `CurrentWorldContext` 的恢复纪律。
  - 对执行失败场景，不只是返回 `ExecuteCode`，还要把 `GetExceptionString()` 或等价异常描述写回 `FAngelscriptSnippetResult`。
- [ ] **P2.2** 📦 Git 提交：`[Runtime/AS] Feat: integrate pooled context and exception capture for fast snippets`

- [ ] **P2.3** 补齐快速路径的 cleanup 与状态不污染验证
  - 快速路径的核心价值是“执行一次就走”，因此本项必须明确 `bDiscardAfterExecution` 的默认语义，并验证即使连续执行多个片段，也不会在引擎里积累不可控的临时函数/模块。
  - 这一项至少要覆盖两类验证：同一引擎内连续执行多个快速片段不会串 declaration/module state；不同 clone/full 引擎执行同名快速片段不会互相覆盖。
  - 若需要额外 helper 才能稳定 discard 模块或释放函数，应把 helper 收口在 runtime executor/engine，而不是散落在测试里复制。
- [ ] **P2.3** 📦 Git 提交：`[Runtime/AS] Test: verify fast snippet cleanup and engine isolation`

### Phase 3：接通 `TransientModule` 内部升级路径（第一阶段先做非注解模块）

> 目标：在不把整套类生成/注解路径一起拖进 MVP 的前提下，为工具层、未来调试器适配层和后续增强预留“多函数/全局/imports”这类 `CompileFunction()` 无法覆盖的内部升级路径。

- [ ] **P3.1** 抽出纯内存模块描述构造逻辑，避免正式功能直接写磁盘
  - 参考 `AngelscriptTestEngineHelper.cpp` 的 `CompileModuleInternal()` / `MakeModuleDesc()` 思路，但不要复用其中 `Saved/Automation` 落盘逻辑；正式 Runtime 功能需要一个真正的内存版 `FAngelscriptModuleDesc` 构造流程。
  - 对于第一阶段的非注解动态模块，优先直接构造 `FAngelscriptModuleDesc` 并走 `CompileModules(ECompileType::SoftReloadOnly)`，而不是先假装它是测试文件再写进磁盘。
  - 只有当后续 Phase 明确要求接入 preprocessor/注解语法时，才引入 synthetic file + preprocessor 的方案。
- [ ] **P3.1** 📦 Git 提交：`[Runtime/AS] Feat: add pure in-memory transient module descriptor builder`

- [ ] **P3.2** 实现 `TransientModule` 路径与显式 entry 选择
  - 模块路径需要支持多顶层声明与指定入口，因此 `FAngelscriptSnippetOptions` 中的 `EntryDeclaration` 在这一路径上必须成为正式必填或等价强约束，而不是继续靠模糊猜测入口函数。
  - 编译完成后按 `GetModuleByModuleName()` / `GetFunctionByDecl()` 找到入口，使用与快速路径一致的上下文执行与结果回传机制，保持两条路径在 API 层的体验一致。
  - 执行后默认立刻 `DiscardModule()`，并验证 `ActiveModules`、`ModulesByScriptModule`、diagnostics 和 reload bookkeeping 能被正确清理。
  - 这一项必须明确写入：它属于内部执行器能力，不是第一阶段公开 façade 的默认对外承诺。
- [ ] **P3.2** 📦 Git 提交：`[Runtime/AS] Feat: implement transient module snippet path with explicit entry selection`

- [ ] **P3.3** 补齐多声明、globals、imports 的动态模块测试
  - 新增或扩展测试覆盖：多函数模块、全局变量、跨函数调用、imports 解析失败与成功边界，以及 execute 后 discard 的状态清理。
  - 重点验证“为什么必须有第二条路径”：这些测试应当证明它们用 `CompileFunction()` 不好处理，而走 `TransientModule` 可以稳定工作。
  - 如果第一阶段对 imports 仍受限于 preprocessor 或 module lookup 行为，本项必须给出清晰 diagnostics，而不是让调用方看到模糊 compile failure。
- [ ] **P3.3** 📦 Git 提交：`[Test/AS] Test: cover transient module snippets with multi-decl and globals`

### Phase 4：明确注解/预处理高级路径的支持策略，而不是模糊留白

> 目标：不要让后续用户误以为“动态片段执行”天然等于“运行时动态定义 `UCLASS`”；要么明确拒绝，要么给出单独的增强路线。

- [ ] **P4.1** 冻结第一阶段对 annotated snippets 的正式结论
  - 现有测试 helper 证明 `CompileAnnotatedModuleFromMemory()` 技术上可行，但它会把动态片段拉进 preprocessor、类生成、reload requirement 与 UObject 生命周期管理，这与“执行一个代码片段”的成本模型完全不同。
  - 本项需要在正式 API 中明确第一阶段的行为：检测到 `UCLASS(` / `USTRUCT(` / `UENUM(` 时，默认返回“当前版本不支持 annotated dynamic snippet”的稳定 diagnostics，而不是静默改走重路径。
  - 这样做的价值不是“保守”，而是避免把 MVP 的轻量执行能力错误包装成通用运行时脚本文件系统。
- [ ] **P4.1** 📦 Git 提交：`[Runtime/AS] Docs: freeze unsupported annotated snippet boundary for MVP`

- [ ] **P4.2** 为后续增强预留 synthetic-file + preprocessor 方案草案
  - 如果后续确有需求支持 annotated dynamic module，应单独围绕 `FAngelscriptPreprocessor`、`CompileAnnotatedModuleFromMemory()` 与 `FAngelscriptClassGenerator` 设计一条高成本路径，而不是继续污染 MVP 主线。
  - 这一项不要求第一阶段就实现，而是要求把后续增强所需的关键改动点、风险和测试落点写清楚：synthetic absolute filename、preprocessor hook、class generation cleanup、discard 后 UObject/class 生命周期边界。
  - 这样下一阶段若真的要做，就不需要重新搜索全仓一次。
- [ ] **P4.2** 📦 Git 提交：`[Runtime/AS] Plan: record follow-up route for annotated transient snippets`

### Phase 5：把新能力接回调试器/工具入口，并补文档与验证矩阵

> 目标：让动态片段执行成为真正可复用的底层能力，而不是又一处只能靠读源码知道怎么调的内部功能。

- [ ] **P5.1** 决定 DebugServer / Editor Console 是否复用新的 runtime executor
  - 当前 `RequestEvaluate` 只做变量求值；在 runtime executor 稳定后，需要明确调试器是否复用这套能力来支持真正的表达式/片段执行，还是继续保持现状。
  - 第一阶段不要求直接做完调试器协议扩展，但至少要把接入方向写清楚：如果要做，应该由 DebugServer 调 runtime executor，而不是再复制一份 `CompileFunction()` 逻辑。
  - 若暂不接入，也要在文档里明确“新能力目前只提供 C++ runtime API”。
- [ ] **P5.1** 📦 Git 提交：`[Debug/AS] Docs: define debugger and tool integration stance for snippet executor`

- [ ] **P5.2** 更新测试与使用文档
  - 在 `Documents/Guides/Test.md` 或相邻文档中补上新的测试前缀/测试组说明，让下一位执行者知道该跑哪些自动化测试验证快速路径、动态模块路径与清理边界。
  - 若新增 public C++ API，也应补一份最小使用说明，至少说明两条路径的适用边界、`EntryDeclaration` / `WrapperDeclaration` 的使用约束、以及 annotated snippet 暂不支持的明确说明。
  - 文档更新的重点是写“为什么是双通道”，而不是只罗列函数签名。
- [ ] **P5.2** 📦 Git 提交：`[Docs/AS] Docs: add snippet execution usage and validation guidance`

## 验收标准

1. 已有正式 runtime 层能力可接受一段 Angelscript 代码片段，并通过**公开 façade**稳定执行非结构性 snippet，而不是继续依赖测试 helper 或 DebugServer 私有逻辑。
2. 快速路径可以编译并执行单函数或函数体片段，能返回 compile/execute diagnostics 与 exception string，并在默认配置下正确清理临时模块/函数。
3. 内部动态模块路径可以处理至少一类 `CompileFunction()` 无法优雅覆盖的场景，例如多函数声明、全局变量或显式入口声明；同时公开 façade 不会把这一路径误包装成轻量 eval API。
4. 第一阶段对 annotated snippets 的态度是明确且可诊断的：要么显式不支持并返回稳定 diagnostics，要么单独实现并附带类生成/清理验证，不能处于模糊状态。
5. 测试已覆盖成功、编译失败、执行异常、cleanup、不同行擎实例隔离，以及至少一组多声明动态模块用例。

## 风险与注意事项

- **风险 1：直接把测试 helper 变成产品代码**
  - `AngelscriptTest` 里的 helper 证明了可行性，但其中的 `Saved/Automation` 文件写入、测试全局作用域和自动化约束不适合作为正式 runtime 行为。正式实现应抽思想，不抄 helper。

- **风险 2：把所有动态片段都强行走 `CompileModules()`**
  - 这样虽然语义统一，但会把即时执行场景拖进完整模块、类生成与热重载语义，成本和副作用都明显过高。

- **风险 3：把 `CompileFunction()` 误当成完整动态脚本系统**
  - 官方 API 与当前仓库测试都表明它适合 ad-hoc function/snippet，不适合承诺完整模块、imports 与注解生命周期。若不明确边界，后续需求会迅速失控。

- **风险 3.5：过早把内部 `TransientModule` 升级路径公开化**
  - 一旦把 `CompileModules()` 路径直接作为 MVP 公共接口承诺，对外语义就会迅速膨胀到模块生命周期、imports、全局状态、甚至类生成与热重载边界；这与“动态执行代码片段”的轻量目标并不等价。

- **风险 4：忽略 diagnostics 与 cleanup，只看能否 execute**
  - 对最终使用者来说，“为什么编不过/跑崩了/下一次为什么又被旧模块影响”比“理论上能执行”更重要。结果对象、诊断捕获与模块清理不是附加项，而是功能本体的一部分。

- **风险 5：过早把调试器协议、Blueprint 暴露与安全问题混进 MVP**
  - 这些方向都可能有价值，但它们属于“如何消费动态片段执行能力”的上层问题。当前阶段先把 runtime 核心能力做对，才能决定后续要不要往 REPL、远程调试或更强隔离扩展。
