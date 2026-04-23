# AS 测试角度扩展与模板落地计划

## 背景与目标

### 背景

当前仓库已经有一套规模不小的 Angelscript 自动化测试基线：`Documents/Guides/TestCatalog.md` 记录了 `Plugins/Angelscript/Source/AngelscriptTest/` 下 `275/275 PASS` 的目录化清单，`Documents/Guides/Test.md` 也已经把 `NullRHI`、`Gauntlet`、测试组、Automation Spec 等入口整理出来。但从“继续稳定扩测、让后来者直接照模板落地”的角度看，仍有三个明显缺口：

1. **测试角度和测试层没有被统一抽象成一份矩阵**
   - 现在能看到很多主题测试和不少成熟目录，但“哪些角度应优先补、该落在哪一层、该用哪种模板写”仍主要靠经验记忆。
   - 尤其是 discovery / 参数化 / standalone-vs-client 模式 / world context / latent wait / hot reload / report export / Gauntlet outer smoke 这些跨目录能力，没有被整理成统一导航。
2. **已有计划偏主题化，缺少“模板化扩测”总控文档**
   - `Documents/Plans/Plan_ASInternalClassUnitTests.md` 解决的是 internal class 深挖问题。
   - `Documents/Plans/Plan_AngelscriptUnitTestExpansion.md` 解决的是分层、分组和大盘补测策略。
   - 但还缺一份专门面向“更多测试角度 / 测试情景 / 复用模板”的计划，用于指导后续按同一种写法持续扩张。
3. **一些最容易反复踩坑的运行时边界还没有被固化为模板优先项**
   - 例如单机与多人模式切换、PIE 多客户端 world 枚举、latency/packet loss 场景、coverage/report export、Gauntlet 作为 outer shell 的使用方式。
   - 这些边界一旦没有统一模板，后续新增测试就很容易出现风格漂移、等待逻辑随意、分层不清的问题。

### 目标

1. 基于现有仓库文档、`knot` 检索结果和外部参考，为 Angelscript 测试补一份**角度矩阵 + 场景库 + 模板库**附件。
2. 明确每类测试该落在哪个目录层级：`Runtime Unit`、`Editor Unit`、`Plugin Fast Tests`、`Scenario / Integration`、`Template`、`Gauntlet outer smoke`。
3. 给出第一波最值得落地的测试情景与模板化文件落点，让后续执行者可以小步提交，而不是重新发明测试结构。
4. 在 `Documents/Guides/TestCatalog.md` 与 `Documents/Guides/Test.md` 之间补齐“怎么补测、先补什么、模板在哪里”的中间层文档。

## 范围与边界

- **范围内**
  - `Documents/Guides/Test.md`
  - `Documents/Guides/TestCatalog.md`
  - `Documents/Plans/Plan_AngelscriptUnitTestExpansion.md`
  - `Documents/Plans/Plan_ASInternalClassUnitTests.md`
  - `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/`
  - `Plugins/Angelscript/Source/AngelscriptEditor/Tests/`
  - `Plugins/Angelscript/Source/AngelscriptTest/`
- **范围外**
  - 不默认修改 `ThirdParty/` 或 `Reference/` 下的运行时实现
  - 不默认新增 GPU 依赖的视觉回归大盘；截图测试只作为可选角度保留在附件中
  - 不把插件逻辑回推到 `Source/AngelscriptProject/`
  - 不顺手重构无关的历史测试目录
- **边界约束**
  - 测试分层以 `Documents/Guides/Test.md` 为准，新增测试先定层，再定目录。
  - `Plugins/Angelscript/Source/AngelscriptTest/AngelScriptSDK/` 继续只使用公共 API；内部类或 `source/as_*.h` 不得混入这个目录。
  - `Template/` 目录只放“可复制的脚手架示例”或少量模板验证，不重新变成泛化场景桶。
  - 路径和命令一律使用工程相对路径或 `AgentConfig.ini` 约定，不写死本机绝对路径。

## 当前事实状态快照

1. `Documents/Guides/TestCatalog.md` 已将测试按 `Shared / Core / Angelscript / Bindings / HotReload / AngelScriptSDK / Compiler / Preprocessor / ClassGenerator / FileSystem / Editor / Themed Integration Tests / Template` 分类。
2. `Documents/Guides/Test.md` 已明确：
   - `NullRHI` 是当前非图形测试主路径。
   - `Gauntlet` 适合作为 outer shell 执行 `UE.EditorAutomation`、`UE.TargetAutomation`、`UE.Networking` 等。
   - `Automation Spec`、`IMPLEMENT_SIMPLE_AUTOMATION_TEST`、`IMPLEMENT_COMPLEX_AUTOMATION_TEST` 都是仓库允许且已文档化的写法。
