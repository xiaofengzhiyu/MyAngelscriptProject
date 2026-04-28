# 全能 Script 覆盖案例专项执行包

## 背景与目标

当前仓库已经有 `Script/Example_Actor.as` 与首波 `Coverage` 资产，但它们仍然停留在“分散的小样本”阶段：

- `Script/Example_Actor.as` 主要展示 `UPROPERTY`、`UFUNCTION`、`BlueprintOverride`、`BlueprintEvent` 与少量 `default` 语句，适合作为入门例子，但无法承担“全能力总览”职责。
- `Documents/Plans/Plan_ScriptExamplesExpansion/Coverage/` 下的四个真实 `.as` 资产已经把 `Actor`、`Component`、`UObject` 与属性说明符做成了 file-backed 验证基线，但覆盖面仍是分片式的，不足以回答“当前分支到底支持哪些声明组合、函数组合和反射标记”。
- 多个函数说明符、委托/事件、接口、构造脚本、计时器、格式化字符串等能力当前仍主要存在于 `Plugins/Angelscript/Source/AngelscriptTest/Examples/` 的内联脚本测试中，用户侧看不到一份统一的真实 `.as` 资产。
- 外部参考 `ASTestActor.as` 已经证明“单个超大脚本文件”可以承担类型矩阵、属性访问/函数调用双轨基准、CSV 输出与循环压测的职责，但它本身对属性说明符、函数说明符、组件层级、接口/委托等反射表面覆盖仍不完整。

本专项执行包的目标是：在 `Plan_ScriptExamplesExpansion` 主线下新增一份**双用途的超大单文件 AngelScript 资产**，既能作为对外展示的“全能脚本”，也能作为内部回归基线，系统覆盖属性声明排列组合、函数签名排列组合、`UFUNCTION` 标记组合，以及 `ASTestActor.as` 中值得保留的基准测试模式。

## 范围与边界

- **范围内**
  - 设计并落地一份单文件、章节化的巨型 `.as` 覆盖资产，暂定落点为 `Documents/Plans/Plan_ScriptExamplesExpansion/Coverage/Example_Coverage_AllInOne.as`
  - 为该资产补充一份 companion 覆盖矩阵文档，登记“已覆盖 / 暂缓 / 当前分支不应伪装支持”的条目
  - 新增专门的 `Examples` 自动化测试，对编译、反射、运行期行为与基准结果结构做验证
  - 同步 `Script/Examples/README.md`、`Documents/Plans/Plan_ScriptExamplesExpansion/README.md` 与必要的测试目录文档入口
- **范围外**
  - 借实现 mega example 顺手扩 Runtime 新能力
  - 把当前未闭环能力包装成正向示例，尤其是 `UScriptWorldSubsystem` / `UScriptGameInstanceSubsystem`
  - 用一次性性能数字作为自动化断言结果；自动化只验证基准框架和结果结构，不锁死机器相关耗时
  - 首波就把 GAS / Editor / 全量 Network parity 全塞进单文件，导致脚本不可读、不可维护
- **执行边界**
  - 本波次的真实资产仍以 `Documents/Plans/Plan_ScriptExamplesExpansion/Coverage/` 为 source-of-truth；是否提升到 `Script/Examples/`，放到基线稳定后的后续步骤处理
  - 该文件虽然是“单文件巨型脚本”，但内部必须按主题章节、命名前缀与 helper 边界强制分区，避免变成不可测试的文本堆栈
  - 所有覆盖项必须先看当前仓库已有测试或真实验证证据，再决定是否进入“已支持矩阵”；不能把理想能力写成现状

## 当前不足快照

1. **公开示例缺少统一总入口。**
   - 当前仓库对外可直观看到的真实示例主要仍是 `Script/Example_Actor.as` 与首波四个 coverage 文件，缺少一份“单文件看全局”的案例。

2. **属性覆盖存在，但函数说明符与签名矩阵仍碎片化。**
   - 属性说明符已有真实资产：`Example_Coverage_PropertySpecifiers.as`。
   - 但 `BlueprintPure`、`NotBlueprintCallable`、`CallInEditor`、`BlueprintEvent + Category` 等函数说明符目前主要还停留在 `AngelscriptScriptExampleFunctionSpecifiersTest.cpp` 的内联脚本中。

