# Angelscript 测试基线对齐与红灯恢复计划

## 背景与目标

当前仓库的 Angelscript 测试已经形成较大的自动化资产，但“文档口径”“源码注册口径”“最近真实跑批结果”三者已经明显分离，导致后来者很难判断测试体系到底是否健康。

- `Documents/Guides/TestCatalog.md` 仍写着 `275/275 PASS`，且这份 catalog 只覆盖 `Plugins/Angelscript/Source/AngelscriptTest/` 的历史目录化基线，并不代表仓库当前全部测试层的真实状态。
- 本次源码扫描显示：`Plugins/Angelscript/Source/AngelscriptTest/` 当前约有 `340` 个 automation/spec/CQTest 注册点，`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/` 另有 `31` 个注册点，而 `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/` 仍为空；这是**仓库级注册点口径**。
- `Saved/Logs/TestModuleAll.log` 的最近一次 `Automation RunTests Angelscript.TestModule` 跑批执行了 `333` 个完成项，其中 `326` 成功、`7` 失败，最终退出码为 `-1`；这是**`Angelscript.TestModule` 运行口径**，与仓库级注册点数量不是同一统计维度。

这 7 个红灯不是同一种问题：

- `Angelscript.TestModule.Angelscript.Misc.Any`
- `Angelscript.TestModule.Angelscript.Misc.DuplicateFunction`
- `Angelscript.TestModule.Angelscript.Operators.GetSet`
- `Angelscript.TestModule.Angelscript.NativeScriptHotReload.Phase2A`
- `Angelscript.TestModule.Angelscript.NativeScriptHotReload.Phase2B`
- `Angelscript.TestModule.Editor.SourceNavigation.Functions`
- `Angelscript.TestModule.ScriptExamples.Actor`

初步审计显示，这 7 个红灯里既有“测试预期已过时”的候选问题，也有“引用的脚本文件根本不存在”“路径拼接重复”“示例脚本与项目真实脚本命名冲突”这类已被日志或文件系统直接支持的环境/资产问题。如果不先把基线与红灯对齐，后续继续扩测只会让 `TestCatalog`、计划文档和真实结果漂移得更远。

### 目标

1. 固定一份可信的“当前测试现状快照”，明确历史 catalog 基线、源码注册口径和最近真实运行结果之间的关系。
2. 收敛并修复当前 `Angelscript.TestModule` 下的 7 个已知红灯，恢复一条可复现的全量回归基线。
3. 将基线更新方式、失败桶划分方式、日志证据路径同步回文档，避免后续再用过期数字做决策。

## 范围与边界

- **范围内**
  - `Documents/Guides/TestCatalog.md`
  - `Documents/Guides/Test.md`
  - `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptMiscTests.cpp`
  - `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptOperatorTests.cpp`
  - `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptNativeScriptHotReloadTests.cpp`
  - `Plugins/Angelscript/Source/AngelscriptTest/Editor/AngelscriptSourceNavigationTests.cpp`
  - `Plugins/Angelscript/Source/AngelscriptTest/Examples/AngelscriptScriptExampleTestSupport.cpp`
  - 必要时新增与上述红灯一一对应的辅助 test helper / fixture
- **范围外**
  - 大规模新增覆盖面；新测试扩张放到单独补测计划处理
  - 与红灯无关的运行时功能重构
  - 把插件逻辑回推到 `Source/AngelscriptProject/`
- **边界约束**
  - 文档中的引擎路径和命令模板必须继续引用 `AgentConfig.ini`，不写死本机绝对路径。
  - 任何“红灯恢复”都要优先修根因，不允许通过删除失败断言或跳过测试来伪造绿灯。
  - `Native/` 继续只使用公共 AngelScript API；本计划不把内部头混进 Native Core 层。

## 当前事实状态快照

1. `Saved/Logs/NativeAllFinal.log` 显示 `Angelscript.TestModule.Native` 最近一轮为全绿，退出码 `0`。
2. `Saved/Logs/RuntimeScenarioRegressionFinal.log` 显示选定的 runtime/scenario 子集最近一轮为全绿，退出码 `0`。
3. `Saved/Logs/TestModuleAll.log` 显示 `Angelscript.TestModule` 全量回归最近一轮退出码为 `-1`，存在 7 个失败项。
4. `AngelscriptMiscTests.cpp` 中 `Misc.Any` 与 `Misc.DuplicateFunction` 仍在断言 raw AngelScript 路径上应 `Build()==asSUCCESS`；当前日志表明，这两条 case 在现有 raw path 下分别因 `any` 类型不可用、重复函数声明冲突而失败，后续需要先判定这是“行为边界已变”还是“测试接到了错误层”。
5. `AngelscriptOperatorTests.cpp` 中 `Operators.GetSet` 仍假设 raw accessor 语法可编译；当前日志表明 `AccessorCarrier.value` 并未被识别为属性，但“raw accessor path 本就不支持”与“测试验证层接错”仍需在执行时进一步确认。
6. `AngelscriptNativeScriptHotReloadTests.cpp` 依赖 `Script/Tests/Test_*.as`，而仓库当前并没有 `Script/Tests/` 目录。
7. `AngelscriptSourceNavigationTests.cpp` 的期望路径与日志里的实际路径相比，多出一层 `../../../../AngelscriptProject/Saved/Automation/` 前缀；当前审计将其归为路径归一化/比较口径问题候选，但执行时仍需确认根因在生产代码还是测试断言。
8. `Examples/AngelscriptScriptExampleTestSupport.cpp` 以 `Example_Actor` 作为模块名，而仓库同时存在真实文件 `Script/Example_Actor.as`，导致类名 `AExampleActorType` 冲突。

