# 改进计划生成规则

## 目的

本规则约束从 `Documents/AutoPlans/` 各分析产出中提取改进点、合成为可执行 Plan 文档的自动化流程。

产出的 Plan 必须回答三个问题：

- **改什么**：从分析产出中筛选出高优先级、可执行的改进项
- **怎么改**：给出分阶段的执行步骤，精确到文件和函数
- **怎么验**：每个改进项必须附带对应的单元测试要求

## ⚠ 输入与输出的严格区分

### 输出位置（只写这里）

- **目录**：`Documents/AutoPlans/Plans/`
- **文件名**：`Plan_<Module>.md`
- **示例**：`Documents/AutoPlans/Plans/Plan_ClassGenerator.md`

### 输入来源（只读不写）

以下目录中的文件是**只读参考输入**，**禁止修改、禁止追加、禁止写入**：

| 来源工具 | 只读文件位置 |
|---------|------------|
| IterationAnalysis | `Documents/AutoPlans/*_Analysis.md` |
| DiscoveryPlanner | `Documents/AutoPlans/DiscoveryPlans/*_Plan.md` |
| TestCoverageGap | `Documents/AutoPlans/TestCoverage/*_TestGaps.md` |
| ArchitectureReview | `Documents/AutoPlans/ArchitectureReview/*_ArchReview.md` |
| ReferenceComparison | `Documents/AutoPlans/ReferenceComparison/*.md` |

**绝对不要向 `DiscoveryPlans/`、`TestCoverage/`、`ArchitectureReview/`、`ReferenceComparison/` 目录写入任何内容。**

## Plan 文档结构

每个 Plan 至少包含以下章节：

### 必须章节

1. **标题** — `# <改进主题>`
2. **背景与目标** — 为什么做、要达到什么状态
3. **分析来源** — 本 Plan 综合了哪些分析文档，每个文档贡献了哪些关键发现
4. **分阶段执行计划** — 按 Phase 组织
5. **单元测试总览** — 汇总所有 Phase 中的测试要求
6. **验收标准** — 可量化的验收条件
7. **风险与注意事项** — 区分"风险"和"已知行为变化"

### 完整结构模板

```markdown
# <改进主题>

## 背景与目标

### 背景

[从分析产出中提炼的问题现状，引用具体的分析文档和源码位置]

### 目标

[要达到什么状态，列出可量化的目标]

## 分析来源

[列出本 Plan 综合了哪些分析文档，每个文档贡献了哪些关键发现]

| 维度 | 分析文档 | 关键发现 |
|------|---------|---------|
| [A] | `*_Analysis.md` | ... |
| [B] | `*_Plan.md` | ... |
| [C] | `*_TestGaps.md` | ... |
| [D] | `*_ArchReview.md` | ... |
| [E] | `*_Analysis.md` (ReferenceComparison) | ... |

## 分阶段执行计划

### Phase 1: <阶段名>

- [ ] **P1.1** <任务标题>
  - <任务的来龙去脉、要达到什么效果、打算怎么实现>
  - 来源：
    - [A] `<分析文档>` — "<具体发现摘要>"
    - [B] `<分析文档>` — "<具体缺陷描述>"
  - 源码验证：`<文件路径>` L<行号> — <当前代码现状确认>
  - 涉及文件：`<具体文件路径>`
- [ ] **P1.1** 📦 Git 提交：`<commit message>`
- [ ] **P1.1-T** 单元测试：<测试描述>
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Tests/<TestFile>.cpp`
  - 测试场景：
    - 正常路径：...
    - 边界条件：...
    - 错误路径：...
  - 测试命名：`Angelscript.TestModule.<Category>.<TestName>`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P1.1-T** 📦 Git 提交：`<test commit message>`

### Phase 2: ...

## 单元测试总览

| 改进项 | 测试文件 | 测试场景 | 优先级 |
|--------|---------|---------|--------|
| P1.1 | ... | ... | ... |

## 验收标准

[可量化的验收条件]

## 风险与注意事项

### 风险

[不确定性，需要评估和缓解策略]

### 已知行为变化

