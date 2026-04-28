# Preprocessor — import 解析、include 处理、模块系统改进计划

## 背景与目标

### 背景

当前 `FAngelscriptPreprocessor` 仍把 `import` 词法解析、module identity 规范化、依赖排序、未来 `#include` 扩展和最终 `CodeHash` 组装混在同一条一次性流水线里。`import` 在 `automatic import` 与 `manual import` 两种模式下走不同且互相打架的后处理路径，module 名称又同时受相对路径、大小写、分隔符和文件发现顺序影响，导致预处理、编译、hot reload 与 precompiled cache 对“同一个模块”的解释并不一致。

经核对 `Documents/Plans/`，当前没有专门覆盖 preprocessor import/include/module identity 的活跃 Plan。最接近的 `Plan_ScriptFileSystemRefactor.md` 只处理文件系统抽象与读盘语义，`Plan_AngelscriptEngineBindAndFileWatchValidation.md` 只扩 watcher / file watch 验证，不替代本计划中“预处理依赖合同本身”的修复与收口。

### 范围与边界

- **范围内**
  - `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/` 中 `import` 解析、module 命名、`#include` 合同、section/hash 组装和相关中间产物
  - 与上述合同直接耦合的 `Core/AngelscriptEngine.*`、`StaticJIT/PrecompiledData.*`、必要时 `as_builder.*` 的 module graph / section provenance / cache 对齐
  - 以 `Preprocessor/`、`StaticJIT/`、`Learning/Runtime/` 为主的回归测试与 trace
- **范围外**
  - 文件 I/O 抽象、同步/异步读盘 owner、全局 `FAngelscriptEngine::Get()` 去依赖等 `Plan_ScriptFileSystemRefactor.md` 已覆盖主题
  - 与 import/include/module 无直接关系的 directive 家族普遍清理、metadata 解析或 quoted-string 通用重构
  - watcher UI、bind 启动链和非 Preprocessor 主题的 HotReload 策略重写

### 目标

1. 让 `automatic import` 与 `manual import` 共享同一套 `import` statement 解析真源，不再出现默认路径 warning/blanking 死区。
2. 让 `import` 的 module key、duplicate 判定、cycle/unresolved diagnostics 和最终 `ImportedModules` 都建立在统一 canonical identity 上，失败时不再暴露半成品状态。
3. 让 module identity 脱离文件系统枚举顺序和路径写法差异，duplicate module 在预处理前 fail-fast，而不是由“先遇到谁/后覆盖谁”决定赢家。
4. 让 `#include` 不再以“静默透传”的未建模状态存在；要么明确 hard error，要么进入 include graph、section provenance、hash 与 hot reload 合同。
5. 让 live preprocess、fully precompiled 和后续增量化都消费同一份 `ModuleIndex + ordered artifact`，避免 import/include/module graph 在不同入口下继续漂移。

## 分析来源

| 分析文档 | 关键发现 |
|---------|---------|
| `Documents/AutoPlans/Preprocessor_Analysis.md` | 发现 1/2/12/13/28/71/73 指出 duplicate import 污染哈希、automatic import 默认路径失效、`import` 漏分号吞源码、module identity 大小写/路径不一致、`#include` 完全未建模、发现顺序和线性扫描放大 nondeterminism。 |
| `Documents/AutoPlans/DiscoveryPlans/Preprocessor_Plan.md` | Issue-4/47/59/5/24/55/57 给出 `ImportResolver`、automatic/manual 阶段拆分、duplicate-module gate、include graph、section-aware diagnostics 与 ordered fingerprint 的落地方案。 |
| `Documents/AutoPlans/TestCoverage/Preprocessor_TestGaps.md` | NewTest-1/2/3/5/13/16/17 与 Issue-22 说明当前测试只覆盖单跳 happy path，没有锁住 cycle chain、automatic-import 兼容、missing semicolon、backslash path、topological order、duplicate import 和 include 合同。 |
| `Documents/AutoPlans/ArchitectureReview/ModuleStructure_ArchReview.md` | 相关模块边界审查强调 runtime 侧应保持 deterministic script-root / compile-pipeline contract，不应继续把 loader / include path 自由度回灌到核心模块解释规则里。 |
| `Documents/AutoPlans/ReferenceComparison/CrossComparison.md` | `D11` 明确当前 Angelscript 的强项是 deterministic project/plugin roots 与稳定 operator surface；对比 loader-heavy 插件后，不应继续把 module identity 建立在隐式搜索顺序或自由 loader 上。 |

## 影响范围

本次改进涉及以下操作（按需组合）：

- **`import` parser/finalizer 拆分**：把 statement 解析、源码 blanking、warning 与 dependency resolve 从同一函数体拆成阶段化入口
- **module identity 收敛**：统一 relative path canonicalization、leaf extension 去除、slash/case 规范化和 duplicate-module gate
- **include graph / section provenance 新增**：新增 `FIncludeDesc` / `FResolvedInclude`、expanded section、source provenance 与 section-aware diagnostics
- **module index / artifact 合同升级**：引入 `ModuleIndex`、ordered fingerprint、live/precompiled 对齐和 cache invalidation
- **测试矩阵补齐**：为 import/include/module graph 新增 fast tests、trace tests 和 precompiled parity tests

按目录分组的受影响文件清单：

`Preprocessor/`（2 个）：
- `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h` — `FImport` / future `FIncludeDesc` / `ModuleIndex` / artifact 结构
- `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` — `FilenameToModuleName()`、`Preprocess()`、`ProcessImports()`、directive scanner、`CodeHash` 组装

`Core/`（2 个）：
- `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h` — section-aware diagnostics、module identity 与 lookup 契约
- `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` — duplicate-module gate、live/precompiled provider lookup、include owner invalidation

`StaticJIT/`（2 个）：
- `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.h` — import/include/module graph 的 schema 扩展
- `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp` — ordered fingerprint / graph restore / stale-cache invalidation

`ThirdParty Builder`（按 include graph 落地需要启用，2 个）：
- `Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_builder.h` — section-aware editor-only / diagnostics interface
- `Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_builder.cpp` — section-level line mapping与 source provenance 消费

`AngelscriptTest/`（6 个）：
- `Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorTests.cpp` — 现有 weak assertions 升级
- `Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorImportTests.cpp` — import parser / resolver / topology / dedupe
- `Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorPathTests.cpp` — module identity / duplicate gate / Windows relative path
- `Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorIncludeTests.cpp` — include fail-fast / include graph / section provenance
- `Plugins/Angelscript/Source/AngelscriptTest/StaticJIT/AngelscriptPrecompiledDataModuleGraphTests.cpp` — live/precompiled import graph parity 与 ordered fingerprint
- `Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningPreprocessorTraceTests.cpp` — 新合同 trace 与 warning/graph 可视化

## 分阶段执行计划

### Phase 1：先修 import correctness 与 module identity

- [ ] **P1.1** 统一 `import` statement parser，并把 `automatic/manual import` 兼容逻辑拆成独立阶段
  - 当前 `Preprocess()` 只在关闭 `automatic import` 时进入 `ProcessImports()`，导致默认配置下手写 `import` 既不 warning 也不从源码中 blank；同一入口还靠“向后扫到下一个 `;`”解析 statement，漏分号时会把后续源码整段吞进模块名。
  - 先把 `ParseIntoChunks()` 阶段收口成“只记录 `RawImportText + SourceLine + StatementSpan`”，再新增 `FinalizeImportStatements()` 或等价阶段，统一做 terminator 校验、comment stripping、源码 blanking 和 `automatic import` 下的 compatibility warning；manual-import 排序改成只消费这份已验证 statement 列表。
  - 这一项先不解决 relative path / duplicate import / include graph，只先把“什么是一个合法 import statement”固定成单一真源，避免后续 resolver、cache 和 trace 继续建立在裸 substring 上。
  - 来源：
    - [A] `Documents/AutoPlans/Preprocessor_Analysis.md` — “发现 2/12：默认 automatic import 路径绕开 manual import warning，缺失 `;` 的 `import` 会吞掉后续源码并留下误导性 module-not-found 诊断”
    - [B] `Documents/AutoPlans/DiscoveryPlans/Preprocessor_Plan.md` — “Issue-47：兼容剥离与 warning 路径完全失效；Issue-11：`import` 缺少 terminator 校验，错误应该前移到预处理阶段”
    - [C] `Documents/AutoPlans/TestCoverage/Preprocessor_TestGaps.md` — “NewTest-2/NewTest-3：缺少默认 automatic-import 场景与 missing semicolon 语法错误回归；Issue-22：现有 import 测试只做 `Contains` 弱断言”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` L232-L238、L3497-L3510、L482-L490 — `Preprocess()` 仅在 automatic import 关闭时调用 `ProcessImports()`，`import` 解析仍靠裸 `;` 扫描，warning/blanking 逻辑全部挂在 `ProcessImports()` 内。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorImportTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningPreprocessorTraceTests.cpp`
- [ ] **P1.1** 📦 Git 提交：`[AngelscriptRuntime/Preprocessor] Fix: split import parsing from automatic-import compatibility`
- [ ] **P1.1-T** 单元测试：锁住 mode-aware `import` statement 合同
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorImportTests.cpp`
  - 测试场景：
    - 正常路径：`automatic import = false` 时，合法 `import Shared;` 被剥离、记录并维持 provider-first 顺序。
    - 边界条件：`automatic import = true` 时，手写 `import` 只触发 compatibility warning，不再残留于 `ProcessedCode`，且不会写入显式 `ImportedModules`。
    - 错误路径：缺失 `;`、尾随 line/block comment 的 `import` 给出 line-aware 语法错误，不再把后续源码吞进模块名。
  - 测试命名：`Angelscript.TestModule.Preprocessor.Import.AutomaticModeManualImportCompatibility`、`Angelscript.TestModule.Preprocessor.Import.MissingSemicolonReportsSyntax`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P1.1-T** 📦 Git 提交：`[AngelscriptTest/Preprocessor] Test: cover import parser mode and syntax failures`

- [ ] **P1.2** 抽出 canonical `ImportResolver`，把依赖图解析改成“先 resolve，后提交 side effects”
  - 当前 `ProcessImports()` 对每条依赖都线性扫描全部 `Files` 做 exact-match，找到 provider 前后立即改写 `ImportedModules` 和 chunk 文本；relative path、filename-style、大小写差异、重复 import 与循环链因此全都混在一个带副作用的 DFS 里。
  - 这一项要新增 `FResolvedImport` / `ResolveImportName()`，统一做 comment stripping、filename-style / relative-path / slash / case canonicalization；resolver 先返回 canonical dependency list、duplicate import warning、cycle/unresolved diagnostics 与 provider-first order，只有整张图成功后才一次性写入 `ImportedModules` 并提交源码 blanking。
  - `ImportedModules` 从这一轮开始明确为“去重且保序的 canonical 依赖列表”；任何 hash、compile-stage import、hot reload 传播都只消费这份列表，不再回看原始 `FImport`。
  - 来源：
    - [A] `Documents/AutoPlans/Preprocessor_Analysis.md` — “发现 1/13/22/73：duplicate import 会污染依赖哈希，import/module lookup 对 casing 敏感，循环诊断丢失真正的边信息，显式 import 每条依赖都线性扫描全部 `Files`”
    - [B] `Documents/AutoPlans/DiscoveryPlans/Preprocessor_Plan.md` — “Issue-4：缺少独立 resolver；Issue-46：重复 import 应生成 canonical 去重列表；Issue-25：comment-aware normalization 不能再由裸 substring 直接生成 key”
    - [C] `Documents/AutoPlans/TestCoverage/Preprocessor_TestGaps.md` — “NewTest-1/NewTest-16/NewTest-17：缺少循环链、provider-first topology 与 duplicate import dedupe 的回归”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h` L101-L167、`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` L439-L497、L86-L99 — `FImport` 只存裸 `ModuleName`，`ProcessImports()` 一边 DFS 一边写 `ImportedModules`/blank chunk，provider 查找依赖 exact-match string，module key 又直接来自简化版 `FilenameToModuleName()`.
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorImportTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningPreprocessorTraceTests.cpp`
- [ ] **P1.2** 📦 Git 提交：`[AngelscriptRuntime/Preprocessor] Fix: resolve imports canonically and commit dependency edges atomically`
- [ ] **P1.2-T** 单元测试：锁住 canonical import resolver 与 side-effect 原子性
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorImportTests.cpp`
  - 测试场景：
    - 正常路径：`Base <- Shared <- Consumer` 在逆序 `AddFile()` 下仍输出 provider-first 拓扑，且 `ImportedModules` 使用 canonical module key。
    - 边界条件：`import ./Shared.as;`、`import Shared /* note */;`、`import gameplay.shared;` 归一后命中同一 provider，并对 duplicate import 只保留一次依赖边。
    - 错误路径：`A -> B -> A` 给出带 source line 的循环链，失败后 `ImportedModules`、`ProcessedCode` 和 compile batch 不暴露半成品状态。
  - 测试命名：`Angelscript.TestModule.Preprocessor.Import.TopologicalOrderRespectsDependencyChain`、`Angelscript.TestModule.Preprocessor.Import.CircularDependencyReportsChain`、`Angelscript.TestModule.Preprocessor.Import.DuplicateStatementsDeduplicateDependency`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P1.2-T** 📦 Git 提交：`[AngelscriptTest/Preprocessor] Test: add canonical import resolver coverage`

- [ ] **P1.3** 收口 module identity，并把 duplicate-module 决策从“枚举顺序驱动”改成预处理前 fail-fast
  - 当前 `FilenameToModuleName()` 直接做 `Replace(".as", "")` 和 `Replace("/", ".")`，既会把目录段里的 `.as` 一起删掉，也完全不处理 Windows `\` 相对路径；这使得 `AddFile()` 输入写法本身就会改变 module identity。
  - 这一项要提取共享的 `CanonicalRelativeScriptPath` / `BuildModuleNameFromRelativePath` helper：先统一 slash/case，只移除 leaf extension，再建立 `ModuleName -> RelativePath[]` 索引；凡 canonical key 冲突，一律在进入 resolver / compile 前报结构化 duplicate-module 错误，而不是让预处理“取第一个”、编译“留最后一个”。
  - 发现顺序要稳定化，但排序只服务于诊断可重复性，不再用来决定哪个 provider 胜出；真正的模块身份应完全脱离文件系统枚举顺序。
  - 来源：
    - [A] `Documents/AutoPlans/Preprocessor_Analysis.md` — “发现 10/13/71：`.as` 全局替换让路径到 module name 不是单射，import/module lookup 与 filename lookup 大小写语义不一致，发现顺序会让 duplicate module 的赢家漂移”
    - [B] `Documents/AutoPlans/DiscoveryPlans/Preprocessor_Plan.md` — “Issue-14/29：relative path 与 backslash 需要统一 canonical helper；Issue-59：duplicate module 和发现顺序必须在预处理前 fail-fast”
    - [C] `Documents/AutoPlans/TestCoverage/Preprocessor_TestGaps.md` — “NewTest-13：Windows 风格相对路径没有测试；现有 discovery/preprocessor 用例没有锁住 duplicate-module determinism”
    - [E] `Documents/AutoPlans/ReferenceComparison/CrossComparison.md` — “D11：当前 Angelscript 的强项是 deterministic project/plugin roots，不应把 module identity 继续交给隐式 loader 或自然枚举顺序”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` L86-L99、L463-L470 — module key 仍由 `FilenameToModuleName()` 的全局字符串替换生成，provider 查找继续依赖 exact-match module string，没有任何 duplicate gate 或 slash/case canonicalization。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorPathTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/FileSystem/AngelscriptFileSystemTests.cpp`
- [ ] **P1.3** 📦 Git 提交：`[AngelscriptRuntime/Preprocessor] Fix: canonicalize module identity and reject duplicate module names`
- [ ] **P1.3-T** 单元测试：锁住 path-to-module 与 duplicate-module determinism
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorPathTests.cpp`
  - 测试场景：
    - 正常路径：普通 forward-slash 相对路径继续映射到既有点分 module name。
    - 边界条件：backslash 相对路径、目录段含 `.as`、大小写不同但同一物理 key 的路径写法都归一到同一 canonical identity 或给出明确冲突诊断。
    - 错误路径：两份脚本 canonicalize 到同一 module name 时，无论 `AddFile()` 顺序如何都得到相同 duplicate-module 错误，并且不进入真正编译。
  - 测试命名：`Angelscript.TestModule.Preprocessor.Paths.BackslashRelativePathNormalizesModuleName`、`Angelscript.TestModule.Preprocessor.ModuleIdentity.DuplicateModuleFailsDeterministically`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P1.3-T** 📦 Git 提交：`[AngelscriptTest/Preprocessor] Test: cover module identity normalization and duplicate gate`

### Phase 2：补 `#include` 合同，并把 module graph 推到 live/precompiled 一致性

- [ ] **P2.1** 为 `#include` 建立显式合同，再把 included file 纳入 include graph、section provenance 与 hash
  - 当前 directive scanner 只识别 `#if/#ifdef/#ifndef/#elif/#else/#endif` 与 `#restrict usage`，`#include` 既没有 fail-fast 也没有 graph model；`Preprocess()` 最后只把当前文件的 `ProcessedCode` 写成单个 `FCodeSection` 并对其做 XOR 聚合。
  - 这一项必须分两阶段落地：先在 `ParseIntoChunks()` 层把裸 `#include` 变成 hard error，停止静默透传；随后再引入 `FIncludeDesc / FResolvedInclude`、relative include resolution、include-once 语义、循环 include 诊断和 `IncludedFile -> OwningModule` 反向索引，把 include file 正式纳入 `Module->Code`、hot reload invalidation、editor-only 行映射与 compile diagnostics provenance。
  - include graph 一旦落地，错误定位、editor-only block、`CodeHash`、precompiled schema 就必须同步升级为 section-aware；否则只会把未建模问题从“静默透传”换成“定位全错”。
  - 来源：
    - [A] `Documents/AutoPlans/Preprocessor_Analysis.md` — “发现 28：`#include` 没有进入任何预处理、依赖跟踪或模块哈希合同”
    - [B] `Documents/AutoPlans/DiscoveryPlans/Preprocessor_Plan.md` — “Issue-5：先 fail-fast 再落 include graph；Issue-55/56/57：multi-section 诊断、editor-only line map 和 `CodeHash` 都需要 section-aware 收口”
    - [C] `Documents/AutoPlans/TestCoverage/Preprocessor_TestGaps.md` — “NewTest-5：当前完全没有 include 合同测试，用户看不出它是支持、忽略还是报错”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` L289-L304、L3257-L3390；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h` L120-L161 — directive 分支里没有 `#include`，`FFile` 也没有任何 include 描述结构，最终 `CodeHash` 只来自当前 `ProcessedCode` section。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_builder.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_builder.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorIncludeTests.cpp`
- [ ] **P2.1** 📦 Git 提交：`[AngelscriptRuntime/Preprocessor] Feat: define include contract and integrate include graph into module sections`
- [ ] **P2.1-T** 单元测试：锁住 `#include` 从 fail-fast 到 include graph 的演进合同
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorIncludeTests.cpp`
  - 测试场景：
    - 正常路径：include graph 开启后，`#include "Shared.as"` 按当前文件目录解析并只展开一次，`Entry()` 可以执行 included symbol。
    - 边界条件：重复 include 只展开一次、循环 include 给出链式诊断、included file 的 section provenance 与 editor-only block 仍能映射回正确源码文件。
    - 错误路径：graph 落地前裸 `#include` 必须 hard error；graph 落地后缺失文件、非法 include literal 和 unresolved include 仍给 line-aware 明确诊断，禁止静默透传。
  - 测试命名：`Angelscript.TestModule.Preprocessor.Include.FailFastUnsupportedDirective`、`Angelscript.TestModule.Preprocessor.Include.RelativeIncludeExpandsOnce`、`Angelscript.TestModule.Preprocessor.Include.CircularIncludeReportsChain`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P2.1-T** 📦 Git 提交：`[AngelscriptTest/Preprocessor] Test: add include contract and graph coverage`

- [ ] **P2.2** 建立 `ModuleIndex + ordered preprocess artifact`，让 live preprocess、fully precompiled 与 future incremental path 共享同一份 module graph
  - 当前 `FFile` 同时承载 `RawCode`、`ChunkedCode`、`ProcessedCode`、`Imports` 和异步句柄，`Preprocess()` 每次都重跑整条流水线；显式 import 解析仍靠 `for (FFile& OtherFile : Files)` 全表扫描 provider，而 `Files = SortedFiles` 还会整体复制大对象状态。
  - 这一项要在 canonical module identity 之上建立 `ModuleIndex` 与 `PreprocessArtifact`：provider lookup、duplicate gate、include provenance、ordered section fingerprint、live/precompiled restore 全部只认同一份 canonical index；`Module->CodeHash` 从“section XOR 集合”升级为“按 section 顺序 + source provenance 计算的 ordered fingerprint”，让 cache 命中条件终于与 `AddScriptSection(...)` 的真实输入一致。
  - fully precompiled 路径必须重建与 live preprocess 相同的 graph 和 section order；若 artifact 缺少 provider/order/provenance，就显式判 stale 并回退 live preprocess，而不是继续吃旧缓存。
  - 来源：
    - [A] `Documents/AutoPlans/Preprocessor_Analysis.md` — “发现 73：显式 import 对每条依赖都线性扫描全部 `Files`；发现 57/59/60：precompiled 与 dependency hash 对 import/module graph 的恢复和命中条件不一致”
    - [B] `Documents/AutoPlans/DiscoveryPlans/Preprocessor_Plan.md` — “Issue-24：缺少稳定 `ModuleIndex` 与 artifact cache；Issue-57：`CodeHash` 仍是 section XOR，顺序和 provenance 都不入签名”
    - [C] `Documents/AutoPlans/TestCoverage/Preprocessor_TestGaps.md` — “NewTest-16 与 Issue-22 表明当前测试既未锁住 provider-first 拓扑，也没有精确约束 `ImportedModules` 的有序最终形态”
    - [E] `Documents/AutoPlans/ReferenceComparison/CrossComparison.md` — “D11：当前项目的优势是 deterministic roots / operator surface，module graph 不能在 fully precompiled 路径下重新退回 loader-style 兜底”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h` L120-L161、`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` L235-L238、L439-L497、L289-L304 — `FFile` 仍是一次性大对象，`Preprocess()` 通过 `Files = SortedFiles` 做整对象重排，provider 查找靠全表扫描，`Module->CodeHash` 仍对 section hash 做 XOR 聚合。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/StaticJIT/AngelscriptPrecompiledDataModuleGraphTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningPreprocessorTraceTests.cpp`
- [ ] **P2.2** 📦 Git 提交：`[AngelscriptRuntime/Preprocessor] Refactor: add module index and ordered preprocess artifacts`
- [ ] **P2.2-T** 单元测试：锁住 live / precompiled module graph 与 ordered fingerprint 合同
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/StaticJIT/AngelscriptPrecompiledDataModuleGraphTests.cpp`
  - 测试场景：
    - 正常路径：同一组 `Base <- Shared <- Consumer` fixture 走 live preprocess 与 fully precompiled restore 时，provider-first 顺序、canonical `ImportedModules` 和最终 compile 结果一致。
    - 边界条件：交换 section 顺序、仅改变 included file provenance 或保持同文不同源时，ordered fingerprint 必须变化并触发 cache miss / live fallback。
    - 错误路径：缺少 provider/order/provenance 的 stale artifact 会被明确拒绝，不得继续静默命中旧缓存或把 diagnostics 锚到错误 section。
  - 测试命名：`Angelscript.TestModule.StaticJIT.LiveAndPrecompiledImportGraphMatch`、`Angelscript.TestModule.StaticJIT.OrderedSectionFingerprintInvalidatesCache`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P2.2-T** 📦 Git 提交：`[AngelscriptTest/StaticJIT] Test: verify module graph artifact parity and ordered fingerprints`

## 单元测试总览

| 改进项 | 测试文件 | 测试场景 | 优先级 |
|--------|---------|---------|--------|
| P1.1 | `Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorImportTests.cpp` | automatic/manual mode parity、missing semicolon、comment-aware import syntax | P0 |
| P1.2 | `Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorImportTests.cpp` | canonical resolver、provider-first 拓扑、cycle chain、duplicate import dedupe | P0 |
| P1.3 | `Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorPathTests.cpp` | backslash relative path、`.as` 目录段、duplicate module determinism | P1 |
| P2.1 | `Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorIncludeTests.cpp` | include fail-fast、relative include expand-once、cycle include、section provenance | P1 |
| P2.2 | `Plugins/Angelscript/Source/AngelscriptTest/StaticJIT/AngelscriptPrecompiledDataModuleGraphTests.cpp` | live/precompiled graph parity、ordered fingerprint、stale artifact rejection | P1 |

## 验收标准

1. 默认 `automatic import` 配置下，手写 `import` 的 blanking / warning / explicit-dependency 语义与文档一致，且有独立自动化回归。
2. `import` 解析不再直接消费裸 substring；relative path、filename-style、slash/case 差异在 resolver 阶段归一为统一 canonical module identity。
3. duplicate import 与 duplicate module 都会在预处理阶段给出可重复、line-aware、path-aware 诊断，不再受 `AddFile()` / 文件发现顺序影响。
4. `#include` 不再以“静默透传”方式存在；要么明确 hard error，要么正式进入 include graph、section provenance、editor-only line map 与 cache invalidation 合同。
5. live preprocess 与 fully precompiled 路径对同一 import/include/module graph 产生一致的 provider 顺序与 fingerprint；section 顺序或 provenance 变化不会再误命中旧缓存。
6. 新增测试至少覆盖 `Preprocessor` fast tests、`Learning` trace 和 `StaticJIT` parity 三层入口，并全部使用 `FAngelscriptEngineScope` 隔离。

## 风险与注意事项

### 风险

1. **canonicalization 会把历史“偶然可用”的脚本写法变成显式错误**
   - 比如 backslash 相对路径、大小写不一致 import、duplicate module 或依赖非规范 filename-style 的脚本，在修复后会从“碰巧编过”变成 fail-fast。
   - **缓解**：所有新错误都必须输出 canonical module name、原始文本和冲突/解析到的真实路径，帮助用户迁移。

2. **`#include` 一旦正式建模，会同步拉高 diagnostics / builder / precompiled schema 的改动半径**
   - 如果只补 directive 解析，不同步 section provenance、editor-only line map 和 ordered fingerprint，include graph 会立刻把错误定位打坏。
   - **缓解**：把 `#include` 按“fail-fast -> include graph”两阶段落地，只有 section-aware 诊断和 fingerprint 方案就位后才允许打开 graph。

3. **artifact/cache key 设计不完整会产生比“慢但正确”更危险的 stale 命中**
   - `automatic/manual import` 模式、preprocessor flags、hook 版本、section provenance 只要漏掉一个，缓存就可能复用错误 graph。
   - **缓解**：先做进程内 artifact cache 与统计验证，确认 key 完整后再考虑 fully precompiled schema 写盘。

### 已知行为变化

1. **默认 `automatic import` 模式将开始真正 blank 掉手写 `import` 并按配置发 warning**
   - 影响文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` L232-L238、L482-L490 对应路径，以及依赖旧“残留 import 文本”的脚本 fixture。

2. **duplicate module name 将从“顺序驱动的隐式赢家”升级为结构化硬错误**
   - 影响文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` L86-L99、L463-L470 相关 identity / provider 路径，以及 `Core/AngelscriptEngine.cpp` 的 compile / active-module 注册逻辑。

3. **`#include` 将不再静默流入后续编译**
   - 第一阶段行为变化是 hard error；第二阶段行为变化是 include file 正式计入 section/hash/hot reload dependency。
   - 影响文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` L3257-L3390、L289-L304，以及 future `StaticJIT/PrecompiledData.*` schema。

4. **ordered fingerprint 落地后，旧 precompiled cache 会被主动判 stale**
   - 影响文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` L289-L304 和 fully precompiled 命中条件。

---

## 本轮补充（2026-04-09）

### Phase 3：失败恢复与下游导入合同收口

- [ ] **P3.1** 在 `ParseIntoChunks()` 与 explicit `import` resolver 之间建立硬失败屏障，并把 import 副作用改成“解析成功后统一提交”
  - 当前 `Preprocess()` 在整轮 chunk 解析结束后，仍会先进入 `ProcessImports()` 再检查 `bHasError`；而 `ProcessImports()` 在子递归已经触发 fatal 后，依然继续追加 `ImportedModules`、blank 原始 `import` 语句并把文件写入 `OutSortedFiles`。这让 directive 语法错与循环 import 都会留下半提交依赖状态。
  - 这一项要把 parse 阶段与 import 提交阶段显式隔开：`ParseIntoChunks()` 一旦报出 fatal，就直接阻断 resolver；resolver 内部也改成“先产出临时拓扑/解析结果，整张图成功后再统一写入 `Module->ImportedModules`、执行 `ReplaceWithBlank()`、翻转 `bImportsResolved`”。
  - 目标不是只补一个 `if (bHasError)`，而是把“失败不提交副作用”固化成 import resolver 的正式合同，避免未来 include graph、增量预处理或 trace hook 再看到半成品状态。
  - 来源：
    - [A] `Documents/AutoPlans/Preprocessor_Analysis.md` — “发现 23/25：fatal directive error 与循环 import 之后，resolver 仍会污染 `ImportedModules`、blank 原文并标记成功状态”
    - [B] `Documents/AutoPlans/DiscoveryPlans/Preprocessor_Plan.md` — “Issue-62：parse-failed 文件必须在 import 排序前 fail-fast，resolver 失败后不得保留半提交状态”
    - [C] `Documents/AutoPlans/TestCoverage/Preprocessor_TestGaps.md` — “NewTest-34：结构性 directive 错误必须给出稳定行号并在失败后停止继续预处理”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` L228-L243、L439-L497 — `Preprocess()` 在 parse 失败后仍先进入 `ProcessImports()`，而 `ProcessImports()` 在子调用报错后仍继续 `Add()`、`ReplaceWithBlank()` 与 `OutSortedFiles.Add(File)`。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorImportFailureTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningPreprocessorTraceTests.cpp`
- [ ] **P3.1** 📦 Git 提交：`[AngelscriptRuntime/Preprocessor] Fix: stop import resolution after fatal parse errors`
- [ ] **P3.1-T** 单元测试：锁住 fatal parse / resolver failure 的“零副作用提交”合同
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorImportFailureTests.cpp`
  - 测试场景：
    - 正常路径：合法 `Base <- Shared <- Consumer` explicit import 链仍能完成 provider-first 排序并移除原始 `import` 文本。
    - 边界条件：非法 `#elif` 或缺失 `#endif` 与 `import` 同时出现时，`Preprocess()` 直接失败，`ImportedModules` 为空且源码中不发生部分 blank。
    - 错误路径：`A -> B -> A` 循环 import 或子 resolver fatal 时，不得留下半成品依赖列表、排序结果或“已解析”状态。
  - 测试命名：`Angelscript.TestModule.Preprocessor.Import.FatalDirectiveStopsResolver`、`Angelscript.TestModule.Preprocessor.Import.CircularFailureLeavesNoHalfState`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P3.1-T** 📦 Git 提交：`[AngelscriptTest/Preprocessor] Test: cover fatal parse barrier and resolver atomicity`

- [ ] **P3.2** 为缺失/读失败脚本建立显式 `source state`，并把 unresolved provider 诊断前移为 line-aware 的 import 合同
  - 当前 `AddFile()` 把 `bTreatAsDeleted` 与同步读失败都折叠成“空 `RawCode`”，`Preprocess()` 仍会为这些文件生成空 `CodeSection`；随后 `CompileModules()` 只要同名 `ModuleDesc` 存在就把它视为 provider，找不到时又统一在模块第 1 行报错，`SwapInModules()` 还会把任何编译结果无条件写回 `ActiveModules`。
  - 这一项要把“模块存在”从“有同名 descriptor”升级成“源码状态有效且可导入”：为 `FFile` / `FAngelscriptModuleDesc` 增加 `Ok / Missing / ReadFailed` 之类的显式状态，import 查找必须拒绝 bad provider，并直接使用 import 的源行号报 `provider missing or unreadable`；hot reload 路径对 read-failed 模块保持旧代码存活，对真实删除模块走显式移除，而不是 swap-in 空模块覆盖旧状态。
  - 同时需要把 `FImport.FileLineNumber` 带进最终错误面，不再继续依赖 `Module->ImportedModules` 这份纯字符串列表和 `ScriptCompileError(Module, 1, ...)` 的退化路径。
  - 来源：
    - [A] `Documents/AutoPlans/Preprocessor_Analysis.md` — “发现 11/14/34/42：读失败被当成删除、unresolved import 丢失源行号、失败模块通过公开 API 泄漏、缺失 provider 还会让下游继续编译”
    - [B] `Documents/AutoPlans/DiscoveryPlans/Preprocessor_Plan.md` — “Issue-39：missing/read-failed source state 必须显式建模，并禁止坏 provider 参与 import 命中与正常 swap-in”
    - [E] `Documents/AutoPlans/ReferenceComparison/CrossComparison.md` — “[D4-Deep-2] 当前插件的优势是 failure memory + keep-old-code，不应把坏脚本 swap-in 成空模块覆盖旧行为”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` L105-L137、L289-L304 — 删除/读失败仍落成空源码并生成 `CodeSection`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h` L1302-L1303 与 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` L3175-L3194、L2907-L2938、L4944-L4956 — module desc 只保留字符串 `ImportedModules`，缺失 provider 统一报第 1 行，且 `SwapInModules()` 无条件回写 `ActiveModules`；`Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/ClassGeneratorTests.cpp` L53-L55 — 当前测试仍把“空模块默认可 soft reload”当成统一合同。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptModuleSourceStateTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/ClassGeneratorTests.cpp`
