# 容器 ToString / Format 支持计划

## 背景与目标

当前 `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TArray.cpp`（1831 行）只在 `Debugging` 路径下能展开数组元素，脚本侧无法将 `TArray<T>` 直接转成字符串：

- `Log("Arr = " + Arr)` 编译失败：`TArray<T>` 没有 `opAdd` / `Append`。
- `FString::Format("{0}", Arr)` 不可用：`FToStringHelper::Generic_AppendToString` 对未注册的 value 类型直接抛 `"Invalid type to append to string."`。
- `TMap` / `TSet` 同样缺失。

代码库已经存在一套统一的字符串化框架——`FToStringHelper`（`Helper_ToString.h`）+ `Bind_FString_Conversion`（`Bind_FString.cpp:1213`）。任何调用 `FToStringHelper::Register("FVector", ...)` 的类型，都会自动获得：

- `FString opAdd(const T) const` / `FString& opAddAssign(const T)` / `FString& Append(const T)`
- `FString ToString() const`
- `FString::Format("{0}", value)` 与 `FString::ApplyFormat(value, specifier)` 链路

但 `TArray` 是模板类型——`TArray<int>` 与 `TArray<FVector>` 是不同的实例化，**无法走标准 Register 路径**（按固定类型名注册）。元素类型只能在运行时通过 `asCObjectType* Meta → FArrayOperations` 拿到。

**目标**：

1. 让脚本侧的 `TArray<T>` 拥有自身的 `ToString()` 方法，并能在字符串拼接、`Log`、`FString::Format` 中直接使用，不需要手写循环。
2. 提供"范围/截断"参数，便于打印大数组时只输出头部若干元素。
3. 元素类型若已注册 ToString（基本类型、`FVector`、`FName` 等），输出该类型的友好表示；未注册时给出明确占位（`<unknown>`）而不是抛异常。
4. 同样能力扩展到 `TMap` / `TSet`（次优先）。

落地后的脚本使用形态参考其他语言：

| 语言 | 默认输出 | 范围/截断 |
|------|---------|----------|
| Python | `[1, 2, 3]` | `list[0:3]` 切片 |
| Rust | `[1, 2, 3]`（Debug） | `&arr[0..3]` 切片 |
| Kotlin | `[1, 2, 3]` | `joinToString(limit, truncated)` |
| JavaScript | `[1, 2, 3]` | `.slice(0, n)` |

设计形态（AS 脚本视角）：

```angelscript
TArray<int> Arr = {1, 2, 3};
FString S1 = Arr.ToString();              // "[1, 2, 3]"
FString S2 = "Arr = " + Arr;              // 直接参与字符串拼接
Log(FString::Format("Items: {0}", Arr));  // 走 FString::Format 链路

TArray<int> Big;  // 100 elements
FString S3 = Big.ToString(10);            // "[0, 1, 2, ..., 9, ...] (100 total)"
FString S4 = Big.ToString(5, 10);         // 范围 [5, 10): "[5, 6, 7, 8, 9]"

TArray<FVector> V; V.Add(FVector(1,2,3));
Log("V = " + V);                          // "V = [X=1.000 Y=2.000 Z=3.000]"
```

## 范围与边界

- **本次 Plan 范围**：`TArray` 的 ToString / 拼接 / Format 全链路，及与之配套的测试与文档。`TMap` / `TSet` 在 Phase 4 同步补齐，但只覆盖 ToString + 拼接，不展开 `ApplyFormat` 的全部 specifier。
- **不在范围**：
  - 修改 `FToStringHelper::Register` 接口本身（保持稳定）。
  - 新增对自定义脚本类型的 ToString 反射（仍由用户在脚本侧自己实现 `FString opImplConv() const` 或显式 `ToString()`）。
  - 修改 Debugger 的数组展示策略（仍走 `GetDebuggerScope`）。

## 当前事实状态