3. 本次检索补充了几条对模板化很关键的外部信号：
   - `Automation Spec` 适合承接 `BeforeEach` / `AfterEach` / Async / latent completion / 参数化。
   - 多客户端世界枚举、等待连接完成、区分 server/client 角色的写法已有现成参考。
   - 覆盖率、network emulation、hot reload batching 都是值得优先模板化的操作面。
4. 当前仓库已有“按主题拆目录”的强约束，因此新增测试模板不能再发明一个新的总桶，必须挂在现有层级语义下面。

## 计划附件

- 测试角度矩阵、场景库、模板骨架统一写入：`Documents/Plans/Plan_AngelscriptTestScenarioExpansion_Appendix.md`
- 执行本计划时，附件应被视为“模板和优先级的唯一来源”，避免同一类测试出现第二种 house style。

## 分阶段执行计划

### Phase 0：冻结角度矩阵、模板边界与首批落点

> 目标：先把“哪些角度值得补、该用什么模板、该落在哪个目录”写死，避免执行时重新讨论。

- [ ] **P0.1** 在 `Documents/Plans/Plan_AngelscriptTestScenarioExpansion_Appendix.md` 固化测试角度矩阵
  - 这一步先不写任何新测试文件，只整理统一视图：discovery / 参数化 / runtime mode / world context / latent / multiplayer / hot reload / report export / Gauntlet / screenshot/perf optional。
  - 每个角度必须标注推荐测试层和推荐目录，避免后续执行者把 world 场景塞进 `Runtime Unit`，或把纯 helper case 塞进 `Scenario / Integration`。
  - 角度矩阵至少要覆盖“为什么值得测、最小首批 case、建议模板编号”这三类信息，让执行时不需要再反查外部资料。
- [ ] **P0.1** 📦 Git 提交：`[Docs/Test] Plan: freeze AS test angle matrix and appendix`

- [ ] **P0.2** 固定模板目录规则
  - 明确 `Template/` 目录只承接模板脚手架或模板自测，不承接长期膨胀的泛化场景用例。
  - 真实要长期保留的 case，仍按 `Shared / Core / Bindings / HotReload / AngelScriptSDK / Actor / Blueprint / Component / Delegate / GC / Interface / Inheritance / Subsystem` 等具体主题目录落位。
  - 这一步的意义是让“模板”与“主题化真实测试”分开，后续复制模板时不把脚手架当成最终落点。
- [ ] **P0.2** 📦 Git 提交：`[Docs/Test] Plan: freeze template-vs-theme directory policy`

- [ ] **P0.3** 定义第一波 12 个高性价比情景
  - 从附件中选出最值得先做的 12 个 case，优先覆盖 naming/discovery、复杂参数化、standalone-vs-client、world 枚举、latent timeout、coverage/report、Gauntlet outer smoke。
  - 首批不追求全面，只追求“覆盖最容易反复踩坑的边界且可独立提交”。
  - 这一步完成后，后续每个 Phase 都只围绕这 12 个 case 分批展开，不再临场扩 scope。
- [ ] **P0.3** 📦 Git 提交：`[Docs/Test] Plan: freeze first-wave AS scenario shortlist`

### Phase 1：落地基础模板与最小自测

> 目标：先建立一组所有人都能复制的模板骨架，确保后续扩测不会出现格式漂移。

- [ ] **P1.1** 在 `Plugins/Angelscript/Source/AngelscriptTest/Template/` 建立“简单单元测试模板”
  - 新建一个最小模板文件，演示 `IMPLEMENT_SIMPLE_AUTOMATION_TEST` 或仓库当前常用轻量写法如何组织 Arrange / Act / Assert，并给出命名、分组、断言消息风格。
  - 模板只验证最小 helper 或最小脚本执行路径，不携带复杂 world 依赖。
  - 目标不是做功能覆盖，而是形成一个“复制后只改脚本和断言即可”的标准骨架。
- [ ] **P1.1** 📦 Git 提交：`[Test/Template] Feat: add simple AS test template scaffold`

