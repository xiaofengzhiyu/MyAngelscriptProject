# Syntax_TMap — `TMap<K,V>` 哈希映射实现原理

> **所属前缀**: Syntax_（容器类型族）
> **关注层面**: 语法机制与实现原理（非用户使用指南）
> **关键源码**:
> `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TMap.cpp` (~1350 行)
> · `Binds/Bind_TMap.h` (~680 行) —— `FMapOperations` / `FMapIterator` / `FAngelscriptMapBinds`
> · `Containers/Map.h` / `Containers/Set.h`（UE 引擎自带 `FScriptMap` / `FScriptSet`）
> · `Core/AngelscriptType.h` —— `CanHashValue` / `GetHash` 接口
> · `StaticJIT/StaticJITBinds.h:91-114` —— `SCRIPT_NATIVE_TEMPLATED_CALL_*` 宏
> **关联文档**:
> `Documents/Knowledges/ZH/Syntax_TArray.md` —— 兄弟容器，本文反复对照其结构
> · `Documents/Knowledges/ZH/Type_BindSystem.md` —— `FAngelscriptType` / `FAngelscriptTypeUsage` 多态分派
> · `Documents/Knowledges/ZH/Syntax_TSet.md`（待写）—— 共享 `FScriptSet` 底层与键哈希约束
> · `Documents/Knowledges/ZH/Type_FunctionCaller.md` —— `asCALL_THISCALL_OBJLAST` 调用约定
> · `Documents/Knowledges/ZH/Syntax_UPROPERTY.md` —— `FMapProperty` 创建链路

---

## 概览：键-值对的"哈希约束"与"稀疏索引"

`TMap<K, V>` 在当前插件里不是 AngelScript 内核类型，而是 `Bind_TMap.cpp` 在引擎启动期注册的 **模板 ValueClass**，底层完全复用 UE 自带的 `FScriptMap`（`Containers/Map.h`，本质是 `FScriptSet` 上的 `TPair<K, V>`）。它与 `TArray<T>` 是兄弟关系——共享 `FAngelscriptType` 接口、`asCObjectType::plainUserData` 元数据挂载点、`AS_ITERATOR_DEBUGGING` 守卫——但有两个本质差异：

```text
[差异 1] 双子类型 (K, V) 而非单子类型 (T)
    -> Flags.TemplateType = "<K,V>" 而非 "<T>"
    -> ValidateMapOperations 同时校验两个子类型
    -> 两套 bNeedConstruct/Copy/Destruct 标志位 (KeyXxx + ValueXxx)
    -> CreateProperty 创建 FMapProperty 时同时设 KeyProp 与 ValueProp

[差异 2] 键类型必须 "可哈希" (CanHashValue)
    -> ValidateMapOperations 额外校验 KeyType.CanHashValue()
    -> 若键是脚本侧 struct/class, 必须实现 uint32 Hash() const 方法
    -> FMapOperations::HashFunction 缓存指向脚本方法的 asIScriptFunction*
    -> 命中时通过 FAngelscriptContext + Execute() 反射调用拿哈希值
```

```text
TMap<K,V> 总览管线
================================================================================
[启动] AS Engine 初始化
   Bind_TMap 走 EOrder::Early+1 (晚于 TArray, 因为 GetKeys/GetValues 依赖 TArray<K>/<V>)
     - ValueClass<FScriptMap>("TMap<class K, class V>")
     - 注册 Add/Contains/Find/FindOrAdd/Remove/OpIndex/OpAssign/OpEquals/...
     - 注册 GetKeys(TArray<K>&) / GetValues(TArray<V>&)
     - 注册 opForBegin/End/Next/Value/Key 五件套 (键值同时可见)
     - 注册 TMapIterator<K,V> / TMapConstIterator<K,V>
     - FAngelscriptType::Register(FAngelscriptMapType)
     - RegisterTypeFinder(FMapProperty -> FAngelscriptTypeUsage)
        |
        v
[AS 编译期] 第一次见 TMap<FName, int>
   asCBuilder 实例化 -> TemplateCallback -> ValidateMapOperations
     - KeyType = FAngelscriptTypeUsage{FName}
     - ValueType = FAngelscriptTypeUsage{int}
     - 校验:
         KeyType.CanBeTemplateSubType()    (拒绝嵌套容器)
         KeyType.CanHashValue()            (拒绝无 GetTypeHash 的键)
         KeyType.CanConstruct/Destruct/Copy/Compare()
         ValueType.CanBeTemplateSubType()
         ValueType.CanConstruct/Destruct/Copy()  (注意: 值不要求 CanCompare)
     - new FMapOperations(KeyType, ValueType)
     - 计算 FScriptMapLayout (KeyOffset / ValueOffset / SetLayout)
     - SetUserData(Ops) 挂到 asCObjectType::plainUserData
        |
        v
[AS 运行期] 操作分派
     [a] FAngelscriptMapBinds::Add(Map, Meta, &Key, &Value)
            -> Ops = GetMapOperations(Meta)
            -> Ops->Add(Map, Key, Value)  (调 FScriptMap::Add 的 7-lambda 重载)
     [b] FAngelscriptMapBinds::OpIndex(Map, Meta, &Key)
            -> Ops->FindPairIndex(Map, Key)  (Hash + Equality lambda 注入)
            -> 找不到 -> Throw "Could not find key in map"
            -> 找到 -> 返回 GetValue(Map, Index)
     [c] for (auto It : Map) -> opForBegin/End/Next/Value/Key
            -> Iterator 是稀疏索引: -1 表 "结束"; FindNextIndex 跳过空槽
        |
        v
[GC 集成] TMap<UObject, FActorRef> 含引用时
   FAngelscriptMapType::EmitReferenceInfo
     -> 计算 FScriptMapLayout
     -> 用 FSchemaBuilder 构造 InnerSchema:
          KeyType.HasReferences()  -> Inner Schema 在 offset 0 加引用
          ValueType.HasReferences() -> Inner Schema 在 ValueOffset 加引用
     -> 顶层用 EMemberType::StructSet 包装 (区别于 TArray 的 ReferenceArray/StructArray)
```

### 三层防御网

| 层 | 触发点 | 报错语句 |
|---|--------|----------|
| 编译期 | `ValidateMapOperations` 拒绝嵌套 / 不可哈希 / 不可构造 | `"Containers cannot be nested in other containers"` / `"Key type does not have a hash function defined"` / `"Subtype cannot be constructed or copied"` |
| 运行期键不存在 | `OpIndex` / `Find` / `Remove` 内部 `FindPairIndex == INDEX_NONE` | `"Could not find key in map for index operator."`（仅 `OpIndex`，其余安静返回 `false`/`null`） |
| 迭代守卫 | `AS_ITERATOR_DEBUGGING` 全局表 `GMapsBeingIterated` | `"TMap is being modified during for loop iteration"` |

### 核心特性速览

| 特性 | AS 语法 | 实现策略 |
|------|--------|---------|
| **声明** | `TMap<FString, int> Lookup;` | `ValueClass<FScriptMap>` + 双 `<K,V>` 模板 |
| **插入** | `Lookup.Add("Key", 42);` | `FMapOperations::Add` 调 `FScriptMap::Add` 7-lambda 重载 |
| **下标** | `Lookup["Key"]` | `OpIndex` 找不到键直接 `Throw` |
| **判存** | `Lookup.Contains("Key")` | `FindPairIndex != INDEX_NONE` |
| **查找** | `Lookup.Find("Key", OutValue)` | 拷贝值到 OutValue；找不到返回 `false` |
| **找或加** | `int& V = Lookup.FindOrAdd("Key");` | `FindOrAdd_Defaulted` 用临时 buffer 默认构造再插入 |
| **删除** | `Lookup.Remove("Key")` | `FindPairIndex` + `Ops->RemoveAt` |
| **删并取** | `Lookup.RemoveAndCopyValue("Key", Out)` | 一次找索引、拷出值、再 `RemoveAt` |
| **范围 for** | `for (auto E : Map) { E.GetKey(); E.GetValue(); }` | 稀疏索引 + `opForBegin/End/Next/Value/Key` 五件套 |
| **比较** | `A == B` | `IsPermutation` 多重计数比对（无序） |
| **批量取键** | `Map.GetKeys(OutArray)` | 遍历有效槽 `Insert` 到 `FScriptArray` |
| **GC 追踪** | `TMap<UObject, FRef>` 自动追踪 | `EmitReferenceInfo` -> `EMemberType::StructSet` |
| **嵌套禁止** | `TMap<FString, TArray<int>>` ✗ | `CanBeTemplateSubType() = false`（容器不可嵌套）|
| **`FMapProperty` 桥接** | `UPROPERTY() TMap<int, AActor>` | `CreateProperty` 创建 `FMapProperty` + `KeyProp` + `ValueProp` |

后续章节按 注册时机 → 元素元数据 → 键哈希约束 → 反射桥接 → 操作实现 → for 展开 → 迭代器 → 调试器 → JIT 直通 → 限制与对照 → 设计哲学 顺序展开。

---

## 一、绑定入口：`EOrder::Early+1` 与模板登记

**源码所在**：`Bind_TMap.cpp:1073-1292` 的 `Bind_TMap` 全局静态绑定块。

### 1.1 类型注册框架

```cpp
// ============================================================================
// 文件: AngelscriptRuntime/Binds/Bind_TMap.cpp
// 函数: Bind_TMap (AS_FORCE_LINK 全局静态)
// ============================================================================
AS_FORCE_LINK const FAngelscriptBinds::FBind Bind_TMap((int)FAngelscriptBinds::EOrder::Early+1, []
{
    FBindFlags Flags;
    Flags.bTemplate = true;
    Flags.TemplateType = "<K,V>";                            // ★ 双子类型, 不同于 TArray 的 "<T>"
    Flags.ExtraFlags = asOBJ_TEMPLATE_SUBTYPE_COVARIANT;     // 允许子类型协变

    auto TMap_ = FAngelscriptBinds::ValueClass<FScriptMap>("TMap<class K, class V>", Flags);
    TMap_.Constructor("void f()", FUNC_TRIVIAL(FAngelscriptMapBinds::Construct));
    TMap_.Destructor("void f()", &FAngelscriptMapBinds::Destruct);
    SCRIPT_NATIVE_TEMPLATED_CALL(TMap_, "FAngelscriptMapBinds::Destruct", false);
    FAngelscriptBinds::PreviousBindPassScriptObjectTypeAsFirstParam();
    ...
});
```

四个关键调用各司其职：

