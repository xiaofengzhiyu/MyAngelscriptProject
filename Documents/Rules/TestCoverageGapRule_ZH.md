# 测试覆盖缺口分析规则

## 目的

本规则用于约束 `Tools/TestCoverageGap/` 工具（基于 RalphLoop 驱动）的 AI 输出。工具的核心任务分两部分：

1. **审查现有测试的质量**：读取每个测试文件，评估测试写得对不对、断言够不够、隔离是否正确、有没有反模式。
2. **发现需要新增的测试**：基于已有测试的覆盖盲区和源码的关键路径，提出具体的新增测试建议。

产出物必须回答三个核心问题：
1. 现有测试本身存在什么质量问题？（断言不足、隔离错误、反模式）
2. 现有测试遗漏了哪些关键场景和边界条件？
3. 哪些源码路径完全没有测试，应该新增什么测试？

**分析优先级**：先审查现有测试质量 → 再发现现有测试的遗漏场景 → 最后发现完全无测试的源码路径。

## 适用范围

- 适用于 `Plugins/Angelscript/Source/` 下各子系统的测试质量审查与覆盖补全。
- 分析以**现有测试文件为起点**，逐文件审查质量，然后再向源码侧查找覆盖盲区。
- 每个子系统通过独立的 bat 文件启动，bat 调用 `Tools/RalphLoop/ralph-loop.ps1` 驱动多轮迭代。

### 可用子系统

| 子系统 | bat 文件 | 源码路径 | 对应测试目录 |
|--------|----------|----------|-------------|
| RuntimeCore | `RunTestCoverageGap_RalphLoop_RuntimeCore.bat` | `AngelscriptRuntime/Core/` | `AngelscriptTest/Core/`, `Subsystem/`, `GC/`, `FileSystem/` |
| ClassGenerator | `RunTestCoverageGap_RalphLoop_ClassGenerator.bat` | `AngelscriptRuntime/ClassGenerator/` | `AngelscriptTest/ClassGenerator/`, `HotReload/` |
| BindSystem | `RunTestCoverageGap_RalphLoop_BindSystem.bat` | `AngelscriptRuntime/Binds/` | `AngelscriptTest/Bindings/` |
| Preprocessor | `RunTestCoverageGap_RalphLoop_Preprocessor.bat` | `AngelscriptRuntime/Preprocessor/` | `AngelscriptTest/Preprocessor/`, `Compiler/` |
| DebuggingAndJIT | `RunTestCoverageGap_RalphLoop_DebuggingAndJIT.bat` | `AngelscriptRuntime/Debugging/`, `StaticJIT/` | `AngelscriptTest/Debugger/` |
| FunctionLibraries | `RunTestCoverageGap_RalphLoop_FunctionLibraries.bat` | `AngelscriptRuntime/FunctionLibraries/` | `AngelscriptTest/Bindings/`（分散） |
| LanguageFeatures | `RunTestCoverageGap_RalphLoop_LanguageFeatures.bat` | `AngelscriptRuntime/Core/`（语言层） | `AngelscriptTest/Angelscript/`, `Internals/`, `Interface/` |
| EditorAndTools | `RunTestCoverageGap_RalphLoop_EditorAndTools.bat` | `AngelscriptEditor/` | `AngelscriptTest/Editor/`, `AngelscriptEditor/Private/Tests/` |

## 工具架构

```text
Tools/TestCoverageGap/
├── RunTestCoverageGap.ps1                               # 主脚本，包含所有模块提示词
├── RunTestCoverageGap_RalphLoop_<子系统>.bat             # 每个子系统的启动 bat
└── RunTestCoverageGap_RalphLoop_All.bat                  # 按顺序执行全部子系统
```

调用链：`模块.bat` → `RunTestCoverageGap.ps1` → `Tools/RalphLoop/ralph-loop.ps1`（RalphLoop 循环引擎）

## 输出文件约定

```text
Documents/AutoPlans/TestCoverage/
├── RuntimeCore_TestGaps.md
├── ClassGenerator_TestGaps.md
├── BindSystem_TestGaps.md
├── Preprocessor_TestGaps.md
├── DebuggingAndJIT_TestGaps.md
├── FunctionLibraries_TestGaps.md
├── LanguageFeatures_TestGaps.md
└── EditorAndTools_TestGaps.md
```

## 追加写入规则

每轮内容追加到文档末尾。发现分为两大类：**现有测试问题** 和 **新增测试建议**。

