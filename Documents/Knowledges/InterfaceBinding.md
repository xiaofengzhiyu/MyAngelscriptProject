# Angelscript 接口绑定（Interface Binding）

## 当前支持范围

### 脚本定义接口（完整支持）

使用 `UINTERFACE()` + `interface` 语法在 `.as` 脚本中声明接口：

```angelscript
UINTERFACE()
interface UIDamageable
{
    // 裸方法：默认语义等价于 BlueprintImplementableEvent
    // 生成 UFunction 带 FUNC_Event | FUNC_BlueprintEvent
    void TakeDamage(float Amount);

    // 签名含 const 限定、参数类型、返回值都参与 Phase 1 的编译期校验
    float GetHealth() const;

    // BlueprintNativeEvent：追加 FUNC_Native，允许 C++ 侧提供 _Implementation 默认分支
    UFUNCTION(BlueprintNativeEvent)
    void OnHealthChanged(float NewHealth);

    // BlueprintImplementableEvent：显式声明，与裸方法等价
    UFUNCTION(BlueprintImplementableEvent)
    void OnDied();

    // BlueprintCallable：追加 FUNC_BlueprintCallable
    UFUNCTION(BlueprintCallable)
    bool CanBeDamaged() const;

    // BlueprintPure：追加 FUNC_BlueprintCallable | FUNC_BlueprintPure
    UFUNCTION(BlueprintPure)
    int GetCurrentArmor();
}

UCLASS()
class AMyActor : AActor, UIDamageable
{
    UPROPERTY()
    float Health = 100.0;

    // 实现方法：签名必须与接口声明**完全一致**（Phase 1 校验参数数量/类型/返回值/const）
    UFUNCTION()
    void TakeDamage(float Amount) override { Health -= Amount; }

    UFUNCTION()
    float GetHealth() const override { return Health; }

    // ...其它接口方法同理实现，签名不一致时 FinalizeClass 阶段就会报错
}
```

支持特性：
- 接口声明、多方法、返回值、const 方法、引用参数
- 接口继承（`interface IChild : IBase`）
- 类实现接口（逗号分隔继承列表 `class Foo : AActor, IBar, IBaz`）
- `Cast<UIFoo>(Object)` 类型转换
- 通过接口引用调用方法
- `ImplementsInterface(UClass)` 查询
- **`Obj.Implements<UIFoo>()` 泛型查询**（Phase 5 预处理器语法糖）
- **接口方法签名完整校验**（Phase 1，参数数量/类型/返回值/const 限定）
- **接口方法支持 `UFUNCTION()` 修饰符**（Phase 4，`BlueprintNativeEvent` / `BlueprintImplementableEvent` / `BlueprintCallable` / `BlueprintPure`）
- **`TScriptInterface<UIFoo>` 作为方法体内的本地变量**（Phase 2），以及 C++ 类上 `UPROPERTY TScriptInterface<IFoo>` 字段的反射层 round-trip
- 接口禁止 `UPROPERTY()` 声明
- 热重载支持

脚本侧尚未开放 `TScriptInterface<UIFoo>` 作为 UPROPERTY 字段 / UFUNCTION 参数 / UFUNCTION 返回类型 —— 这 3 个 surface 目前仍被 class generator 分析阶段拒绝，使用方法请见下方 "`TScriptInterface<I>` 桥接" 节。

### `TScriptInterface<I>` 桥接（Phase 2，2026-04-24 起支持）

脚本可以使用 `TScriptInterface<T>` 作为**方法体内的本地变量**；C++ 类上的 `UPROPERTY TScriptInterface<IFoo>` 字段也能正确 round-trip 到反射层。这是 Phase 2 当前覆盖的子集：

```angelscript
class AInventorySlot : AActor
{
    UFUNCTION(BlueprintCallable)
    void ApplyDamage(UObject Target, float Amount)
    {
        if (Target == nullptr || !Target.Implements<UIDamageable>())
            return;

        // 本地 TScriptInterface<>：从 UObject 构造自动校验 + 填 InterfacePointer
        TScriptInterface<UIDamageable> Ref = Target;
        if (Ref.IsValid())
        {
            UIDamageable Damageable = Cast<UIDamageable>(Ref.Get());
            if (Damageable != nullptr)
                Damageable.TakeDamage(Amount);
        }
    }
}
```

