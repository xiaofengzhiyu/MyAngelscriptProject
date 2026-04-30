# Note_CQTest — CQTest 测试框架使用笔记

> **所属前缀**: Note_（零散笔记族）
> **关注层面**: CQTest 框架原理、优势分析，以及在本插件测试模块中的采用方式与注意事项
> **关键源码**:
> `Engine/Source/Developer/CQTest/Public/CQTest.h`（UE 引擎侧 CQTest 定义）
> · `Engine/Source/Developer/CQTest/Public/Assert/NoDiscardAsserter.h`（默认断言器）
> · `Engine/Source/Developer/CQTest/README.md`（官方 README）
> · `Plugins/Angelscript/Source/AngelscriptTest/Template/Template_CQTest.cpp`（本项目教学模板）
> · `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestMacros.h`（引擎创建 / 生命周期宏）
> · `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptBindingsCoverage.h`（覆盖率 Profile）
> · `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptBindingsModuleBuilder.h`（FCoverageModuleScope RAII）
> · `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptBindingsAssertions.h`（断言 Helpers）
> **关联文档**:
> `Documents/Knowledges/ZH/Test_Layering.md` — 测试模块总体分层
> · `Documents/Knowledges/ZH/Test_Infrastructure.md` — 测试基础设施与 Helper
> **外部参考**:
> [Epic 官方 CQTest 文档](https://dev.epicgames.com/documentation/zh-cn/unreal-engine/cqtest-test-framework-for-unreal-engine)
> · `Engine/Source/Developer/CQTest/README.md`（"Why CQTest?" 设计动机）

---

## 1. CQTest 是什么

CQTest（**C**ode **Q**uality **Test**）是 Unreal Engine 内置的测试框架，位于 `Engine/Source/Developer/CQTest/`。官方定义：

> CQTest is an extension of UE's `FAutomationTestBase` providing **test fixtures** and common automation testing commands. CQTest's goal is to **simplify writing new tests** compared with previous testing frameworks in UE, and support *before* and *after* test actions to **automatically reset state** between tests.

它提供了一种**基于类的测试组织方式**（xUnit 风格），用以替代传统的 `IMPLEMENT_SIMPLE_AUTOMATION_TEST` 单函数宏和 `DEFINE_SPEC` BDD 风格框架。

---

## 2. CQTest 框架原理

### 2.1 模板元编程三层架构

CQTest 的核心由三个 C++ 模板层组成，从底向上：

```text
┌──────────────────────────────────────────────────────────────────────────────┐
│  Layer 3: TEST_CLASS_WITH_FLAGS / TEST / TEST_CLASS 等宏                     │
│           ↓ 展开为 ↓                                                        │
│  用户声明的测试类 struct FMyTest : TTest<FMyTest, FNoDiscardAsserter>         │
├──────────────────────────────────────────────────────────────────────────────┤
│  Layer 2: TTest<Derived, AsserterType>                                      │
│           — CRTP 中间层                                                      │
│           — 拥有 FFunctionRegistrar 内嵌结构体，负责静态注册                  │
│           — 拥有 static TMap<FString, TestMethod> Methods 映射               │
│           — 拥有 static TTestRunner<AsserterType>* TestRunner 指针           │
│           — CreateTestClass() 工厂函数绑定 TestRunner 并创建实例              │
├──────────────────────────────────────────────────────────────────────────────┤
│  Layer 1: TBaseTest<AsserterType>                                           │
│           — 抽象基类                                                         │
│           — 管理 Setup() / TearDown() 虚函数                                 │
│           — 持有 FAutomationTestBase& TestRunner 引用                         │
│           — 持有 AsserterType Assert 断言器实例                               │
│           — 持有 FTestCommandBuilder 延迟命令构建器                            │
│           — BeforeAllFunc / AfterAllFunc 函数指针                             │
│           — AddCommand / AddError / AddWarning / AddInfo                     │
│           — 纯虚 RunTest(const FString& MethodName)                          │
└──────────────────────────────────────────────────────────────────────────────┘

┌──────────────────────────────────────────────────────────────────────────────┐
│  TTestRunner<AsserterType> : public FAutomationTestBase                     │
│           — 真正注册到 UE Automation Framework 的测试实例                     │
│           — 拥有 TArray<FString> TestNames 记录所有 TEST_METHOD 名           │
│           — 拥有 TMap<FString, int32> TestLineNumbers 记录行号               │
│           — 拥有 TTestInstanceGenerator<AsserterType> TestInstanceFactory    │
│           — GetTests() 返回所有注册的测试方法名                              │
│           — RunTest() 通过工厂创建测试类实例 → 分派到对应 TEST_METHOD         │
└──────────────────────────────────────────────────────────────────────────────┘
```

### 2.2 静态注册机制

CQTest 利用 **C++ 静态初始化** 实现零配置测试发现：

```text
编译期：
  TEST_CLASS_WITH_FLAGS(FMyTest, "Path", Flags)
  展开为 → struct FMyTest : TTest<FMyTest, FNoDiscardAsserter> { ... };
          + static TTestRunner<FNoDiscardAsserter> GRunner_FMyTest(...)
            构造时自动注册到 FAutomationTestFramework::Get()

  TEST_METHOD(CaseName)
  展开为 → static FFunctionRegistrar _Reg_CaseName("CaseName", &FMyTest::CaseName, __LINE__, "");
            构造时将方法名+函数指针+行号写入 DerivedType::Methods 和 TestRunner->TestNames
          + void CaseName()  // 方法体

运行期：
  UE Automation Framework 枚举已注册的 FAutomationTestBase 实例
  → 调用 TTestRunner::GetTests() 返回 TestNames
  → 对每个测试名调用 TTestRunner::RunTest(MethodName)
     → TestInstanceFactory 创建 TTest 派生类实例（每次 new）
     → 实例.RunTest(MethodName)
        → 查找 Methods[MethodName] 对应的成员函数指针
        → (this->*Method)() 执行
```

关键设计点：

- **每个 TEST_METHOD 创建独立实例**：`RunTest` 每次通过 `CreateTestClass()` 工厂创建新的测试类对象，确保**成员变量自动重置到声明时的初始值**，无需手动清理。
- **行号追踪**：`__LINE__` 在 `FFunctionRegistrar` 构造时记录，使 IDE 和 Automation 面板可以跳转到精确行。
- **标签系统**：`TEST_METHOD_WITH_TAGS` / `TEST_CLASS_WITH_TAGS` 通过 `FAutomationTestFramework::RegisterAutomationTestTags` 注册过滤标签。

### 2.3 生命周期状态机

```text
┌─── TTestRunner 首次执行 ───────────────────────────────────────────┐
│                                                                    │
│  ① BEFORE_ALL()   — 静态方法，整个类仅执行一次                     │
│      通过 BeforeAllDelegate 挂到 FAutomationTestFramework          │
│      适合：加载关卡、初始化重资源                                   │
│                                                                    │
│  ┌─── 对每个 TEST_METHOD 循环 ──────────────────────────────────┐  │
│  │                                                              │  │
│  │  ② 创建新的 Derived 实例（成员变量重置到声明初始值）          │  │
│  │  ③ Setup()  / BEFORE_EACH()    — 虚函数，每次都执行          │  │
│  │  ④ TestMethod()                 — 实际测试逻辑               │  │
│  │  ⑤ 执行 Latent Commands（如有）                              │  │
│  │  ⑥ TearDown() / AFTER_EACH()  — 虚函数，即使断言失败也执行   │  │
│  │  ⑦ 销毁 Derived 实例                                        │  │
│  │                                                              │  │
│  └──────────────────────────────────────────────────────────────┘  │
│                                                                    │
│  ⑧ AFTER_ALL()   — 静态方法，所有方法完成后执行一次               │
│      通过 AfterAllDelegate 挂到 FAutomationTestFramework          │
│      适合：卸载关卡、释放重资源                                    │
│                                                                    │
└────────────────────────────────────────────────────────────────────┘
```

### 2.4 断言策略：`[[nodiscard]]` bool + ASSERT_THAT

CQTest 不依赖 C++ 异常（因为并非所有 UE 平台支持异常），而是采用 `[[nodiscard]] bool` 返回值策略：

```cpp
// FNoDiscardAsserter 中的典型断言方法：
[[nodiscard]] bool IsTrue(bool Condition);
[[nodiscard]] bool AreEqual(const TExpected& Expected, const TActual& Actual);
[[nodiscard]] bool IsNull(const T& Ptr);
[[nodiscard]] bool IsNear(TExpected Expected, TActual Actual, TEpsilon Epsilon);
```

`[[nodiscard]]` 属性确保编译器在调用者忽略返回值时产生警告。`ASSERT_THAT` 宏在断言失败时执行 `return`，实现短路效果：

```cpp
// ASSERT_THAT 伪展开：
#define ASSERT_THAT(Expr)  if (!(Expr)) { return; }
```

这个设计在三种方案中取得平衡：

| 方案 | 优点 | 缺点 |
| --- | --- | --- |
| 抛异常 | 在 helper / lambda 中也能中断 | 部分平台不支持 |
| 普通 bool | 噪音少，IDE 支持好 | 依赖人工检查，容易遗漏 |
| **`[[nodiscard]]` bool + ASSERT_THAT** | 编译器强制检查 + 宏自动 return | CQTest 默认选择 ✓ |

### 2.5 组合优于继承 — 组件化设计

CQTest 框架的扩展哲学是**组合优于继承**（Composition over Inheritance）。扩展功能通过可组合的 Component 注入，而非深层继承链：

| 组件 | 功能 |
| --- | --- |
| `ActorTestSpawner` | 创建最小 UWorld 并管理 Actor 生命周期 |
| `MapTestSpawner` | 创建临时 Map 或打开指定关卡 |
| `PIENetworkComponent` | 创建 Server + Client PIE 实例，用于 Replication 测试 |
| `InputTestActions` | 向 Pawn 注入 InputAction |
| `CQTestSlateComponent` | UI 更新通知 |
| `CQTestAssetHelper` | 资产搜索与 Blueprint 加载 |

自定义扩展可以通过两种方式：
1. **自定义基类**：`TEST_CLASS_WITH_BASE(Name, "Path", TCustomBase)` — 继承自 `TTest<Derived, AsserterType>` 的自定义模板类。
2. **自定义断言器**：`TEST_CLASS_WITH_ASSERTS(Name, "Path", FCustomAsserter)` — 继承自 `FNoDiscardAsserter` 并扩展断言方法。

### 2.6 延迟动作系统（Latent Actions）

CQTest 内建对异步 / 多帧测试的支持，通过 `FTestCommandBuilder` 流式接口或直接 `AddCommand`：

```cpp
TEST_METHOD(AsyncFlowTest)
{
    TestCommandBuilder
        .Do([&]() { StartOperation(); })                          // FExecute: 执行一次
        .Until([&]() { return IsOperationDone(); })               // FWaitUntil: 多帧等待
        .Then([&]() { ASSERT_THAT(IsTrue(GetResult())); })        // 完成后断言
        .OnTearDown([&]() { Cleanup(); });                        // LIFO 顺序清理
}
```

| 延迟动作 | 说明 |
| --- | --- |
| `FExecute` | 执行一次 |
| `FWaitUntil` | 多帧轮询直到条件满足或超时 |
| `FWaitDelay` | 等待固定时长（慎用，易引入 flaky） |
| `FRunSequence` | 保序执行一组延迟动作 |
| `TAsyncExecute` | 启动异步操作，等待 `TAsyncResult` 完成 |

框架保证所有步骤**按声明顺序执行**，且断言失败后**不再执行后续延迟动作**（但 `AFTER_EACH` 仍会执行）。

---

## 3. CQTest 的优势

### 3.1 与传统测试方案的对比

CQTest 官方 README 的 "Why CQTest?" 部分明确对比了三种 UE 测试方式：

#### (a) 传统宏 `IMPLEMENT_SIMPLE_AUTOMATION_TEST`

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMinimalTest, "Game.Test",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMinimalTest::RunTest(const FString& Parameters)
{
    TestTrue(TEXT("True should be true"), true);
    return true;
}
```

**问题**：
- **一个宏 = 一个类 = 一个函数**：N 个测试用例需要 N 个宏声明 + N 个独立类 + N 个 `RunTest` 实现。
- **无 Setup/TearDown**：测试间的初始化和清理需手工管理。
- **状态不自动重置**：跨测试共享数据容易相互污染。
- **bool 返回值**：断言失败后仍继续执行，容易产生级联错误。

#### (b) Spec 框架 `DEFINE_SPEC`（BDD 风格）

```cpp
BEGIN_DEFINE_SPEC(FMinimalTest, "Game.Test",
    EAutomationTestFlags::ProductFilter | EAutomationTestFlags_ApplicationContextMask)
uint32 SomeValue = 3;
END_DEFINE_SPEC

void FMinimalTest::Define()
{
    Describe("Assertions", [this]() {
        It("Should pass", [this]() {
            TestEqual(TEXT("Value"), SomeValue, 3);
        });
    });
}
```

**问题**：
- **Lambda 捕获陷阱**：`Describe`/`It` 内部的局部变量在 lambda 执行时可能已离开作用域，导致悬空引用。
- **状态不自动重置**：成员变量在多个 `It` 之间共享，修改会累积。
- **调试困难**：嵌套 lambda 使得断点和调用栈不够直观。

#### (c) CQTest（本方案）

```cpp
TEST_CLASS(MinimalFixture, "Game.Test")
{
    uint32 SomeNumber = 0;
    BEFORE_EACH() { SomeNumber++; }

    TEST_METHOD(CanAccessMembers)
    {
        ASSERT_THAT(AreEqual(1, SomeNumber));  // 每次都是 1
    }
};
```

**优势总结**：

| 维度 | `IMPLEMENT_SIMPLE_AUTOMATION_TEST` | `DEFINE_SPEC` | **CQTest** |
| --- | --- | --- | --- |
| 一个类多测试 | ✗ 每测试一个类 | ✓ 但是嵌套 lambda | **✓ 清晰的 TEST_METHOD** |
| 自动状态重置 | ✗ | ✗ | **✓ 每 TEST_METHOD 新实例** |
| Setup / TearDown | ✗ | BeforeEach/AfterEach | **✓ BEFORE_ALL/EACH + AFTER_ALL/EACH** |
| 断言短路 | ✗ 继续执行 | ✗ 继续执行 | **✓ ASSERT_THAT 立即 return** |
| 编译器安全 | 无 | 无 | **✓ `[[nodiscard]]` 警告** |
| 延迟动作 | 手工管理 | 手工管理 | **✓ TestCommandBuilder 流式 API** |
| Lambda 安全 | N/A | ✗ 捕获陷阱 | **✓ 无 lambda 状态问题** |
| 调试体验 | 一般 | 差（嵌套 lambda） | **✓ 函数断点直接命中** |

### 3.2 核心优势详解

**① Make Easy Things Easy — 让简单的事情简单**

CQTest 的设计原则（引用自官方 README）。最简测试只需一行：

```cpp
TEST(MinimalTest, "Game.MyGame") { ASSERT_THAT(IsTrue(true)); }
```

**② 自动状态重置 — 测试原子性保证**

每个 `TEST_METHOD` 都创建全新的测试类实例。成员变量自动回到声明时的初始值，无需在 `TearDown` 中手工重置。这是 CQTest 最核心的设计决策。

**③ 丰富的断言 API**

`FNoDiscardAsserter` 提供：`IsTrue` / `IsFalse` / `IsNull` / `IsNotNull` / `AreEqual` / `AreNotEqual` / `AreEqualIgnoreCase` / `IsNear` / `Fail` / `ExpectError` / `ExpectErrorRegex`。支持自定义类型（需定义 `operator==` 和 `ToString`）。

**④ 自动测试目录生成**

`GenerateTestDirectory` 占位符可根据文件路径自动生成 Automation 面板中的层级目录，消除手工维护路径字符串的负担。

**⑤ 标签过滤**

`TEST_CLASS_WITH_TAGS` / `TEST_METHOD_WITH_TAGS` 支持 `[TagA][TagB]` 格式标签，可在 Automation 面板或命令行中按标签过滤测试子集。

**⑥ 可扩展架构**

通过自定义 Asserter（`TEST_CLASS_WITH_ASSERTS`）和自定义基类（`TEST_CLASS_WITH_BASE`）两个扩展点，框架可以适配不同项目的特殊需求，而无需修改核心代码。

---

| 风格 | 匹配数 | 说明 |
| --- | --- | --- |
| `TEST_CLASS_WITH_FLAGS`（CQTest） | ~187 | 新增测试的首选方式 |
| `IMPLEMENT_SIMPLE_AUTOMATION_TEST` | ~330 | 遗留测试，部分场景仍有使用 |

> 新增绑定覆盖率测试、语法测试、预处理器测试等均优先采用 CQTest 风格。

## 5. CQTest 完整宏清单

| 宏 | 说明 |
| --- | --- |
| `TEST(Name, Dir)` | 最简单的单测试对象 |
| `TEST_WITH_TAGS(Name, Dir, Tags)` | 带标签的单测试对象 |
| `TEST_CLASS(Name, Dir)` | 测试类：支持 Setup/TearDown、共享状态、分组 |
| `TEST_CLASS_WITH_TAGS(Name, Dir, Tags)` | 带标签的测试类 |
| `TEST_CLASS_WITH_ASSERTS(Name, Dir, Asserter)` | 使用自定义断言器的测试类 |
| `TEST_CLASS_WITH_ASSERTS_AND_TAGS(Name, Dir, Asserter, Tags)` | 自定义断言器 + 标签 |
| `TEST_CLASS_WITH_BASE(Name, Dir, Base)` | 继承自定义基类的测试类 |
| `TEST_CLASS_WITH_BASE_AND_TAGS(Name, Dir, Base, Tags)` | 自定义基类 + 标签 |
| `TEST_CLASS_WITH_FLAGS(Name, Dir, Flags)` | 指定 Automation 标志的测试类 |
| `TEST_CLASS_WITH_FLAGS_AND_TAGS(Name, Dir, Flags, Tags)` | Automation 标志 + 标签 |
| `TEST_CLASS_WITH_BASE_AND_FLAGS(Name, Dir, Base, Flags)` | 自定义基类 + 标志 |
| `TEST_CLASS_WITH_BASE_AND_FLAGS_AND_TAGS(Name, Dir, Base, Flags, Tags)` | 全参数版本 |
| `_TEST_CLASS_IMPL_EXT` / `_TEST_CLASS_IMPL` | 底层实现宏 |

生命周期与断言宏：

| 宏 | 说明 |
| --- | --- |
| `BEFORE_ALL()` | 静态方法，整个类仅执行一次 |
| `AFTER_ALL()` | 静态方法，所有方法完成后执行一次 |
| `BEFORE_EACH()` | 虚函数 `Setup()`，每个 TEST_METHOD 前 |
| `AFTER_EACH()` | 虚函数 `TearDown()`，每个 TEST_METHOD 后 |
| `TEST_METHOD(Name)` | 声明一个独立测试用例（void 返回） |
| `TEST_METHOD_WITH_TAGS(Name, Tags)` | 带标签的测试用例 |
| `ASSERT_THAT(Expr)` | 短路断言：失败立即 return |

示例：

```cpp
TEST_CLASS_WITH_FLAGS(ClassName, "Automation.Path.Prefix", Flags)
{
    BEFORE_ALL()  { /* 静态，整个类执行一次 */ }
    AFTER_ALL()   { /* 静态，所有方法完成后执行一次 */ }
    BEFORE_EACH() { /* 虚函数，每个 TEST_METHOD 前 */ }
    AFTER_EACH()  { /* 虚函数，每个 TEST_METHOD 后 */ }

    TEST_METHOD(MethodName)
    {
        ASSERT_THAT(IsTrue(condition));  // 失败立即 return
        TestRunner->TestEqual(TEXT("label"), actual, expected);
    }
};
```

**注意**：`TestRunner` 是 `TTestRunner<FNoDiscardAsserter>*` 类型的静态指针。需要 `FAutomationTestBase&` 的地方要解引用：传 `*TestRunner` 而非 `TestRunner`。

## 6. Angelscript 引擎生命周期适配

传统 `ASTEST_BEGIN/END_*` 宏展开为 RAII 大括号对，无法跨 CQTest 的 `Setup()` / `TearDown()` / `TEST_METHOD()` 边界拆分。项目采用以下适配方案：

```text
┌─────────────────────────────────────────────────────────────────┐
│  BEFORE_ALL   →  ASTEST_CREATE_ENGINE_SHARE_CLEAN()            │
│                  一次性获取干净的共享克隆引擎                    │
├─────────────────────────────────────────────────────────────────┤
│  TEST_METHOD  →  ASTEST_CREATE_ENGINE_SHARE()  获取引擎引用     │
│                  FAngelscriptEngineScope        局部 RAII        │
│                  FCoverageModuleScope           模块 RAII        │
│                  （析构自动 DiscardModule，保证测试间隔离）      │
├─────────────────────────────────────────────────────────────────┤
│  AFTER_ALL    →  ResetSharedCloneEngine(Engine)                │
│                  一次性清理，恢复全局引擎状态                    │
└─────────────────────────────────────────────────────────────────┘
```

`FCoverageModuleScope` 在析构时自动调用 `Engine.DiscardModule()`，保证每个 `TEST_METHOD` 的模块隔离，无需全量引擎重置。

## 7. 标准 CQTest 测试文件结构

```cpp
#include "CQTest.h"
#include "Shared/AngelscriptTestMacros.h"
#include "Shared/AngelscriptBindingsCoverage.h"
#include "Shared/AngelscriptBindingsModuleBuilder.h"
#include "Shared/AngelscriptBindingsAssertions.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace AngelscriptTestSupport;
using namespace AngelscriptTestBindings;
using namespace AngelscriptReflectiveAccess;

// 1. 声明覆盖率 Profile
static const FBindingsCoverageProfile GProfile{
    TEXT("Theme"),           // Theme
    TEXT("Variant"),         // Variant（无变体填空串）
    TEXT("ASModPrefix"),     // ModulePrefix
    TEXT("CasePrefix"),      // CasePrefix
    TEXT("LogCategory"),     // LogCategory
};

// 2. 测试类
TEST_CLASS_WITH_FLAGS(FMyTest,
    "Angelscript.TestModule.Theme.Tests",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
{
    BEFORE_ALL() { ASTEST_CREATE_ENGINE_SHARE_CLEAN(); }
    AFTER_ALL()  {
        FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE();
        AngelscriptTestSupport::ResetSharedCloneEngine(Engine);
    }

    TEST_METHOD(CaseName)
    {
        FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE();
        FAngelscriptEngineScope Scope(Engine);

        FCoverageModuleScope Mod(*TestRunner, Engine, GProfile, TEXT("Section"), TEXT(R"(
            int Foo() { return 42; }
        )"));
        if (!Mod.IsValid()) return;
        auto& M = Mod.GetModule();

        ExpectGlobalInt(*TestRunner, Engine, M, GProfile,
            TEXT("int Foo()"), TEXT("returns 42"), 42);
    }
};

#endif
```

## 8. 常用断言 Helpers

| Helper | 用途 |
| --- | --- |
| `ExpectGlobalInt(Test, Engine, Module, Profile, Decl, Label, Expected)` | 调用无参 `int F()` 函数，比较返回值 |
| `ExpectGlobalInts(Test, Engine, Module, Profile, Cases[])` | 批量 `FExpectedGlobalInt` 数组断言 |
| `ExpectGlobalReturnCustom<T>(Test, Engine, Module, Profile, Decl, Label, Validator)` | 自定义返回类型验证（FString、struct 等） |
| `ExecuteFunctionExpectingScriptException(Test, Engine, Module, Profile, Decl, Label, Substr)` | 负面路径：期望 AS 抛出异常 |
| `FASGlobalFunctionInvoker` | 通用函数调用器，支持 `AddArg` / `AddArgRef` / `CallAndReturn<T>` |

## 9. 常见陷阱

### 9.1 TestRunner 解引用

```cpp
// ✗ 编译错误或类型不匹配
ExpectGlobalInt(TestRunner, Engine, M, Profile, ...);

// ✓ 正确：解引用静态指针
ExpectGlobalInt(*TestRunner, Engine, M, Profile, ...);
```

### 9.2 异常测试的 AddExpectedError

AS 异常处理以 `Error` 级别写日志。若不预注册，CQTest 会将其视为测试失败：

```cpp
// 在 FCoverageModuleScope 之前注册
TestRunner->AddExpectedErrorPlain(
    TEXT("模块名"),
    EAutomationExpectedErrorFlags::Contains, 0);
TestRunner->AddExpectedErrorPlain(
    TEXT("void CrashFunction()"),
    EAutomationExpectedErrorFlags::Contains, 0);
TestRunner->AddExpectedErrorPlain(
    TEXT("预期异常消息子串"),
    EAutomationExpectedErrorFlags::Contains, 0);
```

### 9.3 ASTEST_BEGIN/END 不可跨 CQTest 方法

`ASTEST_BEGIN_*` / `ASTEST_END_*` 展开为大括号 + RAII，**不能**拆分到 `BEFORE_EACH` 和 `TEST_METHOD` 中。在 CQTest 中应直接使用 `ASTEST_CREATE_ENGINE_SHARE()` + `FAngelscriptEngineScope` + `FCoverageModuleScope` 组合。

### 9.4 FCoverageModuleScope 的 IsValid 检查

编译失败时 `Mod.IsValid()` 返回 `false`。CQTest 的 `TEST_METHOD` 是 `void` 返回，直接 `return;` 即可（不需要 `return false`）：

```cpp
if (!Mod.IsValid()) return;      // CQTest: void
if (!Mod.IsValid()) return false; // 传统 IMPLEMENT_SIMPLE: bool
```

## 10. 参考文件

| 文件 | 说明 |
| --- | --- |
| `Template/Template_CQTest.cpp` | 6 个 TEST_METHOD 的教学模板 |
| `Shared/AngelscriptTestMacros.h` | 引擎创建 / 生命周期宏定义 |
| `Shared/AngelscriptBindingsCoverage.h` | `FBindingsCoverageProfile` 结构体 |
| `Shared/AngelscriptBindingsModuleBuilder.h` | `FCoverageModuleScope` RAII |
| `Shared/AngelscriptBindingsAssertions.h` | `ExpectGlobalInt` 等断言 |
| `Shared/AngelscriptGlobalFunctionInvoker.h` | `FASGlobalFunctionInvoker` |
| `TESTING_GUIDE.md` | 测试编写完整指南 |
| `Engine/Source/Developer/CQTest/README.md` | UE 官方 CQTest README（含 "Why CQTest?" 设计动机） |
| `Engine/Source/Developer/CQTest/Public/CQTest.h` | 框架核心头文件（TBaseTest / TTest / TTestRunner） |
| `Engine/Source/Developer/CQTest/Public/Assert/NoDiscardAsserter.h` | 默认断言器 `FNoDiscardAsserter` |