- [ ] **P3.2** 📦 Git 提交：`[AngelscriptRuntime/Core] Fix: treat missing providers as invalid import sources`
- [ ] **P3.2-T** 单元测试：锁住 missing/read-failed provider 的 line-aware 诊断与 keep-old-code 语义
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptModuleSourceStateTests.cpp`
  - 测试场景：
    - 正常路径：provider 源码可读时，consumer 的 module import 仍能成功解析、编译并参与正常 swap-in。
    - 边界条件：provider 被删除或读失败时，consumer 在 import 所在行拿到 `missing or unreadable provider` 诊断，旧 `ActiveModules` 继续保留，上游空模块不会被当成可导入模块。
    - 错误路径：`ProviderMissing -> Consumer -> DownstreamConsumer` 链中，首个缺失 provider 会阻断整个依赖链进入 stage1 / swap-in，不再把错误拖延成后续缺类型或缺函数噪音。
  - 测试命名：`Angelscript.TestModule.HotReload.Import.MissingProviderKeepsOldModule`、`Angelscript.TestModule.Compiler.Import.UnresolvedProviderReportsImportLine`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P3.2-T** 📦 Git 提交：`[AngelscriptTest/HotReload] Test: cover module source-state and missing-provider diagnostics`

- [ ] **P3.3** 将 declared function import 的校验/绑定从 automatic module import 开关中解耦，并把 hot reload 后的 rebind 变成固定收口步骤
  - 当前预处理器在识别 `import` 时直接用 `Contains("(")` 把 declared function import 排除出 `File.Imports`；编译后的 `CheckFunctionImportsForNewModules()` 与 swap-in 之后的 `ResolveAllDeclaredImports()` 又都被 `!ShouldUseAutomaticImportMethod()` 包住。默认 automatic-import 配置下，declared function import 会停留在 AngelScript 模块内部的未绑定占位状态。
  - 这一项要明确“automatic module import 只是模块依赖发现策略，不得顺带关闭语言层 function import 生命周期”：无论 automatic/manual module import 模式如何，都要在编译完成后校验 imported function 来源模块与签名，并在 swap-in 之后统一执行 rebind；hot reload 成功后也要重新解析这类 imported function，避免旧绑定悬挂。
  - 若需要保留兼容开关，也必须是独立的 `ShouldResolveDeclaredFunctionImports()` 或等价合同，而不是继续复用 module import 的模式位。
  - 来源：
    - [A] `Documents/AutoPlans/Preprocessor_Analysis.md` — “发现 19：automatic import 默认模式完全跳过 declared function import 的校验与绑定”
    - [B] `Documents/AutoPlans/DiscoveryPlans/Preprocessor_Plan.md` — “Issue-10：declared function import 需要与 automatic module import 解耦，并在编译/热重载后统一校验与绑定”
    - [E] `Documents/AutoPlans/ReferenceComparison/CrossComparison.md` — “[D4] reload 后缺少显式 rebind 通知；当前 reload contract 应补上稳定的 rebind 闭环”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` L3497-L3510 — 仅“不含 `(`”的 import 才进入 `File.Imports`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` L3865-L3872、L4057-L4063、L4620-L4694 — imported-function 校验与 `ResolveAllDeclaredImports()` 都被 automatic-import 模式短路，且失败诊断仍退化到模块第 1 行；`Plugins/Angelscript/Source/AngelscriptTest/Internals/AngelscriptBuilderTests.cpp` L168-L177 — 现有测试只证明 imported function 在绑定前处于未解析状态。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Compiler/AngelscriptDeclaredFunctionImportTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Internals/AngelscriptBuilderTests.cpp`
- [ ] **P3.3** 📦 Git 提交：`[AngelscriptRuntime/Core] Fix: always validate and bind declared function imports`
- [ ] **P3.3-T** 单元测试：锁住 automatic-import 下的 declared function import 编译、绑定与热重载 rebind
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Compiler/AngelscriptDeclaredFunctionImportTests.cpp`
  - 测试场景：
    - 正常路径：`automatic import = true` 时，consumer 对 source module 的 declared function import 仍会在编译完成后绑定成功并可执行。
    - 边界条件：`automatic import = true/false` 两种模式下，首次编译与 hot reload 后的 imported function 都会重新绑定到当前 source module 实现。
    - 错误路径：source module 缺失或函数签名不匹配时，编译/热重载阶段直接给出明确诊断，不再把错误拖到运行时首次调用。
  - 测试命名：`Angelscript.TestModule.Compiler.DeclaredImports.AutomaticModeBindsFunctions`、`Angelscript.TestModule.Compiler.DeclaredImports.HotReloadRebindsImportedFunction`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P3.3-T** 📦 Git 提交：`[AngelscriptTest/Compiler] Test: cover declared function imports under automatic import`

## 补充测试矩阵

| 改进项 | 测试文件 | 测试场景 | 优先级 |
|--------|---------|---------|--------|
| P3.1 | `Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorImportFailureTests.cpp` | fatal directive error + import、cycle import half-state、resolver atomicity | P0 |
| P3.2 | `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptModuleSourceStateTests.cpp` | missing/read-failed provider、line-aware unresolved import、keep-old-code | P0 |
| P3.3 | `Plugins/Angelscript/Source/AngelscriptTest/Compiler/AngelscriptDeclaredFunctionImportTests.cpp` | automatic-import 下的 declared function import 绑定、hot reload rebind、签名失配诊断 | P0 |

## 补充验收项

7. fatal preprocessor error 与 import resolver error 都不会再提交半成品 `ImportedModules`、blank 结果或排序状态。
8. 删除/读失败脚本不会继续充当合法 provider，也不会以空模块形式覆盖旧 `ActiveModules`；unresolved import 诊断必须落到真实 import 行。
9. declared function import 在 `automatic import` 与 `manual import` 模式下都能完成编译期校验、swap-in 后 rebind 与 hot reload 复绑。

## 补充风险与行为变化

### 风险

4. **把 bad provider 从“空模块占位”改成显式失败，会放大历史上被空模块掩盖的下游错误**
   - 某些脚本过去依赖“provider 读失败但 consumer 继续编译”的偶然行为；修复后会更早暴露真正的 import 缺失。
   - **缓解**：诊断必须带 import 行号、provider 状态和保留/丢弃旧模块的原因，避免只看到更早失败却不知道为什么。

5. **declared function import 与 automatic-import 解耦后，热重载会新增一条必须维持的 rebind 路径**
   - 如果只恢复首次编译时的绑定，不同步覆盖 swap-in 后 rebind，仍会留下“首次可用、热重载后悬挂”的半修复状态。
   - **缓解**：同一批测试同时覆盖首次编译与 hot reload，且要求绑定结果可执行，不只检查 `GetImportedFunctionCount()`。

### 已知行为变化

5. **parse/import 失败将停止在第一个阶段边界，不再留下被部分 blank 的 `import` 文本或半成品依赖列表**
   - 影响文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` L228-L243、L439-L497。

6. **缺失或读失败的 provider 将不再被视为“可导入但内容为空”的模块**
   - 影响文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` L105-L137、L289-L304；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` L3175-L3194、L2907-L2938、L4944-L4956。

7. **declared function import 将在默认 automatic-import 配置下恢复编译期校验与 reload 后 rebind**
   - 影响文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` L3865-L3872、L4057-L4063、L4620-L4694；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` L3497-L3510。

---

## 本轮补充（2026-04-09，第二轮）

### Phase 4：固定预处理 contract 与跨 root 模块身份

- [ ] **P4.1** 收口 `FAngelscriptPreprocessContext` 与 hook 边界，阻断 ambient engine state 和 post-error 副作用继续污染 artifact
  - 当前 `FAngelscriptPreprocessor` 的输入仍然分散在构造函数、`Preprocess()` 和静态 hook 上：构造时直接读取“当前上下文”的 `EDITOR/EDITORONLY_DATA`、settings 默认值，`Preprocess()` 再次从 `FAngelscriptEngine::Get()` 抓 `ConfigSettings`，而 `OnProcessChunks` / `OnPostProcessCode` 仍以可变 `FAngelscriptPreprocessor&` 的形式向外广播。这样同一份源码在不同 engine/context/hook 顺序下可能产生不同输出，但 artifact/cached graph 里没有任何字段记录这些差异来源。
  - 这一项要引入显式 `FAngelscriptPreprocessContext` 与只读 trace surface：由 `FAngelscriptEngine` 在 compile/hot reload 入口一次性构造 `PreprocessorFlags`、automatic-import mode、editor/cooked mode、settings snapshot、hook version/context hash，再注入 preprocessor；public hook 拆成只读 observer，确有改写需求时改为显式 pass registry。与此同时，把“中后段 fatal 后立即停止 hooks 与 `Module->Code` 提交”并入同一合同，确保失败时不再对外暴露半成品 preprocessor 状态。
  - 这不是单纯的测试便利性优化，而是为后续 `P2.1/P2.2` 的 include graph、ordered artifact 与 stale-cache 判定补齐“输入身份”这一缺失前提；否则即使 module graph 本身稳定，context 漂移仍会让 live/precompiled/cache 命中条件继续失真。
  - 来源：
    - [A] `Documents/AutoPlans/Preprocessor_Analysis.md` — “发现 33：中后段报错后不会 fail-fast，`OnProcessChunks` / `OnPostProcessCode` 仍会运行并把 `ProcessedCode` 写回 `Module->Code`”
    - [B] `Documents/AutoPlans/DiscoveryPlans/Preprocessor_Plan.md` — “Issue-2：缺少显式 `FAngelscriptPreprocessContext`，preprocessor 直接依赖全局 engine 状态与可变 hook，缓存与增量预处理没有稳定输入边界”
    - [C] `Documents/AutoPlans/TestCoverage/Preprocessor_TestGaps.md` — “Issue-3：现有 preprocessor 测试依赖当前运行引擎，环境不稳定；NewTest-31：public preprocess hook 的阶段顺序与状态暴露需要独立回归”
    - [D] `Documents/AutoPlans/ArchitectureReview/ModuleStructure_ArchReview.md` — “runtime 侧应保持 deterministic compile-pipeline contract，并把 editor/test hook 收敛到受控 support surface，而不是继续暴露 runtime 内部可变状态”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h` L5-L30、`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` L38-L73、L212-L307 — public header 直接依赖 `AngelscriptEngine.h` / `AngelscriptSettings.h`，构造与 `Preprocess()` 继续读取当前 engine/settings，且中后段出错后仍会广播可变 hook 并写入 `Module->Code`。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessorContracts.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorContextTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningPreprocessorTraceTests.cpp`
- [ ] **P4.1** 📦 Git 提交：`[AngelscriptRuntime/Preprocessor] Refactor: add explicit preprocess context and readonly hook contract`
- [ ] **P4.1-T** 单元测试：锁住 `PreprocessContext` 稳定性、hook 顺序与 fatal 后停止提交的合同
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorContextTests.cpp`
  - 测试场景：
    - 正常路径：同一脚本在同一 `FAngelscriptPreprocessContext` 下重复运行，两次 `ProcessedCode`、canonical dependency 结果和 `PreprocessContextHash` 完全一致，hook 顺序固定为 parse/process/postprocess。
    - 边界条件：切换 automatic-import 或 editor/cooked flag 后，context hash 与 artifact fingerprint 必须变化；trace hook 仍能观察阶段数据，但不能再直接改写 `Files` / `Module->Code`。
    - 错误路径：宏/条件编译 fatal 发生在 `AnalyzeClasses`/`ProcessMacros` 等中后段时，`OnPostProcessCode` 不应继续触发，且失败后不产生可继续编译的 `Code` artifact。
  - 测试命名：`Angelscript.TestModule.Preprocessor.Context.StableHashWithinSameContext`、`Angelscript.TestModule.Preprocessor.Hooks.ContextChangeInvalidatesArtifact`、`Angelscript.TestModule.Preprocessor.Hooks.FatalErrorSkipsPostProcessCommit`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P4.1-T** 📦 Git 提交：`[AngelscriptTest/Preprocessor] Test: cover preprocess context determinism and hook sequencing`

- [ ] **P4.2** 引入 root-aware `FAngelscriptModuleIdentity` 与 canonical filename index，统一 live / hot reload / fully precompiled 的按文件定位合同
  - 当前模块身份仍主要由字符串拼接决定：脚本发现阶段对每个 root 都以 `RelativeRoot = ""` 递归收集，`AddFile()` 只从 `RelativeFilename` 直接生成 `ModuleName`，`GetModuleByFilename()` 在直匹配失败后会对任意同盘路径做 `MakePathRelativeTo_IgnoreCase()` 再尝试推导模块名，而 fully precompiled restore 只回填 `ModuleName + CodeHash + ImportedModules`，完全丢掉 section/root provenance。
  - 这一项要新增轻量值对象 `FAngelscriptModuleIdentity`，至少统一 `RootKey`、`CanonicalRelativePath`、`CanonicalModuleName`、`LookupKeyLower`、`CanonicalAbsoluteFilename` 与 display name，并让 `FindAllScriptFilenames()`、`Preprocessor.AddFile()`、`ActiveModules`、hot reload 状态表、`GetModuleByFilename()`、precompiled schema 全部消费同一 identity，而不是继续各自重复 `.Replace(".as")`、relative-to-root 与 case/slash 处理。
  - 第一阶段先修 root containment 与 canonical filename lookup：`GetModuleByFilename()` 输入先 canonicalize，只有真实位于 script root 内的文件才允许做 root-relative fallback；第二阶段再把 project/plugin root、fully precompiled restore、debug breakpoint bookkeeping 与 hot reload file-state 一起迁到统一 identity/index 上，彻底消掉“project script 能按文件找，plugin/precompiled script 只能靠 module-name 或 filename 字符串兜底”的双轨行为。
  - 来源：
    - [A] `Documents/AutoPlans/Preprocessor_Analysis.md` — “发现 38/41/49/69：脚本收集边界丢失 script root 身份，fully precompiled 缺少 section/root provenance 导致 plugin 脚本 `GetModuleByFilename()` 失效，预处理 path-to-module 又与 runtime filename lookup 的 slash/case 语义脱节”
    - [B] `Documents/AutoPlans/DiscoveryPlans/Preprocessor_Plan.md` — “Issue-54：模块系统缺少跨层共享的 `ModuleIdentity`；Issue-58：fully precompiled 启动下按文件定位模块对 plugin 脚本退化；Issue-68：`GetModuleByFilename()` 不校验 root 边界且不先 canonicalize 输入”
    - [D] `Documents/AutoPlans/ArchitectureReview/ModuleStructure_ArchReview.md` — “runtime 侧应保持 deterministic script-root / compile-pipeline contract，不应把 loader-style 宽松路径解释继续回灌到核心模块系统”
    - [E] `Documents/AutoPlans/ReferenceComparison/CrossComparison.md` — “D11：当前 Angelscript 的优势是 deterministic project/plugin roots 与稳定 operator surface，不应回退到自由 loader 或 root-agnostic filename fallback”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` L2004-L2013、L3029-L3055、`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` L91-L103、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp` L2773-L2783 — 所有 script root 都以空 `RelativeRoot` 进入收集，`AddFile()` 只保存 `RelativeFilename/ModuleName`，`GetModuleByFilename()` 对 root 外路径仍会尝试 module-name fallback，precompiled restore 也没有恢复任何 section/root provenance。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/FileSystem/AngelscriptModuleIdentityTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerBreakpointTests.cpp`
- [ ] **P4.2** 📦 Git 提交：`[AngelscriptRuntime/Core] Refactor: introduce root-aware module identity and filename index`
- [ ] **P4.2-T** 单元测试：锁住 project/plugin root、fully precompiled 与按文件定位共享同一 identity 的合同
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/FileSystem/AngelscriptModuleIdentityTests.cpp`
  - 测试场景：
    - 正常路径：project root 下脚本经 live preprocess 后，可用 canonical absolute path、canonical module name 与 `GetModuleByFilenameOrModuleName()` 三条入口命中同一 `ModuleIdentity`。
    - 边界条件：project root 与 plugin root 各有同名相对路径脚本时，两者保留不同 `RootKey` 与 canonical filename index；生成 precompiled cache 并恢复后，按两个绝对路径仍能稳定命中各自模块。
    - 错误路径：root 外路径、`..`/混合分隔符别名路径或缺少 provenance 的旧 cache 必须得到明确 reject/stale 诊断，并回退到 `ModuleName` fallback 或 live preprocess，而不是构造带 `../` 的伪模块名。
  - 测试命名：`Angelscript.TestModule.FileSystem.ModuleIdentity.LiveAndFilenameLookupAgree`、`Angelscript.TestModule.FileSystem.ModuleIdentity.ProjectAndPluginRootsStayDistinct`、`Angelscript.TestModule.FileSystem.ModuleIdentity.OffRootFilenameFallsBackSafely`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P4.2-T** 📦 Git 提交：`[AngelscriptTest/FileSystem] Test: cover root-aware module identity and filename lookup`

- [ ] **P4.3** 抽离 `FAngelscriptPreprocessedModuleArtifact` / `FAngelscriptCompiledModuleState`，解除“一 source file = 一 `ModuleDesc`”与 runtime-state 混写假设
  - 当前 `AddFile()` 每接收一个源文件就立即创建一份 `FAngelscriptModuleDesc`，`GetModulesToCompile()` 只靠 shared-ref `AddUnique()` 去重，等于把“source unit ownership”和“module ownership”在 API 层写成了 1:1；与此同时，`FAngelscriptModuleDesc` 又同时保存 `Code` / `ImportedModules` / `UsageRestrictions` 这类预处理产物和 `ScriptModule` / `PrecompiledData` / `bCompileError` / `bLoadedPrecompiledCode` 这类运行时状态。
  - 这会直接卡住 `P2.1` 的 include graph 和 future multi-section module：只要底层模型仍是“文件直接产出可变 `ModuleDesc`”，include 展开、artifact cache、section provenance、失败重试和 runtime state 就只能继续围绕一个不断被叠写的大对象打补丁。对外 public API 还通过 `AngelscriptPreprocessor.h` 直接暴露 `Engine.h` / `ModuleDesc`，让测试与其他消费者继续依赖 runtime 内部布局。
  - 这一项要先把 preprocessor 输出改成不可变 artifact 层，例如 `FSourceUnit` -> `FAngelscriptPreprocessedModuleArtifact` / `FModuleAssembly`，再让编译期单独创建 `FAngelscriptCompiledModuleState`；单文件路径先通过桥接保持兼容，多 source section 与 include graph 再顺着同一聚合层落位。public header 也同步收窄到 contracts 层，不再继续把 engine/settings/module runtime state 暴露成 preprocessor contract 的一部分。
  - 来源：
    - [A] `Documents/AutoPlans/Preprocessor_Analysis.md` — “发现 5/34/40：显式 import 仍通过复制整份 `FFile` 排序，`GetModulesToCompile()` 会在失败后暴露半成品 `ModuleDesc`，fully precompiled 恢复出的 descriptor 也天然比 live preprocess 更瘦”
    - [B] `Documents/AutoPlans/DiscoveryPlans/Preprocessor_Plan.md` — “Issue-70/71/72：`AddFile()` 到 `GetModulesToCompile()` 把 ‘一个源文件 = 一个模块’ 与 runtime state 写死，`FAngelscriptModuleDesc` 同时承载 artifact/cache/runtime，public API 又直接依赖 `Engine`/`Settings`”
    - [D] `Documents/AutoPlans/ArchitectureReview/ModuleStructure_ArchReview.md` — “runtime 内部实现与 white-box hook 应通过受控 support surface 暴露，而不是继续让 test/consumer 直接依赖 runtime 内部目录与重 public edge”
    - [E] `Documents/AutoPlans/ReferenceComparison/CrossComparison.md` — “D6：当前 Angelscript 最值得吸收的是单一 artifact authority，而不是让 generator/runtime/debugger/cache 各自维护一份 module/source 合同”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h` L5-L21、`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` L75-L99、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h` L1272-L1327 — preprocessor public API 直接返回 `FAngelscriptModuleDesc`，`AddFile()` 为每个文件立刻创建 `ModuleDesc`，而 `FAngelscriptModuleDesc` 同时混装 preprocessor artifact 和 runtime compile state。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessorContracts.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorArtifactTests.cpp`
- [ ] **P4.3** 📦 Git 提交：`[AngelscriptRuntime/Preprocessor] Refactor: split preprocessed artifacts from compiled module state`
- [ ] **P4.3-T** 单元测试：锁住 artifact 组装、单文件兼容与失败不创建 runtime state 的桥接合同
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorArtifactTests.cpp`
  - 测试场景：
    - 正常路径：单文件脚本仍经 `PreprocessedModuleArtifact -> CompiledModuleState` 桥接成功编译，行为与现有单文件 module 路径保持一致。
    - 边界条件：两个 source unit 归并到同一 module artifact 时，section 顺序、source provenance 与 canonical dependency 列表稳定保留，为 include graph/multi-section 模块提供固定落点。
    - 错误路径：预处理失败只暴露 diagnostics artifact，不创建 `ScriptModule` / compiled state，也不会让 `GetModulesToCompile()` 向外泄漏可继续编译的半成品模块。
  - 测试命名：`Angelscript.TestModule.Preprocessor.Artifact.SingleFileBridgeRemainsCompatible`、`Angelscript.TestModule.Preprocessor.Artifact.MultiSectionAssemblyKeepsOrder`、`Angelscript.TestModule.Preprocessor.Artifact.FailureDoesNotCreateCompiledState`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P4.3-T** 📦 Git 提交：`[AngelscriptTest/Preprocessor] Test: cover preprocessed artifact assembly contract`

## 补充测试矩阵

| 改进项 | 测试文件 | 测试场景 | 优先级 |
|--------|---------|---------|--------|
| P4.1 | `Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorContextTests.cpp` | context hash 稳定性、hook 顺序、fatal 后停止提交 artifact | P1 |
| P4.2 | `Plugins/Angelscript/Source/AngelscriptTest/FileSystem/AngelscriptModuleIdentityTests.cpp` | project/plugin root identity、fully precompiled filename lookup、off-root fallback | P0 |
| P4.3 | `Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorArtifactTests.cpp` | artifact/runtime state 分层、multi-section assembly、failure 不暴露半成品模块 | P1 |

## 补充验收项

10. 预处理输出必须显式携带 `PreprocessContextHash` 或等价上下文身份，同一源码在相同 context 下重复运行得到完全一致的 artifact，而 context 变化会稳定触发 artifact/cache 失效。
11. project root、plugin root、live preprocess、hot reload、fully precompiled restore 与 `GetModuleByFilename()` 必须共享同一份 `ModuleIdentity`，按文件定位模块不再依赖宽松的 root-relative 字符串兜底。
12. `FAngelscriptPreprocessor` 对外不再直接暴露可变 runtime `ModuleDesc` 作为主 artifact；失败路径不会再把半成品模块通过 `GetModulesToCompile()` 或 hook surface 泄漏给下游。

## 补充风险与行为变化

### 风险

6. **`PreprocessContext` 一旦进入 cache key，会把历史上未被建模的环境差异全部显式化**
   - 一些过去“同文件偶然复用同一缓存”的场景，在 context hash 落地后会主动 miss，看起来像缓存命中率下降。
   - **缓解**：同时输出 context 组成项与 cache miss reason，先证明 miss 来自真实环境差异，而不是引入了不必要的波动字段。

7. **引入 root-aware `ModuleIdentity` 后，跨 root 同名脚本与宽松 filename fallback 会从模糊匹配变成显式错误**
   - 过去靠 `ModuleName`、`../` 别名路径或 project-root fallback 勉强工作的一些调试/工具路径，会被更严格的 root containment 合同拒绝。
   - **缓解**：保留一段迁移期兼容日志，把“原始路径 -> canonical path -> identity/root reject reason”打全，帮助工具侧更新调用方式。

8. **artifact/runtime state 分层是数据模型级改造，迁移期最容易出现 bridge 双写不同步**
   - 如果旧 `FAngelscriptModuleDesc` 可写路径与新 artifact/state 继续并存过久，include graph、cache 与 diagnostics 可能出现一半读旧对象、一半读新对象的过渡态。
   - **缓解**：先把旧 `ModuleDesc` 降成只读 façade 或桥接层，明确单向数据流，再逐步迁移 compile/cache/debug 消费点。

### 已知行为变化

8. **预处理 hook 将从“可变全局扩展点”收敛为显式 context + 只读 observer / 受控 pass**
   - 影响文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h` L5-L30；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` L38-L73、L212-L307。

9. **按文件定位模块将开始强制 canonical path 与 root containment，不再把 root 外路径或别名路径静默映射成模块名**
   - 影响文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` L2004-L2013、L3029-L3055；`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp` L2773-L2783。

10. **`FAngelscriptPreprocessor` 的主输出将从可变 `FAngelscriptModuleDesc` 转向不可变 artifact，再由编译阶段创建 runtime state**
   - 影响文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h` L5-L21；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` L75-L99；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h` L1272-L1327。

---

## 本轮补充（2026-04-09，第三轮）

### Phase 5：收口模块名编码歧义与保留目录语义

- [ ] **P5.1** 对带字面 `.` 的 relative path segment 建立 fail-fast 合同，阻断“物理路径段”和“dotted module namespace”继续复用同一编码
  - 现有 `P1.3` 已覆盖 `.as` 扩展、backslash 与 duplicate-module gate，但还没有显式收紧更基础的路径编码歧义：只要目录段或文件基名里本身带字面 `.`，当前 path-to-module 规则仍会把它和真正的模块层级分隔符混在一起，例如 `Gameplay.UI/Inventory.as`、`Gameplay/UI.Inventory.as` 与 `Gameplay/UI/Inventory.as` 仍可能压成同一个 `Gameplay.UI.Inventory`。
  - 这一项要把“生成 module name”升级成共享验证入口，例如 `TryBuildModuleNameFromRelativePath(...)`：统一做 slash-normalization、只移除 leaf extension，并显式拒绝任何包含字面 `.`、空段、`.` 或 `..` 的路径段。`AddFile()`、`GetModuleByFilename()` 的 filename fallback 与 compile 前 duplicate gate 都只能消费这份验证后的结果，不再继续各自 `Replace(".as")` / `Replace("/", ".")`。
  - 目标不是只补一个路径合法性判断，而是把 module identity 从“依赖模糊字符串压缩”收紧为“先验证、后建模”的稳定合同；否则后续 `ModuleIdentity`、precompiled provenance 与 filename lookup 仍会建立在非单射 key 上。
  - 来源：
    - [A] `Documents/AutoPlans/Preprocessor_Analysis.md` — “发现 10：`.as` 全局替换已经让路径到模块名不是单射，路径段里的字面 `.` 会继续制造额外碰撞入口”
    - [B] `Documents/AutoPlans/DiscoveryPlans/Preprocessor_Plan.md` — “Issue-73：路径段里的字面 `.` 与模块层级分隔符复用同一编码，必须先 fail-fast 非法路径编码”
    - [D] `Documents/AutoPlans/ArchitectureReview/ModuleStructure_ArchReview.md` — “runtime 侧应保持 deterministic script-root / compile-pipeline contract，不应继续依赖含糊的字符串推导”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` L86-L99 — `FilenameToModuleName()` 仍直接 `Replace(".as")` 和 `Replace("/", ".")`，`AddFile()` 也立刻用它构造 `ModuleName`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` L1958-L1963、L3043-L3055、L3133-L3135 — 脚本发现阶段允许带 `.` 的目录原样进入 `RelativePath`，`GetModuleByFilename()` 复用同一压缩规则，编译阶段对重复模块名仍只有 `ensureMsgf` 而非结构化错误。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorPathTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/FileSystem/AngelscriptFileSystemTests.cpp`
- [ ] **P5.1** 📦 Git 提交：`[AngelscriptRuntime/Preprocessor] Fix: reject ambiguous dotted relative path segments`
- [ ] **P5.1-T** 单元测试：锁住 dotted path segment 的 fail-fast 与 filename lookup 一致性
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorPathTests.cpp`
  - 测试场景：
    - 正常路径：`Gameplay/UI/Inventory.as` 这类不含歧义路径仍按既有规则生成 `Gameplay.UI.Inventory`。
    - 边界条件：混合分隔符但不含字面 `.` 的相对路径、以及带 `_`/数字的合法路径段仍能稳定归一到同一 module identity。
    - 错误路径：`Gameplay.UI/Inventory.as`、`Gameplay/UI.Inventory.as`、`Gameplay/./Inventory.as` 或 `Gameplay/../Inventory.as` 必须在预处理阶段直接失败；`GetModuleByFilename()` 对这些非法路径也不得再静默映射到现有模块。
  - 测试命名：`Angelscript.TestModule.Preprocessor.Paths.DottedSegmentsFailFast`、`Angelscript.TestModule.FileSystem.ModuleIdentity.IllegalRelativeSegmentsDoNotResolve`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P5.1-T** 📦 Git 提交：`[AngelscriptTest/Preprocessor] Test: cover ambiguous dotted path segments`

- [ ] **P5.2** 统一 `Editor/Dev/Examples` 保留目录与 `isEditorOnlyModule` / developer-only 判定的 ignore-case 语义，避免大小写不敏感文件系统泄漏脚本环境边界
  - 现有 Plan 已覆盖 root-aware `ModuleIdentity` 与 canonical filename，但仍未单独收口“保留目录语义”这条跨 discovery / compile / class generation 的链路：脚本发现只在目录名精确等于 `Examples`、`Dev`、`Editor` 时跳过，builder 只在 `ModuleName.StartsWith("Editor.")` 或 `Contains(".Editor.")` 时标 editor-only，`ASClass.cpp` 也只认 `Dev.` / `Editor.` 大写前缀。Windows 这类 case-insensitive 文件系统上，`editor/`、`dev/`、`examples/` 会因此绕过环境隔离。
  - 这一项要提取共享 helper，例如 `IsReservedScriptDirectory(...)` / `HasReservedModuleSegment(...)`：对路径段与模块名分段统一做 ignore-case 判定，并把结果写成显式 module environment metadata。`FindScriptFiles()`、`CompileModule_Types_Stage1()`、`ASClass.cpp` 的 developer/editor-only 检查都只能消费同一 helper，不再继续从显示名大小写里猜语义。
  - 如果项目确实依赖保留目录大小写规范，也应该把“大小写不一致”降为 warning 或冲突诊断，而不是继续让不同平台用不同结果解释同一脚本树。
  - 来源：
    - [A] `Documents/AutoPlans/Preprocessor_Analysis.md` — “发现 68/70：`Editor/Dev/Examples` 目录过滤与 `isEditorOnlyModule` 判定都大小写敏感，且当前没有自动化锁住模块分类漂移”
    - [B] `Documents/AutoPlans/DiscoveryPlans/Preprocessor_Plan.md` — “Issue-60：保留目录语义需要统一 ignore-case helper，discovery 与 builder/class generation 不能各自猜测”
    - [D] `Documents/AutoPlans/ArchitectureReview/ModuleStructure_ArchReview.md` — “runtime 侧应保持 deterministic script-root / compile-pipeline contract，把环境边界收敛成稳定规则而不是平台相关偶然行为”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` L86-L88 — module name 仍保留原始路径大小写；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` L1973-L1985、L2001-L2013 — discovery 仅对精确大小写的 `Examples/Dev/Editor` 目录跳过；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` L4353-L4356 与 `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp` L1531-L1532 — builder 的 `isEditorOnlyModule` 和 class generation 的 developer/editor-only 判定都只认大小写敏感的 `Editor.` / `Dev.` 前缀。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/FileSystem/AngelscriptFileSystemTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Compiler/AngelscriptCompilerPipelineTests.cpp`
- [ ] **P5.2** 📦 Git 提交：`[AngelscriptRuntime/Core] Fix: normalize reserved script directory semantics`
- [ ] **P5.2-T** 单元测试：锁住 reserved-dir ignore-case 发现、编译与 developer/editor-only 分类
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/FileSystem/AngelscriptFileSystemTests.cpp`
  - 测试场景：
    - 正常路径：`Editor/`、`Dev/`、`Examples/` 既有目录在 editor/runtime 不同上下文下继续保持当前预期的发现与分类结果。
    - 边界条件：`editor/`、`dEv/`、`EXAMPLES/` 等 mixed-case 目录与 canonical 写法表现完全一致，builder/class generator 看到的 editor-only / developer-only 标记也一致。
    - 错误路径：在大小写敏感 fixture 中同时存在 `Editor/` 与 `editor/` 时，系统必须给出冲突诊断或明确拒绝，而不是按枚举顺序静默选边。
  - 测试命名：`Angelscript.TestModule.FileSystem.ReservedDirectoriesIgnoreCase`、`Angelscript.TestModule.Compiler.ModuleEnvironment.EditorDirectoryCasingSetsFlags`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P5.2-T** 📦 Git 提交：`[AngelscriptTest/FileSystem] Test: cover reserved directory casing semantics`

## 补充测试矩阵

| 改进项 | 测试文件 | 测试场景 | 优先级 |
|--------|---------|---------|--------|
| P5.1 | `Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorPathTests.cpp` | dotted segment fail-fast、非法相对段拒绝、filename lookup 不再静默映射 | P0 |
| P5.2 | `Plugins/Angelscript/Source/AngelscriptTest/FileSystem/AngelscriptFileSystemTests.cpp` | reserved-dir ignore-case、editor/developer-only 标记一致、case-sensitive 冲突诊断 | P1 |

## 补充验收项

13. 带字面 `.`、空段、`.` 或 `..` 的相对脚本路径不得再静默压缩成合法 `ModuleName`；模块创建前必须给出明确路径诊断。
14. `Editor/Dev/Examples` 的保留目录语义必须在 discovery、builder、class generation 与 filename lookup 上保持 ignore-case 一致，不再随平台大小写规则漂移。

## 补充风险与行为变化

### 风险

9. **仓库外若已有 dotted path segment 脚本树，修复后会从“静默碰撞”升级成显式失败**
   - 过去可能只是偶发 duplicate-module 或错误 lookup；收紧后会在预处理入口直接报错。
   - **缓解**：诊断里同时输出原始相对路径、冲突段名与建议重命名方案；必要时在 guide 中补迁移说明。

10. **保留目录语义改成 ignore-case 后，大小写敏感平台上 `Editor/` 与 `editor/` 不再可同时作为不同业务目录存在**
   - 这会把过去的平台相关偶然行为变成统一规则，短期可能暴露历史脚本树命名问题。
   - **缓解**：对双写法并存场景输出冲突诊断，并在迁移期保留明确日志，帮助整理目录结构。

### 已知行为变化

11. **带歧义的 dotted relative path segment 将在模块创建前被拒绝，不再进入 duplicate-module 或 filename fallback 的后置路径**
   - 影响文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` L86-L99；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` L3043-L3055、L3133-L3135。

12. **`editor/`、`dev/`、`examples/` 等 mixed-case 保留目录将继承与 `Editor/Dev/Examples` 相同的环境语义**
   - 影响文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` L1973-L1985、L4353-L4356；`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp` L1531-L1532。

---

## 深化（2026-04-09 01:06:05）

### Phase 6：补 script root 物理身份与 `#restrict usage` 合同空白

