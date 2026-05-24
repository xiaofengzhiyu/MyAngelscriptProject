# Syntax_TSubclassOf — `TSubclassOf<T>` UClass 类型安全引用实现原理

> **所属前缀**: Syntax_（智能指针与引用包装族）
> **关注层面**: 语法机制、模板实例化与反射桥接（不写"怎么用"——那是 `Guide_*` 的活；不写 UClass 生成本身——那是 `Type_ClassGeneration.md`）
> **关键源码**:
> `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TSubclassOf.h` (~140 行，`FAngelscriptSubclassOfHelpers` 静态助手)
> · `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp` `:1567-1833` `Bind_TSubclassOf_Declaration` / `FSubclassOfType` / `Bind_TSubclassOf` 三块
> · `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp` `:2474-2574` `BindUClassLookup` —— `RegisterTypeFinder` 反射桥
> · `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` `~1110-1145` —— `__StaticType_<ClassName>` 全局变量生成
> · `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` `:4960-4984` —— `__StaticType_*` 回填脚本类
> · `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AActor.cpp` `:223-278` —— `SpawnActor / SpawnPersistentActor` 签名
> · `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp` `:543-566` —— `NewObject` 签名
> · `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptClassBindingsTests.cpp` —— 行为契约
> **关联文档**:
> `Documents/Knowledges/ZH/Syntax_TWeakObjectPtr.md` —— 兄弟智能指针（同两阶段注册模式）
> · `Documents/Knowledges/ZH/Syntax_TOptional.md` —— 单 T 模板包装的对照样本
> · `Documents/Knowledges/ZH/Syntax_UPROPERTY.md` —— UPROPERTY 反射注册与编辑器元数据
> · `Documents/Knowledges/ZH/Type_Core.md` §4.5 —— `FSubclassOfType` 在类型系统中的位置
> · `Documents/Knowledges/ZH/Type_BindSystem.md` —— `FAngelscriptType` / `FAngelscriptTypeUsage` 多态分派
> · `Documents/Knowledges/ZH/Type_BaseClass.md` —— 类继承与 `IsChildOf` 语义
> **外部参考**（可选）:
> [UE5 `TSubclassOf` 头文件](https://github.com/EpicGames/UnrealEngine/blob/release/Engine/Source/Runtime/CoreUObject/Public/Templates/SubclassOf.h)

---

## 概览

`TSubclassOf<T>` 是 UE 的"**保证子类的 `UClass*`**"——本质是一个带运行时类型校验的 `UClass*` 包装：声明 `TSubclassOf<AActor> Class;` 后，编译器在赋值/构造路径上插入 `IsChildOf(AActor::StaticClass())` 检查，**不是 `AActor` 的子类一律拒绝**（脚本端抛 AS 异常 + 自动重置为 `nullptr`）。

在当前 AS 插件中，`TSubclassOf<T>` 被实现为**两阶段注册的模板值类型**——内部存储是单一的 `TSubclassOf<UObject>`（即一个 `UClass*`），靠 `asCObjectType::GetSubType(0)` 在运行时解析模板参数 `T` 对应的 `UClass*` 来做协变与子类校验。这是 AS 插件**类型擦除模板**的典型范式，与 `TWeakObjectPtr<T>` / `TSoftObjectPtr<T>` 共享同一套两阶段骨架。

```text
        AS 脚本侧                              C++ 实现侧
        ====================                  ============================
   TSubclassOf<AActor> SubClass;            ValueClass<TSubclassOf<UObject>>
                                               ("TSubclassOf<class T>", {bTemplate, Covariant})
   SubClass = AActor::StaticClass();        opAssign(UClass)
                                               -> SetClass: IsChildOf(AActor) ? *Ptr=Class : Throw
   AActor.StaticClass() -> SubClass;        ImplicitConstruct(UClass)
                                               -> 同上校验
   SubClass.IsValid()                       Get() != nullptr
   SubClass.Get()                           直接返回 UClass*
   UClass C = SubClass;                     opImplConv() const -> UClass*
   if (SubClass == AActor::StaticClass())…  opEquals(UClass)
   if (SubClass.IsChildOf(AActor::StaticClass()))…  Class->IsChildOf(Other)
   AActor CDO = SubClass.GetDefaultObject(); Class->GetDefaultObject()
```

### 在族谱中的位置

```text
          ┌──────────────────────────────────────────────────────┐
          │  UClass / UObject 引用三兄弟（同 facade 模式）       │
          ├──────────────────────────────────────────────────────┤
          │  TSubclassOf<T>     "保证子类的 UClass*"  ← 本文     │
          │  TWeakObjectPtr<T>  "可能被 GC 回收的 T*"            │
          │  TSoftObjectPtr<T>  "持有资源路径的 T*"              │
          ├──────────────────────────────────────────────────────┤
          │   共同骨架                                           │
          │   - asOBJ_TEMPLATE_SUBTYPE_COVARIANT 协变            │
          │   - 两阶段注册 (EOrder::Early + Late-10)             │
          │   - 底层值类型统一为 <UObject>，靠 SubType[0] 区分   │
          │   - TemplateCallback 校验子类型是 UObject 派生       │
          │   - FAngelscriptType 子类 facade（CreateProperty 等）│
          └──────────────────────────────────────────────────────┘

          关键差异:
          ┌──────────────────┬──────────────────┬──────────────────┐
          │  TSubclassOf     │  TWeakObjectPtr  │  TSoftObjectPtr  │
          ├──────────────────┼──────────────────┼──────────────────┤
          │ 持有 UClass*     │ 持有 FWeakObject │ 持有 FSoftObject │
          │ 校验 IsChildOf   │ 校验 IsValid     │ 校验 IsValid     │
          │ 不阻 GC（弱引）  │ 不阻 GC          │ 不阻 GC（路径）  │
          │ FClassProperty   │ FWeakObjectProp  │ FSoftObjectProp  │
          │ +CPF_UObjectWrap │ +CPF_UObjectWrap │ +CPF_UObjectWrap │
          │ MetaClass=T      │ PropertyClass=T  │ PropertyClass=T  │
          └──────────────────┴──────────────────┴──────────────────┘
```

后续按以下顺序展开：① 数据布局与设计动机；② 静态助手 `FAngelscriptSubclassOfHelpers` —— 8 个核心操作；③ 阶段 1 模板声明（`Bind_TSubclassOf_Declaration`）；④ 类型 facade `FSubclassOfType` —— 反射桥；⑤ 阶段 2 方法补充（`Bind_TSubclassOf`）—— 隐式转换 / IsChildOf / GetDefaultObject；⑥ AS 端用法链路：`__StaticType_<ClassName>` 与 `StaticClass()` 的协作；⑦ 与 `SpawnActor` / `NewObject` / `CreateComponent` 的协作；⑧ UPROPERTY 编辑器集成；⑨ 限制与避坑。

---

## 一、数据布局与设计动机

### 1.1 物理布局：一个 `UClass*`

`TSubclassOf<T>` 在 UE 5 的 `SubclassOf.h` 中本质是单成员模板：

```cpp
// ============================================================================
// 文件: Engine/Source/Runtime/CoreUObject/Public/Templates/SubclassOf.h
// 性质: UE 标准库（节选示意，非本仓库代码）
// ============================================================================
template<class TClass> class TSubclassOf
{
    UClass* Class;          // ★ 真正持有的就是这一个指针
public:
    UClass* Get() const { return Class; }
    UClass* operator->() const { return Class; }
    operator UClass*() const { return Class; }
    // ... 模板赋值与隐式转换约束 ...
};
```

也就是说**任何 `TSubclassOf<T>` 实例只占 `sizeof(UClass*)`**——这让所有具体实例化（`TSubclassOf<AActor>` / `TSubclassOf<UActorComponent>` / …）在内存上完全等价，**类型 `T` 仅在 C++ 编译期参与赋值检查**。

这给 AS 插件的实现带来一个直接好处：所有 `TSubclassOf<T>` 在 AS 端只需要**一份底层注册** —— `ValueClass<TSubclassOf<UObject>>("TSubclassOf<class T>", …)`，运行时再靠模板 `SubType[0]` 做类型校验即可，不需要为每个 `T` 重复展开。

### 1.2 与裸 `UClass*` 的差异

```text
裸 UClass*:                    TSubclassOf<AActor>:
  - 接受任何 UClass             - 只接受 AActor / 派生类
  - 赋值无校验                   - opAssign 调 IsChildOf 校验
  - 编译期不带泛型信息           - GetDefaultObject() 返回 AActor 而非 UObject
  - 蓝图编辑器看到"任意 Class"   - 蓝图 ClassPicker 自动过滤为 AActor 派生
```

校验时机：

- **C++ 编译期**：`TSubclassOf<T>::operator=` 通过 SFINAE 限定 `From*` 必须是 `T*` 派生，`UPackage*` 赋给 `TSubclassOf<AActor>` 直接编译错误。
- **AS 运行时**：脚本端的"模板参数 T"在 C++ 端被擦除为 `UObject`，校验只能在 `IsChildOf` 调用时做——这也是 `FAngelscriptSubclassOfHelpers::SetClass` 中显式调用 `IsChildOf` 的原因。

### 1.3 为何不能复用 `UClass*`

理论上"传 `UClass*` 自己写 `IsChildOf`"也能工作，但 `TSubclassOf<T>` 在 AS 端有三个不可替代价值：

1. **UPROPERTY 蓝图集成**：`FClassProperty + CPF_UObjectWrapper + MetaClass=T` 才会触发蓝图 ClassPicker 的过滤器；裸 `UClass*` 不带 `MetaClass`，蓝图面板列出全部 UClass。
2. **签名表达力**：`SpawnActor(TSubclassOf<AActor>)` 比 `SpawnActor(UClass*)` 多一份"传错类型在脚本端就会被拒"的契约。
3. **模板派发**：`NewObject<UObject>(Outer, Class.Get(), …)` 写法保留，AS 端 `SubclassOf -> UClass` 的 `opImplConv` 让脚本作者在传裸 `UClass*` API（如 `Cast`）和 `TSubclassOf<>` API 间无感。

---

## 二、静态助手 `FAngelscriptSubclassOfHelpers`

**源码所在**: `Bind_TSubclassOf.h` 全文 ~140 行——是个纯 `struct` 包裹 8 个静态函数，所有 AS 绑定方法最终都路由到这里。

### 2.1 构造与赋值四件套

```cpp
// ============================================================================
// 文件: AngelscriptRuntime/Binds/Bind_TSubclassOf.h
// 角色: TSubclassOf<UObject> 在 AS 侧的构造/拷贝/赋值
// ============================================================================
static void Construct(TSubclassOf<UObject>* Ptr)
{
    new (Ptr) TSubclassOf<UObject>(nullptr);
}

static void CopyConstruct(TSubclassOf<UObject>* Ptr, TSubclassOf<UObject>& Other)
{
    new (Ptr) TSubclassOf<UObject>(Other);
}

static TSubclassOf<UObject>& Assign(TSubclassOf<UObject>* Ptr, TSubclassOf<UObject>& Other)
{
    *Ptr = Other;
    return *Ptr;
}
```

这三个函数完全不接触 `T` 类型——同类型的 `TSubclassOf<T>` 之间相互拷贝/赋值，不需要再校验，因为来源已经是合法的子类（构造时已经过滤）。

### 2.2 关键校验路径：`ImplicitConstruct` 与 `SetClass`

`UClass*` → `TSubclassOf<T>` 的两个入口（构造时 / 赋值时）才是要做 `IsChildOf` 校验的地方：

```cpp
// ============================================================================
// 文件: AngelscriptRuntime/Binds/Bind_TSubclassOf.h
// 函数: FAngelscriptSubclassOfHelpers::ImplicitConstruct
// ============================================================================
static void ImplicitConstruct(TSubclassOf<UObject>* Ptr,
                              asCObjectType* TemplateType, UClass* InClass)
{
    if (InClass == nullptr)
    {
        new (Ptr) TSubclassOf<UObject>(nullptr);     // ★ null 一律放行
        return;
    }

    auto* SubType = TemplateType->GetSubType(0);     // ★ 取模板参数 T 的 asITypeInfo
    if (SubType->GetFlags() & asOBJ_VALUE)
    {
        new (Ptr) TSubclassOf<UObject>(nullptr);     // 值类型不应到这——保险丝
        return;
    }

    UClass* TemplateClass = (UClass*)SubType->GetUserData();   // ★ T 对应的 UClass*
    if (InClass->IsChildOf(TemplateClass))           // ★ 关键校验
    {
        new (Ptr) TSubclassOf<UObject>(InClass);
    }
    else
    {
        FAngelscriptEngine::Throw("Class set to TSubclassOf<> was not a child of templated class.");
        new (Ptr) TSubclassOf<UObject>(nullptr);     // ★ 失败时强制为 null
    }
}
```

关键点：

- **第二个参数 `asCObjectType* TemplateType`** 由 AS 引擎注入——绑定时通过 `FAngelscriptBinds::PreviousBindPassScriptObjectTypeAsFirstParam()` 让 AS 在调用时把当前模板实例的 `asCObjectType*` 作为第一个真实参数传入（紧跟 `this`）。这是 AS 模板能在运行时拿到 `T` 信息的唯一途径。
- **`SubType->GetUserData()`** 是 `Bind_BlueprintType.cpp::BindUClass` 把 `UClass*` 绑到 `asITypeInfo` 时设置的关联——一行 `asITypeInfo::SetUserData(UClass*)` 让 AS 类型反查 `UClass*` 变 O(1)。详见 `Type_Core.md` §三。
- **失败时同时抛异常 + 置 null**：脚本作者既能从异常知道出错，又能从后续 `IsValid()` 检查到状态——双保险。

`SetClass` 的逻辑几乎一致，但多了一段"热重载安全网"：

```cpp
// ============================================================================
// 文件: AngelscriptRuntime/Binds/Bind_TSubclassOf.h
// 函数: FAngelscriptSubclassOfHelpers::SetClass
// ============================================================================
static void SetClass(TSubclassOf<UObject>* Ptr,
                     asCObjectType* TemplateType, UClass* InClass)
{
    // ... null / asOBJ_VALUE 早退同 ImplicitConstruct ...

    UClass* TemplateClass = (UClass*)SubType->GetUserData();
    bool bWillBecomeCorrect = false;

#if AS_CAN_HOTRELOAD
    // We could be inside a hotreload, and have an asset that isn't
    // reinstanced to the new class yet set to a property of the new class.
    // We should allow this, the class asset will be reinstanced later to the correct class.
    if (auto* ASTemplate = Cast<UASClass>(TemplateClass))
    {
        if (UASClass* ASAsset = UASClass::GetFirstASClass(InClass))
        {
            if (ASAsset->GetMostUpToDateClass()->IsChildOf(ASTemplate))   // ★ 走最新版
                bWillBecomeCorrect = true;
        }
    }
#endif

    if (InClass->IsChildOf(TemplateClass) || bWillBecomeCorrect)
    {
        *Ptr = InClass;                              // ★ 直接放行
    }
    else
    {
        FAngelscriptEngine::Throw("Class set to TSubclassOf<> was not a child of templated class.");
        *Ptr = nullptr;
    }
}
```

`bWillBecomeCorrect` 这条分支的意图：在热重载过程中，旧版 `UASClass` 资产可能还挂在场景里，而它在新版本里已经迁移到了一个新 UClass。脚本侧此时若把这个旧资产赋给 `TSubclassOf<NewType>`，直接的 `IsChildOf` 会失败——但稍后 reinstancer 会把 `InClass` 替换为最新版，因此在最新版能匹配的前提下提前放行。这是 `Type_BaseClass.md` 与 `RT_HotReload.md` 共同关心的边角行为。

### 2.3 查询四件套

```cpp
// ============================================================================
// 文件: AngelscriptRuntime/Binds/Bind_TSubclassOf.h
// 角色: 只读查询接口
// ============================================================================
static UClass* GetClass(TSubclassOf<UObject>* Ptr)
{
    return Ptr->Get();                               // 直接透传
}

static bool IsValid(TSubclassOf<UObject>* Ptr)
{
    return Ptr->Get() != nullptr;                    // 仅 null 检查
}

static bool IsChildOf(TSubclassOf<UObject>* Ptr, UClass* Other)
{
    UClass* Class = Ptr->Get();
    return Class != nullptr && Other != nullptr && Class->IsChildOf(Other);
}

static UObject* GetDefaultObject(TSubclassOf<UObject>* Ptr)
{
    UClass* Class = Ptr->Get();
    if (Class != nullptr)
        return Class->GetDefaultObject();
    else
        return nullptr;
}
```

注意：

- **`IsValid`** 只看 `Get() != nullptr`——它**不**像 `TWeakObjectPtr::IsValid` 那样隐含 GC 检查，因为 `UClass*` 通常是常驻的（除非脚本类被 HotReload 替换，但那走的是 reinstancer 路径而不是 GC）。
- **`IsChildOf`** 双 null 防御——任一边 null 都返回 `false`，避免脚本里写 `Class.IsChildOf(SomeClass.Get())` 时 `Get()` 为 null 把 `IsChildOf` 撞 nullptr。
- **`GetDefaultObject`** 直接调 `UClass::GetDefaultObject()`——返回 CDO（Class Default Object），对脚本作者读默认值/观察类元属性极有用。

---

## 三、阶段 1：模板声明注册（`Bind_TSubclassOf_Declaration`）

**源码所在**: `Bind_BlueprintType.cpp:1567-1596`，`EOrder::Early` 优先级。

```cpp
// ============================================================================
// 文件: AngelscriptRuntime/Binds/Bind_BlueprintType.cpp
// 函数: Bind_TSubclassOf_Declaration（lambda）
// ============================================================================
AS_FORCE_LINK const FAngelscriptBinds::FBind Bind_TSubclassOf_Declaration(
    (int32)FAngelscriptBinds::EOrder::Early, []
{
    FBindFlags Flags;
    Flags.bTemplate = true;
    Flags.TemplateType = "<T>";
    Flags.ExtraFlags = asOBJ_TEMPLATE_SUBTYPE_COVARIANT;        // ★ 子类型协变

    auto TSubclassOf_ = FAngelscriptBinds::ValueClass<TSubclassOf<UObject>>(
        "TSubclassOf<class T>", Flags);
    TSubclassOf_.Constructor("void f()",
        FUNC_TRIVIAL(FAngelscriptSubclassOfHelpers::Construct));
    TSubclassOf_.Constructor("void f(const TSubclassOf<T>& Other)",
        FUNC_TRIVIAL(FAngelscriptSubclassOfHelpers::CopyConstruct));
    TSubclassOf_.Method("TSubclassOf<T>& opAssign(const TSubclassOf<T>& Other)",
        FUNC_TRIVIAL(FAngelscriptSubclassOfHelpers::Assign));

    TSubclassOf_.TemplateCallback("bool f(int&in Type, int&out ErrorMessage)",
    [](asITypeInfo* TemplateType, asCString* ErrorMessage) -> bool
    {
        if (TemplateType->GetSubTypeCount() != 1)
            return false;

        auto* SubType = TemplateType->GetSubType(0);
        if (SubType == nullptr || SubType->GetFlags() & asOBJ_VALUE)
        {
            if (ErrorMessage != nullptr)
                *ErrorMessage = "Subtype must be a class type";
            return false;                                       // ★ T=int / FVector 在此被拒
        }
        return true;
    });
});
```

三个关注点：

- **`asOBJ_TEMPLATE_SUBTYPE_COVARIANT`**：`TSubclassOf<ACameraActor>` 可隐式视为 `TSubclassOf<AActor>` 的子类型——意味着可以把 `TSubclassOf<ACameraActor>` 传给参数为 `TSubclassOf<AActor>` 的函数。但 AS 编译器同时禁止"两端都是协变模板但子类型不协变"的隐式互转（见 `as_compiler.cpp:8759-8775` 的特殊处理），避免出现 `TSubclassOf<UPackage> -> opImplConv to UClass -> ImplicitConstruct from UClass` 的伪转换链路绕过类型校验。
- **`ValueClass<TSubclassOf<UObject>>`**：底层物理类型固定为 `<UObject>`——所有具体实例化共享同一份内存布局（一个 `UClass*`）。这让运行时只需保留一份存储，靠 `SubType[0]` 区分。
- **`TemplateCallback`**：模板实例化时 AS 引擎调一次此回调；它检查 `T` 必须是**对象类型**（指针）而非值类型——拒掉 `TSubclassOf<int>` / `TSubclassOf<FVector>` 这种无意义实例化（对应 `AngelscriptSyntaxSmartPointerTests.cpp::TSubclassOf_Negative`）。

阶段 1 不能注册任何依赖具体 `UClass` 的方法，因为 `BindUClass` 在 `EOrder::Early` 阶段才刚开始遍历 `BindDatabase.Classes`——此时大部分 `UClass*` 还没绑定完。

---

## 四、类型 facade `FSubclassOfType`

**源码所在**: `Bind_BlueprintType.cpp:1598-1809`。

`FSubclassOfType` 是 `TSubclassOf<T>` 在 UE 反射 / 类型系统中的多态分派 facade——继承自 `TAngelscriptCppType<TSubclassOf<UObject>>`，重写 14 个虚函数。它的核心职责是**让 `TSubclassOf<T>` 在 AS 反射层与 UE 反射层之间双向流通**。

### 4.1 接口职责一览

| 接口 | 实现 | 作用 |
|------|------|------|
| `GetAngelscriptTypeName` | `"TSubclassOf"` | dump / 错误信息中的类型显示 |
| `GetMetaClass` | `Usage.SubTypes[0].GetClass()` | 解析 T 对应的 UClass |
| `CanCreateProperty` | `SubTypes[0]` 有效 + 可解析 UClass 或 ScriptClass | 决定能否作 UPROPERTY |
| `CreateProperty` | `new FClassProperty + CPF_UObjectWrapper + SetMetaClass` | 创建 UE 反射属性 |
| `MatchesProperty` | 检查 `FClassProperty + CPF_UObjectWrapper + MetaClass` 匹配 | 函数签名匹配 |
| `CanQueryPropertyType` | `false` | **不**参与反向查询（`FObjectProperty` 才是入口） |
| `DescribesCompleteType` | `SubTypes[0].IsValid()` | 类型完整性 |
| `HasReferences` | `true` | UE GC 系统识别 |
| `CanBeArgument` | `true` | 可作 UFunction 参数 |
| `SetArgument` | 从 FFrame 栈取 `TSubclassOf<UObject>` | 与 UFunction 互调 |
| `CanBeReturned` / `GetReturnValue` | 同上反向 | UFunction 返回桥 |
| `DefaultValue_*` | `null` ↔ `nullptr` | 缺省值字符串处理 |
| `GetCppForm` | `TSubclassOf<<前缀><类名>>` | UHT codegen / StaticJIT |
| `GetDebuggerValue` | 显示 `{ ClassName }` | DAP 调试器面板 |

### 4.2 `CreateProperty` —— 生成 `FClassProperty`

```cpp
// ============================================================================
// 文件: AngelscriptRuntime/Binds/Bind_BlueprintType.cpp
// 函数: FSubclassOfType::CreateProperty
// ============================================================================
FProperty* CreateProperty(const FAngelscriptTypeUsage& Usage,
                          const FPropertyParams& Params) const override
{
    if (Usage.SubTypes.Num() == 0)
        return nullptr;

    UClass* SubClass = Usage.SubTypes[0].GetClass();
    check(SubClass);

    auto* Property = new FClassProperty(Params.Outer, Params.PropertyName, RF_Public);
    Property->PropertyFlags |= CPF_UObjectWrapper;       // ★ TSubclassOf 标志
    Property->PropertyClass = UClass::StaticClass();     // ★ 字段类型是 UClass*
    Property->SetMetaClass(SubClass);                    // ★ T 对应的 UClass* 作 MetaClass

    return Property;
}
```

三条 `★` 是 `TSubclassOf<T>` 在 UE 反射中的**身份证**：

- `FClassProperty`（不是 `FObjectProperty`）—— Class 字段而非 Object 字段。
- `CPF_UObjectWrapper` —— 区分裸 `UClass*` 字段（无此标志）与 `TSubclassOf<T>` 字段（有此标志）。`MatchesProperty` 与 `BindUClassLookup` 都靠这一位识别。
- `MetaClass = T` —— 蓝图编辑器 ClassPicker 据此过滤；`UPROPERTY() TSubclassOf<AActor> X;` 在编辑器里只列出 AActor 派生类。

### 4.3 `MatchesProperty` —— 名称回退匹配

```cpp
// ============================================================================
// 文件: AngelscriptRuntime/Binds/Bind_BlueprintType.cpp
// 函数: FSubclassOfType::MatchesProperty
// ============================================================================
bool MatchesProperty(const FAngelscriptTypeUsage& Usage,
                     const FProperty* Property, EPropertyMatchType MatchType) const override
{
    const FClassProperty* ClassProp = CastField<FClassProperty>(Property);
    if (ClassProp == nullptr) return false;
    if ((ClassProp->PropertyFlags & CPF_UObjectWrapper) == 0) return false;

    UClass* AssociatedClass = GetMetaClass(Usage);
    if (AssociatedClass != nullptr)
    {
        return ClassProp->MetaClass == AssociatedClass;        // 直接比 UClass*
    }
    else
    {
        // Workaround: 分析阶段 UClass 还没创建, 退化为字符串名比较
        if (Usage.SubTypes.Num() == 0) return false;
        if (Usage.SubTypes[0].ScriptClass == nullptr) return false;

        FString CheckName = ANSI_TO_TCHAR(Usage.SubTypes[0].ScriptClass->GetName());
        CheckName.RemoveFromStart(TEXT("U"));
        CheckName.RemoveFromStart(TEXT("A"));
        FString PropClassName = ClassProp->MetaClass->GetName();
        return PropClassName == CheckName;
    }
}
```

名称回退的存在感与 `FWeakObjectPtrType::MatchesProperty` 完全一致——脚本类的 `UClass*` 在 `AnalyzeFunctionSignature` 阶段还没被 `AngelscriptClassGenerator` 创建出来，此时只能以"去前缀后的脚本类名"比对 C++ 端 `MetaClass->GetName()`。

### 4.4 `SetArgument` —— UFunction 调用桥

```cpp
// ============================================================================
// 文件: AngelscriptRuntime/Binds/Bind_BlueprintType.cpp
// 函数: FSubclassOfType::SetArgument
// ============================================================================
void SetArgument(const FAngelscriptTypeUsage& Usage, int32 ArgumentIndex,
                 class asIScriptContext* Context, struct FFrame& Stack,
                 const FArgData& Data) const override
{
    TSubclassOf<UObject>* StructMemory = (TSubclassOf<UObject>*)Data.StackPtr;
    new (StructMemory) TSubclassOf<UObject>();

    if (Usage.bIsReference)
    {
        TSubclassOf<UObject>& RefValue =
            Stack.StepCompiledInRef<FClassProperty, TSubclassOf<UObject>>(StructMemory);
        Context->SetArgAddress(ArgumentIndex, &RefValue);
    }
    else
    {
        Stack.StepCompiledIn<FClassProperty>(StructMemory);
        Context->SetArgObject(ArgumentIndex, StructMemory);
    }
}
```

这段代码处理"UE 调脚本 UFunction，参数中含 `TSubclassOf<X>`"——从 UE 调用栈帧 `FFrame` 中取出 `TSubclassOf<UObject>` 写入 AS 上下文。`bIsReference` 区分 by-value 与 by-ref 两种 ABI。

### 4.5 `GetCppForm` —— UHT/StaticJIT 友好

```cpp
// ============================================================================
// 文件: AngelscriptRuntime/Binds/Bind_BlueprintType.cpp
// 函数: FSubclassOfType::GetCppForm
// ============================================================================
bool GetCppForm(const FAngelscriptTypeUsage& Usage, FCppForm& OutCppForm) const override
{
    UClass* MetaClass = GetMetaClass(Usage);
    if (MetaClass != nullptr)
    {
        FString ClassHeaderPath = FAngelscriptBindDatabase::GetSourceHeader(MetaClass);
        if (ClassHeaderPath.Len() != 0)
        {
            OutCppForm.CppType = FString::Printf(TEXT("TSubclassOf<%s%s>"),
                MetaClass->GetPrefixCPP(), *MetaClass->GetName());
            OutCppForm.CppHeader = FString::Printf(TEXT("#include \"%s\""), *ClassHeaderPath);
        }
    }

    OutCppForm.CppGenericType = TEXT("TSubclassOf<UObject>");
    OutCppForm.TemplateObjectForm = TEXT("TSubclassOf<UObject>");
    return true;
}
```

`CppGenericType = "TSubclassOf<UObject>"` 是关键——StaticJIT / UHT 生成 C++ 包装时，**统一类型**用 `<UObject>`，能精确化时再用 `<<前缀><类名>>`（如 `TSubclassOf<AActor>`）。这与 §1.1 的"底层物理布局完全相同"对应。

---

## 五、阶段 2：方法补充（`Bind_TSubclassOf`）

**源码所在**: `Bind_BlueprintType.cpp:1811-1833`，`EOrder::Late-10` 优先级。

```cpp
// ============================================================================
// 文件: AngelscriptRuntime/Binds/Bind_BlueprintType.cpp
// 函数: Bind_TSubclassOf（lambda）
// ============================================================================
AS_FORCE_LINK const FAngelscriptBinds::FBind Bind_TSubclassOf(
    (int32)FAngelscriptBinds::EOrder::Late-10, []
{
    auto TSubclassOf_ = FAngelscriptBinds::ExistingClass("TSubclassOf<T>");
    FSubclassOfType::BaseTypeInfo =
        FAngelscriptEngine::Get().Engine->GetTypeInfoByName("TSubclassOf");

    TSubclassOf_.ImplicitConstructor("void f(UClass Class)",
        FUNC(FAngelscriptSubclassOfHelpers::ImplicitConstruct));     // ★ 含校验
    FAngelscriptBinds::PreviousBindPassScriptObjectTypeAsFirstParam();

    TSubclassOf_.Method("UClass opImplConv() const",
        FUNC_TRIVIAL(FAngelscriptSubclassOfHelpers::GetClass));
    TSubclassOf_.Method("UObject opImplConv() const",
        FUNC_TRIVIAL(FAngelscriptSubclassOfHelpers::GetClass));      // ★ 顺带可转 UObject

    TSubclassOf_.Method("void Set(UClass Class) const",
        FUNC(FAngelscriptSubclassOfHelpers::SetClass));
    FAngelscriptBinds::PreviousBindPassScriptObjectTypeAsFirstParam();
    TSubclassOf_.Method("void opAssign(UClass Class)",
        FUNC(FAngelscriptSubclassOfHelpers::SetClass));
    FAngelscriptBinds::PreviousBindPassScriptObjectTypeAsFirstParam();

    TSubclassOf_.Method("bool opEquals(const TSubclassOf<T>& Other) const",
        FUNC_TRIVIAL(FAngelscriptSubclassOfHelpers::OpEquals));
    TSubclassOf_.Method("bool opEquals(UClass Other) const",
        FUNC_TRIVIAL(FAngelscriptSubclassOfHelpers::OpEqualsClass));

    TSubclassOf_.Method("UClass Get() const",
        FUNC_TRIVIAL(FAngelscriptSubclassOfHelpers::GetClass));
    TSubclassOf_.Method("bool IsValid() const",
        FUNC_TRIVIAL(FAngelscriptSubclassOfHelpers::IsValid));
    TSubclassOf_.Method("bool IsChildOf(UClass Other) const",
        FUNC_TRIVIAL(FAngelscriptSubclassOfHelpers::IsChildOf));
    TSubclassOf_.Method("T handle_only GetDefaultObject() const",
        FUNC_TRIVIAL(FAngelscriptSubclassOfHelpers::GetDefaultObject));    // ★ 返回 T*
});
```

四点细节：

- **`PreviousBindPassScriptObjectTypeAsFirstParam`**：见 §2.2 解释——前一行注册的方法在调用时会获得 `asCObjectType*` 作为隐式首参数。三处需要：`ImplicitConstructor` / `Set` / `opAssign(UClass)`——任何会写入 `*Ptr` 的路径都需要拿到 `T` 做 `IsChildOf` 校验。
- **`UObject opImplConv` 与 `UClass opImplConv` 双注册**：两个隐式转换都返回 `Ptr->Get()`——一个把 `TSubclassOf<T>` 当 `UClass*` 用（如传给 `Cast`），另一个把它当 `UObject*` 用（如比较 `nullptr`）。两者底层就是同一个指针。
- **`bool opEquals(UClass)` 与 `bool opEquals(const TSubclassOf<T>&)`**：脚本里 `Class == AActor::StaticClass()` 与 `Class == OtherSubclass` 都能写。
- **`T handle_only GetDefaultObject()`**：返回类型用 `T handle_only`——这是当前 fork 的特殊语法，表示"返回的是 `T` 的句柄（裸 `UObject*` 指针）"。运行时执行 `Class->GetDefaultObject()` 得到 CDO，AS 端按 `T*` 类型暴露——脚本作者可直接 `Class.GetDefaultObject().BlueprintEditableProperty` 读 CDO 字段。

---

## 六、与 `__StaticType_<ClassName>` / `StaticClass()` 的协作

### 6.1 自动生成的全局变量

每个脚本类在预处理阶段都会被注入一个 `const TSubclassOf<UObject>` 全局变量：

```cpp
// ============================================================================
// 文件: AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp
// 函数: AngelscriptPreprocessor 类生成代码注入
// ============================================================================
FString ClassVar = FString::Printf(TEXT("__StaticType_%s"), *ClassDesc->ClassName);
ClassDesc->StaticClassGlobalVariableName = ClassVar;

if (ClassDesc->Namespace.IsSet())
{
    File.GeneratedCode.Add(FString::Printf(
        TEXT("namespace %s { const TSubclassOf<UObject> %s; "
             "namespace %s { UClass StaticClass() __generated%s "
             "{ return %s; } } }"),
        *ClassDesc->Namespace.GetValue(), *ClassVar,
        *ClassDesc->ClassName, *FunctionSpecifiers, *ClassVar));
}
else
{
    File.GeneratedCode.Add(FString::Printf(
        TEXT("const TSubclassOf<UObject> %s; "
             "namespace %s { UClass StaticClass() __generated%s "
             "{ return %s; } }"),
        *ClassVar, *ClassDesc->ClassName, *FunctionSpecifiers, *ClassVar));
}
```

也就是说，对每一个 `UCLASS() class AMyActor : AActor {}`，预处理器在背后生成两段：

```angelscript
const TSubclassOf<UObject> __StaticType_AMyActor;        // ★ 类静态值变量

namespace AMyActor
{
    UClass StaticClass() __generated
    {
        return __StaticType_AMyActor;                    // ★ 走 opImplConv -> UClass*
    }
}
```

### 6.2 类生成阶段回填值

`__StaticType_*` 变量在脚本编译时只是声明，真正的 `UClass*` 由 `AngelscriptClassGenerator` 在生成 `UASClass` 后回填：

```cpp
// ============================================================================
// 文件: AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp
// 角色: 把生成好的 UASClass 写回脚本端 __StaticType_* 变量
// ============================================================================
asCGlobalProperty* Property = ScriptModule->scriptGlobals.FindFirst(
    TCHAR_TO_ANSI(*ClassDesc->StaticClassGlobalVariableName),
    ScriptNamespace
);
// ...
void* VarAddr = Property->GetAddressOfValue();
**(TSubclassOf<UObject>**)VarAddr = Class;               // ★ 直接覆盖底层 UClass*
```

这段是整套机制的"最后一根钢筋"——`TSubclassOf<UObject>` 物理上就是一个 `UClass*` 指针，C++ 端直接 reinterpret 成指针并赋值即可，**不**走 `SetClass` 校验路径（因为是引擎生成的合法值）。

### 6.3 C++ 侧 BindUClass 同样回填

C++ 端原生 `UClass*`（如 `AActor`）也通过同样机制注册：

```cpp
// ============================================================================
// 文件: AngelscriptRuntime/Binds/Bind_BlueprintType.cpp
// 函数: BindUClass（节选最后一段）
// ============================================================================
{
    FString Decl = FString::Printf(
        TEXT("const TSubclassOf<UObject> __StaticType_%s"), *TypeName);
    TSubclassOf<UObject>* ClassValue = new TSubclassOf<UObject>(Class);
    FAngelscriptBinds::BindGlobalVariable(Decl, ClassValue);
}
```

注册的是一个 `TSubclassOf<UObject>` 全局变量，初始就指向已绑定的 `UClass*`。这让脚本作者无论操作 C++ 类（`AActor`）还是脚本类（`AMyActor`），都通过同一个 `__StaticType_<ClassName>` 入口，得到一个**协变可用的 `TSubclassOf<UObject>`**。

### 6.4 在脚本中的等价用法

借助 §6.1-6.3 的机制，下面这些写法在 AS 端**完全等价**：

```angelscript
// ============================================================================
// 文件: 任意 .as
// 角色: 三种写法在 AS 中都生成相同字节码（依赖 __StaticType_<X>）
// ============================================================================
TSubclassOf<AActor> S1 = AActor::StaticClass();    // 走 namespace AActor::StaticClass()
TSubclassOf<AActor> S2 = __StaticType_AActor;      // 直接读全局变量
TSubclassOf<AActor> S3 = AActor;                   // 当 ConfigSettings 允许"类型作值"
```

第三种是 fork 引入的语法糖，受 `EAngelscriptStaticClassMode::StaticClassDeprecation` 控制（`Disallowed` / `Deprecated` / `Allowed`），让 `AActor` 直接当 `TSubclassOf<UObject>` 用。

---

## 七、与 `SpawnActor` / `NewObject` / `CreateComponent` 的协作

### 7.1 `SpawnActor` —— 把 TSubclassOf 当主类型签名

```cpp
// ============================================================================
// 文件: AngelscriptRuntime/Binds/Bind_AActor.cpp
// 函数: FAngelscriptActorBinds::SpawnActor
// ============================================================================
AActor* FAngelscriptActorBinds::SpawnActor(
    const TSubclassOf<AActor>& Class,
    const FVector& Location, const FRotator& Rotation,
    const FName& Name, bool bDeferredSpawn, ULevel* Level)
{
    UObject* WorldContext = FAngelscriptEngine::TryGetCurrentWorldContextObject();
    UWorld* World = GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull);
    if (World == nullptr)
    { FAngelscriptEngine::Throw("Invalid World Context"); return nullptr; }

    if (Class == nullptr)
    { FAngelscriptEngine::Throw("Class was nullptr."); return nullptr; }

    FActorSpawnParameters Params;
    Params.Name = Name;
    Params.NameMode = FActorSpawnParameters::ESpawnActorNameMode::Requested;
    Params.bDeferConstruction = bDeferredSpawn;
    // ... OverrideLevel 解析 ...
    return World->SpawnActor(Class, &Location, &Rotation, Params);
}
```

注册成 AS 全局函数：

```cpp
FAngelscriptBinds::BindGlobalFunction(
    "AActor SpawnActor(const TSubclassOf<AActor>& Class, "
    "const FVector& Location = FVector::ZeroVector, "
    "const FRotator& Rotation = FRotator::ZeroRotator, "
    "const FName& Name = NAME_None, bool bDeferredSpawn = false, "
    "ULevel Level = nullptr)",
    FUNC(FAngelscriptActorBinds::SpawnActor));
```

调用链路：

```text
AS:    AActor A = SpawnActor(AMyActor::StaticClass());
        │
        ▼
AS->ImplicitConstruct( asCObjectType<TSubclassOf<AActor>>, AMyActor::StaticClass() )
        │  IsChildOf(AActor) ✓
        ▼
TSubclassOf<UObject>{ Class = AMyActor }   ← 物理上
        │
        ▼
C++:   FAngelscriptActorBinds::SpawnActor( const TSubclassOf<AActor>&, ... )
        │  reinterpret_cast<const TSubclassOf<AActor>&>  ← 物理布局相同
        ▼
World->SpawnActor( Class /* UClass* */, ... );
```

注意：脚本端调用 `SpawnActor(SomeRandomClass)` 时，`SomeRandomClass` 不是 `AActor` 派生 → `ImplicitConstruct` 抛 AS 异常，函数体根本进不去。这就是 **`TSubclassOf<T>` 比裸 `UClass*` 更安全**的实质表现。

### 7.2 `NewObject` —— 同样的契约

```cpp
// ============================================================================
// 文件: AngelscriptRuntime/Binds/Bind_UObject.cpp
// 角色: NewObject 全局函数注册（节选）
// ============================================================================
FAngelscriptBinds::BindGlobalFunction(
  "UObject NewObject(UObject Outer, const TSubclassOf<UObject>& Class, "
  "FName Name = NAME_None, bool bTransient = false)",
[](UObject* Outer, const TSubclassOf<UObject>& Class,
   FName Name, bool bTransient) -> UObject*
{
    if (Class.Get() == nullptr)
    {
        FAngelscriptEngine::Throw("Class was nullptr.");
        return nullptr;
    }
    if (Outer == nullptr)
    {
        Outer = GetTransientPackage();
        bTransient = true;
    }
    EObjectFlags Flags = RF_NoFlags;
    if (bTransient) Flags |= RF_Transient;

    FAngelscriptExcludeScopeFromLoopTimeout TimeoutExclusion;
    return NewObject<UObject>(Outer, Class.Get(), Name, Flags);
});
FAngelscriptBinds::SetPreviousBindArgumentDeterminesOutputType(1);          // ★ 出参类型 = 第 2 参数
```

`SetPreviousBindArgumentDeterminesOutputType(1)` 是关键——它告诉 AS 编译器：返回值的具体类型由第 1 个参数（`Class`）决定。也就是说写 `NewObject(this, AMyClass)` 时，编译器会把返回值类型从 `UObject` 自动 narrow 到 `AMyClass`，无需 `Cast`。

### 7.3 `CreateComponent` / `GetComponent` / `GetOrCreateComponent`

```cpp
// ============================================================================
// 文件: AngelscriptRuntime/Binds/Bind_UActorComponent.cpp
// 角色: AActor 上的组件接口（节选签名）
// ============================================================================
AActor_.Method("UActorComponent CreateComponent(const TSubclassOf<UActorComponent>& "
               "ComponentClass, const FName& WithName = NAME_None)",
    FUNC(FAngelscriptActorBinds::CreateComponent));

AActor_.Method("UActorComponent GetComponent(const TSubclassOf<UActorComponent>& "
               "ComponentClass, const FName& WithName = NAME_None)",
    FUNC(FAngelscriptActorBinds::GetComponent));

AActor_.Method("UActorComponent GetOrCreateComponent(const TSubclassOf<UActorComponent>& "
               "ComponentClass, const FName& WithName = NAME_None)",
    FUNC(FAngelscriptActorBinds::GetOrCreateComponent));
```

三者参数都是 `TSubclassOf<UActorComponent>`，AS 端调用时若传一个非 `UActorComponent` 派生的 `UClass*`，会在 `ImplicitConstruct` 阶段被拦下。

### 7.4 SpawnActor 在脚本中的实际样态

```angelscript
// ============================================================================
// 文件: 任意 .as（节选自 AngelscriptActorSpawnPatternsTests）
// 角色: SpawnActor 调用形式
// ============================================================================
AActor PositionalSpawn = Cast<AFunctionalSpawnTargetActor>(SpawnActor(
    AFunctionalSpawnTargetActor::StaticClass(),
    FVector(100.0, 0.0, 0.0), FRotator::ZeroRotator));

AActor NamedSpawn = Cast<AFunctionalSpawnTargetActor>(SpawnActor(
    AFunctionalSpawnTargetActor::StaticClass(),
    Location = FVector(0.0, 100.0, 0.0),
    Rotation = FRotator::ZeroRotator));

TSubclassOf<AFunctionalSpawnTargetActor> TargetSubclass =
    AFunctionalSpawnTargetActor::StaticClass();
AFunctionalSpawnTargetActor TypedSpawn =
    Cast<AFunctionalSpawnTargetActor>(SpawnActor(TargetSubclass));
```

最后一种最能体现 `TSubclassOf<T>` 的价值：**先把 `UClass*` "绑定到一个具体派生类"，后续可重复使用**——传入 `SpawnActor(TSubclassOf<AActor>)` 时利用协变（`TSubclassOf<Target>` → `TSubclassOf<AActor>`）安全降级。

---

## 八、UPROPERTY 集成：编辑器 ClassPicker

### 8.1 声明示例

```angelscript
// ============================================================================
// 文件: 任意 .as（节选自 AngelscriptSyntaxUPropertyTests）
// 角色: UPROPERTY TSubclassOf 字段
// ============================================================================
class AUPropSubclassActor : AActor
{
    UPROPERTY()
    TSubclassOf<AActor> ActorClass;          // 编辑器面板出现 AActor 派生 ClassPicker

    UPROPERTY(EditDefaultsOnly, Category = "Spawning")
    TSubclassOf<UActorComponent> ComponentClass;
}
```

### 8.2 走通的反射链路

```text
.as 源码
   UPROPERTY() TSubclassOf<AActor> ActorClass;
        │
        ▼ 预处理 / 解析 / FAngelscriptType 解析
   FAngelscriptTypeUsage{ Type = SubclassOfType,
                          SubTypes = [ FAngelscriptTypeUsage::FromClass(AActor) ] }
        │
        ▼ AngelscriptClassGenerator 生成 UPROPERTY
   FSubclassOfType::CreateProperty
        │  new FClassProperty + CPF_UObjectWrapper + MetaClass=AActor
        ▼
   FClassProperty {
       PropertyClass = UClass::StaticClass(),     // 字段值类型 = UClass*
       MetaClass = AActor::StaticClass(),         // 蓝图 ClassPicker 过滤器
       PropertyFlags |= CPF_UObjectWrapper        // 区分裸 UClass*
   }
        │
        ▼ UE 编辑器读 FClassProperty
   PropertyEditor 检测 MetaClass → 显示 SClassPropertyEntryBox
        │  (只列出 AActor + 派生类)
        ▼ 用户选完
   FObjectPropertyBase::ImportText_Internal 写入字段
```

UPROPERTY 元数据完整传递让 `Syntax_UPROPERTY.md` 中讲到的 metadata 装饰对 `TSubclassOf` 字段同样有效——`AllowedClasses` / `MustImplement` / `BlueprintBaseOnly` 等：

```angelscript
UPROPERTY(meta = (AllowedClasses = "AActor", MustImplement = "/Script/MyModule.IInteractable"))
TSubclassOf<AActor> SpawnableInteractables;
```

### 8.3 反向 `RegisterTypeFinder` —— C++ 字段反向解析

```cpp
// ============================================================================
// 文件: AngelscriptRuntime/Binds/Bind_BlueprintType.cpp
// 函数: BindUClassLookup（节选反向解析逻辑）
// ============================================================================
// Class properties are sometimes TSubclassOf<>
if (ClassProperty != nullptr && (ClassProperty->PropertyFlags & CPF_UObjectWrapper) != 0)
{
    FAngelscriptTypeUsage InnerType =
        FAngelscriptTypeUsage::FromClass(ClassProperty->MetaClass);
    if (!InnerType.IsValid())
        return false;

    Usage.Type = SubclassOfType;
    Usage.SubTypes.SetNum(1);
    Usage.SubTypes[0] = InnerType;
    return true;
}
```

当 C++ 类暴露 `UPROPERTY() TSubclassOf<UWeapon> DefaultWeaponClass;` 给 AS 时：

1. UHT 生成 `FClassProperty { PropertyClass = UClass::StaticClass(), MetaClass = UWeapon, Flags |= CPF_UObjectWrapper }`；
2. `RegisterTypeFinder` 看到 `FClassProperty + CPF_UObjectWrapper`，把它解析成 `FAngelscriptTypeUsage{ Type=SubclassOfType, SubTypes=[ObjectType(UWeapon)] }`；
3. AS 端就可以写 `MyClass.DefaultWeaponClass.IsValid()` / `MyClass.DefaultWeaponClass.GetDefaultObject()`。

---

## 九、限制与避坑

### 9.1 不能用基本类型 / Struct / 嵌套自身做 T

`TemplateCallback` 拒绝任何非对象类型作 `T`：

```angelscript
TSubclassOf<int> Bad;                 // ✗ asOBJ_VALUE → "Subtype must be a class type"
TSubclassOf<FVector> Bad;             // ✗ 同上
TSubclassOf<NonExistentClass> Bad;    // ✗ 解析阶段就找不到类型
TSubclassOf Bad;                      // ✗ 缺少模板参数
TSubclassOf<TSubclassOf<AActor>> Bad; // ✗ 模板嵌套自身（asOBJ_VALUE 复合）
```

详见 `AngelscriptSyntaxSmartPointerTests.cpp::TSubclassOf_Negative`。

### 9.2 赋值非派生类 = 抛异常 + 置 null

```angelscript
TSubclassOf<AActor> S = UPackage::StaticClass();
// ↑ 运行时抛: "Class set to TSubclassOf<> was not a child of templated class."
// 同时 S.Get() == nullptr
```

下游代码若不检查 `IsValid()`，会在 `S.GetDefaultObject()` / `SpawnActor(S)` 时 fail。**两道防线**：

1. AS 异常已经记到日志，TestRunner 会捕获；
2. 即使忽略异常，`S` 也已被重置为 null，`SpawnActor` 会再抛一次 "Class was nullptr."。

实测见 `AngelscriptClassBindingsTests.cpp::TSubclassOfRejectsUnrelatedClass` 完整契约。

### 9.3 `IsValid()` 不查 GC

```angelscript
TSubclassOf<AActor> S = AMyActor::StaticClass();
// ... AMyActor 资产被卸载 ...
S.IsValid();          // 仍可能返回 true（指针没被清）
S.Get();              // 返回悬空 UClass*，访问触发崩溃
```

`UClass*` 不像 `UObject*` 那样会被弱引用感知 GC——`TSubclassOf::IsValid` 只是 `Get() != nullptr`。如果脚本要在资产可能被卸载的场景使用，应改用 `TSoftClassPtr<T>` —— 它持有路径而非裸指针，能在卸载后失效。

### 9.4 协变陷阱：`TSubclassOf<UPackage>` 与 `TSubclassOf<AActor>` 之间不互转

```text
TSubclassOf<UPackage> Pkg = UPackage::StaticClass();
TSubclassOf<AActor> Actor = Pkg;        // ✗ 编译期就拒绝
```

虽然两者都是 `TSubclassOf<T>` 模板，但 `UPackage` 与 `AActor` 不存在子类关系——`as_compiler.cpp:8759-8775` 的 `bBothTypesFlaggedCovariant` 路径会显式拒绝这种"看似协变实则不共享继承"的转换。这是上文 §三说的"防止 `opImplConv -> ImplicitConstruct` 绕过校验"的兜底。

### 9.5 容器嵌套 `TSubclassOf` 是合法的

```angelscript
TArray<TSubclassOf<AActor>> ActorClasses;        // ✓ Array 元素
TSet<TSubclassOf<AActor>> ClassSet;              // ✓ Set 元素
TMap<TSubclassOf<AActor>, int> ClassToInt;       // ✓ Map 键
```

实测见 `AngelscriptClassBindingsTests.cpp::Array_LiteralNum / SetKey_Contains / MapKey_FindValue`。这与 `TWeakObjectPtr` 等"`CanBeTemplateSubType=false`"的容器不一样——`TSubclassOf<UObject>` 本质是 `UClass*`，在容器中和裸指针的存储/哈希/比较一致。

### 9.6 调试器显示

`FSubclassOfType::GetDebuggerValue` 在 DAP 调试器中把 `TSubclassOf<T>` 显示为 `{ ClassName }` 或 `nullptr`：

```text
SubclassRef:                                  ▼ TSubclassOf<AActor> = { ACameraActor }
EmptyRef:                                       TSubclassOf<UActorComponent> = nullptr
```

不展开成员（`bHasMembers = false`）——因为内容只有一个 `UClass*` 指针，展开没有意义。

---

## 附录 A：API 速查

| AS 签名 | 语义 | 校验 |
|---------|------|------|
| `TSubclassOf<T>()` | 默认构造空 | — |
| `TSubclassOf<T>(const TSubclassOf<T>&)` | 同类型拷贝构造 | 来源已合法 |
| `TSubclassOf<T>(UClass)` 隐式 | 从裸 `UClass*` 构造 | `IsChildOf(T)`，失败抛异常 + 置 null |
| `TSubclassOf<T>& opAssign(const TSubclassOf<T>&)` | 同类型拷贝赋值 | 来源已合法 |
| `void opAssign(UClass)` | 从裸 `UClass*` 赋值 | 同 ImplicitConstruct |
| `void Set(UClass) const` | 显式赋值（同 opAssign） | 同上 |
| `UClass opImplConv() const` | 隐式转 `UClass*` | — |
| `UObject opImplConv() const` | 隐式转 `UObject*`（实际还是 `UClass*`） | — |
| `UClass Get() const` | 取裸 `UClass*` | — |
| `bool IsValid() const` | `Get() != nullptr` | **不查 GC** |
| `bool IsChildOf(UClass Other) const` | 双 null 防御后 `IsChildOf` | — |
| `bool opEquals(const TSubclassOf<T>&) const` | `*Ptr == Other` | — |
| `bool opEquals(UClass) const` | `*Ptr == Other` | — |
| `T handle_only GetDefaultObject() const` | `Class->GetDefaultObject()` | null → null |

### 反射桥（FSubclassOfType → FClassProperty）

| FProperty 字段 | 取值 |
|---------------|------|
| `PropertyClass` | `UClass::StaticClass()` |
| `MetaClass` | `T` 对应的 `UClass*` |
| `PropertyFlags` | `CPF_UObjectWrapper` |

### 协作 API 列表（`TSubclassOf` 出现在签名中）

| 全局函数 / 方法 | 文件 |
|---------------|------|
| `SpawnActor(const TSubclassOf<AActor>&, …)` | `Bind_AActor.cpp` |
| `SpawnPersistentActor(const TSubclassOf<AActor>&, …)` | `Bind_AActor.cpp` |
| `NewObject(UObject, const TSubclassOf<UObject>&, …)` | `Bind_UObject.cpp` |
| `AActor::CreateComponent(const TSubclassOf<UActorComponent>&, …)` | `Bind_UActorComponent.cpp` |
| `AActor::GetComponent(const TSubclassOf<UActorComponent>&, …)` | `Bind_UActorComponent.cpp` |
| `AActor::GetOrCreateComponent(const TSubclassOf<UActorComponent>&, …)` | `Bind_UActorComponent.cpp` |
| `AActor::GetTypedOuter(const TSubclassOf<UObject>&)` | `Bind_UObject.cpp` |
| `__Actor_GetComponentByClass(const TSubclassOf<UObject>&, ?&out, …)` | `Bind_UActorComponent.cpp`（template magic）|

---

## 附录 B：避坑清单

1. **校验时机**：C++ 编译期靠 `TSubclassOf<T>::operator=` SFINAE，AS 运行时靠 `FAngelscriptSubclassOfHelpers::ImplicitConstruct/SetClass` 调 `IsChildOf`。
2. **失败行为**：传错类时**双击**——AS 异常 + 把字段置 null。下游代码再检查 `IsValid()` 时就是 false。
3. **性能**：`TSubclassOf<T>` 在 AS 端只占一个指针，复制/赋值无堆分配；`IsChildOf` 走 UE 反射树遍历，O(深度)，常数级。
4. **`TSubclassOf<T>` ≠ `TSoftClassPtr<T>`**：前者持有运行期 `UClass*`，资产卸载后悬空；后者持有路径，可在 AssetManager 中安全 Resolve。需要在卸载场景下使用时改 SoftClassPtr。
5. **不要靠裸 `UClass*` 当 `TSubclassOf` 用**：失去蓝图 ClassPicker 过滤、失去 `IsChildOf` 类型契约、失去签名表达力——除非确实要"接受任意 Class"。
6. **协变只在 `T` 真有继承关系时存在**：`TSubclassOf<UPackage>` 不能转 `TSubclassOf<AActor>`，编译期就被 `as_compiler.cpp:8759` 拒绝。
7. **`__StaticType_<ClassName>` 是预处理器生成的**：脚本里出现的 `AActor::StaticClass()` 实际等价于读这个全局 `TSubclassOf<UObject>`——理解这一点能解释很多"类型当值用"的语法糖。
8. **HotReload 安全网**：`SetClass` 中的 `bWillBecomeCorrect` 分支允许"还未 reinstance 的旧 UClass 资产"赋给新版字段——这是 fork 引入的细节，不要误以为是 bug。
9. **UPROPERTY metadata 完整支持**：`AllowedClasses` / `MustImplement` / `BlueprintBaseOnly` / `EditDefaultsOnly` 等都直接传给 `FClassProperty`，编辑器表现与原生 C++ UPROPERTY 一致。
10. **调试器面板**：`{ ClassName }` 一行显示，不展开——别期待看到 CDO 字段（看 CDO 用 `Class.GetDefaultObject()` 显式访问）。

---

## 小结

- `TSubclassOf<T>` = "**保证子类的 `UClass*`**" —— 物理上一个指针，逻辑上靠 `IsChildOf` 校验确保是 T 派生类；脚本端违规赋值同时抛 AS 异常并把字段置 null。
- AS 实现采用**两阶段注册**：阶段 1 (`EOrder::Early`) 注册 `ValueClass<TSubclassOf<UObject>>` + 协变 + TemplateCallback 校验子类型；阶段 2 (`EOrder::Late-10`) 补充 `ImplicitConstructor` / `opImplConv` / `Set` / `Get` / `IsChildOf` / `GetDefaultObject` 等需要 `T` 信息的方法（通过 `PreviousBindPassScriptObjectTypeAsFirstParam` 注入 `asCObjectType*`）。
- `FSubclassOfType` 是 UE 反射桥——`CreateProperty` 生成 `FClassProperty + CPF_UObjectWrapper + MetaClass=T` 触发蓝图 ClassPicker；`RegisterTypeFinder` 反向把 C++ 端 `FClassProperty` 解析成 `FAngelscriptTypeUsage`。
- 与 `TWeakObjectPtr` / `TSoftObjectPtr` 共享同一注册骨架，差异只在底层物理类型 / 校验策略 / FProperty 类（`FClassProperty` vs `FWeakObjectProperty` vs `FSoftObjectProperty`）。
- `__StaticType_<ClassName>` 全局 `TSubclassOf<UObject>` 变量由预处理器为每个脚本类自动生成，由 ClassGenerator 回填——这是脚本端 `MyClass::StaticClass()` 与"类型当值用"语法糖的底层支柱。
- 与 `SpawnActor` / `NewObject` / `CreateComponent` 协作时，`TSubclassOf<T>` 替代裸 `UClass*` 提供编译期表达力 + 运行时 `IsChildOf` 拦截；非派生类传入会被签名守卫直接拒掉。

---

## 修订记录

| 版本 | 日期 | 内容 |
|------|------|------|
| v1.0 | 2026-05-24 | 首版：基于 `Bind_TSubclassOf.h`（`FAngelscriptSubclassOfHelpers` 8 个静态助手）+ `Bind_BlueprintType.cpp:1567-1833`（阶段 1 模板声明 / `FSubclassOfType` facade / 阶段 2 方法补充）+ `Bind_BlueprintType.cpp:2474-2574`（`BindUClassLookup` 反射桥）+ `AngelscriptPreprocessor.cpp` `__StaticType_<ClassName>` 注入 + `AngelscriptClassGenerator.cpp` 回填路径 + `Bind_AActor.cpp::SpawnActor` / `Bind_UObject.cpp::NewObject` / `Bind_UActorComponent.cpp::CreateComponent` 签名协作 + `AngelscriptClassBindingsTests.cpp` / `AngelscriptSyntaxSmartPointerTests.cpp` / `AngelscriptSyntaxUPropertyTests.cpp` 行为契约完整产出。覆盖：① 数据布局（一个 `UClass*`）与设计动机（裸 `UClass*` vs `TSubclassOf<T>` 三大差异）；② `FAngelscriptSubclassOfHelpers` 8 个静态函数（构造/拷贝/赋值四件套 + ImplicitConstruct/SetClass 校验路径含 HotReload 安全网 + 查询四件套）；③ 阶段 1 模板声明（`asOBJ_TEMPLATE_SUBTYPE_COVARIANT` / 底层 `<UObject>` 统一 / TemplateCallback 拒绝非对象类型）；④ 类型 facade `FSubclassOfType` 14 个虚函数职责（`CreateProperty` 生成 `FClassProperty + CPF_UObjectWrapper + MetaClass` / `MatchesProperty` 名称回退 / `SetArgument` UFunction 桥 / `GetCppForm` UHT 友好）；⑤ 阶段 2 方法补充（`PreviousBindPassScriptObjectTypeAsFirstParam` 注入 `asCObjectType*` / 双 `opImplConv` / 双 `opEquals` / `T handle_only GetDefaultObject`）；⑥ `__StaticType_<ClassName>` 与 `StaticClass()` 协作链路（预处理器生成 / ClassGenerator 回填 / BindUClass 同样路径）；⑦ 与 `SpawnActor` / `NewObject` / `CreateComponent` 的签名协作 + `SetPreviousBindArgumentDeterminesOutputType(1)` 类型 narrow；⑧ UPROPERTY 编辑器集成（蓝图 ClassPicker 过滤器 / `RegisterTypeFinder` 反向解析 / metadata 直传）；⑨ 6 项限制与避坑（T 类型约束 / 抛异常 + 置 null 双保险 / `IsValid` 不查 GC / 协变陷阱 / 容器可嵌套 / 调试器显示）。 |