| 调用 | 作用 |
|------|------|
| `Flags.bTemplate = true` + `TemplateType = "<K,V>"` | 告知 AS：本类有**两个**模板参数；`ValidateMapOperations` 因此查 `GetSubTypeId(0/1)` |
| `asOBJ_TEMPLATE_SUBTYPE_COVARIANT` | 启用子类型协变；与 `TArray` 同 |
| `Constructor` 走 `FUNC_TRIVIAL` | `placement-new FScriptMap()` 无需 `Meta`，跳过 `THISCALL_OBJLAST` 包装 |
| `Destructor` 配套 `SCRIPT_NATIVE_TEMPLATED_CALL` | 析构需要遍历元素调 `DestructValue`，所以 JIT 路径需要原生 `~TMap<K,V>()` 模板版本 |

### 1.2 `EOrder::Early+1` —— 比 `TArray` 晚一拍

`Bind_TMap` 的注册顺序是 `(int)EOrder::Early+1`，**比 `TArray` 晚 1**。原因：

- `TMap` 的 `GetKeys(TArray<K>& OutKeys)` / `GetValues(TArray<V>& OutValues)` 方法**签名里直接出现 `TArray<K>`**；
- AS 编译这两个签名时需要 `TArray` 模板已注册；
- 所以 `TMap` 必须在 `TArray` 完成注册之后再注册。

这是注册时序的**显式依赖**，违反则 `GetKeys/GetValues` 注册阶段会找不到 `TArray<K>` 模板而报错。`TSet` 注册时也走 `Early+1`，因为它同样依赖 `TArray` 用于 `Array(TArray<T>& OutArray)` 等方法。

### 1.3 `TemplateCallback` —— 元素类型守门员

```cpp
// ============================================================================
// 文件: AngelscriptRuntime/Binds/Bind_TMap.cpp
// 函数: ValidateMapOperations (TemplateCallback 内部)
// ============================================================================
TMap_.TemplateCallback("bool f(int&in Type, int&out ErrorMessage)",
    [](asITypeInfo* TemplateType, asCString* ErrorMessage) -> bool
{
    return ValidateMapOperations(TemplateType, ErrorMessage);
});
```

`ValidateMapOperations` 在每次首次实例化 `TMap<具体K, 具体V>` 时调用一次（详见 §三）。它有两个独有的拒绝点（相比 `TArray::ValidateArrayOperations`）：

```text
TMap<int, int>                  -> 通过 (int 既可哈希又可比较)
TMap<FName, AActor>             -> 通过 (FName 走 GetTypeHash, AActor 是 UObject 指针)
TMap<TArray<int>, int>          -> 拒: "Containers cannot be nested in other containers"
TMap<int, TArray<FString>>      -> 拒: 同上 (值也不能是容器)
TMap<FCustomStructNoHash, int>  -> 拒: "Key type does not have a hash function defined"
TMap<FAbstract, int>            -> 拒: "Subtype cannot be constructed or copied"
```

注意 **值类型不要求 `CanCompare`** —— 这与 `TArray` 不同。`TArray::Sort/Remove(value)` 需要 `IsValueEqual`，而 `TMap` 只在 `OpEquals`（`==` 运算符）时按需要求；`Add/OpIndex/Find/Remove(by key)` 只看键是否相等，值类型甚至可以是没有 `opEquals` 的类。

---

## 二、`FMapOperations` —— 元素元数据缓存与桥接 `FScriptMap`

**源码所在**：`Bind_TMap.h:18-373`、`Bind_TMap.cpp:1294-1350`（`ValidateMapOperations`）。

### 2.1 数据结构

```cpp
// ============================================================================
// 文件: AngelscriptRuntime/Binds/Bind_TMap.h
// 角色: 元素元数据缓存 + FScriptMap 操作入口
// ============================================================================
struct ANGELSCRIPTRUNTIME_API FMapOperations
{
    bool bValid;

    FAngelscriptTypeUsage KeyType;          // 键类型描述符
    FAngelscriptTypeUsage ValueType;        // 值类型描述符

    FScriptMapLayout Layout;                // ★ UE 引擎计算的 K/V 内存布局

    int32 KeySize;       int32 KeyAlignment;
    int32 ValueSize;     int32 ValueAlignment;

    bool bKeyNeedConstruct, bKeyNeedCopy, bKeyNeedDestruct;
    bool bValueNeedConstruct, bValueNeedCopy, bValueNeedDestruct;

    asIScriptFunction* HashFunction;        // ★ 脚本侧 uint32 Hash() const, 缺省 nullptr 走 GetTypeHash

    static FORCEINLINE FMapOperations* GetMapOperations(asCObjectType* Meta)
    {
        return (FMapOperations*)Meta->plainUserData;
    }
    // ...
};
```

与 `FArrayOperations` 的对照：

| 字段 | `TArray::FArrayOperations` | `TMap::FMapOperations` |
|------|---------------------------|------------------------|
| 元素类型 | 单一 `FAngelscriptTypeUsage Type` | 双重 `KeyType` + `ValueType` |
| 内存布局 | `NumBytesPerElement` + `Alignment` | 两套 size/alignment **+ 一个 `FScriptMapLayout`** |
| POD 标志 | 单组 `bNeedConstruct/Copy/Destruct` | **两组**（key 一组、value 一组），命名前缀 `bKey*` / `bValue*` |
| 比较函数 | `CompareFunction*`（用于 `Sort`） | `HashFunction*`（用于键哈希，仅脚本侧 struct 才需要） |
| 元素地址 | `Get(Arr, Index)` 单一 ptr | `GetKey(Map, Index)` + `GetValue(Map, Index)` 两入口 |

### 2.2 `FScriptMapLayout`：UE 引擎给的 KV 偏移

```cpp
Layout = FScriptMap::GetScriptLayout(KeySize, KeyAlignment, ValueSize, ValueAlignment);
```

`FScriptMap` 是 UE 引擎自带的"无类型 TMap"——内部存储 `TPair<Key, Value>` 的 `FScriptSet`。`FScriptMapLayout` 给出三组关键信息：

```text
Layout = {
    KeyOffset       = 0,                     // Key 在 Pair 内的偏移 (恒为 0)
    ValueOffset     = sizeof(K) 对齐到 V,    // ★ 关键: 用于 GetValue 跳过 Key
    SetLayout       = {                       // 嵌套的 FScriptSet 槽位布局
        SparseArrayLayout = { Size, Alignment, ... }
    }
}
```

`GetKey/GetValue` 实现完全依赖此布局：

```cpp
FORCEINLINE void* FMapOperations::GetKey(FScriptMap& Map, int32 Index)
{
    return (void*)((SIZE_T)Map.GetData(Index, Layout));   // Pair 起始地址
}

FORCEINLINE void* FMapOperations::GetValue(FScriptMap& Map, int32 Index)
{
    return (void*)((SIZE_T)Map.GetData(Index, Layout) + (SIZE_T)Layout.ValueOffset);
}
```

`FScriptMap::GetData` 内部按 `SparseArrayLayout` 回算 Pair 槽位地址，跨 `Map.GetMaxIndex()` 范围**包含空闲槽**——这是 §六 `for` 循环必须用 `IsValidIndex` 过滤的根因。

### 2.3 `ValidateMapOperations` —— 一次构造、终生复用

```cpp
// ============================================================================
// 文件: AngelscriptRuntime/Binds/Bind_TMap.cpp
// 函数: ValidateMapOperations
// ============================================================================
bool ValidateMapOperations(asITypeInfo* TemplateType, asCString* ErrorMessage)
{
    FMapOperations* Ops = (FMapOperations*)TemplateType->GetUserData();
    if (Ops != nullptr)
        return Ops->bValid;                                     // 已构造 -> 直接复用

    int32 KeyTypeId   = TemplateType->GetSubTypeId(0);
    int32 ValueTypeId = TemplateType->GetSubTypeId(1);
    auto KeyType   = FAngelscriptTypeUsage::FromTypeId(KeyTypeId);
    auto ValueType = FAngelscriptTypeUsage::FromTypeId(ValueTypeId);

    // [拦截 1] 嵌套容器
    if (!KeyType.CanBeTemplateSubType() || !ValueType.CanBeTemplateSubType())
    {
        *ErrorMessage = "Containers cannot be nested in other containers";
        return false;
    }

    Ops = new FMapOperations(KeyType, ValueType);

    // [拦截 2] 键类型必须可哈希
    bool bCanHash = KeyType.CanHashValue();
    if (!bCanHash)
    {
        // 退路: 脚本类是否定义了 uint32 Hash() const 方法?
        if (asCTypeInfo* SubType = (asCTypeInfo*)TemplateType->GetSubType(0))
        {
            auto* ObjectType = CastToObjectType(SubType);
            if (ObjectType != nullptr && ObjectType->GetFirstMethod("Hash") != nullptr)
            {
                Ops->HashFunction = SubType->GetMethodByDecl("uint32 Hash() const");
                bCanHash = Ops->HashFunction != nullptr;
            }
        }
    }

    // [拦截 3] 构造/析构/拷贝/键比较缺一不可
    Ops->bValid = KeyType.CanConstruct() && KeyType.CanDestruct() && KeyType.CanCopy()
        && KeyType.CanCompare() && bCanHash
        && ValueType.CanConstruct() && ValueType.CanDestruct() && ValueType.CanCopy();

    TemplateType->SetUserData(Ops);

    if (!Ops->bValid && ErrorMessage != nullptr)
        *ErrorMessage = bCanHash ? "Subtype cannot be constructed or copied"
                                  : "Key type does not have a hash function defined";

    return Ops->bValid;
}
```

注意三个关键点：

- **`Ops` 即使最终 `bValid = false` 也被挂到 `userData`**：避免重复构造，`bValid` 字段足以让后续调用直接拒绝。
- **`KeyType.CanCompare()` 必须为真**：键之间需要 `opEquals` 用于哈希冲突时的桶内查找；`ValueType.CanCompare()` 不强求。
- **错误信息分两路**：值类型问题与键哈希问题用不同文案，便于用户定位。

---

## 三、键类型约束：哪些类型可作 `K`

这是 `TMap` 与 `TArray` **最大的语义差异**——`TArray<T>` 只要 T 可以构造/析构/拷贝就行，但 `TMap<K, ?>` 还要求 K **必须能算哈希**。

### 3.1 `FAngelscriptType::CanHashValue` 接口

```cpp
// Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h:277
virtual bool CanHashValue(const FAngelscriptTypeUsage& Usage) const { return false; }
virtual uint32 GetHash(const FAngelscriptTypeUsage& Usage, const void* Address) const
    { ensure(false); return 0; }
```

默认值为 `false`——任何不主动 override 的类型都**不可作为键**。重写它的类型有：

