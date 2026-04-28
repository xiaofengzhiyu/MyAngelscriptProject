# 测试覆盖实现规则

## 目的

本规则约束 `Tools/ImplementTestCoverage/` 工具（基于 RalphLoop 驱动）的 AI 行为。工具的核心任务是：

1. 从 `Documents/AutoPlans/TestCoverage/*_TestGaps.md` 中选取 `NewTest-<编号>` 条目。
2. **实际编写 C++ 测试代码**，将测试添加到 `Plugins/Angelscript/Source/AngelscriptTest/` 对应目录。
3. 在 `Documents/ImplementTestCoverage/ImplementHistory.md` 中记录选取和完成状态。

注意：`NewTest-<编号>` 只是所在 gap 文档内的局部标签，不保证跨文档唯一，甚至不保证同一文档内唯一。实现工具不得把编号本身当成全局去重键或跨模块排序键。

与 `TestCoverageGap` 工具（只分析不实现）不同，本工具 **必须产出可编译的测试代码**。

## 适用范围

- 适用于 `Plugins/Angelscript/Source/AngelscriptTest/` 下各子系统的测试新增。
- 每轮循环实现一个 `NewTest` 条目，不得批量实现。
- 通过独立的 bat 文件启动对应子系统，bat 调用 `Tools/RalphLoop/ralph-loop.ps1` 驱动。

### 覆盖的子系统

| 子系统 | 测试缺口文档 | 测试目标目录 |
|--------|-------------|-------------|
| RuntimeCore | `RuntimeCore_TestGaps.md` | `AngelscriptTest/Core/`, `Subsystem/`, `GC/`, `FileSystem/` |
| ClassGenerator | `ClassGenerator_TestGaps.md` | `AngelscriptTest/ClassGenerator/`, `HotReload/` |
| BindSystem | `BindSystem_TestGaps.md` | `AngelscriptTest/Bindings/` |
| Preprocessor | `Preprocessor_TestGaps.md` | `AngelscriptTest/Preprocessor/`, `Compiler/` |
| DebuggingAndJIT | `DebuggingAndJIT_TestGaps.md` | `AngelscriptTest/Debugger/` |
| FunctionLibraries | `FunctionLibraries_TestGaps.md` | `AngelscriptTest/Bindings/`（分散） |
| LanguageFeatures | `LanguageFeatures_TestGaps.md` | `AngelscriptTest/Angelscript/`, `Internals/`, `Interface/` |
| EditorAndTools | `EditorAndTools_TestGaps.md` | `AngelscriptTest/Editor/` |

单次运行跨全部 8 个子系统统一选取，按 P0 > P1 > P2 > P3 优先级排序。

## GapKey 规则

为避免 `NewTest-<编号>` 串号，所有选题、去重、恢复都必须使用稳定复合键 `GapKey`：

1. 优先使用 `<模块>|<建议测试名>`。
2. 如果建议测试名缺失、重复或无法可靠提取，则改用 `<模块>|<NewTest 标题原文>`。
3. 判断某个 gap 是否已处理时，优先匹配 `ImplementHistory.md` 中显式记录的 `GapKey`。
4. 对旧格式历史记录（未写 `GapKey`）做兼容：仅当“模块相同且测试名相同”时，才视为同一条 gap。
5. 禁止只根据 `NewTest-<编号>` 判定是否重复。

## 工具架构

```text
Tools/ImplementTestCoverage/
├── RunImplementTestCoverage.ps1    # 主脚本，包含合并提示词
└── RunImplementTestCoverage.bat    # 唯一启动入口
```

调用链：`RunImplementTestCoverage.bat` → `RunImplementTestCoverage.ps1` → `Tools/RalphLoop/ralph-loop.ps1`

## 输入/输出文件约定

### 输入（只读）

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

### 输出（读写）

```text
Documents/ImplementTestCoverage/
└── ImplementHistory.md                # 实现记录（进度跟踪）

Plugins/Angelscript/Source/AngelscriptTest/
└── <对应子目录>/                       # 实际测试代码
```

## 每轮工作流程

### 第一步：检查未完成工作

