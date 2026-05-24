# Syntax_TSet — `TSet<T>` 哈希集合实现原理

> **所属前缀**: Syntax_（容器类型族）
> **关注层面**: 语法机制与实现原理（不写"使用指南"，不重复 `Syntax_TMap.md` 已细写的共享底层）
> **关键源码**:
> `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TSet.cpp` (~780 行)
> · `Binds/Bind_TSet.h` (~470 行) —— `FSetOperations` / `FSetIterator` / `FAngelscriptSetBinds`
> · `Containers/Set.h`（UE 引擎自带 `FScriptSet` / `FScriptSetLayout`）
> · `Core/AngelscriptType.h:277` —— `CanHashValue` / `GetHash` 接口
> · `StaticJIT/StaticJITBinds.h` —— `SCRIPT_NATIVE_TEMPLATED_CALL_*` 宏（与 TMap 共享）
> **关联文档**:
> `Documents/Knowledges/ZH/Syntax_TMap.md` —— **最近兄弟**：共享 `FScriptSet` 底层、键哈希约束、`StructSet` GC schema、StaticJIT 宏体系，本文反复对照
> · `Documents/Knowledges/ZH/Syntax_TArray.md` —— 容器族另一个兄弟，对照 `<T>` 单子类型骨架
> · `Documents/Knowledges/ZH/Type_BindSystem.md` —— `FAngelscriptType` / `FAngelscriptTypeUsage` 多态分派
> · `Documents/Knowledges/ZH/Type_FunctionCaller.md` —— `asCALL_THISCALL_OBJLAST` 调用约定
> · `Documents/Knowledges/ZH/Syntax_UPROPERTY.md` —— `FSetProperty` 创建链路

---

## 概览：`TSet<T>` 是 `TMap<K, void>` 的退化形态

`TSet<T>` 在当前插件里与 `TMap<K, V>` **共享同一份哈希底层基础设施**——`FScriptMap` 在 UE 引擎内部就是 `FScriptSet<TPair<K,V>>` 的一层壳，所以 `Bind_TSet.cpp` 大体上是把 `Bind_TMap.cpp` 中"键的部分"原样保留、删去"值的部分"得到的产物。读者若已读过 `Syntax_TMap.md`，本文 ⅖ 的内容可以快速跳过；本文重点放在两者的**差异**上。

```text
[本质 1] 单子类型 <T> 而非双子类型 <K,V>
    -> Flags.TemplateType = "<T>"
    -> ValidateSetOperations 仅校验一个子类型 (但要求与 TMap 键完全相同的能力)
    -> FSetOperations 单组 size/alignment + 单组 bNeedConstruct/Copy/Destruct
    -> CreateProperty 创建 FSetProperty 时只设 ElementProp (vs FMapProperty 的双 inner)

[本质 2] 元素既是键, 没有"值"
    -> 没有 OpIndex (Map 有, Set 没有: 按键查值无意义)
    -> 没有 Find / FindOrAdd / RemoveAndCopyValue (元素 == 键, Contains 已足够)
    -> 没有 GetKeys / GetValues (元素本身就是结果)
    -> 没有 opForKey (foreach 仅 opForValue, 给每个元素一次访问)
    -> TSetIterator::Proceed() 直接返回 const T& (vs FMapIterator 拆 GetKey/GetValue)

[本质 3] 多了"集合代数"风格的批量操作
    -> Append(const TArray<T>&)  -- TArray -> TSet 去重合并
    -> Append(const TSet<T>&)    -- TSet  -> TSet 并集 (差异: TMap 没有这两个方法)
```

```text
TSet<T> 总览管线
================================================================================
[启动] AS Engine 初始化
   Bind_TSet 走 EOrder::Early+1 (与 TMap 同, 因 Append(TArray<T>&) 依赖 TArray)
     - ValueClass<FScriptSet>("TSet<class T>")
     - Constructor / Destructor
     - 注册 Add / Contains / Remove / Append(Arr) / Append(Set) / opAssign / opEquals / Empty / Reset / Num / IsEmpty
     - 注册 opForBegin/End/Next/Value 四件套 (没有 opForKey)
     - 注册 TSetIterator<T> / TSetConstIterator<T>
     - FAngelscriptType::Register(FAngelscriptSetType)
     - RegisterTypeFinder(FSetProperty -> FAngelscriptTypeUsage)
        |
        v
[AS 编译期] 第一次见 TSet<int>
   asCBuilder 实例化 -> TemplateCallback -> ValidateSetOperations
     - Type = FAngelscriptTypeUsage{int}
     - 校验:
         Type.CanBeTemplateSubType()    (拒绝嵌套容器)
         Type.CanHashValue()            (拒绝无 GetTypeHash 的元素)
         Type.CanConstruct/Destruct/Copy/Compare()
     - new FSetOperations(Type)
     - 计算 FScriptSetLayout (单纯的 sparse-array slot 大小/对齐)
     - SetUserData(Ops) 挂到 asCObjectType::plainUserData
        |
        v
[AS 运行期] 操作分派
     [a] FAngelscriptSetBinds::Add(Set, Meta, &Value)
            -> Ops = GetSetOperations(Meta)
            -> Ops->Add(Set, Value)  (调 FScriptSet::Add 的 4-lambda 重载)
                                     -- 比 TMap::Add 的 7-lambda 少 3 个 (无 Value 路径)
     [b] FAngelscriptSetBinds::Contains(Set, Meta, &Value)
            -> Ops->FindIndex(Set, Value) != INDEX_NONE
     [c] for (auto E : Set) -> Iterator() 走 TSetIterator<T> 隐式路径
            -> Proceed() 返回当前元素引用并预读 NextIndex
        |
        v
[GC 集成] TSet<UObject*> 含引用时
   FAngelscriptSetType::EmitReferenceInfo
     -> 计算 FScriptSetLayout (无 ValueOffset; Pair 退化为单元素)
     -> 用 FSchemaBuilder 构造 InnerSchema:
          Type.HasReferences() -> Inner Schema 在 offset 0 加引用
     -> 顶层 EMemberType::StructSet (与 TMap 完全一致, 这是名字符合 Set 本质)
```

### 三层防御网

| 层 | 触发点 | 报错语句 |
|---|--------|----------|
| 编译期 | `ValidateSetOperations` 拒绝嵌套 / 不可哈希 / 不可构造 | `"Containers cannot be nested in other containers"` / `"Key type does not have a hash function defined"` / `"Subtype cannot be constructed or copied"` |
| 运行期键不存在 | `Remove`/`Contains` 内部 `FindIndex == INDEX_NONE` | （无 Throw，安静返回 `false`；TSet 没有 `OpIndex` 这种"找不到就抛错"的入口） |
| 迭代守卫 | `AS_ITERATOR_DEBUGGING` 全局表 `GSetsBeingIterated` | `"TSet is being modified during for loop iteration"` |

### 核心特性速览

| 特性 | AS 语法 | 实现策略 |
|------|--------|---------|
| **声明** | `TSet<int> Visited;` | `ValueClass<FScriptSet>` + `<T>` 模板 |
| **插入** | `Visited.Add(42);` | `FSetOperations::Add` 调 `FScriptSet::Add` 4-lambda 重载 |
| **判存** | `Visited.Contains(42)` | `FindIndex != INDEX_NONE` |
| **删除** | `Visited.Remove(42)` | `FindIndex` + `Ops->RemoveAt`，返回 `bool` |
| **批量插入数组** | `Visited.Append(SourceArray)` | 遍历 `FScriptArray` 逐个 `Add`（去重） |
| **批量并集** | `Visited.Append(OtherSet)` | 同上，但走 `FScriptSet` 遍历 |
| **范围 for** | `for (auto E : Set) { ... }` | 稀疏索引 + `opForBegin/End/Next/Value` 四件套 |
| **比较** | `A == B` | `IsPermutation` 多重计数比对（无序） |
| **GC 追踪** | `TSet<UObject*>` 自动追踪 | `EmitReferenceInfo` -> `EMemberType::StructSet` |
| **嵌套禁止** | `TSet<TArray<int>>` ✗ | `CanBeTemplateSubType() = false` |
| **`FSetProperty` 桥接** | `UPROPERTY() TSet<FName>` | `CreateProperty` 创建 `FSetProperty + ElementProp` |
| **缺失：按值取** | （无 `OpIndex` 入口） | Set 元素既是键又是值，访问入口在 `for` / `Contains` |
| **缺失：FindOrAdd** | （无） | Set 是幂等集合：第二次 `Add(同一元素)` 自动忽略，无须"找或加" |

后续章节按 注册时机 → 元素元数据 → 元素类型约束 → 反射桥接 → 操作实现 → 批量并集 → for 与迭代器 → 调试器 → JIT 直通 → 限制与对照 → 设计哲学 顺序展开。

---

## 一、绑定入口：`EOrder::Early+1` 与单模板登记

**源码所在**：`Bind_TSet.cpp:559-726` 的 `Bind_TSet` 全局静态绑定块。

### 1.1 类型注册框架

```cpp
// ============================================================================
// 文件: AngelscriptRuntime/Binds/Bind_TSet.cpp
// 函数: Bind_TSet (AS_FORCE_LINK 全局静态)
// ============================================================================
AS_FORCE_LINK const FAngelscriptBinds::FBind Bind_TSet((int32)FAngelscriptBinds::EOrder::Early+1, []
{
    FBindFlags Flags;
    Flags.bTemplate = true;
    Flags.TemplateType = "<T>";                              // ★ 单子类型, 与 TArray 同
    Flags.ExtraFlags = asOBJ_TEMPLATE_SUBTYPE_COVARIANT;     // 允许子类型协变

    auto TSet_ = FAngelscriptBinds::ValueClass<FScriptSet>("TSet<class T>", Flags);
    TSet_.Constructor("void f()", FUNC_TRIVIAL(FAngelscriptSetBinds::Construct));
    TSet_.Destructor("void f()", &FAngelscriptSetBinds::Destruct);
    SCRIPT_NATIVE_TEMPLATED_CALL(TSet_, "FAngelscriptSetBinds::Destruct", false);
    FAngelscriptBinds::PreviousBindPassScriptObjectTypeAsFirstParam();
    // ...
});
```