| 重写者 | 实现策略 | 覆盖范围 |
|--------|----------|----------|
| `TAngelscriptCppType<NativeType>` (Helper_CppType.h:82) | `TModels<CGetTypeHashable, NativeType>::Value` 编译期检测 | `int32` / `float` / `FName` / `FString` / 任何特化 `GetTypeHash` 的 C++ 类型 |
| `TAngelscriptPODType<...>` (Helper_PODType.h:46) | 同上，转发到 `GetTypeHash` | `EnumProperty` 包装的 POD enum |
| `FAngelscriptStructType` (Bind_UStruct.cpp:227) | `Ops->HasGetTypeHash()` —— UHT 标记的 `UScriptStruct` | C++ 端 `USTRUCT()` 且 `WithIdenticalViaEquality + WithGetTypeHash` 的 struct |
| `FAngelscriptEnumType` (Bind_UEnum.cpp:231) | `return true`，转发到 `int64` 的 `GetTypeHash` | 任何 `UENUM()` |
| `FAngelscriptBlueprintType` (Bind_BlueprintType.cpp:296) | UClass `*` 直接哈希指针 | `UObject` 派生类（指针哈希） |

### 3.2 脚本侧 `struct` 作为键：`uint32 Hash() const` 退路

如果键是脚本侧定义的 `struct` 或 `class`，UHT 编译期不可能为它生成 `GetTypeHash` 特化——`KeyType.CanHashValue()` 默认返回 `false`。`ValidateMapOperations` 在拒绝前先尝试一条退路：

```cpp
if (asCTypeInfo* SubType = (asCTypeInfo*)TemplateType->GetSubType(0))
{
    auto* ObjectType = CastToObjectType(SubType);
    if (ObjectType != nullptr && ObjectType->GetFirstMethod("Hash") != nullptr)
    {
        Ops->HashFunction = SubType->GetMethodByDecl("uint32 Hash() const");
        bCanHash = Ops->HashFunction != nullptr;
    }
}
```

只要脚本类定义了 `uint32 Hash() const`，`HashFunction` 字段就指向它，运行时通过反射调用：

```cpp
// Bind_TMap.h:114
uint32 FMapOperations::InvokeHashFunction(const void* Object) const
{
    FAngelscriptContext Context(HashFunction->GetEngine());
    PrepareAngelscriptContextWithLog(Context, HashFunction, ...);
    Context->SetObject(const_cast<void*>(Object));   // ★ this 指针
    Context->Execute();                              // ★ 反射执行
    return Context->GetReturnDWord();
}

FORCEINLINE uint32 FMapOperations::HashKey(const void* Ptr) const
{
    if (HashFunction != nullptr)
        return InvokeHashFunction(Ptr);              // 脚本侧 Hash()
    return KeyType.GetHash(Ptr);                     // 内置 GetTypeHash
}
```

```angelscript
// AS 端示例
struct FCompositeKey
{
    int X;
    int Y;

    bool opEquals(const FCompositeKey& Other) const { return X == Other.X && Y == Other.Y; }
    uint32 Hash() const { return uint32(X * 73856093) ^ uint32(Y * 19349663); }
}

TMap<FCompositeKey, FString> Lookup;     // ★ 通过, 因为有 opEquals + Hash
Lookup.Add(FCompositeKey(1, 2), "hello");
```

性能代价：每次插入/查找都触发一次 AS 反射调用——比 `int` / `FName` 这种内置 `GetTypeHash` 慢 **1-2 个量级**。Hot path 上应优先选可哈希的 C++ 类型作键。

### 3.3 键类型选择决策表

| 键类型 | 可用？ | 哈希路径 | 备注 |
|--------|--------|----------|------|
| `int` / `int64` / `uint32` | ✓ | 内置 `GetTypeHash(int)` | 最快 |
| `float` / `double` | ✓ | 内置 | 注意浮点精度——`0.1 + 0.2 ≠ 0.3` 会哈希到不同槽 |
| `FName` | ✓ | 名字索引 | UE 推荐键类型 |
| `FString` | ✓ | 字符串内容哈希 | 比 `FName` 慢 |
| `FText` | ✗ | 无 `GetTypeHash` | 编译期拒绝 |
| `UObject*` 派生类 | ✓ | 指针哈希 | 弱引用陷阱（见 §十） |
| `TSubclassOf<>` | ✓ | UClass 指针哈希 | 仅协变, 实质是指针 |
| `USTRUCT() FVector` | ✗（默认） | 引擎未生成 GetTypeHash | 须自定义 `WithGetTypeHash` |
| `USTRUCT() FGuid` | ✓ | UE 标准提供 GetTypeHash | 推荐做强标识符 |
| 脚本 `struct FXxx` | 条件✓ | 必须实现 `uint32 Hash() const` | 反射调用，慢 |
| `TArray<...>` | ✗ | 容器不可作子类型 | 编译期拒绝 |
| `TSet<...>` / `TMap<...>` | ✗ | 同上 | |

---

## 四、`FAngelscriptMapType` —— UE 反射桥接

**源码所在**：`Bind_TMap.cpp:60-533`。

### 4.1 接口职责映射

| 接口 | 实现 | 作用 |
|------|------|------|
| `GetAngelscriptTypeName` | 返回 `"TMap"` | 调试/错误消息 |
| `CanQueryPropertyType` | `return false` | 不暴露属性查询入口（值/键独立各自查） |
| `CanBeTemplateSubType` | `return false` | **禁止嵌套容器** |
| `HasReferences` | `Key.HasReferences() OR Value.HasReferences()` | GC 是否需要追踪 |
| `EmitReferenceInfo` | 见 §4.2 | GC Schema 写入 |
| `CanCreateProperty` | `K.CanCreateProperty() && K.CanHashValue() && V.CanCreateProperty()` | UPROPERTY 字段创建前置条件 |
| `CreateProperty` | `new FMapProperty + KeyProp + ValueProp + MapLayout` | 创建反射字段 |
| `MatchesProperty` | 对 `FMapProperty.KeyProp/ValueProp` 双向递归匹配 | 反射调用参数校验 |
| `CanCopy/NeedCopy/CopyValue` | 必须深拷贝（遍历元素 `Add` 到目标） | 值赋值/参数传递 |
| `CanConstruct/ConstructValue` | placement-new `FScriptMap()` | 字段初始化 |
| `CanDestruct/DestructValue` | `Empty(Map, 0)` 析构所有元素 + `~FScriptMap()` | 字段销毁 |
| `GetValueSize/Alignment` | `sizeof(FScriptMap) / alignof(FScriptMap)` | 类生成 |
| `SetArgument/GetReturnValue` | `StepCompiledIn<FMapProperty>` | UFunction 互调 |
| `CanCompare/IsValueEqual` | `Ops.IsPermutation` 多重计数比对 | `==` 运算符 |
| `GetDebuggerValue/Scope/Member` | KV pair 嵌套展开 | DAP 调试器 |
| `GetCppForm` | 输出 `TMap<K_CppType, V_CppType>` | StaticJIT 代码生成 |

### 4.2 GC Schema：单一 `EMemberType::StructSet`

```cpp
// ============================================================================
// 文件: Bind_TMap.cpp
// 函数: FAngelscriptMapType::EmitReferenceInfo
// ============================================================================
void FAngelscriptMapType::EmitReferenceInfo(const FAngelscriptTypeUsage& Usage, FGCReferenceParams& Params) const
{
    auto MapLayout = FScriptMap::GetScriptLayout(
        Usage.SubTypes[0].GetValueSize(), Usage.SubTypes[0].GetValueAlignment(),
        Usage.SubTypes[1].GetValueSize(), Usage.SubTypes[1].GetValueAlignment());

    UE::GC::FSchemaBuilder InnerSchema(MapLayout.SetLayout.Size);
    if (Usage.SubTypes[0].HasReferences())
    {
        FGCReferenceParams InnerParams = Params;
        InnerParams.Schema = &InnerSchema;
        InnerParams.AtOffset = 0;                              // Key 在 Pair 起始
        Usage.SubTypes[0].EmitReferenceInfo(InnerParams);
    }

    if (Usage.SubTypes[1].HasReferences())
    {
        FGCReferenceParams InnerParams = Params;
        InnerParams.Schema = &InnerSchema;
        InnerParams.AtOffset = MapLayout.ValueOffset;          // Value 在 ValueOffset
        Usage.SubTypes[1].EmitReferenceInfo(InnerParams);
    }

    Params.Schema->Add(UE::GC::DeclareMember(Params.Names.Top(), Params.AtOffset,
        UE::GC::EMemberType::StructSet, InnerSchema.Build()));
}
```

与 `TArray` 对照：

| 容器 | GC EMemberType | 含义 |
|------|----------------|------|
| `TArray<UObject*>` | `ReferenceArray` | 每槽是 `UObject*`，逐槽 mark |
| `TArray<FStructWithRef>` | `StructArray` | 每槽是 struct，按 sub-Schema 进入 |
| `TMap<K含引用, V含引用>` | **`StructSet`** | 每槽是 Pair（key+value），按 sub-Schema 进入；只要 K 或 V 任一含引用就走这条 |

`StructSet` 反映 `FScriptMap` 内部本质是 `FScriptSet<TPair<K, V>>` 的事实。**没有** `ReferenceMap` 或 `ReferenceSet` 这样的快路径——即使 `TMap<UObject*, int>`，GC 也走 `StructSet` 进入 sub-Schema。

### 4.3 `CreateProperty` —— `FMapProperty` 的双 inner

```cpp
// ============================================================================
// 文件: Bind_TMap.cpp
// 函数: FAngelscriptMapType::CreateProperty
// ============================================================================
FProperty* CreateProperty(const FAngelscriptTypeUsage& Usage, const FPropertyParams& Params) const override
{
    auto* MapProp = new FMapProperty(Params.Outer, Params.PropertyName, RF_Public);

    {
        FPropertyParams InnerParams = Params;
        InnerParams.Outer = MapProp;
        InnerParams.PropertyName = *(Params.PropertyName.ToString() + TEXT("_Key"));
        MapProp->KeyProp = Usage.SubTypes[0].CreateProperty(InnerParams);
    }
    {
        FPropertyParams InnerParams = Params;
        InnerParams.Outer = MapProp;
        InnerParams.PropertyName = *(Params.PropertyName.ToString() + TEXT("_Value"));
        MapProp->ValueProp = Usage.SubTypes[1].CreateProperty(InnerParams);
    }

    MapProp->MapLayout = FScriptMap::GetScriptLayout(...);     // ★ 同上 Layout
    return MapProp;
}
```

UE 约定：`FMapProperty` 子属性命名 `<Field>_Key` / `<Field>_Value`，与 `FArrayProperty.Inner` 命名 `<Field>_Inner` 同规则。`MapLayout` 在 property 上独立缓存一份——序列化、复制、垃圾收集都依赖它。

### 4.4 反向桥接：`FMapProperty` → `FAngelscriptTypeUsage`

