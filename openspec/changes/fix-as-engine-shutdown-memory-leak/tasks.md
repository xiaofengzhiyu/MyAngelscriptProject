## Tasks

### 1. Shutdown 路径 UASClass RemoveFromRoot <!-- Non-TDD -->

- **File**: `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`
- **Change**: 在 `Shutdown()` 的 UASClass 清理循环中追加 `ASClass->RemoveFromRoot()` + `ASClass->ClearFlags(RF_Standalone)`
- **Verification**: 编译通过
- [x] 完成

### 2. Shutdown 路径 UASStruct/UDelegateFunction/UUserDefinedEnum RemoveFromRoot <!-- Non-TDD -->

- **File**: `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`
- **Change**: 在 Shutdown UASClass 循环后，增加 `ForEachObjectWithPackage` 遍历 AngelscriptPackage 内所有相关类型，调用 `RemoveFromRoot()` + `ClearFlags(RF_Standalone)`
- **Verification**: 编译通过
- [x] 完成

### 3. GBlueprintEventsByScriptName Shutdown 清理 <!-- Non-TDD -->

- **File**: `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`
- **Change**: 在 `ReleaseOwnedSharedStateResources()` 中 `extern` 引用并 `Empty()`
- **Verification**: 编译通过
- [x] 完成

### 4. AngelscriptGameplayTagsLookup — 保留不清理 <!-- Non-TDD -->

- **决策变更**: 原计划清理，实施后触发 `GameplayTagNamespaceGlobals` 测试回归（`asNAME_TAKEN`）。分析确认 UE GameplayTag 注册是进程级不可逆操作，此 TSet 作为重复注册 guard 必须跨引擎周期保留。已移除 `Empty()` 调用并添加注释说明。
- [x] 完成（不清理）

### 5. CachedEditorClasses 重置 <!-- Non-TDD -->

- **File**: `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp`
- **Change**: 将 `IsEditorOnlyClass` 函数内 `static TMap` 提升为文件作用域 `GCachedEditorClasses`，新增 `ResetCachedEditorClasses()` 暴露清理入口，在 `ReleaseOwnedSharedStateResources()` 中 `#if WITH_EDITOR` 调用
- **Verification**: 编译通过
- [x] 完成

### 6. Package unroot <!-- Non-TDD -->

- **File**: `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`
- **Change**: Shutdown 末尾对 `AngelscriptPackage` 和 `AssetsPackage` 调用 `RemoveFromRoot()` + `ClearFlags(RF_Standalone)`
- **Verification**: 编译通过
- [x] 完成

### 7. 编译验证 <!-- Non-TDD -->

- **Verification**: `Build.bat AngelscriptProjectEditor Win64 Development` — 成功
- [x] 完成

### 8. 运行测试回归验证 <!-- Non-TDD -->

- **Verification**: 全量测试套件运行，识别并修复 GameplayTag 回归
- [x] 完成

### 8. GScriptNativeForms 泄漏修复（Phase 2） <!-- Non-TDD -->

- **Files**:
  - `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITBinds.h` — 添加 `static void ReleaseAllNativeForms();` 声明
  - `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITBinds.cpp` — 实现：遍历 GScriptNativeForms delete value + Empty
  - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` — 在 `ReleaseOwnedSharedStateResources()` 中 `#if AS_CAN_GENERATE_JIT` 守卫内调用
- **Verification**: 编译通过
- [x] 完成

### 9. AngelscriptDocs 4 个 TMap 清理（Phase 2） <!-- Non-TDD -->

- **Files**:
  - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptDocs.h` — 添加 `static void ResetAllDocumentation();` 声明
  - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptDocs.cpp` — 实现：4 个 TMap 全部 Empty
  - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` — 在 `ReleaseOwnedSharedStateResources()` 中调用
- **Verification**: 编译通过
- [x] 完成