```cpp
// C++ 类上的 UPROPERTY TScriptInterface<IFoo> 通过 Phase 2 的
// FScriptInterfaceType + TypeFinder 被识别，脚本读写时 ObjectPointer 与
// InterfacePointer 双字段均正确 round-trip。
UCLASS()
class UMyCppActor : public AActor
{
    UPROPERTY()
    TScriptInterface<IDamageable> NativeRef;
};
```

运行时保证：
- `FScriptInterface` 双字段 `{ ObjectPointer, InterfacePointer }` 内存布局与 `FInterfaceProperty` 完全一致（16 字节 POD），直接映射到 UE 反射层。
- 从 UObject 构造或赋值时：调用 `GetInterfacePointerForCast` 计算接口偏移并填 `InterfacePointer`，多接口继承场景下 `Secondary` 接口偏移与 `Parent` 接口偏移互不相同。
- 赋值一个不实现接口的 UObject 会触发 AS Throw 异常（BeginPlay 等执行上下文在 Throw 点中断）。
- `TScriptInterface<T>::Get()` 返回 `UObject` 句柄（保持 AS 类型系统简单）；要调用接口方法需先 `Cast<UIFoo>(Ref.Get())`。

**当前未覆盖的 surface**（`FScriptInterfaceType::CreateProperty` / 参数解析在 class generator 分析阶段尚未被放行）：
- 脚本 UCLASS 上的 `UPROPERTY TScriptInterface<UIFoo>` 字段声明
- 脚本 UFUNCTION 的 `TScriptInterface<UIFoo>` 参数类型与返回类型

这些路径留作后续迭代。目前脚本类 UPROPERTY / UFUNCTION 里的接口引用，请用 `UObject` / `AActor` 裸指针搭配 `Cast<UIFoo>()` 的组合模式代替（本文档顶部示例即是）。

### C++ 原生实现类的指针偏移（Phase 3，2026-04-23 起修复）

脚本 `Cast<UIFoo>(NativeActor)` 之后调用接口方法时 `this` 指针已经带上正确的继承偏移；多接口场景下 `Secondary` 接口方法派发不会命中 `Parent` 接口的 vtable。底层由 `FAngelscriptBindHelpers::GetInterfacePointerForCast(UObject*, UClass*)` 统一计算，共享给 Phase 2 的 TScriptInterface 桥接路径。

### C++ UInterface 自动绑定（自 2026-04-23 起支持）

C++ 中通过 `UINTERFACE()` 宏声明的接口，如果标记了 `BlueprintType`，其 `BlueprintCallable` / `BlueprintEvent` / `BlueprintPure` 方法会被**自动注册**到 AngelScript。

脚本类可以：
- 声明实现 C++ 接口：`class Foo : AActor, UMyCppInterface`
- 通过 `Cast<UMyCppInterface>(Obj)` 获取接口引用（带正确偏移）
- 通过接口引用调用 C++ 接口方法
- C++ 侧通过 `Execute_MethodName(Actor)` 调用脚本实现

**当前约束**：
- 使用 `U` 前缀引用（如 `UAngelscriptNativeParentInterface`），不支持 `I` 前缀（见下方架构决策记录）
- 仅自动绑定标记了 `BlueprintType` 的接口
- 方法参数/返回值中类型在 AS 中未注册的会被静默跳过

## 实现架构

### 管线概览

