# C++ UInterface 绑定缺口修复计划

## 背景与目标

### 背景

当前 Angelscript 插件已实现一套完整的**脚本定义接口**管线（**36 个测试**覆盖：Interface/ 目录 33 个 + Preprocessor 2 个 + Compiler 1 个），包括：

- 预处理器识别 `interface` chunk → 方法提取 → 块擦除 → `RegisterObjectType` + `RegisterObjectMethod(CallInterfaceMethod)`
- 类生成器创建接口 `UClass`（`CLASS_Interface | CLASS_Abstract`）+ 极简 UFunction 存根
- 运行时 `Cast<I>` → `ImplementsInterface` → `CallInterfaceMethod`（行 56-68）→ `FindFunction` → `InvokeReflectiveUFunctionFromGenericCall`

**但脚本类无法自动绑定 C++ 定义的 `UINTERFACE` 方法**。当前测试侧有手动绑定的 Native PoC 用例（`EnsureNativeInterfaceFixturesBound` 硬编码注册特定接口），分布在 **3 个测试文件**中，但插件绑定层没有通用的自动识别与注册机制。

### 现状诊断摘要

经全面审查（2026-04-23），发现四个层面的缺口：

| # | 缺口 | 影响 |
|---|------|------|
| 1 | **C++ UInterface 方法未自动注册** | `Bind_BlueprintType.cpp` 的 `TFieldIterator<UFunction>(Class, ExcludeSuper)` 不遍历接口方法；`BlueprintCallableReflectiveFallback` 显式拒绝 `CLASS_Interface`；UHT 对接口类生成 `ERASE_NO_FUNCTION()` 空指针 |
| 2 | **FInterfaceProperty 未参与绑定** | `TScriptInterface<T>` 属性在脚本中不可读写 |
| 3 | **方法签名校验仅按函数名** | `FInterfaceMethodSignature` 只有 `FName`，参数类型/数量不校验 |
| 4 | **架构决策未落地** | `Plan_InterfaceBinding.md` Phase 0 的三个决策点均未记录结论 |

### ThirdParty 现状校准

> **注意**：Plan 原始版本声称"当前插件对 AngelScript ThirdParty 零修改"是**不准确的**。
>
> 实际上 `CanCastScriptObjectToUnrealInterface` 已被注入到 AS 引擎核心的 **3 处关键路径**：
> - `as_scriptengine.cpp:5081` — 隐式/向上转型
> - `as_scriptengine.cpp:5094` — 显式转型（真实类型检查）
> - `as_context.cpp:3296` — 运行时 CAST 指令
>
> 这意味着 ThirdParty 已有与接口相关的 UE 特化修改。Phase 0 的决策应基于"已有少量修改"而非"零修改"来评估增量成本。

### 与现有 Plan 的关系

`Plan_InterfaceBinding.md` 是接口绑定完善的**完整设计文档**，包含架构决策点分析、Patch 方案对比、横向参考、7 个 Phase 定义。本文档是基于现状诊断的**聚焦执行计划**，目标是以最小变更路径补齐"C++ UInterface 脚本可用"这一核心缺口，并在过程中落实必要的架构决策。

### 目标

1. **C++ UInterface 自动绑定**：`Bind_BlueprintType.cpp` 的绑定流程自动识别 C++ 接口类并注册方法到 AS，脚本能 Cast、调方法、引用 StaticClass
2. **落实架构决策**：确定命名约定，记录到 `Plan_InterfaceBinding.md` 的"已确定决策"章节
3. **建立回归基线**：≥12 个新增自动化测试覆盖 C++ 接口自动绑定场景

FInterfaceProperty 和方法签名校验增强作为后续阶段，本计划仅定义前置边界。

## 当前事实状态