3. **当前示例缺少 `ASTestActor.as` 那种“声明 + 运行时基准”同文件合流能力。**
   - 现有示例偏展示，外部参考偏性能矩阵；仓库里还没有把两者合并成一个可读、可验证的真实资产。

4. **反射强项没有被集中体现。**
   - `delegate` / `event`、`UINTERFACE`、组件默认层级、Blueprint 子类化友好表面等能力已经在测试中有证据，但缺少同一份脚本内的串联示例。

5. **能力边界容易被误写。**
   - `WorldSubsystem` / `GameInstanceSubsystem` 目前仍是负例证据，首版 giant example 必须显式排除其正向演示，避免制造“看起来支持”的假信号。

## 影响范围

本波次涉及以下操作（按需组合）：

- **全能脚本资产落盘**：新增单文件巨型 `.as` 资产，作为本波次真正的 source-of-truth。
- **覆盖矩阵文档补充**：为属性 / 函数 / 运行期基准 / 已知排除项建立 companion 矩阵，防止脚本正文与验证目标脱节。
- **专门测试新增**：新增 file-backed `Examples` 自动化测试文件，验证编译、反射、运行期行为和基准结果结构。
- **示例入口同步**：把 mega example 的定位、约束和验证入口同步写回示例 README 与 companion README。

按目录分组的文件清单：

- `Documents/Plans/Plan_ScriptExamplesExpansion/Coverage/Example_Coverage_AllInOne.as` — 新建，全能脚本主体
- `Documents/Plans/Plan_ScriptExamplesExpansion/AllInOneCoverageMatrix.md` — 新建，覆盖矩阵与排除项登记
- `Plugins/Angelscript/Source/AngelscriptTest/Examples/AngelscriptScriptExampleAllInOneCoverageTests.cpp` — 新建，专门验证 file-backed giant example
- `Documents/Plans/Plan_ScriptExamplesExpansion/README.md` — 修改，登记本专项执行包与新资产入口
- `Script/Examples/README.md` — 修改，登记未来正式公开入口与当前波次 source-of-truth
- `Documents/Guides/TestCatalog.md` — 按需修改，补充 `Angelscript.TestModule.ScriptExamples.AllInOneCoverage.*` 条目

## 建议脚本结构

这份全能脚本虽然保持“单文件”，但内部应强制切成以下章节：

1. **类型与声明前置区**
   - `UENUM` / `USTRUCT` / `UINTERFACE` / `delegate` / `event` / helper `UObject` / helper `Component` / helper `Actor`。
   - 所有 supporting types 都以统一前缀（如 `CoverageAllInOne`）命名，避免污染后续测试查找。

2. **属性矩阵区**
   - 以“说明符维度 × 类型维度”组织，而不是随手罗列字段。
   - 至少覆盖：基础标量、字符串、`FName`、`FText`、`FVector`、枚举、结构体、对象引用、类引用、`TArray`、`TSet`、`TMap`。
   - 同时覆盖 `DefaultComponent` / `RootComponent` / `Attach`、`Category`、`BlueprintReadOnly`、`EditDefaultsOnly`、`NotEditable`、`EditConst`、常用 `meta` 标签，并明确哪些说明符只登记不在首版正向验证。

3. **函数与 `UFUNCTION` 矩阵区**
   - 以“调用形态 × 反射说明符 × 参数签名”组织。
   - 至少覆盖：无参、单参、多参、返回值、`&out`、对象参数、结构体参数、容器参数、静态调用、成员调用、脚本私有 helper。
   - `UFUNCTION` 首波应覆盖：裸 `UFUNCTION()`、`BlueprintPure`、`BlueprintEvent`、`BlueprintOverride`、`Category`、`NotBlueprintCallable`、`CallInEditor`，并保留组合案例。

4. **运行期行为区**
   - 覆盖 `BeginPlay`、`Tick`、`EndPlay`、组件 owner 访问、delegate bind/broadcast、interface dispatch、默认组件层级、基础 replication default 与标签/默认值透传。

5. **`ASTestActor` 基准区**
   - 引入 `StartTest` / `EndTest`、`Names` / `Times` 记录、循环压测、CSV 字符串拼装和结果保存入口。
   - 重点保留“属性直接读写 vs getter/setter 调用”、“静态函数 vs 成员函数”、“数组 / Map 元素访问”、“常见类型 get/set”这些高价值基准维度。
   - 不把具体机器耗时当成断言目标，而是验证结果行完整性、列名稳定性和样本生成逻辑可运行。