四个关键调用与 `TMap` 一一对应，但参数表退化为单类型：

| 调用 | 与 TMap 的差异 |
|------|---------------|
| `Flags.bTemplate = true` + `TemplateType = "<T>"` | `<T>` 而非 `<K,V>`；`ValidateSetOperations` 只查 `GetSubTypeId(0)` |
| `asOBJ_TEMPLATE_SUBTYPE_COVARIANT` | 与 `TArray`/`TMap` 同，启用子类型协变 |
| `Constructor` 走 `FUNC_TRIVIAL` | `placement-new FScriptSet()`，与 TMap 同 |
| `Destructor` 配套 `SCRIPT_NATIVE_TEMPLATED_CALL` | 析构需要遍历元素调 `DestructValue`，所以 JIT 路径需要原生 `~TSet<T>()` 模板版本 |

### 1.2 `EOrder::Early+1` —— 与 TMap 同列

`Bind_TSet` 的注册顺序也是 `(int)EOrder::Early+1`，与 `Bind_TMap` 同列、**晚于 `Bind_TArray`**。原因：

- `TSet` 的 `Append(const TArray<T>& Array)` 方法签名直接出现 `TArray<T>`；
- AS 编译这个签名时需要 `TArray` 模板已注册；
- 所以 `TSet` 必须在 `TArray` 完成注册之后再注册。

注意：`TMap` 与 `TSet` 都走 `Early+1`——**这两者之间没有强制顺序依赖**，因为 `TMap` 的方法签名不出现 `TSet`，反之亦然。它们是平级注册。

### 1.3 `TemplateCallback` —— 元素类型守门员

```cpp
// ============================================================================
// 文件: AngelscriptRuntime/Binds/Bind_TSet.cpp
// 函数: ValidateSetOperations (TemplateCallback 内部)
// ============================================================================
TSet_.TemplateCallback("bool f(int&in Type, int&out ErrorMessage)",
    [](asITypeInfo* TemplateType, asCString* ErrorMessage) -> bool
{
    return ValidateSetOperations(TemplateType, ErrorMessage);
});
```

`ValidateSetOperations` 在每次首次实例化 `TSet<具体T>` 时调用一次。它的拒绝点与 `TMap::ValidateMapOperations` 的"键"部分**完全一致**：

```text
TSet<int>                       -> 通过 (int 既可哈希又可比较)
TSet<FName>                     -> 通过
TSet<AActor>                    -> 通过 (UObject 派生类指针)
TSet<TArray<int>>               -> 拒: "Containers cannot be nested in other containers"
TSet<FText>                     -> 拒: "Key type does not have a hash function defined"
TSet<FCustomStructNoHash>       -> 拒: 同上 (除非脚本 struct 实现 uint32 Hash() const)
TSet<FAbstract>                 -> 拒: "Subtype cannot be constructed or copied"
```

**关键观察**：TSet 元素的约束 = TMap **键**的约束。可以把 `TSet<T>` 心智模型固化为 `TMap<T, void>`：T 当键看待，所有"键约束"原样适用。

---

## 二、`FSetOperations` —— 元素元数据缓存与桥接 `FScriptSet`

**源码所在**：`Bind_TSet.h:17-261`、`Bind_TSet.cpp:728-778`（`ValidateSetOperations`）。

### 2.1 数据结构

```cpp
// ============================================================================
// 文件: AngelscriptRuntime/Binds/Bind_TSet.h
// 角色: 元素元数据缓存 + FScriptSet 操作入口
// ============================================================================
struct ANGELSCRIPTRUNTIME_API FSetOperations
{
    bool bValid;

    FAngelscriptTypeUsage Type;             // 元素类型描述符
    FScriptSetLayout      Layout;           // ★ UE 引擎计算的元素槽布局
    int32                 ValueSize;        // 等价于 Type.GetValueSize() 的缓存

    bool bNeedConstruct;
    bool bNeedDestruct;
    bool bNeedCopy;

    asIScriptFunction* HashFunction;        // ★ 脚本侧 uint32 Hash() const, 缺省 nullptr 走 GetTypeHash

    static FORCEINLINE FSetOperations* GetSetOperations(asCObjectType* Meta)
    {
        return (FSetOperations*)Meta->plainUserData;
    }
    // ...
};
```

与 `FMapOperations` 的对照——这是 `TSet` vs `TMap` 在**字段层**的全部区别：

| 字段 | `FMapOperations` | `FSetOperations` | 差异 |
|------|------------------|------------------|------|
| 元素类型 | 双重 `KeyType` + `ValueType` | 单一 `Type` | ★ |
| 内存布局 | `FScriptMapLayout`（含 `ValueOffset`） | `FScriptSetLayout`（无 ValueOffset） | ★ |
| Size/Alignment | 两套（`KeySize`/`KeyAlignment` + `ValueSize`/`ValueAlignment`） | **一套**（`ValueSize` + 来自 `Type.GetValueAlignment()`） | ★ |
| POD 标志 | 两组（`bKey*` / `bValue*`） | **一组**（无前缀） | ★ |
| 哈希函数 | `HashFunction*` | `HashFunction*` | ✓ 完全相同 |
| 元素地址入口 | `GetKey(Map, Index)` + `GetValue(Map, Index)` | **`GetElement(Set, Index)` 单入口** | ★ |
| 比较函数 | （无；走 `KeyType.IsValueEqual`） | （无；走 `Type.IsValueEqual`） | ✓ 共享模式 |

字段数量上 `FSetOperations` ≈ `FMapOperations` × ½——这是"双子类型 → 单子类型"退化的直接体现。

### 2.2 `FScriptSetLayout`：UE 引擎给的元素槽布局

```cpp
Layout = FScriptSet::GetScriptLayout(ValueSize, Type.GetValueAlignment());
```

`FScriptSetLayout` 比 `FScriptMapLayout` **简单一层**——它本身就是 `FScriptMapLayout::SetLayout` 字段的内容（`SparseArrayLayout` + 哈希桶大小）。`FScriptMap` 之所以多一层 `KeyOffset/ValueOffset` 是因为它把 `TPair<K, V>` 当作 set 元素塞进 `FScriptSet`；而 `FScriptSet` 直接以 T 自身为元素，不需要 pair 内的偏移。

`GetElement` 实现因此简单一行：

```cpp
FORCEINLINE void* FSetOperations::GetElement(FScriptSet& Set, int32 Index)
{
    return Set.GetData(Index, Layout);                       // ★ 直接是元素地址
}
```

`FScriptSet::GetData` 内部按 `SparseArrayLayout` 回算槽位地址，跨 `Set.GetMaxIndex()` 范围**包含空闲槽**——这是后文 `for` 循环必须用 `IsValidIndex` 过滤的根因（与 `TMap` 同）。

### 2.3 `ValidateSetOperations` —— 一次构造、终生复用

```cpp
// ============================================================================
// 文件: AngelscriptRuntime/Binds/Bind_TSet.cpp
// 函数: ValidateSetOperations
// ============================================================================
bool ValidateSetOperations(asITypeInfo* TemplateType, asCString* ErrorMessage)
{
    FSetOperations* Ops = (FSetOperations*)TemplateType->GetUserData();
    if (Ops != nullptr)
        return Ops->bValid;                                     // 已构造 -> 直接复用

    int32 SubTypeId = TemplateType->GetSubTypeId(0);
    auto Type = FAngelscriptTypeUsage::FromTypeId(SubTypeId);

    // [拦截 1] 嵌套容器
    if (!Type.CanBeTemplateSubType())
    {
        if (ErrorMessage != nullptr)
            *ErrorMessage = "Containers cannot be nested in other containers";
        return false;
    }

    Ops = new FSetOperations(Type);

    // [拦截 2] 元素必须可哈希
    bool bCanHash = Type.CanHashValue();
    if (!bCanHash)
    {
        // 退路: 脚本类是否定义了 uint32 Hash() const 方法?
        if (asCTypeInfo* SubType = (asCTypeInfo*)TemplateType->GetSubType(0))
        {
            auto* ObjectType = CastToObjectType(SubType);
            if (ObjectType != nullptr && ObjectType->GetFirstMethod("Hash") != nullptr)
            {
                Ops->HashFunction = SubType->GetMethodByDecl(TCHAR_TO_ANSI(*FString(TEXT("uint32 Hash() const"))));
                bCanHash = Ops->HashFunction != nullptr;
            }
        }
    }

    // [拦截 3] 构造/析构/拷贝/比较缺一不可
    Ops->bValid = Type.CanConstruct() && Type.CanDestruct() && Type.CanCopy()
                && Type.CanCompare() && bCanHash;
    TemplateType->SetUserData(Ops);

    if (!Ops->bValid && ErrorMessage != nullptr)
        *ErrorMessage = bCanHash ? "Subtype cannot be constructed or copied"
                                  : "Key type does not have a hash function defined";
    return Ops->bValid;
}
```

注意三个关键点（与 `TMap` 完全同步）：

- **`Ops` 即使最终 `bValid = false` 也被挂到 `userData`**：避免重复构造，`bValid` 字段足以让后续调用直接拒绝。
- **`Type.CanCompare()` 必须为真**：哈希冲突时桶内查找需要 `opEquals`。
- **错误信息复用 TMap 的 `"Key type does not have a hash function defined"`**：从用户视角，`TSet<T>` 的 T 在错误消息中也被称为 "Key"——文案一致而非另起新词。

---

## 三、元素类型约束：哪些类型可作 `T`

`TSet` 元素的约束 ≡ `TMap` 键的约束。这一节是 `Syntax_TMap.md §三` 的浓缩版——若已读过 TMap 文档可快速跳过。

### 3.1 `CanHashValue` 接口与重写者

详见 `Syntax_TMap.md §3.1`。要点速记：

| 重写者 | 覆盖范围 |
|--------|----------|
| `TAngelscriptCppType<NativeType>` | `int32` / `float` / `FName` / `FString` / 任何特化 `GetTypeHash` 的 C++ 类型 |
| `TAngelscriptPODType<...>` | `EnumProperty` 包装的 POD enum |
| `FAngelscriptStructType` | UHT 标记 `WithGetTypeHash` 的 USTRUCT |
| `FAngelscriptEnumType` | 任何 `UENUM()` |
| `FAngelscriptBlueprintType` | UClass `*`（指针哈希） |