```text
C++ UInterface 绑定链路（应有 vs 实有）：

Bind_BlueprintType.cpp
  Bind_Defaults (EOrder::Late+100)
    ├── TFieldIterator<UFunction>(Class, ExcludeSuper) — 不遍历接口方法   ← 核心缺口
    ├── BindBlueprintCallable → ClassFuncMaps → OwningClass 是接口类      ← 查找失败
    └── BlueprintCallableReflectiveFallback → RejectedInterfaceClass      ← 显式拒绝

Bind_UObject.cpp
  opCast — 已有 CLASS_Interface + ImplementsInterface 分支               ✓ 可用
  ImplementsInterface() 绑定                                             ✓ 可用

AngelscriptEngine.cpp
  CanCastScriptObjectToUnrealInterface — 已注入 AS 引擎 3 处             ✓ 可用

ClassGenerator/AngelscriptClassGenerator.cpp
  CallInterfaceMethod（行 56-68）— 泛型回调                              ✓ 可用
  ResolveInterfaceClass — 三级回退查找（行 5335-5382）                    ✓ 可用
  AddInterfaceRecursive — 递归接口添加（行 5386-5413）                    ✓ 可用

测试 Fixture（Shared/AngelscriptNativeInterfaceTestTypes.h）：
  UAngelscriptNativeParentInterface — 3 个方法（GetNativeValue/SetNativeMarker/AdjustNativeValue）
  UAngelscriptNativeChildInterface — 1 个额外方法（GetChildValue），继承 Parent
  ATestNativeParentInterfaceActor — C++ 实现类，用于跨语言桥接测试

手动绑定代码（需清理的 3 个文件）：
  AngelscriptInterfaceNativeTests.cpp            — EnsureNativeInterfaceFixturesBound
  AngelscriptInterfaceNativeBridgeTests.cpp      — 独立副本
  AngelscriptInterfaceNativeInheritedChildSurfaceTests.cpp — 独立副本（Child 绑全部 4 方法）

AS_USE_BIND_DB = (!WITH_EDITOR)
  Editor 构建：走非 BindDB 路径（TObjectRange<UClass> 遍历）    ← 开发时路径
  Shipping 构建：走 BindDB 路径（预生成数据库）                  ← 打包路径
```

## 已确定决策

### 决策 1：ThirdParty 修改策略 → **方案 A+（不新增 ThirdParty 修改）**

- ThirdParty 已有 `CanCastScriptObjectToUnrealInterface` 注入（3 处），接口 cast 运行时已可工作
- 继续使用 `RegisterObjectType`（引用类型模拟）注册 C++ 接口，不额外修改 `as_objecttype.cpp`
- 增量成本为零，与 AS 2.38 升级无冲突

### 决策 2：命名约定 → **仅 U 前缀**

- 与当前脚本定义接口一致（`UIDamageable`）
- 脚本中写 `Cast<UAngelscriptNativeParentInterface>(Obj)` 
- 不做双重注册（U+I），保持简单

### 决策 3：FInterfaceProperty 优先级 → **先跳过**

- 本计划只做 C++ 接口的 Cast + 方法调用
- `TScriptInterface<T>` 属性支持由 `Plan_InterfaceBinding.md` Phase 4 后续承接

### 决策 4：BindDB 双路径策略 → **先做 Editor 路径**

- `AS_USE_BIND_DB = (!WITH_EDITOR)`，Editor 构建走非 DB 路径
- Phase 1-2 的实现先覆盖 Editor（非 DB）路径
- BindDB 路径在 Phase 3 回归中验证是否需要额外处理

## 分阶段执行计划

### Phase 1：C++ UInterface 自动类型注册 + 方法注册

> 目标：在 `Bind_BlueprintType.cpp` 的 `Bind_Defaults` 绑定阶段，自动识别 C++ 接口类，注册为 AS 类型并绑定所有 BlueprintCallable 方法。

- [ ] **P1.1** 在 `Bind_BlueprintType.cpp` 的 `Bind_Defaults` 后追加 C++ 接口的自动注册
  - 当前 `ShouldBindEngineType` 对标记了 `BlueprintType` 的接口类会放行类型注册，但不会注册接口方法（因为 `TFieldIterator<UFunction>(Class, ExcludeSuper)` 不遍历接口函数，且 `BlueprintCallableReflectiveFallback` 显式拒绝 `CLASS_Interface`）
  - 在 `Bind_Defaults` 绑定闭包的末尾，新增一个 C++ 接口方法注册循环：遍历所有已注册的 AS 类型对应的 `UClass`，对 `HasAnyClassFlags(CLASS_Interface) && Class != UInterface::StaticClass()` 的类自动扫描并注册接口方法
  - 方法注册复用已有的 `CallInterfaceMethod` 泛型回调（`AngelscriptClassGenerator.cpp` 行 56-68）+ `FInterfaceMethodSignature` + `RegisterObjectMethod`
  - 方法声明字符串从 `UFunction` 的 `FProperty` 链自动生成，复用 `FAngelscriptFunctionSignature` 或 `FAngelscriptTypeUsage::FromProperty` 的类型映射能力
  - 跳过 `GetOuter() == UInterface::StaticClass()` 的基础方法
  - 对于标有 `BlueprintType` 但接口方法 AS 类型不完整（如参数类型在 AS 中未注册）的方法，静默跳过而非报错
- [ ] **P1.1** 📦 Git 提交：`[Interface] Feat: auto-register C++ UInterface methods in Bind_Defaults`

