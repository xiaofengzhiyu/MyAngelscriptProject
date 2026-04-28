# 发现与规划规则

## 目的

本规则用于约束 `Tools/DiscoveryPlanner/` 工具（基于 RalphLoop 驱动）的 AI 输出，确保产出的缺陷发现、重构建议和架构改进能直接转化为可执行的行动计划。

与 `IterationAnalysis` 侧重"发现并记录问题"不同，`DiscoveryPlanner` 的核心交付是：**每个发现都必须附带具体的解决方案和实施计划**。

产出物必须回答两个核心问题：
1. 当前代码存在什么具体问题，证据是什么？
2. 应该怎么修，修复步骤、优先级和风险是什么？

## 适用范围

- 适用于对 `Plugins/Angelscript/Source/` 下各子系统的缺陷发现与解决方案规划。
- 分析中心必须放在实际源码上，不是 Plan 文档或 AGENTS.md 的声明。
- 每个子系统通过独立的 bat 文件启动，bat 调用 `Tools/RalphLoop/ralph-loop.ps1` 驱动多轮迭代。

### 可用子系统

| 子系统 | bat 文件 | 源码路径 |
|--------|----------|----------|
| RuntimeCore | `RunDiscoveryPlanner_RalphLoop_RuntimeCore.bat` | `AngelscriptRuntime/Core/` |
| ClassGenerator | `RunDiscoveryPlanner_RalphLoop_ClassGenerator.bat` | `AngelscriptRuntime/ClassGenerator/` |
| BindSystem | `RunDiscoveryPlanner_RalphLoop_BindSystem.bat` | `AngelscriptRuntime/Binds/` |
| TestInfrastructure | `RunDiscoveryPlanner_RalphLoop_TestInfrastructure.bat` | `AngelscriptTest/` |
| Preprocessor | `RunDiscoveryPlanner_RalphLoop_Preprocessor.bat` | `AngelscriptRuntime/Preprocessor/` |
| DebuggingAndJIT | `RunDiscoveryPlanner_RalphLoop_DebuggingAndJIT.bat` | `AngelscriptRuntime/Debugging/` + `StaticJIT/` |
| FunctionLibraries | `RunDiscoveryPlanner_RalphLoop_FunctionLibraries.bat` | `AngelscriptRuntime/FunctionLibraries/` |
| UHTTool | `RunDiscoveryPlanner_RalphLoop_UHTTool.bat` | `AngelscriptUHTTool/` |

## 工具架构

```text
Tools/DiscoveryPlanner/
├── RunDiscoveryPlanner.ps1                            # 主脚本，包含所有模块提示词
├── RunDiscoveryPlanner_RalphLoop_<子系统>.bat          # 每个子系统的启动 bat
└── RunDiscoveryPlanner_RalphLoop_All.bat               # 按顺序执行全部子系统
```

调用链：`模块.bat` → `RunDiscoveryPlanner.ps1` → `Tools/RalphLoop/ralph-loop.ps1`（RalphLoop 循环引擎）

## 输出文件约定

每个子系统有一个**固定的发现与规划文件**，所有轮次的内容追加到同一文件末尾：

```text
Documents/AutoPlans/DiscoveryPlans/
├── RuntimeCore_Plan.md
├── ClassGenerator_Plan.md
├── BindSystem_Plan.md
├── TestInfrastructure_Plan.md
├── Preprocessor_Plan.md
├── DebuggingAndJIT_Plan.md
├── FunctionLibraries_Plan.md
└── UHTTool_Plan.md
```

**不按日期分目录**，便于查找和持续积累。

## 追加写入规则

每轮内容追加到文档末尾，每个发现必须包含"问题"和"方案"两部分：

```markdown
---

## 发现与方案 (YYYY-MM-DD HH:MM)

### Issue-<编号>：<标题>

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect / Refactoring / Architecture |
| 严重度 | Critical / High / Medium / Low |
| 文件 | `<相对路径>` |
| 行号 | <起始行>-<结束行> |
| 问题 | <具体表现，引用代码片段> |
| 根因 | <为什么会出现这个问题> |
| 影响 | <不修复的后果> |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | <修复策略的一句话概述> |
| 具体步骤 | 1. ... 2. ... 3. ... |
| 涉及文件 | `file1.cpp`, `file2.h`, ... |
| 预估工作量 | S / M / L / XL |
| 风险 | <修改可能带来的副作用> |
| 前置依赖 | <需要先完成什么> 或 无 |
| 验证方式 | <如何确认修复有效> |

...更多发现...

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P0 | Issue-N | Defect | 立即修复 |
| P1 | Issue-M | Architecture | 下个迭代 |
| ... | ... | ... | ... |
```

关键约束：

1. **只追加不覆盖**：不得删除或修改已有内容，只在文档末尾追加。
2. **不重复**：不重述前面已记录的发现，每轮只写新东西。
3. **首次特殊处理**：如果文件不存在，创建文件并写入文档标题 + 首次内容。
4. **分隔清晰**：每轮以 `---` 分隔线 + `## 发现与方案 (日期时间)` 开头。
5. **方案必填**：每个发现必须附带解决方案，不允许只列问题不给方案。

## 三类发现维度

### Defect — 缺陷

代码中存在的实际 bug 或潜在运行时错误：

