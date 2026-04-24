# Angelscript 接口绑定（Interface Binding）

## 当前支持范围

### C++ UInterface 自动绑定

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

### C++ 接口 Cast 指针偏移修复

当脚本中使用 `Cast<UCppInterface>(NativeActor)` 且目标 C++ 类通过多重继承实现多个接口时，`GetInterfacePointerForCast` helper 会正确计算 `PointerOffset`，确保返回正确的接口指针。

## 关键代码位置

| 组件 | 文件 | 说明 |
|------|------|------|
| C++ 接口自动绑定 | `Binds/Bind_BlueprintType.cpp` | Phase 5 扫描 `CLASS_Interface` 类并注册方法 |
| 接口 Cast 指针偏移 | `Binds/Bind_Helpers.h` | `GetInterfacePointerForCast` helper |
| 接口 Cast 运行时 | `Core/AngelscriptEngine.cpp` | `CanCastScriptObjectToUnrealInterface` |
| 脚本类接口挂接 | `ClassGenerator/AngelscriptClassGenerator.cpp` | `ResolveInterfaceClass` + `AddInterfaceRecursive` |

## 不支持的功能

以下功能已在 commit `96e4560` 中回退，**不再支持**：
- 脚本 `UINTERFACE()` / `interface UIFoo {}` 声明
- 脚本类 `implements UIFoo`
- `TScriptInterface<T>` 在脚本中的任何使用
- `Obj.Implements<T>()` 泛型语法糖
- 脚本接口方法上的 `UFUNCTION()` 修饰符
- `FInterfaceMethodSignature` 完整签名校验

## 测试覆盖

当前接口相关测试：
- `Interface/` 目录：27 个测试（含 NativePointerOffset 回归测试）
- `Preprocessor/`：2 个接口预处理器测试
- `Compiler/`：1 个编译管线接口测试

## 相关文档

- `Documents/Plans/Plan_CppInterfaceBinding.md` — C++ 接口自动绑定执行计划
