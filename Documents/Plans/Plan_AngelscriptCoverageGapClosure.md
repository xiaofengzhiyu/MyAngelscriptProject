# Angelscript 测试覆盖缺口收敛计划

## 背景与目标

本次静态盘点、历史日志和外部对标一起表明：Angelscript 测试体系已经不小，但仍然呈现“核心层相对厚、若干高风险边界明显偏薄”的特征。

### 已确认的覆盖现状

- `Plugins/Angelscript/Source/AngelscriptTest/` 目录下源码注册点约 `340` 个；`Native`、`Internals`、`Bindings`、`Angelscript`、`Core` 等层级已形成较多测试资产。
- `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/` 目前只有 `6` 个文件、约 `31` 个注册点，说明纯 C++ runtime unit 层仍然偏薄。
- `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/` 当前为空，Editor unit 层实际上尚未建立。

### 已确认的薄弱区与结构漂移

- `Delegate/` 只有 `2` 个场景测试。
- `GC/` 只有 `3` 个场景测试，主要覆盖 destroy/world teardown 的 happy path。
- `Blueprint/` 约 `7` 个用例，仍偏集中于基础继承与 runtime 调用链。
- `Component/` 约 `6` 个用例，复杂组件交互与生命周期边界不足。
- `Editor/` 目前只有 `1` 个 source navigation 测试。
- `Template/` 已经存在真实测试，但命名前缀是 `Angelscript.Template.*`，与文档中的 `Angelscript.TestModule.*` 口径不一致。
- `Examples/` 既承担示例回归职责，又与宿主项目真实脚本存在命名碰撞风险，职责边界不够清晰。

### 外部对标补充出的 backlog 输入

结合 UE 官方自动化文档、Hazelight Angelscript 文档、AngelScript 官方测试套件与其他脚本桥接项目，可以为本仓库后续补测提供以下高优先 backlog 输入；这些主题在执行前仍需再确认是否构成本仓库的“已证实缺口”：

1. save/load / bytecode persistence
2. exception / error translation
3. memory / GC stress（循环引用、大对象图、长时压力）
4. networking / replication / hostile network
5. debugging / source navigation / editor helper 族
6. dedicated struct/enum / asset/editor integration buckets

### 目标

1. 先补最薄、最容易形成回归盲区的 scenario/editor/runtime 单元层。
2. 把 `Examples/`、`Template/`、`Editor unit`、`Runtime unit` 的职责再固定一次，避免未来新增测试继续落在错误层级。
3. 形成一条从“短平快补风险桶”到“扩展高级能力面”的渐进路线图，使后续补测可以分批推进。

> 执行顺序约束：本计划默认在 `Plan_AngelscriptTestBaselineRecovery.md` 完成、`Angelscript.TestModule` 基线停止漂移之后再启动；不要与红灯恢复并行推进。

## 范围与边界

- **范围内**
  - `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/`
  - `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/`
  - `Plugins/Angelscript/Source/AngelscriptTest/`
  - `Documents/Guides/Test.md`
  - 必要时更新 `Documents/Guides/TestCatalog.md` 的目录导航
- **范围外**
  - 直接改造第三方 AngelScript 实现
  - 图形截图测试与 GPU 依赖回归
  - 与覆盖补齐无关的大规模宿主项目脚本整理
- **边界约束**
  - 新增测试必须先选层，再选目录；不允许再发明新的泛化桶。
  - `Native/` 只验证公共 API；内部类、`source/as_*.h` 相关测试仍放 `Internals/`。
  - `Template/` 只保留模板或模板自测；长期业务回归应迁回真实主题目录。

## 当前事实状态快照

1. `Delegate/AngelscriptDelegateScenarioTests.cpp` 只覆盖单播/多播的最小 happy path。
2. `GC/AngelscriptGCScenarioTests.cpp` 只覆盖 actor destroy、component destroy、world teardown 三条基本回收路径。
3. `Blueprint/AngelscriptBlueprintSubclassRuntimeTests.cpp` 主要覆盖继承 BeginPlay/Tick、基础调用链与状态隔离，仍缺 editor-facing、asset-facing、复杂 override 组合边界。
4. `Template/` 现有三个真实自动化测试已在跑，但命名未进入 `Angelscript.TestModule.*` 体系。
5. 现有历史计划 `Plan_AngelscriptUnitTestExpansion.md` 与 `Plan_AngelscriptTestScenarioExpansion.md` 已经定义了大方向，本计划聚焦“根据最新审计结果先补什么、按什么顺序补”。

## 分阶段执行计划

### Phase 1：补最薄的 scenario 风险桶

> 目标：先把目前最薄的 Delegate / GC / Component / Blueprint 风险面补到不再只有 happy path。

- [ ] **P1.1** 扩展 `Delegate/`，从“能绑上”补到“复杂使用仍安全”
  - 在 `Plugins/Angelscript/Source/AngelscriptTest/Delegate/` 增加 dynamic delegate、解绑后不触发、重复绑定、参数转发、广播顺序等场景。
  - 至少覆盖一条 script -> C++ -> script 往返链，避免只验证单向调用。
  - 若发现部分用例更适合作为 `Bindings/` 快速回归，应同步拆层，不要硬塞进 `Delegate/`。
- [ ] **P1.1** 📦 Git 提交：`[Test/Coverage] Test: expand delegate scenario coverage`