6. **已知限制与降级区**
   - 用注释和 companion matrix 明确记录当前分支不纳入正向示例的主题，例如 `WorldSubsystem` / `GameInstanceSubsystem`。

## 分阶段执行计划

### Phase 0：冻结专项资产的命名、落点与边界

> 目标：先把“这个全能脚本到底放哪、叫什么、哪些能力明确不做”冻结下来，避免后面边写边改目录和口径。

- [ ] **AW0.1** 固定 giant example 的 source-of-truth 落点与命名
  - 本波次建议统一使用 `Documents/Plans/Plan_ScriptExamplesExpansion/Coverage/Example_Coverage_AllInOne.as` 作为真实资产，避免与未来 `Script/Examples/` 正式入口提前双写。
  - 同时固定对应测试文件名为 `AngelscriptScriptExampleAllInOneCoverageTests.cpp`，让 Automation 前缀沿用 `Angelscript.TestModule.ScriptExamples.*` 家族，不再另开风格。
  - 这一步要把命名一次定死，否则后续测试、README、矩阵文档会跟着反复改名。
- [ ] **AW0.1** 📦 Git 提交：`[Docs/Examples] Docs: freeze all-in-one script example naming and source-of-truth`

- [ ] **AW0.2** 冻结首版明确排除项与证据门槛
  - 首版允许展示“当前已经有证据的能力组合”，不允许把 `WorldSubsystem` / `GameInstanceSubsystem`、未验证的重网络路径、未收口的 GAS/editor 扩展塞进正向示例。
  - 对每个候选 specifier 或行为都先问“当前仓库有没有真实测试或 file-backed 证据”，没有证据就先登记到 deferred 列，而不是直接实现。
  - 这一步的价值是防止 giant example 变成“愿望清单”。
- [ ] **AW0.2** 📦 Git 提交：`[Docs/Examples] Docs: freeze all-in-one example exclusions and evidence gates`

### Phase 1：建立覆盖矩阵，先把“要覆盖什么”写清楚

> 目标：在写巨型脚本之前，先把属性、函数、基准三个矩阵拆清楚，避免最后只得到一份看似很大但覆盖不均的文件。

- [ ] **AW1.1** 新建属性声明矩阵文档
  - 在 `AllInOneCoverageMatrix.md` 中按“类型族 / 说明符 / 元数据 / 默认值 / 是否要求运行期验证”建表，先覆盖当前仓库已经有证据的属性表面。
  - `DefaultComponent`、`RootComponent`、`Attach`、`Category`、`BlueprintReadOnly`、`EditDefaultsOnly`、`NotEditable`、`EditConst`、`ClampMin/Max`、`EditCondition`、`InlineEditConditionToggle`、`MakeEditWidget` 至少应进首版。
  - `ExposeOnSpawn`、`SaveGame`、`Transient`、`Instanced`、`AssetRegistrySearchable` 等若缺少当前分支证据，应在矩阵里显式标为“待验证 / 本波次不承诺”。
- [ ] **AW1.1** 📦 Git 提交：`[Docs/Examples] Docs: add all-in-one property coverage matrix`

- [ ] **AW1.2** 新建函数签名与 `UFUNCTION` 矩阵文档
  - 把“参数形态”和“说明符形态”拆成两个正交维度：无参、单参、多参、返回值、`&out`、对象/结构体/容器参数，与 `BlueprintPure`、`BlueprintEvent`、`BlueprintOverride`、`NotBlueprintCallable`、`CallInEditor`、`Category` 组合。
  - 同时登记哪些调用必须保持 `UFUNCTION`、哪些故意保留 script-only helper，以体现热重载与反射边界差异。
  - 这一步要把“排列组合”从口号变成可验证列表。
- [ ] **AW1.2** 📦 Git 提交：`[Docs/Examples] Docs: add all-in-one function and UFUNCTION matrix`

- [ ] **AW1.3** 冻结 `ASTestActor` 吸收子集与断言策略
  - 把外部参考中值得吸收的基准项单独列出来：空函数、算术函数、属性 direct get/set、getter/setter 调用、静态/成员函数、数组与 Map 元素访问、字符串化输出、CSV 行收集。
  - 同时明确自动化只验证“结果结构存在且维度齐全”，不比较具体机器上的耗时数字，防止基准框架变成脆弱测试。
  - 这一步会决定 giant example 的运行期章节大小和测试策略。