- [ ] **P6.1** 把 script root 收集从字符串路径升级为 canonical physical identity，并在递归扫描阶段引入 visited/去重守卫
  - 现有计划已经覆盖 `RootKey`、`ModuleIdentity` 与跨 root 同名脚本的逻辑身份，但还没有封住更前面的“同一物理目录/文件被重复枚举”入口。当前 `DiscoverScriptRoots()` 只做字符串级 `ScriptPath != RootPath` 比较，`FindScriptFiles()` 递归也没有 `visited` 集合；一旦 `Script/` 下出现 alias root、symlink/junction 指回祖先目录，或同一物理 root 通过不同规范化路径进入发现链，模块系统在进入 `Preprocessor.AddFile()` 前就已经拿到重复 source unit。
  - 这一项要把文件发现前移到统一 canonical path 层：先提取 `CanonicalizeScriptPath()` / `CanonicalizeScriptDirectory()`，统一做 `ConvertRelativePathToFull`、`CollapseRelativeDirectories`、`NormalizeFilename`，在能力允许时补 `realpath`/reparse-point 解析；然后让 `DiscoverScriptRoots()`、`FindScriptFiles()`、`FindAllScriptFilenames()` 与 hot reload 初始化都消费这份 canonical 结果。只有通过 physical identity 去重后的 `FFilenamePair` 才能继续流向 `Preprocessor.AddFile()`，避免后面的 `ModuleName`/`RelativePath` 账本从一开始就建立在重复文件集上。
  - 这不是单纯的 file-system 健壮性修补，而是现有 `ModuleIdentity` 计划的前置护栏：如果 discovery 层仍允许一份物理脚本以两个逻辑 root 或两个递归分支进入系统，那么后续 duplicate-module、hot reload、precompiled provenance 和 include owner 全都会被迫在错误输入上补救。
  - 来源：
    - [A] `Documents/AutoPlans/Preprocessor_Analysis.md` — “发现 38：脚本收集边界在进入 hot reload/module lookup 前就抹掉了 root 身份，文件身份被压窄成裸 `RelativePath`”
    - [B] `Documents/AutoPlans/DiscoveryPlans/Preprocessor_Plan.md` — “Issue-1：alias root、symlink/junction 环与重复 physical path 需要 canonical path 去重和 `visited` 守卫”
    - [E] `Documents/AutoPlans/ReferenceComparison/CrossComparison.md` — “D11：当前 Angelscript 的优势是 deterministic project/plugin roots，不应继续依赖隐式搜索顺序或别名路径”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` L1347-L1357、L1944-L2010、L2876-L2888；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` L91-L103 — plugin root 只按字符串比较并排序，递归扫描无 `visited`/physical 去重，hot reload 状态表又以 `RelativePath` 建 key，`AddFile()` 则立即把这一层字符串结果固化成 `ModuleName`。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/FileSystem/AngelscriptScriptRootDiscoveryTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningFileSystemAndModuleTraceTests.cpp`
- [ ] **P6.1** 📦 Git 提交：`[AngelscriptRuntime/Core] Fix: canonicalize script roots and guard directory loops`
- [ ] **P6.1-T** 单元测试：锁住 alias root、directory loop 与 duplicate physical script 的 discovery 合同
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/FileSystem/AngelscriptScriptRootDiscoveryTests.cpp`
  - 测试场景：
    - 正常路径：project root 与 plugin root 指向不同物理目录时，脚本发现结果保持稳定顺序，module identity 不受新 canonicalization 影响。
    - 边界条件：同一物理 root 通过两条规范化前不同、规范化后相同的路径进入发现链时，只保留一份 `FFilenamePair`，并输出明确去重日志或 warning。
    - 错误路径：构造指回祖先目录的 junction/symlink 或 fake file-tree 递归环，发现过程必须稳定终止，不得重复枚举同一物理 `.as`，也不得把重复脚本继续送入 `Preprocessor.AddFile()`。
  - 测试命名：`Angelscript.TestModule.FileSystem.ScriptRoots.AliasRootsDeduplicatePhysicalDirectories`、`Angelscript.TestModule.FileSystem.ScriptRoots.DirectoryLoopStopsRecursion`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P6.1-T** 📦 Git 提交：`[AngelscriptTest/FileSystem] Test: cover script root canonicalization and loop guards`

- [ ] **P6.2** 收口 `#restrict usage` directive parser，使 restriction 只来自活跃分支并共享统一的空白/comment 终止规则
  - 现有计划已经大规模覆盖 `import/include`，但 `#restrict usage` 仍缺一条与模块系统直接相关的 parser 合同。当前实现先用带字面空格的 `Strncmp("#restrict usage allow ")` / `Strncmp("#restrict usage disallow ")` 匹配 directive，然后立即调用 `ReadUntilWhitespace()` 取 pattern、`KillRawLine()` blank 原文，并在 `bIfDefStackIsFalse` 生效前就把 restriction 写入 `Module->UsageRestrictions`。这意味着 dead branch restriction 会泄漏，tab 分隔会完全跳过 directive，line comment/block comment 又会把注释尾巴写进 wildcard pattern。
  - 这一项要把 `#restrict usage` 接到共享 directive/cursor helper：统一识别空格与 tab，解析 `allow/disallow` 后的 statement span 时同时做 comment stripping 与 `TrimStartAndEnd()`，并在提交 restriction 前显式检查当前 branch 是否 active。若 directive 经过去注释后 pattern 为空，必须在当前行报明确语法错误，而不是继续写入空或脏 wildcard。
  - 同步给 restriction 元数据补最小 provenance，例如源文件、行号与 active/inactive 决策结果，为后续 `CheckUsageRestrictions()` 和 include/multi-section 诊断提供稳定输入；这样未来再收口 directive scanner 时，`#if` / `#restrict` / `#include` 不会继续各自维护一套不同的 whitespace/comment 语义。
  - 来源：
    - [A] `Documents/AutoPlans/Preprocessor_Analysis.md` — “发现 35/67/75：inactive branch 的 `#restrict usage` 会泄漏，tab 分隔 directive 被静默跳过，comment 尾巴会进入 `Restriction.Pattern`”
    - [B] `Documents/AutoPlans/DiscoveryPlans/Preprocessor_Plan.md` — “Issue-18/35/45：restriction 需要 active-branch gating，并共享 whitespace/comment-aware parser”
    - [C] `Documents/AutoPlans/TestCoverage/Preprocessor_TestGaps.md` — “NewTest-14/NewTest-15：tab 分隔与 dead-branch restriction 目前都没有自动化回归”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` L3363-L3402、L4318-L4325、L4349-L4358 — `#restrict usage` 先写入 `UsageRestrictions` 后才进入 inactive-branch blanking；`ReadUntilWhitespace()` 只在空格/`\t`/换行处停止；`KillRawLine()` 又在遇到 `/` 时提前停止，导致 comment span 与 pattern span 分叉。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorDirectiveTests.cpp`
- [ ] **P6.2** 📦 Git 提交：`[AngelscriptRuntime/Preprocessor] Fix: normalize restrict usage directive parsing`
- [ ] **P6.2-T** 单元测试：锁住 `#restrict usage` 的 active-branch、tab 与 comment 合同
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorDirectiveTests.cpp`
  - 测试场景：
    - 正常路径：活跃分支中的 `#restrict usage allow Gameplay.*` / `disallow Runtime.*` 被准确写入 `UsageRestrictions`，并从 `ProcessedCode` 中移除。
    - 边界条件：tab 分隔写法、行尾 `//` 注释与 `/* */` 注释版本都只记录纯 pattern，不影响 restriction 生效集合。
    - 错误路径：inactive branch 中的 restriction 不得泄漏；comment stripping 后 pattern 为空或 directive 体不合法时，必须给出 line-aware 语法错误并拒绝提交 restriction。
  - 测试命名：`Angelscript.TestModule.Preprocessor.Directives.RestrictUsage.ActiveBranchOnly`、`Angelscript.TestModule.Preprocessor.Directives.RestrictUsage.CommentAndWhitespaceNormalization`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P6.2-T** 📦 Git 提交：`[AngelscriptTest/Preprocessor] Test: cover restrict usage directive contracts`

- [ ] **P6.3** 让 `#restrict usage` 校验改成 eligible-module 集合，并补齐 fully precompiled 路径的 restriction/editor-only 元数据
  - 现有 live preprocess 会把 restriction 写进 `ModuleDesc`，但编译与 cache 两侧都还没有把它当成稳定合同。`CheckUsageRestrictions()` 在遍历当前批次和 `ActiveModules` 时，只要遇到一个 `ScriptModule == nullptr` 或 `Code.Num() == 0` 的模块就整函数 `return`；与此同时，`FAngelscriptPrecompiledData::GetModulesToCompile()` 恢复出来的 descriptor 只回填 `ModuleName`、`CodeHash`、`ImportedModules`、`PostInitFunctions`，根本不恢复 `Code`、`UsageRestrictions` 和 `EditorOnlyBlockLines`。结果是一次与 restriction 无关的 import/compile 失败，或一次 fully precompiled 恢复出来的空壳 module，都足以让整轮 restriction 校验静默失效。
  - 这一项要把 restriction 校验前移成显式 eligible-module 筛选：`CompileModules()` 先构造 `RestrictionEligibleModules`，只把 `!bCompileError`、`ScriptModule != nullptr`、拥有可用源码 provenance 的模块送进 `CheckUsageRestrictions()`；函数内部再把当前的三处 `return` 改成 `continue`，并记录跳过原因。与此同时，`FAngelscriptPrecompiledModule` 需要补序列化 `UsageRestrictions`、`EditorOnlyBlockLines` 与最小 section provenance，让 fully precompiled 路径恢复出的 descriptor 能与 live preprocess 共享同一 restriction/editor-only 合同。
  - 这也是当前 artifact/cache 分层计划里一个没被单独落下来的空白：如果 restriction 元数据不进入 cache，`Preprocess -> Compile -> Precompiled -> Debug/Validation` 之间就仍然存在两套不等价的 module descriptor，后续任何 include graph 或 module identity 修复都很难证明 live/cache 真正一致。
  - 来源：
    - [A] `Documents/AutoPlans/Preprocessor_Analysis.md` — “发现 40/43/45：precompiled descriptor 丢失 `Code`/`UsageRestrictions`/`EditorOnlyBlockLines`，`CheckUsageRestrictions()` 会被首个坏模块全局熔断，且没有自动化回归”
    - [B] `Documents/AutoPlans/DiscoveryPlans/Preprocessor_Plan.md` — “Issue-20/43：cache 恢复必须重建 restriction/editor-only 元数据，restriction 校验要按 eligible module 集合继续运行”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` L3369-L3389；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` L4523-L4549；`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp` L2752-L2783 — live preprocess 会写入 `UsageRestrictions`，但 validator 遇到 `ScriptModule == nullptr` 或 `Code.Num() == 0` 就直接 `return`，precompiled 恢复路径也没有把 restriction/editor-only/source section 数据带回来。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Compiler/AngelscriptRestrictionValidationTests.cpp`
- [ ] **P6.3** 📦 Git 提交：`[AngelscriptRuntime/Core] Fix: preserve restriction validation across failed and precompiled modules`
- [ ] **P6.3-T** 单元测试：锁住 failed-module 与 fully precompiled 场景下的 restriction 持续校验
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Compiler/AngelscriptRestrictionValidationTests.cpp`
  - 测试场景：
    - 正常路径：带活跃 restriction 的模块在 live compile 与 fully precompiled restore 两条路径上都保持相同的 restriction 生效结果。
    - 边界条件：同一批次中存在不可校验模块或 cache 恢复出的 module 时，其余 eligible modules 的 restriction 仍继续被检查，并输出跳过统计或 verbose 诊断。
    - 错误路径：`A` 因缺失 import 编译失败、`B` 持有 restriction、`C` 违反 `B` 的 restriction 时，修复后必须同时看到 import error 与 restriction error，而不是被第一处坏模块熔断。
  - 测试命名：`Angelscript.TestModule.StaticJIT.Restrictions.PrecompiledRestorePreservesUsageRestrictions`、`Angelscript.TestModule.Compiler.Restrictions.FailedModuleDoesNotShortCircuitValidation`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P6.3-T** 📦 Git 提交：`[AngelscriptTest/Compiler] Test: cover restriction validation on failed and precompiled paths`

## 补充测试矩阵

| 改进项 | 测试文件 | 测试场景 | 优先级 |
|--------|---------|---------|--------|
| P6.1 | `Plugins/Angelscript/Source/AngelscriptTest/FileSystem/AngelscriptScriptRootDiscoveryTests.cpp` | alias root 去重、directory loop 终止、duplicate physical script 不再进入预处理 | P0 |
| P6.2 | `Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorDirectiveTests.cpp` | active-branch restriction、tab 分隔、comment stripping、empty pattern 错误 | P1 |
| P6.3 | `Plugins/Angelscript/Source/AngelscriptTest/Compiler/AngelscriptRestrictionValidationTests.cpp` | failed-module 不再熔断 restriction、precompiled/live restriction parity | P0 |

## 补充验收项

15. 同一物理 script root 或 `.as` 文件不得因 alias path、symlink/junction 环或递归重复而多次进入 `Preprocessor.AddFile()`；discovery、hot reload 与 module identity 必须共享同一 physical 去重结果。
16. `#restrict usage` 只允许来自活跃分支的 directive 生效；tab 分隔与 comment 变体必须得到与空格版一致的 parser 结果，而 comment-only/empty pattern 必须稳定报语法错误。
17. live preprocess 与 fully precompiled restore 必须暴露等价的 `UsageRestrictions` / `EditorOnlyBlockLines` 合同；单个失败模块不再导致整轮 restriction 校验被静默跳过。

## 补充风险与行为变化

### 风险

11. **canonical physical path 策略会受平台文件系统与测试夹具能力影响**
   - 某些测试环境无法稳定解析 realpath/reparse-point 时，canonicalization 可能只能退回标准化绝对路径，导致 physical 去重精度存在平台差异。
   - **缓解**：helper 设计成“realpath 可用则增强，不可用则退回 normalized absolute path”，并在 trace 中输出命中的 canonicalization 模式。

12. **`#restrict usage` parser 收紧后，一批过去被静默忽略的写法会变成显式诊断**
   - tab 分隔、注释尾巴混入 pattern、comment stripping 后为空的 directive，修复后都会从“偶然不生效”转成“稳定报错/警告”。
   - **缓解**：诊断里明确输出原始 directive 文本、规范化后的 pattern 与拒绝原因，并同步更新相关 guide / fixture。

13. **precompiled schema 增加 restriction/editor-only/source provenance 字段后，会触发现有 cache 版本切换**
   - 旧缓存若继续被新代码读取，最容易表现为 restriction 行为不一致或 filename lookup 退化。
   - **缓解**：显式 bump schema/version，旧 cache 一律按 stale cache 回退到实时预处理，不做隐式兼容猜测。

### 已知行为变化

13. **alias root、symlink/junction 环与 duplicate physical script 将在 discovery 阶段被去重或拒绝，不再把同一物理脚本多次送入模块系统**
   - 影响文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` L1347-L1357、L1944-L2010、L2876-L2888；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` L91-L103。

14. **inactive branch、tab 分隔和 comment-tailed `#restrict usage` 将统一走结构化 parser，不再以“原文残留/脏 pattern”形式静默污染模块元数据**
   - 影响文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` L3363-L3402、L4318-L4325、L4349-L4358。

15. **restriction 校验会继续覆盖同批次中的其它 eligible modules，fully precompiled 模块也会重新参与同一套 restriction/editor-only 校验**
   - 影响文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` L4523-L4549；`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp` L2752-L2783。

---

## 深化（2026-04-09 01:16:13）

### Phase 7：补齐依赖签名、import 边级诊断与 include owner 传播

- [ ] **P7.1** 把 `automatic/manual import` 统一收敛为 `ResolvedModuleDependencies + DependencySignature`，停止让默认路径只靠 `CodeHash` 命中 cache
  - 前面 Phase 1/2 已经要求 import 解析和 live/precompiled artifact 对齐，但还缺一条独立执行项来收口“默认 `automatic import` 的真实依赖到底记在哪里”。当前编译、hot reload 与 restriction 校验已经部分承认依赖存在于 `asCModule::moduleDependencies`，可 `CombinedDependencyHash`、precompiled schema 和 cache 命中条件仍然停留在显式 `ImportedModules`/`CodeHash` 合同上。
  - 这一项要新增持久化的 `ResolvedModuleDependencies` 与顺序敏感 `DependencySignature`：manual import 走 canonical resolver 后投影到同一结构，automatic import 则在 dependency graph 稳定后从 `moduleDependencies` 回写 canonical provider 集合；`CombinedDependencyHash`、precompiled 命中条件、hot reload 传播和后续 function-id/signature 消费都统一只读这份 resolved graph，不再让 explicit/automatic 两套依赖模型并存。
  - 同步把 precompiled schema 从“只保存 `CodeHash + ImportedModules`”升级为“保存 resolved dependency graph + dependency signature + source/provenance 最小集”，确保 provider 变了但 consumer 源码没变时，consumer 仍会稳定 miss 旧 cache，而不是继续短路到陈旧 precompiled 结果。
  - 来源：
    - [A] `Documents/AutoPlans/Preprocessor_Analysis.md` — “发现 16/17/60：默认 automatic-import 下真实依赖只存在于 `moduleDependencies`，`CombinedDependencyHash`/precompiled cache 仍只看 `ImportedModules` 或 `CodeHash`，导入顺序变化也不会进入签名”
    - [B] `Documents/AutoPlans/DiscoveryPlans/Preprocessor_Plan.md` — “Issue-36/53：需要 `ResolvedModuleDependencies` / `DependencySignature`，cache 命中不能继续只比较 `CodeHash` 或显式 import 字符串列表”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` L3173-L3208、L4262-L4290、L4566-L4573 — automatic-import 路径不会填充 `ImportedModules`，`CombinedDependencyHash` 与 precompiled 命中仍只消费显式 imports/`CodeHash`，而 restriction 校验却已经改读 `moduleDependencies`；`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp` L1417-L1422、L2773-L2779 — precompiled 保存/恢复仍只序列化 `CodeHash` 与 `ImportedModules`，没有任何 resolved dependency graph 字段。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`
- [ ] **P7.1** 📦 Git 提交：`[AngelscriptRuntime/Core] Refactor: persist resolved dependency signatures across live and precompiled paths`
- [ ] **P7.1-T** 单元测试：锁住 resolved dependency graph 在 automatic/manual import、live/precompiled 两条路径上的一致性
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/StaticJIT/AngelscriptDependencySignatureTests.cpp`
  - 测试场景：
    - 正常路径：`automatic import = true` 时，`Base <- Shared <- Consumer` 首编译后会把 compiler 推导出的 provider 集合回写到 `ResolvedModuleDependencies`，consumer 的 dependency signature 与 live compile 结果一致。
    - 边界条件：只交换两条 manual `import` 的顺序或只改 automatic-import provider 文件而不改 consumer 源码，dependency signature 必须变化，cache 命中原因日志能区分 `CodeHash` 未变但 dependency signature 失配。
    - 错误路径：生成 precompiled cache 后只修改 provider，fully precompiled 启动不得继续命中旧 cache；diagnostics 必须明确指出 dependency signature/stale graph，而不是静默加载旧模块。
  - 测试命名：`Angelscript.TestModule.StaticJIT.Dependencies.AutomaticImportProviderChangeInvalidatesConsumerCache`、`Angelscript.TestModule.StaticJIT.Dependencies.ImportOrderChangesDependencySignature`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P7.1-T** 📦 Git 提交：`[AngelscriptTest/StaticJIT] Test: cover resolved dependency signature parity`

- [ ] **P7.2** 把 module-level `import` 从字符串列表升级为边级元数据，统一 unresolved / circular import 的行号与原文诊断
  - 当前 Plan 已在 P3.2 提到“缺失 provider 要回到 import 原始行”，但底层合同还没有单独落地。预处理阶段明明已经拿到 `FImport.FileLineNumber`，循环链却只记录 `FFile*`，编译阶段也只知道 `ImportedModules` 的字符串数组，于是 unresolved import 和 circular import 最终都退化成模块级第 1 行或只剩模块名链。
  - 这一项要新增边级依赖结构，例如 `FModuleImportRef` / `FImportEdge`：至少保存 canonical module key、`RawImportText`、`SourceLine`、statement span 与 provider resolve 结果；`ProcessImports()`、cycle detector、compile-stage unresolved provider、precompiled restore 与 trace/hot reload 诊断全部共享这份 edge metadata，而不是继续在预处理和编译阶段各自重建行号来源。
  - 同步把 `FImportChain` 从“访问过哪些文件”改成“沿着哪一条 import 边走到这里”的链式结构，让循环诊断能输出 `File:Line -> Module` 级别的闭环，而不是只打印抽象模块名列表。
  - 来源：
    - [A] `Documents/AutoPlans/Preprocessor_Analysis.md` — “发现 14/22：unresolved import 与循环 import 都丢失边级行号，现有链路只剩模块第 1 行或模块名链”
    - [B] `Documents/AutoPlans/DiscoveryPlans/Preprocessor_Plan.md` — “Issue-34：需要把 module-level import 从 `TArray<FString>` 升级成带 `SourceLine` / `RawImportText` 的依赖元数据”
    - [C] `Documents/AutoPlans/TestCoverage/Preprocessor_TestGaps.md` — “NewTest-1/NewTest-3 与 Issue-22：循环链、缺分号和现有 weak import assertions 都缺少稳定的 line-aware 诊断保护”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h` L101-L108、L163-L167 — `FImport` 保存了 `FileLineNumber`，但 `FImportChain` 只记录 `FFile*`；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` L447-L450、L463-L483、L3502-L3507 — cycle 诊断只打印模块名，`ProcessImports()` 只提交 `ModuleName`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` L3173-L3195 — unresolved import 仍固定 `ScriptCompileError(Module, 1, ...)`。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp`
- [ ] **P7.2** 📦 Git 提交：`[AngelscriptRuntime/Preprocessor] Fix: preserve import edge diagnostics across compile and cache`
- [ ] **P7.2-T** 单元测试：锁住 module import 的边级诊断合同
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorImportDiagnosticsTests.cpp`
  - 测试场景：
    - 正常路径：两条合法 `import` 都解析成功时，module artifact 暴露的 import edge 记录同时保留 canonical provider、原始 import 文本与源行号。
    - 边界条件：同一文件两条 `import` 中只让第二条 unresolved，诊断必须准确锚到第二条 import 所在行，而不是模块第 1 行；大小写/相对路径 canonicalization 后仍回到原始 import 行。
    - 错误路径：`A -> B -> A` 闭环时，循环诊断必须输出带 `File:Line` 的边链；fully precompiled 恢复同样错误时，不得再次退化成模块第 1 行。
  - 测试命名：`Angelscript.TestModule.Preprocessor.Import.Diagnostics.UnresolvedImportReportsSourceLine`、`Angelscript.TestModule.Preprocessor.Import.Diagnostics.CircularImportReportsEdgeChain`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P7.2-T** 📦 Git 提交：`[AngelscriptTest/Preprocessor] Test: cover import edge line diagnostics`

- [ ] **P7.3** 把 `#include` owner 反向索引真正接进 hot reload、polling 删除检测与 precompiled invalidation，避免 include graph 落地后仍然“改了 include 文件但 owner 不动”
  - 现有 P2.1 已经要求 include graph 和 `IncludedFile -> OwningModule` 索引，但还没有单列执行 owner invalidation 这条传播链。当前热重载入口只会把“直接改动的脚本文件”映射到 `RelativeFileToModule`，再沿 `ImportedModules` 构建反向依赖；如果 include graph 落地后不同时补 owner 索引与 shared helper，watcher、polling、cache-hit 三条路径仍会继续漏掉 include 文件改动。
  - 这一项要在 artifact/runtime state 上同时增加 `IncludedFiles` 与 `IncludeOwners`：preprocess 产物保存 canonical include edges，hot reload 入口把 changed/deleted include file 先映射到 owner modules 再进入反向依赖传播，precompiled cache 则把 include fingerprint/owner graph 一起写盘并在恢复时校验。这样无论 include 文件是内容变化、被删除，还是 shared include 被多个 owner 复用，都能稳定触发 owner module reload/cache miss，而不是继续依赖“owner 源文件本身也被改了”这种偶然条件。
  - polling 与 editor watcher 要共用同一条 `QueueChangedOrDeletedScriptDependency(...)` helper，不允许一条路径认 direct file change、另一条路径认 include owner；否则 include graph 只会在 editor watcher 下表现正确，非 watcher / fully precompiled 路径仍然会静默复用旧结果。
  - 来源：
    - [A] `Documents/AutoPlans/Preprocessor_Analysis.md` — “发现 28：`#include` 既不进入依赖跟踪，也不会影响 hot reload 传播与模块哈希”
    - [B] `Documents/AutoPlans/DiscoveryPlans/Preprocessor_Plan.md` — “Issue-5：需要 `IncludedFile -> OwningModule` 反向索引与 include 改动触发的 owner cache miss / hot reload”
    - [C] `Documents/AutoPlans/TestCoverage/Preprocessor_TestGaps.md` — “NewTest-5：`#include` 需要明确且稳定的合同测试，而不是继续静默透传或漏跟踪”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` L3256-L3402 — directive scanner 仍没有任何 `#include` 分支；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` L2388-L2415 — hot reload 只从 `RelativeFileToModule` 和 `ImportedModules` 构建反向传播，完全看不到 include owner；`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp` L1417-L1422、L2773-L2779 — precompiled 也只保存/恢复 `ImportedModules`，没有 include owner/fingerprint 字段。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp`
- [ ] **P7.3** 📦 Git 提交：`[AngelscriptRuntime/Core] Feat: invalidate include owners on file change and cache restore`
- [ ] **P7.3-T** 单元测试：锁住 include owner 的 change/delete 传播与 cache invalidation
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptIncludeDependencyHotReloadTests.cpp`
  - 测试场景：
    - 正常路径：owner 模块通过 `#include "Shared.as"` 引入符号后，仅修改 `Shared.as` 就会把 owner 模块加入 reload 队列，并在重新执行时看到新值。
    - 边界条件：两个 owner 共享同一 include 文件时，一次 include 变更会稳定标记两个 owner 且只入队一次；fully precompiled 路径恢复出的 include owner graph 与 live preprocess 一致。
    - 错误路径：删除 include 文件或让 include 解析失败时，owner 模块必须稳定 cache miss / reload fail-fast，并把诊断锚到 include 语句所在文件与行号，而不是继续静默复用旧 owner 模块。
  - 测试命名：`Angelscript.TestModule.HotReload.Include.ChangePropagatesToOwners`、`Angelscript.TestModule.HotReload.Include.DeleteInvalidatesOwnerCache`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P7.3-T** 📦 Git 提交：`[AngelscriptTest/HotReload] Test: cover include owner invalidation and reload propagation`

## 补充测试矩阵

| 改进项 | 测试文件 | 测试场景 | 优先级 |
|--------|---------|---------|--------|
| P7.1 | `Plugins/Angelscript/Source/AngelscriptTest/StaticJIT/AngelscriptDependencySignatureTests.cpp` | automatic/manual import 统一 resolved dependency graph、provider 改动触发 dependency signature 变化、precompiled cache miss | P0 |
| P7.2 | `Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorImportDiagnosticsTests.cpp` | unresolved import 精确行号、circular import 边链诊断、fully precompiled 诊断不再退化 | P0 |
| P7.3 | `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptIncludeDependencyHotReloadTests.cpp` | include owner 变更传播、shared include 去重入队、delete/include 失败触发 owner cache miss | P1 |

## 补充验收项

18. `automatic import` 与 manual `import` 最终都必须汇入同一份 `ResolvedModuleDependencies + DependencySignature` 合同；provider 变化即使不改 consumer 源码，也必须稳定触发 dependency signature 变化、cache miss 与后续热重载传播。
19. module-level `import` 与 circular import diagnostics 必须锚到真实 import 语句所在文件和行号，fully precompiled 恢复路径不得再退化到模块第 1 行。
20. include graph 一旦启用，include 文件的 change/delete 必须像 direct source file change 一样触发 owner module reload/cache invalidation；watcher、polling 与 fully precompiled 三条路径共享同一 owner graph 合同。

## 补充风险与行为变化

### 风险

14. **resolved dependency graph 写盘后，会把一批过去误命中的 automatic/manual import cache 统一打成 stale miss**
   - 这属于正确性修复，但短期会抬高一次性重编译量，尤其在已有 precompiled cache 的项目上更明显。
   - **缓解**：显式记录 miss 原因（`CodeHash` / dependency signature / include fingerprint），并在 schema bump 后默认走 warning + 回退实时编译。

15. **import edge 元数据收口会触达所有仍假设 `ImportedModules == TArray<FString>` 的调用点**
   - 若 live compile、hot reload、StaticJIT 只迁了一半，最容易出现“live 路径有行号、cache 路径没有行号”的双轨诊断。
   - **缓解**：保留从 `FModuleImportRef` 到兼容 `ImportedModules` 视图的单向投影，迁移期禁止再新增直接写 `ImportedModules` 的新路径。

16. **include owner 反向索引若只在 watcher 或只在 live preprocess 落地，会让 polling / fully precompiled 再次分叉**
   - include 依赖最怕“某个入口好用、另一个入口静默漏跟踪”，这种分叉一旦进入主干很难靠人工排查发现。
   - **缓解**：owner graph、include fingerprint 与 change/delete 入队逻辑统一进 shared helper，并强制由 live、polling、precompiled 三类测试共同覆盖。

### 已知行为变化

16. **默认 automatic-import 模式下，provider 变化将不再被 `CodeHash` 掩盖；consumer 即使源码未改，也会因 dependency signature 变化而触发 cache miss / reload**
   - 影响文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` L3173-L3208、L4262-L4290、L4566-L4573；`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp` L1417-L1422、L2773-L2779。

17. **module import 与 circular import 的错误将从“模块第 1 行/纯模块名链”升级为“具体 import 边 + 源行号”**
   - 影响文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h` L101-L108、L163-L167；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` L447-L450、L463-L483、L3502-L3507；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` L3173-L3195。

18. **include 文件改动或删除将直接让 owner modules 进入 reload / cache invalidation 路径，不再继续依赖 owner 源文件也被修改这一偶然条件**
   - 影响文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` L3256-L3402；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` L2388-L2415；`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp` L1417-L1422、L2773-L2779。

---

## 深化（2026-04-09 01:26:17）

### Phase 8：补 precompiled freshness/key 与 shared source owner 索引

- [ ] **P8.1** 将 `FAngelscriptPrecompiledData::Modules` 从裸 `ModuleName` map 升级为 root-aware / source-aware 复合键，阻断 duplicate module 在写 cache 时被静默压扁
  - 现有 `P4.2` 已要求 live / hot reload / filename lookup 共享 `ModuleIdentity`，但 precompiled schema 还缺一条显式闭环：`FAngelscriptPrecompiledModule::InitFrom()` 只保存 `ModuleName + CodeHash + ImportedModules`，`PrepareToFinalizePrecompiledModules()` 继续 `Modules.FindOrAdd(ModuleName)`，`GetModulesToCompile()` 再按同一 map 恢复单份 descriptor。只要 live compile 仍有 duplicate module 漏网，写 cache 时就会先被 last-writer-wins 压扁，fully precompiled 启动看到的模块集合天然与 live compile 不一致。
  - 这一项要把 precompiled 主键升级为 `FPrecompiledModuleKey { RootKey, CanonicalRelativeFilename, CanonicalModuleName }` 或等价结构，并让 cache 生成、反序列化、debug/provenance 查询都只认这份复合 key。若 live 路径最终对同名模块执行 fail-fast，cache 生成也必须沿用同一条 duplicate-module 规则，禁止再让 `TMap<FString, ...>` 把错误状态固化进磁盘。
  - 这不是对 `P4.2` 的重复表述，而是补齐 cache 落点：只有 precompiled 主键与 `ModuleIdentity` 收敛为同一合同，`P2.2` / `P7.1` 的 ordered fingerprint 与 dependency signature 才不会在 fully precompiled 路径重新掉回 `ModuleName`-only 世界。
  - 来源：
    - [A] `Documents/AutoPlans/Preprocessor_Analysis.md` — “发现 58：precompiled cache 以 `ModuleName` 为唯一 key，同名模块会在序列化阶段被静默压扁成一份”
    - [B] `Documents/AutoPlans/DiscoveryPlans/Preprocessor_Plan.md` — “Issue-27：需要 `FPrecompiledModuleKey`，cache 生成与恢复都不能再用裸 `ModuleName`”
    - [E] `Documents/AutoPlans/ReferenceComparison/CrossComparison.md` — “D11：当前 Angelscript 的 operator surface 应保持 deterministic，project-first roots 与 cache contract 不应在交付路径上退化成隐式覆盖”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp` L1417-L1424、L2655-L2660、L2773-L2779 — `InitFrom()` 仅序列化 `ModuleName/CodeHash/ImportedModules`，`PrepareToFinalizePrecompiledModules()` 仍执行 `Modules.FindOrAdd(ModuleName)`，恢复路径也只回填单份 `ModuleName` descriptor；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` L91-L99 — `AddFile()` 仍把 source identity 先压成单个 `ModuleName`，说明 cache 侧尚未建立独立于显示名的复合主键。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/StaticJIT/AngelscriptPrecompiledDataModuleKeyTests.cpp`
- [ ] **P8.1** 📦 Git 提交：`[AngelscriptRuntime/StaticJIT] Fix: key precompiled modules by root-aware source identity`
- [ ] **P8.1-T** 单元测试：锁住 precompiled 主键与 duplicate-module cache fail-fast 合同
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/StaticJIT/AngelscriptPrecompiledDataModuleKeyTests.cpp`
  - 测试场景：
    - 正常路径：唯一 `RootKey + RelativeFilename + CanonicalModuleName` 组合的模块可稳定写入 cache，并在恢复时保留与 live compile 一致的 descriptor 集合。
    - 边界条件：project root / plugin root 下若出现同名显示模块，cache 侧必须给出与 live compile 一致的 duplicate-module 诊断，而不是 silently overwrite。
    - 错误路径：cache 中某个复合 key 的 root/path 在当前 root registry 已失效时，系统必须给出 stale-cache 诊断并回退 live preprocess，而不是把它继续当作普通 `ModuleName` 恢复。
  - 测试命名：`Angelscript.TestModule.StaticJIT.Precompiled.ModuleKeyRoundTripMatchesLiveIdentity`、`Angelscript.TestModule.StaticJIT.Precompiled.DuplicateModuleDoesNotSilentlyOverwriteCache`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P8.1-T** 📦 Git 提交：`[AngelscriptTest/StaticJIT] Test: cover precompiled module identity keys`