### 3.2 脚本侧 `struct` 作为元素：`uint32 Hash() const` 退路

如果 T 是脚本侧定义的 `struct` 或 `class`，必须显式实现 `uint32 Hash() const`，否则 `ValidateSetOperations` 拒绝：

```angelscript
// AS 端示例
struct FRoomCoord
{
    int X;
    int Y;

    bool opEquals(const FRoomCoord& Other) const { return X == Other.X && Y == Other.Y; }
    uint32 Hash() const { return uint32(X * 73856093) ^ uint32(Y * 19349663); }
}

TSet<FRoomCoord> VisitedRooms;            // ★ 通过, 因为有 opEquals + Hash
VisitedRooms.Add(FRoomCoord(1, 2));
```

`FSetOperations::HashFunction` 字段在 `ValidateSetOperations` 中被赋值为 `asIScriptFunction*`，运行时 `GetHash` 走反射调用：

```cpp
// Bind_TSet.h
FORCEINLINE uint32 FSetOperations::GetHash(const void* Ptr) const
{
    if (this->HashFunction != nullptr)
        return this->InvokeHashFunction(Ptr);              // 脚本侧 Hash()
    return this->Type.GetHash(Ptr);                        // 内置 GetTypeHash
}

uint32 FSetOperations::InvokeHashFunction(const void* Object) const
{
    FAngelscriptContext Context(HashFunction->GetEngine());
    PrepareAngelscriptContextWithLog(Context, HashFunction, ...);
    Context->SetObject(const_cast<void*>(Object));         // ★ this 指针
    Context->Execute();                                    // ★ 反射执行
    return Context->GetReturnDWord();
}
```

性能代价同 `TMap`——每次 `Add/Contains/Remove` 触发一次 AS 反射调用，比 `int`/`FName` 这类内置 `GetTypeHash` 慢 1-2 个量级。

### 3.3 元素类型选择决策表

与 `TMap` 键完全相同。复用 `Syntax_TMap.md §3.3` 表格的结构如下：

| 元素类型 | 可用？ | 备注 |
|---------|--------|------|
| `int` / `int64` / `uint32` | ✓ | 最快 |
| `float` / `double` | ✓ | 浮点精度陷阱：`0.1 + 0.2 ≠ 0.3` 哈希到不同槽 |
| `FName` | ✓ | 推荐——名字索引哈希 |
| `FString` | ✓ | 比 `FName` 慢；按内容哈希 |
| `FText` | ✗ | 无 `GetTypeHash` —— 编译期拒绝 |
| `UObject*` 派生类 | ✓ | 指针哈希，注意弱引用陷阱 |
| `TSubclassOf<>` | ✓ | UClass 指针哈希 |
| `USTRUCT() FVector` | ✗（默认） | 无 `WithGetTypeHash` |
| `USTRUCT() FGuid` | ✓ | UE 标准提供 GetTypeHash |
| 脚本 `struct FXxx` | 条件✓ | 必须实现 `uint32 Hash() const` |
| `TArray/TSet/TMap<...>` | ✗ | 嵌套容器禁止 |

---

## 四、`FAngelscriptSetType` —— UE 反射桥接

**源码所在**：`Bind_TSet.cpp:57-356`。

### 4.1 接口职责映射

| 接口 | 实现 | 与 TMap 的差异 |
|------|------|----------------|
| `GetAngelscriptTypeName` | 返回 `"TSet"` | （Map 返回 `"TMap"`） |
| `CanQueryPropertyType` | `return false` | 同 |
| `CanBeTemplateSubType` | `return false` | 同——禁止嵌套容器 |
| `HasReferences` | `Usage.SubTypes[0].HasReferences()` | Map 是 `Key.HasReferences() OR Value.HasReferences()` |
| `EmitReferenceInfo` | 见 §4.2 —— 单 sub-Schema 在 offset 0 | Map 双 sub-Schema（Key 在 0、Value 在 ValueOffset） |
| `CanCreateProperty` | `Usage.SubTypes[0].CanCreateProperty() && CanHashValue()` | Map 还要 `Value.CanCreateProperty()` |
| `CreateProperty` | `new FSetProperty + ElementProp + SetLayout` | Map 双 inner（KeyProp + ValueProp） |
| `MatchesProperty` | 对 `FSetProperty.ElementProp` 单向递归 | Map 双向递归 |
| `CanCopy/NeedCopy/CopyValue` | 必须深拷贝（遍历元素 `Add` 到目标） | 同模式，单一元素遍历 |
| `CanConstruct/ConstructValue` | placement-new `FScriptSet()` | 同模式 |
| `CanDestruct/DestructValue` | `Empty(Set, 0)` 析构所有元素 + `~FScriptSet()` | 同模式 |
| `GetValueSize/Alignment` | `sizeof(FScriptSet) / alignof(FScriptSet)` | 类型不同 |
| `SetArgument/GetReturnValue` | `StepCompiledIn<FSetProperty>` | Property 类不同 |
| `CanCompare/IsValueEqual` | `Ops.IsPermutation` 多重计数比对 | 同算法 |
| `GetDebuggerValue/Scope/Member` | 见 §八 | TypeIndex 编码退化为单层 |
| `GetCppForm` | 输出 `TSet<T_CppType>` | 同模式 |

### 4.2 GC Schema：单 sub-Schema 的 `EMemberType::StructSet`

```cpp
// ============================================================================
// 文件: Bind_TSet.cpp
// 函数: FAngelscriptSetType::EmitReferenceInfo
// ============================================================================
void EmitReferenceInfo(const FAngelscriptTypeUsage& Usage, FGCReferenceParams& Params) const override
{
    auto SetLayout = FScriptSet::GetScriptLayout(
        Usage.SubTypes[0].GetValueSize(), Usage.SubTypes[0].GetValueAlignment());

    UE::GC::FSchemaBuilder InnerSchema(SetLayout.Size);
    {
        FGCReferenceParams InnerParams = Params;
        InnerParams.Schema = &InnerSchema;
        InnerParams.AtOffset = 0;                              // ★ 元素在槽起始 (无 KeyOffset/ValueOffset)
        Usage.SubTypes[0].EmitReferenceInfo(InnerParams);
    }

    Params.Schema->Add(UE::GC::DeclareMember(Params.Names.Top(), Params.AtOffset,
        UE::GC::EMemberType::StructSet, InnerSchema.Build()));
}
```

**关键观察**：

- `EMemberType::StructSet` 名字本就来自 `Set`——`TMap` 的 GC schema 也用这个类型，因为它内部本质就是 set。
- TSet 走的是单 `AtOffset = 0`（元素直接在槽起始），TMap 多走一遍 `AtOffset = ValueOffset` 给 Value。
- TSet 没有 `ReferenceSet` 这种快路径——即使 `TSet<UObject*>`，GC 也走 `StructSet` 进入 sub-Schema。

### 4.3 `CreateProperty` —— `FSetProperty` 单 inner

```cpp
// ============================================================================
// 文件: Bind_TSet.cpp
// 函数: FAngelscriptSetType::CreateProperty
// ============================================================================
FProperty* CreateProperty(const FAngelscriptTypeUsage& Usage, const FPropertyParams& Params) const override
{
    auto* SetProp = new FSetProperty(Params.Outer, Params.PropertyName, RF_Public);

    FPropertyParams InnerParams = Params;
    InnerParams.Outer = SetProp;
    InnerParams.PropertyName = *(Params.PropertyName.ToString() + TEXT("_Element"));

    SetProp->SetLayout = FScriptSet::GetScriptLayout(
        Usage.SubTypes[0].GetValueSize(), Usage.SubTypes[0].GetValueAlignment());
    SetProp->ElementProp = Usage.SubTypes[0].CreateProperty(InnerParams);

    return SetProp;
}
```

UE 约定：`FSetProperty` 子属性命名 `<Field>_Element`（与 `FArrayProperty.Inner` 命名 `<Field>_Inner`、`FMapProperty.KeyProp` 命名 `<Field>_Key` 三足鼎立）。`SetLayout` 在 property 上独立缓存一份——序列化、复制、垃圾收集都依赖它。

### 4.4 反向桥接：`FSetProperty` → `FAngelscriptTypeUsage`

```cpp
FAngelscriptType::RegisterTypeFinder([SetType](FProperty* Property, FAngelscriptTypeUsage& Usage) -> bool
{
    FSetProperty* SetProp = CastField<FSetProperty>(Property);
    if (SetProp == nullptr) return false;

    FAngelscriptTypeUsage InnerUsage = FAngelscriptTypeUsage::FromProperty(SetProp->ElementProp);
    if (!InnerUsage.IsValid()) return false;

    Usage.Type = SetType;
    Usage.SubTypes.Add(InnerUsage);
    return true;
});
```

C++ 端 `UPROPERTY() TSet<FName> Tags;` 会被反向解析为 `FAngelscriptTypeUsage{ Type=SetType, SubTypes=[FName] }`，AS 端 `MyClass.Tags.Add(...)` 直接可用。

### 4.5 `CopyValue` 的特殊性：永远遍历重建

```cpp
void CopyValue(const FAngelscriptTypeUsage& Usage, void* SourcePtr, void* DestinationPtr) const override
{
    FScriptSet& Source = *(FScriptSet*)SourcePtr;
    FScriptSet& Destination = *(FScriptSet*)DestinationPtr;

    FSetOperations Ops(Usage.SubTypes[0]);
    Ops.Empty(Destination, Source.Num());
    for (int32 i = 0, Num = Source.GetMaxIndex(); i < Num; ++i)
    {
        if (Source.IsValidIndex(i))
            Ops.Add(Destination, Ops.GetElement(Source, i));
    }
}
```

与 `TMap::CopyValue` 三个特性同源：

1. **不能 `memcpy` 整块 buffer**：`FScriptSet` 内部是 sparse array + hash 桶，直接复制内存会让多个 `FScriptSet` 实例共享同一份 buffer 后 double-free。
2. **每个元素要走 `Add` 重新哈希**：因为目的端的桶可能与源端布局不同。
3. **POD vs 非 POD 没有快路径**：与 `TArray` 不同，无论 T 是否平凡都走遍历 `Add`。

