# CQTest 全面迁移计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 将项目全部测试文件从 `IMPLEMENT_SIMPLE_AUTOMATION_TEST` 风格迁移到 CQTest `TEST_CLASS_WITH_FLAGS` + `TEST_METHOD` 风格，统一测试基础设施。

**Architecture:** 分三阶段推进——先升级宏基础设施使其同时兼容新旧两种风格，再按目录分批转换 324 个旧式文件（822 个测试宏实例），最后清理 33 个混合文件中的残留旧代码。每阶段结束后构建验证零错误。

**Tech Stack:** UE5 CQTest (`TEST_CLASS_WITH_FLAGS` / `TEST_METHOD` / `BEFORE_ALL` / `AFTER_ALL`)、AngelscriptTestMacros.h 宏体系、RunBuild.ps1 构建验证

---

## 现状快照（2026-04-30）

| 类别 | 文件数 | 备注 |
| --- | --- | --- |
| 旧式 `IMPLEMENT_SIMPLE/COMPLEX` | **324** | 822 个宏实例 |
| CQTest `TEST_CLASS_WITH_FLAGS` | **170** | 已转换 |
| 混合（两种共存） | **33** | 主要在 Bindings/ |
| `return false;` 行 | ~3013 | 需改为 `return;` |
| `*this,` 传参行 | ~1169 | 需改为 `*TestRunner,` |

### 宏兼容性分析

| 宏 | CQTest 不兼容点 | 使用文件数 |
| --- | --- | --- |
| `ASTEST_BEGIN_NATIVE` | `AddError(...)` 裸调 + `return false;` | 少量 |
| `ASTEST_BEGIN_BARE` | `AddError(...)` 裸调 + `return false;` | 已有 `_VOID` 版本 |
| `ASTEST_COMPILE_RUN_INT` | `*this` + `return false;` | ~10 |
| `ASTEST_COMPILE_RUN_INT64` | `*this` + `return false;` | 少量 |
| `ASTEST_BUILD_MODULE` | `*this` + `return false;` | 少量 |
| `ASTEST_BEGIN_SHARE_CLEAN` 等 | 无不兼容（不含 return/this） | 131 |

### 转换公式

每个文件的机械转换规则如下：

```
旧式:
  #include "Misc/AutomationTest.h"
  IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMyTest, "Path.To.Test", Flags)
  bool FMyTest::RunTest(const FString& Parameters) {
      ...
      return true;
  }

新式:
  #include "CQTest.h"
  TEST_CLASS_WITH_FLAGS(FMyTest, "Path.To.Parent", Flags)
  {
      TEST_METHOD(LastSegment)
      {
          ...
      }
  };
```

核心变更点：
1. `#include "Misc/AutomationTest.h"` → `#include "CQTest.h"`
2. `IMPLEMENT_SIMPLE_AUTOMATION_TEST` → 合并到 `TEST_CLASS_WITH_FLAGS`
3. `bool RunTest(...)` → `TEST_METHOD(Name) {}`
4. `return true;` → 删除
5. `return false;` → `return;`
6. `*this` 传给 `FAutomationTestBase&` → `*TestRunner`
7. 裸调 `TestNotNull(...)` 等 → `TestRunner->TestNotNull(...)`
8. 裸调 `AddError(...)` 等 → `TestRunner->AddError(...)`
9. `using namespace ..._Private;` 必须在 `TEST_METHOD` 体内，不能在文件级

## Phase 0：宏基础设施升级

> 目标：在 `AngelscriptTestMacros.h` 中为所有含 `return false` / `*this` / 裸调 `AddError` 的宏新增 `_VOID` 版本，使宏体系同时兼容旧式和 CQTest。不改动任何测试文件。

### Task 0.1：新增 CQTest 兼容宏

**Files:**
- Modify: `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestMacros.h`

- [ ] **P0.1.1** 在 `ASTEST_BEGIN_NATIVE` 下方新增 `ASTEST_BEGIN_NATIVE_VOID`
  - `AddError(...)` → `TestRunner->AddError(...)`
  - `return false;` → `return;`