- 空指针解引用缺少检查
- 数组/容器越界访问
- 资源泄漏（内存、委托未解绑、Handle 未关闭）
- 静默失败路径（错误被吞掉）
- 线程安全问题（竞态条件、无保护共享状态）
- 释放后使用风险（悬空指针/引用）
- 类型转换不安全（未检查的 Cast）
- 异常路径下的状态不一致

### Refactoring — 重构点

代码可以被改善但目前不构成 bug 的地方：

- 代码重复（两处或更多相似实现应提取公共函数）
- 过长函数（>100 行的函数应考虑拆分）
- 深层嵌套（>3 层嵌套的 if/for 应简化）
- 命名不一致或误导（函数名与实际行为不符）
- 魔法数字/硬编码字符串
- 废弃代码（`#if 0`、注释掉的代码块、永远为 false 的条件）
- 过度复杂的模板元编程
- 可以用更现代的 C++/UE API 替代的旧模式

### Architecture — 架构问题

系统设计层面的结构性问题：

- 循环依赖（模块 A 依赖 B 又被 B 依赖）
- 层级违反（上层模块直接访问下层内部实现）
- 职责混合（一个类/函数承担了不相干的多个职责）
- 全局状态滥用（该通过参数/依赖注入传递的用了全局变量）
- 接口缺失（外部直接操作内部数据结构而非通过抽象接口）
- 扩展性阻碍（添加新功能需要修改多个不相干文件）
- 与已建立的架构模式不一致（如该用 `FAngelscriptEngineScope` 的地方没用）
- 模块边界模糊（Runtime 代码引用了 Editor 功能）

## 方案质量要求

解决方案必须达到以下标准才算合格：

1. **可执行**：开发者读完方案后能直接开始编码，不需要再做额外调研。
2. **步骤具体**：不能只说"重构这个函数"，要说明怎么重构、拆成什么、新函数放哪里。
3. **文件明确**：列出所有需要修改的文件路径。
4. **风险可控**：说明修改可能影响的其他功能，以及如何降低风险。
5. **可验证**：提供验证方式（测试用例、编译检查、运行时行为）。

## 迭代加深规则

RalphLoop 对同一子系统执行多轮分析（由 `-MaxIterations` 控制，默认 10 轮）。

1. **每轮执行**：先读取已有文件了解前面已记录的内容，然后在末尾追加本轮新发现：
   - 探索前面未扫描到的源文件
   - 对前面发现的问题进行更深层次的根因分析
   - 发现跨文件/跨模块的关联问题
   - 完善前面方案中遗漏的步骤或风险
2. **首次特殊**：如果文件不存在，创建文件并写入标题和首次内容
3. **不重复**：前面已记录的发现不再重复，聚焦新内容
4. **方案演进**：后续轮次可以补充前面方案的细节，但以追加"补充说明"形式，不修改原方案

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

- 分析结论应尽量落到具体文件路径和行号
- 不得使用"大约"、"可能"、"似乎"等模糊词汇描述已验证事实
- 所有数字必须自己扫描产生，不得照抄 AGENTS.md 或 Plan 文档

### 与 Plan 声明冲突时的处理

如果 Plan 文档声称某任务"已完成"，但扫描源码发现实际未完成或实现不一致，以源码为准，并在发现中明确指出偏差。

## 优先级评定

每轮结束时的汇总表中，优先级按以下规则评定：

| 优先级 | 条件 |
|--------|------|
| P0 | Critical 严重度的 Defect；影响正确性或安全性 |
| P1 | High 严重度的 Defect 或 Architecture 问题 |
| P2 | Medium 严重度的任意类型；或 High 严重度的 Refactoring |
| P3 | Low 严重度的任意类型 |

## 与 IterationAnalysis 的关系

- `IterationAnalysis` 侧重**发现和记录**代码质量问题（产出 `*_Analysis.md`）
- `DiscoveryPlanner` 侧重**发现问题并制定可执行方案**（产出 `*_Plan.md`）
- 两者可以独立运行，也可以配合使用：先用 IterationAnalysis 做全面扫描，再用 DiscoveryPlanner 对重点问题制定解决方案
- DiscoveryPlanner 应读取对应的 `*_Analysis.md`（如果存在），避免重复已有发现，聚焦新问题或为已知问题提供方案

## 去重规则

- 不得重述 `Documents/Plans/` 或 `Documents/Guides/TechnicalDebtInventory.md` 中已明确追踪的问题
- 不得重述本文档前面轮次已记录的发现
- 如果对应的 `Documents/AutoPlans/<Module>_Analysis.md` 中已有某个发现，可以引用其编号并直接补充方案，无需重复问题描述
- 新发现 + 新方案才是核心价值

## 禁止事项

- 不得生成不可执行的模糊建议（如"后续优化"、"考虑重构"、"建议改进"）
- 不得只列问题不给方案
- 不得把 Plan 中"已标记完成"等同于"代码库中已验证完成"
- 不得编造不存在的源码内容
- 不得把文档写成 Plan 的摘要或复述
- 不得在内存中攒完所有发现再一次性写入文件
- 不得跳过 Critical/High 严重度的发现只写 Medium/Low
- 不得删除或覆盖文件中已有的内容
- 方案步骤中不得使用"视情况而定"、"根据需要"等逃避表述
