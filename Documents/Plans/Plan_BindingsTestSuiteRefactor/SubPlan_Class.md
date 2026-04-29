# SubPlan: Class 测试改造

> 主 Plan：[`../Plan_BindingsTestSuiteRefactor.md`](../Plan_BindingsTestSuiteRefactor.md)  
> 共同规则与基座 API：[`README.md`](./README.md) + [`BaseAPI.md`](./BaseAPI.md)  
> 前置依赖：✅ 基座代码已就绪（`Bindings/Shared/` 5 文件已落地、金丝雀 `Bindings.SharedExample` 通过、Bindings 全量回归 134/134 绿）

## 目标文件与现状

- `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptClassBindingsTests.cpp`
- 文件大小：20.92 KB
- Automation ID 数：**7 个**
- 旧式 case 规模：**83 个 `if` 分支** / **46 个 `return N;`** / **20 个 `TestEqual+TestTrue`**（实地 grep 数据）
- 涉及 reflective 边界：UClass / TSubclassOf / TSoftClassPtr / 注解 ASClass / Native StaticClass / GeneratedStaticClass

### 现有 Automation ID 清单

| # | ID | 主题 |
|---|-----|------|
| 1 | `ClassLookupCompat` | UClass 查找（FindObject / LoadClass） |
| 2 | `TSubclassOfCompat` | TSubclassOf 基本能力 |
| 3 | `TSubclassOfRejectsUnrelatedClass` | TSubclassOf 类型安全：拒绝无关类（异常路径） |
| 4 | `TSoftClassPtrCompat` | TSoftClassPtr 软引用类指针 |
| 5 | `StaticClassCompat` | UClass::StaticClass() / 注解 ASClass StaticClass |
| 6 | `NativeStaticClassNamespace` | Native StaticClass 在命名空间访问 |
| 7 | `NativeStaticTypeGlobal` | Native StaticType 全局函数访问 |

## Section 切分方案

| Section | 包含的旧 ID | 主题 |
|---------|-------------|------|
| `RunClassLookupSection` | `ClassLookupCompat` | FindObject / LoadClass / IsChildOf 等 |
| `RunSubclassOfSection` | `TSubclassOfCompat` | TSubclassOf 构造 / 赋值 / Get / 比较 |
| `RunSubclassOfRejectSection` | `TSubclassOfRejectsUnrelatedClass` | 类型安全异常路径（脚本执行异常 + AddExpectedError） |
| `RunSoftClassPtrSection` | `TSoftClassPtrCompat` | TSoftClassPtr 构造 / TryLoad / IsValid |
| `RunStaticClassSection` | `StaticClassCompat` | C++ 类与注解 ASClass 的 StaticClass / Annotated AS class StaticClass |
| `RunNativeStaticAccessSection` | `NativeStaticClassNamespace` + `NativeStaticTypeGlobal` | Native StaticClass 在 namespace 与 global 函数下的访问形态 |

## Profile 定义

```cpp
const FBindingsCoverageProfile GClassProfile{
    TEXT("Class"), TEXT(""), TEXT("ASClass"),
    TEXT("Class"), TEXT("ClassBindings"),
};
```

## 分阶段执行计划

### Phase 0

- [ ] **P0.1** Dump 案例清单到 `Class_CaseInventory.md`
  - 标注每个 case 依赖的 UClass（如 `AAngelscriptActor` / `UTexture2D` / 注解类 `ARecreateAnnotatedActor` 等）
  - 标注 `TSubclassOfRejectsUnrelatedClass` 旧实现中的 `AddExpectedError` 文本与匹配模式
- [ ] **P0.1** 📦 Git 提交：`[Docs/Plans] Docs: dump class bindings case inventory baseline`

### Phase 1 — Section 实现

- [ ] **P1.1** 实现 `RunClassLookupSection`
  - 覆盖：`UClass` FindObject 命中已注册类、未命中返回 nullptr、IsChildOf 父子关系、StaticClass 名称比较、`UClass::GetSuperClass`
  - 用 `ExpectGlobalInt` 把"找到"/"未找到"/"is-child-of"等都映射为返回 0/1
- [ ] **P1.1** 📦 Git 提交：`[Tests/Bindings] Refactor: rebuild Class lookup section`

- [ ] **P1.2** 实现 `RunSubclassOfSection`
  - 覆盖：`TSubclassOf<AActor>` 默认构造 nullptr、从 UClass 构造、Get 返回 UClass、相等、转 UObject (`StaticClass()`)、`IsValid` 路径
- [ ] **P1.2** 📦 Git 提交：`[Tests/Bindings] Refactor: rebuild Class TSubclassOf section`