---

## 五、关键操作实现剖析

### 5.1 `Add` —— 4-lambda 注入式实现（vs TMap 的 7-lambda）

```cpp
// ============================================================================
// 文件: Bind_TSet.h
// 函数: FSetOperations::Add (注入式 lambda 接到 FScriptSet::Add)
// ============================================================================
FORCEINLINE_DEBUGGABLE void FSetOperations::Add(FScriptSet& Set, void* Value)
{
    auto GetKeyHash = [this](const void* Ptr) -> uint32 { return GetHash(Ptr); };
    auto Compare    = [this](const void* A, const void* B) -> bool
        { return Type.IsValueEqual((void*)A, (void*)B); };

    auto Construct = [this, Value](void* Ptr)
    {
        if (bNeedConstruct) Type.ConstructValue(Ptr);
        if (bNeedCopy)      Type.CopyValue(Value, Ptr);
        else                FMemory::Memcpy(Ptr, Value, ValueSize);
    };

    auto Destruct = [this](void* Ptr)
    {
        if (bNeedDestruct) Type.DestructValue(Ptr);
    };

    Set.Add(Value, Layout, GetKeyHash, Compare, Construct, Destruct);
}
```

**四个 lambda 的语义**：

- `GetKeyHash`：计算元素哈希，分派到 `HashFunction` 或内置 `GetTypeHash`。
- `Compare`：哈希冲突时桶内比对。
- `Construct`：新槽插入元素时构造+拷贝。
- `Destruct`：覆盖旧值时析构原值（哈希冲突情况下）。

**vs `TMap::Add` 的 7-lambda**：少了 `KeyConstructAndAssign` / `ValueConstructAndAssign` / `ValueAssign` / `DestructKey` / `DestructValue` 的对偶分工——`TSet` 只有"元素"一种角色，所以一组 4-lambda 就够。其中 `Set.Add` 接口对"键已存在"的处理是 **silent no-op**（不像 Map 会 `ValueAssignFn` 覆盖值），因为 set 元素既是键，键已存在就意味着语义上"已经在了"。

**绑定层 `FAngelscriptSetBinds::Add` 的迭代守卫与引用失效**：

```cpp
void FAngelscriptSetBinds::Add(FScriptSet& Set, asCObjectType* Meta, void* Value)
{
#if AS_ITERATOR_DEBUGGING
    if (!CheckSetIteratorDebug(Set)) return;                    // 迭代中改写直接 Throw
#endif
    auto* Ops = FSetOperations::GetSetOperations(Meta);
#if AS_REFERENCE_DEBUGGING
    InvalidateReferencesToSet(Set, Ops);                        // 让 buffer 内的 ASRef 失效
#endif
    Ops->Add(Set, Value);
}
```

与 `FAngelscriptMapBinds::Add` 的结构完全一致。

### 5.2 `Contains` / `Remove` —— `FindIndex` 共享路径

```cpp
bool FAngelscriptSetBinds::Contains(FScriptSet& Set, asCObjectType* Meta, void* Value)
{
    auto* Ops = FSetOperations::GetSetOperations(Meta);
    int32 Index = Ops->FindIndex(Set, Value);
    return Index != INDEX_NONE;
}

bool FAngelscriptSetBinds::Remove(FScriptSet& Set, asCObjectType* Meta, void* Value)
{
#if AS_ITERATOR_DEBUGGING
    if (!CheckSetIteratorDebug(Set)) return false;
#endif
    auto* Ops = FSetOperations::GetSetOperations(Meta);
#if AS_REFERENCE_DEBUGGING
    InvalidateReferencesToSet(Set, Ops);
#endif
    int32 Index = Ops->FindIndex(Set, Value);
    if (Index == INDEX_NONE) return false;
    Ops->RemoveAt(Set, Index);
    return true;
}
```

`FindIndex` 内部用 `GetKeyHash` + `Compare` 两个 lambda 注入到 `FScriptSet::FindIndex`：

```cpp
FORCEINLINE_DEBUGGABLE int32 FSetOperations::FindIndex(FScriptSet& Set, void* Value)
{
    auto GetKeyHash = [this](const void* Ptr) -> uint32 { return GetHash(Ptr); };
    auto Compare    = [this](const void* A, const void* B) -> bool
        { return Type.IsValueEqual((void*)A, (void*)B); };
    return Set.FindIndex(Value, Layout, GetKeyHash, Compare);
}
```

**注意 TSet 没有 `OpIndex`**——`Map[K]` 是"按键查值"，但 set 元素既是键又是值，"找到了"和"它就是它自己"等价；`Contains` 已经回答了"是否找到"，所以没必要再有 `OpIndex` 这个抛错入口。

### 5.3 批量并集：`Append(TArray)` / `Append(TSet)`

这是 `TSet` **独有**的批量入口，`TMap` 没有对应的方法。

```cpp
void FAngelscriptSetBinds::AppendArray(FScriptSet& Set, asCObjectType* Meta, FScriptArray& SourceArray)
{
#if AS_ITERATOR_DEBUGGING
    if (!CheckSetIteratorDebug(Set)) return;
#endif
    auto* Ops = FSetOperations::GetSetOperations(Meta);
#if AS_REFERENCE_DEBUGGING
    InvalidateReferencesToSet(Set, Ops);
#endif
    for (int i = 0, Count = SourceArray.Num(); i < Count; ++i)
    {
        void* Value = (void*)((SIZE_T)SourceArray.GetData() + (i * Ops->ValueSize));
        Ops->Add(Set, Value);                                    // ★ 直接复用 Add: 自动去重
    }
}

void FAngelscriptSetBinds::AppendSet(FScriptSet& Set, asCObjectType* Meta, FScriptSet& SourceSet)
{
    // ...同样的迭代守卫与引用失效
    for (int32 i = 0, Num = SourceSet.GetMaxIndex(); i < Num; ++i)
    {
        if (SourceSet.IsValidIndex(i))
            Ops->Add(Set, Ops->GetElement(SourceSet, i));        // ★ Set 遍历需 IsValidIndex
    }
}
```

两个细节：

- **`AppendArray` 直接拿裸内存** `SourceArray.GetData() + i * ValueSize`——绕过 `FArrayOperations`，因为这里只需读取，知道索引连续；
- **`AppendSet` 必须 `IsValidIndex` 过滤**——sparse array 有空槽。

`Append` 提供了"集合代数风格"的批量并集语法：

```angelscript
TSet<int> A;  A.Add(1); A.Add(2);
TSet<int> B;  B.Add(2); B.Add(3);
A.Append(B);              // A 现在是 {1, 2, 3} (并集)
```

**没有内置交集/差集**——若需要，要手工遍历：

```angelscript
// 交集 A ∩ B
TSet<int> Intersection;
for (auto E : A)
    if (B.Contains(E)) Intersection.Add(E);

// 差集 A \ B
TSet<int> Difference;
for (auto E : A)
    if (!B.Contains(E)) Difference.Add(E);
```

### 5.4 `OpEquals` —— `IsPermutation` 多重计数

```cpp
bool FAngelscriptSetBinds::OpEquals(FScriptSet& SetA, asCObjectType* Meta, FScriptSet& SetB)
{
    auto* Ops = FSetOperations::GetSetOperations(Meta);
    if (!Ops->Type.CanCompare())
    {
        FAngelscriptEngine::Throw("Cannot compare set element type for equality.");
        return false;
    }
    return Ops->IsPermutation(SetA, SetB);
}
```

`IsPermutation` 算法与 `FMapOperations::IsPermutation` 完全同构（详见 `Syntax_TMap.md §5.5`）：

```text
1. Num(A) != Num(B)         -> 立即 false
2. 公共前缀逐对比对          -> 全等到底则 true
3. 第一个不等的位置开始,
   对剩余子集做 "多重计数比对":
     for 每个 ElementA in A 剩余:
       count_A = A 剩余中等于 ElementA 的次数
       count_B = B 剩余中等于 ElementA 的次数
       count_A != count_B -> false
   全部计数一致 -> true
```

**O(N²) 复杂度**——对 `TSet` 而言这个开销同样略显冗余（set 是单值集合，count 只会是 0 或 1），但代码与 `TMap` 共享一致行为。JIT 路径用 UE 自带 `TSet::OrderIndependentCompareEqual`（O(N)）做优化。

### 5.5 `Empty` vs `Reset` 的微妙差异

```cpp
void Empty(FScriptSet& Set, asCObjectType* Meta, int32 Slack) { Ops->Empty(Set, Slack); }
void Reset(FScriptSet& Set, asCObjectType* Meta)              { Ops->Empty(Set, Set.Num()); }
```

- `Empty(Slack)`：清空并保留 `Slack` 大小的桶预留
- `Reset()`：清空并保留**当前 Num** 大小的桶预留（"将立即填回相同规模数据"的优化）

两者都遍历析构所有元素。语义与 `TMap::Empty/Reset` 一致。

### 5.6 `Assign`（`opAssign`）—— 遍历 Empty + 重新 Add

```cpp
FScriptSet& FAngelscriptSetBinds::Assign(FScriptSet& Destination, asCObjectType* Meta, FScriptSet& Source)
{
    // ...迭代守卫 + 引用失效
    Ops->Empty(Destination, Source.Num());
    for (int32 i = 0, Num = Source.GetMaxIndex(); i < Num; ++i)
        if (Source.IsValidIndex(i))
            Ops->Add(Destination, Ops->GetElement(Source, i));
    return Destination;
}
```

与 `CopyValue` 实现完全一致——`opAssign` 走 `Assign`，C++ 端 `FAngelscriptSetType::CopyValue` 也是同样的循环。这意味着 `B = A` 永远是 deep copy + 重哈希，没有 POD 快路径。

---

## 六、`for` 范围循环：稀疏索引下的四件套

**源码所在**：`Bind_TSet.cpp:668-717`。

### 6.1 与 `TMap` 的关键差异：少了 `opForKey`

`TSet` 的 `for` 用与 `TMap` 同模式的稀疏索引迭代器，但**只有 `opForValue` 没有 `opForKey`**——元素既是键又是值，单一访问入口足够。