```markdown
---

## 测试审查 (YYYY-MM-DD HH:MM)

### 一、现有测试问题

#### Issue-<编号>：<标题>

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion / BadIsolation / AntiPattern / FlakyRisk / WrongHelper / MissingCleanup |
| 测试文件 | `<相对路径>` |
| 测试名 | `Angelscript.TestModule.<...>` |
| 行号范围 | <起始行>-<结束行> |
| 问题描述 | <具体表现，引用代码片段> |
| 影响 | <该问题导致什么后果：误报绿灯 / 测试间泄漏 / 不稳定 / 维护困难> |
| 修复建议 | <具体修改步骤> |

...更多问题...

### 二、需要新增的测试

#### NewTest-<编号>：<标题>

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario / MissingEdgeCase / MissingErrorPath / NoTestForSource |
| 关联源码 | `<被测源码文件>` |
| 关联函数 | `<函数签名或名称>` |
| 现有测试覆盖 | 完全无测试 / 有测试但缺少 X 场景 |
| 风险评估 | 不补充的后果 |
| 建议测试名 | `Angelscript.TestModule.<Category>.<TestName>` |
| 测试类型 | Unit / Integration / Scenario |
| 测试文件 | 新建 `<文件名>` 或追加到 `<现有文件>` |
| 场景描述 | <测试什么行为> |
| 输入/前置 | <测试需要的初始状态和输入数据> |
| 期望行为 | <通过条件，具体的断言> |
| 使用的 Helper | `ASTEST_COMPILE_RUN_*` / `FAngelscriptTestFixture` / 其他 |
| 优先级 | P0 / P1 / P2 / P3 |

...更多新增...

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | N | Issue-X |
| BadIsolation | M | Issue-Y |
| ... | ... | ... |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | N | NoTestForSource: A, MissingErrorPath: B |
| P1 | M | MissingScenario: C, MissingEdgeCase: D |
| ... | ... | ... |
```

关键约束：

1. **只追加不覆盖**：不得删除或修改已有内容，只在文档末尾追加。
2. **不重复**：不重述前面已记录的内容，每轮只写新发现。
3. **首次特殊处理**：如果文件不存在，创建文件并写入标题 + 首次内容。
4. **分隔清晰**：每轮以 `---` 分隔线 + `## 测试审查 (日期时间)` 开头。
5. **现有问题优先**：每轮应先审查现有测试的问题，再分析新增需求。
6. **修复/建议必填**：每个问题必须附带修复建议，每个新增测试必须附带完整规格。

## 现有测试问题类型

### WeakAssertion — 断言不充分

测试存在但断言太弱，无法真正验证行为。判断标准：
- 只检查了 `!= nullptr` 但没验证具体值
- 只有一个 `TestTrue` 但被测函数有多个分支
- 断言了返回值但没验证状态变化（成员变量、委托触发、日志输出）
- 使用了过于宽泛的判断（如只检查"编译成功"但不验证执行结果）

### BadIsolation — 隔离不正确

测试间存在状态泄漏或依赖。判断标准：
- 未使用 `FAngelscriptEngineScope` 而直接操作全局状态
- 测试 A 的残留状态影响测试 B 的结果
- 共享引擎未在测试间正确 reset
- 测试依赖特定的执行顺序才能通过

### AntiPattern — 测试反模式

违反项目测试规范或通用测试最佳实践。判断标准：
- 使用了已废弃的 helper（如 `FScopedTestEngineGlobalScope`）
- 直接访问 `CurrentWorldContext` 而非通过 `FScopedTestWorldContextScope`
- 测试文件超过 500 行（违反单文件单职责）
- 在一个测试用例中测试了多个不相干功能
- 硬编码了路径、魔法数字或平台相关假设

### FlakyRisk — 不稳定风险

测试可能在特定条件下失败。判断标准：
- 依赖时序（`Sleep` / `FPlatformProcess::Sleep`）
- 依赖外部资源（文件系统、网络）
- 依赖未初始化的 UE 子系统
- 浮点比较未使用容差

### WrongHelper — 使用了错误的 Helper

Helper 选择不当导致测试不正确或不高效。判断标准：
- 需要完整引擎但用了 Clone 模式
- 简单脚本编译测试却构造了完整 World
- 可以用 `ASTEST_COMPILE_RUN_*` 一行搞定但手写了大量 setup 代码

### MissingCleanup — 缺少清理

测试创建了资源但未正确清理。判断标准：
- Spawn 了 Actor 但未在测试结束时销毁
- 注册了委托但未解绑
- 修改了全局设置但未恢复
- 创建了临时文件但未删除

## 新增测试的原因类型

### MissingScenario — 缺少关键场景

现有测试覆盖了基本功能但缺少重要使用场景：
- 多线程/并发场景
- 与其他子系统的交互场景
- 用户常见的组合使用模式
- 性能边界场景

### MissingEdgeCase — 缺少边界条件

现有测试只覆盖了 happy path：
- 空指针 / null / None 输入
- 空容器 / 单元素 / 大容器
- 零值 / 负值 / 最大值 / 溢出
- 对象已销毁 / GC 后的调用

### MissingErrorPath — 缺少错误路径