| 项目 | 现状 | 入口 |
|------|------|------|
| `TArray` 字符串化 | 无 `ToString` / 无 `opAdd` / 无 Format 接入 | `Bind_TArray.cpp:1381-1641`（方法绑定段） |
| `TMap` / `TSet` 字符串化 | 同样缺失 | `Bind_TMap.cpp` / `Bind_TSet.cpp` |
| 标准 ToString 注册 | 已为基础类型（int/float/bool）、`FVector`/`FRotator`/`FName`/`FText` 等注册 | `Bind_FString.cpp:1213`、`Bind_FVector.cpp:313`、`Bind_FName.cpp:182`、`Bind_Primitives.cpp:818-871` |
| 通用类型字符串化分发 | 已存在 `FToStringHelper::Generic_AppendToString(FString&, void*, int TypeId)` | `Bind_FString.cpp:420-586` |
| 数组元素遍历范例 | Debugger 已实现按 `ElementSize` 遍历 + 递归调用元素 ToString | `Bind_TArray.cpp:393-417`（`GetDebuggerScope`） |
| `FArrayOperations` 元数据 | `Ops->Type`（`FAngelscriptTypeUsage`，含 TypeId）、`Ops->NumBytesPerElement`、`Ops->Get(Arr, i)` 提供运行时元素信息 | `Bind_TArray_Functions.h`（449 行） |
| 现有测试覆盖 | 容器测试在 `AngelscriptTest/Containers/`，无 ToString 相关用例 | 需新增 |

## 设计要点

### TArray 侧 API 草案（绑定声明）

```cpp
TArray_.Method("FString ToString() const",                  &FArrayOperations::ToString);
TArray_.Method("FString ToString(int MaxElements) const",   &FArrayOperations::ToString_Limited);
TArray_.Method("FString ToString(int Start, int End) const",&FArrayOperations::ToString_Range);
TArray_.Method("FString opImplConv() const",                &FArrayOperations::ToString); // 字符串拼接走该路径
SCRIPT_NATIVE_TEMPLATED_CALL_NEEDSCOPY(...);
FAngelscriptBinds::PreviousBindPassScriptObjectTypeAsFirstParam();
```

- 拼接走 `opImplConv` 而非注册 `opAdd(const FString&)`：`FString` 已有 `opAdd(const ?&)` 的通用重载，会调用 `Generic_AppendToString`，但 `Generic_AppendToString` 对 value 类型只查 `FToStringHelper` 注册表——模板实例化不会命中。提供显式 `opImplConv()` 是最简、不破坏现有分发的办法。
- 同时为了兼容 `FString::Format("{0}", Arr)`：在 `Generic_AppendToString` 中**新增一条容器分支**，识别 `asTYPEID_TEMPLATE` 标记的对象类型，按数组协议展开。详见 P2.1。

### 输出格式约定

- 默认：`[elem0, elem1, elem2]`，分隔符 `", "`，前缀 `[`，后缀 `]`。
- 截断：`Arr.ToString(N)` 当 `Num > N` 时输出 `[elem0, ..., elemN-1, ...] (Total total)`；`Num <= N` 时与默认一致。
- 范围：`Arr.ToString(Start, End)` 输出 `[Arr[Start], ..., Arr[End-1]]`，越界 / 反序时返回 `"[]"` 并日志一条 `Verbose`，不抛异常（与现有 `Get` 越界容错一致）。
- 元素未注册 ToString：用 `<TypeName>` 或 `<unknown>` 占位，不抛异常（避免日志路径炸掉）。

### 与 `Bind_FString_Conversion` 的协同

- 不再为 TArray 走 `FToStringHelper::Register`（不可行）。
- 在 `FToStringHelper::Generic_AppendToString`（`Bind_FString.cpp:420-586`）的"未识别类型"分支前，**先**判断 `asCObjectType` 是否带 `asOBJ_TEMPLATE` 标志且能取到 `FArrayOperations`：
  - 是 → 复用 `FArrayOperations::AppendToString(Arr, Out)`，统一通过 Generic_AppendToString 递归处理元素。
  - 否 → 维持原 fallback 行为。
- 这样 `FString::Format("{0}", Arr)` 与 `Append(Arr)` 会自动接入，**无需**为模板实例化 Register。

## 分阶段执行计划