```cpp
TSet_.Method("int opForBegin()", [](FScriptSet& Set, asCObjectType* Meta) -> int32
{
    return FSetOperations::GetSetOperations(Meta)->FindNextIndex(Set, -1);   // ★ -1 起步
});

TSet_.Method("bool opForEnd(const int Iterator) const",
    [](FScriptSet&, int32 Iterator) -> bool { return Iterator == -1; });

TSet_.Method("void opForNext(int&inout Iterator)",
    [](FScriptSet& Set, asCObjectType* Meta, int32& Iterator)
{
    if (Iterator == -1) return;
    Iterator = FSetOperations::GetSetOperations(Meta)->FindNextIndex(Set, Iterator);
});

TSet_.Method("T& opForValue(const int Iterator)", [](FScriptSet& Set, asCObjectType* Meta, int32 Iterator) -> void*
{
    auto* Ops = FSetOperations::GetSetOperations(Meta);
    if (!Set.IsValidIndex(Iterator))
    {
        FAngelscriptEngine::Throw("Iterator out of bounds.");
        return nullptr;
    }
    return Ops->GetElement(Set, Iterator);
});
```

`FindNextIndex` 实现：

```cpp
FORCEINLINE_DEBUGGABLE int32 FSetOperations::FindNextIndex(FScriptSet& Set, int32 Index)
{
    int32 MaxIndex = Set.GetMaxIndex();
    for (int32 NextIndex = Index + 1; NextIndex < MaxIndex; ++NextIndex)
        if (Set.IsValidIndex(NextIndex))
            return NextIndex;
    return -1;
}
```

最坏 O(MaxIndex)，平均 O(1)（除非删除导致碎片化严重）——与 `TMap::FindNextIndex` 完全一致。

### 6.2 `for (auto E : Set)` 实际走 `Iterator()` 路径

与 `TMap` 同：AS 编译器对 `for (auto X : TSetContainer)` 的展开**不走 `opForBegin/End/Next/Value` 四件套**，而是查找 `Iterator()` 方法，构造 `TSetIterator<T>`，然后逐字段走 `Proceed()` / `bCanProceed`：

```cpp
TSet_.Method("TSetIterator<T> Iterator()", FUNC_TRIVIAL(FSetIterator::Create));
```

四件套的存在主要是 fallback 与显式手工 `for (auto __it = X.opForBegin(); ...)` 形式的支持。`foreach` 关键字（详见 `Documents/Knowledges/ZH/Syntax_FString.md` 系列文档对 foreach 的解析）也走 `Iterator()` 路径。

```angelscript
// AS 端典型用法
TSet<int> Values;
Values.Add(2);
Values.Add(5);

int Sum = 0;
foreach (int V : Values)                     // ★ foreach 走 Iterator(), 不暴露 sparse 索引
    Sum += V;
// Sum == 7
```

---

## 七、`TSetIterator<T>` / `TSetConstIterator<T>` —— 手工迭代器

**源码所在**：`Bind_TSet.h:263-338`、`Bind_TSet.cpp:643-666`。

### 7.1 数据结构

```cpp
struct FSetIterator
{
    FScriptSet*     Set;           // 被迭代的 Set
    FSetOperations* Ops;           // 元素操作集 (避免每次查 plainUserData)
    int32           Index;         // 当前槽
    int32           NextIndex;     // 下一槽 (预先算好)
    bool            bCanProceed;   // 是否还有元素

    static FSetIterator Create(FScriptSet& Set, asCObjectType* Meta);   // ★ 工厂
    void* Proceed();                                                     // ★ 步进 + 返回当前元素
    static void CopyConstruct(FSetIterator& Iterator, const FSetIterator& Other);
#if AS_ITERATOR_DEBUGGING
    static void Destruct(FSetIterator& Iterator);
#endif
    FSetIterator& Assignment(const FSetIterator& Other);
};
```

与 `FMapIterator` 的两个关键差异：

| 维度 | `FMapIterator` | `FSetIterator` |
|------|----------------|----------------|
| `Proceed()` 行为 | **仅步进**，取值用独立 `GetKey()`/`GetValue()` | **步进 + 返回当前元素**（`void*`） |
| 取键/取值 | 拆成 `GetKey` + `GetValue` 两入口 | 没有这两个入口（`Proceed` 已合并） |
| 删除当前元素 | `RemoveCurrent()` 显式接口 | **没有** —— `TSet` 不支持迭代时安全删除 |
| `SetValue` | 显式方法 | **没有** —— set 元素是键，"修改"语义会破坏哈希 |

### 7.2 工厂方法 + 迭代守卫

```cpp
static FSetIterator FSetIterator::Create(FScriptSet& Set, asCObjectType* Meta)
{
    auto* Ops = FSetOperations::GetSetOperations(Meta);

    FSetIterator It;
    It.Set = &Set;
    It.Ops = Ops;
    It.Index = -1;
    It.NextIndex = It.Ops->FindNextIndex(Set, -1);
    It.bCanProceed = It.NextIndex != -1;

#if AS_ITERATOR_DEBUGGING
    FSetOperations::MarkSetBeingIterated(Set);                  // ★ 入表
#endif
    return It;
}

#if AS_ITERATOR_DEBUGGING
static void FSetIterator::Destruct(FSetIterator& Iterator)
{
    if (Iterator.Set != nullptr)
        FSetOperations::UnmarkSetBeingIterated(*Iterator.Set);  // ★ 出表
}
#endif
```

注意 `CopyConstruct` / `Assignment` / `Destruct` 三处都要维护 `GSetsBeingIterated` 表——值类型迭代器在 AS 里频繁拷贝，每一次都必须正确增减引用计数。这与 `TMap` 完全同源。

### 7.3 `Proceed()` —— 步进与取值合一

```cpp
void* FSetIterator::Proceed()
{
    if (NextIndex == -1)
    {
        FAngelscriptEngine::Throw("Iterator out of bounds.");
        return nullptr;
    }

    Index = NextIndex;
    NextIndex = Ops->FindNextIndex(*Set, Index);
    bCanProceed = NextIndex != -1;

    return Ops->GetElement(*Set, Index);                         // ★ 直接返回当前元素引用
}
```

AS 端绑定声明：

```cpp
TSetIterator_.Method("const T& Proceed()", METHOD(FSetIterator, Proceed));
```

注意 **`Proceed` 返回 `const T&`**——即使是非 const 迭代器也是 const 引用。这与 `FMapIterator::SetValue` 提供"写"入口形成对比：set 元素不能在迭代过程中被修改（修改会破坏哈希），所以接口层就拒绝了。

### 7.4 AS 端用法：两种范式

**范式 A：`for/foreach` —— 隐式迭代器**

```angelscript
TSet<int> Values;
Values.Add(2);
Values.Add(5);

foreach (int V : Values)
{
    Print(f"{V}");           // 输出每个元素
}
```

**范式 B：显式手工迭代**

```angelscript
TSet<int> Values;
Values.Add(2);
Values.Add(5);

TSetIterator<int> It = Values.Iterator();
int Sum = 0;
while (It.CanProceed)
{
    Sum += It.Proceed();      // ★ Proceed 返回当前元素并步进
}
// Sum == 7
```

注意范式 B 调用顺序：**先检查 `CanProceed` 再 `Proceed`**——因为 `Create` 把 `Index = -1`、`NextIndex = 首个有效槽`，第一次 `Proceed` 才把 `Index = NextIndex`（实际访问首个元素）。所以 `Proceed` 是"前进到下一个"而不是"返回当前"。

### 7.5 const vs 非 const 迭代器

```cpp
TSet_.Method("TSetIterator<T> Iterator()", FUNC_TRIVIAL(FSetIterator::Create));
TSet_.Method("TSetConstIterator<T> Iterator() const", FUNC_TRIVIAL(FSetIterator::Create));
```

两者**底层对象都是 `FSetIterator`**——`TSetConstIterator<T>` 只是 AS 类型系统里的一个独立 `ValueClass`，绑定的方法集与 `TSetIterator<T>` 完全相同（毕竟 `Proceed` 都返回 `const T&`，没有非 const 写入口）。区分纯靠 AS 类型系统，C++ 端零分支。

---

## 八、调试器集成：单层展开（vs TMap 双层）

**源码所在**：`Bind_TSet.cpp:217-314`。

### 8.1 顶层值显示

```cpp
bool GetDebuggerValue(...) const override
{
    FScriptSet& Set = Usage.ResolvePrimitive<FScriptSet>(Address);
    int32 Num = Set.Num();
    Value.Value = Num == 0 ? TEXT("Empty") : FString::Printf(TEXT("Num = %d"), Num);
    Value.bHasMembers = true;
    return true;
}
```

VS Code 调试器面板会显示 `Visited    Num = 3`，可点开。

### 8.2 子项展开：直接列元素（无 TypeIndex 双层编码）

```cpp
// ============================================================================
// 文件: Bind_TSet.cpp
// 函数: FAngelscriptSetType::GetDebuggerScope
// ============================================================================
bool GetDebuggerScope(const FAngelscriptTypeUsage& Usage, void* Address, struct FDebuggerScope& Scope) const override
{
    const FAngelscriptTypeUsage& SubType = Usage.SubTypes[0];
    FScriptSet& Set = Usage.ResolvePrimitive<FScriptSet>(Address);

    FSetOperations Ops(SubType);
    for (int32 i = 0, Num = Set.GetMaxIndex(); i < Num; ++i)
    {
        if (!Set.IsValidIndex(i)) continue;

        void* ElemPtr = Ops.GetElement(Set, i);

        FDebuggerValue ElemValue;
        if (SubType.GetDebuggerValue(ElemPtr, ElemValue))
        {
            ElemValue.Name = FString::Printf(TEXT("[%d]"), i);     // ★ 直接用 [槽位号] 命名
            Scope.Values.Add(MoveTemp(ElemValue));
        }
    }

    {
        FDebuggerValue NumValue;
        NumValue.Name = TEXT("Num");
        NumValue.Type = TEXT("int");
        NumValue.Value = LexToString(Set.Num());
        Scope.Values.Add(MoveTemp(NumValue));
    }
    return true;
}
```

调试器面板效果：