源码中有错误处理但测试未触发：
- `if (!IsValid(...))` 分支
- `ensure()` / `check()` 条件
- 编译失败 / 运行时错误的恢复路径
- 日志警告路径

### NoTestForSource — 源码完全无测试

源码文件或公共 API 没有任何对应测试。

## 分析方法

### 第一步：审查现有测试质量（每轮主要工作）

逐文件读取该模块的测试 `.cpp` 文件，对每个文件检查：
1. **断言密度**：每个 `IMPLEMENT_SIMPLE_AUTOMATION_TEST` 内的 `TestEqual` / `TestTrue` / `TestNotNull` 数量是否合理
2. **隔离模式**：是否正确使用 `FAngelscriptEngineScope`、`FScopedTestWorldContextScope`
3. **Helper 选择**：使用的 `ASTEST_*` 宏是否匹配测试场景
4. **清理完整性**：Spawn 的 Actor、注册的委托、修改的状态是否都有 cleanup
5. **反模式检测**：是否使用了废弃 API、是否违反单文件单职责
6. **稳定性**：是否有时序依赖、外部资源依赖、平台假设

### 第二步：分析现有测试的场景遗漏

对已有测试文件，分析它覆盖的被测源码：
1. 被测函数的分支数 vs 测试用例数
2. 是否只测了正常路径没测错误路径
3. 输入值是否单一（只有一组固定输入）
4. 是否有明显的边界条件被跳过

### 第三步：发现完全无测试的源码路径

最后扫描对应源码目录，找出完全没有被任何测试覆盖的文件/函数。

## 优先级评定

| 优先级 | 条件 |
|--------|------|
| P0 | 核心公共 API 完全无测试；影响运行时正确性的代码路径 |
| P1 | 有测试但缺少关键错误路径；生命周期关键函数缺少边界测试 |
| P2 | 辅助函数无测试；非核心路径的边界条件缺失 |
| P3 | 内部实现细节无独立测试（可通过集成测试间接覆盖） |

## 测试建议质量要求

测试建议必须达到以下标准：

1. **可直接编写**：读完建议后能直接写出测试代码，不需要额外调研。
2. **命名规范**：遵循 `Angelscript.TestModule.<Category>.<TestName>` 格式。
3. **Helper 明确**：指定使用哪个现有 helper 宏（`ASTEST_COMPILE_RUN_*` / `FAngelscriptTestFixture` 等）。
4. **文件归属明确**：说明应该新建文件还是追加到哪个现有文件。
5. **断言具体**：不能只说"验证结果正确"，要说明具体的断言表达式。
6. **遵循项目规范**：测试文件 300-500 行，单文件单职责，共享代码放 `Shared/`。

## 迭代加深规则

RalphLoop 对同一子系统执行多轮分析（由 `-MaxIterations` 控制，默认 10 轮）。

1. **每轮执行**：先读取已有文件了解已记录的缺口，然后在末尾追加新发现：
   - 扫描前面未覆盖的源文件
   - 对已有测试做更深的断言充分性分析
   - 发现跨文件交互路径的覆盖缺口
   - 补充前面遗漏的边界条件
2. **首次特殊**：如果文件不存在，创建文件并写入标题和首次内容
3. **不重复**：前面已记录的缺口不再重复
4. **覆盖率快照演进**：每轮更新当前分析范围内的覆盖率快照

## 与其他工具的关系

- 如果对应的 `Documents/AutoPlans/<Module>_Analysis.md` 存在，读取其中 D 维度（测试覆盖缺口）的发现，避免重复
- 如果对应的 `Documents/AutoPlans/DiscoveryPlans/<Module>_Plan.md` 存在，读取其中与测试相关的方案，避免重复
- 本工具聚焦"**测试侧**"的系统性分析，比 IterationAnalysis 的 D 维度更深入、更全面

## 证据规则

### 已验证事实

- 源码文件的存在性和公共 API 列表（通过读取 .h 文件）
- 测试文件的存在性和测试用例列表（通过搜索 `IMPLEMENT_SIMPLE_AUTOMATION_TEST`）
- 测试中的断言数量和类型（通过搜索 `TestEqual` / `TestTrue` 等）
- 函数调用关系（通过 grep 源码函数名在测试中的出现）

### 推断

标注为推断的内容：
- "该函数可能在集成测试中被间接覆盖"
- "该错误路径在生产中触发概率较低"

## 禁止事项

- 不得只列"无测试"不给测试建议
- 不得建议过于笼统的测试（如"测试该函数的各种情况"）
- 不得建议与现有测试重复的用例
- 不得忽略 ThirdParty/ 下的代码（第三方代码不需要测试，直接跳过）
- 不得把 `Testing/` 目录下的测试基础设施代码当作"缺少测试的源码"
- 不得在内存中攒完所有发现再一次性写入
- 不得删除或覆盖已有内容