- [ ] **AW1.3** 📦 Git 提交：`[Docs/Examples] Docs: freeze ASTestActor-inspired benchmark subset`

### Phase 2：落地单文件 giant example 主体

> 目标：按已经冻结的矩阵，把所有高价值覆盖点收进一份单文件、强分区、可读的 `.as` 资产，而不是继续新增多个零散 coverage 文件。

- [ ] **AW2.1** 先搭 giant example 的骨架和 supporting types
  - 先写 `UENUM`、`USTRUCT`、`UINTERFACE`、`delegate`、`event`、辅助 `UObject`、辅助 `UAngelscriptComponent`、辅助 `AActor`，并统一命名前缀。
  - 同时预留 benchmark 结果结构、CSV 输出 helper 与分节注释，保证后续扩充不会打乱测试查找点。
  - 这一步先解决可读性和组织结构，而不是马上把所有字段堆进去。
- [ ] **AW2.1** 📦 Git 提交：`[Script/Coverage] Feat: scaffold all-in-one script example structure`

- [ ] **AW2.2** 实现属性矩阵章节
  - 把属性按“可编辑性 / Blueprint 可见性 / 元数据 / 默认组件 / 复合类型”分组写入 giant example，并为每组安排少量代表字段，而不是纯粹机械列举。
  - 对 `TArray`、`TSet`、`TMap`、对象引用、类引用、枚举、结构体等类型至少保留一个正向示例，保证后续 benchmark 与运行期访问能复用同一批字段。
  - 该章节完成后，应能替代当前大部分“属性碎片示例”。
- [ ] **AW2.2** 📦 Git 提交：`[Script/Coverage] Feat: add all-in-one property declaration matrix`

- [ ] **AW2.3** 实现函数签名与 `UFUNCTION` 章节
  - 写入全局函数、成员函数、静态函数、script-only helper、Blueprint 可见函数、pure/event/override/call-in-editor/not-callable 组合，以及代表性的 `&out`、对象、结构体、容器参数。
  - 每个说明符组合至少保留一个“为什么存在”的业务化示例，避免巨型脚本沦为抽象语法表。
  - 这一步要把当前分散在 `Functions` 与 `FunctionSpecifiers` 内联测试中的能力统一为真实文件资产。
- [ ] **AW2.3** 📦 Git 提交：`[Script/Coverage] Feat: add all-in-one function and UFUNCTION matrix`

- [ ] **AW2.4** 实现运行期行为章节
  - 把 `BeginPlay`、`Tick`、`EndPlay`、默认组件层级、组件 owner 访问、delegate bind/broadcast、interface dispatch、基础 replication default 与标签透传串成一个真实运行路径。
  - 行为章节要尽量复用前面已经声明过的字段和 helper，防止声明矩阵与行为矩阵各写一份相似逻辑。
  - 这一步完成后，单文件 giant example 才真正具备“对外演示 + 内部回归”双属性。
- [ ] **AW2.4** 📦 Git 提交：`[Script/Coverage] Feat: add all-in-one runtime behavior coverage`

- [ ] **AW2.5** 实现 `ASTestActor` 风格的基准章节
  - 引入 `Names` / `Times`、`StartTest` / `EndTest`、循环压测 helper、CSV 内容拼装和结果收集路径。
  - 保留当前仓库最值得验证的高频面：direct property access、getter/setter、静态/成员函数、数组与 Map 元素访问、基础数值和常见 UE 类型。
  - 这一步要强调“结构化基准覆盖”，而不是追求极端数量导致脚本完全不可维护。
- [ ] **AW2.5** 📦 Git 提交：`[Script/Coverage] Feat: add ASTestActor-inspired benchmark coverage`

### Phase 3：补齐专门测试与文档入口

> 目标：让 giant example 成为被自动化和文档共同承接的正式资产，而不是只落一个很大的文件。

- [ ] **AW3.1** 新增 file-backed giant example 自动化测试
  - 新测试文件直接从 `Example_Coverage_AllInOne.as` 读取脚本源码，不复制第二份内联文本。
  - 测试至少拆为“编译与反射”、“运行期行为”、“benchmark 结果结构”三组 case，避免一个超大 test case 同时承担所有失败定位。
  - 断言重点放在类生成、属性 flag / metadata、函数可调用性、事件/接口行为、benchmark 结果行存在性。
