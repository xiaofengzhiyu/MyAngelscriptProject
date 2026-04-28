# 附录 A.1 源码文件索引表

> **所属模块**: 附录 → Source File Index
> **关键源码**: `Plugins/Angelscript/Source/AngelscriptRuntime/`, `Plugins/Angelscript/Source/AngelscriptEditor/`, `Plugins/Angelscript/Source/AngelscriptTest/`, `Plugins/Angelscript/Source/AngelscriptUHTTool/`

这份附录不试图列出每一个源文件，而是给出架构分析主线最常回看的文件簇索引，帮助读者快速跳回关键子系统。

## 核心索引表

| 领域 | 关键路径 |
| --- | --- |
| Runtime 总控 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 类/结构体生成 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.*`, `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASStruct.*` |
| 预处理 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.*` |
| Bind | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.*`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/` |
| Type System | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.*` |
| Function Bridge | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/FunctionCallers.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp` |
| FunctionLibraries | `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/`, `Plugins/Angelscript/Source/AngelscriptEditor/Public/FunctionLibraries/` |
| BaseClasses | `Plugins/Angelscript/Source/AngelscriptRuntime/BaseClasses/` |
| StaticJIT | `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/` |
| Debugger | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptDebugValue.h` |
| Dump | `Plugins/Angelscript/Source/AngelscriptRuntime/Dump/`, `Plugins/Angelscript/Source/AngelscriptTest/Dump/` |
| CodeCoverage | `Plugins/Angelscript/Source/AngelscriptRuntime/CodeCoverage/` |
| Hash / ThirdParty | `Plugins/Angelscript/Source/AngelscriptRuntime/Hash/`, `Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/` |
| Editor 集成 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/` |
| Test Infrastructure | `Plugins/Angelscript/Source/AngelscriptTest/Shared/`, `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/`, `Plugins/Angelscript/Source/AngelscriptTest/` |
| UHT Tool | `Plugins/Angelscript/Source/AngelscriptUHTTool/` |

## 小结

- 这张索引表的目标是快速回到“主文件簇”，不是替代源码树浏览
- 当需要继续写专题文档时，优先从这里定位核心入口文件