```cpp
FAngelscriptType::RegisterTypeFinder([MapType](FProperty* Property, FAngelscriptTypeUsage& Usage) -> bool
{
    FMapProperty* MapProp = CastField<FMapProperty>(Property);
    if (MapProp == nullptr) return false;

    FAngelscriptTypeUsage KeyType = FAngelscriptTypeUsage::FromProperty(MapProp->KeyProp);
    if (!KeyType.IsValid()) return false;

    FAngelscriptTypeUsage ValueType = FAngelscriptTypeUsage::FromProperty(MapProp->ValueProp);
    if (!ValueType.IsValid()) return false;

    Usage.Type = MapType;
    Usage.SubTypes.Add(KeyType);
    Usage.SubTypes.Add(ValueType);
    return true;
});
```

C++ 端 `UPROPERTY() TMap<int32, FVector> Lookup;` 会被反向解析为 `FAngelscriptTypeUsage{ Type=MapType, SubTypes=[Int, FVector] }`，AS 端 `MyClass.Lookup.Add(...)` 直接可用。

### 4.5 `CopyValue` 的特殊性：永远遍历重建

```cpp
void CopyValue(const FAngelscriptTypeUsage& Usage, void* SourcePtr, void* DestinationPtr) const override
{
    FScriptMap& Source = *(FScriptMap*)SourcePtr;
    FScriptMap& Destination = *(FScriptMap*)DestinationPtr;

    FMapOperations Ops(Usage.SubTypes[0], Usage.SubTypes[1]);
    Ops.Empty(Destination, Source.Num());
    for (int32 i = 0, Num = Source.GetMaxIndex(); i < Num; ++i)
    {
        if (Source.IsValidIndex(i))
            Ops.Add(Destination, Ops.GetKey(Source, i), Ops.GetValue(Source, i));
    }
}
```

注意三点与 `TArray::OpAssign` 的差异：

1. **不能 `memcpy` 整块 buffer**：`FScriptMap` 内部是 sparse array + hash 桶 + `FreeList` 链表，直接复制内存会让多个 `FScriptMap` 实例共享同一份 buffer 后造成 double-free。
2. **每个 KV 对要走 `Add` 重新哈希**：因为目的端的桶可能与源端布局不同（`Empty(Slack)` 之后桶大小重新计算）。
3. **POD vs 非 POD 没有快路径**：与 `TArray` 不同，无论键值是否平凡，都走遍历 `Add`。这意味着 `TMap<int, int>` 的拷贝**不会**自动被向量化为 `memcpy`。

---

## 五、关键操作实现剖析

### 5.1 `Add` —— 7-lambda 注入式实现

```cpp
// ============================================================================
// 文件: Bind_TMap.h
// 函数: FMapOperations::Add (注入式 lambda 接到 FScriptMap::Add)
// ============================================================================
FORCEINLINE_DEBUGGABLE void FMapOperations::Add(FScriptMap& Map, void* Key, void* Value)
{
    auto GetKeyHash = [this](const void* Ptr) -> uint32 { return HashKey(Ptr); };
    auto KeyEqualityFn = [this](const void* A, const void* B) -> bool
        { return KeyType.IsValueEqual((void*)A, (void*)B); };

    auto KeyConstructAndAssignFn = [this, Key](void* Ptr) {
        if (bKeyNeedConstruct) KeyType.ConstructValue(Ptr);
        if (bKeyNeedCopy) KeyType.CopyValue(Key, Ptr);
        else FMemory::Memcpy(Ptr, Key, KeySize);
    };

    auto ValueConstructAndAssignFn = [this, Value](void* Ptr) { /* 同 Key 逻辑 */ };
    auto ValueAssignFn = [this, Value](void* Ptr) { /* 仅 copy, 不 construct */ };
    auto DestructKeyFn = [this](void* Ptr) { if (bKeyNeedDestruct) KeyType.DestructValue(Ptr); };
    auto DestructValueFn = [this](void* Ptr) { if (bValueNeedDestruct) ValueType.DestructValue(Ptr); };

    Map.Add(Key, Value, Layout,
        GetKeyHash, KeyEqualityFn,
        KeyConstructAndAssignFn,
        ValueConstructAndAssignFn,    // 新槽: 构造 + 拷贝
        ValueAssignFn,                // 已存在键: 仅拷贝, 复用旧槽
        DestructKeyFn, DestructValueFn);
}
```

**七个 lambda 的语义**：
- `GetKeyHash`：计算键哈希，分派到 `HashFunction` 或内置 `GetTypeHash`
- `KeyEqualityFn`：哈希冲突时桶内比对
- `KeyConstructAndAssignFn`：新槽插入键时构造+拷贝
- `ValueConstructAndAssignFn`：新槽插入值时构造+拷贝
- `ValueAssignFn`：键已存在时**仅拷贝值**（不重新构造，复用现有内存）—— 这是 `Add(Key, NewValue)` 实现"覆盖语义"的关键
- `DestructKeyFn` / `DestructValueFn`：覆盖旧值时析构原值

`FAngelscriptMapBinds::Add`（绑定层）在调用 `Ops->Add` 前还做迭代守卫与引用失效：

```cpp
void FAngelscriptMapBinds::Add(FScriptMap& Map, asCObjectType* Meta, void* Key, void* Value)
{
#if AS_ITERATOR_DEBUGGING
    if (!CheckMapIteratorDebug(Map)) return;                  // 迭代中改写直接 Throw
#endif
    auto* Ops = FMapOperations::GetMapOperations(Meta);
#if AS_REFERENCE_DEBUGGING
    InvalidateReferencesToMap(Map, Ops);                      // 让 buffer 内的 ASRef 失效
#endif
    Ops->Add(Map, Key, Value);
}
```

### 5.2 `OpIndex` —— 不存在即 `Throw`

```cpp
void* FAngelscriptMapBinds::OpIndex(FScriptMap& Map, asCObjectType* Meta, void* Key)
{
    auto* Ops = FMapOperations::GetMapOperations(Meta);
    int32 Index = Ops->FindPairIndex(Map, Key);
    if (!Map.IsValidIndex(Index))
    {
        FAngelscriptEngine::Throw("Could not find key in map for index operator.");
        return nullptr;
    }
    return Ops->GetValue(Map, Index);
}
```

注意 AS 端 `Map[Key]` 找不到键**直接抛错**——与 `std::unordered_map::operator[]` "默认插入"语义不同。要默认插入需用 `FindOrAdd`：

```angelscript
TMap<FString, int> M;
M["miss"];               // ✗ AS 抛错: "Could not find key in map for index operator."
M.FindOrAdd("miss");     // ✓ 找不到时插入默认值 0, 返回引用
```

### 5.3 `Find` / `RemoveAndCopyValue` —— OutValue 拷贝优化

```cpp
bool FAngelscriptMapBinds::Find(FScriptMap& Map, asCObjectType* Meta, void* Key, void* OutValue)
{
    auto* Ops = FMapOperations::GetMapOperations(Meta);
    int32 Index = Ops->FindPairIndex(Map, Key);
    if (Index == INDEX_NONE) return false;

    void* FoundValue = Ops->GetValue(Map, Index);
    if (Ops->bValueNeedCopy)
        Ops->ValueType.CopyValue(FoundValue, OutValue);       // 非 POD 走真拷贝
    else
        FMemory::Memcpy(OutValue, FoundValue, Ops->ValueSize); // POD 走 memcpy
    return true;
}
```

值类型 POD（`int` / `FVector` / `FName` 等）走 `Memcpy` —— 这是 `TMap` 与 `TArray` 共有的 POD 快路径。

### 5.4 `FindOrAdd_Defaulted` —— 临时 buffer + 默认构造

```cpp
void* FAngelscriptMapBinds::FindOrAdd_Defaulted(FScriptMap& Map, asCObjectType* Meta, void* Key)
{
    auto* Ops = FMapOperations::GetMapOperations(Meta);
    int32 Index = Ops->FindPairIndex(Map, Key);
    if (Index == INDEX_NONE)
    {
        TArray<uint8, TInlineAllocator<64>> TempValue;
        TempValue.SetNumZeroed(Ops->ValueSize + 16);
        void* ValuePtr = Align(TempValue.GetData(), Ops->ValueAlignment);

        if (Ops->bValueNeedConstruct)
            Ops->ValueType.ConstructValue(ValuePtr);            // 默认构造到栈缓冲

        Ops->Add(Map, Key, ValuePtr);                           // 经 7-lambda 拷入 Map
        Index = Ops->FindPairIndex(Map, Key);

        if (Ops->bValueNeedDestruct)
            Ops->ValueType.DestructValue(ValuePtr);             // 析构栈缓冲
    }
    return Ops->GetValue(Map, Index);                           // 返回 Map 内的引用
}
```

**为什么不直接传 nullptr 给 `Ops->Add`？** 因为 `Ops->Add` 的 `ValueConstructAndAssignFn` 必须**有值可拷**——它只做"构造 + 从外部 Value 拷贝"，不会"原地默认构造"。所以中转一份栈缓冲来承载默认值。

`TInlineAllocator<64>` 让 ValueSize ≤ 64 的常见情形零堆分配；超过 64 字节才走堆。`+ 16` 余量是为对齐：若 `ValueAlignment > 1`，`Align` 调用可能向后偏移最多 `ValueAlignment - 1` 字节，预留 16 足够覆盖到 `__m128`（16 对齐）。

### 5.5 `OpEquals` —— `IsPermutation` 多重计数

```cpp
bool FAngelscriptMapBinds::OpEquals(FScriptMap& MapA, asCObjectType* Meta, FScriptMap& MapB)
{
    auto* Ops = FMapOperations::GetMapOperations(Meta);
    if (!Ops->KeyType.CanCompare() || !Ops->ValueType.CanCompare())
    {
        FAngelscriptEngine::Throw("Cannot compare map key/value type for equality.");
        return false;
    }
    return Ops->IsPermutation(MapA, MapB);
}
```

`IsPermutation`（`Bind_TMap.h:319`）的算法：

```text
1. Num(A) != Num(B)         -> 立即 false
2. 公共前缀逐对比对          -> 全等到底则 true
3. 第一个不等的位置开始,
   对剩余子集做 "多重计数比对":
     for 每个 PairA in A 剩余:
       count_A = A 剩余中等于 PairA 的次数
       count_B = B 剩余中等于 PairA 的次数
       count_A != count_B -> false
   全部计数一致 -> true
```

**O(N²) 复杂度**——这是 TMap 比较语义"无序但允许重复键的多重集相等"的代价。注意脚本侧 `TMap` 是**单值映射**（`Add(K, V)` 同 K 会覆盖），所以 `count_A` / `count_B` 实际只会是 0 或 1，但代码保留了一般情况以与 UE 内部 `OrderIndependentCompareEqual` 行为一致。

