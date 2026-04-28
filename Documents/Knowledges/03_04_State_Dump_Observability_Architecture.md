# State Dump 可观测性架构

> **所属模块**: 运行时支撑子系统 → State Dump / Observability
> **关键源码**: `Plugins/Angelscript/Source/AngelscriptRuntime/Dump/AngelscriptStateDump.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Dump/AngelscriptStateDump.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Dump/AngelscriptDumpCommand.cpp`, `Documents/Plans/Archives/Plan_ASEngineStateDump.md`

State Dump 这条线最重要的不是 CSV 文件很多，而是它坚持了一个清晰边界：**Runtime 负责导出能力，Test 负责命令入口和自动化回归，整个系统以外部观察者模式读取状态，而不是把导出逻辑侵入回业务类型。**

## 3.4.1 `FAngelscriptStateDump::DumpAll()` 总入口

- `DumpAll()` 是状态导出的总装配点
- 它负责组织各张表的生成，并将结果导出到目标目录
- 设计上它不是“顺手打日志”，而是一个结构化状态快照入口

## 3.4.2 Runtime / Test / Editor 三侧导出链路

- Runtime 负责 `EngineOverview`、`Modules`、`Classes`、`Functions`、`JITDatabase`、`CodeCoverage` 等核心表
- Test 模块通过 `as.DumpEngineState` 控制台命令和自动化测试，把导出能力变成可执行回归入口
- Editor 侧如果有扩展表，会通过扩展委托参与，而不是把 editor-only 逻辑直接塞进 Runtime Dump 主体

因此 Dump 的三侧协作不是三套实现，而是一套 Runtime 主体 + Test/Editor 扩展入口的模式。

## 3.4.3 外部观察者模式与扩展点设计

- Dump 只读取现有 public/runtime 状态，不为导出专门回写原有系统
- `OnDumpExtensions` 这类扩展点允许外部模块追加表，而不改动核心导出结构
- 这与根级和插件级 AGENTS 里强调的“pure external observer”原理完全一致

## 当前体系最值得记住的点

- State Dump 的主语不是测试，也不是 Editor，而是 Runtime 状态快照能力
- Test 只负责暴露控制台命令和自动化回归，不应该反向掌控 Runtime 内部状态
- 扩展点设计保证了 Dump 可以增量成长，而不破坏原有导出主链

## 小结

- `DumpAll()` 是当前状态可观测架构的总入口
- Runtime 实现导出，Test 固化验证，Editor 通过扩展点补表
- 外部观察者原则是这套系统最重要的架构约束