[确定性的副作用，执行时必须处理]
```

## 任务条目规则

### 基本格式

- 所有可执行任务用 `- [ ]` checkbox + 唯一编号（`P1`、`P1.1`）
- **每个可执行任务后必须紧跟一条 `📦 Git 提交` checkbox**
- **每个改进任务后必须紧跟 `-T` 后缀的单元测试任务**
- 紧密关联的小步骤可合并提交，但最后一步仍必须跟提交 checkbox

### 条目详情要求

每个可执行任务不应只有一句标题，**必须在缩进列表中补充**：
- 这个任务的来龙去脉（从哪个分析维度发现的、问题是什么）
- 要达到什么效果
- 打算怎么实现（精确到文件和函数）
- 来源标注（维度编号 + 分析文档路径 + 具体发现摘要）
- 源码验证（文件路径 + 行号 + 当前代码现状确认）

不要使用显式标签（如"背景信息："、"实现目标："），直接用自然语言描述即可。

### 影响范围（迁移/重构类）

对涉及批量文件修改的 Plan，增加"影响范围"章节：先定义操作类型，再按目录分组列出受影响文件及其操作组合。

## 五维交叉分析方法

本工具的核心工作方法是**多维度交叉引用**。AutoPlans 下的 5 个分析维度从不同视角审视同一段代码：

| 维度 | 视角 | 对 Plan 的贡献 |
|------|------|---------------|
| A. 迭代分析 | 代码质量深度审查 | 发现具体代码问题和设计气味 |
| B. 缺陷发现 | 具体缺陷 + 解决方案 | 直接转化为修复条目 |
| C. 测试覆盖 | 测试质量和缺口 | 决定每个条目的 -T 测试任务内容 |
| D. 架构评审 | 架构层面改进建议 | 提供改进方向和根因分析 |
| E. 参考对比 | 其他插件的做法差异 | 提供参考实现和优先级依据 |

**交叉引用规则**：

- 每个 Plan 条目至少引用 **2 个维度**的发现
- 被 3+ 个维度交叉确认的问题 → 优先级提升
- 只被 1 个维度提到的问题 → 优先级降低，需额外源码验证
- 来源标注使用维度编号：`[A]`、`[B]`、`[C]`、`[D]`、`[E]`

## 从分析到 Plan 的转化规则

1. **筛选原则**：不是所有分析发现都要进 Plan。只选取：
   - 有明确源码定位的问题（不含模糊描述）
   - 有可执行解决方案的改进项
   - 与当前项目主线（todo.md）不冲突的改进
   - 被多个维度交叉确认的改进项优先
2. **优先级排序**：
   - P0：影响正确性的缺陷（crash、数据错误、逻辑 bug）
   - P1：影响可维护性的重构（代码重复、职责不清）
   - P2：架构改进（模块边界、扩展性）
   - P3：锦上添花（性能优化、代码风格）
3. **来源追溯**：每个任务条目必须标注来源维度编号和分析文档路径
4. **不凭空发明**：Plan 中的改进项必须有分析产出支撑，不能脱离输入源自行创造
5. **源码验证**：每个条目必须通过读取实际源码确认问题仍然存在，记录文件路径和行号

## 单元测试要求

**每个改进项必须附带对应的单元测试任务**，包括：

1. **测试文件路径**：建议放在 `Plugins/Angelscript/Source/AngelscriptTest/Tests/` 下，文件名遵循 `Angelscript<Feature>Tests.cpp` 命名
2. **测试场景**：至少覆盖
   - 正常路径（happy path）
   - 边界条件（boundary）
   - 错误路径（error path）
3. **测试命名**：遵循 `Angelscript.TestModule.<Category>.<TestName>` 命名规范
4. **测试隔离**：使用 `FAngelscriptEngineScope` 确保引擎隔离
5. **测试框架**：使用 `IMPLEMENT_SIMPLE_AUTOMATION_TEST` 宏

## 迭代探索机制

### 工作方式

```
Round 1: Read all analysis inputs → Generate Plan skeleton with Phase structure → Write to output file
Round 2: Read Round 1 output → Deepen task details, add missing test scenarios → Append to output file
Round 3+: Read previous output → Fill remaining gaps, refine priorities → Append to output file
```

### 续写原则

- **只向 `Documents/AutoPlans/Plans/Plan_<Module>.md` 写入**
- 读取已有 Plan 文档，在文档末尾用 `---` 分隔线和 `## 深化 (日期时间)` 标题追加新内容
- 不得删除或覆盖已有内容
- 已经足够详细的条目保持原样，把精力放在薄弱处
- 每轮可能从源码中发现新的关联改进项，应追加到文档末尾
- 优先级可能随新发现调整，但需在文档中说明调整理由

## 禁止事项

- **不向输入源文件写入任何内容**（`DiscoveryPlans/`、`TestCoverage/`、`ArchitectureReview/`、`ReferenceComparison/` 下的文件）
- 不生成没有来源追溯的改进项
- 不生成没有单元测试要求的改进条目
- 不把多个不相关系统揉成一个 Phase
- 不用"后续优化""待确认"等模糊表述替代具体任务
- 不在 Plan 中写死本地机器路径
- 不重复已存在于 `Documents/Plans/` 中的活跃 Plan 内容
- 不生成 DiscoveryPlanner 风格的 Issue 条目（如 Issue-25），必须使用 Plan 的 checkbox + Phase 格式