- [ ] **P1.2** 在 `Plugins/Angelscript/Source/AngelscriptTest/Template/` 建立“复杂参数化模板”
  - 用一份最小 case 演示 `Complex` 风格或数据驱动风格的写法，固定测试数据来源、case 命名、失败信息和执行分组方式。
  - 模板必须明确“哪些字段是 case 数据、哪些字段是环境准备”，避免后续复杂用例把输入数据和测试 fixture 混在一起。
  - 完成后，附件里的 `Complex` 模板与源码脚手架应能一一对应。
- [ ] **P1.2** 📦 Git 提交：`[Test/Template] Feat: add parameterized AS test template scaffold`

- [ ] **P1.3** 为模板目录补一个最小自测分组
  - 让 `Template` 下至少有 1~2 个能独立跑通的 smoke case，用于验证模板本身不会因为后续重构而过期。
  - 这组测试应足够轻量，可以进入未来的 docs 示例或 smoke 候选，但不要直接把模板目录变成大而全的真实回归目录。
- [ ] **P1.3** 📦 Git 提交：`[Test/Template] Test: add template self-check automation group`

### Phase 2：落地 runtime mode、world 与多客户端模板

> 目标：把最容易反复踩坑的 standalone / client-server / world context / latent wait 场景做成可复制模板。

- [ ] **P2.1** 在 `Plugins/Angelscript/Source/AngelscriptTest/Template/` 建立“单机 UI / 本地控制器模板”
  - 模板聚焦 `PIE_Standalone` 路径，固定获取本地 `PlayerController`、创建 widget、验证 viewport 或本地权限的最小写法。
  - 这样后续任何需要验证单机 UI、本地 controller、非网络权限逻辑的 case，都可以直接复制而不是重新摸索 mode 配置。
  - 附件需同步说明它与 `bUseServerClientModel=false` 的关系，以及为什么这类 case 不该默认落在 client-server 模式。
- [ ] **P2.1** 📦 Git 提交：`[Test/Template] Feat: add standalone UI scenario template`

- [ ] **P2.2** 建立“多人 world 枚举 / server-client 分流模板”
  - 模板演示如何枚举 `WorldContext`、如何区分 server/client、如何在 server 侧与 client 侧写不同断言。
  - 这类模板优先落在 `Template/`，真实长期回归再按主题复制到 `Actor/`、`Component/`、`Subsystem/` 等目录。
  - 要明确规定：server/client 断言应分支清晰，不把两种语义硬塞在一条巨型脚本里。
- [ ] **P2.2** 📦 Git 提交：`[Test/Template] Feat: add multiplayer world enumeration template`

- [ ] **P2.3** 建立“latent wait / timeout 模板”
  - 用一个最小 `wait until condition or timeout` 模板替代随意 sleep，固定 observable condition、timeout、错误信息格式。
  - 该模板后续可被复制到多客户端连接、复制完成、地图切换稳定等需要等待的场景。
  - 完成后，附件中的 latent 模板和源码中的 helper 必须一一对应。
- [ ] **P2.3** 📦 Git 提交：`[Test/Template] Feat: add latent wait and timeout template`

### Phase 3：把模板复制到真实主题目录，形成第一波正式回归

> 目标：不是只留模板，而是把模板复制成一批真正有业务价值的主题测试。

- [ ] **P3.1** 先选 `Shared` / `Core` / `HotReload` 三类目录落第一波真实 case
  - `Shared` 适合 discovery、helper、自带引擎上下文边界。
  - `Core` 适合最小执行、配置、report/export、基础脚本运行面。
  - `HotReload` 适合 batching、module cap、reload 后 smoke 回归。
  - 这样可以先避开复杂 Actor/Blueprint 上下文，把模板价值快速转成正式覆盖。
- [ ] **P3.1** 📦 Git 提交：`[Test] Plan: map first-wave real scenarios to shared core htreload`

- [ ] **P3.2** 在真实主题目录各落至少一个模板衍生 case
  - 至少要有：一个 naming/discovery case、一个 parameterized case、一个 runtime mode case、一个 latent timeout case、一个 hot reload / operational case。
  - 这一步的关键不在于 case 数量，而在于验证模板复制到真实目录后不会出现依赖层级错位。
  - 如发现某类模板复制后天然不适合当前主题目录，应立即回写附件，修正推荐落点，而不是继续错误扩散。
- [ ] **P3.2** 📦 Git 提交：`[Test] Feat: land first-wave real scenarios from templates`

- [ ] **P3.3** 为第一波真实 case 建立稳定的 Automation 路径前缀
  - 新增用例必须有清晰的路径前缀，避免继续把 case 塞回含糊的 `Scenario.*` 风格。
  - 路径前缀应服务于后续按目录/主题/能力面筛选执行，而不是只为当前一次运行方便。
