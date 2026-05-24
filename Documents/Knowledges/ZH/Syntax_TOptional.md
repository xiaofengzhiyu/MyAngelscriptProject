# Syntax_TOptional — `TOptional<T>` 可选值包装实现原理

> **所属前缀**: Syntax_（容器与值包装族）
> **关注层面**: 语法机制与实现原理（不写"怎么用"——那是 Guide_ 的活）
> **关键源码**:
> `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TOptional.cpp` (~588 行)
> · `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TOptional.h` (~173 行)
> · `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITBinds.h` —— `SCRIPT_NATIVE_TEMPLATED_CALL*` 宏
> · `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptOptionalBindingsTests.cpp` —— 行为契约
> · UE 反射：`UObject/PropertyOptional.h` —— `FOptionalProperty`
> **关联文档**:
> `Documents/Knowledges/ZH/Syntax_TWeakObjectPtr.md` —— 最近邻语义对照（弱引用 vs 可空值）
> · `Documents/Knowledges/ZH/Syntax_TArray.md` —— 模板容器注册的参考骨架（`TemplateCallback`）
> · `Documents/Knowledges/ZH/Syntax_TMap.md` —— 兄弟容器（K/V 双子类型）
> · `Documents/Knowledges/ZH/Syntax_TSet.md` —— 兄弟容器（单子类型）
> · `Documents/Knowledges/ZH/Type_BindSystem.md` —— `FAngelscriptType` / `FAngelscriptTypeUsage` 类型系统
> **外部参考**（可选）:
> [UE5 `TOptional` 头文件](https://github.com/EpicGames/UnrealEngine/blob/release/Engine/Source/Runtime/Core/Public/Misc/Optional.h)

---

## 概览

`TOptional<T>` 是"**T 或没有 T**"的值容器——与 `std::optional<T>` / Rust `Option<T>` / Haskell `Maybe a` 同宗，区别于 `TWeakObjectPtr<T>`（"对象**可能**被 GC 回收"）和 `TSubclassOf<T>`（"持有 UClass 指针"）。在当前 AS 插件中，它通过**单文件单 facade** (`Bind_TOptional.cpp`) 实现为 `asOBJ_TEMPLATE_SUBTYPE_DETERMINES_SIZE` 模板值类型，与 `TArray` / `TMap` / `TSet` 共享同一类型系统脚手架，但因只有一个子类型 `T` 且无哈希/迭代器，结构上比真正的容器简化了一个数量级。

```text
         AS 脚本侧                              C++ 实现侧
         ====================                  ========================
    TOptional<int> O;                       Construct → bIsSet = false
    O.Set(42);                              Set(ValuePtr) → 构造 inner + 置位
    O.IsSet()                               读 GetIsSetPtr → bool
    O.GetValue()                            如未 Set → 抛 AS 异常
    O.Get(Fallback)                         如未 Set → 返回 Fallback
    O.Reset()                               析构 inner + 清位
    O = OtherOptional;                      OpAssign(Optional)
    O = 99;                                 OpAssignValue(T)
    if (O == Other) ...                     OpEquals — 双方 bIsSet 与 inner 比较
```

### TOptional 在族谱中的坐标

```text
          ┌────────────────────────────────────────────────┐
          │  AS 模板值类型 (asOBJ_TEMPLATE)                │
          ├────────────────────────────────────────────────┤
          │  TArray<T>     一对 N，FScriptArray 桥接       │
          │  TMap<K,V>     K→V 哈希，FScriptMap 桥接       │
          │  TSet<T>       T 哈希，FScriptSet 桥接         │
          │  TOptional<T>  T 或空，自管 [T][bool] 内联     │ ★ 本文
          ├────────────────────────────────────────────────┤
          │   共同骨架                                     │
          │   - FAngelscriptType 子类（facade）            │
          │   - SCRIPT_NATIVE_TEMPLATED_CALL 注册          │
          │   - PreviousBindPassScriptObjectTypeAsFirstParam│
          │   - TemplateCallback 校验子类型                │
          └────────────────────────────────────────────────┘

          兄弟"可空"语义对照:
          ┌──────────────────┬──────────────────┬──────────────────┐
          │  TWeakObjectPtr  │  TSoftObjectPtr  │  TOptional<T>    │
          ├──────────────────┼──────────────────┼──────────────────┤
          │ 仅限 UObject     │ 仅限 UObject     │ 任何 T           │
          │ GC 自动失效      │ 显式 Resolve     │ 显式 Set/Reset   │
          │ 不阻止 GC        │ 路径引用         │ 持有完整 T       │
          │ IsValid          │ IsValid          │ IsSet            │
          │ Get→nullptr 安全 │ Get→nullptr      │ GetValue→抛异常  │
          └──────────────────┴──────────────────┴──────────────────┘
```

后续按 ① 数据布局 / ② 类型 facade / ③ 操作集 `FOptionalOperations` / ④ AS 端注册 `Bind_TOptional` / ⑤ UE 反射桥（`FOptionalProperty`）/ ⑥ 与 `TWeakObjectPtr` 的语义差异 / ⑦ 限制与避坑 顺序展开。

---

## 一、数据布局：内联存储 + 末位标志位

### 1.1 物理布局

`TOptional<T>` 在 AS 端不分配额外的堆——它把 inner 值 `T` 和 "是否已设置" 的布尔位**紧凑放在同一块栈/堆内联存储**里。布局公式见 `FAngelscriptOptionalType::GetValueSize`：

```cpp
// ============================================================================
// 文件: AngelscriptRuntime/Binds/Bind_TOptional.cpp
// 函数: FAngelscriptOptionalType::GetValueSize / GetValueAlignment
// ============================================================================
virtual int32 GetValueSize(const FAngelscriptTypeUsage& Usage) const override
{
    return Align(Usage.SubTypes[0].GetValueSize() + 1, Usage.SubTypes[0].GetValueAlignment());
}

virtual int32 GetValueAlignment(const FAngelscriptTypeUsage& Usage) const override
{
    return Usage.SubTypes[0].GetValueAlignment();
}
```

也就是说**总大小 = 对齐到 T 自身 alignment 的 `(sizeof(T) + 1)`**——多出来的 `1` 字节就是末位的 `bIsSet` 标志，整体对齐取 T 的对齐。

```text
偏移   0                                 sizeof(T)        sizeof(T)+1
       ┌──────────────────────────────────┬─────────────────┐
       │  inner T                         │ bIsSet (1 字节) │  ── 末尾再 padding 到 T 对齐
       └──────────────────────────────────┴─────────────────┘
       └──── GetValuePtr(Optional) ───────┘└─ GetIsSetPtr(Optional)
```

`Bind_TOptional.h` 中的辅助方法将这两个段地址寻址显式化：

```cpp
// ============================================================================
// 文件: AngelscriptRuntime/Binds/Bind_TOptional.h
// 函数: FOptionalOperations 内联辅助
// ============================================================================
FORCEINLINE bool* GetIsSetPtr(FAngelscriptOptional& Optional)
{
    return (bool*)(((SIZE_T)&Optional) + TypeSize);   // ★ inner 之后第一个字节
}

FORCEINLINE bool IsSet(FAngelscriptOptional& Optional)
{
    return *GetIsSetPtr(Optional);
}

FORCEINLINE void* GetValuePtr(FAngelscriptOptional& Optional)
{
    return &Optional;                                  // ★ 起始即 inner 起始
}
```

`FAngelscriptOptional` 自身是个空结构体（`struct FAngelscriptOptional {}`），仅作类型别名扮演"AS 侧的 Optional 实例"——真实数据靠 `TypeSize` 偏移寻址。

### 1.2 与 UE C++ `TOptional` 的差异

UE 原生 `TOptional<T>` 用 `alignas(T) uint8 Storage[sizeof(T)] + bool bIsSet` 类似的 POD 布局。AS 这边并未直接复用 `TOptional<T>`，而是自管布局——目的是让**单一 `FAngelscriptOptional` 类型在 AS 注册系统中"统包"所有 T 的实例化**：

- `ValueClass("TOptional<class T>", sizeof(bool), Flags)` 注册时基础大小填 `sizeof(bool)`；
- 但 `Flags.ExtraFlags |= asOBJ_TEMPLATE_SUBTYPE_DETERMINES_SIZE` 告诉 AS 引擎"实际大小由 `GetValueSize()` 动态计算"；
- 这样不需要为每个 `TOptional<int>` / `TOptional<FString>` 各注册一个 C++ 类型。

> Bind_TOptional.h 中保留了一个未启用的 `*_Template<T>` 系列函数（如 `Construct_Template` / `Get_Template`），它们用 placement new 直接构造 UE 原生 `TOptional<T>`。这条路径目前**未被绑定调用**——主路径全用 `FOptionalOperations` 通用接口。`*_Template` 形成"未来若启用 StaticJIT 或类型特化时的快路径备份"，与 TArray 的 `*_Template<T>` 机制一致。

### 1.3 `Bind_TOptional` 注册的关键 Flags

```cpp
// ============================================================================
// 文件: AngelscriptRuntime/Binds/Bind_TOptional.cpp
// 函数: Bind_TOptional 顶部
// ============================================================================
FBindFlags Flags;
Flags.bTemplate = true;
Flags.TemplateType = "<T>";
Flags.ExtraFlags |= asOBJ_TEMPLATE_SUBTYPE_DETERMINES_SIZE;  // ★ 大小动态
Flags.ExtraFlags |= asOBJ_TEMPLATE_SUBTYPE_COVARIANT;        // ★ 子类型协变
Flags.Alignment = 1;
auto TOptional_ = FAngelscriptBinds::ValueClass("TOptional<class T>", sizeof(bool), Flags);
```

| Flag | 含义 |
|------|------|
| `bTemplate = true` | 这是模板类型，AS 注册系统会跑 `TemplateCallback` 校验子类型 |
| `asOBJ_TEMPLATE_SUBTYPE_DETERMINES_SIZE` | 实际大小由 `GetValueSize()` 计算，注册时给 `sizeof(bool)` 仅作占位 |
| `asOBJ_TEMPLATE_SUBTYPE_COVARIANT` | `TOptional<UPlayer>` 可视作 `TOptional<UObject>` 子类型（与 TWeakObjectPtr 同） |
| `Alignment = 1` | 注册时基础对齐 1，真实对齐由 `GetValueAlignment` 动态返回 |

---

## 二、类型 facade：`FAngelscriptOptionalType`

### 2.1 facade 在类型系统中的角色

`FAngelscriptOptionalType : FAngelscriptType` 是 `Bind_TOptional.cpp` 主体——它实现 `FAngelscriptType` 的虚函数，把 `TOptional<T>` 桥接到 `FAngelscriptTypeUsage` 多态分派系统。本质上是"**一个 C++ 类，描述任意 `TOptional<T>` 实例化的所有元行为**"，与 `FArrayType` / `FMapType` / `FSetType` 平级。

facade 接口职责对应表：

| 接口 | 实现要点 | 作用 |
|------|----------|------|
| `GetAngelscriptTypeName` | 返回 `"TOptional"` | dump / 错误消息 |
| `CanQueryPropertyType` | `return false` | 不参与从 AS 类型反查 FProperty |
| `CanBeTemplateSubType` | `return false` | TOptional 不能作为容器子类型嵌套 |
| `RequiresProperty` | `return false` | 不强制要求关联 FProperty |
| `HasReferences` | `SubTypes[0].HasReferences()` | GC schema 计算前置 |
| `EmitReferenceInfo` | 写 `EMemberType::Optional` schema | UObject GC 跟踪 |
| `CanCopy` / `NeedCopy` / `CopyValue` | 走 inner T 的拷贝 + 复制 bIsSet | AS 赋值 |
| `CanCompare` / `IsValueEqual` | 双方 bIsSet 与 inner 比较 | `==` 运算 |
| `CanConstruct` / `ConstructValue` | 仅置 `bIsSet = false` | 默认构造 |
| `CanDestruct` / `DestructValue` | 若 bIsSet 则析构 inner | 释放 |
| `GetValueSize` / `GetValueAlignment` | 内联布局公式 | 内存计算 |
| `CanCreateProperty` / `CreateProperty` | 创建 `FOptionalProperty` | UPROPERTY 反射 |
| `MatchesProperty` | 检查 `FOptionalProperty` + inner 匹配 | 参数匹配 |
| `CanBeArgument` | `return false` | TOptional **不能直接作 UFunction 参数** |
| `CanBeReturned` | `return true` | 但**可作返回值** |
| `GetReturnValue` | 处理 ref/value 两种返回路径 | UFunction → AS 桥 |
| `GetDebuggerValue/Scope/Member` | 显示 "Set: <inner>" / "Unset" | 调试器面板 |
| `GetCppForm` | `TOptional<inner>` 字符串形式 | codegen 输出 |

### 2.2 `ConstructValue` —— 仅置位，不构造 inner

```cpp
// ============================================================================
// 文件: AngelscriptRuntime/Binds/Bind_TOptional.cpp
// 函数: FAngelscriptOptionalType::ConstructValue / DestructValue
// ============================================================================
virtual void ConstructValue(const FAngelscriptTypeUsage& Usage, void* DestinationPtr) const override
{
    int32 ElementSize = Usage.SubTypes[0].GetValueSize();
    *(bool*)((SIZE_T)DestinationPtr + ElementSize) = false;     // ★ 只清 bIsSet
}

virtual void DestructValue(const FAngelscriptTypeUsage& Usage, void* DestinationPtr) const override
{
    int32 ElementSize = Usage.SubTypes[0].GetValueSize();
    if (*(bool*)((SIZE_T)DestinationPtr + ElementSize))         // ★ 仅在已设置时析构 inner
        Usage.SubTypes[0].DestructValue(DestinationPtr);
}
```

- **默认构造不构造 inner**——这与 UE C++ `TOptional` 一致：未 Set 的 `TOptional<T>` 内部 T 段是"未初始化的字节"，永不应被读取。
- 这意味着对 `TOptional<FString>` 调用 `ConstructValue` 是 O(1) 的纯标志位写入，比 `TArray` / `TMap` 还轻。

### 2.3 `CopyValue` —— 状态机 4 路分支

```cpp
// ============================================================================
// 文件: AngelscriptRuntime/Binds/Bind_TOptional.cpp
// 函数: FAngelscriptOptionalType::CopyValue
// ============================================================================
virtual void CopyValue(const FAngelscriptTypeUsage& Usage, void* SourcePtr, void* DestinationPtr) const override
{
    int32 ElementSize = Usage.SubTypes[0].GetValueSize();
    if (*(bool*)((SIZE_T)SourcePtr + ElementSize))              // Source 已 Set
    {
        if (!*(bool*)((SIZE_T)DestinationPtr + ElementSize))    //   Dst 未 Set: 先构造
        {
            Usage.SubTypes[0].ConstructValue(DestinationPtr);
            *(bool*)((SIZE_T)DestinationPtr + ElementSize) = true;
        }
        Usage.SubTypes[0].CopyValue(SourcePtr, DestinationPtr); // 拷贝 inner
    }
    else                                                         // Source 未 Set
    {
        if (*(bool*)((SIZE_T)DestinationPtr + ElementSize))     //   Dst 已 Set: 析构 + 清位
        {
            Usage.SubTypes[0].DestructValue(DestinationPtr);
            *(bool*)((SIZE_T)DestinationPtr + ElementSize) = false;
        }
    }
}
```

四个状态分支构成完整的状态机：

```text
              Source.bIsSet
            true        false
          ┌─────────┬──────────┐
   Dst   t│ 拷贝    │ 析构+清位 │
   .bIsSet │ inner   │           │
          ├─────────┼──────────┤
        f │ 构造+   │ no-op     │
          │ 拷贝    │           │
          └─────────┴──────────┘
```

注意 inner 的拷贝走 `Usage.SubTypes[0].CopyValue`——会递归到 inner 类型的 facade（如 `TOptional<FString>` 内层是 `FString` 的深拷贝）。

### 2.4 GC schema：`EMemberType::Optional`

当 inner 持有 UObject 引用（如 `TOptional<UObject>` 或 `TOptional<TArray<AActor>>`），`EmitReferenceInfo` 把 inner 的 schema 嵌入到一个 Optional schema 节点里：

```cpp
// ============================================================================
// 文件: AngelscriptRuntime/Binds/Bind_TOptional.cpp
// 函数: FAngelscriptOptionalType::EmitReferenceInfo
// ============================================================================
void EmitReferenceInfo(const FAngelscriptTypeUsage& Usage, FGCReferenceParams& Params) const
{
    check(HasReferences(Usage));
    UE::GC::FSchemaBuilder InnerSchema(Usage.SubTypes[0].GetValueSize());
    {
        FGCReferenceParams InnerParams = Params;
        InnerParams.AtOffset = 0;
        InnerParams.Schema = &InnerSchema;
        Usage.SubTypes[0].EmitReferenceInfo(InnerParams);
    }
    Params.Schema->Add(DeclareMember(Params.Names.Top(), Params.AtOffset,
        UE::GC::EMemberType::Optional, InnerSchema.Build()));
}
```

`UE::GC::EMemberType::Optional` 是 UE GC 内置的 schema 节点种类——GC 在扫描时会先读 bIsSet，仅在已设置时才递归扫描 inner。如果 `T` 是 `UObject*`，未设置的 Optional 不会向 GC 报告任何引用，避免误读未初始化字节。

---

## 三、操作集：`FOptionalOperations`

### 3.1 角色

`FOptionalOperations` 是"**绑定到 AS 模板类型 UserData 的小型对象**"——AS 引擎给每个 `TOptional<T>` 实例化分配一份，在 `TemplateCallback` 阶段创建并填充元信息。AS 端方法（`Set` / `Reset` / `OpEquals` 等）通过 `Meta->GetUserData()` 取出 `FOptionalOperations*`，再调它的 `IsSet` / `Set` / `Reset` 等内联辅助。

```cpp
// ============================================================================
// 文件: AngelscriptRuntime/Binds/Bind_TOptional.cpp
// 函数: FOptionalOperations::ValidateOptionalOperations
// ============================================================================
FOptionalOperations* FOptionalOperations::ValidateOptionalOperations(asITypeInfo* TemplateType, asCString* ErrorMessage)
{
    if (FOptionalOperations* Ops = static_cast<FOptionalOperations*>(TemplateType->GetUserData()))
    {
        return Ops->bValid ? Ops : nullptr;
    }
    const FAngelscriptTypeUsage Type = FAngelscriptTypeUsage::FromTypeId(TemplateType->GetSubTypeId(0));
    if (!Type.CanBeTemplateSubType())
    {
        if (ErrorMessage != nullptr)
            *ErrorMessage = "Containers cannot be nested in other containers";
        return nullptr;
    }
    FOptionalOperations* Ops = new FOptionalOperations(Type);
    TemplateType->SetUserData(Ops);
    return Ops->bValid ? Ops : nullptr;
}
```

校验链路：
1. 子类型存在且**可作为模板子类型**（防止 `TOptional<TArray<int>>` 这类嵌套）；
2. 子类型可构造、可析构、可拷贝；
3. 失败返回 `nullptr` —— AS 编译期报错 "Containers cannot be nested in other containers"。

### 3.2 `Set` 与 `Reset` 的状态转换

```cpp
// ============================================================================
// 文件: AngelscriptRuntime/Binds/Bind_TOptional.cpp
// 函数: FOptionalOperations::Set / Reset
// ============================================================================
void FOptionalOperations::Reset(FAngelscriptOptional& Optional)
{
    if (IsSet(Optional))
    {
        if (bNeedDestruct)
            Type.DestructValue(GetValuePtr(Optional));
        *GetIsSetPtr(Optional) = false;
    }
}

void FOptionalOperations::Set(FAngelscriptOptional& Optional, void* ValuePtr)
{
    void* DestinationPtr = GetValuePtr(Optional);
    if (!IsSet(Optional))                   // 之前未 Set: 先构造 inner
    {
        *GetIsSetPtr(Optional) = true;
        if (bNeedConstruct)
            Type.ConstructValue(DestinationPtr);
    }
    if (bNeedCopy)                          // 之前已 Set: 直接覆盖
        Type.CopyValue(ValuePtr, DestinationPtr);
    else
        FMemory::Memcpy(DestinationPtr, ValuePtr, TypeSize);
}
```

关键不变量：

- **`Set` 是幂等的状态机推进**：如果之前已 Set，直接覆盖 inner；如果之前未 Set，先构造再赋值。两条路径都保证 inner 处于"已构造"状态。
- **`bNeedCopy`** 标志位决定走 `CopyValue`（如 FString 深拷贝）还是 `Memcpy`（POD 类型如 int / FVector）。
- **`Reset` 先析构再清位**——确保 FString 内的堆内存被释放。

### 3.3 状态转换图

```text
                  ┌─────────────────────────┐
        Construct │   bIsSet = false        │
        ─────────▶│   inner: 未初始化字节    │
                  └────────────┬────────────┘
                               │ Set(v)
                               ▼
              ┌────────────────────────────────┐
              │  bIsSet = true                 │ ◀──┐
              │  inner: 完整有效 T             │    │ Set(v')   覆写
              └─────┬──────────────────────────┘    │
                    │ Reset()                       │
                    ▼                                │
              ┌────────────────────────────────┐    │
              │  bIsSet = false                │────┘ Set(v)
              │  inner: 已析构（字节脏）        │
              └────────────────────────────────┘
                    │ Destruct
                    ▼
                  生命周期终结
```

---

## 四、AS 端注册：`Bind_TOptional`

### 4.1 注册全图

`Bind_TOptional` 在 `EOrder::Early` 阶段执行（与 `TArray` / `TMap` 相同优先级）。下面按注册顺序列出 AS 端可见的所有签名：

```cpp
// ============================================================================
// 文件: AngelscriptRuntime/Binds/Bind_TOptional.cpp
// 函数: Bind_TOptional 注册块（节选）
// ============================================================================
TOptional_.Constructor("void f()",                                      &Construct);
TOptional_.ImplicitConstructor("void f(const T&in if_handle_then_const Other)", &InitConstruct);
TOptional_.Constructor("void f(const TOptional<T>& Other)",             &CopyConstruct);
TOptional_.Destructor("void f()",                                       &Destruct);
TOptional_.TemplateCallback("bool f(int&in Type, int&out ErrorMessage)", &ValidateOptionalOperations);

TOptional_.Method("TOptional<T>& opAssign(const TOptional<T>& Other)",                    &OpAssign);
TOptional_.Method("TOptional<T>& opAssign(const T&in if_handle_then_const Value)",        &OpAssignValue);
TOptional_.Method("bool opEquals(const TOptional<T>& Other) const",                       &OpEquals);
TOptional_.Method("bool IsSet() const",                                                   &IsSet);
TOptional_.Method("void Set(const T&in if_handle_then_const Value) const",                &Set);
TOptional_.Method("const T& GetValue() const",                                            &GetValue);
TOptional_.Method("T& GetValue()",                                                        &GetValue);
TOptional_.Method("const T& Get(const T&in if_handle_then_const DefaultValue) const",     &Get);
TOptional_.Method("void Reset()",                                                         &Reset);
```

### 4.2 三件套 `IsSet / Get / Reset` 详解

| AS 签名 | C++ 实现 | 行为 |
|---------|----------|------|
| `bool IsSet() const` | `Ops->IsSet(Optional)` | 直接读 bIsSet 字节 |
| `const T& GetValue() const` | 未 Set 时 `FAngelscriptEngine::Throw` | **强制要求先 IsSet**，否则抛 AS 异常 |
| `T& GetValue()` | 同上（非 const 版） | 允许在脚本中修改 inner |
| `const T& Get(const T& Default)` | 未 Set 时返回 `DefaultValuePtr` | 永不抛异常，缺省回退 |
| `void Set(const T& Value)` | `Ops->Set(Optional, ValuePtr)` | 见 §3.2 |
| `void Reset()` | `Ops->Reset(Optional)` | 析构 inner + 清位 |

**`GetValue` 的双重重载**——const 和非 const 同时绑定。脚本写 `O.GetValue() = 99;` 时编译器会选非 const 版，得到可写引用：

```angelscript
TOptional<int> O;
O.Set(10);
int& Ref = O.GetValue();   // 非 const 重载
Ref = 20;
ensure(O.GetValue() == 20);
```

这一细节在 `RunOptionalApiCoverageSection` 的 `OptApi_GetMutableViaRef` 用例里有覆盖（`AngelscriptOptionalBindingsTests.cpp:420-427`）。

### 4.3 `GetValue` 抛异常 vs `Get` 缺省值

```cpp
// ============================================================================
// 文件: AngelscriptRuntime/Binds/Bind_TOptional.cpp
// 函数: FAngelscriptOptionalBinds::GetValue / Get
// ============================================================================
void* FAngelscriptOptionalBinds::GetValue(FAngelscriptOptional& Optional, asCObjectType* Meta)
{
    auto* Ops = FOptionalOperations::GetOptionalOperations(Meta);
    if (!Ops->IsSet(Optional))
        FAngelscriptEngine::Throw("GetValue() called on Optional when not set! Check the optional with IsSet() first.");
    return Ops->GetValuePtr(Optional);
}

void* FAngelscriptOptionalBinds::Get(FAngelscriptOptional& Optional, asCObjectType* Meta, void* DefaultValuePtr)
{
    auto* Ops = FOptionalOperations::GetOptionalOperations(Meta);
    if (!Ops->IsSet(Optional))
        return DefaultValuePtr;
    return Ops->GetValuePtr(Optional);
}
```

二者的契约差异是**整个 TOptional API 的核心**：

- `GetValue()` 是"我**知道**有值"——错了就**抛 AS 异常**，进入 `FAngelscriptEngine::Throw` 错误链路。脚本调用栈被中止，错误日志包含 `"GetValue() called on Optional when not set"`。
- `Get(Default)` 是"**没有就给我兜底**"——零异常成本，但要求构造 `Default` 值。

> 历史上 `Get(Default)` 是 UE C++ 中也存在的便捷方法（`TOptional<T>::Get(const T& DefaultValue)`），AS 这边语义完全一致。脚本中**优先用 `Get(Default)`** 减少异常处理代码。

### 4.4 `if_handle_then_const` 修饰符

注意 `Set`、`OpAssignValue`、`Get` 的 `T&in` 参数都带 `if_handle_then_const` 修饰符。该修饰符的语义见 AngelScript fork：

> 当 T 是对象句柄时，把句柄修饰为指向 const 对象。

```cpp
// 来自 source/as_scriptengine.cpp:3620-3625（AngelScript 内核）
if (dt.IsObjectHandle() && orig.HasIfHandleThenConst())
    dt.MakeHandleToConst(true);
```

实际意义：当脚本写 `TOptional<UObject>` 时，`Set(const UObject&in if_handle_then_const Value)` 会被解释为 `Set(const UObject@ Value)` —— 即"接受指向 const UObject 的句柄"。这避免了 AS 端"const T&" 在 T 是 UObject 时的语义歧义。

### 4.5 `SCRIPT_NATIVE_TEMPLATED_CALL` 与 `PreviousBindPassScriptObjectTypeAsFirstParam`

每条 `Method` / `Constructor` 都跟着两条额外行：

```cpp
TOptional_.Method("void Set(const T&in if_handle_then_const Value) const", &Set);
SCRIPT_NATIVE_TEMPLATED_CALL_NEEDSCOPY(TOptional_, "FAngelscriptOptionalBinds::Set", false);
FAngelscriptBinds::PreviousBindPassScriptObjectTypeAsFirstParam();
```

- **`SCRIPT_NATIVE_TEMPLATED_CALL_NEEDSCOPY`**：通知 StaticJIT codegen 这是模板实例化的 native 调用，且参数中含需要 copy-construct 的类型。`NEEDSCOMPARE` 变体用于 `OpEquals`，普通 `_TEMPLATED_CALL` 用于不带值参数的方法（如 `IsSet` / `Reset`）。
- **`PreviousBindPassScriptObjectTypeAsFirstParam`**：告诉 AS 引擎绑定函数的实际签名是 `(FAngelscriptOptional&, asCObjectType*, ...)`——即把模板 `asCObjectType*`（携带子类型信息）作为第一参数注入。这就是为什么 `FAngelscriptOptionalBinds::Set` 第二参数是 `asCObjectType* Meta`。

整套机制让 AS 引擎在调用 `Set(42)` 时自动生成 `(Optional, Meta, &42)` 的真实调用栈。

### 4.6 测试入口对照

`AngelscriptOptionalBindingsTests.cpp` 把每条 binding 都至少覆盖了一次：

| AS API | 测试用例（节选） |
|--------|-----------------|
| 默认构造 + IsSet | `OptEmpty_IsSet` |
| ImplicitConstructor | `OptFName_IsSet`（`TOptional<FName> O(FName("Alpha"))`）|
| Set / GetValue | `OptSet_GetValue` |
| CopyConstruct | `OptCopy_Equals` |
| OpAssign(Optional) | `OptApi_AssignOptionalFromSet_*` |
| OpAssignValue(T) | `OptApi_AssignFromValue_*` |
| OpEquals 4 路 | `OptApi_EmptyEqualsEmpty` / `SetEqualsSameValue` / `SetNotEqualsDifferentValue` / `SetNotEqualsEmpty` |
| Reset | `OptReset_IsSet` |
| Get(Fallback) 命中 | `OptString_FallbackVsValue` |
| Get(Fallback) 未命中 | `OptString_EmptyFallback` |
| **GetValue 未 Set 抛异常** | `OptionalGetValueUnsetError` 测试方法 |

异常路径的测试用例尤其关键——它通过 `Test.AddExpectedErrorPlain` 标记预期错误日志，再用 `ExecuteFunctionExpectingScriptException` 触发：

```cpp
// 节选自 RunOptionalErrorSection
Test.AddExpectedErrorPlain(TEXT("GetValue() called on Optional when not set"),
    EAutomationExpectedErrorFlags::Contains, 0);
return ExecuteFunctionExpectingScriptException(/* ... */, FString(TEXT("GetValue() called on Optional when not set")));
```

---

## 五、UE 反射桥：`FOptionalProperty`

### 5.1 `FOptionalProperty` 是什么

UE 5.5 引入的 `FOptionalProperty`（位于 `UObject/PropertyOptional.h`）是反射系统中描述 `TOptional<T>` 的属性类——与 `FArrayProperty` / `FMapProperty` 平级。它内部持有一个 `FProperty* ValueProperty` 描述 inner T，并暴露 `IsSet(const void* Address)` / `GetValuePointerForRead(const void*)` 等运行时辅助。

> 在 UE 5.5 之前，`TOptional<T>` 不在反射图谱里——任何使用 `TOptional<T>` 的 UPROPERTY 都得手动绕开反射或借助辅助类型。`Bind_TOptional` 的 `CreateProperty` / `RegisterTypeFinder` 路径正是搭在 5.5 之上的。

### 5.2 `CreateProperty` —— AS UPROPERTY → FOptionalProperty

```cpp
// ============================================================================
// 文件: AngelscriptRuntime/Binds/Bind_TOptional.cpp
// 函数: FAngelscriptOptionalType::CreateProperty
// ============================================================================
virtual FProperty* CreateProperty(const FAngelscriptTypeUsage& Usage, const FPropertyParams& Params) const override
{
    auto* OptionalProp = new FOptionalProperty(Params.Outer, Params.PropertyName, RF_Public);
    FPropertyParams InnerParams = Params;
    InnerParams.Outer = OptionalProp;
    InnerParams.PropertyName = *(Params.PropertyName.ToString() + TEXT("_Inner"));
    OptionalProp->SetValueProperty(Usage.SubTypes[0].CreateProperty(InnerParams));
    return OptionalProp;
}
```

构造链路：
1. 创建空的 `FOptionalProperty`，宿主是 `Params.Outer`；
2. 递归用 inner 的 `CreateProperty` 创建子属性，命名为 `<外层名>_Inner`；
3. 把 inner 属性挂到 `OptionalProp->SetValueProperty`。

这样 AS 中写：

```angelscript
class AMyActor : AActor
{
    UPROPERTY() TOptional<FString> Note;
}
```

UE 反射树就有 `FOptionalProperty Note` 持有 `FStrProperty Note_Inner` 子属性。编辑器面板自动呈现 "(Set)/(Unset)" 切换。

### 5.3 `MatchesProperty` —— 函数参数匹配

```cpp
// ============================================================================
// 文件: AngelscriptRuntime/Binds/Bind_TOptional.cpp
// 函数: FAngelscriptOptionalType::MatchesProperty
// ============================================================================
virtual bool MatchesProperty(const FAngelscriptTypeUsage& Usage, const FProperty* Property, EPropertyMatchType MatchType) const override
{
    if (Usage.SubTypes.Num() != 1) return false;
    const FOptionalProperty* OptionalProp = CastField<FOptionalProperty>(Property);
    if (OptionalProp == nullptr) return false;
    return Usage.SubTypes[0].MatchesProperty(OptionalProp->GetValueProperty(),
        FAngelscriptType::EPropertyMatchType::InContainer);
}
```

注意 `EPropertyMatchType::InContainer`——告诉 inner facade "你正在被一个容器包裹"，inner 可能放宽某些匹配规则（如 ObjectProperty 在容器内允许更宽容的类型对照）。

### 5.4 `RegisterTypeFinder` —— C++ 端 FOptionalProperty 反向解析

```cpp
// ============================================================================
// 文件: AngelscriptRuntime/Binds/Bind_TOptional.cpp
// 函数: Bind_TOptional 中的 RegisterTypeFinder lambda
// ============================================================================
FAngelscriptType::RegisterTypeFinder([OptionalType](FProperty* Property, FAngelscriptTypeUsage& Usage) -> bool
{
    FOptionalProperty* OptionalProp = CastField<FOptionalProperty>(Property);
    if (OptionalProp == nullptr) return false;
    if ((OptionalProp->GetPropertyFlags() & CPF_NonNullable) != 0)   // ★ 拒绝 NonNullable
        return false;

    FAngelscriptTypeUsage InnerUsage = FAngelscriptTypeUsage::FromProperty(OptionalProp->GetValueProperty());
    if (!InnerUsage.IsValid()) return false;

    Usage.Type = OptionalType;
    Usage.SubTypes.Add(InnerUsage);
    return true;
});
```

**`CPF_NonNullable` 检查是一个微妙的护栏**——UE 5.5 后一些 ObjectProperty 标记为 NonNullable（保证非空），把这种场景误判为 TOptional 会丢失 nullable 语义。这里显式跳过。

当 C++ 类暴露：

```cpp
UPROPERTY()
TOptional<int32> RetryCount;
```

给 AS 时：
1. UHT 生成 `FOptionalProperty { ValueProperty = FIntProperty }`；
2. RegisterTypeFinder 解析为 `FAngelscriptTypeUsage{ Type=OptionalType, SubTypes=[IntType] }`；
3. AS 端可写 `MyObj.RetryCount.IsSet()` / `MyObj.RetryCount.Get(0)`。

### 5.5 `FOptionalPropertyLayout` —— 内存布局可变

测试 helper `AngelscriptReflectiveAccess.h:486-490` 中的注释说明了一个重要事实：

> AS `TOptional<T>` fields reflect as FOptionalProperty, whose layout can vary between "is-set bool after the value" and "intrusive unset state", depending on the inner property; the FOptionalPropertyLayout base class hides that.

UE 端 `FOptionalProperty` 对某些 inner 类型用 "intrusive sentinel" 表示 unset（如 ObjectProperty 用 `nullptr` 作 sentinel，省 1 字节）；对其他类型回退到 "value + bool" 布局——和 AS 内部的 `[T][bool]` 内联布局一致但不必然字节对齐相同。读取必须经 `OptProp->IsSet(Address)` / `OptProp->GetValuePointerForRead(Address)` 抽象，**不能假设固定偏移**。

> AS 这边自管的 `FAngelscriptOptional` 永远是 `[T][bool]` 布局——但这只在 AS 模板实例化的栈/对象成员中成立。如果脚本访问的是宿主 C++ 提供的 UPROPERTY，反射桥负责差异隐藏。

---

## 六、与 `TWeakObjectPtr` / `TSubclassOf` 的语义差异

三种"可空"包装常被混淆。下表把它们的语义本质并列：

| 维度 | `TOptional<T>` | `TWeakObjectPtr<T>` | `TSubclassOf<T>` |
|------|----------------|---------------------|------------------|
| **持有什么** | 完整 T 值（POD 或对象） | 指向 UObject 的弱句柄 | UClass* 指针 |
| **T 的约束** | 任意 T（int / FString / UObject*） | T 必须 derived from UObject | T 必须 derived from UObject |
| **失效原因** | 显式 `Reset` / 默认构造 | GC 回收目标 / 显式赋 nullptr | 显式赋 nullptr |
| **空状态查询** | `IsSet()` | `IsValid()` / `IsStale()` / `IsExplicitlyNull()` | `Get() == nullptr` |
| **空状态读取** | `GetValue()` 抛异常 / `Get(Default)` 兜底 | `Get()` 返回 nullptr | `Get()` 返回 nullptr |
| **GC 行为** | 持有 UObject* 时**强引用** | **弱引用，不阻止 GC** | UClass 不参与 GC |
| **底层注册** | 单 facade `FAngelscriptOptionalType` | facade `FWeakObjectPtrType` 在 `Bind_BlueprintType.cpp` | 同一文件 |
| **协变** | `asOBJ_TEMPLATE_SUBTYPE_COVARIANT` | 同 | 同 |
| **反射属性** | `FOptionalProperty` (UE 5.5+) | `FWeakObjectProperty` | `FClassProperty` |
| **可作 UFunction 参数** | **否** (`CanBeArgument = false`) | 是 | 是 |
| **可作 UFunction 返回值** | 是 | 是 | 是 |

### 6.1 关键概念差：弱引用 vs 可空值

`TWeakObjectPtr<AActor> Ref` 是"我**记下了**这个 Actor，但它**可能被 GC 回收**——访问前必须验证"。语义重点在"我无法控制对象生命周期"。

`TOptional<AActor*> Opt` 是"我**有时**有 AActor 指针，**有时没有**——是不是已经设过我自己说了算"。语义重点在"是否曾被赋值"。

如果 `Opt = MyActor`，然后 `MyActor` 被 GC 销毁，`Opt.IsSet()` **仍然返回 true**——但 inner 的 `AActor*` 已成野指针。**`TOptional` 不参与 GC 跟踪 UObject 指针的有效性**（它只是值容器）。要避免野指针，应使用 `TOptional<TWeakObjectPtr<AActor>>`（嵌套——允许，因为 TWeakObjectPtr 不是容器）。

### 6.2 何时选谁

```text
我要表达                                选什么
─────────────────────────────────────  ──────────────────────────
"这个 Actor 可能被关卡卸载了"           TWeakObjectPtr<AActor>
"这个 UClass 字段允许蓝图选择"          TSubclassOf<UMyComponent>
"这次重试可能没有上次的结果"             TOptional<int32>
"这个字符串字段可能为空"                FString（直接用空串） 或 TOptional<FString>
"这个 Actor 的某个属性可能未设"         TOptional<FVector>
"这个外部 Actor 可能没了 + 我自己也可空" TOptional<TWeakObjectPtr<AActor>>
```

---

## 七、限制、避坑与常见错误

### 7.1 `CanBeArgument = false` —— 不能直接作 UFunction 参数

```cpp
virtual bool CanBeArgument(const FAngelscriptTypeUsage& Usage) const override { return false; }
```

意味着以下**不合法**：

```angelscript
UFUNCTION(BlueprintCallable)
void MyFunc(TOptional<int> OptArg)   // ✗ 编译期报错: TOptional cannot be a UFunction argument
{
}
```

绕开方式：
- 改用两个参数：`bool bHasValue, int Value` 显式传递；
- 改用 `FOptionalIntStruct`（自定义 USTRUCT 包一层）；
- 或把 TOptional 作为返回值（**这是允许的**，`CanBeReturned = true`）。

> 此限制本质来自 AS 调用约定——TOptional 因其内联布局 + 子类型动态大小，不被 SetArgument 路径支持。`SetArgument` 内部直接 `check(false)`：
>
> ```cpp
> virtual void SetArgument(/*...*/) const override { check(false); }
> ```

### 7.2 不能作为容器子类型

```angelscript
TArray<TOptional<int>> Arr;     // ✗ 编译期报错
TMap<int, TOptional<FString>> M; // ✗ 同
TOptional<TArray<int>> O;        // ✗ 同
TSet<TOptional<int>> S;          // ✗ 同
```

`CanBeTemplateSubType() = false` —— 与 `TArray` / `TMap` / `TSet` 的同名标志一致：所有容器（含 TOptional）互不嵌套。

变通：用 USTRUCT 包一层。

```angelscript
struct FOptionalIntWrapper
{
    bool bHasValue;
    int Value;
}
TArray<FOptionalIntWrapper> Arr;   // ✓
```

### 7.3 未检查 IsSet 直接 GetValue → AS 异常

```angelscript
TOptional<int> O;
int X = O.GetValue();   // ✗ 运行时抛 AS 异常: GetValue() called on Optional when not set
```

正确写法：

```angelscript
// 方式 1: 显式检查
if (O.IsSet())
{
    int X = O.GetValue();
    // ...
}

// 方式 2: Get + 默认值（推荐——零异常成本）
int X = O.Get(0);

// 方式 3: 写入前确保 Set
O.Set(42);
int X = O.GetValue();   // 安全
```

错误信息会清晰指出：

```
GetValue() called on Optional when not set! Check the optional with IsSet() first.
```

### 7.4 `TOptional<UObject>(nullptr)` —— "持有 nullptr" 是 Set 状态

测试 `OptObject_NullSet_IsSet` (`AngelscriptOptionalBindingsTests.cpp:283-287`) 验证了一个易混淆事实：

```angelscript
TOptional<UObject> O(nullptr);   // 调用 ImplicitConstructor，传入 nullptr 作为值
ensure(O.IsSet());                // ★ 返回 true！inner 已被赋值（虽然值是 nullptr）
```

`TOptional` 的语义是"槽位是否被赋过值"，不是"槽位里的值是否非空"。`TOptional<UObject>(nullptr)` 等于"我赋了一个 nullptr 进去"——`IsSet()` 为真，`GetValue()` 返回 nullptr 句柄。

要表达"槽位未赋值"应使用默认构造：

```angelscript
TOptional<UObject> O;            // bIsSet = false
ensure(!O.IsSet());
```

### 7.5 引用类型不合法

AS 不允许 `TOptional<T&>` 或 `TOptional<T&in>`——`T` 必须是值类型或对象句柄类型，不能是引用。这一约束由 `CanCopy` / `CanConstruct` 链路传递（引用本身不可拷贝构造）。

### 7.6 拷贝语义：浅 vs 深取决于 inner

```angelscript
TOptional<FString> A(FString("Hello"));
TOptional<FString> B = A;       // 深拷贝（FString 自身深拷贝）
A.GetValue() = "Modified";
ensure(B.GetValue() == "Hello"); // B 不受影响

TOptional<UObject> X(SomeObj);
TOptional<UObject> Y = X;        // ★ 共享同一个 UObject 指针
ensure(X.GetValue() == Y.GetValue());  // 同一对象
```

inner 是值类型 → 深拷贝；inner 是 UObject 句柄 → 拷贝句柄（不复制对象）。这与 `Usage.SubTypes[0].CopyValue` 的递归行为一致。

### 7.7 `TOptional` 与序列化的边界

`FOptionalProperty` 的序列化由 UE 5.5+ 反射框架接管——AS 这边不参与。脚本作者写 `UPROPERTY() TOptional<...>` 时序列化由引擎完成；写本地变量 `TOptional<...> Local;` 时无序列化语义（生命周期同函数栈）。

---

## 附录 A：API 速查表

### A.1 构造与析构

| 用法 | C++ 入口 | 语义 |
|------|----------|------|
| `TOptional<T> O;` | `Construct` | bIsSet = false，inner 未构造 |
| `TOptional<T> O(value);` | `InitConstruct` | bIsSet = true，inner = value |
| `TOptional<T> O(Other);` | `CopyConstruct` | 同步 Other 状态 |
| `O = Other;` | `OpAssign` | 拷贝 Other（含 bIsSet）|
| `O = value;` | `OpAssignValue` | Set(value) |
| `O.Set(value);` | `Set` | 同上，更显式 |
| `O.Reset();` | `Reset` | 析构 inner，bIsSet = false |
| `~TOptional()` | `Destruct` | 等价 Reset |

### A.2 查询与读取

| 用法 | C++ 入口 | 失败行为 |
|------|----------|----------|
| `O.IsSet()` | `IsSet` | 永不失败 |
| `O.GetValue()` | `GetValue` | **未 Set 时抛 AS 异常** |
| `O.Get(Default)` | `Get` | 未 Set 时返回 Default |
| `O == Other` | `OpEquals` | 比较 bIsSet + inner |

### A.3 内部辅助（不直接暴露）

| 入口 | 作用 |
|------|------|
| `Ops->IsSet(Optional)` | 读 bIsSet 字节 |
| `Ops->Set(Optional, ValuePtr)` | 状态机推进到 Set |
| `Ops->Reset(Optional)` | 状态机回退到未 Set |
| `Ops->GetValuePtr(Optional)` | inner 起始地址 |
| `Ops->GetIsSetPtr(Optional)` | bIsSet 字节地址 |

### A.4 类型 facade 接口表（`FAngelscriptOptionalType`）

| 接口 | 答 | 含义 |
|------|----|------|
| `CanBeTemplateSubType` | 否 | 不能嵌套到容器 |
| `CanBeArgument` | 否 | 不能作 UFunction 参数 |
| `CanBeReturned` | 是 | 可作 UFunction 返回值 |
| `CanCopy` | 取决于 inner | 委托 inner.CanCopy |
| `CanCompare` | 取决于 inner | 委托 inner.CanCompare |
| `CanCreateProperty` | 取决于 inner | 委托 inner.CanCreateProperty |
| `RequiresProperty` | 否 | UPROPERTY 可选 |
| `CanQueryPropertyType` | 否 | 不参与反向类型查询 |

---

## 附录 B：避坑清单与 FAQ

### B.1 避坑要点

1. **`GetValue()` 是断言调用**：未检查 `IsSet()` 直接调会抛 AS 异常——脚本写防御性代码或改用 `Get(Default)`。
2. **`TOptional<T>(nullptr)` 是 Set 状态**：槽位被赋了 nullptr 不等于槽位未赋值。区分二者用默认构造。
3. **不能嵌套到容器**：`TArray<TOptional<T>>` 等不合法，需要包一层 USTRUCT。
4. **不能作 UFunction 参数**：但**可作返回值**——这条是常见踩坑点。
5. **GC 不跟踪 inner UObject 句柄**：`TOptional<UObject>` 持有的对象可能在脚本不知情下被 GC 回收。需要 GC 安全用 `TOptional<TWeakObjectPtr<UObject>>` 或直接用 `TWeakObjectPtr`。
6. **拷贝语义跟随 inner**：FString 深拷贝、UObject 句柄共享指针——别假设统一行为。
7. **UE 5.5+ 才有 `FOptionalProperty`**：在更早版本上 UPROPERTY TOptional 反射可能不可用——核对编译器目标版本。
8. **`FOptionalPropertyLayout` 内存可变**：宿主 C++ 暴露的 UPROPERTY TOptional 不一定是 `[T][bool]` 内联——访问必须经 reflection API，不能假设字节偏移。

### B.2 决策树：什么时候用 TOptional

```text
有一个 T 类型的字段/局部变量, 它可能没有有效值?
│
├─ T 是 UObject 派生类, 关心生命周期?
│   ├─ 是 → TWeakObjectPtr<T>
│   └─ 否, 关心"赋过值没有" → TOptional<T> 或 TOptional<TWeakObjectPtr<T>>
│
├─ T 是 UClass / 某个类型的元数据引用?
│   └─ → TSubclassOf<T>
│
├─ T 是 USTRUCT 实例 (FVector / FString / 自定义 struct)?
│   └─ → TOptional<T>
│
├─ T 是基础类型 (int/float/bool)?
│   ├─ 用某个 sentinel 值 (INDEX_NONE / NAME_None) 够用 → 直接 T
│   └─ 必须区分 "未设置" 与 sentinel → TOptional<T>
│
└─ 是 UFunction 参数?
    └─ ✗ 不能用 TOptional, 改用两个参数或 USTRUCT 包装
```

### B.3 调试技巧

- **断点位置**：`FAngelscriptOptionalBinds::GetValue` —— 抓"未 Set 抛异常" 路径。
- **DAP 调试器面板**：`GetDebuggerValue` 会显示 `Set: 42` 或 `Unset`，子值可展开（见 §2.1 接口表中的 `GetDebuggerValue/Scope/Member`）。
- **Log 输出**：`Log("Opt: " + O.GetValue())` 在已 Set 时正常，未 Set 时抛异常——如果只想观察状态用 `Log("Opt IsSet: " + O.IsSet())`。
- **测试模板**：仿照 `RunOptionalApiCoverageSection` 的写法可快速覆盖一个新 inner 类型的全 API。

---

## 小结

- **TOptional<T> = "T 或没有 T"**：单 facade `FAngelscriptOptionalType` 实现，与 std::optional / Rust Option / Maybe 同语义。
- **内联布局 `[T][bIsSet][padding]`**：`GetValueSize = align(sizeof(T)+1, align(T))`；`FAngelscriptOptional` 是空 struct 占位符，靠 TypeSize 偏移寻址。
- **三件套 `IsSet / Get / Reset`**：`GetValue()` 未 Set 抛 AS 异常；`Get(Default)` 永不失败；`Reset` 析构 inner 并清位。
- **状态机四路转换**：默认构造（仅清位）→ Set（构造或覆盖 inner）→ Reset（析构 inner 清位）→ 析构（等价 Reset）。
- **与 TWeakObjectPtr 的本质差异**：弱引用 = 对象 GC 生命周期；TOptional = 槽位是否被赋值——TOptional 不参与 GC 有效性跟踪，inner 持 UObject 时需要嵌套 TWeakObjectPtr 来表达"两层可空"。
- **关键限制**：① 不能作 UFunction 参数（`CanBeArgument = false`）但可作返回值；② 不能嵌套到容器；③ inner 必须可拷贝构造析构；④ `TOptional<T>(nullptr)` 是 Set 状态。
- **UE 反射桥**：UE 5.5+ 的 `FOptionalProperty` 提供反射支持；`CreateProperty` / `MatchesProperty` / `RegisterTypeFinder` 形成双向桥；`CPF_NonNullable` 标志会被反向解析显式跳过。

---

## 修订记录

| 版本 | 日期 | 内容 |
|------|------|------|
| v1.0 | 2026-05-24 | 首版：基于 `Bind_TOptional.cpp` 完整 facade（`FAngelscriptOptionalType` + `FOptionalOperations` + `FAngelscriptOptionalBinds`）+ `Bind_TOptional.h` + `AngelscriptOptionalBindingsTests.cpp` 行为契约 + `Template_ReflectionAccess.cpp` 中 UPROPERTY 用法 + `AngelscriptReflectiveAccess.h` 反射访问辅助。覆盖：① 物理布局 `[T][bIsSet]` 内联存储与 `GetValueSize` 公式；② facade 19 个接口职责对照表；③ `ConstructValue` / `CopyValue` 状态机四路分支；④ GC schema `EMemberType::Optional`；⑤ `FOptionalOperations::Set/Reset` 状态机推进；⑥ AS 端 14 条注册签名（构造/析构/赋值/比较/IsSet/Set/GetValue/Get/Reset）；⑦ `GetValue` 抛异常 vs `Get(Default)` 兜底的契约差异；⑧ `if_handle_then_const` 修饰符与 `SCRIPT_NATIVE_TEMPLATED_CALL_NEEDSCOPY` / `PreviousBindPassScriptObjectTypeAsFirstParam` 注册细节；⑨ UE 反射桥（`FOptionalProperty` 创建 + `RegisterTypeFinder` 反向解析 + `CPF_NonNullable` 守卫 + `FOptionalPropertyLayout` 内存可变性提醒）；⑩ 与 `TWeakObjectPtr` / `TSubclassOf` 的语义差异对照表 + 选型决策树；⑪ 7 项关键限制（CanBeArgument=false、不可嵌套、未检查抛异常、nullptr 是 Set 状态、引用类型禁用、拷贝随 inner、序列化由反射代理）。 |