- [ ] **P0.1.2** 新增 `ASTEST_COMPILE_RUN_INT_VOID` / `ASTEST_COMPILE_RUN_INT64_VOID` / `ASTEST_BUILD_MODULE_VOID`
  - `*this` → `*TestRunner`
  - `return false;` → `return;`

- [ ] **P0.1.3** 构建验证：`Tools/RunBuild.ps1 -Label cqtest-p0 -TimeoutMs 600000`（不应引入任何新错误，因为没有调用方改动）

- [ ] **P0.1.4** 📦 Git 提交：`[AngelscriptTest/Shared] Feat: add CQTest-compatible _VOID macro variants for all test lifecycle macros`

## Phase 1：Bindings 混合文件清理（33 个混合文件）

> 目标：这 33 个文件已经部分使用 CQTest，但还残留 `IMPLEMENT_SIMPLE_AUTOMATION_TEST`。优先清理它们，使 Bindings 目录达到纯 CQTest 状态。

### Task 1.1：Bindings/ 混合文件批量转换

**Files:**
- Modify: `Plugins/Angelscript/Source/AngelscriptTest/Bindings/` 下 29 个混合文件
- Modify: `Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/` 下 2 个混合文件

每个文件的操作：
- [ ] **P1.1.1** 将文件中残留的 `IMPLEMENT_SIMPLE_AUTOMATION_TEST` + `RunTest` 函数体转为 `TEST_METHOD`，合并进已有的 `TEST_CLASS_WITH_FLAGS`
- [ ] **P1.1.2** 替换 `return true;` → 删除，`return false;` → `return;`
- [ ] **P1.1.3** 替换 `*this,` → `*TestRunner,` 在 TEST_METHOD 体内
- [ ] **P1.1.4** 确认 `using namespace ..._Private;` 在 TEST_METHOD 体内
- [ ] **P1.1.5** 构建验证
- [ ] **P1.1.6** 📦 Git 提交：`[AngelscriptTest/Bindings] Refactor: complete CQTest migration for mixed-style binding tests`

### Task 1.2：Template/ 混合文件清理

**Files:**
- Modify: `Plugins/Angelscript/Source/AngelscriptTest/Template/Template_CQTest.cpp`
- Modify: `Plugins/Angelscript/Source/AngelscriptTest/Template/Template_WorldTick.cpp`

- [ ] **P1.2.1** 同上转换规则
- [ ] **P1.2.2** 📦 Git 提交：`[AngelscriptTest/Template] Refactor: complete CQTest migration for template files`

## Phase 2：Bindings/ 剩余纯旧式文件

> 目标：Bindings 目录中没有 CQTest 的纯旧式文件。

### Task 2.1：Bindings/ 纯旧式文件转换（~14 个文件）

**Files:**
- Modify: `Plugins/Angelscript/Source/AngelscriptTest/Bindings/` 下所有纯旧式 `.cpp`

- [ ] **P2.1.1** 每个文件按转换公式处理
- [ ] **P2.1.2** 构建验证
- [ ] **P2.1.3** 📦 Git 提交：`[AngelscriptTest/Bindings] Refactor: migrate remaining binding tests to CQTest`

## Phase 3：Core/ 全量转换（46 个文件）

> 目标：Core 目录是最大的旧式文件集合，包含 GAS 测试、引擎核心测试等。

### Task 3.1：Core/GAS 测试转换（~20 个文件）

**Files:**
- Modify: `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGAS*.cpp`
- Modify: `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptAbilityTask*.cpp`
- Modify: `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGameplay*.cpp`

- [ ] **P3.1.1** 按转换公式批量处理
- [ ] **P3.1.2** 注意 GAS 测试中 `ASTEST_BEGIN_SHARE_CLEAN` + `ASTEST_END_SHARE_CLEAN` + `return true` 的模式——`ASTEST_BEGIN/END_SHARE_CLEAN` 本身兼容 CQTest（无 return），但包裹的 `return true` 需要删除
- [ ] **P3.1.3** 构建验证
- [ ] **P3.1.4** 📦 Git 提交：`[AngelscriptTest/Core] Refactor: migrate GAS tests to CQTest`