## 分阶段执行计划

### Phase 1：冻结 live baseline 与失败清单

> 目标：先让“当前到底有多少测试、最近哪一轮失败了什么”成为可复查事实，而不是口头印象。

- [ ] **P1.1** 对齐 `TestCatalog` 中的历史基线与当前 live inventory
  - 修改 `Documents/Guides/TestCatalog.md`，把现有 `275/275 PASS` 明确标注为“已编目历史基线”，避免继续被误读为当前全量状态。
  - 追加“源码注册口径”小节，记录本次扫描得到的 `AngelscriptTest≈340`、`AngelscriptRuntime/Tests≈31`、`AngelscriptEditor/Private/Tests=0` 事实，并写明匹配模式。
  - 同时单列“最近一次 `Automation RunTests Angelscript.TestModule` 运行口径”，记录 `333 completed / 326 success / 7 failed`，避免把 repo-wide inventory、`AngelscriptTest` catalog、`Angelscript.TestModule` 运行结果混成一个数字。
- [ ] **P1.1** 📦 Git 提交：`[Test/Baseline] Docs: separate catalog baseline from live inventory`

- [ ] **P1.2** 在 `Documents/Guides/Test.md` 补一节“当前已知失败桶”导航
  - 记录最近一次全量 `Angelscript.TestModule` 回归的日志来源是 `Saved/Logs/TestModuleAll.log`。
  - 列出 7 个失败测试名，并按“已证实事实”与“初步归因假设”两栏整理，避免后来者把审计推断误读成已定论。
  - 明确后续任何人更新 baseline 前，都必须先重新跑一次对应 bucket，并把新日志位置写回 docs。
- [ ] **P1.2** 📦 Git 提交：`[Test/Baseline] Docs: capture current failing buckets and evidence paths`

### Phase 2：修复 raw AngelScript 预期漂移的三条红灯

> 目标：先解决最明显的“测试预期不再符合当前分支行为”的问题，避免把行为变化长期伪装成红灯。

- [ ] **P2.1** 重新定义 `Misc.Any` 的断言边界
  - 修改 `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptMiscTests.cpp` 中的 `FAngelscriptMiscAnyTest`。
  - 当前日志表明 raw path 下 `any` 已经不是可用数据类型；需要先确认这是本分支刻意不支持，还是测试本来就接错了运行路径。
  - 若确认当前分支确实不支持 raw `any`，则把测试改成“明确验证报错信息与边界”的负例，而不是继续断言 `Build()==asSUCCESS`。
  - 若确认应当支持，则在单独 bugfix 中恢复实现，本计划只负责先把测试语义调回真实边界。
- [ ] **P2.1** 📦 Git 提交：`[Test/Baseline] Test: align Misc.Any with current raw-engine boundary`

- [ ] **P2.2** 重新定义 `Misc.DuplicateFunction` 的断言边界
  - 同样修改 `AngelscriptMiscTests.cpp` 中的 `FAngelscriptMiscDuplicateFunctionTest`。
  - 当前 raw path 已经报出“同签名函数重复定义”错误；如果这正是当前引擎行为，就应该把测试翻成负例，断言报错稳定且可读。
  - 与 `Misc.Any` 一样，不允许继续保留“明知行为变化却坚持成功断言”的 stale expectation。
- [ ] **P2.2** 📦 Git 提交：`[Test/Baseline] Test: align duplicate-function raw expectation`

- [ ] **P2.3** 调整 `Operators.GetSet` 的断言与落层
  - 修改 `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptOperatorTests.cpp` 中的 `FAngelscriptOperatorGetSetTest`。
  - 先明确：当前 raw accessor path 是否根本不支持 `get_` / `set_` 属性糖；若是，就把这条测试改成负例并记录原因。
  - 如果仓库真正想验证的是 runtime integration 层而不是 raw engine path，应把脚本接到正确 helper 上，避免把“层级接错”伪装成行为回归。
- [ ] **P2.3** 📦 Git 提交：`[Test/Baseline] Test: realign operator getset coverage to valid layer`

### Phase 3：修复资产路径与命名冲突导致的四条红灯

> 目标：恢复所有依赖仓内脚本资产或生成路径的测试，使其不再依赖已经缺失的文件布局或会与宿主项目互撞的命名。