- [ ] **P1.3** 实现 `RunSubclassOfRejectSection`
  - 用 `ExecuteFunctionExpectingScriptException`
  - 脚本：`void Trigger() { TSubclassOf<AActor> Sub = UTexture2D::StaticClass(); }`（赋值无关类）
  - 期望异常子串：旧文件实际异常字符串，dump 时记录
  - 同时保留旧文件的 `AddExpectedError(TEXT("ASTSubclassOfRejectsUnrelatedClass"), ...)` 与 `AddExpectedError(TEXT("AssignClass"), ...)` 两条注册（迁移到新模块名）
- [ ] **P1.3** 📦 Git 提交：`[Tests/Bindings] Refactor: rebuild Class TSubclassOf rejection section`

- [ ] **P1.4** 实现 `RunSoftClassPtrSection`
  - 覆盖：默认构造 IsNull、从 UClass 构造、ToString、TryLoad、ResolveClass、`==`/`!=`
- [ ] **P1.4** 📦 Git 提交：`[Tests/Bindings] Refactor: rebuild Class TSoftClassPtr section`

- [ ] **P1.5** 实现 `RunStaticClassSection`
  - 覆盖：内置类 `AAngelscriptActor::StaticClass()`、注解 ASClass 编译后 `MyAnnotatedActor::StaticClass()`、生成类 GeneratedStaticClass 路径
  - 注解 ASClass 部分需要先 `CompileAnnotatedModuleFromMemory` 注入一个临时类（参考旧实现），然后查 StaticClass 返回非 null
- [ ] **P1.5** 📦 Git 提交：`[Tests/Bindings] Refactor: rebuild Class StaticClass section`

- [ ] **P1.6** 实现 `RunNativeStaticAccessSection`
  - 合并 `NativeStaticClassNamespace` 与 `NativeStaticTypeGlobal`
  - 覆盖：命名空间下的 `MyNamespace::MyClass::StaticClass()`、全局 `StaticType<T>()` 之类函数路径，确保两种访问形态都能 round-trip 到同一 UClass*
- [ ] **P1.6** 📦 Git 提交：`[Tests/Bindings] Refactor: rebuild Class native static access section`

### Phase 2 — 接线 + 验证

- [ ] **P2.1** 7 个 ID 接线，删除所有裸 `BuildModule` / `int Entry()` / return code
- [ ] **P2.1** 📦 Git 提交：`[Tests/Bindings] Refactor: wire Class automation IDs to coverage sections`

- [ ] **P2.2** 对位 `Class_CaseInventory.md` 打勾
- [ ] **P2.2** 📦 Git 提交：`[Docs/Plans] Docs: confirm Class case inventory coverage`

- [ ] **P2.3** 单文件 7 ID 回归全绿
- [ ] **P2.3** 📦 Git 提交：`[Tests/Bindings] Test: class subplan single-id regression green`

- [ ] **P2.4** Bindings 整体回归
- [ ] **P2.4** 📦 Git 提交：`[Tests/Bindings] Test: class subplan full bindings regression`

## 验收标准

1. `AngelscriptClassBindingsTests.cpp` 内 `grep "int Entry()"` = 0
2. `grep "return 1[0-9][0-9]"` = 0
3. `grep "BuildModule(.*\"AS"` = 0
4. 7 个原 Automation ID 全部保留且全绿
5. `TSubclassOfRejectsUnrelatedClass` 异常路径完整四元组（Prepare 成功 / Exception / 消息匹配 / 行号 > 0）
6. 注解 ASClass 类的 StaticClass 路径在新 Section 中可重复编译多次不冲突（依赖基座 `FCoverageModuleScope` 的 DiscardModule）

## 风险与注意事项

### 风险

1. **`CompileAnnotatedModuleFromMemory` 在 share-clean 引擎下的行为**：旧实现可能依赖特定引擎初始化时序。
   - **缓解**：`RunStaticClassSection` 内若发现注解类编译失败，先单独跑该 Section（不通过 `RunTest` 而通过临时 ID）确认基础可行，再合入。
2. **`ResolveClass` 路径在 EditorContext 之外可能不可用**：本测试本身就是 EditorContext，但要确认基座 `FCoverageModuleScope` 不会绕过 EditorContext 假设。

### 已知行为变化

1. 旧 `TSubclassOfRejectsUnrelatedClass` 的两条 `AddExpectedError` 注册（`ASTSubclassOfRejectsUnrelatedClass` + `AssignClass`）必须迁移到新模块名 `ASClass_SubclassOfReject`，否则 expected error 不匹配会被 framework 报错。