### Task 3.2：Core/ 引擎核心测试转换（~26 个文件）

**Files:**
- Modify: `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEngine*.cpp`
- Modify: `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBind*.cpp`
- Modify: 其他 Core/ 下非 GAS 文件

- [ ] **P3.2.1** 按转换公式批量处理
- [ ] **P3.2.2** 构建验证
- [ ] **P3.2.3** 📦 Git 提交：`[AngelscriptTest/Core] Refactor: migrate engine core tests to CQTest`

## Phase 4：ClassGenerator/ + Compiler/ 转换（57 个文件）

### Task 4.1：ClassGenerator/ 转换（28 个文件）

**Files:**
- Modify: `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/*.cpp`

- [ ] **P4.1.1** 按转换公式批量处理
- [ ] **P4.1.2** 构建验证
- [ ] **P4.1.3** 📦 Git 提交：`[AngelscriptTest/ClassGenerator] Refactor: migrate class generator tests to CQTest`

### Task 4.2：Compiler/ 转换（29 个文件）

**Files:**
- Modify: `Plugins/Angelscript/Source/AngelscriptTest/Compiler/*.cpp`

- [ ] **P4.2.1** 按转换公式批量处理
- [ ] **P4.2.2** 构建验证
- [ ] **P4.2.3** 📦 Git 提交：`[AngelscriptTest/Compiler] Refactor: migrate compiler pipeline tests to CQTest`

## Phase 5：Debugger/ + HotReload/ 转换（35 个文件）

### Task 5.1：Debugger/ 转换（21 个文件）

**Files:**
- Modify: `Plugins/Angelscript/Source/AngelscriptTest/Debugger/*.cpp`

- [ ] **P5.1.1** 按转换公式批量处理
- [ ] **P5.1.2** 注意 Debugger 测试使用专用 fixture（`AngelscriptDebuggerTestSession`/`AngelscriptDebuggerTestClient`），helper 签名有 `FAutomationTestBase&` 参数需要走 `*TestRunner`
- [ ] **P5.1.3** 构建验证
- [ ] **P5.1.4** 📦 Git 提交：`[AngelscriptTest/Debugger] Refactor: migrate debugger tests to CQTest`

### Task 5.2：HotReload/ 转换（14 个文件）

**Files:**
- Modify: `Plugins/Angelscript/Source/AngelscriptTest/HotReload/*.cpp`

- [ ] **P5.2.1** 按转换公式批量处理
- [ ] **P5.2.2** 构建验证
- [ ] **P5.2.3** 📦 Git 提交：`[AngelscriptTest/HotReload] Refactor: migrate hot reload tests to CQTest`

## Phase 6：中小目录批量转换（~50 个文件）

### Task 6.1：Functional/ 子目录（~15 个文件）

**Files:**
- Modify: `Plugins/Angelscript/Source/AngelscriptTest/Functional/*/` 下各 1 个旧式文件

- [ ] **P6.1.1** 按转换公式批量处理
- [ ] **P6.1.2** 构建验证
- [ ] **P6.1.3** 📦 Git 提交：`[AngelscriptTest/Functional] Refactor: migrate functional tests to CQTest`

### Task 6.2：Examples/ 转换（21 个文件）

**Files:**
- Modify: `Plugins/Angelscript/Source/AngelscriptTest/Examples/*.cpp`

- [ ] **P6.2.1** 按转换公式批量处理
- [ ] **P6.2.2** 构建验证
- [ ] **P6.2.3** 📦 Git 提交：`[AngelscriptTest/Examples] Refactor: migrate script example tests to CQTest`

### Task 6.3：Learning/ 转换（21 个文件）

**Files:**
- Modify: `Plugins/Angelscript/Source/AngelscriptTest/Learning/Native/*.cpp`
- Modify: `Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/*.cpp`