### Phase 1 — TArray 自身 ToString 能力

> 目标：`TArray<T>::ToString()` / `ToString(MaxElements)` / `ToString(Start, End)` 三种形态可在脚本中直接调用，输出符合上文约定。元素类型查找通过 `FToStringHelper` 已注册类型，未注册回落到 `<unknown>`。

- [ ] **P1.1** 在 `Bind_TArray_Functions.h` 中实现 `FArrayOperations::AppendToString`、`ToString` 三种形态
  - `AppendToString(const FScriptArray& Arr, FString& Out, int32 Start, int32 End, int32 Limit)` 是底层实现：负责前缀 `[`、按 `Ops->Get(Arr, i)` 循环、对每个元素调用 `FToStringHelper::Generic_AppendToString(Out, ElemPtr, Ops->Type.TypeId)`、分隔符 `", "`、截断省略号、后缀 `]`、可选的 `(N total)` 后缀
  - `ToString(const FScriptArray& Arr) → FString`：`AppendToString(Arr, Out, 0, Num, INT32_MAX)`
  - `ToString_Limited(const FScriptArray& Arr, int32 MaxElements) → FString`：`AppendToString(Arr, Out, 0, Num, MaxElements)`，截断时追加 `(N total)` 标尾
  - `ToString_Range(const FScriptArray& Arr, int32 Start, int32 End) → FString`：参数校验后 `AppendToString(Arr, Out, Start, End, INT32_MAX)`，越界/反序返回空数组字面量 `[]`
  - 元素 ToString 失败保护：在 `Generic_AppendToString` 抛异常前不容易拦截；改为**预检**——若 `FToStringHelper::HasRegistered(TypeId)` 为 false 且 TypeId 不属于 UObject/Enum/Delegate 这三种 Generic_AppendToString 已能处理的形态，则元素直接输出 `<TypeName>` 占位（占位 helper 复用 `FAngelscriptTypeUsage::GetCPPName` 或 `asGetTypeDeclaration`）
  - 空数组返回 `"[]"`
- [ ] **P1.1** 📦 Git 提交：`[Runtime/Binds] Feat: implement TArray ToString helpers in FArrayOperations`

- [ ] **P1.2** 在 `Bind_TArray.cpp` 的方法绑定段（约 `1381-1641` 行的 `Bind_TArray` lambda 内）注册三个 `ToString` 重载
  - 紧跟现有 `Add` / `Insert` 等方法的注册风格：`TArray_.Method("FString ToString() const", &FArrayOperations::ToString);` + `SCRIPT_NATIVE_TEMPLATED_CALL_NEEDSCOPY(TArray_, "FArrayOperations::ToString", false);` + `FAngelscriptBinds::PreviousBindPassScriptObjectTypeAsFirstParam();`
  - 三个重载分别对应 P1.1 实现的三个 C++ 函数；注意带参重载需要在 SCRIPT_NATIVE_TEMPLATED_CALL_NEEDSCOPY 标识中使用唯一字符串避免冲突
- [ ] **P1.2** 📦 Git 提交：`[Runtime/Binds] Feat: register TArray.ToString overloads`

- [ ] **P1.3** 注册 `FString opImplConv() const`，让 `"prefix" + Arr` 与 `Arr + "suffix"` 编译通过
  - AS 在字符串拼接时优先匹配现有 `FString::opAdd(const ?&)`，但 value 类型在 `Generic_AppendToString` 内查不到注册——因此需要为 `TArray` 自身提供 `opImplConv → FString` 的最短通路
  - 实现直接复用 `FArrayOperations::ToString`
  - 验证不会与现有 TArray 上其它隐式转换冲突（当前 `Bind_TArray.cpp` 没有任何 `opImplConv` 注册，安全）
- [ ] **P1.3** 📦 Git 提交：`[Runtime/Binds] Feat: register TArray opImplConv to FString`

