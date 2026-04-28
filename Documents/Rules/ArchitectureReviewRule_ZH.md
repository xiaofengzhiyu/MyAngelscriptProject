# 架构与扩展性分析规则

## 目的

本规则用于约束 `Tools/ArchitectureReview/` 工具（基于 RalphLoop 驱动）的 AI 输出。工具的核心任务是：

1. **分析当前插件的架构设计**：审视模块边界、依赖方向、抽象层次、扩展点设计。
2. **对比参考插件的架构实践**：从 UnLua、puerts、sluaunreal、UnrealCSharp、UEAS2 中提取优秀的架构模式。
3. **提出可操作的架构改进方案**：将参考插件中值得借鉴的模式转化为当前插件的具体改进步骤。

与 `ReferenceComparison`（广度对比 D1-D11）不同，本工具**深度聚焦架构层面**，每个模块只关注一个架构关注点，深入到代码级别分析。

产出物必须回答：
1. 当前架构在该关注点上的具体设计是什么？（用源码证据）
2. 参考插件在同一关注点上是怎么做的？（引用参考源码）
3. 有什么具体可落地的改进？（步骤、文件、风险）

## 适用范围

- 以 `Plugins/Angelscript/Source/` 为分析中心。
- 参考源码位于 `Reference/`（UnLua、puerts、sluaunreal、UnrealCSharp）和 UEAS2（通过 `AgentConfig.ini` 的 `References.HazelightAngelscriptEngineRoot` 配置）。
- 已有的横向对比文档 `Documents/Comparisons/2026-04-07/05-12_CrossComparison_*.md` 可作为输入参考，但本工具需要更深入到代码级别。

### 可用模块

| 模块 | 关注点 | 当前源码 | 参考重点 |
|------|--------|----------|---------|
| ModuleStructure | 模块划分与依赖拓扑 | 所有 .Build.cs + .uplugin | puerts 模块拆分、UnLua 模块边界 |
| BindingPipeline | 绑定注册管线与可扩展性 | Binds/, Core/AngelscriptBinds.* | UnrealCSharp 代码生成、puerts 声明文件 |
| TypeSystem | 类型系统与反射集成 | Core/AngelscriptType.*, ClassGenerator/ | UnLua 反射桥接、puerts TypeScript 映射 |
| HotReloadArch | 热重载架构与状态保持 | ClassGenerator/, Core/ | UnLua 热重载策略、UEAS2 原版实现 |
| ScriptLifecycle | 脚本编译-加载-执行管线 | Preprocessor/, Core/AngelscriptEngine.* | puerts 模块系统、UnLua chunk 管理 |
| DebugAndToolchain | 调试与工具链架构 | Debugging/, StaticJIT/ | puerts V8 Inspector、UnLua 调试器 |
| ExtensionPoints | 插件扩展点与用户可定制性 | 全局（Settings, Subsystem, Delegate） | UnrealCSharp 扩展模式、UnLua 模块钩子 |
| EditorArch | 编辑器集成架构 | AngelscriptEditor/ | UnLua 编辑器工具、puerts 编辑器集成 |

## 工具架构

```text
Tools/ArchitectureReview/
├── RunArchitectureReview.ps1                              # 主脚本
├── RunArchitectureReview_RalphLoop_<模块>.bat              # 每个模块的启动 bat
└── RunArchitectureReview_RalphLoop_All.bat                 # 按顺序执行全部模块
```

调用链：`模块.bat` → `RunArchitectureReview.ps1` → `Tools/RalphLoop/ralph-loop.ps1`

## 输出文件约定

```text
Documents/AutoPlans/ArchitectureReview/
├── ModuleStructure_ArchReview.md
├── BindingPipeline_ArchReview.md
├── TypeSystem_ArchReview.md
├── HotReloadArch_ArchReview.md
├── ScriptLifecycle_ArchReview.md
├── DebugAndToolchain_ArchReview.md
├── ExtensionPoints_ArchReview.md
└── EditorArch_ArchReview.md
```

## 追加写入规则

每轮内容追加到文档末尾，每个架构发现包含"现状分析"、"参考对比"和"改进方案"三部分：

```markdown
---

## 架构分析 (YYYY-MM-DD HH:MM)

### Arch-<编号>：<标题>

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | <具体的架构关注点> |
| 当前设计 | <一句话概述当前做法> |
| 源码证据 | `<文件路径>:<行号>` — <代码片段或设计要点> |
| 优点 | <当前设计的优势> |
| 不足 | <当前设计的局限性> |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | <做法描述> | `Reference/UnLua/<路径>` | <值得学习的点> |
| puerts | <做法描述> | `Reference/puerts/<路径>` | <值得学习的点> |
| ... | ... | ... | ... |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | <一句话概述> |
| 具体步骤 | 1. ... 2. ... 3. ... |
| 涉及文件 | `file1.h`, `file2.cpp`, ... |
| 预估工作量 | S / M / L / XL |
| 架构风险 | <修改可能引起的连锁影响> |
| 兼容性 | <对现有脚本/插件用户的影响> |
| 验证方式 | <如何验证改进有效且未引入回归> |

...更多发现...

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P0 | Arch-N | ... | 结构性重构 | 高 |
| P1 | Arch-M | ... | 扩展点新增 | 中 |
| ... | ... | ... | ... | ... |
```