```
[预处理器] UINTERFACE() + interface 块
    ├── 提取方法声明到 InterfaceMethodDeclarations
    ├── Phase 4: 解析 UFUNCTION() specifier，填 InterfaceMethodFlags（与方法索引对齐）
    ├── 擦除 interface 块（AS 编译器不支持 interface 关键字）
    ├── RegisterObjectType（引用类型）
    └── RegisterObjectMethod（CallInterfaceMethod 泛型回调）
         ↓
[预处理器 post-pass] Phase 5: Obj.Implements<T>() → Obj.ImplementsInterface(T::StaticClass())
    └── 正则 + 词法状态机，字符串/注释隔离
         ↓
[类生成器] 创建 UClass (CLASS_Interface | CLASS_Abstract)
    ├── 创建 UFunction 存根（Phase 4: 按 InterfaceMethodFlags[i] 设置 FunctionFlags）
    ├── ResolveInterfaceClass（三级回退查找）
    ├── AddInterfaceRecursive（递归接口添加）
    ├── Phase 1: FinalizeClass 做完整签名比对（FInterfaceMethodSignature 扩展了 ParamTypes、ReturnType、bIsConst）
    └── 方法存在性验证 + 签名匹配验证
         ↓
[运行时] Cast<I> → opCast
    ├── ImplementsInterface 检查
    ├── Phase 3: GetInterfacePointerForCast 计算偏移
    ├── CanCastScriptObjectToUnrealInterface（已注入 AS 引擎 3 处）
    └── CallInterfaceMethod → FindFunction → InvokeReflectiveUFunctionFromGenericCall
         ↓
[绑定层] Bind_BlueprintType.cpp
    ├── Phase 5: 扫描 CLASS_Interface 类的 BlueprintCallable 方法并注册
    ├── Phase 5: CopySystemType 链接接口继承链
    ├── Phase 2: FScriptInterfaceType + TScriptInterface<class T> AS template + TypeFinder 映射 FInterfaceProperty
    └── BindStaticClass 给每个 UInterface 注册 namespace-scoped StaticClass()（Phase 5 sugar 依赖）
```

### 关键代码位置

| 组件 | 文件 | 位置 |
|------|------|------|
| 接口方法调度 | `ClassGenerator/AngelscriptClassGenerator.cpp` | `CallInterfaceMethod`（行 56-68）|
| 接口解析（预处理器） | `Preprocessor/AngelscriptPreprocessor.cpp` | `EChunkType::Interface` 相关逻辑（含 Phase 4 UFUNCTION specifier 提取）|
| C++ 接口自动绑定 | `Binds/Bind_BlueprintType.cpp` | `Bind_Defaults` Phase 5 |
| 接口 Cast 运行时 | `Core/AngelscriptEngine.cpp` | `CanCastScriptObjectToUnrealInterface`（行 112-138）|
| 接口指针偏移 helper | `Binds/Bind_Helpers.h` | `FAngelscriptBindHelpers::GetInterfacePointerForCast` |
| `TScriptInterface<T>` 桥接 | `Binds/Bind_TScriptInterface.h` + `Bind_BlueprintType.cpp` | `FScriptInterfaceType` + AS template |
| 接口挂接（类生成） | `ClassGenerator/AngelscriptClassGenerator.cpp` | `ResolveInterfaceClass` + `AddInterfaceRecursive`（行 5332-5460）|
| 接口 UFunction flag 设置 | `ClassGenerator/AngelscriptClassGenerator.cpp` | 行 2980 附近（读 `InterfaceMethodFlags[i]`）|
| 签名校验 | `Core/AngelscriptEngine.h` + `ClassGenerator/AngelscriptClassGenerator.cpp` | `FInterfaceMethodSignature` + `FinalizeClass` 对比路径 |
| `Implements<T>()` sugar | `Preprocessor/AngelscriptPreprocessor.cpp` | `PostProcessImplementsTemplate` |

## 已知限制

1. **接口块不进入 AS 编译器** — 由预处理器擦除后在插件层模拟；这是 AS fork 结构性分叉点，不修复（见 `Documents/Guides/AngelscriptForkStrategy.md`）
2. **AS 语言级 `interface I...` 关键字不启用** — 同上 fork 分叉点，`as_tokendef.h::interface` 关键字保持未启用状态
3. **C++ UInterface 使用 `U` 前缀而非 `I` 前缀** — 保持与脚本接口命名一致；不做 U/I 双前缀注册（见架构决策记录）
4. **不提供脚本侧 `Execute_MethodName(Actor, ...)` 语法糖** — 脚本直接用 `Target.Foo(...)` 或 `Cast<UIFoo>(Obj).Foo(...)` 即可；Execute_ 是 UHT C++ 路径的遗产，脚本路径不需要
5. **`Implements<T>()` sugar 仅识别简单 `Obj.Implements<T>()` 形态** — 预处理器正则匹配 `.Implements<T>()` 后紧跟 `()`；支持 namespace-qualified T（如 `Ns::UIFoo`），但不支持复杂表达式（目前未见需求）

