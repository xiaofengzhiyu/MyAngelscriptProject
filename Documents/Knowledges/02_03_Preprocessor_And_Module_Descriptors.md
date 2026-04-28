# 预处理与模块描述符

> **所属模块**: 类型系统与生成链路 → Preprocessor / Module Descriptor
> **关键源码**: `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`, `Documents/Plans/Plan_UnrealCSharpArchitectureAbsorption.md`

这一节要补的不是“预处理器会扫脚本文件”这种表层结论，而是脚本文件如何被组织成模块、宏块和导入链。当前 Preprocessor 的核心职责可以压成一句话：**把离散 `.as` 文件整理成一组有顺序、有类型描述、有生成补链的模块输入。**

## 2.3.1 脚本文件发现与 import 解析

- 脚本根目录由 `FAngelscriptEngine` 发现，既包括工程 `Script/`，也包括启用插件贡献的脚本根
- `FilenameToModuleName()` 把相对路径映射为模块名，例如 `Game/Player.as -> Game.Player`
- `ParseIntoChunks()` 在顶层作用域识别 `import ...;`，把它们记录为 `FImport`

这说明 import 不是编译阶段临时扫出来的字符串，而是预处理阶段就被结构化进文件描述符里。

## 2.3.2 `FAngelscriptModuleDesc` / `ClassDesc` / `EnumDesc` / `DelegateDesc`

- `FFile` 持有一个 `TSharedPtr<FAngelscriptModuleDesc>`，它是文件级到模块级的收敛点
- `FAngelscriptModuleDesc` 汇总该模块的类、枚举、委托、代码段、导入模块、后初始化函数和哈希信息
- `FChunk`、`FMacro`、`FImport` 是更细粒度的语法中间层；`ClassDesc` / `EnumDesc` / `DelegateDesc` 则是面向生成器与运行时的类型化描述符

换句话说，Preprocessor 不是只做文本清洗，而是在构造“脚本模块元数据树”。

## 2.3.3 文件到类型的组织边界

- `FFile` 是单文件容器：保存原始代码、切块结果、导入、生成代码和最终 `ProcessedCode`
- `FAngelscriptModuleDesc` 是模块容器：保存对运行时有意义的类、枚举、委托、代码段和哈希
- `StaticsClass` 这类文件级辅助描述，负责把文件中的全局函数/静态内容折到可编译的类型结构里

因此当前组织边界是：**文件负责承载源文本和块，模块负责承载类型化结果。**

## 2.3.4 Chunk / Macro / Import 的预处理模型

Preprocessor 的扫描器会把脚本切成几类 `Chunk`：

- `Global`
- `Class`
- `Struct`
- `Interface`
- `Enum`

同时它还会在 chunk 内采集：

- `FMacro`：`UPROPERTY` / `UFUNCTION` / `UCLASS` / `USTRUCT` / `UENUM`
- `FDefaultsCode`：`default` 语句
- `FImport`：模块导入
- `FDelegateDesc`：委托与事件声明

最终顺序不是“先编译后再找宏”，而是：

```text
ParseIntoChunks
  -> DetectClasses
  -> AnalyzeClasses
  -> ProcessMacros
  -> ProcessDelegates
  -> ProcessDefaults
  -> CondenseFromChunks
```

这条链说明预处理器本质上是一个“脚本前端整理器”。

## 2.3.5 Import 排序、循环依赖与装配顺序

- `ProcessImports()` 用深度优先方式递归处理依赖，并把结果收敛到 `OutSortedFiles`
- `bIsResolvingImports` 用来检测回边；一旦发现循环依赖，会沿 `FImportChain` 回溯并报出导入链
- 当当前上下文启用 automatic import 模式时，手写 import 不再主导装配顺序，只保留兼容性与警告路径

因此 import 排序的真正目的不是“让文件顺序更整齐”，而是确保：

- 依赖模块先进入处理序列
- 循环依赖在预处理阶段就暴露，而不是拖到类型生成或编译执行阶段
- 最终 `ProcessedCode` 的装配顺序与模块依赖顺序一致

## 当前边界最值得记住的点

- Preprocessor 既处理文本，也构造模块描述符，不是单纯的宏替换器
- `FFile` / `FChunk` / `FMacro` / `FImport` 是前端中间层，`FAngelscriptModuleDesc` 是面向后续生成与编译的收敛层
- Import 解析与排序决定了后续类型分析和生成器装配顺序，因此它属于架构主链，而不是可有可无的语法糖

## 小结

- 文件发现、模块命名、import 解析、chunk 切分和描述符汇总共同组成了当前脚本前端
- `FAngelscriptModuleDesc` 把文本世界收敛成运行时/生成器可消费的模块元数据
- 循环依赖、自动 import 和最终装配顺序都在预处理阶段被提前治理
