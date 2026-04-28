# 附录 A.4 测试专题到运行时子系统映射表

> **所属模块**: 附录 → Test Topic / Runtime Subsystem Mapping
> **关键源码**: `Plugins/Angelscript/Source/AngelscriptTest/`, `Plugins/Angelscript/Source/AngelscriptRuntime/`, `Documents/Guides/TestCatalog.md`, `Documents/Guides/TestConventions.md`

## 映射表

| 测试专题 | 主要运行时子系统 | 说明 |
| --- | --- | --- |
| `ClassGenerator/` | `ClassGenerator/`, `ASClass`, `ASStruct` | 覆盖类与结构体生成、重载传播 |
| `Preprocessor/` | `Preprocessor/` | 覆盖模块组织、import、chunk/macro 前端 |
| `HotReload/` | `Core/AngelscriptEngine`, `ClassGenerator`, `Editor Reload Helper` | 覆盖文件变化到 reload 主链 |
| `Debugger/` | `Debugging/`, `AngelscriptDebugValue` | 覆盖协议、会话、断点和值提取 |
| `Dump/` | `Dump/`, `CodeCoverage/`, 扩展表入口 | 覆盖状态导出与回归 |
| `Bindings/` | `Binds/`, `FunctionLibraries/`, `Type System` | 覆盖原生 API 暴露与参数封送 |
| `Actor/` / `Component/` / `Interface/` / `Delegate/` | `ClassGenerator`, `BaseClasses`, Bind/Library | 覆盖最终 UObject/World 行为语义 |
| `Subsystem/` | `BaseClasses/`, Runtime subsystem 链 | 覆盖脚本子系统生命周期 |
| `Native/` | 公共 AngelScript API / ASSDK 适配层 | 覆盖公共 API，不碰 Runtime 私有实现 |
| `Learning/` | 多个运行时子系统的可观测视图 | 用教学/trace 方式解释系统行为 |
| `AngelscriptRuntime/Tests/` | Core / Debugging / CodeCoverage / PrecompiledData | 覆盖 Runtime 私有实现与内部安全网 |

## 小结

- 这张映射表把测试目录树反向对齐到运行时子系统地图
- 当新增测试时，先看它要验证哪条子系统主线，再决定目录与前缀