- [ ] **P6.3.1** 按转换公式批量处理
- [ ] **P6.3.2** 构建验证
- [ ] **P6.3.3** 📦 Git 提交：`[AngelscriptTest/Learning] Refactor: migrate learning trace tests to CQTest`

### Task 6.4：零散目录（StaticJIT/FileSystem/GC/Networking/Component/Dump/Performance/Validation/Preprocessor/Shared）

**Files:**
- Modify: 上述目录下所有旧式 `.cpp`（约 30 个文件）

- [ ] **P6.4.1** 按转换公式批量处理
- [ ] **P6.4.2** 构建验证
- [ ] **P6.4.3** 📦 Git 提交：`[AngelscriptTest] Refactor: migrate remaining scattered tests to CQTest`

## Phase 7：Editor 模块转换（47 个文件）

> 目标：`AngelscriptEditor/Tests/` 是独立模块，与 AngelscriptTest 分开处理。

### Task 7.1：Editor/Tests/ 全量转换

**Files:**
- Modify: `Plugins/Angelscript/Source/AngelscriptEditor/Tests/*.cpp`（47 个文件）

- [ ] **P7.1.1** 按转换公式批量处理
- [ ] **P7.1.2** Editor 测试中有些 helper 使用 `Test.TestNotNull(...)` 形式（接收 `FAutomationTestBase&` 参数），调用方需要改成 `*TestRunner`
- [ ] **P7.1.3** 构建验证
- [ ] **P7.1.4** 📦 Git 提交：`[AngelscriptEditor/Tests] Refactor: migrate editor tests to CQTest`

## Phase 8：旧式宏清理 + Template/ 更新

> 目标：所有测试文件转换完成后，旧式宏不再有调用方。清理 `AngelscriptTestMacros.h` 中的旧版宏，将 `_VOID` 版本提升为默认版本。更新 Template/ 模板文件反映 CQTest 最佳实践。

### Task 8.1：宏头文件清理

**Files:**
- Modify: `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestMacros.h`

- [ ] **P8.1.1** 删除旧式宏（`ASTEST_BEGIN_NATIVE`/`ASTEST_BEGIN_BARE`/`ASTEST_COMPILE_RUN_INT`/`ASTEST_COMPILE_RUN_INT64`/`ASTEST_BUILD_MODULE`）
- [ ] **P8.1.2** 将 `_VOID` 后缀版本改名为无后缀版本（如 `ASTEST_BEGIN_NATIVE_VOID` → `ASTEST_BEGIN_NATIVE`）
- [ ] **P8.1.3** 全局搜索替换所有调用方的 `_VOID` 后缀
- [ ] **P8.1.4** 构建验证
- [ ] **P8.1.5** 📦 Git 提交：`[AngelscriptTest/Shared] Refactor: promote _VOID macros to default and remove legacy bool-return variants`

### Task 8.2：Template/ 模板更新

**Files:**
- Modify: `Plugins/Angelscript/Source/AngelscriptTest/Template/Template_CQTest.cpp`
- Modify: 其他 Template 文件（确保全部使用 CQTest 风格）

- [ ] **P8.2.1** 确保所有 Template 文件展示 CQTest 最佳实践
- [ ] **P8.2.2** 📦 Git 提交：`[AngelscriptTest/Template] Docs: update templates to reflect CQTest-only convention`

## Phase 9：文档同步 + 终态验证

### Task 9.1：文档更新

**Files:**
- Modify: `Documents/Guides/TestConventions.md`
- Modify: `Plugins/Angelscript/AGENTS.md`
- Modify: `AGENTS.md`
- Modify: `AGENTS_ZH.md`

- [ ] **P9.1.1** 在 TestConventions.md 中：
  - 移除对 `IMPLEMENT_SIMPLE_AUTOMATION_TEST` 的引用
  - 更新"新增测试的标准流程"为 CQTest 唯一入口
  - 更新典型测试场景样本为 CQTest 形式
  - 更新宏使用指南