1. 读取 `Documents/ImplementTestCoverage/ImplementHistory.md`。
2. 搜索所有 `- [ ]` 标记的条目。
3. 如有未完成条目 → 跳到第三步继续实现。
4. 如无未完成条目 → 进入第二步。

### 第二步：选择新测试并记录

1. 读取全部 8 个 `*_TestGaps.md`。
2. 为每个 `NewTest` 提取模块、标题原文、建议测试名、优先级，并计算 `GapKey`。
3. 找到尚未在 `ImplementHistory.md` 中出现的 `GapKey` 条目；旧格式历史记录按“模块相同且测试名相同”兼容匹配。
4. 按优先级 P0 > P1 > P2 > P3 选择。
5. 同优先级时，优先选择当前 `ImplementHistory.md` 中累计条目更少的模块；若仍相同，则按子系统表顺序，再按条目在 gap 文档中的出现顺序选择。
6. 在 `ImplementHistory.md` 末尾追加 `- [ ]` 条目（含开始时间）。
7. 追加后才进入实现。

### 第三步：实现测试

1. 读取 `NewTest` 条目的详细描述。
2. 读取目标目录中已有的测试文件，了解 include、helper、宏风格。
3. 验证 `NewTest` 关联的源码 API 仍然存在。
4. 编写测试代码：
   - 遵循 `ASTEST_*` 宏体系
   - 遵循 `Angelscript.TestModule.<Theme>.*` 命名
   - 单文件 300-500 行
   - 文件名 `Angelscript` 开头
5. 新建或追加到已有测试文件。

### 第四步：标记完成

实现完成后，将 `ImplementHistory.md` 中对应条目更新为 `- [x]`，附带：
- 完成时间
- 实际文件路径
- 用例数
- 备注

如果条目不可行，标记为 `- [~]` 并说明原因，然后选择下一个。

## ImplementHistory.md 格式

### 条目格式

**进行中**：
```markdown
- [ ] **NewTest-<编号>** (<模块>) [GapKey: <模块>|<建议测试名或标题>] `<测试名>` — <描述> (开始时间: YYYY-MM-DD HH:MM)
```

**已完成**：
```markdown
- [x] **NewTest-<编号>** (<模块>) [GapKey: <模块>|<建议测试名或标题>] `<测试名>` — <描述> (开始: YYYY-MM-DD HH:MM, 完成: YYYY-MM-DD HH:MM)
  - 文件: `<路径>`
  - 用例数: <N>
  - 备注: <说明>
```

**已跳过**：
```markdown
- [~] **NewTest-<编号>** (<模块>) [GapKey: <模块>|<建议测试名或标题>] `<测试名>` — 跳过: <原因> (YYYY-MM-DD HH:MM)
```

### 统计表

文档顶部维护一张分模块统计表，每轮更新：

```markdown
| 模块 | 已完成 | 进行中 | 已跳过 |
|------|--------|--------|--------|
| RuntimeCore | N | N | N |
| ... | ... | ... | ... |
```

## 关键约束

1. **每轮一个 NewTest**。保持原子性，便于中断恢复。
2. **先记录后实现**。`- [ ]` 先写入文件，确保中断后可恢复。
3. **完成后才标 [x]**。代码确认正确后才标记。
4. **TestGaps 只读**。不得修改 `Documents/AutoPlans/TestCoverage/` 下的文件。
5. **不破坏已有测试**。只新增，不修改已有测试逻辑。
6. **API 不存在则跳过**。如源码 API 已变更，标记 `[~]` 并选下一个。
7. **遵循项目规范**：`TestConventions.md`、`TESTING_GUIDE.md`。
8. **中文文字，英文代码**。

## 测试代码质量要求

1. **精确断言**：每个测试用例必须有至少一个 `TestTrue` / `TestEqual` / `TestFalse` / `TestNotNull` 等断言。
2. **隔离性**：使用 `FAngelscriptEngineScope` 或 `ASTEST_CREATE_ENGINE_*` 宏确保引擎隔离。
3. **清理**：使用 `ON_SCOPE_EXIT` 或 RAII 确保资源释放。
4. **命名**：测试名体现被测行为，非实现细节。
5. **参考风格**：写新测试前先读同目录已有测试，保持一致风格。