- [ ] **P1.2** 扩展 `GC/`，补循环引用与压力边界
  - 在 `Plugins/Angelscript/Source/AngelscriptTest/GC/` 增加 actor-component 双向引用、脚本对象链、重复 spawn/destroy、多轮 collect 后状态不泄漏等场景。
  - 引入明确的弱引用/可观察指标，避免只靠“没崩”判断 GC 正常。
- [ ] **P1.2** 📦 Git 提交：`[Test/Coverage] Test: add gc stress and cycle scenarios`

- [ ] **P1.3** 扩展 `Component/` 与 `Blueprint/` 的复杂交互边界
  - 在 `Component/` 中补组件注册顺序、默认组件与动态组件共存、脚本组件销毁后宿主状态恢复等用例。
  - 在 `Blueprint/` 中补 blueprint event override 链、BP-only 默认值、重复重建后 state reset、script parent 与 BP child 的更复杂 override 组合。
- [ ] **P1.3** 📦 Git 提交：`[Test/Coverage] Test: deepen component and blueprint interaction cases`

### Phase 2：建立缺失的 Editor unit 与 Runtime unit 分层

> 目标：把现在还挤在 `AngelscriptTest` 里的 editor/runtime 责任拆出真正的 unit 层，减少对世界和脚本场景的过度依赖。

- [ ] **P2.1** 在 `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/` 建立 Editor unit 起步集
  - 至少从 source navigation、class reload helper、content browser data source 三类中选 1~2 个作为首批落点。
  - 重点覆盖纯 editor helper 的输入/输出与路径归一化，不让所有 editor 问题都只能靠 `AngelscriptTest/Editor/` 场景回归发现。
- [ ] **P2.1** 📦 Git 提交：`[Test/Coverage] Feat: bootstrap editor unit test layer`

- [ ] **P2.2** 扩展 `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/` 的纯 runtime 单元边界
  - 优先补 `save/load`、`exception handling`、`debugging protocol`、`bind database`、`precompiled data`、`fake net driver` 一类不依赖 `UWorld` 的纯 C++ 边界。
  - 这些测试要尽量避免通过 `AngelscriptTest` 场景层绕道验证，直接在 runtime unit 层给出最小可信基线。
- [ ] **P2.2** 📦 Git 提交：`[Test/Coverage] Test: expand runtime unit coverage for engine-core gaps`

### Phase 3：规范 `Examples/` 与 `Template/` 的职责边界

> 目标：防止后续新增测试继续落进语义不清的目录，形成第二轮结构漂移。

- [ ] **P3.1** 明确 `Examples/` 是“示例回归”还是“泛化集成测试”
  - 若保留为示例回归，就固定命名隔离规则、模块命名规则和适用范围，只验证示例资产仍可编译/运行。
  - 若其中某些文件本质上是在验证具体主题能力，应迁回 `Actor/`、`Delegate/`、`Bindings/` 等真实目录。
  - 这一阶段优先处理最容易与宿主脚本碰撞的例子，不必一次迁完全部示例。
- [ ] **P3.1** 📦 Git 提交：`[Test/Coverage] Chore: clarify examples versus theme regression scope`

- [ ] **P3.2** 明确 `Template/` 的长期策略
  - 现有三个模板测试已经被当成真实自动化项运行，需决定是否统一迁入 `Angelscript.TestModule.Template.*` 前缀，或明确继续作为单独前缀族存在。
  - 同时限定：新增模板只验证脚手架本身，稳定业务回归应复制后迁回目标主题目录。
- [ ] **P3.2** 📦 Git 提交：`[Test/Coverage] Chore: lock template naming and ownership policy`

### Phase 4：补高级能力面 backlog

> 目标：在薄桶补齐后，为下一轮更深的能力验证建立明确 backlog，而不是继续凭记忆拾遗。

- [ ] **P4.1** 建立 advanced backlog 清单并排序
  - 形成至少以下主题的目录/计划候选：`Networking`、`SaveLoad`、`Debugging`、`StructEnum`、`AssetEditorIntegration`、`PerformanceOptional`。
  - 每个主题都写清推荐落层（Runtime Unit / Editor Unit / Scenario）与首批 2~3 个最小 case。
  - 如果某些主题更适合继续写成单独计划文档，也要在这里明确依赖顺序，避免与已有计划重叠。
- [ ] **P4.1** 📦 Git 提交：`[Test/Coverage] Docs: define advanced angelscript test backlog`

## 验收标准

1. `Delegate`、`GC`、`Component`、`Blueprint` 四个当前薄桶至少都有一轮明确的补测路线和首批 case。
2. `AngelscriptEditor/Private/Tests/` 不再为空，Editor unit 层开始承担 editor helper 回归。
3. `AngelscriptRuntime/Tests/` 新增一批不依赖 `UWorld` 的核心单元测试方向，且与 scenario 层职责清晰分离。
4. `Examples/` 与 `Template/` 的命名、职责和迁移规则有明确文档，不再让新增测试继续自由漂移。
5. advanced backlog 至少列出 networking/save-load/debugging 等下一轮关键主题及优先级。

## 风险与注意事项

- 覆盖补齐最容易犯的错是“哪里有空就往哪里塞测试”；必须先尊重现有层级语义，再决定目录。
- `Editor unit` 与 `Runtime unit` 一旦启动，后续就要坚持把对应问题优先收进这些层，不要又退回大而杂的 `AngelscriptTest`。
- `Examples/` 和 `Template/` 的职责不清会持续制造统计漂移、前缀混乱和命名冲突，这一层虽然看起来是“文档问题”，其实直接影响回归质量。
- advanced backlog 不要求一次做完，但必须足够具体，让后来者能据此开新任务，而不是重新做一遍审计。
