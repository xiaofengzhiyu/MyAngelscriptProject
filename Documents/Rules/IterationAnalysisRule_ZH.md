# 迭代分析规则

## 目的

本规则用于约束 `Tools/IterationAnalysis/` 工具（基于 RalphLoop 驱动）的 AI 输出，确保产出的代码分析具有可执行性、可验证性和可追溯性。

产出物必须回答核心问题：当前代码子系统存在哪些具体的质量问题、安全风险和改进机会，证据是什么。

## 适用范围

- 适用于对 `Plugins/Angelscript/Source/` 下各子系统的迭代式代码分析。
- 分析中心必须放在实际源码上，不是 Plan 文档或 AGENTS.md 的声明。
- 每个子系统通过独立的 bat 文件启动分析，bat 调用 `Tools/RalphLoop/run-ralph-loop.bat` 驱动多轮迭代。

### 可用子系统

| 子系统 | bat 文件 | 源码路径 |
|--------|----------|----------|
| RuntimeCore | `RunIterationAnalysis_RalphLoop_RuntimeCore.bat` | `AngelscriptRuntime/Core/` |
| ClassGenerator | `RunIterationAnalysis_RalphLoop_ClassGenerator.bat` | `AngelscriptRuntime/ClassGenerator/` |
| BindSystem | `RunIterationAnalysis_RalphLoop_BindSystem.bat` | `AngelscriptRuntime/Binds/` |
| TestInfrastructure | `RunIterationAnalysis_RalphLoop_TestInfrastructure.bat` | `AngelscriptTest/` |
| Preprocessor | `RunIterationAnalysis_RalphLoop_Preprocessor.bat` | `AngelscriptRuntime/Preprocessor/` |
| DebuggingAndJIT | `RunIterationAnalysis_RalphLoop_DebuggingAndJIT.bat` | `AngelscriptRuntime/Debugging/` + `StaticJIT/` |
| FunctionLibraries | `RunIterationAnalysis_RalphLoop_FunctionLibraries.bat` | `AngelscriptRuntime/FunctionLibraries/` |
| UHTTool | `RunIterationAnalysis_RalphLoop_UHTTool.bat` | `AngelscriptUHTTool/` |

## 工具架构

```text
Tools/IterationAnalysis/
├── RunIterationAnalysis_RalphLoop_<子系统>.bat   # 每个子系统的启动 bat（提示词内嵌）
└── RunIterationAnalysis_RalphLoop_All.bat        # 按顺序执行全部子系统
```

每个 bat 文件自包含全部提示词，不引用外部模板文件。运行时通过 `echo` 将提示词写入 `%TEMP%` 临时文件，传给 RalphLoop，结束后自动清理。

调用链：`模块.bat` → `Tools/RalphLoop/run-ralph-loop.bat` → `Tools/RalphLoop/ralph-loop.ps1`（RalphLoop 循环引擎）

## 输出文件约定

每个子系统有一个**固定的分析文件**，所有轮次的分析内容追加到同一文件末尾：

```text
Documents/AutoPlans/
├── RuntimeCore_Analysis.md
├── ClassGenerator_Analysis.md
├── BindSystem_Analysis.md
├── TestInfrastructure_Analysis.md
├── Preprocessor_Analysis.md
├── DebuggingAndJIT_Analysis.md
├── FunctionLibraries_Analysis.md
└── UHTTool_Analysis.md
```

**不按日期分目录**，便于查找和持续积累。

## 追加写入规则

每轮分析的内容追加到文档末尾，格式如下：

```markdown
---

## 分析 (YYYY-MM-DD HH:MM)

### 发现 <编号>：<标题>

| 项目 | 内容 |
|------|------|
| 维度 | A / B / C / D / E |
| 严重度 | Critical / High / Medium / Low |
| 文件 | `<相对路径>` |
| 行号 | <起始行>-<结束行> |
| 描述 | <问题的具体表现> |
| 根因 | <为什么会出现这个问题> |
| 影响 | <如果不修复会怎样> |

...更多发现...
```

关键约束：