- [ ] **P1.4** 新增 `AngelscriptTArrayToStringTests.cpp` 覆盖 P1 全部能力
  - 落点：`Plugins/Angelscript/Source/AngelscriptTest/Containers/AngelscriptTArrayToStringTests.cpp`，前缀沿用 `Angelscript.TestModule.Containers.*`
  - 用例清单：
    - `TArray_Int_ToString_Default`：`TArray<int>{1,2,3}.ToString() == "[1, 2, 3]"`
    - `TArray_Int_ToString_Empty`：空数组 → `"[]"`
    - `TArray_Int_ToString_Limited_Truncates`：100 元素 + `ToString(3)` → `"[0, 1, 2, ...] (100 total)"`
    - `TArray_Int_ToString_Limited_NoTruncation`：5 元素 + `ToString(10)` → 完整输出，不带 `(N total)`
    - `TArray_Int_ToString_Range`：`{0..9}.ToString(2, 5) == "[2, 3, 4]"`
    - `TArray_Int_ToString_Range_Invalid`：负数 / 反序 / 越界 → `"[]"`
    - `TArray_FVector_ToString`：`{FVector(1,2,3)}.ToString()` 包含 `"X="` `"Y="` `"Z="`（不硬编码精度）
    - `TArray_FString_ToString`：`{"Alice", "Bob"}.ToString() == "[Alice, Bob]"`
    - `TArray_OpImplConv_Concat`：`"V=" + Arr` 等价于 `"V=" + Arr.ToString()`
  - 测试形态参考 `AngelscriptTest/Containers/` 现有用例（脚本字符串内联编译 + Automation 断言）
- [ ] **P1.4** 📦 Git 提交：`[Tests/Containers] Test: cover TArray ToString variants`

### Phase 2 — 接入 FString::Format / Generic_AppendToString

> 目标：`FString::Format("{0}", Arr)`、`FString::ApplyFormat(Arr, "")`、`FString.Append(Arr)` 三条链路自动可用，覆盖任意 `TArray<T>` 实例化（不需要为每种实例化注册）。

- [ ] **P2.1** 在 `FToStringHelper::Generic_AppendToString` 中插入"模板对象 → 容器分支"
  - 位置：`Bind_FString.cpp:420-586` 中的 value 类型分支末尾、抛 `"Invalid type to append to string."` 异常之前
  - 取得 `asCObjectType*`（通过 `engine->GetObjectTypeById(TypeId)` 或现有上下文里已有的 `asITypeInfo*`），判断是否带 `asOBJ_TEMPLATE` 标志
  - 调用 `FArrayOperations::GetArrayOperations(Meta)`（或 TMap/TSet 同名 helper）取到容器 Ops，若返回非 nullptr 则视为已知容器
  - 调用 P1.1 实现的 `FArrayOperations::AppendToString(Arr, Out, 0, Num, INT32_MAX)`
  - **限制递归深度**：在 thread-local 计数器或带深度参数的 Generic_AppendToString 内部分支中，深度 > 4 时输出 `"[...]"` 截断，避免 `TArray<TArray<TArray<...>>>` 栈爆
- [ ] **P2.1** 📦 Git 提交：`[Runtime/Binds] Feat: route template containers through Generic_AppendToString`

- [ ] **P2.2** 验证 `FString::Format("{0}", Arr)` 与 `FString::ApplyFormat(Arr, "")` 的全链路
  - `FString::Format` 在 `Bind_FString.cpp:1294-1313`，本身只做位置替换并对每个参数调用 `Generic_AppendToString`——P2.1 完成后即自动生效，本步骤主要做行为验证
  - `ApplyFormat` 在 `Bind_FString.cpp:887-939`，目前对 value 类型也调 `Generic_AppendToString`，本步骤验证它对容器输入返回数组字符串本身（对齐 specifier 仍按字符串生效）
- [ ] **P2.2** 📦 Git 提交：`[Tests/Containers] Test: verify FString::Format works with TArray`（与 P2.3 测试用例合并提交）