- [ ] **P3.1** 为 NativeScriptHotReload 两个阶段恢复可用输入源
  - 修改 `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptNativeScriptHotReloadTests.cpp`。
  - 当前失败根因不是热重载逻辑本身，而是测试引用了并不存在的 `Script/Tests/Test_*.as` 文件。
  - 需要在“补回测试资产”与“把测试改成从内存/稳定 fixture 读脚本”之间做一个明确选择；优先选择仓内稳定、不会被宿主项目内容布局破坏的方案。
  - 修完后，`Phase2A/Phase2B` 必须继续验证 `FullReload -> SoftReloadOnly` 的 handled reload 语义，而不是退化成单纯“文件能读到”的 smoke。
- [ ] **P3.1** 📦 Git 提交：`[Test/Baseline] Fix: restore native script hot reload test inputs`

- [ ] **P3.2** 修复 SourceNavigation 路径重复拼接
  - 修改 `Plugins/Angelscript/Source/AngelscriptTest/Editor/AngelscriptSourceNavigationTests.cpp`。
  - 当前日志显示生成函数的 source path 多出一层 `Saved/Automation` 前缀，属于路径归一化/比较口径问题。
  - 需要决定“生产代码修规范化”还是“测试端对比 normalized path”；优先修根因，使其他导航用例也能复用同一规则。
- [ ] **P3.2** 📦 Git 提交：`[Test/Baseline] Fix: normalize generated source-navigation paths`

- [ ] **P3.3** 隔离 `ScriptExamples.Actor` 与宿主项目真实脚本的命名碰撞
  - 修改 `Plugins/Angelscript/Source/AngelscriptTest/Examples/AngelscriptScriptExampleTestSupport.cpp` 及必要的 example test 数据。
  - 当前 `Example_Actor` 用例通过模块名 `Example_Actor` 编译，而宿主项目下已经存在 `Script/Example_Actor.as`，引发类名重复。
  - 需要把 example regression 的模块命名、虚拟文件名或隔离策略设计成“绝不与项目真实脚本共名”的形式，且不只修 `Actor` 一个例子。
  - 如发现整个 `Examples/` 目录都潜在受此问题影响，应顺手把统一命名规则补进 helper，而不是逐个文件打补丁。
- [ ] **P3.3** 📦 Git 提交：`[Test/Baseline] Fix: isolate script example modules from project scripts`

- [ ] **P3.4** 重新跑 `Angelscript.TestModule` 全量回归并更新证据
  - 使用 `AgentConfig.ini` 解析出的 `EngineRoot` / `ProjectFile` 重新执行 `Automation RunTests Angelscript.TestModule`。
  - 必须把日志固定到新的独立 `-ABSLOG`，并在必要时配合 `-ReportExportPath` 导出结果；旧日志只作为历史证据，不作为最终验收。
  - 如果仍有红灯，必须把剩余失败留在计划中继续跟踪，不能因为“主要问题修完了”就提前收尾。
- [ ] **P3.4** 📦 Git 提交：`[Test/Baseline] Test: refresh full testmodule baseline after red fixes`

### Phase 4：把恢复后的基线同步回 catalog 与维护入口

> 目标：让这次恢复工作真正进入长期维护闭环，而不是留下一份只有当事人看得懂的抢修记录。

- [ ] **P4.1** 更新 `Documents/Guides/TestCatalog.md` 的现行状态说明
  - 在不把整份计划全文复制进文档的前提下，把“当前 live inventory”“最近一轮全量状态”“日志入口”写成可导航内容。
  - 若 `Angelscript.TestModule` 已恢复全绿，要写明对应日志和日期；若仍保留少量已知红灯，也要明确列出，不允许继续写笼统 PASS 口径。
- [ ] **P4.1** 📦 Git 提交：`[Test/Baseline] Docs: publish refreshed baseline status`

## 验收标准

1. `Documents/Guides/TestCatalog.md` 不再把 `275/275 PASS` 表述成当前全量真实状态。
2. `Saved/Logs/TestModuleAll.log` 对应的 7 个失败测试都有明确根因和处理方案，不再停留在“看到红灯但没人解释”。
3. `Misc.Any`、`Misc.DuplicateFunction`、`Operators.GetSet` 不再用过时的成功断言去验证当前不成立的 raw path 行为。
4. `NativeScriptHotReload.Phase2A/B`、`Editor.SourceNavigation.Functions`、`ScriptExamples.Actor` 的根因被修复，而不是通过跳过测试绕开。
5. 至少一轮新的 `Automation RunTests Angelscript.TestModule` 结果被记录并可从文档导航到对应日志/报告。

## 风险与注意事项

- 最大风险是把“行为变了”误当成“实现坏了”，或者反过来把真实回归误包装成“测试过时”；每条红灯都必须先判定边界语义，再决定修测试还是修实现。
- `Examples/` 与宿主项目 `Script/` 的命名隔离如果处理不好，后续会不断冒出同类冲突，不应只修 `Example_Actor` 一个文件名。
- `NativeScriptHotReload` 若直接重新引入散落在宿主项目下的脚本资产，会再次弱化插件测试的可移植性；优先把 fixture 收敛到插件控制范围内。
- 更新 baseline 文档时必须附带日期与证据来源，否则几周后又会重新失真。
