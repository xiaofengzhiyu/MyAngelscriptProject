# 附录 A.5 关键生成链路 ASCII 流程图索引

> **所属模块**: 附录 → ASCII Flow Index
> **关键源码**: `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/`, `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/`, `Plugins/Angelscript/Source/AngelscriptRuntime/Dump/`

## 关键流程索引

### 1. 脚本前端整理链

```text
DiscoverScriptRoots
  -> FindScriptFiles
      -> ParseIntoChunks
          -> ProcessImports
              -> Detect/Analyze Classes
                  -> ProcessMacros / Delegates / Defaults
                      -> CondenseFromChunks
```

### 2. 类/结构体生成链

```text
Preprocessed ModuleDesc
  -> ClassGenerator Analyze
      -> Build UASClass / UASStruct
          -> Prepare CppStructOps / Function Bindings
              -> Finalize Class / Struct
```

### 3. 热重载链

```text
DirectoryWatcher
  -> QueueScriptFileChanges
      -> CheckForHotReload
          -> Analyze Reload Requirement
              -> SoftReload or FullReload
                  -> Editor-side Reinstance
```

### 4. StaticJIT 执行链

```text
Script Function
  -> Optional JIT Compile
      -> FJITDatabase Mapping
          -> UASFunction JitFunction Entry
              -> Fallback to Script VM if needed
```

### 5. State Dump 可观测链

```text
FAngelscriptStateDump::DumpAll()
  -> Gather Runtime Tables
      -> Invoke Dump Extensions
          -> Write CSV Files
              -> Test Module Command / Automation Validation
```

## 小结

- 这份索引不是展开每个流程细节，而是提供一眼能回忆主链的最小 ASCII 入口
- 继续写专题时，可以把这里的流程图当成正文中的骨架草图