1. **只追加不覆盖**：不得删除或修改已有内容，只在文档末尾追加。
2. **不重复**：不重述前面已记录的发现，每轮只写新东西。
3. **首次特殊处理**：如果文件不存在，创建文件并写入文档标题 + 首次分析内容。
4. **分隔清晰**：每轮以 `---` 分隔线 + `## 分析 (日期时间)` 开头。

## 分析维度

分析时必须覆盖以下 A-D 四个维度，E 为可选：

**A — 代码质量与安全**

读取 5-8 个代表性源文件，查找：空指针解引用缺少检查、静默失败路径、释放后使用风险、资源泄漏（内存、未解绑的委托）、线程安全问题。

**B — 架构与设计**

查找：紧耦合导致难以测试、应该注入但使用了全局状态、职责混合、违反已建立的插件模式（如该用 `FAngelscriptEngineScope` 的地方没用）。

**C — 功能完整性**

查找：UEAS2/Reference 中存在但本仓库缺失或不完整的 API、TODO/FIXME/HACK 注释标注的已知债务、桩实现、应该激活但被禁用的代码路径。

**D — 测试覆盖缺口**

查找：源码目录无对应测试文件、未覆盖的边界条件、使用了错误隔离模式的测试（如直接访问全局状态而非通过 `FAngelscriptEngineScope`）。

**E — 性能与可扩展性**（可选，仅在有具体证据时包含）

查找：对大集合的 O(n²) 遍历、冗余重复计算、游戏线程上的同步等待。

## 迭代加深规则

RalphLoop 对同一子系统执行多轮分析（由 `-MaxIterations` 控制，默认 10 轮）。每轮在已有内容的基础上继续深入，无需感知当前是第几轮。

1. **每轮执行**：先读取已有分析文件了解前面已记录的内容，然后在末尾追加本轮新发现：
   - 探索前面未扫描到的源文件
   - 对前面发现的问题做更深层的根因分析
   - 发现新的维度上的问题
   - 补充遗漏的边界条件和关联影响
2. **首次特殊**：如果分析文件不存在，创建文件并写入标题和首次分析内容
3. **不重复**：前面已记录的发现不再重复，聚焦新内容
4. **每轮结束**：最终消息简要说明本轮新增了什么

## 证据规则

### 已验证事实

"已验证事实"必须来自可直接检查的证据：

- 当前仓库中的源码文件（引用路径和行号）
- 当前仓库中的测试文件（引用具体测试名和断言）
- 当前仓库中的文档（引用具体文档路径和章节）
- 目录结构和文件存在性
- 自己执行的 glob/grep/find 统计结果

### 推断

"推断"必须在文中明确标注为推断，并说明推断依据。允许推断，但不允许把推断伪装成事实。

### 证据粒度

- 分析结论应尽量落到具体文件路径，有行号更好但不强制
- 不得使用"大约"、"可能"、"似乎"等模糊词汇描述已验证事实
- 所有数字必须自己扫描产生，不得照抄 AGENTS.md 或 Plan 文档

### 与 Plan 声明冲突时的处理

如果 Plan 文档声称某任务"已完成"，但扫描源码发现实际未完成或实现不一致，以源码为准，并在分析中明确指出偏差。不得因为 Plan 说了就跳过验证。

## 去重规则

- 不得重述 `Documents/Plans/` 或 `Documents/Guides/TechnicalDebtInventory.md` 中已明确追踪的问题
- 如果某个发现已被追踪，用一行注明后继续
- 不得重述本文档前面轮次已记录的发现
- 新发现的问题才是分析的核心价值

## 禁止事项

- 不得生成不可执行的模糊建议（如"后续优化"、"考虑重构"、"建议改进"）
- 不得把 Plan 中"已标记完成"等同于"代码库中已验证完成"
- 不得编造不存在的源码内容
- 不得把分析文档写成 Plan 的摘要或复述
- 不得在内存中攒完所有发现再一次性写入文件
- 不得跳过 Critical/High 严重度的发现只写 Medium/Low
- 不得删除或覆盖分析文件中已有的内容