- [ ] **P3.3** 📦 Git 提交：`[Test] Chore: normalize first-wave scenario registration paths`

### Phase 4：补齐 operational / report export / Gauntlet outer smoke

> 目标：把“会跑测试”升级成“会稳定运行、会导出、会在 CI/会话层外壳中复用”。

- [ ] **P4.1** 在 `Documents/Guides/Test.md` 同步 unattended / report export 推荐入口
  - 把附件中的最小 unattended 命令、coverage/report export 推荐开关、恢复运行方式整理进 `Test.md`，避免它们只存在于计划附件里。
  - 只补与本计划直接相关的入口，不顺手重写整份测试指南。
- [ ] **P4.1** 📦 Git 提交：`[Docs/Test] Docs: sync unattended and report-export guidance`

- [ ] **P4.2** 为 `Gauntlet` 增加最小 outer smoke 方案
  - 这里不追求把所有逻辑都搬进 Gauntlet，只固定最小壳：启动 `UE.EditorAutomation` 或 `UE.Networking`，运行一个 Angelscript smoke bucket，采集 pass/fail/crash/timeout。
  - 这个 outer smoke 的意义是给未来 server+client 或 editor boot 回归提供最外层会话壳，而不是替代内层自动化断言。
  - 如果当前仓库还没有专用 Gauntlet test script，本阶段至少要把方案、角色数和命令模板固化在 docs 中。
- [ ] **P4.2** 📦 Git 提交：`[Docs/Test] Plan: define minimal Gauntlet outer smoke path`

- [ ] **P4.3** 把 coverage、latency/loss、resume 等操作面纳入附件速查区
  - 这一步是为了让执行者能直接复制 operational case，不需要回头在不同文档里翻找零散命令参数。
  - 如果这些操作面最终没有立刻转成源码测试，至少也要被清晰收录为“已选中角度，后续按模板补齐”。
- [ ] **P4.3** 📦 Git 提交：`[Docs/Test] Docs: add AS operational scenario quick reference`

### Phase 5：同步目录总表与验收基线

> 目标：让这份计划不是一次性草稿，而是能进入长期维护闭环。

- [ ] **P5.1** 更新 `Documents/Guides/TestCatalog.md` 的模板与首波案例导航
  - 对新增模板目录和第一波真实 case，在 `TestCatalog.md` 中补充条目或新增一小段“模板/场景扩测导航”，说明这些 case 的验证内容。
  - 不要求把附件全文复制进 `TestCatalog`，但至少要让后来者知道模板目录和首波真实案例在哪里。
- [ ] **P5.1** 📦 Git 提交：`[Docs/Test] Docs: catalog AS template and scenario expansion entries`

- [ ] **P5.2** 跑首波最小回归并记录通过口径
  - 先跑模板自测，再跑第一波真实主题 case，最后再跑一个 unattended 或 Gauntlet outer smoke 路径。
  - 任何失败都先确认是模板假设过窄还是实际能力回归，不能为了“让计划显得完成”而删掉失败断言。
  - 这一步完成后，计划才能从“准备落地”进入“可按批执行”的状态。
- [ ] **P5.2** 📦 Git 提交：`[Docs/Test] Test: verify first-wave AS scenario expansion baseline`

## 验收标准

1. `Documents/Plans/Plan_AngelscriptTestScenarioExpansion_Appendix.md` 中至少包含一份角度矩阵、一份场景库和一组可复制模板。
2. `Template/` 目录的用途被明确限制为模板脚手架，不重新变成泛化场景桶。
3. 第一波至少选定 12 个高性价比情景，并给出推荐落点与推荐模板。
4. 至少定义一种 unattended 运行路径和一种 Gauntlet outer smoke 路径。
5. `TestCatalog.md` 或 `Test.md` 至少有一处能导航到本计划产出的模板与首波案例。

## 风险与注意事项

- 最大风险不是“少补几个 case”，而是补出第二套风格和第二套分层；因此附件必须成为唯一模板来源。
- world / multi-client / latent 场景天然更容易 flaky，必须优先统一等待条件和超时风格，禁止任意 sleep。
- `Template/` 目录不能代替真实主题目录；模板一旦复制成稳定回归，应迁回具体主题目录继续维护。
- `Gauntlet` 只做 outer shell，不要把内层业务断言全部塞进去，否则调试成本会陡增。
- 如果执行过程中发现现有目录比附件推荐落点更合理，应优先回写计划和附件，而不是硬按旧判断推进。