```text
TSet<FString>     Num = 3
  [0]   "Alpha"
  [1]   "Beta"
  [3]   "Gamma"               // 注意 [2] 缺失: sparse array, 槽 2 是空闲槽
  Num   3
```

**与 `TMap::GetDebuggerScope` 的差异**：

- TMap 用 `TypeIndex` 编码两层（`TypeIndex == 0` = 顶层；`TypeIndex >= 1` = 在某个 Pair 内部展开 Key/Value）；
- TSet **只有一层**——元素直接展开，`TypeIndex` 字段未被本类型使用。这是因为 set 元素就是元素本身，不存在"展开后再分键值两栏"的需要。

### 8.3 通过 Member 字符串查询

```cpp
bool GetDebuggerMember(..., const FString& Member, ...) const override
{
    if (Member.StartsWith(TEXT("[")) && Member.EndsWith(TEXT("]")))
    {
        int32 Index = -1;
        LexFromString(Index, *Member.Mid(1, Member.Len() - 2));      // ★ 仅支持槽位号

        if (!Set.IsValidIndex(Index)) return false;

        FSetOperations Ops(SubType);
        void* ElemPtr = Ops.GetElement(Set, Index);
        return SubType.GetDebuggerValue(ElemPtr, Value);
    }
    else if (Member == TEXT("Num"))
    {
        // ... 返回 Num
    }
    return true;
}
```

**与 `TMap::GetDebuggerMember` 的差异**：TMap 的 Watch 表达式 `Lookup["Alpha"]` 会先尝试 `KeyType.FromStringIdentifier("Alpha")` 把字符串反序列化为键再查；TSet 的 Watch 表达式 `Visited["Alpha"]` **只支持整数槽位号**——不会反序列化字符串去查元素。这是因为 set 没有"按键查值"的语义入口（`Contains` 是布尔判存，不返回元素位置），所以调试器也对应裁剪。

要按值查 set 元素，得手工算槽位号或在脚本端打 `Print` 之类的临时代码。

---

## 九、StaticJIT 直通：模板特化路径

**源码所在**：`Bind_TSet.h:340-468`（`*_Template<T>` 系列）、`StaticJIT/StaticJITBinds.h`（宏）。

### 9.1 宏的双面孔（与 TMap 共享）

四种宏组合（NeedsCompare × NeedsCopy）覆盖所有 TSet 方法的子类型能力依赖。注册位置见 `Bind_TSet.cpp:570-616`：

| TSet 方法 | 用的宏 | 含义 |
|-----------|--------|------|
| `Destructor` | `SCRIPT_NATIVE_TEMPLATED_CALL` | 仅需要 size/alignment |
| `Add` | `..._NEEDSCOPY_NEEDSCOMPARE` | 既要元素比对又要拷贝 |
| `Append(TArray)` | `..._NEEDSCOPY_NEEDSCOMPARE` | 同上 |
| `Append(TSet)` | `..._NEEDSCOPY_NEEDSCOMPARE` | 同上 |
| `Contains` | `..._NEEDSCOMPARE` | 仅需要元素 `opEquals` |
| `Remove` | `..._NEEDSCOPY_NEEDSCOMPARE` | 注意虽然只删不拷, 但内部 destructor 路径间接需要 copy 信息 |
| `opAssign` | `..._NEEDSCOPY_NEEDSCOMPARE` | 拷贝整个 Set |
| `opEquals` | `..._NEEDSCOMPARE` | 仅需要比对 |
| `Empty` / `Reset` | `SCRIPT_NATIVE_TEMPLATED_CALL` | 无依赖 |
| `Iterator()` | `SCRIPT_NATIVE_TARRAY_ITERATOR_CREATE` | 与 TArray/TMap 共享 iterator 构造的 native 路径 |

### 9.2 模板版函数对应表

```cpp
// Bind_TSet.h:349-426
template<typename T>
static void Add_Template(TSet<T>& Set, void* Value)
{
    Set.Add(*(T*)Value);                            // ★ 直转原生 TSet<T>::Add
}

template<typename T>
static bool Contains_Template(TSet<T>& Set, void* Value)
{
    return Set.Contains(*(T*)Value);
}

template<typename T>
static bool Remove_Template(TSet<T>& Set, void* Value)
{
    int32 RemovedCount = Set.Remove(*(T*)Value);
    return RemovedCount != 0;                       // ★ 注意 TSet::Remove 返回删除条数, 转 bool
}

template<typename T>
static void AppendArray_Template(TSet<T>& Set, FScriptArray& SourceArray)
{
    Set.Append((TArray<T>&)SourceArray);            // ★ 二进制兼容: FScriptArray == TArray<T>
}

template<typename T>
static void AppendSet_Template(TSet<T>& Set, FScriptSet& SourceSet)
{
    Set.Append((TSet<T>&)SourceSet);                // ★ 二进制兼容: FScriptSet == TSet<T>
}

template<typename T>
static FScriptSet& Assign_Template(TSet<T>& Destination, FScriptSet& Source)
{
    Destination = *(TSet<T>*)&Source;
    return *(FScriptSet*)&Destination;
}

template<typename T>
static bool OpEquals_Template(TSet<T>& SetA, FScriptSet& SetB)
{
    return SetA.OrderIndependentCompareEqual(*(TSet<T>*)&SetB);   // ★ 走 UE 内置无序比对
}
```

利好（与 TMap 同模式）：

- **二进制兼容**：`reinterpret_cast<TSet<T>*>(FScriptSet*)` 直接安全——这是 UE 引擎自身的设计承诺。
- **编译期内联**：对 `TSet<int>::Add`，编译器知道 T 是 4 字节 POD，可省略所有 `bNeedConstruct/Copy` 分支与 lambda 间接调用。
- **`OrderIndependentCompareEqual`**：UE 自带的 hash-based 无序比对，O(N) 而非 `IsPermutation` 的 O(N²)。

### 9.3 关闭 JIT 时的退化

宏退化为空，所有方法走通用 `FAngelscriptSetBinds::*` 路径——功能完整、性能略低。这意味着启用 JIT 不是必需，关闭后行为不变。

---

## 十、关键限制与边缘案例

### 10.1 嵌套容器禁止（与 TMap 同）

```angelscript
TSet<TArray<int>> ByLists;            // ✗ 编译期: "Containers cannot be nested in other containers"
TSet<TSet<int>>   Nested;             // ✗ 同上
TSet<TMap<int,int>> WithMap;          // ✗ 同上
```

变通：用 `USTRUCT` 包一层。

```angelscript
struct FIntList { TArray<int> Values; uint32 Hash() const { return Values.Num(); } bool opEquals(const FIntList& O) const { ... } }
TSet<FIntList> Bucketed;              // ✓ struct 不是容器（但要自带 Hash + opEquals）
```

### 10.2 元素无哈希时编译期拒绝

```angelscript
TSet<FText> S;                        // ✗ FText 没有 GetTypeHash
TSet<FCustomNoHash> S2;               // ✗ "Key type does not have a hash function defined"
```

修复路径与 `TMap` 完全相同（详见 `Syntax_TMap.md §10.2`），不再展开。

### 10.3 迭代时改写仅调试构建报错

```angelscript
TSet<int> S;
foreach (int E : S)
{
    S.Remove(E);                      // ✗ 调试构建抛错; 发布构建未定义行为
}
```

发布构建里 `AS_ITERATOR_DEBUGGING` 关闭，`GSetsBeingIterated` 全局表不存在。改写底层 `FScriptSet` 可能让 `NextIndex` 指向已删除槽。

**注意**：`TSet` 的迭代器**没有 `RemoveCurrent` 接口**（与 `TMap::TMapIterator` 不同）。要边遍历边删，必须先把目标元素收集到临时数组：

```angelscript
TArray<int> ToRemove;
foreach (int E : S)
    if (ShouldRemove(E)) ToRemove.Add(E);

for (int E : ToRemove)
    S.Remove(E);                       // ✓ 安全
```

### 10.4 `TSet<UObject*>` 的"弱引用陷阱"

```angelscript
TSet<AActor> KnownActors;              // T 是 AActor* (UObject 派生)
KnownActors.Add(SomeActor);
// SomeActor 被 destroy / GC...
KnownActors.Contains(SomeActor);       // ★ UB? 还是怎样?
```

实际行为与 `TMap<UObject*, ?>` 同源：

1. `EmitReferenceInfo` 让 `TSet` 的 `StructSet` 知晓元素是 reference；
2. GC 走 sub-Schema 时**会清理元素指针**为 `nullptr`，但**不会从 Set 中物理删除**；
3. 后续 `Contains(SomeActor)` 因为指针已经被新对象重用可能误命中或漏命中。

**结论**：`TSet<UObject*>` 的语义是"弱引用集合"，**绝不应该依赖元素的生命周期**——读到 `nullptr` 元素是常态，必须显式过滤。安全实践：用 `FName` 做元素、用 `FObjectKey` 做包装、或周期性 `for` 清理 `nullptr` 元素。

### 10.5 函数参数按引用强制

```angelscript
void Mutate(TSet<int> S)               // 等价 TSet<int>& out S
{
    S.Add(99);                          // 调用者的 Set 也被改
}
```

虽然 `FAngelscriptSetType` 没有显式 `IsParamForcedOutParam = true` 的覆盖，但 `SetArgument` 实现走 `StepCompiledIn<FSetProperty>`/`StepCompiledInRef`：传值时是 deep copy（`CopyValue` 遍历重建），传引用时透传。AS 端默认值类型容器走值传递时**会触发深拷贝**——`TSet` 的深拷贝 O(N) 重哈希，比 `TArray` 的 memcpy 慢得多。**总是用 `const TSet<T>&` 或 `TSet<T>&` 传参**。

### 10.6 没有 `OpIndex` / `Find` / `FindOrAdd` / `GetKeys`

这是 `TSet` 与 `TMap` 在 API 表面**最显著的差异**：

```angelscript
TSet<int> S;
S.Add(42);

S[0];                                  // ✗ 编译错误: TSet 没有 opIndex
S.Find(42);                            // ✗ 编译错误: TSet 没有 Find
S.FindOrAdd(42);                       // ✗ 编译错误: TSet 没有 FindOrAdd
S.GetKeys(SomeArray);                  // ✗ 编译错误: TSet 没有 GetKeys

S.Contains(42);                        // ✓ 这是 set 唯一的"按值查询"入口
```