- [ ] **P9.1.2** 在 AGENTS.md / AGENTS_ZH.md 中同步更新测试模块描述
- [ ] **P9.1.3** 📦 Git 提交：`[Docs] Docs: update test conventions and agent guidance for CQTest-only`

### Task 9.2：终态全量构建 + 冗余扫描

- [ ] **P9.2.1** 清理 intermediate 全量构建：`rm -rf Plugins/Angelscript/Intermediate/Build/...; Tools/RunBuild.ps1 -Label cqtest-final -TimeoutMs 600000`
- [ ] **P9.2.2** 扫描残留：`grep -r "IMPLEMENT_SIMPLE_AUTOMATION_TEST\|IMPLEMENT_COMPLEX_AUTOMATION_TEST" Plugins/Angelscript/Source/ --include="*.cpp"` 应返回空
- [ ] **P9.2.3** 扫描残留裸调：`grep -rn "^\s*TestNotNull\|^\s*TestTrue\|^\s*TestEqual\|^\s*TestFalse" Plugins/Angelscript/Source/AngelscriptTest/ --include="*.cpp"` 应仅在 `#if 0` 块或 helper 函数定义中出现
- [ ] **P9.2.4** 📦 Git 提交：`[AngelscriptTest] Chore: validate CQTest-only migration complete with zero errors`

### Task 9.3：Plan 归档

- [ ] **P9.3.1** 将本 Plan 顶部加归档元信息并移到 `Documents/Plans/Archives/`
- [ ] **P9.3.2** 更新 `Plan_StatusPriorityRoadmap.md` 和 `Plan_OpportunityIndex.md`
- [ ] **P9.3.3** 📦 Git 提交：`[Docs/Plans] Docs: archive CQTest migration plan`

## 验收标准

1. `grep -r "IMPLEMENT_SIMPLE_AUTOMATION_TEST\|IMPLEMENT_COMPLEX_AUTOMATION_TEST" Plugins/Angelscript/Source/ --include="*.cpp"` 返回空（`#if 0` 禁用的文件除外）
2. 全量 unity build 零错误：`Tools/RunBuild.ps1 -Label cqtest-final` → `Result: Succeeded`
3. `AngelscriptTestMacros.h` 中不含 `return false;` 和 `*this` 的旧式宏
4. `TestConventions.md` 中 CQTest 是唯一推荐的测试编写方式
5. 所有 TEST_METHOD 体内的 assertion 通过 `TestRunner->` 调用

## 风险与注意事项

1. **CQTest `TEST_METHOD` 是 void 返回值**：所有 `return false;` 必须改为 `return;`，不能保留 `bool bPassed` 累积判断模式
2. **`BEFORE_ALL` / `AFTER_ALL` 是 static 上下文**：里面用 `TestRunner` 而非 `Test.`；但有些现有代码在 `BEFORE_ALL` 里做引擎初始化并检查返回值，需要改写
3. **unity build 兼容**：转换后必须确保 `using namespace ..._Private;` 保持在函数体内，不回流到文件级
4. **Shared/ helper 函数签名不变**：helper 仍接收 `FAutomationTestBase&`，调用方从 `*this` 改为 `*TestRunner` 即可，不需要改 helper 签名
5. **批量转换建议用并行 agent**：每个 Phase 可拆为多个并行 agent，每个处理 10-15 个文件，加速迭代
6. **每个 Phase 末尾必须构建验证**：不允许跨 Phase 积累未验证的改动

## 执行顺序建议

```
Phase 0 (宏升级) → Phase 1 (混合文件) → Phase 2 (Bindings 剩余)
→ Phase 3 (Core) → Phase 4 (ClassGen+Compiler) → Phase 5 (Debugger+HotReload)
→ Phase 6 (中小目录) → Phase 7 (Editor) → Phase 8 (宏清理) → Phase 9 (文档+验证)
```

预计总工作量：324 个文件，822 个测试宏实例，~3013 行 `return false` + ~1169 行 `*this` 需要替换。按每 Phase 并行 agent 处理，预计 4-6 轮 agent 迭代可完成。