- [ ] **P8.2** 为 fully precompiled 启动补 `RawSourceHash` / `SourceFingerprint` 校验，停止让 `CodeHash == CodeHash` 充当缓存自校验
  - 现有 `P2.2`、`P7.1` 已经覆盖 ordered fingerprint 与 dependency signature，但 fully precompiled 启动还有一个更前置的 freshness 缺口：`InitialCompile()` 直接 `GetModulesToCompile()`，`ModuleDesc->CodeHash` 来自 cache，`CompileModule_Types_Stage1()` 再用同一 cache 条目的 `CompiledModule->CodeHash` 比较 `Module->CodeHash`。如果没有独立的磁盘源码指纹，这条所谓命中条件只是在比较“缓存里的值是否等于缓存里的值”。
  - 这一项要在 `Preprocess()` 读入 `File.RawCode` 时先生成 `RawSourceHash/SourceFingerprint`，按 section 写入 precompiled schema，并在 fully precompiled 启动进入 `GetModulesToCompile()` 前执行 `ValidateCachedSources()`。只有 source fingerprint、`PreprocessContextHash` 和 dependency signature 全部通过时，才允许进入 precompiled hit；否则必须显式记录 stale 原因并回退 live preprocess。
  - 这样做的目的不是把 precompiled 路径变慢，而是把 D11 中已经被当成交付 contract 的 cache/snapshot 变成“可验证的 contract”。否则脚本文件被替换、打包残留旧 cache 或 plugin 脚本版本漂移时，系统仍会稳定加载陈旧模块。
  - 来源：
    - [A] `Documents/AutoPlans/Preprocessor_Analysis.md` — “发现 39：fully precompiled 启动不会验证磁盘 `.as` 是否仍与 cache 一致，`CodeHash` 守卫退化为缓存自校验”
    - [B] `Documents/AutoPlans/DiscoveryPlans/Preprocessor_Plan.md` — “Issue-19：需要 `RawSourceHash/SourceFingerprint + ValidateCachedSources()`，fully precompiled 命中前必须先验证磁盘脚本”
    - [E] `Documents/AutoPlans/ReferenceComparison/CrossComparison.md` — “D11：当前 Angelscript 以 cache/snapshot 作为正式交付 contract，contract 必须在消费前有可执行 freshness guard”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` L2046-L2052、L4284-L4290 — fully precompiled 路径直接 `GetModulesToCompile()`，命中条件仅比较 `CompiledModule->CodeHash == Module->CodeHash`；`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp` L2773-L2779 — cache 恢复只回填 `CodeHash` 和 `ImportedModules`，没有任何磁盘源码指纹；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` L289-L304 — live preprocess 目前只对 `ProcessedCode` 计算 `Section.CodeHash` / `Module->CodeHash`，并未保存原始源码级 fingerprint。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/StaticJIT/AngelscriptPrecompiledSourceFingerprintTests.cpp`
- [ ] **P8.2** 📦 Git 提交：`[AngelscriptRuntime/StaticJIT] Fix: validate precompiled caches against raw source fingerprints`
- [ ] **P8.2-T** 单元测试：锁住 fully precompiled 的 source fingerprint freshness 合同
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/StaticJIT/AngelscriptPrecompiledSourceFingerprintTests.cpp`
  - 测试场景：
    - 正常路径：cache 与磁盘脚本完全一致时，fully precompiled 启动仍能命中 precompiled module，且行为与 live preprocess 一致。
    - 边界条件：多 section / multi-file module 的所有 source fingerprint 都通过时，命中条件仍稳定成立，不因 section 数量增加而退化成只验 `CodeHash`。
    - 错误路径：只修改一个 provider/source 文件或删除其中一个脚本而不更新 cache 时，启动必须输出 stale-cache 原因并回退 live preprocess，不得静默接受旧 precompiled module。
  - 测试命名：`Angelscript.TestModule.StaticJIT.Precompiled.SourceFingerprintMatchKeepsCacheHit`、`Angelscript.TestModule.StaticJIT.Precompiled.SourceFingerprintMismatchForcesLivePreprocess`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P8.2-T** 📦 Git 提交：`[AngelscriptTest/StaticJIT] Test: cover precompiled source fingerprint freshness`

- [ ] **P8.3** 深化 `P7.3`：把 hot reload 文件索引从单值 `RelativeFileToModule` 升级为 `SourceKeyToOwningModules`，为 shared include / multi-section module 提前封住 owner 丢失
  - `P7.3` 已经要求 include owner invalidation，但当前 Plan 还缺少一个明确的实现前置：hot reload 入口现在把 `Section.RelativeFilename` 映射成单个 `FAngelscriptModuleDesc*`，changed file 也只会命中一个 owner。即使未来 include graph 落地，只要这层还是单值 map，shared include 或 multi-section module 就仍会有“改了一个 source，只 reload 到最后一个 owner”的结构性漏报。
  - 这一项要在 `P4.2` 的 canonical `SourceKey` / `ModuleIdentity` 上补 `SourceKeyToOwningModules` 多值索引：构建时按 section provenance 收集全部 owner 集合，automatic-import / explicit-import / delete path 都从这份 owner 集合起步，再去做 reverse dependency 扩散。`IncludeOwners` 可以作为 `SourceKeyToOwningModules` 的特化视图，但不再允许 watcher、polling、precompiled restore 各自私有一套 owner lookup。
  - 这样 `P7.3` 的目标才有稳定落点：一个 include file、共享 source section 或 generated overlay file 可以拥有 `0..N` 个 owner modules，hot reload / cache invalidation 必须先拿到完整 owner 集，再决定后续传播，而不是反过来让 include graph 去适配一个只支持 `1:1` 的旧账本。
  - 来源：
    - [A] `Documents/AutoPlans/Preprocessor_Analysis.md` — “发现 27/28：`RelativeFileToModule` 仍按单个 `RelativeFilename` 查 owner，而 `#include` 还完全不在依赖跟踪里”
    - [B] `Documents/AutoPlans/DiscoveryPlans/Preprocessor_Plan.md` — “Issue-77：hot reload 仍把 `RelativeFilename -> Module` 建成单值索引，future `#include` / shared section 无法把一个源码文件映射到多个 owner module”
    - [C] `Documents/AutoPlans/TestCoverage/Preprocessor_TestGaps.md` — “NewTest-5：`#include` 需要明确且稳定的合同测试；当前没有任何 shared owner / include propagation 回归”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` L2298-L2309、L2321-L2333、L2389-L2415 — hot reload 仍构建单值 `RelativeFileToModule`，changed file 最多命中一个 owner，再沿 `ImportedModules` 做 reverse dependency；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` L289-L304 — 每个 `FFile` 只把当前 source 填成一个 `FCodeSection`，section provenance 里还没有可直接复用的 owner-set/source-key 结构。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessorContracts.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptSharedSourceHotReloadTests.cpp`
- [ ] **P8.3** 📦 Git 提交：`[AngelscriptRuntime/Core] Refactor: track source owners as multi-value hot reload indices`
- [ ] **P8.3-T** 单元测试：锁住 shared source / include file 的 multi-owner reload 合同
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptSharedSourceHotReloadTests.cpp`
  - 测试场景：
    - 正常路径：一个 shared include/source 被两个 owner module 复用时，修改该文件会把两个 owner 都纳入 reload 集合，并且只各入队一次。
    - 边界条件：同一 owner 通过多个 section 命中同一 `SourceKey` 时，owner 集合会去重；live preprocess 与 fully precompiled restore 恢复出的 owner 集合同样一致。
    - 错误路径：shared include 被删除或解析失败时，所有 owner 都必须得到 cache miss / reload fail-fast，不得再出现“只更新最后一个 owner、其余 owner 继续沿旧状态运行”的 silent stale-state。
  - 测试命名：`Angelscript.TestModule.HotReload.SourceOwners.SharedSourceChangeReloadsAllOwners`、`Angelscript.TestModule.HotReload.SourceOwners.DeleteInvalidatesEveryOwner`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P8.3-T** 📦 Git 提交：`[AngelscriptTest/HotReload] Test: cover shared source multi-owner reload indices`

## 补充测试矩阵

| 改进项 | 测试文件 | 测试场景 | 优先级 |
|--------|---------|---------|--------|
| P8.1 | `Plugins/Angelscript/Source/AngelscriptTest/StaticJIT/AngelscriptPrecompiledDataModuleKeyTests.cpp` | root-aware precompiled key、duplicate-module 不再 silently overwrite、stale root/path 回退 | P0 |
| P8.2 | `Plugins/Angelscript/Source/AngelscriptTest/StaticJIT/AngelscriptPrecompiledSourceFingerprintTests.cpp` | raw source fingerprint match/mismatch、multi-section cache freshness、stale-cache fallback | P0 |
| P8.3 | `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptSharedSourceHotReloadTests.cpp` | shared source 多 owner reload、去重 owner 集、delete 传播到全部 owner | P1 |

## 补充验收项

21. precompiled cache 的持久化主键必须与 live `ModuleIdentity` 对齐；同名 module 不得在写 cache 时再被 `TMap<FString, ...>` 静默压成单份 descriptor。
22. fully precompiled 启动在命中 cache 前必须先验证磁盘 source fingerprint；cache 自校验不再被当成 freshness guard。
23. hot reload / include invalidation 必须先基于 `SourceKeyToOwningModules` 拿到完整 owner 集合，再做 reverse dependency 扩散；shared source 不得只 reload 到单个 owner。

## 补充风险与行为变化

### 风险

17. **precompiled 主键从 `ModuleName` 升级为复合 key 后，会一次性暴露历史 cache 中被 silent overwrite 掩盖的 duplicate module**
   - 这类问题过去可能只在 fully precompiled 路径偶发缺模块；修复后会在写 cache 或读 cache 时稳定报错/判 stale。
   - **缓解**：把 duplicate-module 诊断同时输出 `RootKey`、`RelativeFilename` 与显示 `ModuleName`，并让 live / cache 共用同一 fail-fast 文案。

18. **source fingerprint 校验会给 fully precompiled 启动新增一次轻量磁盘读取**
   - 在大型脚本集上会提高启动前的 I/O 数量，但这是把陈旧 cache 从 silent wrong result 变成显式回退的必要成本。
   - **缓解**：按 section/mtime batching 设计 `ValidateCachedSources()`，并把 miss 原因细分为 `MissingFile` / `SourceFingerprintMismatch` / `ContextMismatch`。

19. **multi-owner hot reload 索引会放大一次变更触发的 reload 集**
   - 这会让过去被单值 map 掩盖的 stale owner 立即暴露，短期可能看到更多模块参与 reload。
   - **缓解**：owner 集合先去重再扩散，并通过 shared source trace 输出“为何被纳入 reload”的明确原因。

### 已知行为变化

19. **precompiled cache 不再允许 `ModuleName`-only 的静默覆盖语义**
   - 影响文件：`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp` L1417-L1424、L2655-L2660、L2773-L2779；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` L91-L99。

20. **fully precompiled 启动将先验证磁盘脚本与 cache 的 source fingerprint，再决定是否命中 precompiled module**
   - 影响文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` L2046-L2052、L4284-L4290；`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp` L2773-L2779；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` L289-L304。

21. **shared include / shared source file 的变更将同时使全部 owner modules 进入 reload 或 cache invalidation 路径**
   - 影响文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` L2298-L2309、L2321-L2333、L2389-L2415；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` L289-L304。

---

## 深化（2026-04-09 01:38:16）

### Phase 9：把 single-file 假设从 include / import 下游合同里拔掉

- [ ] **P9.1** 把 `ScriptCompileError` 与 precompiled 恢复诊断升级为 section-aware `FScriptSourceLocation`
  - 前面 `P2.1` 已经要求 include graph 和 section provenance，`P4.2` 也已经要求 root-aware `ModuleIdentity`；但当前下游错误入口仍然是“`Module + LineNumber`”，真正报错时继续无条件回落到 `Code[0]` 或 `ModuleName`。只要 include graph、expanded section 或 plugin-root precompiled restore 一落地，第二份源码文件的错误就会重新被打回首文件或模块名兜底。
  - 这一项要把 diagnostics 合同收口成显式 `FScriptSourceLocation { SectionIndex / SourceKey / AbsoluteFilename / Row / Column }`：preprocessor/live restore/precompiled restore 都先恢复稳定 section provenance，再让 `ScriptCompileError(...)`、debugger filename lookup、compile-stage unresolved dependency 与 future include diagnostics 统一走这份 location，而不是继续共享“第一个 section 就代表整个模块”的历史假设。
  - 旧的 `ScriptCompileError(Module, LineNumber)` 只保留兼容桥，且在 `Module->Code.Num() > 1` 或 provenance 缺失时必须输出 warning/ensure；cache 路径若拿不到完整 section provenance，要显式判 stale 并回退 live preprocess，而不是悄悄退化成 `Code[0]` 或 project-root fallback。
  - 来源：
    - [A] `Documents/AutoPlans/Preprocessor_Analysis.md` — “发现 40/41：precompiled descriptor 不重建 `Code` 且 plugin script section 会被错误拼到 project root，缓存路径的 `ScriptCompileError` / source identity 天生比 live preprocess 更瘦”
    - [B] `Documents/AutoPlans/DiscoveryPlans/Preprocessor_Plan.md` — “Issue-55：多 section 模块的编译诊断固定落到 `Code[0]`；Issue-21/58：precompiled restore 必须带 section/root provenance 才能支撑按文件定位与断点”
    - [C] `Documents/AutoPlans/TestCoverage/Preprocessor_TestGaps.md` — “Issue-12：现有 happy-path 预处理测试根本不检查 diagnostics，source-location 漂移会被静默放过”
    - [E] `Documents/AutoPlans/ReferenceComparison/CrossComparison.md` — “D5 source identity owner：当前插件最值得保留的是 debugger / editor navigation 共享同一份 metadata-owned source identity，不应退回字符串或首文件兜底”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` L289-L304 — 预处理仍只把每个 `FFile` 压成一条 `FCodeSection`，没有任何统一 `SourceLocation` 结构；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h` L351-L352、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` L3192-L3194、L4944-L4956 — 模块级错误入口仍只有 `ScriptCompileError(Module, LineNumber)`，并固定回落到 `Code[0]`；`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp` L1417-L1424、L1485-L1494、L2773-L2779 — cache 仍只保存 `CodeHash/ImportedModules/ScriptRelativeFilename`，没有可恢复的 section provenance。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorSectionDiagnosticsTests.cpp`
- [ ] **P9.1** 📦 Git 提交：`[AngelscriptRuntime/Core] Refactor: route module diagnostics through section-aware source locations`
- [ ] **P9.1-T** 单元测试：锁住 multi-section / precompiled diagnostics 的 section-aware 锚点
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorSectionDiagnosticsTests.cpp`
  - 测试场景：
    - 正常路径：两段 source section 或 owner+include 组成的同一模块在无错误时，`Module->Code` / provenance / filename lookup 能稳定区分两份源码，并保持与 live preprocess 一致。
    - 边界条件：只让第二个 section 触发编译错误时，diagnostic 必须锚到第二个 section 的真实文件和行号；同一 fixture 走 fully precompiled restore 时，错误锚点保持一致，不再回落到首文件。
    - 错误路径：cache 缺失某个 section provenance 或 root key 时，系统必须显式判 stale 并回退 live preprocess，而不是继续用 `Code[0]`、`ModuleName` 或 project-root fallback 伪装成成功定位。
  - 测试命名：`Angelscript.TestModule.Preprocessor.Diagnostics.MultiSectionErrorsReportOwningSection`、`Angelscript.TestModule.Preprocessor.Diagnostics.PrecompiledSectionProvenanceMismatchFallsBack`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P9.1-T** 📦 Git 提交：`[AngelscriptTest/Preprocessor] Test: cover section-aware diagnostics and precompiled provenance`

- [ ] **P9.2** 把 `EditorOnlyBlockLines` 从裸行号对升级为 section-aware range，并补齐 `EDITORONLY_DATA`
  - 当前 Plan 虽然在 `P2.1` 里总括提到“editor-only line map 要 section-aware”，但还缺一条明确执行项去拔掉 single-file 假设。预处理器一边把 `EDITORONLY_DATA` 当成合法 editor-only 条件参与宏校验，另一边 `UpdateEditorBlockLines()` 却只识别 `EDITOR`；builder 端又把所有 editor-only 行号都解释到 `scripts[0]`。这会让 include/multi-section 模块的 editor-only 保护在 live 与 cache 两条路径同时错位。
  - 这一项要把 `EditorOnlyBlockLines` 升级成显式 `FEditorOnlyBlockRange { SectionIndex / SourceKey / StartLine / EndLine }`：preprocessor 在记录 block 时同步保存 section 身份，并让 `EDITOR` 与 `EDITORONLY_DATA` 共享同一判定；builder 的 `SetEditorOnlyBlockLinePositions(...)` / `IsNodeInEditorOnlyCode(...)` 则改为按 `SectionIndex` 把行号映射到对应 `scripts[n]`，不再把所有 block 强行解释到 `scripts[0]`。
  - fully precompiled/schema 路径也必须一并序列化并恢复这份 section-aware editor-only 元数据，否则 include graph 一旦落地，live 路径会修好，cache 路径却仍然把第二份源码的 editor-only 语义丢回首文件。
  - 来源：
    - [A] `Documents/AutoPlans/Preprocessor_Analysis.md` — “发现 32/54：`EditorOnlyBlockLines` 既漏掉 `EDITORONLY_DATA`，又会让 builder 基于偏瘦的 editor-only 行视图发出错误诊断”
    - [B] `Documents/AutoPlans/DiscoveryPlans/Preprocessor_Plan.md` — “Issue-56：`EditorOnlyBlockLines` 仍按 `scripts[0]` 解释行号，多 section 模块的 editor-only 校验会整体错位”
    - [C] `Documents/AutoPlans/TestCoverage/Preprocessor_TestGaps.md` — “NewTest-28：必须精确锁住 `EditorOnlyBlockLines` 的行号映射；当前没有任何回归读取这份元数据”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` L3092-L3123 — `UpdateEditorBlockLines()` 只在 `IfDef.Condition == "EDITOR"` 时记录裸 `TPair<int,int>`；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` L3637-L3643 — 同一文件里宏校验又把 `EDITORONLY_DATA` 视为 editor-only；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h` L1319、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` L4353-L4356、`Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_builder.cpp` L6881-L6907 — runtime/builder 仍把这份数据建模成不带 section 身份的裸行号并固定解释到 `scripts[0]`。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_builder.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_builder.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorEditorOnlySectionTests.cpp`
- [ ] **P9.2** 📦 Git 提交：`[AngelscriptRuntime/Preprocessor] Fix: make editor-only block metadata section-aware`
- [ ] **P9.2-T** 单元测试：锁住 `EDITOR` / `EDITORONLY_DATA` 的 section-aware block 映射
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorEditorOnlySectionTests.cpp`
  - 测试场景：
    - 正常路径：单 section `#if EDITOR` fixture 继续产出稳定 block range，并保持现有 builder/editor-only 行为不回归。
    - 边界条件：第二个 section 或 included file 中的 `#if EDITORONLY_DATA` 会写入带 section 身份的 block range；builder 对同一 fixture 的 editor-only 判定只命中对应 section，而不是首文件。
    - 错误路径：editor-only API 落在 section 外或 cache 恢复丢失 section-aware range 时，系统必须给出真实 section/file 的诊断或 stale-cache 回退，不得继续静默按 `scripts[0]` 解释。
  - 测试命名：`Angelscript.TestModule.Preprocessor.EditorOnly.SectionAwareRangesHandleEditorOnlyData`、`Angelscript.TestModule.Preprocessor.EditorOnly.SecondSectionDiagnosticsDoNotAliasFirstSection`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P9.2-T** 📦 Git 提交：`[AngelscriptTest/Preprocessor] Test: cover section-aware editor-only block metadata`

- [ ] **P9.3** 为 declared function import 持久化源码位置，并把 imported-function diagnostics 从模块第 1 行挪回真实声明
  - `P3.3` 已经要求 declared function import 与 automatic/manual module import 开关解耦，但还没有单独收口“哪一条 declared import 出错”的基本定位能力。当前 preprocessor 仅用 `Contains("(")` 把这类语句排除出 `File.Imports`，third-party `AddImportedFunction()` 也只保存签名和 source module 名，结果 `CheckFunctionImportsForNewModules()` 仍只能 `ScriptCompileError(Module, 1, ...)`。
  - 这一项要在 builder/module bridge 上引入显式 `FDeclaredImportRef` 或扩展 `sBindInfo`，至少保存 `scriptSectionIdx + declaredAtRow/Column + DeclString + FromModuleName`；编译、热重载 rebind、precompiled restore 和 dependency-invalid diagnostics 全部统一读取这份 metadata，不再让 declared function import 继续游离于 module import edge 之外。
  - 若短期不愿把位置信息直接塞进 third-party runtime 对象，也至少要在 engine 层镜像一份 `DeclaredFunctionImports` side-table 并随 artifact/cache 写盘；但无论实现位置选哪层，最终错误锚点都必须回到真实声明行列，而不是继续把所有 imported-function 失败压成“这个模块第 1 行”。
  - 来源：
    - [A] `Documents/AutoPlans/Preprocessor_Analysis.md` — “发现 56：declared function import 在创建时就丢失源码位置，所有相关错误都退化到模块第 1 行”
    - [B] `Documents/AutoPlans/DiscoveryPlans/Preprocessor_Plan.md` — “Issue-23：declared function import 在注册时丢失源码位置；Issue-22：declared function import 应进入统一依赖图与 descriptor 合同”
    - [E] `Documents/AutoPlans/ReferenceComparison/CrossComparison.md` — “D5 source identity owner：当前插件最有价值的是 metadata-owned source identity 可被调试/导航复用，declared import 也不应继续只有模块名而没有源码锚点”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` L3489-L3510 — preprocessor 仍用“是否包含 `(`”把 declared function import 整体排除出 `File.Imports`；`Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_module.cpp` L1390-L1429 — `AddImportedFunction()` 只把签名和 `importFromModule` 放进 `bindInformations`，没有 `scriptSectionIdx/declaredAt`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` L4669-L4694 — imported-function 校验失败仍统一 `ScriptCompileError(Module, 1, ...)`。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_builder.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_module.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Compiler/AngelscriptDeclaredFunctionImportDiagnosticsTests.cpp`
- [ ] **P9.3** 📦 Git 提交：`[AngelscriptRuntime/Core] Fix: preserve declared function import source locations`
- [ ] **P9.3-T** 单元测试：锁住 declared function import 的源码锚点与 automatic-import 诊断一致性
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Compiler/AngelscriptDeclaredFunctionImportDiagnosticsTests.cpp`
  - 测试场景：
    - 正常路径：同一 consumer 模块含两条合法 declared function import 时，两条导入函数都能在默认 automatic-import 配置下绑定成功并执行。
    - 边界条件：只破坏第二条 declared function import 的 provider 或签名时，diagnostic 必须锚到第二条声明的真实行列，第一条保持可用；provider hot reload 后 rebind 仍能回到新实现。
    - 错误路径：provider 模块缺失、provider 编译失败或 precompiled restore 缺失 declared-import metadata 时，系统必须给出真实 declared import 行号的 dependency-invalid / stale-cache 诊断，不得再退化到模块第 1 行或运行时晚失败。
  - 测试命名：`Angelscript.TestModule.Compiler.DeclaredImport.Diagnostics.ReportExactDeclarationLine`、`Angelscript.TestModule.Compiler.DeclaredImport.AutomaticImportRebindPreservesSourceLocations`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P9.3-T** 📦 Git 提交：`[AngelscriptTest/Compiler] Test: cover declared function import source locations`

## 补充测试矩阵

| 改进项 | 测试文件 | 测试场景 | 优先级 |
|--------|---------|---------|--------|
| P9.1 | `Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorSectionDiagnosticsTests.cpp` | multi-section 诊断锚点、precompiled provenance 一致性、stale provenance fallback | P0 |
| P9.2 | `Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorEditorOnlySectionTests.cpp` | `EDITOR`/`EDITORONLY_DATA` section-aware range、builder 映射、cache 路径一致性 | P1 |
| P9.3 | `Plugins/Angelscript/Source/AngelscriptTest/Compiler/AngelscriptDeclaredFunctionImportDiagnosticsTests.cpp` | declared import 精确行列、automatic-import rebind、provider 失败/缓存缺元数据诊断 | P0 |

## 补充验收项

24. multi-section / include / fully precompiled 三条路径必须共享同一份 `FScriptSourceLocation` 或等价 provenance 合同；模块级错误不再默认落到 `Code[0]` 或 `ModuleName`。
25. `EditorOnlyBlockLines` 必须升级为 section-aware 元数据，`EDITOR` 与 `EDITORONLY_DATA` 在 preprocessor、builder、cache 恢复三条路径上的语义完全一致。
26. declared function import 必须拥有稳定的 section/行列锚点；automatic-import、hot reload rebind 与 precompiled restore 任何一条路径失败时，错误都回到真实声明，而不是模块第 1 行。

## 补充风险与行为变化

### 风险

20. **section-aware diagnostics 会波及大量仍只传 `Module + LineNumber` 的历史调用点**
   - 如果 `ScriptCompileError`、debugger、precompiled restore 和 builder 只迁了一部分，最容易出现“live 路径定位正确，cache/断点路径仍回落首文件”的双轨行为。
   - **缓解**：先引入兼容桥和 `ensure/warning`，再逐步把所有调用点迁到 `FScriptSourceLocation`；live/cache 两条路径必须共用一份 provenance 结构。

21. **editor-only 元数据改成 section-aware 后，builder 与 cache schema 需要同时升级**
   - 若只修 preprocessor 侧的 `EDITORONLY_DATA` 判定，不同步 `scripts[n]` 映射与 precompiled schema，multi-section/include 场景依旧会在后面阶段错位。
   - **缓解**：`P9.2` 必须按“preprocessor 记录 -> builder 消费 -> cache 恢复”整链实施，并以 `NewTest-28` 风格用例在 live/precompiled 双路径共同验证。

22. **declared function import 位置元数据可能触及 third-party 结构与序列化格式**
   - 这会增加 future AS 2.38 selective migration 时的 merge 面；若设计不当，也可能让 engine-side side-table 与 third-party runtime 对同一 import 维护两份不一致状态。
   - **缓解**：优先定义 engine-owned `DeclaredFunctionImport` metadata contract，再决定是镜像到 `sBindInfo` 还是仅在 bridge 层持久化；迁移期禁止新增第二套临时行号来源。

### 已知行为变化

22. **include / multi-section / precompiled 路径下的错误文件名将从“首文件/模块名兜底”升级为“真实 section 文件 + 行列”**
   - 影响文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h` L351-L352；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` L3192-L3194、L4944-L4956；`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp` L1417-L1424、L1485-L1494、L2773-L2779。

23. **`EDITORONLY_DATA` 块将正式进入 builder/editor-only 行映射，而不再只在宏校验层被当成 editor-only**
   - 影响文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` L3092-L3123、L3637-L3643；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h` L1319；`Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_builder.cpp` L6881-L6907。

24. **declared function import 的错误将从“模块第 1 行”升级为“具体声明行列”，并在 automatic-import / hot reload / precompiled 路径下更早 surfaced**
   - 影响文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` L3489-L3510；`Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_module.cpp` L1390-L1429；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` L4669-L4694。

---

## 深化（2026-04-09 01:47:52）

### 本轮去重说明

- 已核对 `Documents/Plans/Plan_TestEngineIsolation.md`：通用 `FAngelscriptEngineScope` 基础设施与多数 scenario/helper 迁移已完成；本轮只补 `Preprocessor` / `Learning` 专用测试仍残留的 ambient-engine 假设，不重复扩展全仓测试基建。
- 已核对 `Documents/Plans/Plan_ScriptFileSystemRefactor.md`：文件 I/O 抽象与 `Preprocess()` 去全局单例已另行规划；本轮新增条目只补 parser 合同与 preprocessor-specific test contract，不重开文件系统主题。

### Phase 10：收口条件指令与测试合同，为 import/include/module identity 收尾清障

- [ ] **P10.1** 把 `#if/#ifdef/#ifndef/#elif` 收口为统一 conditional parser，补齐 bool 语义、short-circuit 与 whitespace/token 边界
  - 现有计划已经覆盖 `import`、`#include` 与 `#restrict usage`，但 directive 主 parser 仍有一组更基础的偏差：`#ifdef/#ifndef` 只看 key 是否存在、`#elif` 在前分支已命中后仍先求值、directive lexer 只接受关键字后的字面空格、`ReadIdentifier()` 又会把 trailing token 一起吞进条件体。这些缺陷会继续污染 import/include 上游的 branch reachability、editor-only line map 和 future include fail-fast 入口。
  - 这一项要把条件指令统一接到结构化 helper，例如 `TryParseConditionalDirective()` / `ParseSimpleFlagCondition()`：先做 keyword boundary 与可达性判断，再决定是否需要求值；`#ifdef/#ifndef` 必须读取 flag 的布尔值并拒绝 trailing token，`#elif` 仅在此前没有命中分支时才调用 `ParsePreProc()`，tab/space/comment 变体统一走同一套 token 边界。
  - 与 `P2.1` / `P6.2` 的关系是“先清底座，再让 include/restrict 复用同一 directive 词法合同”；不再继续让 `#if`、`#restrict`、future `#include` 各自维护一套 whitespace / short-circuit 语义。
  - 来源：
    - [A] `Documents/AutoPlans/Preprocessor_Analysis.md` — “发现 6/8/66/67：`#ifdef/#ifndef` 把 flag 当存在性判断；失活分支和已命中 `#elif` 仍会求值未知条件；directive 关键字后的 tab 会整体漏判”
    - [B] `Documents/AutoPlans/DiscoveryPlans/Preprocessor_Plan.md` — “Issue-72/69/74：conditional directive 需要 short-circuit、结构化 simple-flag parser 与严格 `#ifdef/#ifndef` body 校验”
    - [C] `Documents/AutoPlans/TestCoverage/Preprocessor_TestGaps.md` — “NewTest-4/NewTest-9/NewTest-12/NewTest-14：缺少不可达分支短路、`#ifdef/#ifndef` 布尔语义与 tab 分隔 directive 的回归”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` L3259-L3275、L3290-L3315、L4298-L4346 — `#ifdef/#ifndef` 仍用 `Find(...) != nullptr` / `== nullptr` 判断，`#elif` 在检查 `bAnyBranchTaken` 前先调用 `ParsePreProc()`，`ReadIdentifier()` 不在空格或 tab 处停止，`ParsePreProc()` 继续把未知条件直接打成 `Invalid preprocessor condition`。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`、`Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorDirectiveTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningPreprocessorTraceTests.cpp`
- [ ] **P10.1** 📦 Git 提交：`[AngelscriptRuntime/Preprocessor] Fix: unify conditional directive parsing and short-circuit semantics`
- [ ] **P10.1-T** 单元测试：锁住 conditional directive 的 reachability、bool 语义与 whitespace 合同
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorDirectiveTests.cpp`
  - 测试场景：
    - 正常路径：`#if EDITOR ... #elif UNKNOWN_FLAG ... #else ...` 在 editor 上下文成功短路，编译执行 `Entry()` 返回命中分支的值。
    - 边界条件：`#ifdef MYFLAG` / `#ifndef MYFLAG` 在 `MYFLAG=false` 时按布尔值裁剪；`#if\tEDITOR`、`#ifdef\tEDITOR` 与空格版得到完全一致的 `ProcessedCode` 和 diagnostics。
    - 错误路径：`#ifdef EDITOR EXTRA`、`#if !   `、真正可达的 `#if UNKNOWN_FLAG` 必须在预处理阶段给出明确 syntax / condition error，不得静默走错分支。
  - 测试命名：`Angelscript.TestModule.Preprocessor.Directives.ElifShortCircuitSkipsUnknownCondition`、`Angelscript.TestModule.Preprocessor.Directives.IfdefUsesFlagBooleanValue`、`Angelscript.TestModule.Preprocessor.Directives.TabSeparatedConditionalDirectivesMatchSpaceSyntax`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P10.1-T** 📦 Git 提交：`[AngelscriptTest/Preprocessor] Test: cover conditional directive parser contracts`

- [ ] **P10.2** 抽出 comment-aware statement terminator helper，让 `delegate/event` 与 import/include 共用 statement span 合同
  - 现有 `P1.1` 只把 `import` statement 解析收成单一真源，但 `delegate/event` 仍单独依赖 `FindSemicolonDirectlyAfter()`。这个 helper 只接受 `)` 后直接跟空白和 `;`，一旦在 `)` 与 `;` 之间插入 `/* ... */`，预处理器就会静默漏掉委托声明，导致 `Module->Delegates`、generated wrapper 与后续运行时绑定一起丢失。
  - 这一项要把 terminator 查找提升为共享的 `ConsumeStatementTerminator()` 或等价 helper：它必须同时跳过空白、line comment 与 block comment，并返回稳定的 statement span；`delegate/event`、`import`、future `#include` / directive body 都改为消费这同一套 span，而不是继续各自维护“哪里算语句结束”的分叉规则。
  - 这样做的目标不是单点修一个 delegate bug，而是补 `ParseIntoChunks()` 里最缺的 statement 级合同，让 `P1.1` 与 `P2.1` 后续不再被“comment-aware terminator 只在某一类语句里实现了半套”拖住。
  - 来源：
    - [B] `Documents/AutoPlans/DiscoveryPlans/Preprocessor_Plan.md` — “Issue-3/78：应以共享 cursor / terminator helper 取代 `FindSemicolonDirectlyAfter()`，并单独修复 `delegate/event` 注释边界”
    - [C] `Documents/AutoPlans/TestCoverage/Preprocessor_TestGaps.md` — “NewTest-26：`delegate/event` 在 `)` 与 `;` 之间带块注释时仍必须注册并可执行”
    - [E] `Documents/AutoPlans/ReferenceComparison/CrossComparison.md` — “delegate 在当前 Angelscript 中仍是语言一等值；预处理阶段不应因为注释边界把整条声明降级成普通源码”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` L3678-L3712、L4170-L4190 — `delegate/event` 仍在 `FindScopeCloseBracket()` 之后调用 `FindSemicolonDirectlyAfter()`，而该 helper 只接受空白字符，遇到 block comment 就直接返回 `-1`。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`、`Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorDelegateStatementTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Compiler/AngelscriptCompilerPipelineDelegateTests.cpp`
- [ ] **P10.2** 📦 Git 提交：`[AngelscriptRuntime/Preprocessor] Refactor: share comment-aware statement terminator parsing`
- [ ] **P10.2-T** 单元测试：锁住 `delegate/event` 的 comment-aware statement 合同
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorDelegateStatementTests.cpp`
  - 测试场景：
    - 正常路径：无注释的 `delegate` / `event` 声明继续生成 `Module->Delegates` 与 wrapper，不回归现有路径。
    - 边界条件：`delegate void FCommentSingle(int Value) /* note */;` 与 `event void FCommentMulti(int Value) /* note */;` 仍被预处理识别，编译后可成功绑定并执行。
    - 错误路径：真正缺失 `;` 或注释后仍没有 terminator 的声明，必须在预处理阶段给出明确语法错误，而不是静默漏掉委托元数据。
  - 测试命名：`Angelscript.TestModule.Preprocessor.Delegates.BlockCommentBeforeSemicolonStillRegisters`、`Angelscript.TestModule.Preprocessor.Delegates.MissingSemicolonReportsSyntax`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P10.2-T** 📦 Git 提交：`[AngelscriptTest/Preprocessor] Test: cover delegate statement terminator parsing`

- [ ] **P10.3** 把 preprocessor fast tests 与 learning trace 全部切到显式 current-engine contract，停止依赖 ambient engine / optional guard
  - `Plan_TestEngineIsolation.md` 已完成通用 `FAngelscriptEngineScope` 基础设施与多数 scenario/helper 迁移，但 `Preprocessor` 自身的基础自动化仍保留独立 helper：先拿一个 `Engine*`，却不把它 push 到 context stack，就直接调用 `Preprocess()`；learning trace 甚至只有在 `TryGetCurrentEngine()` 命中时才可选关闭 automatic import。这使 default automatic-import、diagnostics sink 和 context-sensitive flags 的验证继续依赖宿主环境是否刚好带着 current engine。
  - 这一项只收口 preprocessor 主题残留：`GetEngineForPreprocessorTests()` 返回的引擎必须立即包进 `FAngelscriptEngineScope`，`TGuardValue<bool>` 必须在 scope 存活期间修改当前引擎，learning trace 不再接受 “CurrentEngine 为空就 silently 退回 manual-import 语义” 的偶然等价；需要 clean engine 的 case 统一走 test helper，而不是再回退到 running production engine。
  - 这样才能让 `P1.1` / `P7.1` 里的 automatic import 与 dependency signature 回归有可信的 test bed，而不是继续在错误上下文下“验证通过”。
  - 来源：
    - [A] `Documents/AutoPlans/Preprocessor_Analysis.md` — “发现 9/70：现有 preprocessor 自动化几乎只覆盖 happy path，并主动绕开默认 automatic-import 与 directive 语义”
    - [B] `Documents/AutoPlans/DiscoveryPlans/Preprocessor_Plan.md` — “Issue-79：preprocessor 测试没有把选定 engine 放进 context stack，automatic import 与上下文 flag 覆盖依赖环境偶然状态”
    - [C] `Documents/AutoPlans/TestCoverage/Preprocessor_TestGaps.md` — “Issue-1/3/23：唯一 import 用例强制关闭 automatic import，三个 preprocessor 用例拿到 `Engine*` 却没有建立 `FAngelscriptEngineScope`”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` L702-L709、L718-L725 — `ShouldUseAutomaticImportMethodForCurrentContext()` 只读取 `TryGetCurrentEngine()`；`Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorTests.cpp` L13-L20、L76-L91、L171-L196 — 测试 helper 仅返回 `Engine*`，三个用例都未建立 `FAngelscriptEngineScope` 即直接调用 `Preprocess()`；`Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningPreprocessorTraceTests.cpp` L118-L121 — learning trace 只在 `TryGetCurrentEngine()` 命中时才可选关闭 automatic import。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningPreprocessorTraceTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestUtilities.h`
- [ ] **P10.3** 📦 Git 提交：`[AngelscriptTest/Preprocessor] Refactor: enforce explicit engine scope in preprocessor tests`
- [ ] **P10.3-T** 单元测试：锁住 preprocessor 测试自身的 current-engine 与 default automatic-import 合同
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorTests.cpp`
  - 测试场景：
    - 正常路径：在显式 `FAngelscriptEngineScope` 下运行默认 automatic-import fixture，`Preprocess()` 与 diagnostics 稳定反映当前测试引擎的配置。
    - 边界条件：在同一测试引擎上切换 `bUseAutomaticImportMethod` 前后，两次预处理得到可区分且可重复的结果；learning trace 同步记录当前模式，不再依赖 `TryGetCurrentEngine()` 是否偶然命中。
    - 错误路径：若 helper 返回引擎但未建 scope，新增自检必须直接失败或报明确断言，禁止测试再次静默跑在 ambient engine 上。
  - 测试命名：`Angelscript.TestModule.Preprocessor.Context.EngineScopeControlsAutomaticImport`、`Angelscript.TestModule.Preprocessor.Context.MissingScopeFailsSelfCheck`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P10.3-T** 📦 Git 提交：`[AngelscriptTest/Preprocessor] Test: self-check preprocessor engine context isolation`

- [ ] **P10.4** 让 preprocessor tests / learning trace 从 legacy dotted `ModuleName` 主断言迁到 source provenance / `ModuleIdentity` 兼容合同
  - 现有 `P4.2` 已经把 root-aware `ModuleIdentity` 定成主线，但基础 preprocessor 测试与 learning trace 仍把 `FilenameToModuleName()` 和固定 dotted `ModuleName` 当成唯一身份：helper 用 `FindModuleByName(...)` 定位模块，basic parse 直接断言 `ModuleName == "Tests.Preprocessor.BasicModule"`，learning trace 也把 `FilenameToModuleName()` 输出当成“后续 compilation will use”的主身份。这样一来，只要后续按 `P4.2` / `P8.1` 把 identity 从 lossy display name 收口到结构化 key，这些测试就会先把正确改造拦成回归。
  - 这一项要把 preprocessor tests 的主定位方式切到 `Code[0].RelativeFilename`、section provenance 与 future `ModuleIdentity`：保留一条 compatibility 断言覆盖 `FilenameToModuleName()` 当前显示行为，但不再让所有 fast test / learning trace 都依赖 exact dotted name。trace 文本也要把 `FilenameToModuleName()` 降级成 `DisplayModuleName` 或 compatibility note，避免继续把它暗示成唯一 authority。
  - 这样既能给 `P4.2` / `P8.1` 留出结构化 identity 的落点，也能让 import/include/module graph 改造的回归噪音显著下降。
  - 来源：
    - [A] `Documents/AutoPlans/Preprocessor_Analysis.md` — “发现 10/13/69：当前 path-to-module 仍是 lossy dotted encoding，大小写/filename-style/反斜杠等写法会使模块身份漂移”
    - [B] `Documents/AutoPlans/DiscoveryPlans/Preprocessor_Plan.md` — “Issue-80：preprocessor 测试与 learning trace 把 legacy dotted `ModuleName` 当成唯一身份，正在固化与 `ModuleIdentity` 路线冲突的旧合同”
    - [E] `Documents/AutoPlans/ReferenceComparison/CrossComparison.md` — “D11：当前插件的优势是 deterministic project/plugin roots 与稳定 compile-pipeline contract，不应继续把 module identity 固化在自由 loader 或字符串偶然形态上”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h` L26-L27 — `FilenameToModuleName()` 仍作为 public helper 暴露；`Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorTests.cpp` L36-L48、L105、L210-L218 — helper 仅用 `FindModuleByName(...)` 定位模块，basic/import tests 直接把 dotted `ModuleName` 作为主断言；`Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningPreprocessorTraceTests.cpp` L140-L143、L156、L215-L216 — learning trace 把 `FilenameToModuleName()` 记录为主身份，并继续用模块名字符串定位 importing module。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`、`Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningPreprocessorTraceTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestUtilities.h`
- [ ] **P10.4** 📦 Git 提交：`[AngelscriptTest/Preprocessor] Refactor: pivot preprocessor assertions to provenance-first module lookup`
- [ ] **P10.4-T** 单元测试：锁住 provenance-first lookup 与 legacy display-name 兼容边界
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorTests.cpp`
  - 测试场景：
    - 正常路径：按 `RelativeFilename` / section provenance 定位模块后，basic parse、import order 与 processed code 断言继续稳定通过。
    - 边界条件：保留一条单独 compatibility case 验证 `FilenameToModuleName()` 当前 display 形式，但 trace 与 helper 主路径不再依赖 exact dotted name。
    - 错误路径：当未来 `ModuleIdentity` / root-aware path 与 display name 出现差异时，测试应继续按 provenance 命中正确 module，而不是因为 display string 变化大面积误报。
  - 测试命名：`Angelscript.TestModule.Preprocessor.Identity.ProvenanceLookupOutlivesDisplayModuleName`、`Angelscript.TestModule.Preprocessor.Identity.LegacyDisplayNameCompatibilityNote`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P10.4-T** 📦 Git 提交：`[AngelscriptTest/Preprocessor] Test: decouple module identity assertions from legacy dotted names`

## 补充测试矩阵

| 改进项 | 测试文件 | 测试场景 | 优先级 |
|--------|---------|---------|--------|
| P10.1 | `Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorDirectiveTests.cpp` | `#elif` short-circuit、`#ifdef/#ifndef` bool 语义、tab/space 条件 directive 一致性、malformed conditional syntax | P0 |
| P10.2 | `Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorDelegateStatementTests.cpp` | `delegate/event` comment-aware terminator、缺失分号错误前移、wrapper/元数据继续可执行 | P1 |
| P10.3 | `Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorTests.cpp` | explicit engine scope、自检 current-engine、default automatic-import 模式稳定性 | P0 |
| P10.4 | `Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorTests.cpp` | provenance-first module lookup、legacy display-name compatibility、未来 `ModuleIdentity` 兼容 | P1 |

## 补充验收项

27. `#if/#ifdef/#ifndef/#elif` 必须共享同一套 keyword boundary、flag 语义与 short-circuit 合同；不可达分支与已命中 `#elif` 不得再触发未知 condition 诊断。
28. `delegate/event`、`import` 与 future `#include` 必须共享 comment-aware statement terminator 规则，注释不得再改变语句是否被预处理识别。
29. preprocessor fast tests 与 learning trace 必须显式绑定当前测试引擎；automatic-import、diagnostics 与 context-sensitive flags 的回归不再依赖 ambient engine。
30. preprocessor tests / learning trace 的主断言必须迁到 provenance / `ModuleIdentity` 兼容合同；legacy dotted `ModuleName` 只保留单独 compatibility coverage。

## 补充风险与行为变化

### 风险

23. **conditional directive parser 一旦统一，会同时触发多类历史宽松写法的 fail-fast**
   - 例如 `#ifdef FLAG EXTRA`、`#if !   ` 或 tab/space 混写的 conditional directive，修复后会从“偶然走错分支”转成稳定 syntax / condition error。
   - **缓解**：先以 table-driven 负向用例锁住文案与行号，再让 include/restrict 复用同一 matcher，避免一半 directive 修好、一半继续宽松。

24. **preprocessor tests 从 ambient engine 切到 explicit scope 后，可能揭露一批长期被环境掩盖的真差异**
   - 尤其是 default automatic-import、diagnostics sink 与 learning trace hook 顺序，短期内可能出现“本地绿、CI 红”被校正为稳定行为。
   - **缓解**：先在测试内加 current-engine 自检，再统一切 helper；任何依赖 running production engine 的 case 必须显式拆成 integration scenario，不再混在 fast test 里。

25. **provenance-first assertion 会与 `P4.2` / `P8.1` 的结构化 identity 改造发生阶段性并行**
   - 若 helper 同时保留 string lookup 和 provenance lookup，最容易出现“双路径都能过，但命中的不是同一 module”的过渡态。
   - **缓解**：明确把 string lookup 降为 compatibility-only helper；新增断言必须优先比较 `RelativeFilename` / section provenance，并在 helper 内对“双命中不一致”直接报错。

### 已知行为变化

25. **conditional directive 的 malformed body 将更早在 preprocessor 阶段失败**
   - 影响文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` L3259-L3315、L4298-L4346。

26. **`delegate/event` 在 `)` 与 `;` 之间插入注释后将不再被静默当作普通源码**
   - 影响文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` L3678-L3712、L4170-L4190。