要把 set 元素转成数组遍历，必须手工：

```angelscript
TArray<int> Sorted;
foreach (int E : S)
    Sorted.Add(E);
Sorted.Sort();                         // ✓ 现在可以排序了
```

### 10.7 `==` 要求元素可比较

```angelscript
TSet<FCustomStructNoEquals> S;
S == S2;                               // ✗ Throw: "Cannot compare set element type for equality."
```

`Add/Contains/Remove` 在 `ValidateSetOperations` 阶段已经强制 T 必须 `CanCompare`（与键约束一致），所以这个 Throw 实际只在 `OpEquals` 内部做防御性检查——理论上走到这里说明 `Ops->Type.CanCompare()` 必须为真。

### 10.8 元素是引用语义但接口拿不到 mutable 引用

```angelscript
TSet<FRoomCoord> Rooms;
Rooms.Add(FRoomCoord(1, 2));

foreach (FRoomCoord R : Rooms)
{
    R.X = 99;                          // ✗ 即使 R 是 const T&, 修改也无效——但更糟的是修改会破坏哈希
}
```

`Proceed()` 返回 `const T&`、`opForValue` 也只暴露 `T&`（非 const 版）但**修改会让哈希失效**。设计上 set 不该让你 in-place 改元素——要"修改"得 `Remove` 旧元素、`Add` 新元素。这是底层哈希数据结构的硬性约束，与 STL `std::unordered_set::iterator` 不可写入的设计同源。

---

## 十一、与 `Bind_TMap` / `Bind_TArray` 的代码模式对照

### 11.1 共享的基础设施（三方共用）

| 维度 | `Bind_TArray` | `Bind_TMap` | `Bind_TSet` | 共享？ |
|------|---------------|-------------|-------------|--------|
| 元数据挂载 | `asCObjectType::plainUserData` | 同 | 同 | ✓ 完全共享机制 |
| 模板 ValueClass | `ValueClass<FScriptArray>` | `ValueClass<FScriptMap>` | `ValueClass<FScriptSet>` | ✓ 同基础设施 |
| `AS_ITERATOR_DEBUGGING` 全局表 | `GArraysBeingIterated` | `GMapsBeingIterated` | `GSetsBeingIterated` | 同模式，独立表 |
| `AS_REFERENCE_DEBUGGING` 失效 | `InvalidateReferencesToArray` | `InvalidateReferencesToMap` | `InvalidateReferencesToSet` | 同模式，独立函数 |
| TemplateCallback 守门 | `ValidateArrayOperations` | `ValidateMapOperations` | `ValidateSetOperations` | 同模式 |
| `CanBeTemplateSubType = false` | ✓ | ✓ | ✓ | ✓ 共同拒绝嵌套 |
| 注册时机 | `EOrder::Early` | `EOrder::Early+1` | `EOrder::Early+1` | TArray 早 1 拍 |
| StaticJIT 宏 | `SCRIPT_NATIVE_TEMPLATED_CALL_*` | 同四种 | 同四种 | ✓ 共享宏体系 |
| `*_Template<...>` 函数 | `Add_Template<T>` 等 | `Add_Template<K,V>` 等 | `Add_Template<T>` 等 | 同模板套路 |
| `RegisterTypeFinder` | `FArrayProperty -> Usage` | `FMapProperty -> Usage` | `FSetProperty -> Usage` | 同模式 |

### 11.2 `TSet` 与 `TMap` 共享、与 `TArray` 不同的逻辑

- **`HashFunction` 字段** + 脚本侧 `Hash()` 退路（仅 Set/Map 有）
- **`CanHashValue` 元素约束**（仅 Set/Map 有）
- **稀疏索引** `FindNextIndex`（仅 Set/Map 有，TArray 是密集）
- **`IsPermutation` 多重计数比对**（仅 Set/Map 有，TArray 是位序比对）
- **`StructSet` GC schema**（仅 Set/Map 有，TArray 是 `ReferenceArray`/`StructArray`）
- **`opAssign` 必走遍历重建**（仅 Set/Map 有，TArray 有 POD 快路径）

### 11.3 `TSet` 独有的逻辑（vs `TMap`）

- **单子类型 `<T>`** + 单组 size/alignment + 单组 POD 标志
- **4-lambda 注入式 `Add`**（vs Map 的 7-lambda）
- **`Append(TArray<T>&)` / `Append(TSet<T>&)` 批量并集**（Map 没有对应方法）
- **`Proceed()` 步进+返回值合并**（vs Map 的 `Proceed` 仅步进 + 独立 `GetKey/GetValue`）
- **调试器单层展开**（vs Map 的 `TypeIndex` 双层编码）

### 11.4 `TSet` **缺失**的逻辑（相对 `TMap`）

- **没有 `OpIndex`** —— 无"按键查值"入口
- **没有 `Find`** —— `Contains` 已经覆盖"是否找到"，无需返回元素
- **没有 `FindOrAdd`** —— set 是幂等集合，第二次 `Add(同一元素)` 自动忽略
- **没有 `RemoveAndCopyValue`** —— 删除时不需要返回值
- **没有 `GetKeys` / `GetValues`** —— 元素本身就是结果
- **没有 `opForKey`** —— 单一访问入口 `opForValue`
- **迭代器没有 `RemoveCurrent` / `SetValue`** —— 修改 set 元素会破坏哈希

---

## 十二、设计哲学

### 12.1 为什么 `TSet` 不复用 `TMap<T, void>` 的实现？

理论上 `TSet<T>` ≡ `TMap<T, void>`，可以让 `TSet` 直接转发到 `TMap`。但：

- **`FScriptSet` 与 `FScriptMap` 在 UE 内部是独立的 C++ 类型**——`FScriptMap` 是 `FScriptSet` 上加 KV 偏移的封装，不是别名。强行复用 TMap 会引入额外的 `ValueOffset = 0` + `bValueNeedConstruct = false` 等冗余字段。
- **二进制兼容**：StaticJIT 的 `*_Template<T>` 路径需要 `FScriptSet ↔ TSet<T>` 直接 reinterpret，这要求 `FScriptSet` 是独立类型。
- **GC schema 不同**：TSet 的 `EmitReferenceInfo` 只有单 sub-Schema，避免无谓的 `ValueOffset` 计算。

所以 `Bind_TSet` 选择**复制 TMap 的"键"部分代码**，删去"值"路径——结果是 ~780 行（vs TMap 的 ~1350 行），少了 ~40% 体积。

### 12.2 为什么 `TSet` 有 `Append(TArray)` / `Append(TSet)` 而 `TMap` 没有？

`TMap` 没有这两个方法是因为**没有合理的语义**：

- `Map.Append(TArray<???>)`：数组元素如何拆成 K/V？需要 `TArray<TPair<K,V>>` 才有意义，但脚本侧不支持嵌套容器。
- `Map.Append(TMap<K,V>)`：技术上可以，但当键冲突时是"覆盖"还是"忽略"？语义模糊，索性不提供，让用户写显式循环。

`TSet` 没有这种歧义——元素就是元素，"已存在则跳过"是 set 的天然语义。所以 `Append` 自然成立。

### 12.3 为什么 `Proceed()` 同时返回值而 `FMapIterator::Proceed` 只步进？

TMap 里"键"和"值"是两个独立访问入口，`Proceed` 不能"同时返回两者"——所以拆成 `Proceed()` 步进 + `GetKey()`/`GetValue()` 独立读。

TSet 只有元素一种角色，`Proceed` 步进后立即就能返回当前元素引用——合并成一个调用反而更简洁。

### 12.4 为什么 TSet 没有 `RemoveCurrent` 而 TMap 有？

`FMapIterator::RemoveCurrent` 安全的关键是：`NextIndex` 已经在前一次 `Proceed` 时算过，删当前槽不影响 `NextIndex`。

理论上 `FSetIterator` 也可以加 `RemoveCurrent`——但当前实现没加。原因可能是：

- TSet 用法相对简单，foreach 中删除的需求不强烈；
- `Proceed` 已经合并了步进+取值，再加 `RemoveCurrent` 会让接口表面变复杂；
- 可以用"先收集到 TArray 再批量删"的模式替代（详见 §10.3）。

这是已知的功能缺口——若高频需要边遍历边删，可考虑后续在 `FSetIterator` 上补 `RemoveCurrent`。

### 12.5 为什么调试器 Watch `Visited["Alpha"]` 不能按字符串查 set？

TMap 的调试器对 `Map[Key]` 做了 `KeyType.FromStringIdentifier("Alpha")` 反序列化——但这是因为 Map 有"按键查值"的天然语义入口。

TSet 没有 `Map[Key]` 这种语义——`Set.Contains("Alpha")` 是 bool 返回，不会"取出元素"。所以调试器对应裁剪：Watch `Visited["Alpha"]` 直接当成无效表达式（或按整数槽位号解析）。这与运行时 API 表面保持一致。

### 12.6 为什么 set 元素不能 in-place 修改？

哈希表的核心契约："元素的位置由它的哈希值决定"。如果允许修改元素，新值的哈希可能落在不同桶——元素就被"挂"在错误的桶上，下次 `Contains` 找不到。

要"修改"元素，正确流程是 `Remove(旧值)` + `Add(新值)`——让哈希表重新计算位置。TSet 接口层因此把 `Proceed`/`opForValue` 都标 `const T&`，不给写口子。这与 STL `std::unordered_set::iterator::operator*` 返回 `const T&` 是同一个设计哲学。

---

## 附录 A：API 速查表