### 5.6 删除变体：仅 2 种（vs `TArray` 的 6 种）

| 方法 | 行为 | 复杂度 | 语义 |
|------|------|--------|------|
| `Remove(K)` | 删除键对应的 KV 对 | 平均 O(1) | `bool` 返回是否删除成功 |
| `RemoveAndCopyValue(K, V&)` | 删除并把值拷贝出来 | 平均 O(1) | 同上，多一次拷贝 |
| ~~`RemoveAt`~~ | **未暴露** | — | 内部 `Ops->RemoveAt` 仅供迭代器 `RemoveCurrent` 使用 |

`TMap` 没有"按值"删除（`Remove(value)`）—— 因为值类型不要求 `CanCompare`。要按值删除需手工遍历 + `RemoveAndCopyValue`。

### 5.7 `Empty` vs `Reset` 的微妙差异

```cpp
void Empty(FScriptMap& Map, asCObjectType* Meta, int32 Slack)  { Ops->Empty(Map, Slack); }
void Reset(FScriptMap& Map, asCObjectType* Meta)               { Ops->Empty(Map, Map.Num()); }
```

- `Empty(Slack)`：清空并保留 `Slack` 大小的桶预留
- `Reset()`：清空并保留**当前 Num** 大小的桶预留（即"将立即填回相同规模数据"的优化）

两者都遍历析构所有 KV。

### 5.8 `GetKeys` / `GetValues` —— 遍历填充 `FScriptArray`

```cpp
void FAngelscriptMapBinds::GetKeys(FScriptMap& Map, asCObjectType* Meta, FScriptArray& OutKeys)
{
    auto* Ops = FMapOperations::GetMapOperations(Meta);
    int32 ArrayIndex = 0;
    for (int32 SlotIndex = 0; SlotIndex < Map.GetMaxIndex(); ++SlotIndex)
    {
        if (Map.IsValidIndex(SlotIndex))
        {
            OutKeys.Insert(ArrayIndex, 1, Ops->KeySize, Ops->KeyAlignment);
            uint8* DestPtr = static_cast<uint8*>(OutKeys.GetData()) + (ArrayIndex * Ops->KeySize);

            if (Ops->bKeyNeedConstruct) Ops->KeyType.ConstructValue(DestPtr);
            if (Ops->bKeyNeedCopy) Ops->KeyType.CopyValue(KeyPtr, DestPtr);
            else FMemory::Memcpy(DestPtr, KeyPtr, Ops->KeySize);

            ArrayIndex++;
        }
    }
}
```

注意三点：

- **逐个 `Insert`** 而非一次性扩容——是为了通过 `FScriptArray::Insert` 走它的对齐路径，避免 `TMap` 与 `TArray` 之间共享 alignment 计算逻辑的耦合。
- **直接写裸内存**：跳过 `FArrayOperations::Add` 的别名/迭代守卫——因为 `OutKeys` 是新数组，绝不可能与 `Map` 别名。
- 源码中 `GetValues` 第 1054 行 `if (Ops->bKeyNeedConstruct)` 与 1059 行 `if (Ops->bKeyNeedCopy)` 是**笔误**，本应写 `bValueNeedConstruct/Copy`——结果在键与值的 POD 标志位刚好同步时无影响，但若键是 POD 而值非 POD（罕见），可能跳过值的非平凡拷贝。这是已知的潜在 bug，但当前所有内置类型都在两个标志位上一致，未触发。

---

## 六、`for` 范围循环：稀疏索引下的五件套

**源码所在**：`Bind_TMap.cpp:1222-1283`。

### 6.1 与 `TArray` 的关键差异

`TArray` 的 `for` 用**密集索引** 0..N-1，`opForNext` 简单 `++It`。`TMap` 的 `FScriptMap` 内部是 sparse array（删除不压缩），有效槽随机分布，因此：

```cpp
TMap_.Method("int opForBegin()", [](FScriptMap& Map, asCObjectType* Meta) -> int32
{
    return FMapOperations::GetMapOperations(Meta)->FindNextIndex(Map, -1);   // ★ -1 起步, 找首个有效槽
});

TMap_.Method("bool opForEnd(const int Iterator) const",
    [](FScriptMap&, int32 Iterator) -> bool { return Iterator == -1; });

TMap_.Method("void opForNext(int&inout Iterator)",
    [](FScriptMap& Map, asCObjectType* Meta, int32& Iterator)
{
    if (Iterator == -1) return;
    Iterator = FMapOperations::GetMapOperations(Meta)->FindNextIndex(Map, Iterator);
});
```

`FindNextIndex` 实现：

```cpp
FORCEINLINE_DEBUGGABLE int32 FMapOperations::FindNextIndex(FScriptMap& Map, int32 Index)
{
    int32 MaxIndex = Map.GetMaxIndex();
    for (int32 NextIndex = Index + 1; NextIndex < MaxIndex; ++NextIndex)
        if (Map.IsValidIndex(NextIndex))
            return NextIndex;
    return -1;
}
```

最坏 O(MaxIndex)，但平均 O(1)（除非删除导致碎片化严重）。

### 6.2 `opForKey` —— 数组返索引、Map 返键拷贝

```cpp
TMap_.Method("const K& opForKey(const int Iterator) const",
    [](FScriptMap& Map, asCObjectType* Meta, int32 Iterator) -> void*
{
    auto* Ops = FMapOperations::GetMapOperations(Meta);
    if (!Map.IsValidIndex(Iterator))
    {
        FAngelscriptEngine::Throw("Iterator out of bounds.");
        return nullptr;
    }
    return Ops->GetKey(Map, Iterator);
});
```

`opForKey` 返回**键的引用**而非"槽位索引"——这与 `TArray::opForKey` 返回 `int Index` 形成鲜明对比。这是因为 AS 编译器对 `for (auto K, V : Map)` 的展开期望 K 是"业务键"而非"内部位置"：

```angelscript
// AS 源码
for (auto K, V : Lookup)
    Print(f"{K}: {V}");

// 编译器改写为
int __it = Lookup.opForBegin();
while (!Lookup.opForEnd(__it))
{
    auto K = Lookup.opForKey(__it);     // 键引用
    auto V = Lookup.opForValue(__it);   // 值引用
    Print(f"{K}: {V}");
    Lookup.opForNext(__it);
}
```

注意 `Example_Map.as` 里官方写法是 `for (auto Element : Map) { Element.GetKey(); Element.GetValue(); }`——这条路径其实**没用** `opForKey/opForValue` 五件套，而是直接构造 `TMapIterator<K, V>` 后让 AS 编译器走 `Element.GetKey()` 方法调用。两条路径并存（详见 §七）。

---

## 七、`TMapIterator` / `TMapConstIterator` —— 手工迭代器

**源码所在**：`Bind_TMap.h:375-497`、`Bind_TMap.cpp:1188-1220`。

### 7.1 数据结构

```cpp
struct FMapIterator
{
    FScriptMap*      Map;          // 被迭代的 Map
    FMapOperations*  Ops;          // 元素操作集 (避免每次查 plainUserData)
    int32            Index;        // 当前槽
    int32            NextIndex;    // 下一槽 (预先算好, Proceed 一步到位)
    bool             bCanProceed;  // 是否还有元素

    static FMapIterator Create(FScriptMap& Map, asCObjectType* Meta);   // ★ 工厂
    FMapIterator& Proceed();                                             // 步进
    void* GetKey() const;
    void* GetValue() const;
    void  SetValue(void* NewValue);
    void  RemoveCurrent();
};
```

与 `FArrayIterator` 的两个关键差异：

| 维度 | `FArrayIterator` | `FMapIterator` |
|------|-----------------|----------------|
| Stride 字段 | `uint32 Stride`（运行时元素大小） | 用 `Ops->Layout` 间接计算（无独立字段） |
| `Proceed()` 行为 | 取当前值并步进（值兼步进） | **仅步进**，取值用独立的 `GetValue()` / `GetKey()` |
| 删除当前元素 | 无 | `RemoveCurrent()` 显式接口 |
| `SetValue` | 无（直接 `It.Value = X`）| 显式方法 |

### 7.2 工厂方法 + 迭代守卫

```cpp
static FMapIterator FMapIterator::Create(FScriptMap& Map, asCObjectType* Meta)
{
    auto* Ops = FMapOperations::GetMapOperations(Meta);

    FMapIterator It;
    It.Map = &Map;
    It.Ops = Ops;
    It.Index = -1;
    It.NextIndex = It.Ops->FindNextIndex(Map, -1);
    It.bCanProceed = It.NextIndex != -1;

#if AS_ITERATOR_DEBUGGING
    FMapOperations::MarkMapBeingIterated(Map);                // ★ 入表
#endif
    return It;
}

#if AS_ITERATOR_DEBUGGING
static void FMapIterator::Destruct(FMapIterator& Iterator)
{
    if (Iterator.Map != nullptr)
        FMapOperations::UnmarkMapBeingIterated(*Iterator.Map);   // ★ 出表
}
#endif

FMapIterator& FMapIterator::Assignment(const FMapIterator& Other)
{
#if AS_ITERATOR_DEBUGGING
    if (Map != nullptr)        FMapOperations::UnmarkMapBeingIterated(*Map);
    if (Other.Map != nullptr)  FMapOperations::MarkMapBeingIterated(*Other.Map);
#endif
    *this = Other;
    return *this;
}
```

注意 `CopyConstruct` / `Assignment` / `Destruct` 三处都要维护 `GMapsBeingIterated` 表——值类型迭代器在 AS 里频繁拷贝，每一次都必须正确增减引用计数（实质是位置标记）。

### 7.3 AS 端用法：两种范式

**范式 A：`for (auto E : Map)` —— 隐式迭代器**

```angelscript
for (auto Element : Lookup)                  // Element 类型是 TMapIterator<K,V>
{
    Print(f"Key={Element.GetKey()}, Value={Element.GetValue()}");
    if (Element.GetKey() == "victim")
        Element.SetValue(99);                // 引用语义, 直写 Map
}
```

AS 编译器对 `for (auto X : TMapContainer)` 的展开**不走 `opForBegin/Value/Key` 五件套**——而是查找 `Iterator()` 方法，构造 `TMapIterator<K,V>`，然后逐字段走 `Proceed()` / `bCanProceed`。看这段绑定：

```cpp
TMap_.Method("TMapIterator<K,V> Iterator()", FUNC_TRIVIAL(FMapIterator::Create));
```

而五件套绑定（`opForBegin/End/Next/Value/Key`）的存在主要是 fallback 与 `for (auto K, V : ...)`（双变量结构化）形式的支持。

**范式 B：显式手工迭代**

```angelscript
TMapIterator<FName, int> It = Lookup.Iterator();
while (It.CanProceed)
{
    It.Proceed();
    if (ShouldRemove(It.GetKey()))
        It.RemoveCurrent();                  // 安全: RemoveAt 不影响 NextIndex
}
```