27. **preprocessor fast tests 与 learning trace 将不再默认接受 ambient engine 或 exact dotted `ModuleName` 作为主合同**
   - 影响文件：`Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorTests.cpp` L13-L20、L36-L48、L76-L91、L171-L218；`Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningPreprocessorTraceTests.cpp` L118-L143、L156、L215-L216。

---

## 深化（2026-04-09 01:59:20）

### Phase 11：补 import body grammar 与 fully precompiled 顺序合同

- [ ] **P11.1** 为 module `import` 引入严格 statement/body parser，并把关键字起点判定从“裸字符片段”升级为真实语法入口
  - 前面的 `P1.1` / `P1.2` 已经准备收口 terminator 和 canonical resolver，但当前 `import` 入口仍缺 body grammar：预处理器只要看到顶层 `import ` 就一路扫到首个 `;`，然后用 `Contains("(")` 粗暴排除 declared function import。这样 `import Foo Bar;`、`import Foo + Bar;`、`import "Foo.Bar";` 之类语句都会被当成“一个不存在的模块名”，把本该在预处理阶段 fail-fast 的语法错拖到更后面的 module lookup。
  - 同一入口还继续依赖有缺陷的 `IsStartOfIdentifier()`。它只屏蔽前导数字 `0-1`，没有拦住 `2-9`，因此普通标识符拼接文本仍可能误触发 `import` 关键字入口。只要这条边界不收口，后续 `P1.2` 的 canonical resolver、`P7.1` 的 dependency signature 和 cache 命中都会继续接收未经验证的脏 `ImportName`。
  - 这一项要新增 `ParseImportStatement()` / `ParseImportReference()` 或等价结构化 helper：module import 只接受一个明确、可 canonicalize 的引用体；解析完成后除空白与注释外不得再有 trailing token。declared function import 与 module import 在这里正式分流，任何 `InvalidImportSyntax` 都要在预处理阶段直接报 line-aware 语法错，并阻止该语句进入 `File.Imports` / `ImportedModules`。
  - 来源：
    - [A] `Documents/AutoPlans/Preprocessor_Analysis.md` — “发现 12/21：`import` 漏分号会吞掉后续源码，`IsStartOfIdentifier()` 只屏蔽 `0-1`，普通标识符可误触发 `import` 入口”
    - [B] `Documents/AutoPlans/DiscoveryPlans/Preprocessor_Plan.md` — “Issue-81：`import` 语句体只按首个 `;` 截断，额外 token 会被当成模块名拖到编译阶段”
    - [C] `Documents/AutoPlans/TestCoverage/Preprocessor_TestGaps.md` — “NewTest-3/Issue-22：缺少 malformed `import` 的预处理语法回归，现有 `ImportParsing` 只做 `Contains` 弱断言，发现不了脏 `ImportedModules`”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` L3076-L3089、L3489-L3510，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` L3173-L3194 — `IsStartOfIdentifier()` 仍只排除 `0-1`，`import` 解析仍是“扫到首个 `;` + `Contains("(")`”，编译阶段继续拿原始 `ImportName` 做精确匹配并在模块第 1 行报 `could not find module ... to import`。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorImportSyntaxTests.cpp`
- [ ] **P11.1** 📦 Git 提交：`[AngelscriptRuntime/Preprocessor] Fix: parse module imports as strict statements`
- [ ] **P11.1-T** 单元测试：锁住 `import` body grammar、identifier boundary 与错误前移合同
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorImportSyntaxTests.cpp`
  - 测试场景：
    - 正常路径：`import Tests.Preprocessor.Shared;` 与配合 `P1.2` canonical resolver 的 `import ./Shared.as;` 都能被解析成单一合法引用体，并生成精确 `ImportedModules`。
    - 边界条件：普通标识符或数字后缀文本如 `Value2imported` / `foo9importHelper` 不得误触发 `import` 入口；statement 尾部仅允许空白与注释。
    - 错误路径：`import Foo Bar;`、`import Foo + Bar;`、`import "Foo.Bar";`、`import ./Shared.as extra;` 都必须在预处理阶段给出 line-aware 语法错误，不得进入 `ImportedModules`，也不得再追加一条后续 `module not found` 噪音诊断。
  - 测试命名：`Angelscript.TestModule.Preprocessor.ImportSyntax.RejectsTrailingTokens`、`Angelscript.TestModule.Preprocessor.ImportSyntax.IdentifierBoundaryDoesNotFabricateImport`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P11.1-T** 📦 Git 提交：`[AngelscriptTest/Preprocessor] Test: cover strict import statement grammar`

- [ ] **P11.2** 为 fully precompiled 恢复持久化稳定 `ModuleLoadOrder`，让 class generation / `PostInitFunctions` / test discovery 与 live preprocess 共享同一顺序输入
  - 前面的 `P2.2` / `P7.1` / `P8.1` 已经覆盖 identity、graph 与 key，但 fully precompiled 路径仍缺最基础的“模块顺序”合同。当前 cache 恢复直接 `for (auto Elem : Modules)` 遍历 `TMap`，随后 `InitialCompile()` 把返回数组原样喂给 `CompileModules()`；`CompilationQueue.Append(InModules)`、`ClassGenerator.AddModule()`、`CallPostInitFunctions()` 与 `DiscoverUnitTests()` / `DiscoverIntegrationTests()` 又继续消费这份顺序。这意味着 live preprocess 与 cache 路径即使模块集合相同，编译副作用顺序也可能分叉。
  - 这个问题不只影响 explicit `import`。automatic-import 或无-import 模块只要携带 `PostInitFunctions`、class generation side effects 或测试发现入口，就会把 `TMap` 哈希顺序直接放大成用户可见行为。若不在 schema 层补 `ModuleLoadOrder`，后面的 include graph、precompiled parity 和 test discovery 仍然会持续受到无序容器漂移影响。
  - 这一项要在 `FAngelscriptPrecompiledData` / `FAngelscriptPrecompiledModule` 中持久化 live path 已确定的稳定顺序字段，并把 `GetModulesToCompile()` 改成先恢复顺序、再按 explicit-import DAG 做二次 provider-before-consumer 校验。旧 cache 若缺少顺序元数据，必须明确 stale/fallback 到 live preprocess，而不是继续默默使用 `TMap` 迭代顺序。
  - 来源：
    - [A] `Documents/AutoPlans/Preprocessor_Analysis.md` — “发现 57/71/72：fully precompiled 不恢复显式 import 拓扑，文件发现顺序本身未稳定化，现有自动化又主动忽略顺序合同”
    - [B] `Documents/AutoPlans/DiscoveryPlans/Preprocessor_Plan.md` — “Issue-82：fully precompiled 恢复按 `TMap` 哈希顺序吐模块，live preprocess 与 cache 路径的模块副作用顺序会分叉”
    - [C] `Documents/AutoPlans/TestCoverage/Preprocessor_TestGaps.md` — “NewTest-16 与 `ImportParsing` 顺序缺口说明当前测试只对 live manual-import 有半套顺序覆盖，fully precompiled / `PostInitFunctions` / test discovery 的顺序仍是盲区”
    - [D] `Documents/AutoPlans/ArchitectureReview/ModuleStructure_ArchReview.md` — “相关模块边界审查强调 compile pipeline contract 应保持 deterministic，不应继续让无序容器或隐式 loader 决定运行时行为”
    - [E] `Documents/AutoPlans/ReferenceComparison/CrossComparison.md` — “D11：当前 Angelscript 的价值之一是 deterministic project/plugin roots 与稳定 operator surface，不应把运行时装载语义退回容器自然顺序”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp` L2758-L2783，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` L2046-L2052、L3101-L3105、L2245-L2248，`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` L5775-L5784 — cache 恢复仍直接迭代 `Modules` 这个 `TMap`，`InitialCompile()`/`CompilationQueue` 原样承接该顺序，class generator 与 `PostInitFunctions`、测试发现也继续沿用 compile 输入顺序。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/StaticJIT/AngelscriptPrecompiledModuleOrderTests.cpp`
- [ ] **P11.2** 📦 Git 提交：`[AngelscriptRuntime/StaticJIT] Fix: persist stable module load order for precompiled restore`
- [ ] **P11.2-T** 单元测试：锁住 live / fully precompiled 的模块顺序、`PostInitFunctions` 与 test discovery parity
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/StaticJIT/AngelscriptPrecompiledModuleOrderTests.cpp`
  - 测试场景：
    - 正常路径：同一组多模块 fixture 分别走 live preprocess 与 fully precompiled restore，两条路径得到完全一致的 compile order、`PostInitFunctions` 执行顺序，以及 unit/integration test 发现顺序。
    - 边界条件：automatic-import 或无-import 模块只通过 recorded load order 建立顺序时，cache 路径仍与 live path 完全一致；explicit-import 模式则在恢复 recorded order 后继续满足 provider-before-consumer。
    - 错误路径：旧 cache 缺失顺序元数据、顺序元数据与模块集合不一致，或 duplicate module 让 load-order 无法唯一恢复时，系统必须显式 stale/fallback 到 live preprocess，不得再回退到 `TMap` 哈希顺序。
  - 测试命名：`Angelscript.TestModule.StaticJIT.ModuleOrder.LiveAndPrecompiledStayAligned`、`Angelscript.TestModule.StaticJIT.ModuleOrder.MissingOrderMetadataFallsBackToLive`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P11.2-T** 📦 Git 提交：`[AngelscriptTest/StaticJIT] Test: cover precompiled module load order parity`

- [ ] **P11.3** 把 discovery / preprocessor / learning trace 的顺序与精确依赖断言从“集合语义”升级为一等测试合同
  - 即使 `P11.2` 落地，当前测试表面也仍会给顺序回归放行。discovery 用例把 `FindAllScriptFilenames()` 结果立刻塞进 `TSet`，preprocessor import 用例只检查 `ImportedModules.Contains(...)`，learning trace 也只是记录 `ImportedModules` keyword 和 `ModuleCount`，没有把 `DiscoveredFilesOrder`、`ModuleCompileOrder` 或精确依赖列表写成断言。
  - 这意味着任何“成员集合没变，但顺序已经漂移”的回归都还能继续绿着进主干。对 preprocessor / module system 来说，这类回归和真正的正确性缺陷没有本质区别，因为 duplicate-module winner、precompiled side effect、`PostInitFunctions` 顺序与 trace 输出都依赖这条合同。
  - 这一项要重构现有 `FileSystem`、`Preprocessor` 与 learning trace 断言方式：不再先丢进 `TSet`/`Contains`，而是保留 exact sequence 并比较稳定顺序；`ImportedModules` 要升级成“长度 + 顺序 + 唯一值”联合断言；learning trace 需要显式输出 `DiscoveredFilesOrder`、`ModuleCompileOrder` 与 `ImportedModulesExact` 等结构化字段，并把至少一条关键顺序从“日志提示”升级为 `TestEqual`。
  - 来源：
    - [A] `Documents/AutoPlans/Preprocessor_Analysis.md` — “发现 72：现有 discovery/preprocessor 自动化主动忽略文件顺序，重复模块冲突没有任何回归保护”
    - [B] `Documents/AutoPlans/DiscoveryPlans/Preprocessor_Plan.md` — “Issue-83：现有 discovery/preprocessor 自动化主要校验集合成员，不校验顺序合同，module-order 回归仍可静默进入主干”
    - [C] `Documents/AutoPlans/TestCoverage/Preprocessor_TestGaps.md` — “Issue-22/NewTest-16：`ImportParsing` 只用 `Contains` 检查依赖，provider-first 顺序与精确依赖列表都没有被锁住”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptTest/FileSystem/AngelscriptFileSystemTests.cpp` L258-L273，`Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorTests.cpp` L204-L218，`Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningPreprocessorTraceTests.cpp` L186-L227 — discovery 仍把结果塞进 `TSet`，manual import 仍只做 `ImportedModules.Contains(...)`，learning trace 也只检查 keyword 存在与最小事件数，没有任何 exact order 断言。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptTest/FileSystem/AngelscriptFileSystemTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningPreprocessorTraceTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptLearningTrace.h`
- [ ] **P11.3** 📦 Git 提交：`[AngelscriptTest/Preprocessor] Refactor: make module order and exact dependencies first-class assertions`
- [ ] **P11.3-T** 单元测试：新增顺序护栏，覆盖 discovery、live preprocess 与 trace 的 exact-order 合同
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorOrderContractTests.cpp`
  - 测试场景：
    - 正常路径：`FindAllScriptFilenames()` 返回 canonical relative path 的稳定顺序，manual `import` 的 `GetModulesToCompile()` 返回 provider-before-consumer，且 `ImportedModules` 精确等于预期列表而不是仅 `Contains`。
    - 边界条件：多次运行、不同路径分隔符输入、以及 learning trace 模式下，都得到完全一致的 `DiscoveredFilesOrder` / `ModuleCompileOrder` / `ImportedModulesExact` 输出。
    - 错误路径：duplicate module、cache 顺序元数据缺失或故意打乱 compile 输入时，新增断言必须直接报出期望顺序与实际顺序差异，不能再因为成员集合相同而误绿。
  - 测试命名：`Angelscript.TestModule.Preprocessor.OrderContracts.DiscoveryAndImportsUseExactSequences`、`Angelscript.TestModule.Preprocessor.OrderContracts.LearningTraceEmitsStructuredOrder`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P11.3-T** 📦 Git 提交：`[AngelscriptTest/Preprocessor] Test: add order-aware discovery and trace contracts`

## 补充测试矩阵

| 改进项 | 测试文件 | 测试场景 | 优先级 |
|--------|---------|---------|--------|
| P11.1 | `Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorImportSyntaxTests.cpp` | `import` 严格语法体、identifier boundary、防重复噪音诊断 | P0 |
| P11.2 | `Plugins/Angelscript/Source/AngelscriptTest/StaticJIT/AngelscriptPrecompiledModuleOrderTests.cpp` | live/precompiled compile order、`PostInitFunctions`、test discovery parity 与 stale fallback | P0 |
| P11.3 | `Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorOrderContractTests.cpp` | discovery exact order、manual import exact dependency list、learning trace 结构化顺序输出 | P1 |

## 补充验收项

31. module `import` 必须只接受一个明确引用体；trailing token、quoted string、运算符拼接与假关键字入口都必须在预处理阶段失败，且不得进入 `ImportedModules`。
32. fully precompiled 恢复必须持久化并恢复稳定 `ModuleLoadOrder`；class generation、`PostInitFunctions` 与 test discovery 在 live/cache 两条路径上的顺序必须完全一致。
33. discovery / preprocessor / learning trace 的自动化必须把 exact order 与 exact dependency list 写成一等断言；`Contains` / `TSet` 不得再作为此类合同的唯一验证方式。

## 补充风险与行为变化

### 风险

26. **严格 `import` grammar 会把历史上的宽松容错一次性前移成预处理错误**
   - 例如 `import Foo Bar;`、`import "Foo.Bar";` 或带数字拼接的假关键字文本，修复后会从“后段 module lookup 失败”转成稳定 syntax error。
   - **缓解**：先补 line-aware 负向用例与精确文案，再在 diagnostics 中同时回显原始 `RawImportText` 与解析阶段，减少迁移期定位成本。

27. **precompiled schema 新增 `ModuleLoadOrder` 会触发 cache 版本升级与 fallback 行为变化**
   - 若旧 cache 没有顺序字段，或者字段与当前模块集合不一致，修复后将不再继续命中旧 cache，而是转入 stale/fallback。
   - **缓解**：把顺序元数据缺失视为显式版本分界，首轮落地时优先回退 live preprocess，并在 trace/log 中打印缺失原因，避免静默使用 `TMap` 顺序。

28. **顺序型测试从成员断言升级为 exact-order 断言后，短期内可能把既有 nondeterminism 集中暴露出来**
   - 尤其是 discovery、learning trace 与 fully precompiled parity 场景，现有主干如果仍有隐式顺序漂移，新测试会立即报红。
   - **缓解**：先把 runtime/cache 顺序合同在 `P11.2` 固化，再同步改造测试；对仍只关心 membership 的老用例明确重命名，避免混淆“成员存在”与“顺序正确”。

### 已知行为变化

28. **malformed module `import` 将在预处理阶段直接失败，而不再拖到后段 `module not found`**
   - 影响文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` L3076-L3089、L3489-L3510；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` L3173-L3194。

29. **fully precompiled 路径将恢复 live compile 的稳定模块顺序；缺失顺序元数据时会 stale/fallback，而不是继续使用 `TMap` 迭代顺序**
   - 影响文件：`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp` L2758-L2783；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` L2046-L2052、L3101-L3105、L2245-L2248；`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` L5775-L5784。

30. **discovery / preprocessor / learning trace 测试将从 membership 断言升级为 exact-order / exact-list 断言**
   - 影响文件：`Plugins/Angelscript/Source/AngelscriptTest/FileSystem/AngelscriptFileSystemTests.cpp` L258-L273；`Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorTests.cpp` L204-L218；`Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningPreprocessorTraceTests.cpp` L186-L227。

---

## 深化（2026-04-09 06:35:21）

### Phase 12：补宏括号与 namespace 作用域的结构化边界

- [ ] **P12.1** 让 `FindScopeCloseBracket()` 升级为 string/comment-aware scope scanner，并统一服务 `UCLASS/USTRUCT/UINTERFACE/UENUM/UMETA` 宏参数解析
  - 现有 `P10.2` 只修了 `delegate/event` 在 `)` 后 comment 的 terminator 边界，但 class/enum 宏仍共享一个更底层的缺口：`FindScopeCloseBracket()` 只对 `(` / `)` 做裸计数，不跳过字符串字面量或注释，因此合法 metadata 文本里的 `)` 仍会被提早当成宏结束位置。
  - 这一项要把括号扫描提升成共享 cursor helper，例如 `FindScopeCloseBracketRespectingStringsAndComments()`：复用预处理器已有的 string/comment 语义，至少覆盖 escaped quote、line/block comment 与字符串中的嵌套括号。`UMETA`、`UCLASS/USTRUCT/UINTERFACE/UENUM` 以及后续仍依赖括号闭合的 parser 入口统一迁到这条 helper，不再继续把“字符串里的 `)` 不算结束”散落成特判。
  - 目标不是只修 `DisplayName="Do (Test)"` 这一个 fixture，而是把“宏参数边界”收成稳定合同；否则 `ProcessClassMacro()`、enum/value metadata 与 compiler smoke 仍会继续消费被截断的 `Arguments`，把真正的 parser 失真拖到更后面的反射或 generated code 阶段。
  - 来源：
    - [A] `Documents/AutoPlans/Preprocessor_Analysis.md` — “发现 7：宏参数括号匹配不识别字符串，`UCLASS/USTRUCT/UINTERFACE/UENUM/UMETA` 的带 `)` 文本会被提前截断”
    - [C] `Documents/AutoPlans/TestCoverage/Preprocessor_TestGaps.md` — “NewTest-25：macro specifier 字符串里出现 `)` 仍必须完整解析并写入 metadata”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` L3539-L3549、L3571-L3581、L3592-L3601、L4145-L4167 — `UMETA`、`UCLASS/USTRUCT/UINTERFACE` 与 `UENUM` 仍直接调用只做裸括号计数的 `FindScopeCloseBracket()`，扫描器没有任何 string/comment-aware 分支。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorMacroBoundaryTests.cpp`