- [ ] **AW3.1** 📦 Git 提交：`[Test/Examples] Test: add all-in-one script example coverage tests`

- [ ] **AW3.2** 同步 companion README、示例 README 与测试目录索引
  - `Documents/Plans/Plan_ScriptExamplesExpansion/README.md` 需要登记本专项执行包和 giant example 资产。
  - `Script/Examples/README.md` 需要说明当前这份全能脚本仍以 companion 目录为真实资产来源，以及未来何时提升到正式入口。
  - 如新增了稳定测试前缀，还要把 `Documents/Guides/TestCatalog.md` 同步到可导航状态。
- [ ] **AW3.2** 📦 Git 提交：`[Docs/Examples] Docs: register all-in-one script example entrypoints`

- [ ] **AW3.3** 执行专项回归并记录 closeout 结论
  - 至少跑通 giant example 的专门测试，以及受影响的 `Coverage` / `ScriptExamples` 基线测试。
  - 若某些候选 specifier 或行为最终因当前分支边界不能进入正向示例，必须在 `AllInOneCoverageMatrix.md` 中记录“延期原因”，而不是悄悄删除。
  - closeout 时要能回答：这份全能脚本到底覆盖了哪些真实能力、哪些能力故意未纳入，以及未来下一波扩展从哪里继续。
- [ ] **AW3.3** 📦 Git 提交：`[Test/Examples] Test: validate all-in-one script example baseline`

## 验收标准

1. 仓库中存在一份真实 file-backed 的单文件 giant example：`Documents/Plans/Plan_ScriptExamplesExpansion/Coverage/Example_Coverage_AllInOne.as`。
2. 该脚本同时覆盖属性说明符矩阵、函数签名矩阵、`UFUNCTION` 说明符矩阵，以及 `ASTestActor` 风格的基准章节，而不是只做其中一面。
3. giant example 至少正向体现当前仓库已经有证据的 `delegate` / `event`、`UINTERFACE`、组件层级、`BlueprintOverride`、`BlueprintEvent`、`BlueprintPure`、`CallInEditor` 等能力。
4. `WorldSubsystem` / `GameInstanceSubsystem` 等当前分支未闭环能力不会被伪装成首版正向示例，并在 companion matrix 中有明确排除记录。
5. 存在与 giant example 对应的 `Examples` 自动化测试，并能验证编译、反射、运行期行为和 benchmark 结果结构。
6. companion README、示例 README 与必要测试文档能把执行者导航到该文件、其验证入口和已知边界。

## 风险与注意事项

### 风险

1. **单文件体量过大导致可读性崩塌**
   - 如果不做章节化与命名前缀控制，脚本会迅速退化成“巨大但不可维护”的演示垃圾场。
   - **缓解**：先冻结内部章节结构和 companion matrix，再按章节落地；每一节都复用前置 helper，而不是平铺重复样板。

2. **把“想覆盖”的能力误写成“当前已支持”的能力**
   - 这在 subsystem、较深网络路径和部分高级说明符上尤其危险。
   - **缓解**：所有条目都以当前仓库已有测试或真实资产证据为前置门槛，没有证据先写进 deferred。

3. **性能基准测试脆弱**
   - 若直接比较时间数字，自动化会高度依赖机器状态、编译配置和测试环境。
   - **缓解**：自动化只验证基准结果结构、样本数量与导出逻辑；性能数字只作为人工比较产物。

4. **全能脚本与既有 coverage 资产职责重叠**
   - 若不提前定义关系，未来会同时维护多个几乎相同的示例，重新制造 source-of-truth 分叉。
   - **缓解**：把 giant example 定位为“总入口 + 第二波汇总资产”，并在 README 中明确它与现有四个首波 coverage 文件的关系。

### 已知行为变化

1. **示例真实入口仍暂留在 companion 目录**
   - 首版 giant example 不会立即提升到 `Script/Examples/`，当前正式 source-of-truth 仍位于 `Documents/Plans/Plan_ScriptExamplesExpansion/Coverage/`。

2. **首版 giant example 不承诺全量 parity**
   - 例如 `WorldSubsystem` / `GameInstanceSubsystem` 正向示例不会进入首版；若未来能力闭环，再在后续波次翻转为正例。

3. **benchmark 章节是“结构回归 + 手工对比”双用途资产**
   - 自动化只校验结构和可执行性，人工对比时才关注输出的 `Names/Times/CSV` 内容。