`RemoveCurrent` 安全的原因：`Ops->RemoveAt(Map, Index)` 把当前槽 `IsValidIndex` 改为 false，但 `NextIndex` 已经在前一次 `Proceed()` 时算过；下一次 `Proceed()` 直接跳到 `NextIndex`。

### 7.4 const vs 非 const 迭代器

```cpp
TMap_.Method("TMapIterator<K,V> Iterator()", FUNC_TRIVIAL(FMapIterator::Create));
TMap_.Method("TMapConstIterator<K,V> Iterator() const", FUNC_TRIVIAL(FMapIterator::Create));
```

两者**底层对象都是 `FMapIterator`**——`TMapConstIterator<K,V>` 只是 AS 类型系统里的一个独立 `ValueClass`，绑定的方法集少了 `SetValue` 与 `RemoveCurrent`。区分纯靠 AS 端的方法表，不在 C++ 端做任何运行时分支。

---

## 八、调试器集成：嵌套 KV 与 `TypeIndex` 编码

**源码所在**：`Bind_TMap.cpp:258-485`、`535-777`（迭代器调试器）。

### 8.1 顶层值显示

```cpp
bool GetDebuggerValue(...) const override
{
    FScriptMap& Map = Usage.ResolvePrimitive<FScriptMap>(Address);
    int32 Num = Map.Num();
    Value.Value = Num == 0 ? TEXT("Empty") : FString::Printf(TEXT("Num = %d"), Num);
    Value.bHasMembers = true;
    return true;
}
```

VS Code 调试器面板会显示 `Lookup    Num = 3`，可点开。

### 8.2 子项展开：`TypeIndex` 双编码

`FAngelscriptMapType::GetDebuggerScope` 用 `Usage.TypeIndex` 区分两层：

```text
TypeIndex == 0  -> 顶层, 列出所有 KV 对作为子项
TypeIndex >= 1  -> 在 (TypeIndex - 1) 这个 Pair 内部, 列出 Key 与 Value 两个子项
```

```cpp
if (Usage.TypeIndex != 0)
{
    int32 MapIndex = Usage.TypeIndex - 1;                    // ★ 解码
    if (!Map.IsValidIndex(MapIndex)) return false;
    // 展开 Key + Value 两条
    Scope.Values.Add(KeyValue with Name="Key");
    Scope.Values.Add(ValueValue with Name="Value");
    return true;
}

// TypeIndex == 0: 顶层
for (int32 i = 0; i < Map.GetMaxIndex(); ++i)
{
    if (!Map.IsValidIndex(i)) continue;

    if (KeyType.GetStringIdentifier(KeyPtr, ElemValue.Name))
    {
        // 键有字符串表示 -> 用 [Key] 显示, 不嵌套
        ElemValue.Name = FString::Printf(TEXT("[%s]"), *ElemValue.Name);
        Scope.Values.Add(MoveTemp(ElemValue));
    }
    else
    {
        // 键无字符串 ID -> 用 [i] 包裹一对 KV (展开后看到 Key + Value)
        FDebuggerValue PairValue;
        PairValue.Name = FString::Printf(TEXT("[%d]"), i);
        PairValue.Value = FString::Printf(TEXT("%s: %s"), *KeyValue.Value, *ElemValue.Value);
        PairValue.Usage = Usage;
        PairValue.Usage.TypeIndex = 1 + i;                    // ★ 编码
        PairValue.bHasMembers = true;
        Scope.Values.Add(MoveTemp(PairValue));
    }
}
```

调试器面板效果：

```text
TMap<FString, int> Lookup       Num = 3
  ["Alpha"]   1                  // 键有 GetStringIdentifier -> 一行展示
  ["Beta"]    2
  ["Gamma"]   3

TMap<FCustomStruct, FVector>    Num = 2
  [0]   <X=1,Y=2>: (1,0,0)       // 键无字符串 ID -> 显示 [槽位] 后可展开
    Key   X=1, Y=2
    Value (1, 0, 0)
  [1]   ...
```

### 8.3 通过 Member 字符串查询：`Lookup["Alpha"]` 调试器表达式

```cpp
bool GetDebuggerMember(..., const FString& Member, ...) const override
{
    if (Member.StartsWith(TEXT("[")) && Member.EndsWith(TEXT("]")))
    {
        FString Identifier = Member.Mid(1, Member.Len() - 2);
        int32 Index = -1;

        void* KeyBuffer = (void*)FMemory_Alloca(Ops.KeySize);
        bool bHasKeyBuffer = false;
        if (KeyType.FromStringIdentifier(Identifier, KeyBuffer))
        {
            Index = Ops.FindPairIndex(Map, KeyBuffer);            // 用键查
            bHasKeyBuffer = true;
        }
        else
        {
            LexFromString(Index, *Identifier);                    // 用槽位号查
        }
        // ... 拿到 Index 后返回对应 DebuggerValue
        if (bHasKeyBuffer && KeyType.CanDestruct() && KeyType.NeedDestruct())
            KeyType.DestructValue(KeyBuffer);                     // 释放 alloca 上构造的键
    }
}
```

VS Code 的 Watch 表达式 `Lookup["Alpha"]` 会先尝试用字符串 `"Alpha"` 反序列化为键（`FromStringIdentifier`），找到则按键查；失败则按整数槽位号查。`KeyBuffer` 在 `_alloca` 上构造，必须显式析构以避免泄漏（典型的 FString 在 alloca 上构造时持有的 SharedRef）。

---

## 九、StaticJIT 直通：模板特化路径

**源码所在**：`StaticJITBinds.h:91-114`、`Bind_TMap.h:513-645`（`*_Template<K, V>` 系列）。

### 9.1 宏的双面孔

```cpp
// StaticJIT 启用时:
#define SCRIPT_NATIVE_TEMPLATED_CALL(Binds, Name, Trivial)
    FScriptFunctionNativeForm::BindTemplateInstantiatedCall(Binds, Name, Trivial, false, false)

#define SCRIPT_NATIVE_TEMPLATED_CALL_NEEDSCOMPARE(Binds, Name, Trivial)
    FScriptFunctionNativeForm::BindTemplateInstantiatedCall(Binds, Name, Trivial, true, false)

#define SCRIPT_NATIVE_TEMPLATED_CALL_NEEDSCOPY(Binds, Name, Trivial)
    FScriptFunctionNativeForm::BindTemplateInstantiatedCall(Binds, Name, Trivial, false, true)

#define SCRIPT_NATIVE_TEMPLATED_CALL_NEEDSCOPY_NEEDSCOMPARE(Binds, Name, Trivial)
    FScriptFunctionNativeForm::BindTemplateInstantiatedCall(Binds, Name, Trivial, true, true)
```

四种宏组合（NeedsCompare × NeedsCopy）覆盖所有 TMap 方法的子类型能力依赖：

| TMap 方法 | 用的宏 | 含义 |
|-----------|--------|------|
| `Empty` / `Reset` / `Destructor` | `SCRIPT_NATIVE_TEMPLATED_CALL` | 仅需要 size/alignment，无依赖 |
| `Contains` | `..._NEEDSCOMPARE` | 需要键 `opEquals` 做哈希桶比对 |
| `OpIndex` | `..._NEEDSCOMPARE` | 同上 |
| `Add` | `..._NEEDSCOPY_NEEDSCOMPARE` | 既要键比对又要值拷贝 |
| `Find` / `RemoveAndCopyValue` | `..._NEEDSCOPY_NEEDSCOMPARE` | 同上 |
| `OpAssign` | `..._NEEDSCOPY_NEEDSCOMPARE` | 拷贝整个 Map |
| `OpEquals` | `..._NEEDSCOMPARE` | 仅需要比对 |
| `Remove` | `..._NEEDSCOPY_NEEDSCOMPARE` | 注意虽然只删不拷, 但内部 destructor 路径可能间接需要 copy 信息 |
| `FindOrAdd_Defaulted` | (无 native call) | 默认构造路径不走 JIT, 留给通用版 |
| `FindOrAdd(K, V)` | `SCRIPT_NATIVE_TEMPLATED_CALL` | 需要 V 拷贝, 但宏标 Trivial=false 退回普通 |

### 9.2 模板版函数对应表

```cpp
// Bind_TMap.h:548-645
template<typename K, typename V>
static void Add_Template(TMap<K, V>& Map, void* Key, void* Value)
{
    Map.Add(*(K*)Key, *(V*)Value);                            // ★ 直转原生 TMap<K,V>::Add
}

template<typename K, typename V>
static bool Contains_Template(TMap<K, V>& Map, void* Key)
{
    return Map.Contains(*(K*)Key);
}

template<typename K, typename V>
static void* OpIndex_Template(TMap<K, V>& Map, void* Key)
{
    V* Value = Map.Find(*(K*)Key);
    if (Value == nullptr) { FAngelscriptEngine::Throw("..."); return nullptr; }
    return Value;
}

template<typename K, typename V>
static FScriptMap& OpAssign_Template(TMap<K, V>& Destination, FScriptMap& Source)
{
    Destination = *(TMap<K, V>*)&Source;                      // ★ 二进制兼容: FScriptMap == TMap<K,V>
    return *(FScriptMap*)&Destination;
}

template<typename K, typename V>
static bool OpEquals_Template(TMap<K, V>& MapA, FScriptMap& MapB)
{
    return MapA.OrderIndependentCompareEqual(*(TMap<K, V>*)&MapB);   // ★ 走 UE 内置无序比对
}
```

利好：

- **二进制兼容**：`reinterpret_cast<TMap<K,V>*>(FScriptMap*)` 直接安全——这是 UE 引擎自身的设计承诺。
- **编译期内联**：对 `TMap<int, int>::Add`，编译器知道 K/V 都是 4 字节 POD，可省略所有 `bNeedConstruct/Copy` 分支与 lambda 间接调用。
- **`OrderIndependentCompareEqual`**：UE 自带的 hash-based 无序比对，O(N) 而不是 `IsPermutation` 的 O(N²)。

### 9.3 关闭 JIT 时的退化

宏退化为空：

```cpp
#define SCRIPT_NATIVE_TEMPLATED_CALL(Binds, Name, Trivial)
#define SCRIPT_NATIVE_TEMPLATED_CALL_NEEDSCOMPARE(Binds, Name, Trivial)
// ... 全部为空
```

所有方法走通用 `FAngelscriptMapBinds::*` 路径——功能完整、性能略低。这意味着启用 JIT 不是必需，关闭后行为不变。

---

## 十、关键限制与边缘案例

### 10.1 嵌套容器禁止