- [ ] **P1.2** 实现接口继承链的方法链接
  - C++ 接口可以有继承关系（如 `UAngelscriptNativeChildInterface : UAngelscriptNativeParentInterface`）。子接口需要能访问父接口的方法
  - 在 P1.1 的注册循环中使用两轮遍历：第一轮注册所有接口的自身方法，第二轮沿 `GetSuperClass()` 递归，用 `CopySystemType` 或逐方法注册将父接口方法链接到子接口
  - 参考 `NativeInheritedChildSurfaceTests.cpp` 中的手动绑定策略：Child 需要绑定全部 4 个方法（自身 1 个 + 继承的 3 个）
- [ ] **P1.2** 📦 Git 提交：`[Interface] Feat: link C++ interface inheritance chain for method visibility`

### Phase 2：手动绑定清理 + 回归

> 目标：移除测试中的手动绑定代码，验证自动绑定路径覆盖所有现有场景。

- [ ] **P2.1** 移除 3 个测试文件中的手动绑定代码
  - `AngelscriptInterfaceNativeTests.cpp`：移除 `EnsureNativeInterfaceFixturesBound()` 及其调用（`TestCallInterfaceMethod`、`BindNativeInterfaceMethod`、`EnsureNativeInterfaceBoundForTests`）。5 个测试中的 `EnsureNativeInterfaceFixturesBound()` 调用全部删除
  - `AngelscriptInterfaceNativeBridgeTests.cpp`：移除同名函数的独立副本（行 17-81 的整个 namespace 内的绑定辅助代码）。1 个测试中的调用删除
  - `AngelscriptInterfaceNativeInheritedChildSurfaceTests.cpp`：移除独立副本（行 17-93）。注意此文件的 Child 绑定策略不同（绑全部 4 方法），自动绑定必须覆盖此行为
  - 验证全部 7 个 Native 相关测试仍通过（NativeImplement 5 + NativeBridge 1 + NativeInherited 1）
- [ ] **P2.1** 📦 Git 提交：`[Interface] Refactor: remove manual native interface binding from 3 test files`

- [ ] **P2.2** 回归全部接口测试
  - 运行 `Angelscript.TestModule.Interface.*`（现有 33 个）
  - 运行 `Angelscript.TestModule.Preprocessor.Interface.*`（2 个）
  - 运行 `Angelscript.TestModule.Compiler.Interface.*`（1 个）
  - 确认无回归：脚本定义接口的 Declare/Implement/Cast/Advanced/Lifecycle/Validation 全部不受影响
- [ ] **P2.2** 📦 Git 提交：`[Interface] Test: verify all 36 interface tests pass with auto-binding`

### Phase 3：新增 C++ 接口自动绑定测试

> 目标：建立针对自动绑定路径的专项测试，覆盖 Phase 1 现有测试未触及的场景。

- [ ] **P3.1** 扩展 `Shared/AngelscriptNativeInterfaceTestTypes.h` 测试 fixture
  - 当前已有 `UAngelscriptNativeParentInterface`（3 个方法）和 `UAngelscriptNativeChildInterface`（1 个额外方法）
  - 新增 `UAngelscriptNativeSecondaryInterface`（1-2 个方法，不继承前者），用于测试"同时实现多个 C++ 接口"场景
- [ ] **P3.1** 📦 Git 提交：`[Test/Interface] Feat: add secondary C++ UInterface test fixture`

- [ ] **P3.2** 创建 `Interface/AngelscriptCppInterfaceAutoBindTests.cpp`，覆盖自动绑定专项用例
  - `CppInterfaceAutoBind.RegisterType` — C++ 接口在 AS 中作为类型可见（无手动绑定前提下）
  - `CppInterfaceAutoBind.MultipleNativeInterfaces` — 脚本类同时实现 `UAngelscriptNativeParentInterface` + `UAngelscriptNativeSecondaryInterface`
  - `CppInterfaceAutoBind.MixedInterfaces` — 脚本类同时实现 C++ 接口和脚本定义接口
- [ ] **P3.2** 📦 Git 提交：`[Test/Interface] Feat: add C++ UInterface auto-binding scenario tests`

- [ ] **P3.3** 回归全部测试（现有 36 + 新增），确认通过
- [ ] **P3.3** 📦 Git 提交：`[Test/Interface] Test: verify complete interface test suite`

### Phase 4：文档与后续路径

> 目标：更新相关文档，明确后续方向。

- [ ] **P4.1** 更新 `Plan_InterfaceBinding.md` 状态和已确定决策
- [ ] **P4.1** 📦 Git 提交：`[Interface] Docs: update Plan_InterfaceBinding status and decisions`