- [ ] **P2.3** 扩展 `AngelscriptTArrayToStringTests.cpp` 覆盖 Format 链路
  - 用例：
    - `TArray_Format_Single`：`FString::Format("{0}", Arr) == "[1, 2, 3]"`
    - `TArray_Format_Mixed`：`FString::Format("{0} has {1} items", Arr, Arr.Num())` 拼出预期字符串
    - `TArray_Append_Method`：`FString S; S.Append(Arr);` 等价于 `S += Arr.ToString();`
    - `TArray_Nested_Truncation`：`TArray<TArray<int>>` 在深度 4 处出现 `"[...]"` 截断，且不抛异常
- [ ] **P2.3** 📦 Git 提交：`[Tests/Containers] Test: cover TArray FString::Format and Append`

### Phase 3 — 文档与示例

> 目标：能力对脚本作者可见，新人按文档即可上手；同步更新现有同类知识文档。

- [ ] **P3.1** 新增 `Documents/Knowledges/ZH/Syntax_TArray.md` 中"字符串化"小节，或独立 `Syntax_TArrayToString.md`
  - 检查现有 Knowledges 索引（`Documents/Knowledges/ZH/Index.md`）是否已有 TArray 文档；有则补节，无则新增并登记进 Index
  - 内容：三种 `ToString` 重载的签名与示例、`opImplConv` 的拼接行为、`FString::Format` 协同、嵌套容器深度限制、未注册元素的占位规则
- [ ] **P3.1** 📦 Git 提交：`[Docs/Knowledges] Docs: document TArray ToString and Format integration`

- [ ] **P3.2** 在 `Script/Examples/` 下补一个最小示例
  - 落点：`Script/Examples/Core/Example_ContainerFormat.as`（如已有 Container 示例则合并到现有文件）
  - 演示：`TArray<int>` 默认 ToString、截断、范围、与 `Log` / `FString::Format` 协同
  - 在 `Script/Examples/README.md`（如存在）登记
- [ ] **P3.2** 📦 Git 提交：`[Script/Examples] Docs: add container ToString example script`

### Phase 4 — TMap / TSet 同等能力（次优先）

> 目标：TMap 与 TSet 拥有同样的 ToString + 拼接 + Format 能力。由于 P2.1 的 Generic_AppendToString 容器分支已按"模板 + Ops"判断，理论上只需补对应 Ops 的 `AppendToString` 即可。

- [ ] **P4.1** 调研 `Bind_TMap.cpp` / `Bind_TSet.cpp` 中是否存在与 `FArrayOperations` 对等的 `FMapOperations` / `FSetOperations`，确认元素遍历入口（key/value 指针、Pair 类型 id 等）
  - 如果结构对称则按同样套路实现 `AppendToString`；如果差异较大需要在本 Plan 内追加子任务
- [ ] **P4.1** 📦 Git 提交：`[Runtime/Binds] Chore: scope TMap/TSet ToString implementation`（仅当有调研产物需要落地，例如新增的内部辅助文件；纯调研可跳过提交，并在 P4.2 一起提交）

- [ ] **P4.2** 实现 `FMapOperations::ToString` / `ToString_Limited`，注册 `TMap.ToString()` 与 `opImplConv`
  - 输出格式：`{key1: value1, key2: value2}`（与 Python / Rust dict-like 表达对齐）
  - key / value 任一未注册 ToString → 元素位置 `<unknown>`
  - 在 P2.1 的容器分支中加入 TMap 路径（识别 Ops 类型）
- [ ] **P4.2** 📦 Git 提交：`[Runtime/Binds] Feat: implement TMap ToString and Format integration`

- [ ] **P4.3** 实现 `FSetOperations::ToString` / `ToString_Limited`，注册 `TSet.ToString()` 与 `opImplConv`
  - 输出格式：`{1, 2, 3}`（无 key:value，仅元素列表）
  - 同样接入 P2.1 容器分支
- [ ] **P4.3** 📦 Git 提交：`[Runtime/Binds] Feat: implement TSet ToString and Format integration`

- [ ] **P4.4** 新增 `AngelscriptTMapToStringTests.cpp` 与 `AngelscriptTSetToStringTests.cpp`
  - 用例参考 P1.4 + P2.3 的结构，覆盖：默认输出、空容器、截断、Format 接入、未注册元素占位