> **以下三条限制已在 Phase 1-5 解除**（保留此注释便于对照旧版本知识库）：
> - ~~`TScriptInterface<T>` 属性不支持~~ → Phase 2 已实现，见本文档"TScriptInterface<I> 属性与参数桥接"节
> - ~~方法签名校验仅按函数名~~ → Phase 1 已实现完整签名校验
> - ~~接口方法无法使用 `UFUNCTION()` 修饰符~~ → Phase 4 已实现修饰符传递

## 架构决策记录

| 决策点 | 结论 | 理由 |
|--------|------|------|
| ThirdParty 修改策略 | 方案 A+（零新增修改） | ThirdParty 已有 `CanCastScriptObjectToUnrealInterface` 注入；继续用 `RegisterObjectType` 模拟。Phase 1-5 完全在插件层完成 |
| 命名约定 | 仅 U 前缀 | 与现有脚本接口一致，不做双重 U+I 注册；`GetCppForm` 在生成 C++ 侧 `TScriptInterface<IFoo>` 时自动转前缀 |
| `FInterfaceProperty` 实现 | 直接复用 `FScriptInterface` 值类型路径 | 16B POD 内存布局与 `FInterfaceProperty` 完全对齐，AS 引擎的 `Binds.Property(Decl, Offset)` 默认路径即正确；无需新增 4 个 `Bind_Helpers` getter/setter |
| `Implements<T>()` 实现路径 | 预处理器语法糖 | AS 引擎不支持方法级模板实例化（只支持类模板 `asOBJ_TEMPLATE`）；走 `PostProcess` 通道与 `PostProcessRangeBasedFor` / `PostProcessLiteralAssets` 对齐，成本最低 |
| 接口方法 `Execute_` 语法糖 | 不实施 | UHT 产出的 `Execute_*` 是 C++ 路径遗产，脚本不需要；`CallInterfaceMethod` + UE 反射分发已足够 |

## 测试覆盖

当前共 **41** 个接口测试全部通过（Interface 主目录）+ 3 个 Preprocessor.Interface 测试：

- `Interface/` 目录（41 用例，16 个 `.cpp` 文件）：
  - 声明 / 实现 / Cast / Advanced / Native / NativeBridge / NativeInheritedChildSurface / CppBridge / Lifecycle / NativeLifecycle / NativeBinding / Validation / NoProperty 原有用例
  - **Property（Phase 2）**：`LocalDeclaration` / `AssignFromObject` / `CppReflection` / `InvalidAssign`
  - **EventFlags（Phase 4）**：`Matrix`（end-to-end 验证 UFunction 的 FunctionFlags 与 UFUNCTION specifier 对齐）
  - **ImplementsGeneric（Phase 5）**：`ScriptInterfaceTrue` / `CppInterfaceTrue` / `NotImplementedFalse` / `LegacyApiCompat`
  - **Signature（Phase 1）**：签名完整校验用例
  - **NativePointerOffset（Phase 3）**：多接口偏移回归
- `Preprocessor/Interface`（3 用例）：包含 Phase 4 的 `BlueprintEventFlags` specifier 解析验证

## 相关文档

- `Documents/Plans/Plan_CppInterfaceBinding.md` — C++ 接口自动绑定执行计划（已关闭）
- `Documents/Plans/Plan_InterfaceBinding.md` — 完整接口绑定设计文档（Phase 4/5 由 `Plan_InterfaceParityWithCpp.md` 承接）
- `Documents/Plans/Archives/Plan_InterfaceParityWithCpp.md` — 接口与 C++ 使用一致性补齐计划（Phase 1-5 完成，已归档）
- `Documents/Guides/AngelscriptForkStrategy.md` — AngelScript fork 结构性分叉点说明