| 方法 | AS 签名 | C++ 入口 | StaticJIT 宏 |
|------|--------|---------|-------------|
| 默认构造 | `TSet<T>()` | `FAngelscriptSetBinds::Construct` | — |
| 析构 | (隐式) | `FAngelscriptSetBinds::Destruct` | `SCRIPT_NATIVE_TEMPLATED_CALL` |
| 元素数 | `int Num() const` | `FAngelscriptSetBinds::Num` | (FUNC_TRIVIAL) |
| 是否为空 | `bool IsEmpty() const` | `FAngelscriptSetBinds::IsEmpty` | (FUNC_TRIVIAL) |
| 添加 | `void Add(const T&)` | `FAngelscriptSetBinds::Add` | `..._NEEDSCOPY_NEEDSCOMPARE` |
| 批量并集（数组） | `void Append(const TArray<T>&)` | `FAngelscriptSetBinds::AppendArray` | `..._NEEDSCOPY_NEEDSCOMPARE` |
| 批量并集（集合） | `void Append(const TSet<T>&)` | `FAngelscriptSetBinds::AppendSet` | `..._NEEDSCOPY_NEEDSCOMPARE` |
| 判存 | `bool Contains(const T&) const` | `FAngelscriptSetBinds::Contains` | `..._NEEDSCOMPARE` |
| 删 | `bool Remove(const T&)` | `FAngelscriptSetBinds::Remove` | `..._NEEDSCOPY_NEEDSCOMPARE` |
| 清空 | `void Empty(int32 Slack = 0)` | `FAngelscriptSetBinds::Empty` | `SCRIPT_NATIVE_TEMPLATED_CALL` |
| 重置 | `void Reset()` | `FAngelscriptSetBinds::Reset` | `SCRIPT_NATIVE_TEMPLATED_CALL` |
| 赋值 | `TSet<T>& opAssign(const TSet<T>&)` | `FAngelscriptSetBinds::Assign` | `..._NEEDSCOPY_NEEDSCOMPARE` |
| 等比 | `bool opEquals(const TSet<T>&) const` | `FAngelscriptSetBinds::OpEquals` | `..._NEEDSCOMPARE` |
| 取迭代器 | `TSetIterator<T> Iterator()` | `FSetIterator::Create` | `SCRIPT_NATIVE_TARRAY_ITERATOR_CREATE` |
| 取迭代器 (const) | `TSetConstIterator<T> Iterator() const` | 同上 | 同上 |
| for 起点 | `int opForBegin() [const]` | lambda → `FindNextIndex(-1)` | — |
| for 终点 | `bool opForEnd(const int) const` | `Iterator == -1` | — |
| for 步进 | `void opForNext(int& inout)` | lambda → `FindNextIndex` | — |
| for 取值 | `T& opForValue(const int) [const]` | lambda → `Ops->GetElement` | — |

### `TSetIterator<T>` 方法

| 方法 | AS 签名 | 说明 |
|------|--------|------|
| 拷贝构造 | `TSetIterator(const TSetIterator&)` | 同时维护迭代守卫表 |
| 赋值 | `TSetIterator& opAssign(const TSetIterator&)` | 同上 |
| 步进+取值 | `const T& Proceed()` | 越界时 Throw；返回当前元素 |
| 状态 | `bool CanProceed` | property，公开字段 |

**对比 `TMapIterator<K,V>`**：TSet 迭代器表面只有 4 项（构造/赋值/步进/状态），TMap 迭代器还有 `GetKey` / `GetValue` / `SetValue` / `RemoveCurrent` 共 8 项——差距来自 set 元素只读的硬性约束。

---

## 附录 B：避坑清单

1. **元素类型必须可哈希**：`FText` / 无 `Hash()` 的脚本 struct / 无 `WithGetTypeHash` 的 USTRUCT 都会被 `ValidateSetOperations` 拒绝。优先用 `FName` / `int` / `FGuid` 做元素。
2. **没有 `OpIndex` / `Find` / `FindOrAdd` / `GetKeys`**：与 `TMap` 在 API 表面差距最大的几处。要"按值取出"必须 `Contains` + `foreach` 组合。
3. **迭代时不能改写 Set**：调试构建抛错，发布构建未定义行为。要边遍历边删需先收集到临时 `TArray` 再批量删——`TSetIterator` **没有 `RemoveCurrent`**（与 `TMapIterator` 不同）。
4. **`for/foreach` 走 `Iterator()` 路径**：实际后端是 `TSetIterator<T>`，`Proceed()` 返回 `const T&`。
5. **嵌套容器禁止**：`TSet<TArray<T>>` / `TSet<TSet<T>>` / `TSet<TMap<K,V>>` 编译期失败。
6. **`UObject*` 作元素有弱引用陷阱**：GC 清理对象后元素指针可能 stale，必须周期性过滤 `nullptr`。优先用 `FName` 或 `FObjectKey`。
7. **值参数走深拷贝**：`void Foo(TSet<T> S)` 触发 O(N) 重哈希。**总是**用 `const TSet<T>&`。
8. **`opAssign` 没 POD 快路径**：`TSet<int>` 拷贝**不会**自动 memcpy——总是遍历 `Add`。
9. **脚本侧 `Hash()` 调用慢**：每次 `Add/Contains/Remove` 触发 AS 反射调用，慢 1-2 个量级。
10. **元素不能 in-place 修改**：`Proceed()` / `opForValue` 即使返回 `T&`，修改也会破坏哈希。要修改请 `Remove(旧)` + `Add(新)`。
11. **`Append(TArray)` 自动去重**：与 `TArray::Append(TArray)` 不同，TSet 的 `Append` 走 `Add` 路径，重复元素被吞。
12. **没有交集/差集 API**：必须手工 `for + Contains` 实现，或封装到 mixin。
13. **调试器 Watch 不支持 `Visited["Alpha"]` 字符串查找**：只支持整数槽位号，因为 set 没有"按值取元素"的语义入口。

---

## 小结

- **`TSet<T>` 是 `Bind_TSet.cpp` 注册的单子类型 ValueClass**，底层桥接 UE 自带的 `FScriptSet`（与 `FScriptMap` 内部实质同源），与 `TMap` 共享 `plainUserData` 元数据挂载、`AS_ITERATOR_DEBUGGING` 守卫、`StructSet` GC schema、StaticJIT 宏体系。
- **元素约束 ≡ TMap 键约束**：编译期 `Type.CanHashValue() || HashFunction != nullptr`（脚本侧 `uint32 Hash() const` 退路），运行时 `Type.CanCompare()` 用于哈希桶冲突时的 `opEquals` 比对。
- **`FSetOperations` 缓存元素 size/alignment + POD 标志位 + `FScriptSetLayout`**，所有 `Add/Contains/Remove` 经 4-lambda 注入式 `FScriptSet::Add` / `FindIndex` 接口完成（vs TMap 的 7-lambda）。
- **API 表面比 TMap 精简**：没有 `OpIndex` / `Find` / `FindOrAdd` / `GetKeys` / `RemoveAndCopyValue`，但**多了** `Append(TArray<T>&)` / `Append(TSet<T>&)` 两个集合代数风格的并集入口。
- **For 循环用稀疏索引 + 四件套**（`opForBegin/End/Next/Value`，无 `opForKey`）；`Iterator()` 路径返回 `TSetIterator<T>`，是 AS 端 `for/foreach` 的实际后端。`Proceed()` 步进+返回值合并（vs Map 的 `Proceed` 仅步进）。
- **StaticJIT 启用时通过 `*_Template<T>` 模板版函数将通用调用替换为原生 `TSet<T>::Add/Contains/...`**；二进制兼容 `FScriptSet` ↔ `TSet<T>` 让转换零成本。

---

## 修订记录

| 版本 | 日期 | 内容 |
|------|------|------|
| v1.0 | 2026-05-22 | 首版：基于 `Bind_TSet.cpp:1-778` / `Bind_TSet.h:1-469` / `Core/AngelscriptType.h:277` / `StaticJIT/StaticJITHelperFunctions.h` / `AngelscriptSetBindingsTests.cpp` / `AngelscriptSetBindingsAdvancedTests.cpp` / `AngelscriptForeachBindingsTests.cpp` / `AngelscriptIteratorBindingsTests.cpp` 完整产出。覆盖：① `TSet<T>` 与 `TMap<K,V>` 在 ~⅖ 实现上共享、其余差异（单子类型 / 单组 size+POD / 4-lambda Add / Append 批量并集 / 单层调试器 / 缺 OpIndex/Find/FindOrAdd/GetKeys/opForKey/RemoveCurrent/SetValue）逐一列出；② `EOrder::Early+1` 注册时序（与 TMap 同列、晚于 TArray）；③ `FSetOperations` 数据结构与 `FMapOperations` 字段对照表（约 ½ 字段数）；④ `ValidateSetOperations` 三层拦截（嵌套/可哈希/可构造）共享 TMap 错误文案；⑤ 元素类型选择决策表（与 TMap 键决策表完全一致）；⑥ `FAngelscriptSetType` 接口职责映射、单 sub-Schema `StructSet` GC、`FSetProperty` 单 inner（`ElementProp`）创建链路；⑦ 关键操作剖析：`Add` 4-lambda、`Contains`/`Remove` 共享 `FindIndex`、`AppendArray`/`AppendSet` 直接复用 `Add` 自动去重、`OpEquals` 走 `IsPermutation` 多重计数 O(N²)、`Empty` vs `Reset`、`Assign` 永远 deep copy；⑧ 稀疏索引 + 四件套 `opForBegin/End/Next/Value`（无 `opForKey`）；⑨ `TSetIterator` 双范式（`foreach` 隐式 vs 显式 `CanProceed/Proceed`）、`Proceed` 步进+取值合并、迭代器**没有 RemoveCurrent/SetValue**；⑩ 调试器单层展开（vs TMap `TypeIndex` 双层编码）+ Watch 表达式仅支持槽位号；⑪ StaticJIT 四种宏 + `*_Template<T>` 二进制兼容路径；⑫ 8 项关键限制（嵌套/无哈希/迭代改写/`UObject*` 弱引用/值参数深拷贝/无 OpIndex 等表面缺失/`==` 元素可比较/元素不可 in-place 修改）；⑬ 与 `TArray` / `TMap` 三方共享/独有/缺失逻辑四表；⑭ 6 项设计哲学解析（不复用 TMap<T,void>/为什么有 Append/Proceed 合并/无 RemoveCurrent/调试器裁剪/不可 in-place）；⑮ API 速查 + 避坑 13 条。所有 ASCII 图遵循纯 ASCII 风格，与 `Syntax_TArray.md` v1.0 / `Syntax_TMap.md` v1.0 / `Syntax_DefaultStatement.md` 等统一。 |