- [ ] **P4.4** 📦 Git 提交：`[Tests/Containers] Test: cover TMap and TSet ToString and Format`

### Phase 5 — 全量回归与索引同步

> 目标：确认本 Plan 不破坏现有测试，并把新增能力登记进项目索引。

- [ ] **P5.1** 执行 `Tools\RunTests.ps1`（或 `RunTestSuite.ps1` 的 Containers / Smoke 波次），确认 `Angelscript.TestModule.Containers.*` 与全量回归 0 失败、0 新增 Warning
  - 如有失败按"修复优先 → 必要时调整 expectation"次序处理
- [ ] **P5.1** 📦 Git 提交：`[Tests] Test: full regression after container ToString landing`

- [ ] **P5.2** 更新 `Documents/Plans/Plan_OpportunityIndex.md` 第四节"功能增强"，登记本 Plan
  - 在 `4.1 已有 Plan` 表格新增一行：`Plan_ContainerToStringFormat.md`，状态写明落地阶段
  - 如完成度足够高，同步更新优先级总结
- [ ] **P5.2** 📦 Git 提交：`[Docs/Plans] Docs: register container ToString plan in opportunity index`

- [ ] **P5.3** 完工后按 `Plan.md` 归档规则评估是否移入 `Documents/Plans/Archives/`
  - 在文档顶部补 `归档状态` / `归档日期` / `完成判断` / `结果摘要`
  - 同步 `Documents/Plans/Archives/README.md`
- [ ] **P5.3** 📦 Git 提交：`[Docs/Plans] Chore: archive container ToString plan`

## 影响范围

本计划属于**功能增量 + 少量分发逻辑修改**，文件总数 < 10，不需要按目录分组矩阵化。受影响清单如下：

| 文件 | 变更类型 | 说明 |
|------|---------|------|
| `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TArray_Functions.h` | 修改 | 新增 `FArrayOperations::AppendToString` / `ToString` / `ToString_Limited` / `ToString_Range` |
| `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TArray.cpp` | 修改 | 在 `Bind_TArray` lambda 中注册 3 个 ToString 重载 + 1 个 `opImplConv` |
| `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FString.cpp` | 修改 | `Generic_AppendToString` 增加模板容器分支与递归深度保护 |
| `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TMap.cpp` | 修改 | （Phase 4）注册 `TMap.ToString` / `opImplConv` |
| `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TSet.cpp` | 修改 | （Phase 4）注册 `TSet.ToString` / `opImplConv` |
| `Plugins/Angelscript/Source/AngelscriptTest/Containers/AngelscriptTArrayToStringTests.cpp` | 新增 | TArray 专项测试 |
| `Plugins/Angelscript/Source/AngelscriptTest/Containers/AngelscriptTMapToStringTests.cpp` | 新增 | TMap 专项测试（Phase 4） |
| `Plugins/Angelscript/Source/AngelscriptTest/Containers/AngelscriptTSetToStringTests.cpp` | 新增 | TSet 专项测试（Phase 4） |
| `Documents/Knowledges/ZH/Syntax_TArray.md`（或新增 `Syntax_TArrayToString.md`） | 新增/修改 | 字符串化文档 |
| `Documents/Knowledges/ZH/Index.md` | 修改 | 登记新文档 |
| `Script/Examples/Core/Example_ContainerFormat.as` | 新增 | 示例脚本 |
| `Documents/Plans/Plan_OpportunityIndex.md` | 修改 | 登记本 Plan |

## 验收标准