- [ ] **P12.1** 📦 Git 提交：`[AngelscriptRuntime/Preprocessor] Fix: make macro scope scanning string-aware`
- [ ] **P12.1-T** 单元测试：锁住 macro specifier 中字符串/注释括号的完整解析合同
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorMacroBoundaryTests.cpp`
  - 测试场景：
    - 正常路径：`UCLASS(DisplayName="Do (Test)")`、`UFUNCTION(meta=(ToolTip="Accepts ) in text"))`、`UENUM(DisplayName="State ) Ready")`、`UMETA(DisplayName="Alpha ) Value")` 都完整进入 metadata，不发生截断。
    - 边界条件：同一宏体内混合多个 specifier、line/block comment 紧贴 specifier、字符串里同时出现 `(` 与 `)` 时，`Arguments` 仍保持完整并按原始顺序传给后续 `ProcessClassMacro()` / `DetectEnum()`。
    - 错误路径：真正缺失闭括号的 macro 仍要在预处理阶段报 line-aware 语法错误，不得因为新 scanner 宽松化而退回静默截断或后段编译错误。
  - 测试命名：`Angelscript.TestModule.Preprocessor.Macros.StringLiteralParenthesesDoNotTerminateScope`、`Angelscript.TestModule.Preprocessor.Macros.UnclosedSpecifierStillFailsEarly`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P12.1-T** 📦 Git 提交：`[AngelscriptTest/Preprocessor] Test: cover string-aware macro scope parsing`

- [ ] **P12.2** 把 `namespace` 解析改成 brace-committed block contract，并在 EOF 与 generated helper 阶段校验栈平衡
  - 当前 `ParseIntoChunks()` 只要看到顶层 `namespace` + whitespace 就立即 `NamespaceStack.Add(ReadIdentifier(...))`，既不验证后续是否真的进入 `{}` block，EOF 也只检查 `IfDefStack` 不检查 `NamespaceStack`。结果是 `namespace Foo` 这种缺少 `{` 的输入会把后续所有 top-level chunk、`ClassDesc->Namespace` 与 generated `StaticClass()` helper 一起静默绑到伪造命名空间下。
  - 这一项要把 namespace 收口成两阶段状态机：先解析 `PendingNamespaceIdentifier`，只有确认随后的第一个结构化 token 真的是 `{` 才提交到 `NamespaceStack`；`}` 退出与 EOF 都要做 namespace balance 校验，并在失败时阻止 `Chunk.Namespace`、`File.GeneratedCode` 与类描述符继续消费脏 namespace。对合法的 namespaced annotated class，还要同步锁住 `ClassDesc->Namespace`、`__StaticType_*` 变量和 `Gameplay::UNamespaceCarrier::StaticClass()` helper 的一致性。
  - 这不是单纯的语法洁癖。当前 namespace 状态既驱动 chunk 描述，也直接驱动 generated helper；如果不先把 block 进入/退出合同固定下来，后续 import/include/module graph 再稳定，generated code 这一侧仍会继续因为隐式栈状态污染而漂移。
  - 来源：
    - [A] `Documents/AutoPlans/Preprocessor_Analysis.md` — “发现 74：`namespace` 关键字一出现就直接压栈，却从不校验后续是否真的进入 `{}` 作用域”
    - [C] `Documents/AutoPlans/TestCoverage/Preprocessor_TestGaps.md` — “NewTest-40：namespaced annotated class 必须生成可用的 namespace-scoped `StaticClass()` helper，当前目标集没有任何 namespace fixture”
    - [D] `Documents/AutoPlans/ArchitectureReview/ModuleStructure_ArchReview.md` — “相关模块边界审查强调 compile pipeline contract 应保持 deterministic，不应继续依赖隐式状态与偶然的实现细节”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` L3159-L3161、L3759-L3765、L3823-L3824、L3934-L3938、L874-L895、L1173-L1173 — `Chunk.Namespace` 直接来自 `NamespaceStack`，扫描 `namespace` 时立即压栈，弹栈只在顶层 `}`，EOF 不校验 `NamespaceStack`，而 generated `__StaticType_*` 与 `StaticClass()` helper 又继续直接消费 `ClassDesc->Namespace`。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorNamespaceTests.cpp`
- [ ] **P12.2** 📦 Git 提交：`[AngelscriptRuntime/Preprocessor] Fix: require brace-scoped namespace blocks`
- [ ] **P12.2-T** 单元测试：锁住 namespaced annotated class helper 与 malformed namespace 的 fail-fast 合同
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorNamespaceTests.cpp`
  - 测试场景：
    - 正常路径：`namespace Gameplay { UCLASS() class UNamespaceCarrier : UObject { UFUNCTION() int GetValue() { return 42; } } }` 预处理后保留 `ClassDesc->Namespace == "Gameplay"`，generated code 包含 `__StaticType_UNamespaceCarrier` 与 `Gameplay::UNamespaceCarrier::StaticClass()` helper，完整编译执行 `Entry()` 返回 `42`。
    - 边界条件：嵌套 namespace `Gameplay::Inventory` 与同文件中多个顶层 namespace block 都能生成稳定 `Chunk.Namespace` 与 helper，不会把上一个 block 的 namespace 泄漏给下一个 chunk。
    - 错误路径：`namespace Gameplay` 缺少 `{`，或 namespace block 到 EOF 仍未闭合时，`Preprocess()` 必须直接失败并给出 line-aware 语法错误；失败后不得再让后续 chunk、class desc 或 generated helper 继承脏 namespace。
  - 测试命名：`Angelscript.TestModule.Preprocessor.Namespace.AnnotatedClassKeepsScopedHelper`、`Angelscript.TestModule.Preprocessor.Namespace.MissingBraceFailsBeforeNamespaceLeak`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P12.2-T** 📦 Git 提交：`[AngelscriptTest/Preprocessor] Test: cover namespace block validation and helper generation`

## 补充测试矩阵

| 改进项 | 测试文件 | 测试场景 | 优先级 |
|--------|---------|---------|--------|
| P12.1 | `Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorMacroBoundaryTests.cpp` | macro specifier 中的 `)` / 注释边界、真实未闭合 macro 早失败 | P1 |
| P12.2 | `Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorNamespaceTests.cpp` | namespaced annotated class helper、namespace 缺 `{`/缺 `}` fail-fast 与无泄漏 | P1 |

## 补充验收项

34. `UCLASS/USTRUCT/UINTERFACE/UENUM/UMETA` 的参数扫描必须忽略字符串与注释里的 `)`；合法 metadata 不得再被提前截断，而真正未闭合的 macro 仍需在预处理阶段稳定报错。
35. `namespace` 只有在确认进入 `{}` block 后才能影响 `Chunk.Namespace`、`ClassDesc->Namespace` 与 generated `StaticClass()` helper；缺失 `{` 或未闭合 namespace 必须在预处理阶段直接失败，且不得污染后续 chunk 或生成代码。

## 补充风险与行为变化

### 风险

29. **string/comment-aware macro scanner 会把一批历史上“能编过但 metadata 已损坏”的输入前移成确定性 parser 合同**
   - 某些脚本过去可能只是因为 specifier 被提前截断、但后段又碰巧没用到对应 metadata，所以一直未显性报错；修复后这类输入要么保留完整 metadata，要么更早失败。
   - **缓解**：先用 `NewTest-25` 对应的 table-driven fixture 锁住合法字符串样本，再补一组真正缺括号的负向用例，确保“合法更稳、非法更早报错”的边界清晰。

30. **namespace 改成 brace-committed 后，会把历史上的宽松 `namespace Foo` 写法一次性暴露成预处理错误**
   - 过去这类输入可能只是把后续声明静默挂到伪造 namespace 下，没有在第一时间报错；修复后将直接阻断编译。
   - **缓解**：先补正向的 namespaced annotated class helper 回归，再补 malformed namespace 负向用例，确保迁移期能明确区分“合法 namespace block”与“语法错误但过去被静默吞掉”的脚本。

### 已知行为变化

31. **macro specifier 中合法出现的 `)` 将完整保留到 class/function/enum metadata，而不再被预处理器提前截断**
   - 影响文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` L3539-L3549、L3571-L3581、L3592-L3601、L4145-L4167。

32. **malformed `namespace` 声明将直接在预处理阶段失败，而不再以“伪造 namespace 泄漏”形式污染后续 chunk 与 generated helper**
   - 影响文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` L3159-L3161、L3759-L3765、L3823-L3824、L3934-L3938、L874-L895、L1173。

---

## 深化（2026-04-09 06:42:43）

### Phase 13：补 module graph 执行层与 hot reload 路径身份合同

- [ ] **P13.1** 深化 `P7.1/P7.3/P8.3`：把 `automatic import` 热重载传播从固定点全表扫描收口为 artifact-backed reverse dependency graph
  - 现有 Plan 已要求 `ResolvedModuleDependencies`、`IncludeOwners` 与 `SourceKeyToOwningModules`，但 automatic-import 的热重载传播仍没有真正消费这份图。当前 `PerformHotReload()` 在 automatic-import 分支先用 `RelativeFileToModule` 找 changed file 的 owner，再通过 `while (bDidMarkModules)` 多轮扫描 `ActiveModules` 和每个 `moduleDependencies` 才逐步找出依赖者；反过来 explicit-import 分支已经临时构建了 `ReverseDeps`。这让同一套 module graph 在两种 import 模式下继续维持两条不同算法，且 automatic-import 路径的成本会随模块数和依赖链长度一起放大。
  - 这一项要把 reverse dependency graph 变成 compile artifact/runtime state 的正式组成：在 live compile、hot reload swap-in 与 fully precompiled restore 后统一生成 `ReverseModuleDependencies`，并让 automatic/manual import、include owner invalidation 与 shared source owner-set 全部走同一个 `CollectAffectedModulesFromChangedSources()` queue/BFS helper。changed source 先映射到 canonical owner module 集合，再沿 reverse graph 一次性展开，不再允许 automatic-import 继续保留“全表扫描收敛”，也不再让 explicit-import 维护单独的临时 `ReverseDeps` 分支。
  - 这条深化只收口“哪些模块受影响”的 authority，不改当前 `SoftReload / FullReloadSuggested / FullReloadRequired` 的决策规则；它服务的是当前已被 [E] 确认的 compile-transaction reload state machine，而不是另起一套 watcher 语义。与 `Documents/Plans/Plan_AngelscriptEngineBindAndFileWatchValidation.md` 的区别也要保持明确：那份 Plan 关注 queue/operator 行为可观测性，这一项只修 module graph 执行内核。
  - 来源：
    - [A] `Documents/AutoPlans/Preprocessor_Analysis.md` — “发现 36：automatic-import 的热重载依赖扩散仍靠固定点全表扫描 `moduleDependencies`，会把单次变更放大成多轮 `O(V*E)`；模块图执行层仍缺 reverse graph owner”
    - [E] `Documents/AutoPlans/ReferenceComparison/CrossComparison.md` — “`D4` 明确当前 Angelscript 的强项是 `file queue -> compile -> ReloadRequirement -> soft/full switch` 的显式 reload state machine；依赖传播应继续作为这套 transaction contract 的确定性前置步骤，而不是散落成运行时隐式扫描”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` L289-L304 — preprocess 产物目前只把 `RelativeFilename/AbsoluteFilename/CodeHash` 填进 `FCodeSection`，没有任何 reverse-dependency artifact；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` L2298-L2373、L2400-L2415 — automatic-import 分支仍通过 `while (bDidMarkModules)` 全表扫 `ActiveModules`，而 explicit-import 分支又单独临时构建 `ReverseDeps`，两条路径没有共享同一张 reverse graph。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadDependencyGraphTests.cpp`
- [ ] **P13.1** 📦 Git 提交：`[AngelscriptRuntime/Core] Refactor: unify hot reload dependency propagation on reverse module graph`
- [ ] **P13.1-T** 单元测试：锁住 automatic-import 热重载的 reverse graph 传播、去重与模式一致性
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadDependencyGraphTests.cpp`
  - 测试场景：
    - 正常路径：`Base <- Shared <- Consumer <- Leaf` 在 `automatic import = true` 下修改 `Base.as` 时，受影响集合稳定扩散到整条依赖链，且 compile/reload 后新实现生效。
    - 边界条件：diamond graph 或多个 changed source 同时命中同一个下游模块时，每个 owner 只入队一次；同一组 fixture 在 manual/automatic import 两种模式下得到等价的 affected-module 集合。
    - 错误路径：某个 changed source 当前没有 active owner，或某个中间模块因上轮失败暂时没有 `ScriptModule` 时，系统只对可识别 owner 做确定性扩散并保持旧代码存活，不得进入无限扫描、重复入队或漏掉其余合法依赖者。
  - 测试命名：`Angelscript.TestModule.HotReload.DependencyGraph.AutomaticImportUsesReverseGraph`、`Angelscript.TestModule.HotReload.DependencyGraph.SharedDependentsQueueOnce`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P13.1-T** 📦 Git 提交：`[AngelscriptTest/HotReload] Test: cover reverse dependency graph propagation`

- [ ] **P13.2** 深化 `P4.2/P8.3`：把 hot reload 全部账本从原始路径字符串收口到 canonical `SourceKey`
  - 现有 Plan 已要求 `ModuleIdentity`、`SourceKeyToOwningModules` 与 root-aware filename contract，但 hot reload 自身的状态账本还没有跟上。当前 `FAngelscriptPreprocessor::AddFile()` 和 `FCodeSection` 继续保存原始 `RelativeFilename/AbsoluteFilename`；`FFilenamePair` 的 hash/equality 也直接比较这两个原文；`FileHotReloadState` 用 `RelativePath` 做 key，`PreviouslyFailedReloadFiles` / `QueuedFullReloadFiles` / `AlreadyDeletedFiles` 则全靠原始 `FFilenamePair` 精确命中。与此同时，`GetModuleByFilename()` 又会对绝对路径走 `IgnoreCase` 和 `MakePathRelativeTo_IgnoreCase()`。结果是同一物理脚本可能在 module lookup 上被视为同一文件，却在 reload 队列、失败重试和删除检测里被拆成多个 key。
  - 这一项要把 `P4.2` 里的 root-aware identity 继续压到热重载账本层，新增或复用 engine-owned `FScriptSourceKey`：至少统一 `RootKey`、canonical absolute path、canonical relative path、display path 与稳定 hash。`DiscoverScriptRoots()`、`FindAllScriptFilenames()`、watcher/polling 事件接入、`Preprocessor.AddFile()`、`FileHotReloadState`、`PreviouslyFailedReloadFiles`、`QueuedFullReloadFiles`、`AlreadyDeletedFiles`、`FilesToHotReload` 与 `RelativeFileToModule` 都只认这份 canonical key；诊断和 UI 再单独保留原始 display path，不允许再拿 raw string equality 兼任身份判断。
  - 这样可以把 Windows 上最容易漂移的 casing、slash 与 root-relative alias 一次性压回统一身份层，也能让 `DiscardModule()`、delete path、retry queue 与 `GetModuleByFilename()` 真正消费同一把钥匙。它是 `P4.2/P8.3` 的执行层收口，不与 watcher 行为测试或目录扫描策略重复。
  - 来源：
    - [A] `Documents/AutoPlans/Preprocessor_Analysis.md` — “发现 27/38：`FileHotReloadState`、失败队列与删除队列仍按原始路径字符串精确匹配，跨 casing/root alias 会把同一物理文件拆成多个 key；script root 身份在状态表边界继续丢失”
    - [E] `Documents/AutoPlans/ReferenceComparison/CrossComparison.md` — “`D11` 强调当前 Angelscript 的优势是 deterministic project/plugin roots 与稳定 operator surface；若 hot reload 账本继续允许 path alias 分裂，同一脚本会在 operator layer 看起来像多个文件，直接削弱这条确定性合同”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` L91-L103、L289-L304 — `AddFile()` 与最终 `FCodeSection` 仍原样保存 `RelativeFilename/AbsoluteFilename`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h` L361-L365、L1426-L1433 — `FFilenamePair` 仅按原始绝对/相对路径做 hash 和 equality；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` L1089-L1094、L2451-L2452、L2880-L2888、L3029-L3049 — discard/remove、delete path、polling 状态表继续用 raw string key，而 `GetModuleByFilename()` 却已经改走 `IgnoreCase` 与 `MakePathRelativeTo_IgnoreCase()`。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadSourceKeyTests.cpp`
- [ ] **P13.2** 📦 Git 提交：`[AngelscriptRuntime/Core] Fix: canonicalize hot reload state keys on SourceKey`
- [ ] **P13.2-T** 单元测试：锁住 casing/slash/root alias 变体下的 hot reload 单一身份合同
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadSourceKeyTests.cpp`
  - 测试场景：
    - 正常路径：同一脚本分别以不同大小写、不同 slash 形式或等价 root-relative 写法进入 lookup/reload 路径时，最终都命中同一个 module 与同一个 queue entry。
    - 边界条件：`PreviouslyFailedReloadFiles`、`QueuedFullReloadFiles` 与 delete/retry 路径在路径写法变化后仍能命中原先状态，不会因为 alias 差异留下第二份队列项。
    - 错误路径：先以一种路径写法触发失败，再以另一种 alias 写法触发删除或修复重载时，系统必须正确清理旧状态并仅保留一份 canonical key，不得出现 missed delete、重复 full-reload queue 或 stale diagnostics 悬挂。
  - 测试命名：`Angelscript.TestModule.HotReload.SourceKey.CasingVariantsShareOneQueueEntry`、`Angelscript.TestModule.HotReload.SourceKey.RetryAndDeleteUseCanonicalIdentity`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P13.2-T** 📦 Git 提交：`[AngelscriptTest/HotReload] Test: cover canonical source keys for reload ledgers`

## 补充测试矩阵

| 改进项 | 测试文件 | 测试场景 | 优先级 |
|--------|---------|---------|--------|
| P13.1 | `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadDependencyGraphTests.cpp` | automatic-import reverse graph、diamond graph 去重、manual/automatic affected-set 一致性 | P1 |
| P13.2 | `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadSourceKeyTests.cpp` | casing/slash/root alias 单一身份、retry/delete/full-reload queue 命中一致性 | P0 |

## 补充验收项

36. automatic/manual import、include owner invalidation 与 shared source reload 必须共享同一条 reverse graph affected-set 收集逻辑；受影响模块集合不得再依赖 `ActiveModules` 迭代顺序或路径分支。
37. hot reload 的 lookup、失败重试、删除检测、full-reload queue 与 `GetModuleByFilename()` 必须基于同一份 canonical `SourceKey`；大小写、分隔符与等价 root alias 不得再把同一物理脚本拆成多个状态实体。

## 补充风险与行为变化

### 风险

31. **reverse dependency graph 若不在 live compile、swap-in 与 fully precompiled restore 后同步刷新，可能把旧图继续用于新模块集合**
   - 这会把 `P13.1` 从“收敛 authority”反而变成“缓存错误 graph”的新来源，尤其是在 reload 失败后保留旧代码、随后再次重试的场景里更隐蔽。
   - **缓解**：把 reverse graph 刷新挂到统一 artifact 生成点，并用 `P13.1-T` 同时覆盖 live compile、失败后重试与 manual/automatic 两种模式，禁止手写分支各自维护私有图。

32. **SourceKey 迁移会一次性暴露历史脚本路径大小写不一致或 alias 依赖**
   - 某些工程过去可能依赖“同一文件不同写法被当成不同队列项”的偶然行为；收口后这类漂移会消失，但也可能让现有日志、失败重试或删除窗口的表现一次性变得更严格。
   - **缓解**：在 ingest 端先输出 canonicalization trace 或 warning，测试中锁住“一个物理文件只保留一份 queue entry”的边界，再逐步清理历史 fixture 的路径别名。

### 已知行为变化

33. **automatic-import 热重载将通过共享 reverse dependency graph 收集受影响模块，而不再依赖多轮全表扫描 `moduleDependencies`**
   - 影响文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` L2298-L2373、L2400-L2435。

34. **hot reload 的失败队列、删除检测与 full-reload queue 将把路径大小写/分隔符/root alias 变体视为同一逻辑脚本**
   - 影响文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h` L361-L365、L1426-L1433；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` L1089-L1094、L2451-L2452、L2880-L2888、L3029-L3049；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` L91-L103、L289-L304。

---

## 深化（2026-04-09 06:53:20）

本轮复核发现用户给定的 [B] 输入 `Documents/AutoPlans/DiscoveryPlans/Preprocessor_Plan.md` 当前仓库不存在，因此以下内容只基于现存 [A] / [C] / [D] / [E] 分析文档与源码复核，作为对既有 import / include / module system 主线的实现深化，不新增脱离当前主线的主题。

### Phase 14：把 parser owner 与 dependency edge owner 收成单一合同

- [ ] **P14.1** 深化 `P2.1/P6.2/P10.1/P10.2/P11.1`：先抽出统一的 statement lexer / directive cursor，再让 `#if/#restrict/#include/import` 共用同一套 span authority
  - 现有 Plan 已分别要求修 `import` body、conditional directive、`#restrict usage` 与 future `#include`，但源码里这四类入口仍由四套互不一致的裸扫描拼出来：conditional directive 走 `Strncmp + ReadIdentifier + ParsePreProc`，restriction 走 `Strncmp + ReadUntilWhitespace + KillRawLine`，`import` 走“扫到首个 `;`”，blanking 又再走另一套 `KillRawLine` 规则。只要继续按症状逐条补丁，include graph 一落地，comment/whitespace/body-span 语义就会再次分叉。
  - 这一项要先引入共享的 `FPreprocessorStatementSpan` / `FDirectiveCursor` 或等价结构，把 keyword、body span、statement terminator、leading/trailing comment 与 branch-active 信息一次性收口；`#if/#ifdef/#ifndef/#elif`、`#restrict usage`、future `#include` 和 module `import` 全部改为消费这份结构化结果。`KillRawLine()` 只保留为“如何 blank 已解析语句”的输出层 helper，不再兼任 parser authority。
  - 这样做不是重复 `P6.2/P10.1/P11.1`，而是给这些 Phase 补一个共同前置 owner。没有这一步，tab/space、comment-tail、comment-only body、trailing garbage 与 malformed statement 的边界仍会在不同 directive 家族里各自回归。
  - 来源：
    - [A] `Documents/AutoPlans/Preprocessor_Analysis.md` — “发现 12/67/75：`import` 仍按首个 `;` 截断；directive lexer 只接受关键字后的字面空格；`#restrict usage` 的 comment 尾巴会混入 pattern，说明 statement/body/span 语义仍分散在多套 scanner 里”
    - [C] `Documents/AutoPlans/TestCoverage/Preprocessor_TestGaps.md` — “NewTest-3/NewTest-5/NewTest-14/NewTest-15：缺失分号、`#include` 合同、tab 分隔 directive 和 dead-branch restriction 目前都缺少共享 scanner 层的回归保护”
    - [D] `Documents/AutoPlans/ArchitectureReview/ModuleStructure_ArchReview.md` — “架构审查持续强调 compile pipeline contract 要 deterministic，不能继续依赖多处隐式解释规则”
    - [E] `Documents/AutoPlans/ReferenceComparison/CrossComparison.md` — “`D11` 明确当前插件应保持 deterministic operator surface；statement parser 若继续按 ad-hoc 字符串匹配分裂，就会直接削弱这条确定性合同”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` L3257-L3390、L3490-L3510、L4313-L4356 — conditional directive、`#restrict usage`、module `import` 与 blanking 仍分别依赖 `Strncmp`、`ReadIdentifier`、`ReadUntilWhitespace`、裸 `;` 扫描与 `KillRawLine()`，没有单一 statement/span owner。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorStatementScannerTests.cpp`
- [ ] **P14.1** 📦 Git 提交：`[AngelscriptRuntime/Preprocessor] Refactor: unify statement lexer for directives and imports`
- [ ] **P14.1-T** 单元测试：锁住 shared statement lexer 的 whitespace/comment/body-span 合同
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorStatementScannerTests.cpp`
  - 测试场景：
    - 正常路径：`#if\tEDITOR`、`#restrict usage allow Gameplay.* // note`、`import Tests.Preprocessor.Shared /* note */;` 与 `#include "Shared.as" // note` 都能产出稳定的 statement span、blanking 结果与规范化 body。
    - 边界条件：tab/space 混写、尾随 block comment、statement 结尾前只有注释或空白时，scanner 仍给出与空格版一致的 parser 结果。
    - 错误路径：`import Foo Bar;`、`#restrict usage allow // note`、裸 `#include` 或 `#include "Shared.as" extra` 都必须在同一 parse stage 失败，且不得向 `ImportedModules`、`UsageRestrictions` 或 future include edge 写入半成品状态。
  - 测试命名：`Angelscript.TestModule.Preprocessor.Scanner.SharedStatementLexerNormalizesWhitespaceAndComments`、`Angelscript.TestModule.Preprocessor.Scanner.MalformedStatementsFailBeforeCommit`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P14.1-T** 📦 Git 提交：`[AngelscriptTest/Preprocessor] Test: add shared statement lexer coverage`

- [ ] **P14.2** 深化 `P7.1/P8.3/P11.2/P13.1`：把 explicit import、future include、automatic-import 回写与 hot reload owner-set 合并成同一份 `DependencyEdge` artifact
  - 当前 Plan 已分别要求 resolved import graph、include owner invalidation、reverse dependency graph 与 precompiled order/freshness，但源码 authority 仍然分裂成多套形状：预处理器只存 `ImportedModules` 字符串数组，hot reload 入口用单值 `RelativeFileToModule`，运行时 restriction/automatic-import 又读 `moduleDependencies`，precompiled cache 仍只序列化 `ModuleName + CodeHash + ImportedModules`。这些结构彼此只能部分投影，无法保证 include/import/automatic-import/live/precompiled/hot reload 真正共用同一张图。
  - 这一项要新增统一的 `FDependencyEdgeArtifact` / `FModuleDependencyEdge` 或等价结构：至少包含 `EdgeKind(import/include/automatic)`, `FromModuleKey`, `ToModuleKey` 或 `SourceKey`, `SourceLocation`, `RawText`, `ResolveState`, `OrderIndex`。explicit import 在 preprocessor 阶段写入，include graph 写入 file-edge，automatic-import 在 compile graph 稳定后回写同一结构；reverse dependency graph、dependency signature、precompiled schema 与 hot reload owner-set 只消费这份 artifact，不再继续各自从 `ImportedModules`、`RelativeFileToModule` 或 `moduleDependencies` 重新拼图。
  - 与现有 `P7.1/P8.3/P11.2/P13.1` 的区别是：那些条目修的是 graph 行为，这一项修的是 graph 的唯一 owner。没有统一 edge schema，future include、automatic-import 回写与 precompiled restore 仍会在不同阶段各自保存半套依赖事实。
  - 来源：
    - [A] `Documents/AutoPlans/Preprocessor_Analysis.md` — “发现 28/36/57/59/60：`#include` 尚未建模，automatic-import 依赖扩散仍读运行时 `moduleDependencies`，fully precompiled 不恢复 import 拓扑，dependency/hash 对顺序和 graph ownership 的合同也不一致”
    - [C] `Documents/AutoPlans/TestCoverage/Preprocessor_TestGaps.md` — “NewTest-1/NewTest-5/NewTest-16/NewTest-17 与 Issue-17：循环 import、include 合同、provider-first 顺序与 duplicate import 仍是分散回归，说明测试侧也还没有单一 dependency-edge view”
    - [D] `Documents/AutoPlans/ArchitectureReview/ModuleStructure_ArchReview.md` — “架构审查反复指出模块/工具链 owner 需要单一 contract，不能继续让不同阶段各自维护影子拓扑”
    - [E] `Documents/AutoPlans/ReferenceComparison/CrossComparison.md` — “`D11` 强调当前插件的优势是 deterministic project/plugin roots 与稳定 operator surface；依赖图若继续分裂成多套 owner，determinism 会直接在 cache 和 reload 边界丢失”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h` L101-L161 — 预处理器当前只有 `FImport` 和裸 `Module->ImportedModules` 视图，没有 include/source-owner edge 结构；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` L2298-L2333、L2400-L2415、L3173-L3195、L4262-L4290、L4566-L4573 — hot reload 依赖 `RelativeFileToModule` 与临时 `ReverseDeps`，编译与 hash 依赖 `ImportedModules`，restriction/automatic-import 又读取 `moduleDependencies`；`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp` L1417-L1424、L2759-L2779 — precompiled 仍只保存/恢复 `CodeHash`、`ImportedModules` 和首 section 文件名，没有统一 dependency-edge artifact。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptDependencyEdgeArtifactTests.cpp`
- [ ] **P14.2** 📦 Git 提交：`[AngelscriptRuntime] Refactor: unify dependency edges across preprocess cache and reload`
- [ ] **P14.2-T** 单元测试：锁住 live / automatic / precompiled 共享 dependency-edge artifact 的合同
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptDependencyEdgeArtifactTests.cpp`
  - 测试场景：
    - 正常路径：显式 `import` 链与 include owner graph 在 live preprocess、compile 后的 automatic-import 回写、以及 fully precompiled restore 中都暴露相同的 canonical edge 列表与稳定顺序。
    - 边界条件：同一依赖图分别走 manual import 与 automatic import 时，edge artifact 的 `From/To`、`EdgeKind` 与 `OrderIndex` 对等；shared include 被多个 owner 使用时，每个 owner 只保留一条规范化 source edge。
    - 错误路径：unresolved import、missing include 或旧 cache 缺失 edge metadata 时，系统必须拒绝提交半成品 edge 并显式 fallback 到 live preprocess，不得继续用 `ImportedModules`、`RelativeFileToModule` 或首 section 文件名拼出一张不完整的图。
  - 测试命名：`Angelscript.TestModule.Preprocessor.DependencyEdges.LiveAndPrecompiledExposeSameCanonicalGraph`、`Angelscript.TestModule.Preprocessor.DependencyEdges.AutomaticImportBackfillsCanonicalEdges`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P14.2-T** 📦 Git 提交：`[AngelscriptTest/Preprocessor] Test: cover unified dependency edge artifacts`

## 补充测试矩阵

| 改进项 | 测试文件 | 测试场景 | 优先级 |
|--------|---------|---------|--------|
| P14.1 | `Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorStatementScannerTests.cpp` | shared statement lexer、tab/space/comment 统一解析、malformed statement fail-before-commit | P1 |
| P14.2 | `Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptDependencyEdgeArtifactTests.cpp` | live/automatic/precompiled 统一 edge schema、shared include owner、stale edge metadata fallback | P0 |

## 补充验收项

38. `#if/#ifdef/#ifndef/#elif`、`#restrict usage`、module `import` 与 future `#include` 必须共享同一份 statement/span authority；tab、space、comment-tail 与 comment-only body 不得再让不同 directive 家族产生不同 parser 结果。
39. live preprocess、automatic-import 回写、include owner invalidation、fully precompiled restore 与 hot reload reverse graph 必须消费同一份 canonical dependency-edge artifact；`ImportedModules` 只能作为兼容投影视图，不能再是唯一 graph authority。

## 补充风险与行为变化

### 风险

33. **共享 statement lexer 会把多类历史宽松写法同时前移成统一的 preprocessor 语法合同**
   - 过去某些脚本可能依赖“tab 分隔 directive 被忽略”“comment-tail 被吞掉”或“extra token 被拖到后段 module lookup 才报错”的偶然行为；scanner 收口后，这些输入会在更早阶段一起暴露。
   - **缓解**：先用 `P14.1-T` 的 golden-style scanner fixture 锁住 whitespace/comment/body-span，再逐步切换 `#if/#restrict/#include/import` 到共享 lexer，避免半迁移状态。

34. **统一 dependency-edge artifact 会触发 cache schema 与历史兼容视图的双向迁移**
   - 一旦 `ImportedModules` 从 graph authority 降级为 projection，旧 precompiled cache、hot reload 索引和 diagnostics fallback 都可能在过渡期读到缺字段或旧字段。
   - **缓解**：先给 `ImportedModules` 保留只读兼容投影，再让 `P14.2-T` 同时覆盖 live、automatic-import、shared include 与 precompiled fallback，缺失 edge metadata 时一律显式判 stale。

### 已知行为变化

35. **malformed `#restrict usage`、module `import` 与 future `#include` 将在同一 parse stage 失败**
   - 影响文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` L3257-L3390、L3490-L3510、L4313-L4356。

36. **依赖图的主 authority 将从分散的 `ImportedModules` / `RelativeFileToModule` / `moduleDependencies` 收口到统一 edge artifact**
   - 影响文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h` L101-L161；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` L2298-L2333、L2400-L2415、L3173-L3195、L4262-L4290、L4566-L4573；`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp` L1417-L1424、L2759-L2779。

---

## 深化 (2026-04-09 06:59:26)

本轮不再扩新主题，只把既有 `P1.2/P1.3/P2.1/P14.1/P14.2` 拆成更低风险的前置结构层。由于 [B] `Documents/AutoPlans/DiscoveryPlans/Preprocessor_Plan.md` 仍不存在，以下深化只基于现存 [A] / [C] / [D] / [E] 与 `Preprocessor/` 源码复核追加。

### Phase 15：把 import / include / module 迁移拆成可独立落地的前置结构层

- [ ] **P15.1** 深化 `P1.2/P14.2`：先把裸 `FImport` 收口为带 raw/canonical/source-range 的 `ImportSite` 结构，再让 resolver 与 cycle 诊断消费它
  - 现有 Plan 已要求 canonical resolver 与统一 `DependencyEdge` artifact，但 `Preprocessor` 当前还停留在“裸字符串 import”模型：`FImport` 只有 `ModuleName`、chunk span 和 `FileLineNumber`；`ParseIntoChunks()` 直接把原始 substring `TrimStartAndEnd()` 后塞进 `ImportDesc.ModuleName`；`ProcessImports()` 再按这个字符串 exact-match 查 provider，并立刻写 `ImportedModules`。这意味着 filename-style 归一、block comment 去除、cycle edge 定位和后续 dependency-edge 建模都还挤在同一段 DFS 里。
  - 这一项先新增过渡层，例如 `FImportSite` / `FImportResolveRequest`：至少保存 `RawImportBody`、`CanonicalCandidate`、`ChunkIndex`、`StatementRange`、`SourceLine` 与 optional `SourceColumn`。`ParseIntoChunks()` 只负责产出 site，`ResolveImportName()` 只消费 site 做 comment stripping、filename-style 归一与重复判定；cycle/unresolved diagnostics 也先回到具体 statement site，而不是继续只打印 module 名。
  - 这是 `P14.2` 之前的低风险落点：先把 import 自身的结构化事实固定下来，后续 `DependencyEdge` artifact、include graph 和 hot reload owner-set 才不会继续建立在“已经被 trim 过的一段文本”上。
  - 来源：
    - [A] `Documents/AutoPlans/Preprocessor_Analysis.md` — “发现 18/22/24/26：filename-style import 不归一、循环 import 诊断缺边信息、provider 查找仍全表扫描、block comment 会污染模块名”
    - [C] `Documents/AutoPlans/TestCoverage/Preprocessor_TestGaps.md` — “NewTest-1/NewTest-3/NewTest-16/NewTest-17 与 Issue-22：缺少循环链、missing semicolon、provider-first 排序与脏 import 记录的精确断言”
    - [D] `Documents/AutoPlans/ArchitectureReview/ModuleStructure_ArchReview.md` — “compile pipeline contract 应保持 deterministic，不应继续依赖模糊字符串与隐式解释”
    - [E] `Documents/AutoPlans/ReferenceComparison/CrossComparison.md` — “`D11` 强调当前插件的优势是 deterministic roots / operator surface；import edge 若继续只保留裸 module string，后续 cache/reload 很难维持同一真相源”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h` L101-L108、L163-L166；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` L447-L450、L467-L483、L3497-L3507 — `FImport` 仅保存裸 `ModuleName` 和简单 span；cycle 诊断只输出 module 名；provider 查找依赖 exact-match string；import body 直接由 substring `TrimStartAndEnd()` 生成。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorImportTests.cpp`
- [ ] **P15.1** 📦 Git 提交：`[AngelscriptRuntime/Preprocessor] Refactor: introduce structured import sites before resolver`
- [ ] **P15.1-T** 单元测试：锁住 `ImportSite` 的 raw/canonical/source-range 合同
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorImportTests.cpp`
  - 测试场景：
    - 正常路径：`import Foo/Bar.as;`、`import Foo.Bar;` 指向同一 provider 时，resolver 产出的 canonical target 一致，但保留各自 raw statement 与源行号。
    - 边界条件：`import Foo.Bar /* note */;`、重复 spelling 变体与 provider-first 逆序 `AddFile()` 同时存在时，只保留一条 canonical 依赖边，且 diagnostics 仍能回到具体 statement site。
    - 错误路径：`A -> B -> A` 循环 import 时，诊断必须指出触发闭环的 statement 行号；缺失分号或 block comment 污染不能再把脏文本写进 canonical target。
  - 测试命名：`Angelscript.TestModule.Preprocessor.Import.ImportSiteKeepsRawAndCanonicalForms`、`Angelscript.TestModule.Preprocessor.Import.CycleDiagnosticsPointToImportSite`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P15.1-T** 📦 Git 提交：`[AngelscriptTest/Preprocessor] Test: cover structured import sites`

- [ ] **P15.2** 深化 `P2.1/P14.1`：先把 `#include` 落成结构化 `DirectiveStatement` + deterministic unsupported-contract，再推进真正的 include graph
  - 现有 `P2.1` 已覆盖“先 fail-fast，再上 include graph”，但源码侧当前连 `#include` statement 的结构化记录都没有：directive 分支只识别 `#if/#ifdef/#ifndef/#elif/#restrict usage`，因此一旦直接尝试落 include expansion，就会把 parser 迁移、unsupported 诊断和 future include graph 混成一次大改。
  - 这一项要先补一个过渡层，例如 `FDirectiveStatement` / `EDirectiveKind::Include`：共享 `P14.1` 的 statement scanner，把合法 `#include "..."` 识别成结构化 statement，并在当前阶段统一产出 deterministic “unsupported include” 诊断；只有 syntax 合同和 statement/span owner 稳定后，后续 `P2.1` 才继续把该结构接到真正的 include graph、owner graph 和 section provenance。
  - 这样可以把“识别 include”与“执行 include”拆开：先消灭静默透传，再避免 future include graph 一上来就同时背负 parser 回归、diagnostics 漂移和 owner graph 改动三类风险。
  - 来源：
    - [A] `Documents/AutoPlans/Preprocessor_Analysis.md` — “发现 28：`#include` 未进入任何预处理、依赖跟踪或模块哈希合同；发现 67：directive lexer 仍依赖字面空格匹配”
    - [C] `Documents/AutoPlans/TestCoverage/Preprocessor_TestGaps.md` — “NewTest-5/NewTest-14：`#include` 合同与 tab 分隔 directive 目前都没有稳定回归”
    - [D] `Documents/AutoPlans/ArchitectureReview/ModuleStructure_ArchReview.md` — “compile pipeline contract 需要单一 parser owner，不能继续让 unsupported 路径依赖偶然透传”
    - [E] `Documents/AutoPlans/ReferenceComparison/CrossComparison.md` — “`D11` 要求 deterministic operator surface；`#include` 若继续处于未建模状态，会直接削弱 module/load contract 的确定性”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` L3259-L3381 — directive 分支仍只处理 conditional/restriction，没有任何 `#include` 分类或结构化记录；L4313-L4325、L4353-L4356 — statement 边界仍分散在 `ReadUntilWhitespace()` 与 `KillRawLine()` 中，unsupported directive 也没有统一 source/span authority。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorIncludeTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorStatementScannerTests.cpp`
- [ ] **P15.2** 📦 Git 提交：`[AngelscriptRuntime/Preprocessor] Refactor: classify include directives before include graph`
- [ ] **P15.2-T** 单元测试：锁住 `#include` 的结构化 unsupported-contract
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorIncludeTests.cpp`
  - 测试场景：
    - 正常路径：合法 `#include "Shared.as"` 会被稳定识别为 `Include` directive，并在当前阶段产出包含源文件与行号的 deterministic unsupported 诊断，而不是静默透传到后续编译。
    - 边界条件：tab/space 变体、尾随 `//` 或 `/* */` 注释版本都映射到同一 `DirectiveKind` 与同一 source span。
    - 错误路径：裸 `#include`、缺失 closing quote、`#include "Shared.as" extra` 必须在 parser stage 报 syntax error，而不是退化成 generic compile error 或 module-not-found 噪音。
  - 测试命名：`Angelscript.TestModule.Preprocessor.Include.UnsupportedDirectiveIsStructuredAndDeterministic`、`Angelscript.TestModule.Preprocessor.Include.MalformedDirectiveFailsInScanner`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P15.2-T** 📦 Git 提交：`[AngelscriptTest/Preprocessor] Test: cover structured include unsupported contract`

- [ ] **P15.3** 深化 `P1.3/P5.1/P5.2`：先在 `AddFile()` 入口引入 typed `ModuleKey` / `ModulePathBuildResult`，再让 wider consumer 逐步迁移
  - 现有 Plan 已要求 canonical module identity、dotted-segment fail-fast 与 reserved-dir ignore-case，但源码入口仍是单个 `FilenameToModuleName()`：直接 `Replace(".as", "")` 再 `Replace("/", ".")`，随后 `AddFile()` 立刻把结果塞进 `Module->ModuleName`。这会让 dotted segment、backslash、大小写、保留目录语义和 duplicate-module fail-fast 继续混在一个 lossy string 里。
  - 这一项先不直接改所有 consumer，而是在 `Preprocessor` 入口新增 typed `ModuleKey` / `FModulePathBuildResult`：至少保存 `NormalizedRelativePath`、`CanonicalModuleName`、`Segments`、`ReservedSegmentFlags` 与 validation error。`AddFile()`、duplicate gate 和后续 resolver 优先消费这个结构；旧 `Module->ModuleName` 暂时只保留为兼容投影视图，等 Core/HotReload/StaticJIT 再逐步迁移。
  - 这是比直接全局替换更低风险的迁移顺序：先把 path-to-module 的真相源固定在 `Preprocessor` 入口，后面无论是 duplicate gate、reserved-dir 语义还是 `SourceKey`/cache/index 迁移，都不再需要围绕同一个模糊字符串反复补丁。
  - 来源：
    - [A] `Documents/AutoPlans/Preprocessor_Analysis.md` — “发现 10/13/69/71：`.as` 全局替换、大小写敏感 import、反斜杠路径与不稳定发现顺序都会让 module identity 漂移”
    - [C] `Documents/AutoPlans/TestCoverage/Preprocessor_TestGaps.md` — “NewTest-13 以及现有 discovery/preprocessor 测试缺口说明，Windows 相对路径与 duplicate-module determinism 还没有被入口层统一锁住”
    - [D] `Documents/AutoPlans/ArchitectureReview/ModuleStructure_ArchReview.md` — “runtime 应保持 deterministic script-root / compile-pipeline contract，模块身份不应继续依赖含糊字符串压缩”
    - [E] `Documents/AutoPlans/ReferenceComparison/CrossComparison.md` — “`D11` 强调 deterministic project/plugin roots；若 module key 仍在入口处丢失路径结构，后续 loader/cache/reload 都只能继续补救”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` L86-L99 — `FilenameToModuleName()` 仍通过全局字符串替换生成 module 名，`AddFile()` 也在没有任何结构化验证或 canonical path carrier 的情况下立刻写入 `Module->ModuleName`。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorPathTests.cpp`
- [ ] **P15.3** 📦 Git 提交：`[AngelscriptRuntime/Preprocessor] Refactor: introduce typed module keys at file ingress`
- [ ] **P15.3-T** 单元测试：锁住 `ModuleKey` 的 canonical path / reserved-segment / fail-fast 合同
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorPathTests.cpp`
  - 测试场景：
    - 正常路径：`Gameplay/UI/Inventory.as` 与 `Gameplay\\UI\\Inventory.as` 归一到同一 `CanonicalModuleName`，并保留稳定的 normalized relative path。
    - 边界条件：`editor/Gameplay/Foo.as`、`Editor/Gameplay/Foo.as` 这类保留目录大小写变体生成相同 reserved-segment 判定，不再让平台大小写规则决定 module environment。
    - 错误路径：dotted segment、空段、`./`、`../` 或 canonical 后冲突到同一 `ModuleKey` 的输入必须在 `AddFile()` / preprocess ingress 直接失败，不得再进入后续 compile list。
  - 测试命名：`Angelscript.TestModule.Preprocessor.Paths.ModuleKeyNormalizesSlashAndCase`、`Angelscript.TestModule.Preprocessor.Paths.IllegalOrCollidingModuleKeysFailFast`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P15.3-T** 📦 Git 提交：`[AngelscriptTest/Preprocessor] Test: cover typed module key ingress`

## 补充测试矩阵

| 改进项 | 测试文件 | 测试场景 | 优先级 |
|--------|---------|---------|--------|
| P15.1 | `Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorImportTests.cpp` | raw/canonical import site、cycle edge 定位、comment/filename-style 归一 | P0 |
| P15.2 | `Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorIncludeTests.cpp` | structured unsupported include、tab/comment 变体、malformed include syntax | P1 |
| P15.3 | `Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorPathTests.cpp` | typed module key、reserved-dir casing、一致的 fail-fast collision | P0 |

## 补充验收项

40. `import` 在进入 resolver 前必须先落成结构化 `ImportSite`；raw statement、canonical target 与 source range 同时可追溯，cycle / unresolved diagnostics 不得再只剩 module 名。
41. `#include` 在真正支持 include graph 之前，至少必须先进入结构化 directive owner，并以 deterministic unsupported-contract 取代静默透传。
42. `FilenameToModuleName()` 的字符串压缩语义必须退位为兼容投影；preprocessor 入口的 typed `ModuleKey` 才是 path-to-module 的唯一真相源。

## 补充风险与行为变化

### 风险

35. **`ImportSite` 过渡期可能与旧 `FImport.ModuleName` 形成双写状态**
   - 如果 resolver、diagnostics 与兼容 `ImportedModules` 投影没有在同一轮迁移，容易出现 raw/canonical 两套字段短期不一致。
   - **缓解**：把 `P15.1` 明确做成“site 为主、旧字段只读投影”的迁移，不允许新逻辑继续直接写裸 `ModuleName`。

36. **先引入 structured unsupported include 会把一批历史脚本从“偶然编译”前移成稳定失败**
   - 过去依赖 `#include` 静默透传的脚本在这一阶段会更早暴露，但这属于合同收紧而不是行为回退。
   - **缓解**：先用 `P15.2-T` 锁住行号、文案和 span，再推进真正 include graph，避免 unsupported 与 expansion 语义混杂。

37. **typed `ModuleKey` 会一次性暴露历史路径编码与保留目录大小写漂移**
   - 某些 fixture 或本地脚本树可能依赖 `Replace(".as").Replace("/", ".")` 的宽松行为；入口收紧后会直接转成 fail-fast。
   - **缓解**：先在 preprocessor path tests 中锁住 slash/case/segment 规则，再让 Core/HotReload/StaticJIT 逐步改读 `ModuleKey`，避免跨层同时爆炸。

### 已知行为变化

37. **`import Foo/Bar.as;`、comment-tailed import 与循环 import 错误将开始保留 raw statement 与 canonical target 两套视图**
   - 影响文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h` L101-L108、L163-L166；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` L447-L450、L467-L483、L3497-L3507。

38. **`#include` 将先进入结构化 unsupported-contract，而不是继续作为未建模文本流入后续编译**
   - 影响文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` L3259-L3381、L4313-L4325、L4353-L4356。

39. **path-to-module 真相源将从 `FilenameToModuleName()` 的 lossy string replace 迁到 typed `ModuleKey`**
   - 影响文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` L86-L99。

---

## 深化 (2026-04-09 07:07:55)

本轮不再扩新的大主题，只补前面 15 个 Phase 之间仍缺少的结构护栏。经复核，`Documents/Plans/Plan_KnownTestFailureFixes.md` 的 `Phase 3` 仍只覆盖 import 记录/剥离与 learning trace 最小修复；以下条目只处理其后的失败状态封口、排序载体收口和 compile availability 合同，不重复最小可用修复范围。

说明：
- 当前工作区仍未找到 [B] `Documents/AutoPlans/DiscoveryPlans/Preprocessor_Plan.md`；以下条目仅基于 [A] / [C] / [D] / [E] 与 `Preprocessor/`、`Core/` 实际源码复核追加。

### Phase 16：补失败状态封口、排序载体与 compile availability 合同

- [ ] **P16.1** 深化 `P4.1/P4.3`：把 `Preprocess()` 的“执行过”与“成功产物可公开消费”拆成两层状态，阻断失败态 `ModuleDesc` 外泄
  - 现在的公开合同仍把“执行过一次预处理”误当成“已经拿到稳定模块产物”：`Preprocess()` 刚开始就置 `bIsPreprocessed = true`，`GetModulesToCompile()` 只看这个布尔值就直接把 `Files[*].Module` 暴露出去；`InitialCompile` 与 learning trace 也都存在“先 `Preprocess()`，后无条件取模块数组”的调用点。这样一旦 late-stage fatal 发生，外部读到的就是半成品 `ModuleDesc`，而不是明确失败。
  - 这一项需要在 `FAngelscriptPreprocessor` 内引入独立结果态，例如 `EPreprocessExecutionState` / `bPreprocessSucceeded`，并把公开读取接口改成“只有成功态才返回 compile-ready snapshot”。如果保留 `GetModulesToCompile()` 名称，失败态至少必须返回空数组并 `ensureMsgf`；需要调试失败现场的 learning/test helper 应改读显式 diagnostic snapshot 或直接检查 `Files`，而不是再把失败工作区伪装成 compile input。
  - 同时要把 `OnProcessChunks` / `OnPostProcessCode` 与最终 `Module->Code` 写入纳入同一 success gate：late-stage fatal 不能再在公开 API 层表现成“返回 `false`，但模块数组和代码段已经能被外部拿走”。这样后续 `ImportSite`、`DirectiveStatement`、`DependencyEdge` 迁移才有稳定失败边界。
  - 来源：
    - [A] `Documents/AutoPlans/Preprocessor_Analysis.md` — “发现 33/34：中后段 `bHasError` 后仍继续广播 hook、写 `Module->Code`；`GetModulesToCompile()` 把‘已执行’与‘已成功’混为一谈”
    - [C] `Documents/AutoPlans/TestCoverage/Preprocessor_TestGaps.md` — “NewTest-6/NewTest-34：失败后状态污染与结构性 directive 错误后的模块可见性缺少自动化保护”
    - [E] `Documents/AutoPlans/ReferenceComparison/CrossComparison.md` — “[D11] 结论强调当前 Angelscript 的强项是 deterministic、fail-fast 的 artifact/self-verification contract，不应让半成品结果继续通过公开接口外泄”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` L75-L83、L220-L223、L265-L305；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` L2081-L2082；`Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningPreprocessorTraceTests.cpp` L145-L150 — `GetModulesToCompile()` 仅检查 `bIsPreprocessed`；`Preprocess()` 一开始就置位；late-stage 仍广播 hook 并写 `Module->Code`；`InitialCompile` 与 learning trace 都会在失败后继续读取模块数组。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorFailureStateTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningPreprocessorTraceTests.cpp`
- [ ] **P16.1** 📦 Git 提交：`[AngelscriptRuntime/Preprocessor] Fix: expose compile modules only after successful preprocess`
- [ ] **P16.1-T** 单元测试：锁住失败态 `Preprocess()` 不再公开 compile-ready 模块
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorFailureStateTests.cpp`
  - 测试场景：
    - 正常路径：最小合法脚本 `Preprocess()` 成功后，`GetModulesToCompile()` 返回 1 个 module，且 `Code[0].Code` 非空。
    - 边界条件：孤立 `#else` / `#endif` 或缺失闭合 `#endif` 的脚本 `Preprocess()` 返回 `false`，`GetModulesToCompile()` 返回空结果，不再暴露 compile-ready `Code`。
    - 错误路径：late-stage macro/default fatal 后，不再广播 `OnPostProcessCode` 给外部成功消费者，也不再通过公开结果接口吐出半成品 `Module->Code`。
  - 测试命名：`Angelscript.TestModule.Preprocessor.Failures.GetModulesRequiresSuccessfulPreprocess`、`Angelscript.TestModule.Preprocessor.Failures.LateFatalErrorDoesNotPublishCodeArtifacts`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P16.1-T** 📦 Git 提交：`[AngelscriptTest/Preprocessor] Test: cover preprocess success gating and failure-state sealing`

- [ ] **P16.2** 深化 `P1.2/P15.1`：把 manual `import` 排序从 `TArray<FFile>` 值复制改成独立的稳定 order/index artifact
  - 当前 `ProcessImports()` 仍把整份 `FFile` 当排序结果容器：递归解析完成后直接 `OutSortedFiles.Add(File)`，上层再做 `Files = SortedFiles`。但 `FFile` 已经承载 `RawCode`、`TChunkedArray<FChunk>`、`ProcessedCode`、`GeneratedCode`、`Imports`、`Delegates` 等完整预处理工作区，这让“依赖图顺序”继续和“大块可变 payload”绑在一起。
  - 这一项需要把排序输出改成独立载体，例如 `TArray<int32>`、`TArray<FFile*>` 或显式 `FResolvedImportOrder` / `FModuleOrderArtifact`：`ProcessImports()` 只负责产出 provider-first 的稳定 order、cycle 状态和 canonical dependency 结果，不再复制 `FFile`。真正需要 provider-first compile list 的地方再从该 order 组装模块数组；`Files` 存储本身保持稳定，供 hook、learning trace、white-box 检查和后续 `ImportSite`/`ModuleKey` 迁移复用。
  - 这样可以同时封住两条旧问题：一是显式 `import` 排序不再复制整块 payload，二是失败恢复不再依赖“复制前/复制后哪份 `FFile` 算真相源”。它也是后续 `DependencyEdge` 与 root-aware `ModuleKey` 真正落地前的必要前置层。
  - 来源：
    - [A] `Documents/AutoPlans/Preprocessor_Analysis.md` — “发现 5/44：manual import 排序复制整份 `FFile`，预处理阶段又同时保留多份源码拷贝，成本和回滚复杂度都会被放大”
    - [C] `Documents/AutoPlans/TestCoverage/Preprocessor_TestGaps.md` — “NewTest-7/Issue-22：长源码 determinism 与精确依赖列表目前都没有被强断言锁住”
    - [D] `Documents/AutoPlans/ArchitectureReview/ModuleStructure_ArchReview.md` — “稳定 owner / stable ordering 的建议强调，不应让顺序与 identity 继续依附在一次性可变载体上；可变性应下沉到更轻量的 manifest/index，而不是 payload 本身”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h` L120-L160；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` L232-L238、L439-L497 — `Files` 是 `TArray<FFile>`，`FFile` 负担完整工作区；manual import 路径仍通过 `TArray<FFile> SortedFiles`、`OutSortedFiles.Add(File)` 与 `Files = SortedFiles` 来表达排序结果。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorImportOrderTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorStressTests.cpp`
- [ ] **P16.2** 📦 Git 提交：`[AngelscriptRuntime/Preprocessor] Refactor: separate import order from FFile payload storage`
- [ ] **P16.2-T** 单元测试：锁住 manual `import` 顺序来自稳定 index artifact，而不是 `FFile` 值复制
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorImportOrderTests.cpp`
  - 测试场景：
    - 正常路径：在逆序 `AddFile()` 的 `Base <- Shared <- Consumer` 样本中，`GetModulesToCompile()` 仍输出稳定 provider-first 顺序，且依赖列表精确无重复。
    - 边界条件：程序化生成长脚本与多模块 import 链，连续两次 `Preprocess()` 得到完全一致的 module order、exact `ImportedModules` 和 processed code hash。
    - 错误路径：循环 import 失败时，不得因为排序过程复制 payload 而重排 `Preprocessor.Files` 的白盒可观察顺序，也不得留下部分 blanked/import-resolved 的半成品存储副本。
  - 测试命名：`Angelscript.TestModule.Preprocessor.ImportOrder.ProviderFirstOrderUsesStableIndexGraph`、`Angelscript.TestModule.Preprocessor.Stress.LongSourceAndManualImportRemainDeterministic`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P16.2-T** 📦 Git 提交：`[AngelscriptTest/Preprocessor] Test: cover stable import order artifacts`

- [ ] **P16.3** 深化 `P3.2/P6.3`：给 compile batch 引入明确的 dependency availability gate，并让 `#restrict usage` 只在 eligible modules 上继续校验
  - 现在的 compile 侧仍把“名字出现在当前 batch”误当成“这个 provider 已经可导入”：`CompileModules()` 先把当前批次模块都塞进 `CompilingModulesByName`，consumer 解析 `ImportedModules` 时只要能按名字找到上游 module desc，就会把它加入 `ImportedModules` 列表；即便该 provider 随后因为缺失 import 或 compile error 没有拿到 `ScriptModule`，下游模块仍可能继续进入后续 parse/generate。随后 `CheckUsageRestrictions()` 又会在遇到第一个 `ScriptModule == nullptr` 或 `Code.Num() == 0` 的模块时直接 `return`，把同批次其它模块的 restriction 校验一起熔断。
  - 这一项要在 compile batch 内引入显式可用性状态，例如 `Resolved` / `Unavailable` / `CompileFailed`：只有成功到达可导入阶段的 provider 才能参与 module import 解析、`CombinedDependencyHash` 计算和 downstream compile；不可用 provider 必须把 consumer 直接标成 dependency-unavailable 并提前停止，而不是继续放进 Stage1。`CheckUsageRestrictions()` 也要从“遇坏即 `return`”改成“只在 eligible modules 上迭代，坏模块 `continue` 跳过”。
  - 这一步会把 explicit import、失败隔离与 restriction contract 真正接起来：根因模块先失败，consumer 收到单一的 availability 诊断，同批次其他合法模块仍继续完成 restriction 检查，不再被一个 unrelated failure 熔断。
  - 来源：
    - [A] `Documents/AutoPlans/Preprocessor_Analysis.md` — “发现 42/43：缺失 import 的 provider 仍可被下游当占位导入，单个失败模块又会让整轮 `CheckUsageRestrictions()` 直接短路”
    - [C] `Documents/AutoPlans/TestCoverage/Preprocessor_TestGaps.md` — “NewTest-6/NewTest-15：失败路径污染后续正常编译与 restriction 元数据合同都缺少直接回归保护”
    - [E] `Documents/AutoPlans/ReferenceComparison/CrossComparison.md` — “[D11] self-verification gate 结论要求当前 Angelscript 在 artifact 不可用时 fail-fast，而不是对不可用 provider 继续做软占位传播”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` L3175-L3205、L4523-L4566、L4673-L4685 — module import 解析按名字从 `CompilingModulesByName` 取 provider，遇不到才报错；`CheckUsageRestrictions()` 在遇到坏模块时直接 `return`；declared function import 路径已经显式区分 “module 不存在 / `bCompileError`”，说明 compile availability gate 在其它依赖子系统里已经有局部先例。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Compiler/AngelscriptCompilerImportFailureTests.cpp`
- [ ] **P16.3** 📦 Git 提交：`[AngelscriptRuntime/Core] Fix: gate downstream imports and restrictions on provider availability`
- [ ] **P16.3-T** 单元测试：锁住 unavailable provider 不再污染 downstream compile 与 restriction 校验
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Compiler/AngelscriptCompilerImportFailureTests.cpp`
  - 测试场景：
    - 正常路径：`Base <- Mid <- Consumer` 的合法 import 链仍能成功编译并执行，证明 availability gate 不影响健康依赖。
    - 边界条件：上游 provider 因缺失 import 或语法错误失败时，下游 consumer 只收到 dependency-unavailable 级根因诊断，不再继续进入 Stage1/生成二次噪音。
    - 错误路径：同一批次里另一个带 `#restrict usage` 的合法模块仍会完成 restriction 校验；坏模块不再让 `CheckUsageRestrictions()` 整轮 `return`。
  - 测试命名：`Angelscript.TestModule.Compiler.Imports.UnavailableProviderStopsDownstreamCompilation`、`Angelscript.TestModule.Compiler.Restrictions.FailedModuleDoesNotShortCircuitValidation`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P16.3-T** 📦 Git 提交：`[AngelscriptTest/Compiler] Test: cover provider availability gates and eligible restriction validation`

## 补充测试矩阵

| 改进项 | 测试文件 | 测试场景 | 优先级 |
|--------|---------|---------|--------|
| P16.1 | `Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorFailureStateTests.cpp` | failed preprocess 不再公开 compile-ready modules、late fatal 不再发布半成品 code artifact | P0 |
| P16.2 | `Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorImportOrderTests.cpp` | stable import order artifact、长源码 determinism、循环失败不重写 payload storage | P1 |
| P16.3 | `Plugins/Angelscript/Source/AngelscriptTest/Compiler/AngelscriptCompilerImportFailureTests.cpp` | unavailable provider gate、downstream fail-fast、eligible restriction validation | P0 |

## 补充验收项

43. 公开 `Preprocessor` 结果访问器必须区分 `Started` 与 `Succeeded`；失败态不得再把半成品 `ModuleDesc` / `Code` 当 compile input 暴露给外部。
44. manual `import` 的 provider-first 顺序必须由独立 order/index artifact 表达，而不是通过复制整份 `FFile` payload 得到；连续两次对同一输入预处理，顺序与依赖清单必须完全一致。
45. compile batch 中只有真正可用的 provider 才能满足 downstream imports；单个失败模块不得再熔断同批次其他 eligible modules 的 `#restrict usage` 校验。

## 补充风险与行为变化

### 风险

38. **把失败态从 `GetModulesToCompile()` 中收走后，现有 learning/test 辅助代码里依赖半成品模块做 trace 的路径会立刻显形**
   - 某些白盒测试或调试脚本可能默认把“失败后的 `Files[*].Module`”当成可消费结果；收口后这些路径需要改读显式 diagnostic / internal snapshot。
   - **缓解**：保留受控的调试读取面，但 compile-ready 公开接口必须只返回 success snapshot；`Learning` 测试同步改成先看 `Preprocess()` 返回值。

39. **manual import 顺序从 `Files` 物理重排迁到独立 order artifact 后，旧白盒断言可能把“存储顺序”误当成“编译顺序”**
   - 一些 trace 或测试若直接遍历 `Preprocessor.Files` 推断最终 compile order，会在结构收口后出现假失败。
   - **缓解**：新增 `P16.2-T` 显式区分 storage order 与 compile order，只允许后者驱动 import/topology 断言。

40. **provider availability gate 会把一批历史“二次噪音错误”前移成单一根因诊断**
   - 这会改变部分 compile-error 数量与顺序，看起来像诊断减少，但本质上是更早阻断错误传播。
   - **缓解**：测试里固定“root-cause diagnostic + downstream skip”语义，不再依赖旧的二次噪音数量。

### 已知行为变化

40. **失败态 `Preprocess()` 将不再允许 `GetModulesToCompile()` 返回半成品模块数组**
   - 影响文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` L75-L83、L220-L223、L265-L305；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` L2081-L2082。

41. **manual `import` 的最终 compile order 将改由独立 order/index artifact 驱动，而不是由 `Files = SortedFiles` 的 payload 重排隐式表达**
   - 影响文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h` L120-L160；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` L232-L238、L439-L497。

42. **compile batch 中不可用的 provider 将被视为 dependency-unavailable，而不再作为占位 module 继续参与 downstream import 与 restriction 流程**
   - 影响文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` L3175-L3205、L4523-L4566、L4673-L4685。

---

## 深化 (2026-04-09 07:17:37)

本轮只补 source-ingest 入口的两处 correctness contract。经复核，`Documents/Plans/Plan_ScriptFileSystemRefactor.md` 已覆盖文件系统抽象与依赖注入，但尚未覆盖这里的“删除参数错位”与“异步读盘未等待完成”两条问题；另外，`Documents/AutoPlans/DiscoveryPlans/Preprocessor_Plan.md` 本轮仍未在工作区中定位到，因此以下新增条目只引用 [A] / [C] / [E] 与实际源码交叉确认。

### Phase 17：补 source-ingest 入口与完成屏障合同

- [ ] **P17.1** 深化 `P3.2`：把 hot reload 删除入口从双 bool 位置参数改成显式 load request，彻底消除 `bTreatAsDeleted` 误入 async 分支
  - `PerformHotReload()` 现在先根据 `AlreadyDeletedFiles` 计算 `bTreatAsDeleted`，随后直接调用 `Preprocessor.AddFile(PathPair.RelativePath, PathPair.AbsolutePath, bTreatAsDeleted)`。但 `AddFile()` 第三个参数的真实语义是 `bLoadAsynchronous`，只有第四个参数才是 `bTreatAsDeleted`。结果是删除文件事件会被错误地当成“异步读盘请求”，而不是 delete marker；真正的删除路径 `RawCode = ""` 根本没有被走到。
  - 这一项不应只做一处 callsite 修补，而要顺手收掉 `AddFile()` 的双 bool 位置参数。建议引入 `EScriptSourceLoadMode` 或 `FScriptSourceLoadRequest` 之类的显式请求体，把 `Sync`、`Async`、`Deleted` 三种模式从 API 形状上区分开；`PerformHotReload()`、初次编译和任何测试 helper 全部统一走同一个构造入口，避免未来再次把“模式”错传成“位序”。
  - 这与 `Plan_ScriptFileSystemRefactor.md` 的边界明确不同：那一份计划处理“通过什么接口读盘”，这里处理“进入 preprocessor 时到底声明成哪一种源码状态”。即使后续读盘后端可注入，只要入口还保留双 bool，delete contract 仍然是脆弱的。
  - 来源：
    - [A] `Documents/AutoPlans/Preprocessor_Analysis.md` — “发现 3：hot reload 删除文件路径把 `bTreatAsDeleted` 错传成了 `bLoadAsynchronous`”
    - [C] `Documents/AutoPlans/TestCoverage/Preprocessor_TestGaps.md` — “NewTest-27：`bTreatAsDeleted` 必须把已删除脚本稳定转换为空模块描述”
    - [E] `Documents/AutoPlans/ReferenceComparison/CrossComparison.md` — “`D4` 强调当前 Angelscript 的优势是 delete-delay、失败记忆与 keep-old-code 的显式事务 contract；删除事件若先被降级成‘试着再读一次文件’，这条 contract 就会被入口层直接削弱”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` L2449-L2452 — `bTreatAsDeleted` 计算后直接作为 `AddFile()` 第三个实参传入；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h` L15、`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` L91-L113 — 第三个参数定义为 `bLoadAsynchronous`，只有第四个参数才会触发 deleted 分支并把 `RawCode` 置空。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptDeletedFileHotReloadTests.cpp`
- [ ] **P17.1** 📦 Git 提交：`[AngelscriptRuntime/Preprocessor] Fix: replace positional AddFile flags with explicit source load mode`
- [ ] **P17.1-T** 单元测试：锁住 hot reload 删除事件走显式 deleted load mode，而不是误入 async 读盘
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptDeletedFileHotReloadTests.cpp`
  - 测试场景：
    - 正常路径：已有模块的源文件被删除后，hot reload 通过 deleted load request 进入预处理，删除文件不会再尝试重新读取已不存在的磁盘路径。
    - 边界条件：同一批次同时存在 deleted file 与 changed file 时，delete marker 只作用于目标文件，不污染其它文件的 load mode。
    - 错误路径：删除文件后即使磁盘路径已经不存在，也不得因为误走 async 读盘而崩溃、挂起或把旧源码重新混入新一轮预处理；keep-old-code / remove-module 的事务语义保持与既有 contract 一致。
  - 测试命名：`Angelscript.TestModule.HotReload.DeletedFile.UsesDeletedLoadMode`、`Angelscript.TestModule.HotReload.DeletedFile.MixedBatchKeepsModeIsolation`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P17.1-T** 📦 Git 提交：`[AngelscriptTest/HotReload] Test: cover deleted file load mode routing`

- [ ] **P17.2** 深化 `P16.1`：为 `PerformAsynchronousLoads()` 增加完成屏障和显式 load status，禁止 `ParseIntoChunks()` 读取未完成 `RawCode`
  - 当前异步分支只是为每个文件发起 `SizeRequest/ReadRequest`，然后在第二个 `for` 循环里对仍处于 `bLoadAsynchronous == true` 的文件睡眠 `0.001f` 一次就返回。`Preprocess()` 随后立刻进入 `ParseIntoChunks(File)`。只要回调还没完成，预处理线程和异步回调就会并发读写同一个 `FFile.RawCode`、`bLoadAsynchronous` 与请求句柄，最终产物取决于调度时机，而不是源码本身。
  - 这一项要把异步读盘从“fire-and-sleep-once”改成“启动所有请求 -> 等待全部进入 `Ready/Failed/Deleted` 终态 -> 统一清理请求句柄 -> 才允许 parse”。实现上至少需要显式 `EFileLoadStatus` 或等价状态结构，禁止继续拿 `volatile bool bLoadAsynchronous` 同时兼任“仍在加载”和“最终是否成功”两种语义。
  - 这同样不重复 `Plan_ScriptFileSystemRefactor.md`：那一份解决 backend 可替换，这一项解决“即使仍使用当前 backend，进入 chunk parser 前也必须拿到完整、稳定、一次性的源码快照”。否则 module desc、diagnostics 与 import/macro 结果会继续带着 nondeterministic 输入进入后续 Phase。
  - 来源：
    - [A] `Documents/AutoPlans/Preprocessor_Analysis.md` — “发现 4：异步脚本读取没有真正等待完成，`ParseIntoChunks()` 会与回调并发访问同一 `FFile`”
    - [C] `Documents/AutoPlans/TestCoverage/Preprocessor_TestGaps.md` — “NewTest-19：异步 `AddFile` 读取路径必须与同步路径产出完全一致”
    - [E] `Documents/AutoPlans/ReferenceComparison/CrossComparison.md` — “`D11/D4` 对当前 Angelscript 的判断强调 deterministic artifact 与显式 reload contract；若 async 输入阶段仍由调度时机决定，module artifact 就会退回 loader-style nondeterminism”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` L141-L209 — `PerformAsynchronousLoads()` 当前只发请求并单次 `Sleep(0.001f)`；L224-L230 — `Preprocess()` 调完 `PerformAsynchronousLoads()` 后立即 `ParseIntoChunks(File)`；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h` L156-L160 — 只有 `volatile bool bLoadAsynchronous` 和裸请求指针，没有独立 completion/result 状态。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorAsyncLoadTests.cpp`
- [ ] **P17.2** 📦 Git 提交：`[AngelscriptRuntime/Preprocessor] Fix: await async source loads before parsing`
- [ ] **P17.2-T** 单元测试：锁住 async/sync 预处理产物一致性与完成屏障
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorAsyncLoadTests.cpp`
  - 测试场景：
    - 正常路径：同一组 provider/importer fixture 分别走 sync `AddFile(..., false)` 与 async `AddFile(..., true)`，两条路径得到完全一致的 module 顺序、`ImportedModules`、`ProcessedCode`、宏记录与 diagnostics。
    - 边界条件：脚本同时包含 manual import、宏和多 chunk 内容时，async 路径仍不会遗漏 import blanking、macro 记录或 provider-first order。
    - 错误路径：async 读盘失败或被取消时，preprocessor 以明确 `Failed/Deleted` 状态结束，不得在 `RawCode` 未完成填充时开始 `ParseIntoChunks()`，也不得留下悬空请求句柄。
  - 测试命名：`Angelscript.TestModule.Preprocessor.AsyncLoad.SyncAndAsyncProduceSameArtifacts`、`Angelscript.TestModule.Preprocessor.AsyncLoad.FailedReadDoesNotParsePartialSource`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P17.2-T** 📦 Git 提交：`[AngelscriptTest/Preprocessor] Test: cover async load completion barriers`

## 补充测试矩阵

| 改进项 | 测试文件 | 测试场景 | 优先级 |
|--------|---------|---------|--------|
| P17.1 | `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptDeletedFileHotReloadTests.cpp` | deleted load mode、mixed batch mode isolation、删除文件不再误入 async 读盘 | P0 |
| P17.2 | `Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorAsyncLoadTests.cpp` | sync/async parity、async 失败不解析半截源码、请求句柄清理 | P0 |

## 补充验收项

46. hot reload 删除事件必须以显式 `Deleted` 源码状态进入 preprocessor；删除文件不得再通过 `AddFile()` 的参数位序误入 async 读盘。
47. `ParseIntoChunks()` 只能消费已经完成加载的源码快照；async 输入路径与 sync 输入路径必须产出完全一致的 module 顺序、依赖列表与 processed code。

## 补充风险与行为变化

### 风险

41. **把 `AddFile()` 从双 bool 改成显式 load request 会触及所有入口调用点**
   - 初次编译、hot reload 与测试 helper 都要一起迁移；若只改其中一部分，新的 mode contract 反而会出现双轨。
   - **缓解**：先在编译期移除旧重载或把旧重载降级成仅内部转发，并用 `P17.1-T` 覆盖 delete batch、普通增量 reload 与直接 preprocessor 调用三条入口。

42. **为 async 路径补完成屏障后，历史上被时序侥幸掩盖的问题会一次性显形**
   - 某些测试或本地工作流可能以前偶然依赖“回调还没完成也继续 parse”的非确定行为；补屏障后，这些路径会更早暴露真实失败。
   - **缓解**：让 `P17.2-T` 固定 sync/async parity 与 failed-read 终态，先把 nondeterminism 收敛成稳定失败，再处理具体上层症状。

### 已知行为变化

43. **删除脚本文件的 hot reload 将不再尝试把删除标记误当成 async 读盘请求**
   - 影响文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` L2449-L2452；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h` L15；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` L91-L113。

44. **async `AddFile()` 将在进入 `ParseIntoChunks()` 前先等待所有源码加载完成，并以显式终态区分 `Ready/Failed/Deleted`**
   - 影响文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` L141-L230；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h` L156-L173。

---

## 深化 (2026-04-09 07:25:48)

本轮不再扩新主题，只补 `P4.2/P7.1/P8.*` 落地后仍然悬空的两个跨层 contract seam：一是 module lookup 仍靠“filename 失败就 module-name fallback”的模糊接口兜底，二是 precompiled cache-hit 之后的 module signature / `CreateFunctionId()` 仍与 live compile 路径不一致。已再次确认 `Documents/AutoPlans/DiscoveryPlans/` 目录当前为空，因此以下条目仅引用现存 [A]/[D]/[E] 与实际源码复核结果。

### Phase 18：补 module lookup authority 与 precompiled module signature 尾处理

- [ ] **P18.1** 深化 `P4.2/P10.4`：把 `GetModuleByFilenameOrModuleName()` 从模糊 fallback 改成 typed lookup request/result，停止用 module-name 兜底掩盖 provenance 缺口
  - 当前 lookup contract 仍然把“按文件定位”和“按 module key 定位”压进同一个双参数 API：`GetModuleByFilenameOrModuleName()` 先尝试 `GetModuleByFilename()`，失败后无条件回落到 `GetModuleByModuleName()`。这在 live path 上只是模糊，在 fully precompiled / plugin-root path 上则会直接掩盖 contract 缺口，因为 `GetModulesToCompile()` 恢复出的 descriptor 仍没有 section/root provenance，filename lookup 失败后就会被 module-name fallback 静默吞掉。
  - 这一项要新增显式 `FAngelscriptModuleLookupRequest` / `FAngelscriptModuleLookupResult` 或等价结构，至少区分 `LookupIntent`、`RequestedFilename`、`RequestedModuleKey`、`ExpectedSourceKey`、`MatchKind` 与 `FailureReason`。`GetModuleByFilename()` / `GetModuleByModuleName()` 继续保留窄语义 wrapper，`GetModuleByFilenameOrModuleName()` 降级成仅供过渡期使用的 compatibility shim；`DebugServer`、breakpoint bookkeeping、file-system helper 与相关测试改为消费 typed result，不再把“filename 找不到但 module name 碰巧存在”视为正常命中。
  - 这样可以把 `P4.2` 的 root-aware `ModuleIdentity` 真正推到 lookup owner，而不再停留在数据结构层。后续若 cache/provenance 缺字段，系统应明确返回 `MissingSourceProvenance` / `StalePrecompiledLookup` / `FilenameModuleMismatch`，而不是继续让 fallback 把错误隐藏成“看起来还能找到模块”。
  - 来源：
    - [A] `Documents/AutoPlans/Preprocessor_Analysis.md` — “发现 41/49：fully precompiled / plugin-root 场景会失去按文件定位模块的能力；发现 69：preprocessor path normalization 与 runtime filename lookup 仍是两套语义”
    - [D] `Documents/AutoPlans/ArchitectureReview/ModuleStructure_ArchReview.md` — “runtime 侧应保持 deterministic script-root / compile-pipeline contract，不应继续让 loader-style 宽松解释主导核心模块系统”
    - [E] `Documents/AutoPlans/ReferenceComparison/CrossComparison.md` — “D11：当前插件的优势是 deterministic project/plugin roots 与稳定 operator surface，不应回退到自由 loader 式宽松 fallback”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` L3018-L3055 — `GetModuleByFilenameOrModuleName()` 仍先 filename lookup 再无条件回落到 module-name lookup；`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp` L2758-L2779 — precompiled 恢复仍只回填 `ModuleName/CodeHash/ImportedModules`，没有任何 lookup provenance；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp` L933-L968 — breakpoint 路径直接依赖该双参数 API，找不到模块时还会继续以 raw filename 建账本。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerBreakpointTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/FileSystem/AngelscriptFileSystemTests.cpp`
- [ ] **P18.1** 📦 Git 提交：`[AngelscriptRuntime/Core] Refactor: replace filename-or-module fallback with typed module lookup requests`
- [ ] **P18.1-T** 单元测试：锁住 breakpoint/file-system consumer 的 typed module lookup contract
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerBreakpointTests.cpp`
  - 测试场景：
    - 正常路径：live compile 的脚本同时按 canonical filename 与 canonical module key 查找时，typed lookup 返回同一 `ModuleIdentity`，且 `MatchKind` 明确为 filename/provenance 命中而不是 fallback。
    - 边界条件：plugin-root 脚本在 fully precompiled 恢复后仍能按 source-aware lookup 被断点路径正确命中；如果只能走 compatibility shim，也必须显式回报 fallback reason，而不是静默退化。
    - 错误路径：传入互相矛盾的 `Filename + ModuleName`、root 外 alias 路径或缺少 provenance 的旧 cache 时，lookup 必须返回明确 `FailureReason` 并拒绝把 breakpoint bookkeeping 绑定到错误模块，不得再靠 module-name fallback 伪装成功。
  - 测试命名：`Angelscript.TestModule.Debugger.Lookup.FilenameRequestPrefersSourceIdentity`、`Angelscript.TestModule.Debugger.Lookup.PrecompiledPluginScriptKeepsSourceAwareResolution`、`Angelscript.TestModule.Debugger.Lookup.MismatchedFilenameAndModuleDoNotSilentlyFallback`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P18.1-T** 📦 Git 提交：`[AngelscriptTest/Debugger] Test: cover typed module lookup requests and mismatch failures`

- [ ] **P18.2** 深化 `P7.1/P8.2`：把 module signature 回填与 `CreateFunctionId()` 改成 live/cache-hit 共用的 full-width contract
  - 当前 `CompileModule_Types_Stage1()` 虽然在进入 stage1 前就算出了 `Module->CombinedDependencyHash`，但一旦命中 precompiled cache，就在 `ApplyToModule_Stage1()` 后直接 `return`，完全跳过后面的 `ScriptModule->SetUserData((void*)(size_t)Module->CombinedDependencyHash, 0)`。随后 `CreateFunctionId()` 又只把 `ScriptModule->GetUserData()` 强转成 `uint32` 参与 `HashCombine()`。结果是 cache-hit 路径会丢掉 module signature，而 live path 即便写回了 signature，也只消费低 32 位。
  - 这一项要把 module signature 落成显式尾处理 contract：新增 `ApplyModuleSignatureToScriptModule()` / `GetStableModuleSignatureBits()` / `HashModuleSignatureForFunctionId(uint64)` 或等价 helper，让 live compile 与 precompiled hit 都经过同一段 finalization，再让 `CreateFunctionId()` 同时混入高低 32 位；若仍需兼容非 64-bit user-data 通道，则补 sidecar signature map，由 `CreateFunctionId()` 统一读 full-width 值，`SetUserData()` 仅保留 legacy mirror。
  - 这一步不是另起 JIT 主题，而是把 `P7.1` 已经要求的 dependency signature 真正接到 `StaticJIT` / precompiled function identity。只要 cache-hit path 和 live path 继续看到两套 module signature，前面的 dependency graph、ordered fingerprint 与 stale-cache 判定就无法闭环到最终可执行产物。
  - 来源：
    - [A] `Documents/AutoPlans/Preprocessor_Analysis.md` — “发现 61/62：precompiled cache-hit 不会把 `CombinedDependencyHash` 写回 `ScriptModule`，`CreateFunctionId()` 又只消费低 32 位 signature”
    - [D] `Documents/AutoPlans/ArchitectureReview/ModuleStructure_ArchReview.md` — “runtime 侧应保持 deterministic compile-pipeline contract，不能让不同入口生成不同模块身份语义”
    - [E] `Documents/AutoPlans/ReferenceComparison/CrossComparison.md` — “D11：当前插件的价值之一是 build/cache self-verification gate；cache-hit path 不应再生成弱于 live path 的 module signature”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` L4262-L4300、L4348-L4350 — cache-hit 早退发生在 `SetUserData()` 之前，只有 non-precompiled 路径会把 `CombinedDependencyHash` 写入 `ScriptModule`；`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp` L2692-L2725 — `CreateFunctionId()` 仍只把 `ScriptModule->GetUserData()` 的低 32 位混进 ID。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/StaticJIT/AngelscriptFunctionIdSignatureTests.cpp`
- [ ] **P18.2** 📦 Git 提交：`[AngelscriptRuntime/StaticJIT] Fix: unify module signature finalization and hash full-width signatures into function ids`
- [ ] **P18.2-T** 单元测试：锁住 live/cache-hit 一致的 module signature 与 full-width function-id seed
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/StaticJIT/AngelscriptFunctionIdSignatureTests.cpp`
  - 测试场景：
    - 正常路径：同一 dependency graph 分别走 live compile 与 precompiled cache-hit 后，同名函数得到完全一致的 stable function ID，且 `ScriptModule` 上可观测到非零 module signature。
    - 边界条件：通过新增的 pure helper 或等价 seam 构造两个仅高 32 位不同的 `uint64` module signature，二者生成的 function-id seed 必须不同，证明高位熵不再被静默丢弃。
    - 错误路径：只改变 provider dependency signature 而不改 consumer 源码时，cache-hit 路径不得继续复用旧 function ID；若 module signature 缺失或为零，测试必须直接报错或触发 stale/fallback，而不是继续生成弱签名 ID。
  - 测试命名：`Angelscript.TestModule.StaticJIT.FunctionId.PrecompiledHitKeepsDependencySignature`、`Angelscript.TestModule.StaticJIT.FunctionId.HighBitsAffectStableIdSeed`、`Angelscript.TestModule.StaticJIT.FunctionId.ProviderChangeInvalidatesStableId`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P18.2-T** 📦 Git 提交：`[AngelscriptTest/StaticJIT] Test: cover cache-hit module signatures and full-width function-id seeds`

## 补充测试矩阵

| 改进项 | 测试文件 | 测试场景 | 优先级 |
|--------|---------|---------|--------|
| P18.1 | `Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerBreakpointTests.cpp` | typed lookup request、plugin-root precompiled breakpoint lookup、filename/module mismatch failure | P1 |
| P18.2 | `Plugins/Angelscript/Source/AngelscriptTest/StaticJIT/AngelscriptFunctionIdSignatureTests.cpp` | live/precompiled module signature parity、high-32-bit seed coverage、provider-change invalidation | P0 |

## 补充验收项

48. module lookup consumer 必须显式区分 `MatchedByFilename`、`MatchedByModuleKey` 与 `FailureReason`；缺少 provenance、root 不匹配或 filename/module-name 冲突时，不得再通过 `GetModuleByFilenameOrModuleName()` 静默兜底成功。
49. live compile 与 precompiled cache-hit 必须发布同一份 full-width module signature；`CreateFunctionId()` 不得再因为 cache-hit 早退或只取低 32 位而生成漂移的 stable ID。

## 补充风险与行为变化

### 风险

43. **typed lookup request 会立即暴露历史上依赖 module-name fallback 的调用点**
   - Debugger、file-system helper、旧测试甚至外部工具脚本，可能都默认“文件路径错了也能靠 module name 继续工作”；改成显式 `FailureReason` 后，这些路径会从假成功变成稳定失败。
   - **缓解**：先保留 compatibility shim 与 fallback telemetry，再按 `P18.1-T` 把 breakpoint/file-system 主消费方切到 typed result；仅在主路径稳定后再收紧旧 API。

44. **full-width module signature 会触发 StaticJIT / precompiled 产物的一次性身份重算**
   - 历史缓存、golden-style ID 断言和任何依赖旧 `CreateFunctionId()` 种子的辅助脚本，都会在这轮之后看到新的 ID 分布。
   - **缓解**：把 `P18.2` 与显式 schema/version bump 或 rejection telemetry 绑定；测试里优先锁“同图 live/cache-hit 一致”和“provider 变更必失效”，不要继续锁旧数值常量。

### 已知行为变化

45. **`GetModuleByFilenameOrModuleName()` 将不再默认把 filename failure 静默降级成 module-name 命中**
   - 影响文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` L3018-L3055；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp` L933-L968；`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp` L2758-L2779。

46. **precompiled cache-hit 模块将与 live compile 一样写回 full-width module signature，`CreateFunctionId()` 也会开始消费高低 32 位**
   - 影响文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` L4262-L4300、L4348-L4350；`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp` L2692-L2725。

---

## 深化 (2026-04-09 07:33:44)

本轮只补 `P16.2/P17.2` 之间尚未显式落地的 owner/lifetime seam：当前源码不仅“没有等异步读盘真正完成”，还把异步请求、回调目标和 explicit `import` 排序都绑在可复制的 `FFile` payload 上。经再次核对，`Documents/AutoPlans/DiscoveryPlans/Preprocessor_Plan.md` 仍不存在，因此以下条目仅引用 [A]/[C]/[E] 与实际源码复核结果。

### Phase 19：拆开 async source state 与 explicit-import payload owner

- [ ] **P19.1** 深化 `P16.2/P17.2`：把异步读盘状态与 manual `import` 排序都从可复制 `FFile` payload 中抽离，禁止回调再写 `TArray<FFile>` 元素地址
  - 当前 `FFile` 同时承载 `RawCode`、`ChunkedCode`、`ProcessedCode`、`Imports`、`Delegates` 以及 `AsyncReadHandle/AsyncSizeRequest/AsyncReadRequest` 三个裸指针。`PerformAsynchronousLoads()` 的 `SizeCallback/ReadCallback` 直接以引用捕获 `FFile&`，而 manual `import` 路径又在同一轮 `Preprocess()` 中通过 `OutSortedFiles.Add(File)` 和 `Files = SortedFiles` 复制整份 `FFile`。这意味着异步回调目标、compile order 载体和预处理工作区仍是同一个 move/copy 敏感对象。
  - 这一项不能只在 `PerformAsynchronousLoads()` 前后补等待。需要把 `FFile` 改成稳定 owner：至少让 `Files` 保存 heap-stable 的 `FFileWorkItem` / `TSharedRef<FFile>`（或等价 id-based owner），再把异步读取结果写到单独的 `FAsyncSourceLoadState` / `FSourceLoadSlot`。`ProcessImports()` 输出的也不再是 `TArray<FFile>`，而是 `OrderedFileIds` / `OrderedWorkItems` 这类轻量顺序视图；manual `import` 只移动索引或引用，不再复制 `RawCode`、`ChunkedCode` 和 raw async handle。
  - 只有这样，`P17.2` 的完成屏障才真正有稳定 owner 可以等待，`P16.2` 的 order/index artifact 才不会继续背着异步句柄和回调目标一起移动。否则即使补了 wait loop，后续一旦再引入取消、超时、批量 explicit `import` 或 future `#include` expansion，`Files` 仍会因为 copy semantics 把同一个 load state 分裂成多份别名。
  - 来源：
    - [A] `Documents/AutoPlans/Preprocessor_Analysis.md` — “发现 47/48：异步回调直接捕获 `FFile&`，而 explicit `import` 排序会在 `Preprocess()` 中途复制并替换整份 `Files` 数组；晚到回调会写旧副本甚至悬挂内存”
    - [C] `Documents/AutoPlans/TestCoverage/Preprocessor_TestGaps.md` — “NewTest-19：async `AddFile` 必须与 sync 路径产出完全一致；这条 parity 只有在 load state 与 compile-order owner 都稳定时才有可验证前提”
    - [E] `Documents/AutoPlans/ReferenceComparison/CrossComparison.md` — “`D4/D11` 强调当前插件的价值在于 deterministic reload contract 与 deterministic operator surface；若 source load callback 仍依赖数组元素地址，artifact 就会继续受调度与重排时机影响”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h` L120-L161 — `FFile` 仍同时持有完整预处理 payload 与三个异步请求裸指针；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` L149-L188 — `SizeCallback/ReadCallback` 直接捕获 `FFile&` 并回写 `RawCode/bLoadAsynchronous`；L235-L238、L497 — explicit `import` 路径仍通过 `OutSortedFiles.Add(File)` 和 `Files = SortedFiles` 复制整份 `FFile`。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorAsyncLoadTests.cpp`
- [ ] **P19.1** 📦 Git 提交：`[AngelscriptRuntime/Preprocessor] Refactor: decouple async load state from file payload ordering`
- [ ] **P19.1-T** 单元测试：锁住 async load state 与 explicit-import order 的 owner/lifetime 合同
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorAsyncLoadTests.cpp`
  - 测试场景：
    - 正常路径：同一组 `Base <- Shared <- Consumer` fixture 在 manual `import` 模式下走 async source load，预处理完成后 compile order 仍是 provider-first，且 `ProcessedCode`、`ImportedModules`、`CodeHash` 与 sync 路径完全一致，不因 `Files` 重排丢失源码。
    - 边界条件：同时存在多个 async 文件并发生 topological reorder 时，internal load slot 仍一一对应原始 source unit；切回 automatic `import` 模式后，不会因为排序视图不同而改变 source artifact 身份。
    - 错误路径：延迟完成或取消的 async 回调在 preprocessor 进入 reorder/teardown 后不得继续写旧 `FFile` 副本或悬挂地址；系统要么等待到 deterministic terminal state，要么把晚到回调安全丢弃，并稳定清理请求句柄。
  - 测试命名：`Angelscript.TestModule.Preprocessor.AsyncLoad.ExplicitImportOrderKeepsStableSourceOwners`、`Angelscript.TestModule.Preprocessor.AsyncLoad.LateCallbacksDoNotWriteStaleFileCopies`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P19.1-T** 📦 Git 提交：`[AngelscriptTest/Preprocessor] Test: cover async load owner stability during import reordering`

## 补充测试矩阵

| 改进项 | 测试文件 | 测试场景 | 优先级 |
|--------|---------|---------|--------|
| P19.1 | `Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorAsyncLoadTests.cpp` | async load owner stability、manual import reorder parity、late callback stale-write 防御 | P0 |

## 补充验收项

50. async source load callback 不得再把 `TArray<FFile>` 元素地址当成长期 owner；manual `import` reorder 与 preprocessor teardown 后，每个 source 仍只有一份稳定 load state。
51. manual `import` 的 compile order 必须以 id/ref 级 artifact 表达，而不是通过复制 `FFile` payload 达成；sync/async、automatic/manual 组合下的 module/source artifact 必须保持一致。

## 补充风险与行为变化

### 风险

45. **把 `FFile` 从值语义 payload 改成稳定 ref/id owner 会触及大部分 preprocessor helper 签名**
   - `ParseIntoChunks()`、`ProcessDefaults()`、`ResolveFilePos()`、`ProcessDelegates()` 等入口若残留旧的 raw-reference/复制语义，容易出现“一半走稳定 owner、一半还在拷贝 payload”的混合模型。
   - **缓解**：先用 `P19.1-T` 固定 async/manual-import 组合场景，再一次性把 order/load-state 相关 helper 切到统一 owner 类型，避免长期双轨。

46. **async load sidecar 与 UE async I/O 回调的清理顺序需要明确终态约束**
   - 如果 request 句柄、cancel 路径和 terminal state 之间的所有权不清楚，可能从“悬挂写”变成“句柄泄漏”或“失败被静默吞掉”。
   - **缓解**：让 load slot 只暴露 `Pending/Ready/Failed/Cancelled` 终态，并在 `P19.1-T` 里显式覆盖 delayed callback、cancel 与 cleanup 顺序。

### 已知行为变化

47. **manual `import` 模式下，`Files` 的物理数组顺序将不再等同于 compile order**
   - 影响文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h` L120-L161；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` L235-L238、L439-L497。

48. **async 回调将改为写入独立 load state，而不是直接回写数组元素中的 `FFile` payload**
   - 影响文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h` L156-L160；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` L149-L188。

---

## 深化 (2026-04-09 07:43:14)

本轮只补 `P3.2` 留下的 reload 闭环缺口：前面的 Plan 已经把 missing/read-failed source 从“合法 provider”里剥离，但 `ClassGenerator` 仍只对 `removed class` 建了对称移除路径。`Documents/AutoPlans/DiscoveryPlans/Preprocessor_Plan.md` 依旧不存在，因此以下条目仅引用 [A]/[C]/[E] 与实际源码复核结果。

### Phase 20：补空模块退出时的 enum/delegate 对称移除

- [ ] **P20.1** 深化 `P3.2`：把 `empty module` / `bad source state` 的 reload 收口扩展到 `enum/delegate`，并收紧 `EmptyModuleSetup` 测试合同
  - 当前 `P3.2` 已计划把 missing/read-failed source 从有效 provider 合同里移除，但 `ClassGenerator` 仍把“旧模块里有、新模块里没有”的对称移除逻辑只写给 class。`InitEnums()` 只从 `NewModule->ScriptModule` 枚举当前还存在的 enum，`InitDelegates()` 只遍历 `NewModule->Delegates`；`Analyze()` 结束时唯一会遍历旧模块缺失符号的分支仍是 `RemovedClasses`。这意味着一旦 `Preprocessor` 把 provider/模块状态从“空模块也能继续进图”改成 `Deleted/ReadFailed/Missing`，class 路径能被 `FullReloadRequired` 阻断，enum/delegate 路径却仍可能沿 soft reload 正常 swap-in。
  - 这一项需要在 `FModuleData` 上补 `RemovedEnums` / `RemovedDelegates` 或等价的 `RemovedReflectedSymbols` 视图：当 `OldModule` 存在而 `NewModule` 因空源码、删除、读失败或正常重构缺失某个 enum/delegate 时，reload analysis 必须像 removed class 一样显式提升 requirement，并在 `PerformReload()` / post-reload cleanup 阶段执行对称撤销或强制 full reload。若当前没有安全的软移除路径，应明确把这类场景提升成 `FullReloadRequired`，而不是继续让旧 reflected symbol 悬挂在 UE 状态里。
  - 同时要把 `FAngelscriptClassGeneratorEmptyModuleSetupTest` 从“空模块默认 SoftReload”改成更窄的 scaffolding 断言：允许“没有旧模块、只做空 scaffold”继续不崩，但不得再把“旧模块 -> 空模块/坏模块”隐含为合法 soft-reload 合同。否则 `P3.2` 对 source state 的修复一落地，测试层又会把 class-generator 重新拉回旧语义。
  - 来源：
    - [A] `Documents/AutoPlans/Preprocessor_Analysis.md` — “发现 63/64/65：删除或读失败脚本仍以同名空模块留在模块图里；`EmptyModuleSetup` 自动化把空模块写成 SoftReload 合同；reload 分析只显式处理 `removed class`，`enum/delegate` 整体消失时没有对称移除路径”
    - [C] `Documents/AutoPlans/TestCoverage/Preprocessor_TestGaps.md` — “NewTest-11：编译失败后不能残留旧 class/enum/delegate；当前 coverage 没有任何用例锁住 enum/delegate 模块在失败/删除后的撤销语义”
    - [E] `Documents/AutoPlans/ReferenceComparison/CrossComparison.md` — “D4 强调当前插件真正的优势是 `keep-old-code + queued full reload + 显式 reload contract`；若 enum/delegate 删除仍沿 soft reload 静默换入，这条 contract 在非 class 模块上就是破口”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` L105-L137、L289-L304 — 删除/读失败文件仍会落成空源码并生成带原始路径的 `CodeSection`；`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` L1565-L1621、L1771-L1790、L1856-L1866、L2162-L2184、L2382-L2388 — enum/delegate 只从 `NewModule` 现存符号建模，removed 分支与 cleanup 只覆盖 `RemovedClasses`；`Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/ClassGeneratorTests.cpp` L27-L56 — `EmptyModuleSetup` 仍把空模块直接断言为 `SoftReload`。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/ClassGeneratorTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptClassGeneratorModuleRemovalTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptModuleSourceStateTests.cpp`
- [ ] **P20.1** 📦 Git 提交：`[AngelscriptRuntime/ClassGenerator] Fix: make empty-module reloads remove enums and delegates symmetrically`
- [ ] **P20.1-T** 单元测试：锁住 `enum/delegate` 模块在 empty/bad-source reload 下的对称移除合同
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptClassGeneratorModuleRemovalTests.cpp`
  - 测试场景：
    - 正常路径：old/new 模块都保留同名 enum 与 delegate，且签名/值未变时，reload requirement 仍保持既有 soft/full 判定，不因新增移除逻辑误伤正常模块。
    - 边界条件：`OldModule` 只含 enum 或只含 delegate，`NewModule` 因删除脚本、读失败或显式空模块输入而不再包含对应符号时，`Setup()` 必须返回 `FullReloadRequired` 或走明确 removal path，绝不能继续落到 `SoftReload`。
    - 错误路径：在“旧模块有 enum/delegate，新模块空壳”的场景下，不得留下旧 `UUserDefinedEnum` / `UDelegateFunction` 的半更新引用；坏 reload 之后旧代码只能按 keep-old-code/full-reload contract 保活，不能把空模块当成功换入。
  - 测试命名：`Angelscript.TestModule.ClassGenerator.ModuleRemoval.EmptyReloadEscalatesRemovedEnum`、`Angelscript.TestModule.ClassGenerator.ModuleRemoval.EmptyReloadEscalatesRemovedDelegate`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P20.1-T** 📦 Git 提交：`[AngelscriptTest/ClassGenerator] Test: cover removed enum and delegate handling for empty-module reloads`

## 补充测试矩阵

| 改进项 | 测试文件 | 测试场景 | 优先级 |
|--------|---------|---------|--------|
| P20.1 | `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptClassGeneratorModuleRemovalTests.cpp` | enum-only/delegate-only module removal、empty/bad-source reload escalation、keep-old-code contract | P0 |

## 补充验收项

52. 当 `OldModule` 含有 class/enum/delegate 任一 reflected symbol，而 `NewModule` 因删除、读失败或空源码不再提供该符号时，reload analysis 不得再只对 class 生效；至少要统一升级到 `FullReloadRequired` 或执行对称 cleanup。
53. `EmptyModuleSetup` 不得再被视为“旧模块 -> 空模块”的广义 soft-reload 合同；测试与实现都必须明确区分“纯 scaffold 空模块”与“坏 source state 导致的空模块”。

## 补充风险与行为变化

### 风险

47. **把 removed-symbol 合同从 class 扩到 enum/delegate 会提升部分历史热重载场景的 reload 等级**
   - 某些过去“看似能 soft reload”的 enum-only/delegate-only 脚本删除或重构，现在会更早进入 `FullReloadRequired`。
   - **缓解**：先用 `P20.1-T` 把 old/new 模块差分固定下来，只在“旧模块真有符号且新模块确实缺失”时提升等级，不误伤纯 scaffold 或正常 unchanged case。

48. **收紧 `EmptyModuleSetup` 测试合同会暴露其它依赖空模块软重载假设的测试与工具**
   - 如果周边 helper 或学习用例把“空壳 module 可以直接软重载”当成默认前提，这轮会联动报红。
   - **缓解**：把旧测试拆成“scaffold 不崩”与“坏 source state 不得 soft reload”两类，避免继续用单一 smoke 测试混淆这两个语义。

### 已知行为变化

49. **enum-only 与 delegate-only 模块在删除、读失败或被空模块替换时，将不再静默沿 soft reload 继续换入**
   - 影响文件：`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` L1565-L1621、L1771-L1790、L1856-L1866、L2162-L2184、L2382-L2388。

50. **`ClassGenerator` 的空模块测试将从“默认 SoftReload”改成区分 scaffold 与 bad-source reload 两种合同**
   - 影响文件：`Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/ClassGeneratorTests.cpp` L27-L56。

---

## 深化 (2026-04-09 07:50:06)

本轮只补 `P3.2/P17.1` 尚未显式落地的同步缺失文件入口合同。经复核，`Documents/Plans/Plan_ScriptFileSystemRefactor.md` 主要处理 `IAngelscriptFileSystem` 注入、读盘抽象与日志级别，不替代这里的 `MissingOnDisk/DeletedByRequest/ReadFailed` 源码状态语义，以及它们对 `import`/provider/module graph 的影响。

### Phase 21：把同步缺失文件入口从“空模块 fallback”改成显式 source-state artifact

- [ ] **P21.1** 深化 `P3.2/P17.1`：在 `AddFile()` 同步入口区分 `MissingOnDisk`、`ReadFailed` 与 `DeletedByRequest`，禁止缺失文件继续伪装成可导入空模块
  - 当前同步入口仍把“磁盘上缺失/读失败”和“用户显式删除脚本”压成同一种后果：`AddFile()` 在 `LoadFileToString()` 重试失败后只打印 `Treating file as deleted.` warning，但不会记录任何 load-result；`Preprocess()` 随后仍照常 `ParseIntoChunks()`、`CondenseFromChunks()`，并在结尾为该文件追加一条空 `CodeSection`。这让首次编译、非删除型 hot reload 与显式 deleted request 在预处理侧共享同一空模块外观。
  - 这一项要把 source ingress 改成显式 artifact，例如 `EScriptSourceIngressState { Ready, MissingOnDisk, ReadFailed, DeletedByRequest }` 或等价结构：`AddFile(..., false, false)` 的同步失败必须写入 `MissingOnDisk/ReadFailed`，只保留 display path 与 load diagnostics，不再沿用 deleted 分支的空源码语义；`Preprocess()`、`ProcessImports()` 与公开结果访问器只允许 `Ready` 或明确允许的 `DeletedByRequest` scaffolding 继续进入后续阶段，`MissingOnDisk/ReadFailed` 则直接走 source-state gate、line-aware import/provider 诊断与 keep-old-code 路径。
  - 这一步不重复 `Plan_ScriptFileSystemRefactor.md` 的文件系统抽象，也不重复 `P17.1` 的删除入口改形。这里补的是“同步缺失文件分支的长期语义 owner”：删除是用户动作，missing/read-failed 是 I/O 结果，两者不能继续共享同一个 module/provider 身份，否则 `P3.2` 的 bad-provider 修复会在最早的 `AddFile()` ingress 就被重新打平。
  - 来源：
    - [A] `Documents/AutoPlans/Preprocessor_Analysis.md` — “发现 11/63：脚本读取失败会被静默降级成删除模块处理；删除或读失败脚本会以同名空模块继续留在模块图里，`import` 不再对缺失 provider fail-fast”
    - [C] `Documents/AutoPlans/TestCoverage/Preprocessor_TestGaps.md` — “NewTest-55：当前没有任何用例触发同步 `LoadFileToString()` 失败后的 fallback 分支；这条入口若不单独覆盖，缺失文件会继续以空模块语义漂移”
    - [D] `Documents/AutoPlans/ArchitectureReview/ModuleStructure_ArchReview.md` — “runtime 侧应保持 deterministic compile-pipeline contract，不应让 loader-style fallback 把不同输入原因重新压成同一模块解释规则”
    - [E] `Documents/AutoPlans/ReferenceComparison/CrossComparison.md` — “`D4/D11` 的横向对比强调当前 Angelscript 的优势是 failure memory + keep-old-code + plugin-owned deterministic artifact contract，不应退回 source-first loader 式的宽松缺失文件语义”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` L91-L137 — 同步 `AddFile()` 失败后只记录 `Treating file as deleted.` warning，没有任何 source-state 字段；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h` L120-L160 — `FFile` 只有 `RawCode` 与 `bLoadAsynchronous`，没有 `Missing/ReadFailed/Deleted` 区分；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` L220-L307 — `Preprocess()` 无条件 `ParseIntoChunks()` 并为每个 `FFile` 追加 `CodeSection`，即使源码从未成功载入。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorFileLoadTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptModuleSourceStateTests.cpp`
- [ ] **P21.1** 📦 Git 提交：`[AngelscriptRuntime/Preprocessor] Fix: distinguish missing file ingress from deleted source state`
- [ ] **P21.1-T** 单元测试：锁住同步缺失文件与显式删除的 source-state 分流合同
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorFileLoadTests.cpp`
  - 测试场景：
    - 正常路径：`bTreatAsDeleted=true` 的显式删除输入仍可产出受控 `DeletedByRequest` scaffolding，不读取磁盘，也不会附带旧源码、`Imports`、`Classes` 或 `Delegates`。
    - 边界条件：`AddFile(RelativePath, MissingAbsolutePath, false, false)` 在磁盘缺失时记录 `MissingOnDisk` 或等价状态，并保留相对路径到 module key 的稳定映射；该状态不会再和 deleted request 共用同一 diagnostics / provider eligibility。
    - 错误路径：另一个脚本 `import` 这份缺失文件时，预处理必须在 import 所在文件与行号给出 `missing or unreadable provider` 级别诊断；`GetModulesToCompile()` 或等价 compile-ready 结果不得再吐出一个可继续参与依赖解析的空 provider 模块。
  - 测试命名：`Angelscript.TestModule.Preprocessor.FileLoad.MissingSourceDoesNotMasqueradeAsDeleted`、`Angelscript.TestModule.Preprocessor.FileLoad.MissingSourceIsNotAnImportableProvider`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P21.1-T** 📦 Git 提交：`[AngelscriptTest/Preprocessor] Test: cover missing-file source ingress contracts`

## 补充测试矩阵

| 改进项 | 测试文件 | 测试场景 | 优先级 |
|--------|---------|---------|--------|
| P21.1 | `Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorFileLoadTests.cpp` | sync missing-file ingress、deleted-vs-missing source state、missing provider fail-fast | P1 |

## 补充验收项

54. 同步 `AddFile(..., false, false)` 的缺失文件输入不得再与显式 `Deleted` 请求共享同一 source-state artifact；`MissingOnDisk/ReadFailed/DeletedByRequest` 至少要在 preprocessor ingress 或等价 artifact 中可区分。
55. 缺失或读失败源码不得再通过空 `CodeSection` 伪装成 compile-ready provider；consumer `import` 必须在真实 import site 拿到 line-aware `missing/unreadable provider` 诊断，旧代码保持既有 keep-old-code 合同。

## 补充风险与行为变化

### 风险

56. **收紧同步缺失文件入口会暴露历史上依赖“空模块 fallback”做 smoke/trace 的 helper**
   - 某些 learning trace、白盒 helper 或旧测试，可能默认把“文件不存在也会得到一个空 module desc”当成可观察面；改成显式 source-state gate 后，这些路径会从假成功转成稳定失败。
   - **缓解**：先用 `P21.1-T` 固定 `DeletedByRequest` 与 `MissingOnDisk` 的差异，再把需要观察失败现场的辅助代码改读 diagnostics/source-state snapshot，而不是继续依赖 compile-ready 空模块。

### 已知行为变化

57. **同步缺失脚本路径将不再被记录成“treat as deleted”的空 provider 模块**
   - 影响文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` L91-L137、L220-L307；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h` L120-L160。