```angelscript
TMap<FString, TArray<int>> ByName;       // ✗ 编译期: "Containers cannot be nested in other containers"
TMap<TArray<int>, FString> ByList;       // ✗ 同上
TMap<FString, TMap<int, int>> Nested;    // ✗ 同上
```

变通：用 `USTRUCT` 包一层。

```angelscript
struct FIntList { TArray<int> Values; }
TMap<FString, FIntList> ByName;          // ✓ struct 不是容器
```

### 10.2 键无哈希时编译期拒绝

```angelscript
TMap<FText, int> M;                      // ✗ FText 没有 GetTypeHash
TMap<FCustomNoHash, int> M2;             // ✗ "Key type does not have a hash function defined"
```

修复（脚本侧）：

```angelscript
struct FCustomKey
{
    int Id;
    bool opEquals(const FCustomKey& O) const { return Id == O.Id; }
    uint32 Hash() const { return uint32(Id); }     // ★ 关键
}
TMap<FCustomKey, int> M;                 // ✓
```

修复（C++ 侧）：在 `USTRUCT()` 上加 `WithIdenticalViaEquality + WithGetTypeHash`，并实现 `friend uint32 GetTypeHash(const FXxx&)`。

### 10.3 迭代时改写仅调试构建报错

```angelscript
for (auto E : Lookup)
{
    Lookup.Remove(E.GetKey());    // ✗ 调试构建抛错; 发布构建未定义行为
}
```

发布构建里 `AS_ITERATOR_DEBUGGING` 关闭，`GMapsBeingIterated` 全局表不存在。改写底层 `FScriptMap` 可能让 `NextIndex` 指向已删除槽，下次 `Proceed()` 会 trip on `IsValidIndex`。

正确做法：用手工迭代器 + `RemoveCurrent`：

```angelscript
TMapIterator<FName, int> It = Lookup.Iterator();
while (It.CanProceed)
{
    It.Proceed();
    if (ShouldRemove(It.GetKey()))
        It.RemoveCurrent();      // ✓ 安全
}
```

### 10.4 `TMap<UObject*, ...>` 的"弱引用陷阱"

```angelscript
TMap<AActor, FString> ActorTags;         // K 是 AActor* (UObject 派生)
ActorTags.Add(SomeActor, "Tag");
// SomeActor 被 destroy / GC...
ActorTags.Contains(SomeActor);           // ★ UB? 还是怎样?
```

**实际行为**：`AActor` 作键时键是 `UObject*` 指针。GC 回收 `SomeActor` 时：

1. `EmitReferenceInfo` 让 `TMap` 的 `StructSet` 知晓键是 reference；
2. GC 走 sub-Schema 时**会清理键指针**为 `nullptr`，但**不会从 Map 中物理删除整个 KV 对**（因为 Map 自己不知道哪个键被清了）；
3. 后续 `Contains(SomeActor)` 因为指针已经被新对象重用可能误命中或漏命中。

**结论**：`TMap<UObject*, ...>` 的语义是"弱引用键"，**绝不应该依赖键的生命周期**——读到 `nullptr` 键是常态，必须显式过滤。安全实践：用 `FName` 做键、用 `FObjectKey` 做包装、或周期性 `for` 清理 `nullptr` 键。

### 10.5 函数参数按引用强制

```angelscript
void Mutate(TMap<FName, int> M)          // 等价 TMap<FName, int>& out M
{
    M.Add(NAME_None, 99);                // 调用者的 Map 也被改
}
```

虽然源码里 `FAngelscriptMapType` 没有显式 `IsParamForcedOutParam = true` 的覆盖（与 `TArray` 不同），但 `SetArgument` 的实现走的是 `StepCompiledIn<FMapProperty>`/`StepCompiledInRef`，根据 `Usage.bIsReference` 决定：传值时是 deep copy（`CopyValue` 遍历重建），传引用时透传。AS 端默认值类型容器走值传递时**会触发深拷贝**——`TMap` 的深拷贝 O(N) 重哈希，比 `TArray` 的 memcpy 慢得多。**总是用 `const TMap<K,V>&` 或 `TMap<K,V>&` 传参**。

### 10.6 `Map[Key]` 找不到时不自动插入

```angelscript
TMap<FString, int> M;
M["miss"];                               // ✗ Throw, 不会默认插入
M.FindOrAdd("miss");                     // ✓ 不存在时插入默认值
M.FindOrAdd("miss") = 42;                // ✓ 链式赋值
```

这与 `std::unordered_map::operator[]` "不存在则插入"语义不同——AS 端的 `OpIndex` 严格只读已知键。

### 10.7 `OpEquals` 要求双向 `CanCompare`

```angelscript
TMap<FName, FCustomStructNoEquals> M;
M == M2;                                 // ✗ Throw: "Cannot compare map key/value type for equality."
```

虽然 `Add/Find/Remove` 不需要值的 `opEquals`，但 `==` 需要——值类型缺 `opEquals` 时直接抛错。

---

## 十一、与 `Bind_TArray` 的代码模式对照

### 11.1 共享的 Helper 与模式

| 维度 | `Bind_TArray.cpp` | `Bind_TMap.cpp` | 共享？ |
|------|-------------------|-----------------|--------|
| `AS_ITERATOR_DEBUGGING` 全局表 | `GArraysBeingIterated` | `GMapsBeingIterated` | 同模式，独立表 |
| `AS_REFERENCE_DEBUGGING` | `InvalidateReferencesToArray` | `InvalidateReferencesToMap` | 同模式，独立函数 |
| 元数据挂载 | `asCObjectType::plainUserData` | 同 | ✓ 完全共享机制 |
| 模板 ValueClass | `ValueClass<FScriptArray>` | `ValueClass<FScriptMap>` | ✓ 同基础设施 |
| TemplateCallback 守门 | `ValidateArrayOperations` | `ValidateMapOperations` | 同模式 |
| `CanBeTemplateSubType = false` | ✓ | ✓ | ✓ 共同拒绝嵌套 |
| GC `EmitReferenceInfo` | `ReferenceArray` / `StructArray` | **`StructSet`** | 不同 EMemberType |
| StaticJIT 宏 | `SCRIPT_NATIVE_TEMPLATED_CALL_*` | 同四种 | ✓ 共享宏 |
| `*_Template<...>` 函数 | `Add_Template<T>` 等 | `Add_Template<K,V>` 等 | 同模板套路 |
| `RegisterTypeFinder` | `FArrayProperty -> Usage` | `FMapProperty -> Usage` | 同模式 |

### 11.2 `TMap` 独有的逻辑

- **键哈希约束** `CanHashValue` 与脚本侧 `Hash()` 退路（§三）
- **稀疏索引** `FindNextIndex`（§六）
- **7-lambda 注入式 `Add`**（§5.1）
- **`IsPermutation` 多重计数比对**（§5.5）
- **`FindOrAdd_Defaulted` 临时栈缓冲**（§5.4）
- **调试器 `TypeIndex` 双层编码**（§八）
- **`StructSet` GC schema**（§4.2）

### 11.3 `TMap` **缺失**的逻辑（相对 `TArray`）

- **没有 `Sort`** —— Map 的迭代顺序由插入顺序与哈希桶决定，无法稳定排序
- **没有 `RemoveSwap` 系列** —— Sparse array 内部已经"删除即留洞"，无 swap 语义
- **没有别名检测** `CheckArrayValueDoesNotAliasStorage` —— `Add(K, V)` 中 K/V 来自 Map 内部时由 7-lambda 拷贝兜底，但仍**应避免**自引用
- **没有 POD 整块 memcpy 拷贝** —— `OpAssign` 永远遍历 `Add`，与 `TArray::OpAssign` 的 POD 快路径形成对比

---

## 十二、设计哲学

### 12.1 为什么 `EOrder::Early+1` 而不是 `Early`？

`TMap.GetKeys(TArray<K>&)` / `GetValues(TArray<V>&)` 方法签名直接出现 `TArray`。AS 编译期解析签名时若 `TArray` 模板未注册，会报模板未识别错误。`+1` 是显式表达"晚于 TArray、早于普通绑定"的依赖关系，避免依赖 enum 的字典序。

### 12.2 为什么 `FindOrAdd_Defaulted` 用栈缓冲而不直接传 nullptr？

`Ops->Add` 的 `ValueConstructAndAssignFn` 必须既构造又拷贝——它依赖一个有效的 source pointer。如果允许 nullptr，要么 lambda 内做条件分支（增加每次插入的开销），要么提供独立的 `AddDefaulted` 路径（代码膨胀）。栈缓冲方案在编译期固定大小、零堆分配（≤64 字节）、并复用现有 `Add` 路径——综合最优。

### 12.3 为什么键约束 `CanCompare` 而不是要求专用 `opEquals` 重载？

`KeyType.CanCompare()` 检查的是 `IsValueEqual` 是否可用——这与 `opEquals` 是同一个底层接口。键的"可比较"是哈希冲突时桶内查找的硬需求；脚本侧 `struct` 必须自带 `opEquals` 才能作键，与必须自带 `Hash()` 是对偶要求。

### 12.4 为什么 `OpEquals` 走 `IsPermutation` 而不是按位序？

`FScriptMap` 是 sparse + hash-bucket，**插入顺序不影响等价性**——这是 Map 的语义本质。两个 `TMap<int, int>` 即使插入顺序不同，只要键值对集合一致就该相等。`IsPermutation` O(N²) 是为通用性付的代价；JIT 路径用 UE 自带 `OrderIndependentCompareEqual`（哈希桶比对）做 O(N) 优化。

### 12.5 为什么 GC 用 `StructSet` 而非 `ReferenceMap`？

UE GC 没有 `ReferenceMap` 类型——`FScriptMap` 内部就是 `FScriptSet<TPair<K,V>>`，所以从 GC 视角只看到一个 set 的元素布局（pair size + sub-Schema）。`StructSet` 类型直接对应这个抽象。即使 `TMap<UObject*, int>` 也走 `StructSet` 进入 sub-Schema，牺牲一点性能换取"GC 视角统一"。

### 12.6 为什么没有 `MoveAssignFrom`？

`TArray` 有 `MoveAssignFrom` 用于显式 move。`TMap` 没有——因为 `FScriptMap` 的 move 实现需要交换三个内部字段（sparse array、hash buckets、free list），且不像 `TArray` 三字段简单。当前实现选择**永远 deep copy**，性能权衡上可接受（Map 通常不在最热路径，hot path 更关心 `Add/Find` 而非 assign）。

---

## 附录 A：API 速查表