- [ ] **P4.2** 创建 `Documents/Knowledges/InterfaceBinding.md` 知识文档
  - 当前接口支持的完整范围：脚本接口（完整）+ C++ 接口自动绑定（本计划完成后）
  - 已知限制、架构决策、脚本语法示例
- [ ] **P4.2** 📦 Git 提交：`[Docs] Feat: add InterfaceBinding knowledge document`

## 验收标准

1. **C++ UInterface 自动绑定**：C++ 定义的 `UINTERFACE` 在 AS 脚本中自动可见，无需手动 `ReferenceClass`；能 Cast、调方法、引用 `StaticClass()`
2. **测试覆盖**：≥3 个新增 C++ 接口自动绑定测试通过，现有 36 个接口测试全部回归通过
3. **手动绑定移除**：3 个测试文件（`NativeTests`、`NativeBridgeTests`、`NativeInheritedChildSurfaceTests`）不再包含手动绑定代码
4. **架构决策文档化**：方案 A+、仅 U 前缀、FInterfaceProperty 先跳过有明确记录
5. **后续路径清晰**：FInterfaceProperty 和签名校验的优先级和启动条件已明确

## 风险与注意事项

### 风险 1：接口注册时序

`Bind_Defaults` 中接口类型注册和方法注册的顺序可能导致接口继承链中子接口先于父接口被遍历到。

**缓解**：使用两轮遍历——先注册所有接口自身方法，再链接继承。

### 风险 2：方法声明字符串生成

需要从 `UFunction` 的 `FProperty` 链自动生成 AS 方法声明字符串。现有的 `FAngelscriptFunctionSignature` 是为 `BindBlueprintCallable` 设计的，可能需要调整以支持接口方法的特殊需求（如不需要 `OwningClass` 查找）。

**缓解**：复用 `FAngelscriptTypeUsage::FromProperty` 做类型映射，手动拼接声明字符串（参考测试中 `BindNativeInterfaceMethod` 的模式）。

### 风险 3：已有 `BlueprintType` 接口的类型注册冲突

标记了 `UINTERFACE(BlueprintType)` 的接口类（如测试 fixture）会被 `ShouldBindEngineType` 放行并通过 `BindUClass` 注册为普通类型。Phase 1 的接口方法注册不应重复注册类型，只需在已注册的类型上追加方法。

**缓解**：P1.1 先检查接口类型是否已通过 `FAngelscriptType::GetByClass` 注册，已注册则只追加方法。

### 已知行为变化

1. **C++ 接口方法自动可见**：之前需要手动 `ReferenceClass` + 逐方法绑定，现在自动注册
   - 影响文件：`AngelscriptInterfaceNativeTests.cpp`、`AngelscriptInterfaceNativeBridgeTests.cpp`、`AngelscriptInterfaceNativeInheritedChildSurfaceTests.cpp`

## 依赖关系

```text
Phase 1（类型注册 + 方法注册 + 继承链接）
  ↓
Phase 2（手动绑定移除 + 回归）
  ↓
Phase 3（新增测试）
  ↓
Phase 4（文档）
```

## 参考文档索引

| 文档 | 用途 |
|------|------|
| `Documents/Plans/Plan_InterfaceBinding.md` | 完整接口绑定设计文档（含 Patch 方案对比、FInterfaceProperty、签名校验） |
| `Plugins/Angelscript/Source/AngelscriptTest/Interface/AngelscriptInterfaceNativeTests.cpp` | 现有手动绑定 PoC（Phase 2 清理对象之一） |
| `Plugins/Angelscript/Source/AngelscriptTest/Interface/AngelscriptInterfaceNativeBridgeTests.cpp` | 手动绑定独立副本（Phase 2 清理对象之二） |
| `Plugins/Angelscript/Source/AngelscriptTest/Interface/AngelscriptInterfaceNativeInheritedChildSurfaceTests.cpp` | 手动绑定独立副本（Phase 2 清理对象之三） |
| `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptNativeInterfaceTestTypes.h` | C++ 测试 UInterface fixture（Parent 3 方法 + Child 1 方法） |
| `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp` | 核心改造文件（Bind_Defaults 闭包） |
| `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` | CallInterfaceMethod 实现（行 56-68）、接口 UClass 创建、FinalizeClass 接口挂接 |
| `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp` | opCast 接口分支（已可用）、ImplementsInterface 绑定 |
| `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` | CanCastScriptObjectToUnrealInterface（行 112-138，已注入 AS ThirdParty） |