关键约束：

1. **只追加不覆盖**：不得删除或修改已有内容。
2. **不重复**：不重述前面已记录的发现。
3. **首次特殊处理**：文件不存在时创建并写入标题 + 首次内容。
4. **三段必填**：每个发现必须包含"现状"、"对比"、"方案"三段。
5. **源码级证据**：现状和对比都必须引用具体源码文件和行号。

## 分析维度

### 结构性分析（模块层面）

- 模块划分是否合理：每个模块是否单一职责
- 依赖方向是否正确：是否存在反向依赖或循环依赖
- 抽象层次是否清晰：是否有跨层调用（Runtime 直接依赖 Editor）
- 构建隔离：修改一个模块是否会触发大范围重编

### 扩展性分析（接口层面）

- 扩展点是否充足：用户（脚本开发者）能否在不修改插件源码的情况下定制行为
- 抽象接口 vs 具体实现：关键组件是否通过接口解耦
- 依赖注入：是否支持替换核心组件（如替换预处理器、替换绑定注册策略）
- 事件/委托暴露：关键生命周期事件是否可被外部订阅

### 演进性分析（未来需求）

- 多实例支持：架构是否阻碍多引擎实例并存
- 新语言特性：添加新的 AS 语言特性需要改动多少文件
- 新绑定类型：添加新的 UE 类型绑定的阻力有多大
- 跨平台：架构中是否有阻碍其他平台支持的硬编码

## 参考插件的查看重点

### UnLua (`Reference/UnLua/`)
- 模块化设计：如何把 Lua VM 集成、反射绑定、编辑器工具拆分
- 热重载架构：Lua chunk 的重载策略和状态保持
- 扩展钩子：用户自定义绑定的注册机制

### puerts (`Reference/puerts/`)
- TypeScript 声明文件生成：代码生成架构
- V8 Inspector 集成：调试架构如何与引擎解耦
- 多引擎实例：是否支持以及如何实现

### sluaunreal (`Reference/sluaunreal/`)
- 绑定自动生成：与手写绑定的架构差异
- 性能优化策略：内存和调用开销的架构级优化

### UnrealCSharp (`Reference/UnrealCSharp/`)
- .NET 运行时集成：跨语言桥接的抽象层设计
- 代码生成管线：从 UHT 到可用绑定的完整管线架构
- 扩展模块模式：用户如何扩展绑定能力

### UEAS2（原版参考）
- 作为直接上游，架构差异反映了当前项目的演化方向
- 重点关注 AngelPortV2 偏离 UEAS2 的部分，评估偏离是改进还是退化

## 与已有对比文档的关系

- `Documents/Comparisons/2026-04-07/05_CrossComparison_Architecture.md` 等已有横向对比是**输入参考**
- 本工具在已有对比的基础上**深入到代码级别**，关注"能落地的架构改进"而非"差异描述"
- 已有对比中提到但未给出改进方案的架构差异，是本工具的重点分析对象

## 改进方案质量要求

1. **可落地**：步骤具体到文件和函数级别。
2. **向后兼容**：明确对现有脚本用户的影响。
3. **可增量**：优先推荐可以分步实施的方案，避免"大爆炸"重构。
4. **有验证**：每个方案都有明确的验证方式（测试、编译、运行时行为）。
5. **标注参考来源**：明确哪个参考插件启发了该方案，方便后续追溯。

## 优先级评定

| 优先级 | 条件 |
|--------|------|
| P0 | 阻碍当前主线（P1/P2）推进的架构问题 |
| P1 | 影响扩展性但当前可绕过的架构限制 |
| P2 | 改善开发体验或代码质量的架构优化 |
| P3 | 面向未来需求的架构储备 |

## 证据规则

- 当前架构分析必须引用 `Plugins/Angelscript/Source/` 下的具体文件和行号
- 参考插件分析必须引用 `Reference/` 下的具体文件和行号
- 已有对比文档中的结论可以引用但必须通过源码验证
- 推断必须明确标注

## 禁止事项

- 不得把已有 `CrossComparison_*.md` 的内容复述为新发现
- 不得只做差异描述不给改进方案
- 不得建议不可增量实施的"大爆炸"重构（除非确实无法避免，此时需要明确标注风险）
- 不得忽略向后兼容性
- 不得在未读取参考源码的情况下凭印象描述参考插件的架构
- 不得在内存中攒完所有发现再一次性写入
- 不得删除或覆盖已有内容
