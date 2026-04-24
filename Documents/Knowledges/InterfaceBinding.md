# Angelscript 接口绑定（Interface Binding）

## 当前支持范围

### 脚本定义接口（完整支持）

使用 `UINTERFACE()` + `interface` 语法在 `.as` 脚本中声明接口：

```angelscript
UINTERFACE()
interface UIDamageable
{
    void TakeDamage(float Amount);
    float GetHealth() const;
}

UCLASS()
class AMyActor : AActor, UIDamageable
{
    UPROPERTY()
    float Health = 100.0;

    UFUNCTION()
    void TakeDamage(float Amount) { Health -= Amount; }

    UFUNCTION()
    float GetHealth() const { return Health; }
}
```

支持特性：
- 接口声明、多方法、返回值、const 方法、引用参数
- 接口继承（`interface IChild : IBase`）
- 类实现接口（逗号分隔继承列表 `class Foo : AActor, IBar, IBaz`）
- `Cast<UIFoo>(Object)` 类型转换
- 通过接口引用调用方法
- `ImplementsInterface(UClass)` 查询
- 编译期缺失方法检查
- 接口禁止 `UPROPERTY()` 声明
- 热重载支持

### C++ UInterface 自动绑定（自 2026-04-23 起支持）

C++ 中通过 `UINTERFACE()` 宏声明的接口，如果标记了 `BlueprintType`，其 `BlueprintCallable` / `BlueprintEvent` / `BlueprintPure` 方法会被**自动注册**到 AngelScript。

脚本类可以：
- 声明实现 C++ 接口：`class Foo : AActor, UMyCppInterface`
- 通过 `Cast<UMyCppInterface>(Obj)` 获取接口引用
- 通过接口引用调用 C++ 接口方法
- C++ 侧通过 `Execute_MethodName(Actor)` 调用脚本实现

**限制**：
- 使用 `U` 前缀引用（如 `UAngelscriptNativeParentInterface`），不支持 `I` 前缀
- 仅自动绑定标记了 `BlueprintType` 的接口
- 方法参数/返回值中类型在 AS 中未注册的会被静默跳过

## 实现架构

### 管线概览

```
[预处理器] UINTERFACE() + interface 块
    ├── 提取方法声明到 InterfaceMethodDeclarations
    ├── 擦除 interface 块（AS 编译器不支持 interface 关键字）
    ├── RegisterObjectType（引用类型）
    └── RegisterObjectMethod（CallInterfaceMethod 泛型回调）
         ↓
[类生成器] 创建 UClass (CLASS_Interface | CLASS_Abstract)
    ├── 创建 UFunction 存根
    ├── ResolveInterfaceClass（三级回退查找）
    ├── AddInterfaceRecursive（递归接口添加）
    └── 方法存在性验证
         ↓
[运行时] Cast<I> → opCast
    ├── ImplementsInterface 检查
    ├── CanCastScriptObjectToUnrealInterface（已注入 AS 引擎 3 处）
    └── CallInterfaceMethod → FindFunction → InvokeReflectiveUFunctionFromGenericCall
         ↓
[绑定层] Bind_BlueprintType.cpp Phase 5（C++ 接口）
    ├── Round 1: 扫描 CLASS_Interface 类的 BlueprintCallable 方法并注册
    └── Round 2: CopySystemType 链接接口继承链
```

### 关键代码位置

| 组件 | 文件 | 位置 |
|------|------|------|
| 接口方法调度 | `ClassGenerator/AngelscriptClassGenerator.cpp` | 行 56-68 `CallInterfaceMethod` |
| 接口解析（预处理器） | `Preprocessor/AngelscriptPreprocessor.cpp` | `EChunkType::Interface` 相关逻辑 |
| C++ 接口自动绑定 | `Binds/Bind_BlueprintType.cpp` | `Bind_Defaults` Phase 5 |
| 接口 Cast 运行时 | `Core/AngelscriptEngine.cpp` | 行 112-138 `CanCastScriptObjectToUnrealInterface` |
| 接口挂接（类生成） | `ClassGenerator/AngelscriptClassGenerator.cpp` | 行 5332-5460 `ResolveInterfaceClass` + `AddInterfaceRecursive` |

## 已知限制

1. **`TScriptInterface<T>` 属性不支持** — `FInterfaceProperty` 未参与绑定
2. **方法签名校验仅按函数名** — `FInterfaceMethodSignature` 只有 `FName`，不检查参数类型
3. **接口块不进入 AS 编译器** — 由预处理器擦除后在插件层模拟
4. **AS 语言级 `interface I…` 不支持** — AS 引擎的 `interface` 关键字在 `as_tokendef.h` 中被注释
5. **接口方法无法使用 `UFUNCTION()` 修饰符** — 预处理器跳过接口块中的 UFUNCTION 宏

## 架构决策记录

| 决策点 | 结论 | 理由 |
|--------|------|------|
| ThirdParty 修改策略 | 方案 A+（不新增修改） | ThirdParty 已有 `CanCastScriptObjectToUnrealInterface` 注入；继续用 `RegisterObjectType` 模拟 |
| 命名约定 | 仅 U 前缀 | 与现有脚本接口一致，不做双重 U+I 注册 |
| FInterfaceProperty 优先级 | 先跳过 | 由 `Plan_InterfaceBinding.md` Phase 4 后续承接 |

## 测试覆盖

当前共 36+ 个接口相关测试：
- `Interface/` 目录：33 个（Declare 4 / Implement 3 / Cast 4 / Advanced 10 / Native 5 / NativeBridge 1 / NativeInheritedChildSurface 1 / CppBridge 1 / Lifecycle 1 / NativeLifecycle 1 / NativeBinding 1 / Validation 1）
- `Preprocessor/`：2 个接口预处理器测试
- `Compiler/`：1 个编译管线接口测试

## 相关文档

- `Documents/Plans/Plan_CppInterfaceBinding.md` — C++ 接口自动绑定执行计划
- `Documents/Plans/Plan_InterfaceBinding.md` — 完整接口绑定设计文档（含 FInterfaceProperty、签名校验等后续阶段）