| 方法 | AS 签名 | C++ 入口 | StaticJIT 宏 |
|------|--------|---------|-------------|
| 默认构造 | `TMap<K,V>()` | `FAngelscriptMapBinds::Construct` | — |
| 析构 | (隐式) | `FAngelscriptMapBinds::Destruct` | `SCRIPT_NATIVE_TEMPLATED_CALL` |
| 元素数 | `int Num() const` | `FAngelscriptMapBinds::Num` | (FUNC_TRIVIAL) |
| 是否为空 | `bool IsEmpty() const` | `FAngelscriptMapBinds::IsEmpty` | (FUNC_TRIVIAL) |
| 添加 | `void Add(const K&, const V&)` | `FAngelscriptMapBinds::Add` | `..._NEEDSCOPY_NEEDSCOMPARE` |
| 索引 | `V& opIndex(const K&)` | `FAngelscriptMapBinds::OpIndex` | `..._NEEDSCOMPARE` |
| 索引 (const) | `const V& opIndex(const K&) const` | 同上 | `..._NEEDSCOMPARE` |
| 判存 | `bool Contains(const K&) const` | `FAngelscriptMapBinds::Contains` | `..._NEEDSCOMPARE` |
| 找 | `bool Find(const K&, V& out) const` | `FAngelscriptMapBinds::Find` | `..._NEEDSCOPY_NEEDSCOMPARE` |
| 找或加 | `V& FindOrAdd(const K&)` | `FindOrAdd_Defaulted` | (无, 退回通用) |
| 找或加 | `V& FindOrAdd(const K&, const V&)` | `FindOrAdd` | `SCRIPT_NATIVE_TEMPLATED_CALL` |
| 删 | `bool Remove(const K&)` | `FAngelscriptMapBinds::Remove` | `..._NEEDSCOPY_NEEDSCOMPARE` |
| 删并取值 | `bool RemoveAndCopyValue(const K&, V& out)` | `RemoveAndCopyValue` | `..._NEEDSCOPY_NEEDSCOMPARE` |
| 清空 | `void Empty(int Slack = 0)` | `FAngelscriptMapBinds::Empty` | `SCRIPT_NATIVE_TEMPLATED_CALL` |
| 重置 | `void Reset()` | `FAngelscriptMapBinds::Reset` | `SCRIPT_NATIVE_TEMPLATED_CALL` |
| 拷取键集 | `void GetKeys(TArray<K>& out)` | `FAngelscriptMapBinds::GetKeys` | (无) |
| 拷取值集 | `void GetValues(TArray<V>& out)` | `FAngelscriptMapBinds::GetValues` | (无) |
| 赋值 | `TMap<K,V>& opAssign(const TMap<K,V>&)` | `FAngelscriptMapBinds::OpAssign` | `..._NEEDSCOPY_NEEDSCOMPARE` |
| 等比 | `bool opEquals(const TMap<K,V>&) const` | `FAngelscriptMapBinds::OpEquals` | `..._NEEDSCOMPARE` |
| 取迭代器 | `TMapIterator<K,V> Iterator()` | `FMapIterator::Create` | `SCRIPT_NATIVE_TARRAY_ITERATOR_CREATE` |
| 取迭代器 (const) | `TMapConstIterator<K,V> Iterator() const` | 同上 | 同上 |
| for 起点 | `int opForBegin() [const]` | lambda → `FindNextIndex(-1)` | — |
| for 终点 | `bool opForEnd(const int) const` | `Iterator == -1` | — |
| for 步进 | `void opForNext(int& inout)` | lambda → `FindNextIndex` | — |
| for 取值 | `V& opForValue(const int) [const]` | lambda → `Ops->GetValue` | — |
| for 取键 | `const K& opForKey(const int) const` | lambda → `Ops->GetKey` | — |

### `TMapIterator<K,V>` 方法

| 方法 | AS 签名 | 说明 |
|------|--------|------|
| 拷贝构造 | `TMapIterator(const TMapIterator&)` | 同时维护迭代守卫表 |
| 赋值 | `TMapIterator& opAssign(const TMapIterator&)` | 同上 |
| 步进 | `TMapIterator& Proceed()` | 越界时 Throw |
| 状态 | `bool CanProceed` | property，公开字段 |
| 取键 | `const K& GetKey() const` | 越界 Throw |
| 取值 | `V& GetValue() const` | 越界 Throw |
| 写值 | `void SetValue(const V&) const` | 仅非 const 版可用 |
| 删当前 | `void RemoveCurrent() const` | 仅非 const 版可用 |

---

## 附录 B：避坑清单

1. **键类型必须可哈希**：`FText` / 无 `Hash()` 的脚本 struct / 无 `WithGetTypeHash` 的 USTRUCT 都会被 `ValidateMapOperations` 拒绝。优先用 `FName` / `int` / `FGuid` 做键。
2. **`Map[Key]` 找不到时 Throw**：与 STL 不同，**不会**默认插入。要默认插入用 `FindOrAdd`。
3. **迭代时不能改写 Map**：调试构建抛错，发布构建未定义行为。要边遍历边删用手工 `TMapIterator + RemoveCurrent`。
4. **`for (auto E : Map)` 走 `Iterator()` 路径**：E 是 `TMapIterator<K,V>`，不是 KV pair——通过 `E.GetKey()` / `E.GetValue()` 访问。
5. **嵌套容器禁止**：`TMap<K, TArray<V>>` 编译期失败。用 `USTRUCT { TArray<V> Values; }` 包一层。
6. **`UObject*` 作键有弱引用陷阱**：GC 清理对象后键指针可能 stale，必须周期性过滤 `nullptr`。优先用 `FName` 或 `FObjectKey`。
7. **值参数走深拷贝**：`void Foo(TMap<K,V> M)` 触发 O(N) 重哈希。**总是**用 `const TMap<K,V>&`。
8. **`OpAssign` 没 POD 快路径**：`TMap<int, int>` 拷贝**不会**自动 memcpy——总是遍历 `Add`。容器赋值在热点要避免。
9. **脚本侧 `Hash()` 调用慢**：每次插入/查找触发 AS 反射调用，慢 1-2 个量级。Hot path 上避免。
10. **`==` 需要双向可比较**：值类型缺 `opEquals` 时 `Map == Other` 直接 Throw。
11. **删除变体只有 2 种**（vs `TArray` 的 6 种）：`Remove(K)` / `RemoveAndCopyValue(K, V&)`。无"按值删除"。
12. **`GetKeys/GetValues` 在 Bind_TMap.cpp 第 1054/1059 行的笔误**：`bKeyNeedConstruct/Copy` 应为 `bValueNeedConstruct/Copy`，目前内置类型 K/V 标志位一致未触发。已知潜在 bug。

---

## 小结

- **`TMap<K,V>` 是 `Bind_TMap.cpp` 注册的双子类型 ValueClass**，底层桥接 UE 自带的 `FScriptMap`（实质是 `FScriptSet<TPair<K,V>>`），与 `TArray` 共享 `plainUserData` 元数据挂载、`AS_ITERATOR_DEBUGGING` 守卫、StaticJIT 宏体系。
- **键约束有两层**：编译期 `KeyType.CanHashValue() || HashFunction != nullptr`（脚本侧 `uint32 Hash() const` 退路），运行时 `KeyType.CanCompare()` 用于哈希桶冲突时的 `opEquals` 比对。
- **`FMapOperations` 缓存键值双方的 size/alignment + POD 标志位 + `FScriptMapLayout`**，所有插入/查找/删除经 7-lambda 注入式 `FScriptMap::Add` / `FindPairIndex` 接口完成。
- **GC schema 走 `EMemberType::StructSet`**，键值任一含引用都进入 sub-Schema 遍历——区别于 `TArray` 的 `ReferenceArray`/`StructArray` 双分支。
- **For 循环用稀疏索引** + 五件套（`opForBegin/End/Next/Value/Key`）；`Iterator()` 路径返回 `TMapIterator<K,V>`，是 AS 端 `for (auto E : Map)` 的实际后端。
- **StaticJIT 启用时通过 `*_Template<K,V>` 模板版函数将通用调用替换为原生 `TMap<K,V>::Add/Find/...`**；二进制兼容 `FScriptMap` ↔ `TMap<K,V>` 让转换零成本。

---

## 修订记录

| 版本 | 日期 | 内容 |
|------|------|------|
| v1.0 | 2026-05-24 | 首版：基于 `Bind_TMap.cpp:1-1350` / `Bind_TMap.h:1-680` / `Core/AngelscriptType.h:277-280` / `StaticJITBinds.h:91-114` / `Script/Examples/Core/Example_Map.as` / `AngelscriptMapBindingsTests.cpp` 完整产出。覆盖：① `TMap<K,V>` 与 `TArray<T>` 的两个本质差异（双子类型 + 键哈希约束）；② `EOrder::Early+1` 注册时序与对 `TArray` 的依赖；③ `FMapOperations` 数据结构及与 `FArrayOperations` 对照（双 size/alignment、双 POD 标志位、`HashFunction` 替代 `CompareFunction`）；④ `ValidateMapOperations` 三层拦截（嵌套/可哈希/可构造）；⑤ 键类型选择决策表（`FName` / `int` / `FGuid` / 脚本 struct + `Hash()`）；⑥ `FAngelscriptMapType` 接口职责映射、`StructSet` GC schema、`FMapProperty` 双 inner（`KeyProp`/`ValueProp`）创建链路；⑦ 关键操作剖析：`Add` 7-lambda 注入式、`OpIndex` 找不到 Throw、`Find/RemoveAndCopyValue` POD 快路径、`FindOrAdd_Defaulted` 栈缓冲方案、`OpEquals` 用 `IsPermutation` 多重计数 O(N²)、删除变体只有 2 种、`Empty` vs `Reset` 微妙差异、`GetKeys/GetValues` 已知笔误；⑧ 稀疏索引 + 五件套 `opForBegin/End/Next/Value/Key`、`opForKey` 返键引用而非槽位；⑨ `TMapIterator` 双范式（`for (auto E : Map)` 隐式路径 vs 显式手工 `Proceed/RemoveCurrent`）；⑩ 调试器 `TypeIndex` 双层编码（顶层=KV 对列表，>0=进入 Pair 内部）+ `Lookup["Alpha"]` Watch 表达式 alloca 路径；⑪ StaticJIT 四种宏（`NEEDSCOMPARE` × `NEEDSCOPY`）与 `*_Template<K,V>` 二进制兼容路径；⑫ 7 项关键限制（嵌套/无哈希/迭代改写/`UObject*` 弱引用/值参数深拷贝/`OpIndex` 不自动插入/`==` 双向可比较）；⑬ 与 `TArray` 共享/独有/缺失逻辑三表；⑭ 6 项设计哲学解析（注册时序/栈缓冲/键约束二元/`IsPermutation`/`StructSet`/无 `MoveAssignFrom`）；⑮ API 速查 + 避坑 12 条。所有 ASCII 图遵循纯 ASCII 风格，与 `Syntax_TArray.md` v1.0 / `Syntax_DefaultStatement.md` v1.3 等统一。 |