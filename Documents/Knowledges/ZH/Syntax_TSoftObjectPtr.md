# Syntax_TSoftObjectPtr — `TSoftObjectPtr<T>` 与 `TSoftClassPtr<T>` 软引用资源/类实现原理

> **所属前缀**: Syntax_（智能指针与引用包装族）
> **关注层面**: 语法机制、模板实例化、资源路径解析、异步加载链路（不写"用户怎么用"——那是 `Guide_*` 的活；不写 `FSoftObjectPath` 内部布局——那是 UE 标准库实现，本文仅引用其 AS 绑定面）
> **关键源码**:
> `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TSoftObjectPtr.cpp` (~668 行，单文件覆盖 `TSoftObjectPtr` + `TSoftClassPtr`)
> · `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_SoftObjectPath.cpp` (~82 行，`FSoftObjectPath` / `FSoftClassPath` 表面绑定)
> · `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UAssetManager.cpp` (~113 行，`UAssetManager` / `FPrimaryAssetId` 入口)
> · `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/SoftReferenceStatics.h` (~16 行，`FOnSoftObjectLoaded` / `FOnSoftClassLoaded` 委托声明)
> · `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/BlueprintCallableReflectiveFallback.cpp` `:196-208` —— 反射 fallback 显式跳过软指针属性
> · `Plugins/Angelscript/Source/AngelscriptTest/Syntax/AngelscriptSyntaxSmartPointerTests.cpp` —— 编译契约
> · `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptSoftReferenceFunctionLibraryTests.cpp` —— 异步回调行为契约
> **关联文档**:
> `Documents/Knowledges/ZH/Syntax_TWeakObjectPtr.md` —— 兄弟弱引用，最近的对照样本
> · `Documents/Knowledges/ZH/Syntax_TSubclassOf.md` —— `TSoftClassPtr` 的"硬"对应（持有 `UClass*`）
> · `Documents/Knowledges/ZH/Syntax_TOptional.md` —— 单 T 模板 facade 对照
> · `Documents/Knowledges/ZH/Type_Core.md` §4 —— UObject / UClass 在 AS 类型系统中的桥接
> · `Documents/Knowledges/ZH/Type_BindSystem.md` —— `FAngelscriptType` / `FAngelscriptTypeUsage` 多态分派
> **外部参考**（可选）:
> [UE5 `SoftObjectPtr.h`](https://github.com/EpicGames/UnrealEngine/blob/release/Engine/Source/Runtime/CoreUObject/Public/UObject/SoftObjectPtr.h)

---

## 概览

`TSoftObjectPtr<T>` 与 `TSoftClassPtr<T>` 是 UE 的"**资源路径引用**"——不强制持有目标对象，而是持有一个 `FSoftObjectPath`（`PackageName + AssetName + SubObject`）。这意味着：
- **不阻止 GC**——目标对象可以被卸载，路径仍保留；
- **不在加载时占用内存**——只有真正需要时才解析为对象；
- **打包阶段可被 cooker 捕获**——cooker 会扫描所有 `FSoftObjectPath` 字段，把指向的资源拉进打包依赖图；
- **必须显式 Load**——`Get()` 仅返回当前已加载的对象，未加载时返回 `nullptr`。

在 AS 插件中，二者**共用一份底层 C++ 类型 `FSoftObjectPtr`**，由 `Bind_TSoftObjectPtr.cpp` 单文件覆盖：通过两个并列的 `FAngelscriptType` 子类（`FSoftObjectPtrType` / `FSoftClassPtrType`）共享继承基类 `FBaseSoftReferenceType` 的反射桥逻辑，只在 `CreateProperty`（`FSoftObjectProperty` vs `FSoftClassProperty`）与 `GetClassOfObject` 上分岔。

```text
                AS 脚本侧                                    C++ 实现侧
                ====================                          ======================
TSoftObjectPtr<UStaticMesh> Mesh;                       ValueClass<FSoftObjectPtr>
                                                            ("TSoftObjectPtr<class T>", {bTemplate, Covariant})
Mesh = SomeMesh;                                         ImplicitConstructor(T handle_only)
                                                            -> new FSoftObjectPtr(Object)
Mesh.IsValid()  / IsPending()  / IsNull()               转发 FSoftObjectPtr::IsValid 等
Mesh.Get()                                              FSoftObjectPtr::Get() + IsA(SubType) 校验
Mesh.LoadAsync(OnLoaded)                                LoadPackageAsync + Lambda 适配 dyn 委托
Mesh.ToString() / ToSoftObjectPath()                    暴露路径
Mesh.GetLongPackageName() / GetAssetName()              资源路径分解
TSoftClassPtr<AActor> Cls;                              同 ValueClass<FSoftObjectPtr> 复用
Cls.Get() -> TSubclassOf<AActor>                        比 Soft Object 多一层 Cast<UClass> + IsChildOf

#if WITH_EDITOR
Mesh.EditorOnlyLoadSynchronous()                        同步加载（仅 Editor 暴露）
#endif
```

### 在族谱中的位置

```text
            +----------------------------------------------------------+
            |  UObject 三种引用强度（强 / 弱 / 软）—— 共享同 facade 模式 |
            +----------------------------------------------------------+
            |  T*  /  UObject*       hard       阻 GC，立即指向对象      |
            |  TWeakObjectPtr<T>     weak       不阻 GC，仅弱句柄         |
            |  TSoftObjectPtr<T>     soft       不阻 GC，仅 Path（资源路径）★本文 |
            +----------------------------------------------------------+

            类对应:                      物对应:
            +----------------+        +----------------+
            | UClass*        |        | UObject*       |   hard
            | TSubclassOf<T> |   <->  | T*             |   hard (类型化)
            | TSoftClassPtr  |   <->  | TSoftObjectPtr |   soft (资源路径)
            +----------------+        +----------------+

            共同骨架（由本仓库 Bind_TSoftObjectPtr.cpp 实现）:
            - 模板值类型 + asOBJ_TEMPLATE_SUBTYPE_COVARIANT 协变
            - 两阶段注册: EOrder::Early 声明 + EOrder::Late-5 方法补充
            - 底层 C++ 统一为 FSoftObjectPtr（不区分 Object / Class）
            - 靠 SubType[0]->GetUserData() 拿到 T 对应的 UClass*
            - FSoftObjectProperty / FSoftClassProperty 反射桥
            - LoadAsync + LoadPackageAsync 异步链路
            - Actor / Component 显式拒绝异步加载 -> 走 LevelStreaming
```

### 与兄弟智能指针的关键差异

| 维度 | `TSoftObjectPtr<T>` (本文) | `TWeakObjectPtr<T>` | `TSubclassOf<T>` |
|------|---------------------------|---------------------|------------------|
| 物理布局 | `FSoftObjectPtr`（含 `FSoftObjectPath`） | `FWeakObjectPtr`（含 ObjectIndex+SerialNumber） | `UClass*` |
| 引用强度 | 软（路径） | 弱（GC 句柄） | 硬（直接持有 UClass） |
| GC 影响 | 不阻 GC、对象可卸载 | 不阻 GC、对象被 GC 后失效 | 持有 UClass 但不持有实例 |
| 异步加载 | **支持** `LoadAsync` | 无意义（弱引用本身就是弱化的硬引用） | 类已加载，无需 |
| Cooker 捕获 | **是**——cooker 扫描 path 字段并 build 依赖 | 否 | 是（持有 UClass*） |
| UE 反射属性 | `FSoftObjectProperty` (+ `FSoftClassProperty`) | `FWeakObjectProperty` | `FClassProperty` (+ `MetaClass`) |
| AS 类型 facade | `FSoftObjectPtrType` / `FSoftClassPtrType` | `FWeakObjectPtrType` | `FSubclassOfType` |
| Editor Asset Picker | 是（按 PropertyClass 过滤） | 是（按 PropertyClass 过滤） | 是（按 MetaClass 过滤） |

后续按以下顺序展开：① 三种引用对比与设计动机；② `FSoftObjectPtr` 物理布局与 `FSoftObjectPath`；③ 类型 facade 体系（`FBaseSoftReferenceType` + 两个子类）；④ 阶段 1 模板注册（`Bind_SoftReferences_Declaration`）；⑤ 阶段 2 方法注册（`Bind_SoftReferences` + `BindSoftPtrBaseMethods`）；⑥ AS 端语法速查与典型 `.as` 模式；⑦ UPROPERTY 资源选择器与反射 fallback 跳过；⑧ 异步加载链路（`LoadAsync` 与 `LoadPackageAsync` 适配）；⑨ AssetManager 与 Streamable 集成；⑩ Cooked Build 依赖图；⑪ 限制与避坑。

---

## 一、三种引用对比：hard / weak / soft

### 1.1 内存语义

```text
[hard]    SomeActor* Actor = NewActor;       // Actor 立即指向对象
                                              // 持有 Actor 防止 GC
                                              // sizeof = 8 bytes (UObject*)

[weak]    TWeakObjectPtr<AActor> WeakRef;    // 仍持有 Actor 反查所需的 Index+Serial
          WeakRef = NewActor;                 // GC 时不被算作活引用，但能查"是否还活着"
                                              // sizeof ~ 16 bytes

[soft]    TSoftObjectPtr<UStaticMesh> Soft;   // 仅持有路径字符串
          Soft = MeshPath;                    // 对象可以从未加载、加载后卸载、再加载——路径不变
                                              // sizeof ~ 32-64 bytes (FName + FName + FString)
```

### 1.2 生命周期对比

```text
                  对象未加载             加载后 alive          GC 后
hard:             [编译期错误]           Actor != nullptr       崩溃（dangling）
weak:             nullptr               WeakRef.Get() != null  IsStale=true, Get=null
soft:             IsValid=false         IsValid=true           IsValid=false（再加载即恢复）
                  IsPending=true                                 IsPending=true
```

`IsPending` 是软引用独有概念——表示"路径有效但对象未在内存中"。这给了脚本作者一个判断点："要不要发起 `LoadAsync`？"。

### 1.3 适用场景决策

| 场景 | 选 hard | 选 weak | 选 soft |
|------|---------|---------|---------|
| 同关卡/同蓝图常驻引用 | ★ | | |
| 偶尔触发的回调持有者（避免循环持有） | | ★ | |
| 武器/法术/UI 图标/技能数据库等"按需加载" | | | ★ |
| 角色装备的 Mesh / Material 配置 | | | ★ |
| Cinematic 序列的 LevelSequence | | | ★ |
| 子弹的发射者（玩家死后可清空） | | ★ | |
| `OwningActor->StaticMeshComponent` 这类同 actor 内引用 | ★ | | |
| 跨地图/跨 World 的资源引用 | | | ★（必须） |

记住**软引用的核心收益**是"延迟成本"——对象不被加载到内存就不付出内存代价；而它的代价是**首次访问需要异步等待**。

---

## 二、底层布局：`FSoftObjectPtr` 与 `FSoftObjectPath`

### 2.1 `FSoftObjectPtr` 是什么

`FSoftObjectPtr` 是 `TSoftObjectPtr<T>` 的非模板版本——`TSoftObjectPtr<T>` 在 UE 标准库里就是 `FSoftObjectPtr` 的薄类型化包装（与 `TSubclassOf<T>` 是 `UClass*` 的薄包装的设计完全平行）。这给本仓库带来一个直接好处：

> **AS 端的 `TSoftObjectPtr<T>` 与 `TSoftClassPtr<T>` 都用同一个 C++ 类型 `FSoftObjectPtr` 注册**，区分 Object / Class 仅靠类型 facade 与 SubType，不需要为每个 T 重复展开模板。

```cpp
// ============================================================================
// 文件: AngelscriptRuntime/Binds/Bind_TSoftObjectPtr.cpp
// 节选: 模板声明阶段——两个 ValueClass 共用 FSoftObjectPtr
// ============================================================================
auto TSoftObjectPtr_ = FAngelscriptBinds::ValueClass<FSoftObjectPtr>(
    "TSoftObjectPtr<class T>", Flags);
// ...
auto TSoftClassPtr_ = FAngelscriptBinds::ValueClass<FSoftObjectPtr>(   // ★ 同 FSoftObjectPtr
    "TSoftClassPtr<class T>", Flags);
```

这种"两个 AS 类型映射到同一个 C++ 类型"的写法在 AS 引擎中是合法的——因为模板实例化的边界由模板名 + SubType 决定，C++ 端只需要内存布局与析构语义一致即可。

### 2.2 `FSoftObjectPath` 在 AS 端的暴露

`FSoftObjectPath` 是 UE 反射结构（`USTRUCT()`），UHT 自动生成 AS 类型注册；`Bind_SoftObjectPath.cpp` 为其补充方法绑定：

```cpp
// ============================================================================
// 文件: AngelscriptRuntime/Binds/Bind_SoftObjectPath.cpp
// 节选: FSoftObjectPath 主要方法
// ============================================================================
SoftObjectPath_.Constructor("void f(const FString& Path)", /* ... */);
SoftObjectPath_.Constructor("void f(const UObject InObject)", /* ... */);
SoftObjectPath_.Method("FString GetLongPackageName() const", /* ... */);
SoftObjectPath_.Method("FString GetAssetName() const",       /* ... */);
SoftObjectPath_.Method("FTopLevelAssetPath GetAssetPath() const", /* ... */);
SoftObjectPath_.Method("bool IsValid() const",  /* ... */);
SoftObjectPath_.Method("bool IsAsset() const",  /* ... */);
SoftObjectPath_.Method("UObject TryLoad() const",     /* 同步加载 */);
SoftObjectPath_.Method("UObject ResolveObject() const", /* 仅查询，不触发加载 */);
```

`TryLoad` 与 `ResolveObject` 的区别：
- `TryLoad` —— 同步加载（阻塞），找不到时返回 `nullptr`；
- `ResolveObject` —— 只查内存，未加载返回 `nullptr`。

`FSoftClassPath` 是 `FSoftObjectPath` 的子类型化变体（USTRUCT 继承 + `MetaClass` 字段），在 AS 端独立绑定，提供 `ResolveClass` / `TryLoadClass` 两个 UClass 专用入口。

### 2.3 `FSoftObjectPtr` vs `FSoftObjectPath`

```text
FSoftObjectPath:   仅是路径字符串容器（FName PackageName + FName AssetName + FString SubPathString）
                   是 USTRUCT 反射类型，可以独立做 UPROPERTY，但用得少
FSoftObjectPtr:    在 FSoftObjectPath 之上加了 mutable WeakObjectPtr<UObject> + Owner
                   "解析过的对象就缓存"，避免每次 Get() 都重新查 FName 表
                   TSoftObjectPtr<T> / TSoftClassPtr<T> 的真正基础类型
```

记忆：**Path 是字符串、Ptr 是字符串 + 缓存的弱句柄**。`Bind_TSoftObjectPtr.cpp` 处理后者；`Bind_SoftObjectPath.cpp` 处理前者。两者都对外可用，但脚本作者绝大多数时候用 `TSoftObjectPtr<T>`。

---

## 三、类型 facade：`FBaseSoftReferenceType` + 两个子类

**源码所在**: `Bind_TSoftObjectPtr.cpp:23-281`。

facade 体系采用"基类聚合公共行为，子类只覆盖差异"的模式——这是本仓库 Bind 文件第一次出现的 facade 继承（`TWeakObjectPtr` / `TSubclassOf` 都是单 facade），原因是 Object 与 Class 共享了 90% 的反射逻辑，仅在 `CreateProperty` 与 `GetClassOfObject` 上分叉。

### 3.1 `FBaseSoftReferenceType`：5 项公共反射

```cpp
// ============================================================================
// 文件: AngelscriptRuntime/Binds/Bind_TSoftObjectPtr.cpp
// 角色: TSoftObjectPtr 与 TSoftClassPtr 共享的反射基类
// ============================================================================
struct FBaseSoftReferenceType : public TAngelscriptCppType<FSoftObjectPtr>
{
    UClass* GetSubTypeClass(const FAngelscriptTypeUsage& Usage) const
    {
        if (Usage.SubTypes.Num() == 0)
            return nullptr;
        return Usage.SubTypes[0].GetClass();
    }

    virtual UClass* GetClassOfObject(const FAngelscriptTypeUsage& Usage) const
    {
        return nullptr;        // ★ 留给子类覆盖
    }
    // ... DescribesCompleteType / CanCreateProperty / SetArgument /
    //     GetReturnValue / GetDebuggerValue 等公共实现 ...
};
```

| 方法 | 实现位置 | 作用 |
|------|---------|------|
| `DescribesCompleteType` | 基类 | `SubTypes.Num() >= 1 && SubTypes[0].IsValid()` |
| `CanCreateProperty` | 基类 | 检查 SubType 已注册或为脚本类（`Type->IsObjectPointer()`） |
| `SetArgument` | 基类 | `Stack.StepCompiledIn<FSoftObjectProperty>(ValuePtr)` 取 UFunction 入参 |
| `GetReturnValue` | 基类 | 从 AS context 读 `FSoftObjectPtr` 返回值 |
| `GetDebuggerValue/Scope/Member` | 基类 | 调试器面板：未加载显示 `{ Pending <path> }`，已加载委托给底层 UObject facade |
| `GetSubTypeClass` | 基类 | 取 `SubTypes[0]` 的 UClass |
| `GetClassOfObject` | 基类返回 nullptr | 子类覆盖：Object 返回 SubType；Class 返回 `UClass::StaticClass()` |
| `CreateProperty` | **子类**实现 | Object → `FSoftObjectProperty`；Class → `FSoftClassProperty` |
| `MatchesProperty` | **子类**实现 | 反向匹配 cast 检查 |
| `GetCppForm` | **子类**实现 | Codegen 输出的 C++ 类型字符串 |

### 3.2 关键基类方法 1：`SetArgument`

`UFunction` 的入参从 AS 端调进来时，需要把 AS 栈上的 `FSoftObjectPtr` 推到 UE Frame 栈：

```cpp
// ============================================================================
// 文件: AngelscriptRuntime/Binds/Bind_TSoftObjectPtr.cpp
// 函数: FBaseSoftReferenceType::SetArgument
// ============================================================================
void SetArgument(/*...*/, struct FFrame& Stack, const FAngelscriptType::FArgData& Data) const
{
    FSoftObjectPtr* ValuePtr = (FSoftObjectPtr*)Data.StackPtr;
    new(ValuePtr) FSoftObjectPtr();                       // ★ 默认构造一个空槽

    if (Usage.bIsReference)
    {
        FSoftObjectPtr& ObjRef = Stack.StepCompiledInRef<
            FSoftObjectProperty, FSoftObjectPtr>(ValuePtr); // ★ 引用模式
        Context->SetArgAddress(ArgumentIndex, &ObjRef);
    }
    else
    {
        Stack.StepCompiledIn<FSoftObjectProperty>(ValuePtr); // ★ 值模式
        Context->SetArgObject(ArgumentIndex, ValuePtr);
    }
}
```

注意一个细节：**即使是 `TSoftClassPtr<T>` 入参，调用的也是 `FSoftObjectProperty` 的 `StepCompiledIn`**——这是因为 `FSoftClassProperty` 继承自 `FSoftObjectProperty`，而 `StepCompiledIn` 模板的实例化只看父类。底层布局相同，序列化路径也相同，没有问题。

### 3.3 关键基类方法 2：`GetDebuggerValue`

调试器面板要回答两个问题："对象加载了吗？""加载了的话长什么样？"。基类把这两步串起来：

```cpp
// ============================================================================
// 文件: AngelscriptRuntime/Binds/Bind_TSoftObjectPtr.cpp
// 函数: FBaseSoftReferenceType::GetDebuggerValue
// ============================================================================
bool GetDebuggerValue(/*...*/) const override
{
    FSoftObjectPtr& SoftPtr = Usage.ResolvePrimitive<FSoftObjectPtr>(Address);

    UObject* Object = SoftPtr.Get();
    if (Object == nullptr)
    {
        Value.Value = FString::Printf(TEXT("{ Pending %s }"), *SoftPtr.ToString());
        // ★ 未加载分支：直接显示路径
        return true;
    }
    // ★ 已加载分支：委托给目标 UClass 的 facade（如 UStaticMeshType）展开成员
    FAngelscriptTypeUsage ObjectUsage(FAngelscriptType::GetByClass(GetClassOfObject(Usage)));
    if (ObjectUsage.IsValid())
    {
        // ... 把 Object 当作普通 UObject 输出 ...
    }
    return true;
}
```

效果：调试器看 `MeshAsset` 字段，未加载时显示 `{ Pending /Game/Meshes/Sword.Sword }`；加载后显示完整 mesh 对象树。

### 3.4 子类 1：`FSoftObjectPtrType`

```cpp
// ============================================================================
// 文件: AngelscriptRuntime/Binds/Bind_TSoftObjectPtr.cpp
// 角色: 物体软引用 facade
// ============================================================================
struct FSoftObjectPtrType : public FBaseSoftReferenceType
{
    FString GetAngelscriptTypeName() const override { return TEXT("TSoftObjectPtr"); }

    virtual UClass* GetClassOfObject(const FAngelscriptTypeUsage& Usage) const
    {
        return GetSubTypeClass(Usage);    // ★ T 即对象类
    }

    FProperty* CreateProperty(/*...*/) const override
    {
        auto* ObjectProp = new FSoftObjectProperty(/*...*/);
        ObjectProp->PropertyClass = GetClassOfObject(Usage);  // ★ 编辑器据此过滤
        return ObjectProp;
    }
    // ... MatchesProperty / GetCppForm ...
};
```

`PropertyClass` 决定**编辑器面板的 Asset Picker 弹出时只列出此类及派生**——比如 `TSoftObjectPtr<UStaticMesh> Mesh;` 触发的选择器只列 `UStaticMesh` 资源。

### 3.5 子类 2：`FSoftClassPtrType`

```cpp
// ============================================================================
// 文件: AngelscriptRuntime/Binds/Bind_TSoftObjectPtr.cpp
// 角色: 类软引用 facade
// ============================================================================
struct FSoftClassPtrType : public FBaseSoftReferenceType
{
    FString GetAngelscriptTypeName() const override { return TEXT("TSoftClassPtr"); }

    virtual UClass* GetClassOfObject(const FAngelscriptTypeUsage& Usage) const
    {
        return UClass::StaticClass();    // ★ Class 引用的"对象"恒为 UClass
    }

    FProperty* CreateProperty(/*...*/) const override
    {
        auto* ClassProp = new FSoftClassProperty(/*...*/);
        ClassProp->PropertyClass = UClass::StaticClass();    // 永远是 UClass
        ClassProp->MetaClass = GetSubTypeClass(Usage);       // ★ T 是 MetaClass，过滤蓝图类
        return ClassProp;
    }
    // ... MatchesProperty / GetCppForm ...
};
```

差异点：
- `PropertyClass = UClass::StaticClass()`（指向的对象是 UClass 本身）
- `MetaClass = T`（要求加载的 UClass 必须是 T 的子类——这与 `TSubclassOf` 完全相同）

这让编辑器的 ClassPicker 自动过滤为 T 派生的蓝图/原生类。

### 3.6 `GetCppForm` —— Codegen 双层视图

```cpp
// ============================================================================
// 文件: AngelscriptRuntime/Binds/Bind_TSoftObjectPtr.cpp
// 函数: FSoftObjectPtrType::GetCppForm
// ============================================================================
bool GetCppForm(/*...*/) const override
{
    UClass* MetaClass = GetClassOfObject(Usage);
    if (MetaClass != nullptr)
    {
        OutCppForm.CppType = FString::Printf(TEXT("TSoftObjectPtr<%s%s>"),
            MetaClass->GetPrefixCPP(), *MetaClass->GetName());     // 强类型 T 形式
        OutCppForm.CppHeader = FString::Printf(TEXT("#include \"%s\""),
            *FAngelscriptBindDatabase::GetSourceHeader(MetaClass));
    }

    OutCppForm.CppGenericType = TEXT("TSoftObjectPtr<UObject>");   // 反射形式
    OutCppForm.TemplateObjectForm = TEXT("TSoftObjectPtr<UObject>");
    return true;
}
```

`CppType` 给 IDE 用（带正确的 T、自动 `#include`）；`CppGenericType` / `TemplateObjectForm` 给 StaticJIT 与反射代码用（统一为 `TSoftObjectPtr<UObject>`，避免触发 N 次模板实例化）。

---

## 四、阶段 1：模板声明（`Bind_SoftReferences_Declaration`）

**源码所在**: `Bind_TSoftObjectPtr.cpp:283-364`。

```cpp
// ============================================================================
// 文件: AngelscriptRuntime/Binds/Bind_TSoftObjectPtr.cpp
// 节选: 阶段 1——共享的模板与子类型校验
// ============================================================================
AS_FORCE_LINK const FAngelscriptBinds::FBind Bind_SoftReferences_Declaration(
    (int32)FAngelscriptBinds::EOrder::Early, []
{
    FBindFlags Flags;
    Flags.bTemplate = true;
    Flags.TemplateType = "<T>";
    Flags.ExtraFlags = asOBJ_TEMPLATE_SUBTYPE_COVARIANT;    // ★ 协变

    TSharedRef<FSoftObjectPtrType> SoftObjectPtrType = MakeShared<FSoftObjectPtrType>();
    FAngelscriptType::Register(SoftObjectPtrType);

    TSharedRef<FSoftClassPtrType> SoftClassPtrType = MakeShared<FSoftClassPtrType>();
    FAngelscriptType::Register(SoftClassPtrType);

    auto TSoftObjectPtr_ = FAngelscriptBinds::ValueClass<FSoftObjectPtr>(
        "TSoftObjectPtr<class T>", Flags);
    TSoftObjectPtr_.TemplateCallback("bool f(int&in Type, int&out ErrorMessage)", /* T 必须是类类型 */);

    auto TSoftClassPtr_ = FAngelscriptBinds::ValueClass<FSoftObjectPtr>(
        "TSoftClassPtr<class T>", Flags);
    TSoftClassPtr_.TemplateCallback("bool f(int&in Type, int&out ErrorMessage)", /* 同上 */);
    // ...
});
```

关键点：
- **两个 `ValueClass` 都用 `FSoftObjectPtr`**——构造/析构对齐，不同 facade 对应同一内存布局。
- **`asOBJ_TEMPLATE_SUBTYPE_COVARIANT`**：`TSoftObjectPtr<UStaticMesh>` 隐式视作 `TSoftObjectPtr<UObject>`，让"接受 `TSoftObjectPtr<UObject>` 参数"的函数能吃任何具体子类。
- **`TemplateCallback`**：编译期校验 SubType 是 UObject 派生（拒绝 `TSoftObjectPtr<int>` / `TSoftObjectPtr<FVector>` / `TSoftObjectPtr<TSoftObjectPtr<X>>`）；具体测试见 `AngelscriptSyntaxSmartPointerTests.cpp:235-269`。

### 4.1 `RegisterTypeFinder` 反向解析

阶段 1 的最后注册一个 lambda，让 C++ 端导出的 `FSoftObjectProperty` / `FSoftClassProperty` 字段在 AS 端被解析为 `TSoftObjectPtr<T>` / `TSoftClassPtr<T>`：

```cpp
// ============================================================================
// 文件: AngelscriptRuntime/Binds/Bind_TSoftObjectPtr.cpp
// 节选: 反射桥——FProperty -> FAngelscriptTypeUsage
// ============================================================================
FAngelscriptType::RegisterTypeFinder([SoftObjectPtrType, SoftClassPtrType](
    FProperty* Property, FAngelscriptTypeUsage& Usage) -> bool
{
    FSoftObjectProperty* ObjectProperty = CastField<FSoftObjectProperty>(Property);
    if (ObjectProperty == nullptr) return false;

    // ★ FSoftClassProperty 继承自 FSoftObjectProperty，要先 cast 子类
    FSoftClassProperty* ClassProperty = CastField<FSoftClassProperty>(Property);
    if (ClassProperty != nullptr)
    {
        Usage.Type = SoftClassPtrType;
        Usage.SubTypes.Emplace_GetRef().Type =
            FAngelscriptType::GetByClass(ClassProperty->MetaClass);
        return true;
    }

    Usage.Type = SoftObjectPtrType;
    Usage.SubTypes.Emplace_GetRef().Type =
        FAngelscriptType::GetByClass(ObjectProperty->PropertyClass);
    return true;
});
```

注意"先 Class 再 Object"的顺序：`FSoftClassProperty` 是 `FSoftObjectProperty` 的子类，必须先用具体子类 cast，否则会被错误识别为普通 Object 引用。

---

## 五、阶段 2：方法注册（`Bind_SoftReferences`）

**源码所在**: `Bind_TSoftObjectPtr.cpp:437-667`。Late-5 阶段（晚于绝大多数 UObject 类型注册，但早于最末段的全局别名等收尾）。

### 5.1 共享方法集 `BindSoftPtrBaseMethods`

`Bind_TSoftObjectPtr.cpp:373-435` 定义了一个静态函数 `BindSoftPtrBaseMethods`，对 `TSoftObjectPtr<T>` 与 `TSoftClassPtr<T>` 都调用一次。

```cpp
// ============================================================================
// 文件: AngelscriptRuntime/Binds/Bind_TSoftObjectPtr.cpp
// 函数: BindSoftPtrBaseMethods —— Object 与 Class 共享的方法
// ============================================================================
void BindSoftPtrBaseMethods(FAngelscriptBinds& SoftPtr_)
{
    SoftPtr_.Constructor("void f()",                          /* 默认空构造 */);
    SoftPtr_.Constructor("void f(const FSoftObjectPath& Path)", /* 从路径构造 */);
    SoftPtr_.Destructor("void f()",                            /* 析构 */);

    SoftPtr_.Method("FSoftObjectPath ToSoftObjectPath() const", /* */);
    SoftPtr_.Method("FString ToString() const",                  /* */);
    SoftPtr_.Method("FString GetLongPackageName() const",        /* */);
    SoftPtr_.Method("FString GetAssetName() const",              /* */);

    SoftPtr_.Method("bool IsValid() const",   /* 已加载且对象存活 */);
    SoftPtr_.Method("bool IsPending() const", /* 路径有效但未加载 */);
    SoftPtr_.Method("bool IsNull() const",    /* 路径都为空 */);
    SoftPtr_.Method("void Reset()",            /* 清空路径与缓存 */);

    SoftPtr_.Method("TSoftObjectPtr<T>& opAssign(const FSoftObjectPath& Path)", /* */);
}
```

注意 `opAssign(const FSoftObjectPath&)` 的签名里写的是 `TSoftObjectPtr<T>`——**当这段被 `TSoftClassPtr` 复用时，AS 引擎按"当前正在注册的模板名"自动替换为 `TSoftClassPtr<T>`**。这是 AS 引擎的模板系统提供的便利，避免我们写两份。

### 5.2 三种状态查询的精确含义

| 方法 | 含义 | 何时用 |
|------|------|--------|
| `IsValid()` | 路径有效**且**对象已加载到内存 | 真正要访问对象前 |
| `IsPending()` | 路径有效**但**对象未加载（尚需 LoadAsync） | 决定是否触发异步加载 |
| `IsNull()` | 路径本身为空 | 检查"这个引用有没有被设过值" |

```text
                  路径为空    路径有效未加载    路径有效已加载
IsValid:          false       false            true
IsPending:        false       true             false
IsNull:           true        false            false
```

三者**互斥且穷尽**——任何 `FSoftObjectPtr` 必处于其中一种状态。

### 5.3 `TSoftObjectPtr<T>` 专属方法

```cpp
// ============================================================================
// 文件: AngelscriptRuntime/Binds/Bind_TSoftObjectPtr.cpp
// 节选: TSoftObjectPtr<T> 专属（不含 BindSoftPtrBaseMethods 共享部分）
// ============================================================================
TSoftObjectPtr_.ImplicitConstructor("void f(T handle_only Object)", /* 从裸指针构造 */);
TSoftObjectPtr_.Constructor("void f(const TSoftObjectPtr<T>& Other)", /* 拷贝构造 */);

TSoftObjectPtr_.Method("TSoftObjectPtr<T>& opAssign(T handle_only Object)", /* */);
TSoftObjectPtr_.Method("TSoftObjectPtr<T>& opAssign(const TSoftObjectPtr<T>& Other)", /* */);
TSoftObjectPtr_.Method("bool opEquals(const TSoftObjectPtr<T>& Other) const", /* */);
TSoftObjectPtr_.Method("bool opEquals(T handle_only Object) const", /* */);

TSoftObjectPtr_.Method("T handle_only Get() const", /* 加载过返回对象指针，否则 nullptr */);
TSoftObjectPtr_.Method("void LoadAsync(FOnSoftObjectLoaded OnLoaded) const", /* 异步加载 */);

#if WITH_EDITOR
TSoftObjectPtr_.Method("T handle_only EditorOnlyLoadSynchronous() const", /* 同步加载 */);
#endif
```

### 5.4 `Get()` —— 子类型校验门

```cpp
// ============================================================================
// 文件: AngelscriptRuntime/Binds/Bind_TSoftObjectPtr.cpp
// 函数: TSoftObjectPtr<T>::Get 的 lambda 实现
// ============================================================================
TSoftObjectPtr_.Method("T handle_only Get() const", [](FSoftObjectPtr* Self) -> UObject*
{
    UObject* Object = Self->Get();
    if (Object != nullptr && !Object->IsA(GetSoftPtrSubType()))
        return nullptr;          // ★ 类型校验失败强制返回 null
    return Object;
});
```

`GetSoftPtrSubType` 通过 `FAngelscriptEngine::GetCurrentFunctionObjectType()->GetSubType(0)->GetUserData()` 在运行时取出 T 对应的 `UClass*`——这是**类型擦除模板**的核心套路：底层 `FSoftObjectPtr` 不知道 T 是谁，AS 端通过当前模板上下文动态查询。

校验失败强制返回 `nullptr` 是安全保险——避免脚本作者拿到一个"路径写错的、其他类型的"对象当作 T 用，后续访问字段就崩了。

### 5.5 `TSoftClassPtr<T>` 专属方法

`TSoftClassPtr<T>` 与 `TSoftObjectPtr<T>` 的差异：
- 操作的是 `UClass*` 而不是 `UObject*`
- 多了 `TSubclassOf<T>` 互转入口（构造/赋值/比较）
- `Get()` 返回 `TSubclassOf<T>` 而不是 `T*`

```cpp
// ============================================================================
// 文件: AngelscriptRuntime/Binds/Bind_TSoftObjectPtr.cpp
// 节选: TSoftClassPtr<T> 专属
// ============================================================================
TSoftClassPtr_.Constructor("void f(UClass Object)",                  /* */);
TSoftClassPtr_.Constructor("void f(const TSoftClassPtr<T>& Other)",   /* */);
TSoftClassPtr_.Constructor("void f(const TSubclassOf<T>& Other)",     /* ★ 与 TSubclassOf 互通 */);

TSoftClassPtr_.Method("TSoftClassPtr<T>& opAssign(UClass Object)",    /* + IsChildOf 校验 */);
TSoftClassPtr_.Method("TSoftClassPtr<T>& opAssign(const TSubclassOf<T>& Other)", /* */);
TSoftClassPtr_.Method("bool opEquals(const TSubclassOf<T>& Other) const", /* */);
TSoftClassPtr_.Method("bool opEquals(UClass Object) const",           /* */);

TSoftClassPtr_.Method("TSubclassOf<T> Get() const", /* ★ 返回类型化 TSubclassOf */);
TSoftClassPtr_.Method("void LoadAsync(FOnSoftClassLoaded OnLoaded) const", /* 异步加载 */);
```

### 5.6 `TSoftClassPtr<T>::opAssign(UClass)` —— 显式校验路径

```cpp
// ============================================================================
// 文件: AngelscriptRuntime/Binds/Bind_TSoftObjectPtr.cpp
// 函数: TSoftClassPtr<T>::opAssign(UClass) 校验
// ============================================================================
TSoftClassPtr_.Method("TSoftClassPtr<T>& opAssign(UClass Object)",
    [](FSoftObjectPtr* Self, UClass* NewClass) -> FSoftObjectPtr&
{
    if (NewClass != nullptr && !NewClass->IsChildOf(GetSoftPtrSubType()))
    {
        FAngelscriptEngine::Throw(
            "Provided class is does not inherit from TSoftClassPtr subtype.");
        return *Self;            // ★ 抛异常但返回原值
    }
    *Self = NewClass;
    return *Self;
});
```

与 `TSubclassOf::opAssign` 设计完全一致：失败抛 AS 异常 + 不修改原值；这让脚本端 `try/catch` 模型可以工作，但**未捕获异常会终止当前脚本调用**。

### 5.7 `TSoftClassPtr<T>::Get()` —— 双层校验

```cpp
// ============================================================================
// 文件: AngelscriptRuntime/Binds/Bind_TSoftObjectPtr.cpp
// 函数: TSoftClassPtr<T>::Get 的 lambda 实现
// ============================================================================
TSoftClassPtr_.Method("TSubclassOf<T> Get() const", [](FSoftObjectPtr* Self) -> TSubclassOf<UObject>
{
    UClass* Object = Cast<UClass>(Self->Get());                  // ★ 第一层：是 UClass 吗
    if (Object != nullptr && !Object->IsChildOf(GetSoftPtrSubType()))
        return TSubclassOf<UObject>();                            // ★ 第二层：是 T 派生吗
    return TSubclassOf<UObject>(Object);
});
```

返回 `TSubclassOf<T>` 而非裸 `UClass*` 的好处：脚本端拿到的就是已通过 `IsChildOf` 校验的"安全"句柄，下游 `SpawnActor` / `NewObject` 不需要再校验。

---

## 六、AS 端语法速查与 `.as` 用法

### 6.1 声明与基本访问

```angelscript
// 字段：UPROPERTY 暴露给编辑器
UCLASS()
class AMySword : AActor
{
    UPROPERTY()
    TSoftObjectPtr<UStaticMesh> BladeMesh;

    UPROPERTY()
    TSoftClassPtr<AActor> ProjectileClass;

    UFUNCTION(BlueprintOverride)
    void BeginPlay()
    {
        if (BladeMesh.IsPending())
        {
            // 路径有效但未加载 -> 异步加载
            FOnSoftObjectLoaded OnLoaded;
            OnLoaded.BindUFunction(this, n"OnBladeLoaded");
            BladeMesh.LoadAsync(OnLoaded);
        }
        else if (BladeMesh.IsValid())
        {
            ApplyMesh(BladeMesh.Get());
        }
        // 第三种状态 IsNull() 表示什么都没设——不需要处理
    }

    UFUNCTION()
    void OnBladeLoaded(UObject Loaded)
    {
        UStaticMesh Mesh = Cast<UStaticMesh>(Loaded);
        if (Mesh != nullptr)
            ApplyMesh(Mesh);
    }
}
```

### 6.2 从路径字面量构造

```angelscript
// 测试用例 AngelscriptSoftReferenceFunctionLibraryTests.cpp:243-273 中的真实写法
TSoftObjectPtr<UTexture2D> TexRef =
    TSoftObjectPtr<UTexture2D>(FSoftObjectPath("/Engine/EngineResources/DefaultTexture.DefaultTexture"));
TexRef.LoadAsync(Delegate);

TSoftClassPtr<AActor> ClassRef =
    TSoftClassPtr<AActor>(FSoftObjectPath("/Script/Engine.Actor"));
ClassRef.LoadAsync(ClassDelegate);
```

**注意**：从 `FSoftObjectPath` 的字符串构造**只有路径**——没有对象指针，所以构造后 `IsPending=true` 而非 `IsValid=true`。要想立即可访问，需 `LoadAsync` 或 `EditorOnlyLoadSynchronous`（仅 Editor）。

### 6.3 与 `TSubclassOf` 互通

```angelscript
// 持久数据库中保存的是软引用
TSoftClassPtr<AActor> StoredProjectile = LoadFromDB();

// 加载完成后转 TSubclassOf 用 SpawnActor
StoredProjectile.LoadAsync(FOnSoftClassLoaded(this, n"OnLoaded"));

UFUNCTION()
void OnLoaded(UClass Loaded)
{
    TSubclassOf<AActor> Cls(Loaded);          // 这里隐式构造 + IsChildOf 校验
    if (Cls.IsValid())
        SpawnActor(Cls, GetActorLocation());
}

// 反向：把已经持有的 TSubclassOf 写入软引用
TSubclassOf<AActor> Hot = AMyProjectile::StaticClass();
TSoftClassPtr<AActor> Soft;
Soft = Hot;        // 走 TSoftClassPtr<T>& opAssign(const TSubclassOf<T>&)
```

### 6.4 用法约束（编译期）

```angelscript
TSoftObjectPtr Soft;                       // ✗ 缺模板参数——编译失败
TSoftObjectPtr<int> X;                     // ✗ T 不是类——编译失败
TSoftObjectPtr<FVector> Y;                 // ✗ T 是 USTRUCT 不是 UCLASS——编译失败
TSoftObjectPtr<NonExistent> Z;             // ✗ T 未注册——编译失败
TSoftObjectPtr<TSoftObjectPtr<UTexture>> W; // ✗ 不能嵌套软指针——编译失败
TArray<TSoftObjectPtr<UTexture>> Arr;       // ✓ 可以放进容器（与 TWeakObjectPtr 不同）
```

注意最后一行——**`TSoftObjectPtr` 可以作为 TArray/TMap 的元素，但 `TWeakObjectPtr` 不行**。原因是 `FSoftObjectPtr` 是 USTRUCT 反射类型，序列化与构造析构都符合容器要求；而 `TWeakObjectPtr` 在 AS 侧的注册只支持一阶值类型。

---

## 七、UPROPERTY 资源选择器与反射 fallback

### 7.1 编辑器面板的 Asset Picker

```angelscript
UPROPERTY()
TSoftObjectPtr<UStaticMesh> Mesh;
```

经过 `FSoftObjectPtrType::CreateProperty` 创建 `FSoftObjectProperty { PropertyClass = UStaticMesh }`，UE 编辑器自动给这个字段挂上 SAssetPicker：
- 类型过滤为 `UStaticMesh`
- 仅列出 `.uasset` 文件
- 选择时只写路径，不加载
- 配合 `meta = (AllowedClasses="...")` 还可以进一步精炼（但 AS 端目前无该 metadata 暴露）

`TSoftClassPtr<AActor> ActorClass;` 走 `FSoftClassProperty { PropertyClass=UClass, MetaClass=AActor }`，得到 SClassPicker（按 MetaClass 过滤蓝图与原生类）。

### 7.2 序列化

`FSoftObjectProperty` / `FSoftClassProperty` 是 UE 标准反射类型，序列化路径自动走：
- 资源 cooker 阶段：扫描所有 `FSoftObjectPath` 字段，把指向的资源拉进打包依赖图
- 编辑器保存：仅保存路径字符串
- 运行时载入：仅解析路径，不加载对象

这与脚本端的 `IsValid` / `IsPending` / `IsNull` 三态完全闭环。

### 7.3 反射 fallback 显式跳过

`BlueprintCallableReflectiveFallback.cpp:196-208` 在判断"哪些 UFunction 参数能走反射 fallback 调用"时，明确把软指针属性排除：

```cpp
// ============================================================================
// 文件: AngelscriptRuntime/Binds/BlueprintCallableReflectiveFallback.cpp
// 角色: 反射 fallback 的属性类型黑名单
// ============================================================================
if (CastField<FArrayProperty>(Property) != nullptr
    || CastField<FMapProperty>(Property) != nullptr
    // ...
    || CastField<FSoftObjectProperty>(Property) != nullptr   // ★ 软对象引用
    || CastField<FSoftClassProperty>(Property) != nullptr    // ★ 软类引用
    || CastField<FFieldPathProperty>(Property) != nullptr
    || CastField<FOptionalProperty>(Property) != nullptr)
{
    return false;            // 这些类型不走反射 fallback
}
```

这意味着：**带 `TSoftObjectPtr` / `TSoftClassPtr` 入参的 BlueprintCallable 函数无法通过反射 fallback 直接被 AS 调用**——必须有手动 `Bind_*.cpp` 绑定，或 UHT 生成的直接绑定。原因是反射 fallback 走通用 `FProperty::CopySingleValue`，无法自动桥接 `FSoftObjectPtr` 的"路径 + 缓存指针"语义。

---

## 八、异步加载链路：`LoadAsync` 实现细节

**源码所在**: `Bind_TSoftObjectPtr.cpp:483-535`（Object 版本）、`:629-666`（Class 版本）。

`LoadAsync` 是软引用最重要的运行时入口——把延迟加载具象化为一个"路径 → `LoadPackageAsync` → 回调脚本"的链路。

### 8.1 Object 版本完整链路

```cpp
// ============================================================================
// 文件: AngelscriptRuntime/Binds/Bind_TSoftObjectPtr.cpp
// 函数: TSoftObjectPtr<T>::LoadAsync 的 lambda 实现（节选关键分支）
// ============================================================================
TSoftObjectPtr_.Method("void LoadAsync(FOnSoftObjectLoaded OnLoaded) const",
    [](FSoftObjectPtr* Self, FOnSoftObjectLoaded OnLoaded)
{
    UClass* ObjClass = GetSoftPtrSubType();        // T 对应的 UClass*

    // ★ 拒绝 Actor / Component 的软加载——这两类必须走 LevelStreaming
    if (ObjClass->IsChildOf(AActor::StaticClass()))
    {
        FAngelscriptEngine::Throw("Actor soft references cannot be loaded, "
            "stream the level in instead.");
        return;
    }
    if (ObjClass->IsChildOf(UActorComponent::StaticClass())) { /* 同上 */ }

    // ★ 已加载：立即回调（同步）
    if (UObject* Object = Self->Get())
    {
        if (!Object->IsA(ObjClass)) Object = nullptr;
        OnLoaded.ExecuteIfBound(Object);
        return;
    }

    // ★ 未加载：发起异步加载
    TWeakObjectPtr<UClass> WeakClass = ObjClass;       // 防止脚本类被卸载导致崩溃
    FSoftObjectPtr ObjectCopy(*Self);                  // 闭包按值捕获
    const FString PackageName = FPackageName::ObjectPathToPackageName(ObjectCopy.ToString());

    if (PackageName.IsEmpty() || (FindPackage(nullptr, *PackageName) == nullptr
        && !FPackageName::DoesPackageExist(PackageName)))
    {
        OnLoaded.ExecuteIfBound(nullptr);     // ★ 找不到包：立即以 null 回调
        return;
    }

    FLoadPackageAsyncDelegate Delegate;
    Delegate.BindLambda([ObjectCopy, OnLoaded, WeakClass](
        const FName& PkgName, UPackage* LoadedPkg, EAsyncLoadingResult::Type Result)
    {
        UObject* Object = ObjectCopy.Get();
        if (Object != nullptr)
        {
            if (!WeakClass.IsValid() || !Object->IsA(WeakClass.Get()))
                Object = nullptr;            // ★ 加载成功但类型不对：回调 null
        }
        OnLoaded.ExecuteIfBound(Object);
    });

    LoadPackageAsync(*PackageName, Delegate, 100);     // 优先级 100
});
```

### 8.2 链路决策图

```text
LoadAsync(Delegate)
    |
    +-- ObjClass IsA<AActor> / UActorComponent ?
    |         |
    |         +-- 是: Throw "use LevelStreaming"  --> 终止
    |
    +-- Self->Get() != nullptr ?
    |         |
    |         +-- 是: 立即调 Delegate（同步）  --> 终止
    |
    +-- 解析 PackageName 失败 / Package 不存在 ?
    |         |
    |         +-- 是: 立即调 Delegate(nullptr)（同步）  --> 终止
    |
    +-- LoadPackageAsync(PackageName, lambda, prio=100)
                |
                v
           [异步线程]
                |
                v
           lambda 回调:
              Object = ObjectCopy.Get()
              if !WeakClass.IsValid() || !Object IsA WeakClass:
                  Object = nullptr
              Delegate.ExecuteIfBound(Object)
```

### 8.3 三个关键安全点

1. **Actor / Component 拒绝**：UE 的 Actor 与 Component 都依附于 Level，独立加载会破坏 World 的 Actor 注册表。引擎**强制**这类引用走 `ULevelStreaming` 而非 `LoadPackageAsync`。
2. **`WeakClass` 防卸载**：异步加载期间，脚本可能被热重载，原 `ObjClass` 已失效。捕获 `TWeakObjectPtr<UClass>` 让回调 lambda 在执行时再校验一次。
3. **重复 `IsA` 校验**：路径字符串可能指向**其他类型**（被人手改过），即使加载成功也要再用 `IsA` 排除。

### 8.4 Class 版本差异

```cpp
// ============================================================================
// 文件: AngelscriptRuntime/Binds/Bind_TSoftObjectPtr.cpp
// 节选: TSoftClassPtr<T>::LoadAsync 的关键差异
// ============================================================================
TSoftClassPtr_.Method("void LoadAsync(FOnSoftClassLoaded OnLoaded) const", /*...*/)
{
    // ★ 不拒绝任何类型——UClass 加载没有 Actor/Component 限制

    if (UClass* Object = Cast<UClass>(Self->Get()))      // 要 Cast<UClass>
    {
        if (!Object->IsChildOf(ObjClass)) Object = nullptr;  // IsChildOf 而非 IsA
        OnLoaded.ExecuteIfBound(Object);
        return;
    }
    // ... LoadPackageAsync 链路同 Object 版本 ...
}
```

差异点：
- 不拒绝 Actor / Component 类**——加载 UClass 不等于实例化 Actor**，是合法操作（蓝图 ClassPicker 也常这么用）
- `Cast<UClass>(Self->Get())` 因为 `FSoftObjectPtr::Get()` 返回 `UObject*`
- 用 `IsChildOf` 而非 `IsA`——类继承校验

### 8.5 委托声明：`FOnSoftObjectLoaded` / `FOnSoftClassLoaded`

```cpp
// ============================================================================
// 文件: AngelscriptRuntime/FunctionLibraries/SoftReferenceStatics.h
// 角色: 软引用异步加载用的两个动态委托
// ============================================================================
DECLARE_DYNAMIC_DELEGATE_OneParam(FOnSoftObjectLoaded, UObject*, LoadedObject);
DECLARE_DYNAMIC_DELEGATE_OneParam(FOnSoftClassLoaded, UClass*, LoadedClass);
```

注意是 **`DYNAMIC` 委托**——必须用 `BindUFunction(this, n"FuncName")` 绑定（脚本侧用 `n"..."` 字面量），不能直接绑 lambda。这与蓝图的 `Async Asset Loading` 节点 API 一致，保证 AS / BP 互通。

`USoftReferenceStatics` 是个空 UCLASS，仅作为 GENERATED_BODY 寄主——把这两个动态委托类型导出到 UHT/AS 反射系统。

### 8.6 Editor-only 同步加载

```cpp
// ============================================================================
// 文件: AngelscriptRuntime/Binds/Bind_TSoftObjectPtr.cpp
// 节选: WITH_EDITOR 下的同步加载
// ============================================================================
#if WITH_EDITOR
TSoftObjectPtr_.Method("T handle_only EditorOnlyLoadSynchronous() const",
    [](FSoftObjectPtr* Self) -> UObject*
{
    if (ObjClass->IsChildOf(AActor::StaticClass())) {
        FAngelscriptEngine::Throw(/* */); return nullptr;
    }
    // ...
    return Self->LoadSynchronous();      // ★ 阻塞主线程加载
});
SCRIPT_BIND_DOCUMENTATION("Synchronously load the asset references by the soft pointer. "
    "Only available in editor, because it would cause hitches during gameplay.");
FAngelscriptBinds::SetPreviousBindIsEditorOnly(true);
#endif
```

`SetPreviousBindIsEditorOnly(true)` 是关键——告诉 AS 引擎此方法仅在 `WITH_EDITOR` 编译场景下注册。脚本作者在 Game build 中调用就会得到"未知方法"编译错误，避免在打包后游戏中误用。

---

## 九、AssetManager / Streamable 集成

`Bind_UAssetManager.cpp` 提供另一条延迟加载路径：通过 `Primary Asset` 系统按"分类 + ID"管理资源批量加载。

### 9.1 `FPrimaryAssetType` / `FPrimaryAssetId`

```cpp
// ============================================================================
// 文件: AngelscriptRuntime/Binds/Bind_UAssetManager.cpp
// 节选: PrimaryAsset 标识符
// ============================================================================
FPrimaryAssetType_.Constructor("void f(FName InName)", /* */);
FPrimaryAssetType_.Method("FName GetName() const", /* */);
FPrimaryAssetType_.Method("bool IsValid() const", /* */);

FPrimaryAssetId_.Constructor("void f(const FString& InString)", /* "Type:Name" 格式 */);
FPrimaryAssetId_.Method("bool IsValid() const", /* */);
```

`FPrimaryAssetId` 字符串形式 `"Map:OpeningScene"`——`Map` 是 Type，`OpeningScene` 是具体资产 ID。通过 `UAssetManager` 配置（`DefaultGame.ini` 的 `[/Script/Engine.AssetManagerSettings]` 段）映射到 PrimaryAssetType 与目录扫描规则。

### 9.2 `UAssetManager` AS 端入口

```cpp
// ============================================================================
// 文件: AngelscriptRuntime/Binds/Bind_UAssetManager.cpp
// 节选: AssetManager 关键方法
// ============================================================================
UAssetManager_.Method("FPrimaryAssetId GetPrimaryAssetIdForPath(const FSoftObjectPath& ObjectPath) const",
    /* 路径 → ID */);
UAssetManager_.Method("FSoftObjectPath GetPrimaryAssetPath(const FPrimaryAssetId& PrimaryAssetId) const",
    /* ID → 路径 */);

UAssetManager_.Method("void LoadPrimaryAsset(const FPrimaryAssetId& AssetToLoad, "
    "const TArray<FName>& LoadBundles, int32 Priority, /* 回调三件套 */)",
    /* 异步加载 + 完成/取消回调 */);
UAssetManager_.Method("int UnloadPrimaryAsset(const FPrimaryAssetId& AssetToUnload)",
    /* 卸载 */);
```

### 9.3 两条加载路径的对比

| 入口 | 用途 | 颗粒度 | 回调 |
|------|------|--------|------|
| `TSoftObjectPtr.LoadAsync` | 单个资源 | 细 | `FOnSoftObjectLoaded` |
| `UAssetManager.LoadPrimaryAsset` | 一批 PrimaryAsset + Bundle | 粗 | UFunction (`OptionalFinishedCallbackFunctionName`) |
| `UAssetManager.LoadPrimaryAssets` | 多 ID + 一组 Bundle | 最粗 | 同上 |

游戏开发的典型分层：
- 装备/法术等"独立资源"用 `TSoftObjectPtr` 直接加载；
- 关卡数据、剧情资产、整套 UI 主题用 `UAssetManager` + Bundle 批量调度。

### 9.4 `UAssetManager` 的 lambda 适配层

`Bind_UAssetManager.cpp:49-81` 定义了一个 `AssetManager_LoadPrimaryAssets` 辅助函数，把 AS 风格的"对象 + 函数名"回调转成 `FStreamableDelegate`：

```cpp
// ============================================================================
// 文件: AngelscriptRuntime/Binds/Bind_UAssetManager.cpp
// 函数: AssetManager_LoadPrimaryAssets —— AS 友好的回调适配
// ============================================================================
FStreamableDelegate CompleteDelegate{};
if (OptionalCallbackObject != nullptr && !OptionalFinishedCallbackFunctionName.IsNone())
{
    CompleteDelegate.BindUFunction(OptionalCallbackObject, OptionalFinishedCallbackFunctionName);
}

TSharedPtr<FStreamableHandle> Handle = AssetManager->LoadPrimaryAssets(
    AssetsToLoad, LoadBundles, CompleteDelegate, Priority);

if (Handle.IsValid() && OptionalCallbackObject != nullptr && !OptionalCanceledCallbackName.IsNone())
{
    FStreamableDelegate CancelDelegate;
    CancelDelegate.BindUFunction(OptionalCallbackObject, OptionalCanceledCallbackName);
    Handle->BindCancelDelegate(CancelDelegate);
}
```

这让脚本写法仍然是"对象 + UFunction 名"模型，与 `BindUFunction` 一致。`FStreamableManager` 本身在 AS 端不直接暴露——脚本作者只见 AssetManager 入口。

---

## 十、Cooked Build 依赖图

软引用对 cooker 是**透明可见**的——cooker 扫描所有 `FSoftObjectPath` 字段（包括 AS 类生成的 UProperty），把指向的资源拉进打包依赖图。这是软引用相对纯字符串路径的关键优势：

```text
打包阶段:
  AS 类 AMySword
    ├── UPROPERTY TSoftObjectPtr<UStaticMesh> BladeMesh = "/Game/Meshes/Sword.Sword"
    │       │
    │       ▼
    │   FSoftObjectProperty (PropertyClass=UStaticMesh)
    │       │
    │       ▼  cooker 序列化时扫描
    │   AssetRegistry → "/Game/Meshes/Sword" 包加入打包队列
    │
    └── UPROPERTY TSoftClassPtr<AActor> ProjectileClass = "/Game/Blueprints/Bullet.Bullet_C"
            │
            ▼
        FSoftClassProperty (MetaClass=AActor)
            │
            ▼  cooker 序列化时扫描
        AssetRegistry → "/Game/Blueprints/Bullet" 包加入打包队列

运行时 game build:
    BladeMesh.IsPending() == true 但 Package 已 cooked，立即可加载
    BladeMesh.LoadAsync() 不需要联网或下载
```

这与 BP 的 `TSoftObjectPtr` 行为完全一致——AS 类的反射 schema 走 UE 标准路径，`FSoftObjectProperty` / `FSoftClassProperty` 是 cooker 与 AssetRegistry 直接识别的类型。

---

## 十一、限制与避坑

### 11.1 同步 `Get()` 不会触发加载

```angelscript
// ✗ 错误：以为 Get() 会自动加载
UStaticMesh Mesh = MyMeshRef.Get();        // 未加载时返回 nullptr！
Mesh.GetBoundingBox();                     // 空指针解引用 -> 崩溃

// ✓ 正确：先 LoadAsync 或先检查
if (MyMeshRef.IsValid())
{
    UStaticMesh Mesh = MyMeshRef.Get();
    Mesh.GetBoundingBox();
}
else if (MyMeshRef.IsPending())
{
    // 触发加载，回调里再用
    FOnSoftObjectLoaded OnLoaded;
    OnLoaded.BindUFunction(this, n"OnMeshLoaded");
    MyMeshRef.LoadAsync(OnLoaded);
}
```

### 11.2 Actor / Component 不能 `LoadAsync`

```angelscript
TSoftObjectPtr<AActor> ActorRef;
ActorRef.LoadAsync(Delegate);       // ✗ 抛 AS 异常: "use LevelStreaming"
```

引擎强制要求 Actor / Component 走 `ULevelStreaming` 或 `UWorld::LoadStreamLevel`——这是因为 Actor 必须依附 Level 才能正确注册到 World，独立加载会绕过这个流程。脚本端的拦截逻辑见 `Bind_TSoftObjectPtr.cpp:489-499`。

### 11.3 `TSoftClassPtr` 没有同步加载入口

注意阶段 2 的 Class 版本里**没有 `EditorOnlyLoadSynchronous`**——只有 Object 版本提供。要同步加载类，需要走 `FSoftObjectPath::TryLoad()` + `Cast<UClass>` 或 `FSoftClassPath::TryLoadClass()`：

```angelscript
TSoftClassPtr<AActor> ClassRef;
// ✗ ClassRef.EditorOnlyLoadSynchronous();   // 不存在

// ✓ 走 FSoftObjectPath
UObject Loaded = ClassRef.ToSoftObjectPath().TryLoad();
UClass Cls = Cast<UClass>(Loaded);
```

### 11.4 类型擦除导致跨脚本类型不安全

```angelscript
TSoftObjectPtr<UStaticMesh> Soft1 = MeshRef;
TSoftObjectPtr<UTexture> Soft2;
Soft2 = Soft1;                  // ✗ 编译失败——AS 模板系统拒绝不同 T 间互转
```

但是注意——**底层 `FSoftObjectPtr` 是相同的内存布局**，理论上能 reinterpret 复制。AS 模板系统的 `TemplateCallback` 把这种风险拦在编译期。**不要试图通过 C++ 端绕过校验**。

### 11.5 反射 fallback 不支持软指针入参

如 §7.3 所述，带 `TSoftObjectPtr` / `TSoftClassPtr` 入参的 BlueprintCallable 函数无法直接通过反射 fallback 调用——必须有手动 `Bind_*.cpp` 绑定。如果调用一个没有手动绑定且带软指针入参的 BP 函数，AS 编译期会报"无可用绑定"。

### 11.6 `LoadAsync` 回调可能立即触发

```angelscript
// 路径已加载或不存在的情况下，回调会在 LoadAsync 调用栈内同步执行
TSoftObjectPtr<UMaterial> M = AlreadyLoadedMaterial;
M.LoadAsync(Delegate);      // Delegate 立即调用，不在下一帧
```

不要假设回调一定异步——参见 `Bind_TSoftObjectPtr.cpp:503-519` 的两个早返回分支（"已加载"与"包不存在"都立即同步回调）。脚本作者要写"幂等回调"，不能依赖延迟。

### 11.7 `IsPending` 不区分"路径错"与"未加载"

`IsPending=true` 包含两种情况：
- 路径正确，但对象未加载
- 路径写错（指向不存在的资源）

这两种情况只有调 `LoadAsync` 后才能区分（前者回调里拿到对象、后者回调里拿到 `nullptr`）。开发期建议先用 `FPackageName::DoesPackageExist` 验证。

---

## 附录 A：API 速查

### A.1 `TSoftObjectPtr<T>` 完整方法

| 类别 | 方法 | 描述 |
|------|------|------|
| 构造 | `TSoftObjectPtr<T>()` | 默认空 |
| 构造 | `TSoftObjectPtr<T>(const FSoftObjectPath&)` | 从路径构造（IsPending） |
| 构造 | `TSoftObjectPtr<T>(T)` 隐式 | 从对象构造（IsValid） |
| 构造 | `TSoftObjectPtr<T>(const TSoftObjectPtr<T>&)` | 拷贝 |
| 赋值 | `opAssign(const FSoftObjectPath&)` | 仅设路径 |
| 赋值 | `opAssign(T handle_only)` | 设对象（路径自动派生） |
| 赋值 | `opAssign(const TSoftObjectPtr<T>&)` | 拷贝赋值 |
| 比较 | `opEquals(const TSoftObjectPtr<T>&)` | 路径比较 |
| 比较 | `opEquals(T handle_only)` | 与裸对象比较 |
| 状态 | `bool IsValid() const` | 已加载且对象存活 |
| 状态 | `bool IsPending() const` | 路径有效但未加载 |
| 状态 | `bool IsNull() const` | 路径为空 |
| 访问 | `T handle_only Get() const` | 取对象（含 IsA 校验） |
| 路径 | `FString ToString() const` | 路径字符串 |
| 路径 | `FSoftObjectPath ToSoftObjectPath() const` | 路径结构 |
| 路径 | `FString GetLongPackageName() const` | "/Game/..." |
| 路径 | `FString GetAssetName() const` | "MyMesh" |
| 操作 | `void Reset()` | 清空 |
| 加载 | `void LoadAsync(FOnSoftObjectLoaded)` | 异步加载 |
| 加载 | `T handle_only EditorOnlyLoadSynchronous()` | **仅 Editor**——同步加载 |

### A.2 `TSoftClassPtr<T>` 完整方法

| 类别 | 方法 | 描述 |
|------|------|------|
| 构造 | `TSoftClassPtr<T>()` | 默认空 |
| 构造 | `TSoftClassPtr<T>(const FSoftObjectPath&)` | 从路径 |
| 构造 | `TSoftClassPtr<T>(UClass)` | 从 UClass |
| 构造 | `TSoftClassPtr<T>(const TSoftClassPtr<T>&)` | 拷贝 |
| 构造 | `TSoftClassPtr<T>(const TSubclassOf<T>&)` | **从 TSubclassOf** |
| 赋值 | `opAssign(UClass)` | + IsChildOf 校验 |
| 赋值 | `opAssign(const TSubclassOf<T>&)` | 与硬引用互通 |
| 赋值 | `opAssign(const TSoftClassPtr<T>&)` | 拷贝赋值 |
| 比较 | `opEquals(const TSoftClassPtr<T>&)` | |
| 比较 | `opEquals(const TSubclassOf<T>&)` | |
| 比较 | `opEquals(UClass)` | |
| 状态 | `IsValid` / `IsPending` / `IsNull` | 同 Object 版 |
| 访问 | **`TSubclassOf<T> Get() const`** | 返回类型化句柄（不是裸 UClass） |
| 路径 | `ToString` / `ToSoftObjectPath` / `GetLongPackageName` / `GetAssetName` | 同 Object 版 |
| 操作 | `Reset` | |
| 加载 | `void LoadAsync(FOnSoftClassLoaded)` | 不限制 Actor/Component |
| 加载 | **没有 EditorOnlyLoadSynchronous** | 同步走 `FSoftObjectPath::TryLoad` |

### A.3 三种状态对照

```text
                创建空      赋路径未加载   赋对象/已加载
IsValid:        false       false          true
IsPending:      false       true           false
IsNull:         true        false          false
```

### A.4 文件结构地图

```text
Bind_TSoftObjectPtr.cpp (~668 lines)
  ├── struct FBaseSoftReferenceType                  L23-170   公共反射基类
  ├── struct FSoftObjectPtrType : FBase…             L172-225  Object facade
  ├── struct FSoftClassPtrType  : FBase…             L227-281  Class facade
  ├── Bind_SoftReferences_Declaration (Early)        L283-364  阶段 1: 模板 + RegisterTypeFinder
  ├── static UClass* GetSoftPtrSubType()             L366-371  运行时取 SubType
  ├── void BindSoftPtrBaseMethods(...)               L373-435  共享方法集
  └── Bind_SoftReferences (Late-5)                   L437-667  阶段 2: 双方法体绑定
        ├── TSoftObjectPtr<T>: BindBase + ImplCtor + opAssign + Get + LoadAsync
        │     + WITH_EDITOR EditorOnlyLoadSynchronous
        └── TSoftClassPtr<T>:  BindBase + Ctor(UClass/TSubclassOf) + opAssign + Get + LoadAsync
```

---

## 附录 B：避坑清单与调试技巧

### B.1 避坑清单

1. **`Get()` 返回 nullptr ≠ 路径错** —— 也可能是路径正确但未加载；用 `IsPending` 判断。
2. **`LoadAsync` 可能同步触发回调** —— "已加载"与"包不存在"两种情况立即调用 Delegate。
3. **`TSoftObjectPtr<AActor>` 不能 LoadAsync** —— 走 `LoadStreamLevel`。
4. **`TSoftClassPtr<T>::Get()` 返回 `TSubclassOf<T>` 不是 `UClass`** —— 与 Object 版接口不同。
5. **`EditorOnlyLoadSynchronous` 只在 Editor 编译时存在** —— 打包脚本会编译失败。
6. **反射 fallback 跳过软指针属性** —— 此类参数的 BP 函数必须手动绑定。
7. **`FOnSoftObjectLoaded` 是 DYNAMIC 委托** —— 必须 `BindUFunction(this, n"FuncName")`，不能直接 lambda。
8. **同一 `FSoftObjectPath` 多次 LoadAsync 会触发多次 LoadPackageAsync** —— 引擎内部去重，但每个 Delegate 都会被调用一次。
9. **`opAssign(UClass)` 失败抛 AS 异常但不修改原值** —— 与 `TSubclassOf` 一致；脚本要 try/catch 或前置 `IsChildOf` 检查。
10. **`TSoftObjectPtr` 可作为 TArray/TMap 元素**，但容器中元素的 `LoadAsync` 仍各自独立——`TArray<TSoftObjectPtr<T>>` 不等于"批量加载列表"。

### B.2 调试技巧

**调试器里查看软引用**：未加载时面板显示 `{ Pending /Game/Meshes/Sword.Sword }`；加载后显示完整 mesh 对象树（自动委托给 UStaticMesh 的 facade）。这个行为由 `FBaseSoftReferenceType::GetDebuggerValue` 实现（§3.3）。

**追踪 LoadAsync 调用**：在 `Bind_TSoftObjectPtr.cpp:483` 的 lambda 入口加临时 `UE_LOG`，可以看到每次脚本触发软加载的路径与回调对象。注意 lambda 捕获了 `OnLoaded` 副本，多次回调会持有多份。

**验证 cooker 是否捕获到引用**：`Saved/Cooked/<Platform>/.../<Asset>.uasset` 的依赖图（用 `AssetRegistry` 查）中若不含目标资源，说明软引用字段未被正确序列化——常见原因是 `UPROPERTY()` 标记缺失或 cooker 配置（`CookSettings`）排除了相关目录。

**异步加载行为契约的回归测试**：见 `AngelscriptSoftReferenceFunctionLibraryTests.cpp:194-425` 的 `AsyncDelegates` 测试，验证四个场景（Object 成功 / Object 失败 / Class 成功 / Class 失败）的回调次数、参数 null 性、类型匹配。

**编译期约束验证**：`AngelscriptSyntaxSmartPointerTests.cpp:203-269` 的 `TSoftObjectPtr_Negative` 验证 5 类编译失败（无模板 / 非 UObject / 原生类型 / 不存在的类 / 嵌套）。

**AS 端三态调试速查**：

```angelscript
void DiagnoseSoft(TSoftObjectPtr<UStaticMesh> Ref)
{
    Print(f"IsNull   = {Ref.IsNull()}");      // 路径空
    Print(f"IsPending= {Ref.IsPending()}");   // 待加载
    Print(f"IsValid  = {Ref.IsValid()}");     // 可用
    Print(f"Path     = {Ref.ToString()}");
    Print(f"Pkg      = {Ref.GetLongPackageName()}");
    Print(f"Asset    = {Ref.GetAssetName()}");
}
```

---

## 小结

- `TSoftObjectPtr<T>` 与 `TSoftClassPtr<T>` 是 UE 的"**资源路径引用**"，AS 端通过单文件 `Bind_TSoftObjectPtr.cpp` 一并实现，底层共用 C++ 类型 `FSoftObjectPtr`，靠两个并列 facade（`FSoftObjectPtrType` / `FSoftClassPtrType`）继承公共基类 `FBaseSoftReferenceType` 区分 Object / Class。
- 三态语义（`IsValid` / `IsPending` / `IsNull`）穷尽且互斥；`Get()` 仅返回当前已加载对象，未加载时返回 `nullptr` 而非自动加载。
- 异步加载链路：`LoadAsync(FOnSoftObjectLoaded)` → 拒绝 Actor/Component → 已加载立即回调 → 包不存在立即回调 null → `LoadPackageAsync` + lambda 适配 → 二次 `IsA` / `IsChildOf` 校验回调对象。
- 编辑器集成自动化：`FSoftObjectProperty.PropertyClass` 触发 SAssetPicker 按类型过滤；`FSoftClassProperty.MetaClass` 触发 SClassPicker 按 MetaClass 过滤；cooker 自动扫描所有 `FSoftObjectPath` 字段并建立打包依赖图。
- 与兄弟智能指针的边界清晰：硬引用阻 GC；弱引用不阻 GC 但仍是内存内句柄；软引用是路径，对象可不在内存——本文从字节布局到 `LoadPackageAsync` 链路逐层闭环说明。
- 反射 fallback 显式跳过软指针属性，意味着带软引用入参的 BP 函数必须有手动绑定才能被 AS 调用——这是设计选择（路径 + 缓存指针的语义无法被通用 `FProperty::CopySingleValue` 覆盖）。

---

## 修订记录

| 版本 | 日期 | 内容 |
|------|------|------|
| v1.0 | 2026-05-22 | 首版：基于 `Bind_TSoftObjectPtr.cpp`（668 行单文件双类型）、`Bind_SoftObjectPath.cpp`、`Bind_UAssetManager.cpp`、`SoftReferenceStatics.h`、`BlueprintCallableReflectiveFallback.cpp:196-208`、`AngelscriptSyntaxSmartPointerTests.cpp:203-269` 与 `AngelscriptSoftReferenceFunctionLibraryTests.cpp:194-425` 完整产出。覆盖：① 三种引用强度对比（hard / weak / soft）的内存语义、生命周期、适用场景；② `FSoftObjectPtr` 与 `FSoftObjectPath` 的物理布局差异；③ `FBaseSoftReferenceType` + `FSoftObjectPtrType` + `FSoftClassPtrType` 三层 facade 体系（`SetArgument` / `GetReturnValue` / `GetDebuggerValue` 等公共反射 + 子类差异点）；④ 阶段 1 模板声明（共用 `FSoftObjectPtr` 双 ValueClass + `RegisterTypeFinder` Class 优先 cast）；⑤ 阶段 2 方法注册（`BindSoftPtrBaseMethods` 共享集 + Object/Class 分岔的 `Get` / `LoadAsync` / `TSubclassOf` 互通）；⑥ AS 端语法速查与典型 `.as` 用法（声明 / 路径构造 / TSubclassOf 互通 / 编译期约束）；⑦ UPROPERTY 资源选择器与反射 fallback 显式跳过软指针属性的设计动机；⑧ 异步加载完整链路（拒 Actor/Component → 已加载即时回调 → 包不存在即时 null 回调 → LoadPackageAsync → WeakClass 防卸载 → 二次 IsA 校验）；⑨ AssetManager / Streamable 集成与"细粒度 Soft vs 粗粒度 PrimaryAsset"两条加载路径分层；⑩ Cooked Build 依赖图扫描机制；⑪ 11 项关键限制与 5 类调试技巧。 |