1. 脚本中 `TArray<int>{1,2,3}.ToString() == "[1, 2, 3]"`、`TArray<int>().ToString() == "[]"`，`Log("V=" + Arr)` 编译且输出预期。
2. `Arr.ToString(MaxElements)` 在超长时输出 `"[..., ..., ...] (N total)"`；不超长时与默认输出一致。
3. `Arr.ToString(Start, End)` 行为符合半开区间 `[Start, End)`；越界/反序输出 `"[]"` 且不抛异常。
4. `FString::Format("{0}", Arr)` / `FString::ApplyFormat(Arr, "")` / `FString.Append(Arr)` 三条路径均输出与 `Arr.ToString()` 一致的字符串。
5. `TArray<TArray<int>>` 等嵌套容器在深度 4 处优雅截断为 `"[...]"`，不触发栈溢出或异常。
6. 元素类型未注册 ToString 时输出 `<TypeName>` 占位，不抛 `"Invalid type to append to string."`。
7. Phase 4 完成后，`TMap` / `TSet` 拥有同等能力，对应测试全绿。
8. 全量回归（`RunTests.ps1` 全前缀）相对基线零回归、零新增 Warning。
9. 新文档与示例可被 `Documents/Knowledges/ZH/Index.md` 索引到，且与 `Plan_OpportunityIndex.md` 登记一致。

## 风险与注意事项

### 风险

1. **`Generic_AppendToString` 增加容器分支可能影响现有未识别类型的报错路径**：当前 `"Invalid type to append to string."` 是诊断脚本作者的关键提示。若新增分支吃掉了本应报错的非容器对象，会让排错变难。
   - **缓解**：分支内严格判断 `asOBJ_TEMPLATE` + `FArrayOperations::GetArrayOperations(Meta) != nullptr` 才进入容器路径；其他模板对象继续走原 fallback。补一个 `Negative` 测试断言：注册一个非容器模板类型的 dummy，确认仍然报错。
2. **递归深度保护的实现位置**：`Generic_AppendToString` 当前是无状态函数，新增递归深度需要 thread-local 或在函数签名加深度参数。后者会破坏现有调用点。
   - **缓解**：优先选用 thread-local `static thread_local int32 GAppendDepth = 0;` + RAII guard。`Bind_FString.cpp` 的现有调用点不需要改签名。在测试中验证 `TArray<TArray<int>>` 的浅嵌套（深度 ≤ 4）仍能完整展开。
3. **`opImplConv` 在脚本中可能引发非预期的隐式 cast**：例如 `if (Arr) { ... }` 之类的上下文若被 `FString` 实现影响。
   - **缓解**：仅注册 `FString opImplConv() const`，不注册 `bool opImplConv()`。条件位置不会命中（除非启用 AS 2.38 的 boolConversionMode 1，且 `FString` 已有自身的 bool 转换逻辑，与本 Plan 不冲突）。
4. **`SCRIPT_NATIVE_TEMPLATED_CALL_NEEDSCOPY` 的字符串标识冲突**：三个 `ToString` 重载若用相同 key 注册 native call shim，会在运行时崩溃或调到错的实现。
   - **缓解**：分别使用 `"FArrayOperations::ToString"` / `"FArrayOperations::ToString_Limited"` / `"FArrayOperations::ToString_Range"` 唯一字符串。在 P1.2 自审 + P1.4 测试覆盖三个重载分别被调用一次。
5. **TMap key 的字符串化可能涉及 hash/equality 之外尚未触达的元数据**：Phase 4 的 `FMapOperations` 若没有暴露 key TypeId，需要从底层 AS 类型补取，工作量可能比预估大。
   - **缓解**：P4.1 先做调研，必要时把 Phase 4 拆成单独后续 Plan，不阻塞 Phase 1-3 落地与归档。

### 已知行为变化

1. **`FString::Format("{0}", Arr)` 之前抛异常，现在返回字符串**：脚本中曾经用 try/catch 或事先 `.ToString()` 规避此异常的代码，现在能跑通且不会抛——属于能力增强，不是回归，但需在 P3.1 文档明确告知。
2. **`Log("Arr=" + Arr)` 之前编译失败，现在编译成功**：依赖编译失败做"提示作者用 ToString" 的代码（极少见）会失去提示。`Plugins/Angelscript/Source/AngelscriptTest/` 中目前未发现该模式。
3. **`Generic_AppendToString` 新增 thread-local 状态**：会带来极少量 TLS 访问开销（O(1)，仅在每次 Append 一次进入/退出 RAII guard）。在单元测试中通过 `AngelscriptEnginePerformanceTests` 的现有快照对比，预期无可观测差异。若 P5.1 性能波动 > 5%，需要回到设计阶段重新评估。
