# Type_Preprocessor — 预处理与模块描述符

> **所属前缀**: Type_（类型系统与生成链路族）
> **关注层面**: 插件自带的 `.as` 预处理器（条件编译、import 解析、注释提取、chunk 切分、UPROPERTY/UFUNCTION 宏识别、代码生成片段拼装）以及它产出的 **`FAngelscriptModuleDesc`** 这一份模块描述符——也就是后续 `CompileModules` 四阶段消费的中间表示。本文不重写 AS 内核 `asCBuilder` / `asCParser` 怎么把字符代码变成字节码（那是 `AS_Compiler.md` 的范围），不重写 `UASClass` / `UASFunction` 在反射树上的字段（那是 `Type_ClassGeneration` / `Type_BaseClass`），也不重写 HotReload 决策怎么挑选 `SoftReload` / `FullReload`（那是 `RT_HotReload`）；本文聚焦的是"一组 `.as` 文件磁盘字符 → 一棵 `FAngelscriptModuleDesc` 树"这一段
> **关键源码**:
> `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h` (~344 行，公开 API + 内部数据结构)
> · `Preprocessor/AngelscriptPreprocessor.cpp` (~4911 行，`Preprocess` / `ParseIntoChunks` / `ProcessImports` / `ParsePreProc` / `KillRawLine` / `Process*Macro` / `ProcessDelegates` / `CondenseFromChunks` / `PostProcess*` / 各类错误回流)
> · `Preprocessor/Helper_CommentFormat.h` (~242 行，`FormatCommentForToolTip` 注释 → tooltip 文本归一化，源自 `FHeaderParser`)
> · `Core/AngelscriptEngine.cpp` 的 `InitialCompile` / `PerformHotReload` / `FindAllScriptFilenames` / `MakeAllScriptRoots` 三处入口
> · `Core/AngelscriptEngine.h` 中 `FAngelscriptModuleDesc` / `FAngelscriptModuleDesc::FCodeSection` / `FAngelscriptModuleDesc::FUsageRestriction` 的字段定义
> · `ThirdParty/angelscript/source/as_module.cpp` `asCModule::AddScriptSection`（处理后字符代码进入 AS 内核的唯一入口）
> **关联文档**:
> `Documents/Knowledges/ZH/Type_Core.md` — 预处理生成的 `FAngelscriptClassDesc::SuperClass` 字符串如何反查成 `UClass*`
> · `Documents/Knowledges/ZH/Type_BaseClass.md` — `ResolveSuperClass` 与 `AnalyzeClasses` 如何把字符串基类绑成 `CodeSuperClass`、自动注入 `Spawn` / `Get` / `GetOrCreate` 静态方法
> · `Documents/Knowledges/ZH/Type_ClassGeneration.md` — `FAngelscriptModuleDesc::Classes` / `Enums` / `Delegates` 字段如何被 `AngelscriptClassGenerator` 消费
> · `Documents/Knowledges/ZH/AS_Compiler.md` — AS 内核 `asCBuilder` 如何接收 `AddScriptSection` 后的字符代码
> · `Documents/Knowledges/ZH/AS_LanguageSyntax.md` — `import`、`namespace`、`delegate` / `event` 等关键字的 AS 语义
> · `Documents/Knowledges/ZH/Arch_RuntimeLifecycle.md` — `InitialCompile` / `PerformHotReload` 在生命周期中的位置
> · `Documents/Knowledges/ZH/Arch_ErrorDiagnostics.md` — 预处理阶段错误如何回流到 `FAngelscriptEngine::ScriptCompileError`

---

## 概览

本文聚焦一个核心问题：**一组分散在若干 ScriptRoot 下的 `.as` 文件，是怎样被切成"chunk → macro → class/struct/enum/delegate 描述符"的中间表示，再以 `FAngelscriptModuleDesc` 数组的形式交给后续编译流水线的？为什么这层预处理器在 AS 内核之外另起炉灶、而不直接复用 AngelScript 自带的 `CScriptBuilder` add-on？**

```text
================================================================================
  预处理总览：磁盘字符 → FAngelscriptModuleDesc 数组
================================================================================

[ScriptRoot 集合]                                  [FAngelscriptPreprocessor]
  - <Project>/Script                                  Files: TArray<FFile>
  - <Engine>/Script                                  ┌────────────────────────┐
  - <PluginA>/Script                                 │ FFile {                 │
  - <PluginB>/Script                                 │   AbsoluteFilename      │
       │                                             │   RelativeFilename      │
       │ FindAllScriptFilenames(*.as)                │   RawCode      ←磁盘原文 │
       ▼                                             │   ChunkedCode  ←TChunk[]│
[FFilenamePair 数组]                                  │   GeneratedCode←合成片段│
       │                                             │   Imports             │
       │ Preprocessor.AddFile(rel, abs)              │   Delegates           │
       ▼                                             │   ProcessedCode ←给AS  │
                                                     │   Module:             │
[FAngelscriptPreprocessor::Preprocess()]             │     FAngelscriptModule │
       │                                             │       Desc            │
       │  ┌─ Step 1: ParseIntoChunks                 │ }                      │
       │  │     • 字符级状态机扫描 RawCode             └────────────────────────┘
       │  │     • 处理 #if / #ifdef / #ifndef / #else
       │  │     / #elif / #endif / #include / #restrict
       │  │     • import → File.Imports[]                   每文件一个 Module:
       │  │     • event / delegate → File.Delegates[]      ┌──────────────────┐
       │  │     • UPROPERTY / UFUNCTION / UCLASS /         │ ModuleName       │
       │  │       USTRUCT / UENUM / UMETA → Macros[]       │ Code: FCodeSection[]
       │  │     • class / struct / enum 关键字切 chunk     │ Classes          │
       │  │     • namespace XYZ { } 入栈                   │ Enums            │
       │  │     • 注释（//, /* */）→ ChunkComment /        │ Delegates        │
       │  │       Macro.Comment                            │ ImportedModules  │
       │  │                                                │ PostInitFunctions│
       │  ├─ Step 2: ProcessImports                        │ UsageRestrictions│
       │  │     • 拓扑排序（manual import 模式）           │ EditorOnlyBlock- │
       │  │     • 循环 import 检测                         │   Lines          │
       │  │                                                │ CodeHash         │
       │  ├─ Step 3: DetectClasses                         │ CombinedDepHash  │
       │  │     • 正则 (class|struct)\s+([A-Za-z_]+)       └──────────────────┘
       │  │     • 创建 FAngelscriptClassDesc
       │  │     • 注入 __StaticType_<X> 全局变量
       │  │
       │  ├─ Step 4: AnalyzeClasses                          │
       │  │     • ResolveSuperClass → CodeSuperClass         │ Preprocessor.GetModulesToCompile()
       │  │     • 自动注入 Spawn / Get / GetOrCreate          ▼
       │  │
       │  ├─ Step 5: ProcessMacros / ProcessDelegates      [TArray<TSharedRef<
       │  │     • 解析 specifiers → Meta / FunctionDesc    FAngelscriptModuleDesc>>]
       │  │
       │  ├─ Step 6: ProcessDefaults
       │  │     • default Foo = X 行 → ClassDesc->DefaultsCode
       │  │
       │  ├─ Step 7: CondenseFromChunks
       │  │     • Chunk[] 拼回 ProcessedCode
       │  │     • GeneratedCode 追加到尾部
       │  │
       │  └─ Step 8: PostProcessRangeBasedFor / LiteralAssets
       │        • for(T : C) → for(auto _It = C.Iterator(); ...)
       │        • asset Foo of UTexture2D → 全局变量声明
       ▼
       Compiler: ScriptModule->AddScriptSection(file, ProcessedCode)
                 (ASIScriptModule, 见 AS_Compiler.md)
```

**核心设计选择**：插件**没有**使用 AngelScript 上游的 `CScriptBuilder` add-on（`ThirdParty/angelscript/` 下不存在 `add_on` 目录，源码搜索 `CScriptBuilder` 零命中）。所有"include / 条件编译 / 注释提取 / 元数据收集"的活全部由 `FAngelscriptPreprocessor` 自己完成；AS 内核只做最纯粹的"字符 → 字节码"工作，通过 `asIScriptModule::AddScriptSection` 一次性吃下处理后的代码字符串。

后续章节按"ScriptRoot 与文件入队 → ParseIntoChunks 状态机 → 条件编译与 #include 政策 → 注释收集与文档化 → import 与拓扑排序 → 描述符产出与生成代码 → CondenseFromChunks 与后处理 → 错误回流 → 增量与 HotReload"的顺序展开，最后以两个附录收口（预处理指令速查 + 调试技巧）。

---

## 一、ScriptRoot 与文件入队：从磁盘到 `FFile` 数组

### 1.1 ScriptRoot 的来源

预处理器本身**不知道**有哪些 `.as` 文件存在；它只接收 `AddFile(relative, absolute)` 调用。文件的发现由 `FAngelscriptEngine` 上游负责：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 函数: FAngelscriptEngine::FindAllScriptFilenames
// 性质: 把 AllRootPaths 里的每个目录递归扫一遍 *.as 收齐
// ============================================================================
void FAngelscriptEngine::FindAllScriptFilenames(TArray<FFilenamePair>& OutFilenames)
{
    const bool bSkipDevelopmentScripts = !ShouldUseEditorScripts();
    const bool bSkipEditorScripts      = bSkipDevelopmentScripts;

    for (auto& Path : AllRootPaths)
    {
        FindScriptFiles(
            IFileManager::Get(),
            TEXT(""),
            Path,
            TEXT("*.as"),                  // ★ 只扫 .as 后缀
            OutFilenames,
            bSkipDevelopmentScripts,
            bSkipEditorScripts);
    }
}
```

`AllRootPaths` 由 `MakeAllScriptRoots` / `DiscoverScriptRoots` 在 `InitialCompile` 之前构建，包含三类目录：

| 来源 | 典型路径 | 触发条件 |
|------|---------|---------|
| 项目根 | `<Project>/Script` | 始终包含 |
| 引擎根 | `<Engine>/Script` | 始终包含 |
| 插件根 | `<Plugin>/Script` | 由 `Dependencies.GetEnabledPluginScriptRoots()` 枚举所有启用插件 |

注意：**只识别 `.as` 后缀**——不存在 `.asu` / `.asi` 之类的扩展类型。所有脚本文件都被一视同仁地走完整预处理流程；"模块"的概念由文件路径决定（详见 §1.3）。

### 1.2 `AddFile` 的入队动作

```cpp
// ============================================================================
// 文件: Preprocessor/AngelscriptPreprocessor.cpp
// 函数: FAngelscriptPreprocessor::AddFile
// ============================================================================
void FAngelscriptPreprocessor::AddFile(const FString& RelativeFilename,
                                       const FString& AbsoluteFilename,
                                       bool bLoadAsynchronous, bool bTreatAsDeleted)
{
    if (!ensureMsgf(!bIsPreprocessed, TEXT("Cannot add files after preprocessing is done.")))
        return;

    FFile& File = Files.AddDefaulted_GetRef();

    TSharedRef<FAngelscriptModuleDesc> Module = MakeShared<FAngelscriptModuleDesc>();
    Module->ModuleName = FilenameToModuleName(RelativeFilename);   // ★ 一文件一 Module

    File.Module             = Module;
    File.AbsoluteFilename   = AbsoluteFilename;
    File.RelativeFilename   = RelativeFilename;
    // ... 同步 / 异步加载 RawCode（见后）
}
```

三个关键约束：

- **预处理阶段一旦开始，不允许再 `AddFile`**——`bIsPreprocessed` 是单调旗标。
- **每个 `.as` 文件对应独立的 `FAngelscriptModuleDesc`**——不存在"多个文件合并成一个模块"。
- **`bTreatAsDeleted = true`** 时不读盘，`RawCode` 留空字符串，相当于"该模块即将变成空"，HotReload 删除文件路径会走这条。

### 1.3 文件名 → 模块名

```cpp
// ============================================================================
// 文件: Preprocessor/AngelscriptPreprocessor.cpp
// 函数: FAngelscriptPreprocessor::FilenameToModuleName
// ============================================================================
FString FAngelscriptPreprocessor::FilenameToModuleName(const FString& Filename)
{
    FString NormalizedFilename = Filename.Replace(TEXT("\\"), TEXT("/"));
    NormalizedFilename.RemoveFromEnd(TEXT(".as"));
    return NormalizedFilename.Replace(TEXT("/"), TEXT("."));        // ★ '/' → '.'
}
```

例：相对路径 `Gameplay/Combat/Weapon.as` → 模块名 `Gameplay.Combat.Weapon`。这个名字被 AS 内核当作模块全局唯一标识符（`asCModule::name`），也是 `import Gameplay.Combat.Weapon;` 语法的目标。

### 1.4 同步 vs 异步读盘

`AddFile(..., bLoadAsynchronous = false)` 走带重试的同步读盘：

```cpp
// 同上文件，AddFile 函数内同步分支节选
int32 Tries = 0;
for (; Tries < 6; ++Tries)
{
    if (FFileHelper::LoadFileToString(File.RawCode, *AbsoluteFilename))
    { bLoaded = true; break; }
    if (Tries >= 4) FPlatformProcess::Sleep(0.2f);
    else if (Tries >= 3) FPlatformProcess::Sleep(0.1f);
    else if (Tries >= 2) FPlatformProcess::Sleep(0.01f);
}
if (!bLoaded)
    UE_LOG(Angelscript, Warning, TEXT("Unable to open script file %s after several retries. Treating file as deleted."), *AbsoluteFilename);
```

6 次重试 + 渐进 backoff 是为对抗 HotReload 时编辑器还在写盘的竞争窗口（IDE 保存 → 文件系统通知触发 reload → 文件还没 close）。

异步路径（`bLoadAsynchronous = true`）见 `PerformAsynchronousLoads`，在 `Preprocess()` 入口处统一拉齐再继续：

```cpp
// ============================================================================
// 文件: Preprocessor/AngelscriptPreprocessor.cpp
// 函数: FAngelscriptPreprocessor::Preprocess（开头节选）
// ============================================================================
bool FAngelscriptPreprocessor::Preprocess()
{
    if (!ensureMsgf(!bIsPreprocessed, TEXT("Can only preprocess once."))) return false;
    bIsPreprocessed = true;

    if (bLoadingAnyFilesAsynchronous)
        PerformAsynchronousLoads();     // ★ 拉齐 RawCode 再开始

    for (FFile& File : Files) ParseIntoChunks(File);
    // ...（后续 8 步见概览图）
}
```

---

## 二、与 AS 内核 `CScriptBuilder` 的分工

| 维度 | 上游 AS `CScriptBuilder` add-on | 本插件 `FAngelscriptPreprocessor` |
|------|-------------------------------|-----------------------------------|
| `#include "file.as"` | 文件包含+去重，是该 add-on 的**核心功能** | **明确禁用**：`#include` 直接报错 `"Unsupported preprocessor directive '#include'. Use import or automatic imports instead."` |
| 模块导入 | 无（`import` 是 AS 关键字层面的事） | 显式 `import Mod.Name;` 解析为 `FImport`，并触发拓扑排序 |
| 条件编译 `#if/#ifdef/#else/#endif` | 支持简单 flag 查表 | 支持 `#if / #ifdef / #ifndef / #elif / #else / #endif`，flag 来源 `FAngelscriptPreprocessorContext::PreprocessorFlags` |
| 元数据 `[metadata]` | 自带语法 | 完全弃用，改用 UE 风格的 `UPROPERTY()` / `UFUNCTION()` / `UCLASS()` / `USTRUCT()` / `UENUM()` / `UMETA()` |
| 文档注释 | 无统一抽取 | `///` 与 `/** */` 由状态机捕获，`FormatCommentForToolTip` 归一化后挂到 `Meta[ToolTip]` |
| `default Foo = X` | N/A | 自定义语法，单独捕获到 `ClassDesc->DefaultsCode` |
| `event` / `delegate` 关键字 | N/A | 转写为 `struct ... { Broadcast/Execute/BindUFunction/... }` 模式 |
| `f"..."` 格式串 / `n"..."` Name 字面量 | N/A | 后处理替换 |
| Range-based for `for (T x : c)` | N/A | 后处理改写为 `for (auto _It = c.Iterator(); _It.CanProceed; )` |
| `asset Foo of UTexture2D` | N/A | 后处理生成全局 `TSoftObjectPtr<>` 引用 |

**结论**：本插件**完全替换**了 `CScriptBuilder` 的所有职责，并在其之上叠加了 UE 反射所需的全部元数据收集。AS 内核 `asCModule::AddScriptSection` 只看到一段已经"清洗 + 元数据无关 + 条件编译已剪枝"的纯 AS 源码。

`#include` 被禁用而非沉默忽略，是有意的策略——读到 `#include` 直接 `LineError`，迫使作者使用 `import` 或自动 import 模式：

```cpp
// 节选自 ParseIntoChunks 内 '#' 分支
else if ((RawSize - ChunkEnd) >= 8
    && FCString::Strncmp(&File.RawCode[ChunkEnd], TEXT("#include"), 8) == 0
    && (ChunkEnd + 8 >= RawSize || IsWhitespace(File.RawCode[ChunkEnd + 8])))
{
    KillRawLine(File, ChunkEnd);
    if (!HasInactiveIfDef(false))
    {
        LineError(File, LineNumber, TEXT("Unsupported preprocessor directive '#include'. Use import or automatic imports instead."));
        bHasError = true;
    }
}
```

---

## 三、`ParseIntoChunks` 字符级状态机

`ParseIntoChunks` 是本预处理器**最核心的一个 ~1000 行函数**（cpp 行 3096–4168），把磁盘文本切成若干 `FChunk`（global / class / struct / enum 四种类型）并伴随地收集 import / delegate / macro / defaults 等附属信息。

### 3.1 状态机骨架

```text
[变量]
  bInComment / bInLineComment / bInBlockComment / bInString
  IfDefStack[]            ← #if / #ifdef / #ifndef / #else 栈
  bIfDefStackIsFalse      ← 当前是否身处一个被剪枝分支
  ScopeCount              ← 当前 { } 嵌套深度
  BracketCount            ← 当前 ( ) 嵌套深度
  ChunkType ∈ {Global, Class, Struct, Enum}
  ChunkStart / ChunkEnd / ChunkLine
  PendingMacros[] / PendingReplaces[] / PendingDefaults[]
  NamespaceStack[]
  PrevCommentStart / PrevCommentEnd     ← 上一段紧邻注释的位置
  bIsParsingMacro                       ← 当前是否在 UPROPERTY/UFUNCTION 内部
  ChunkComment                          ← 待赋给下一个 chunk 的注释

[主循环]
  while (ChunkEnd < RawSize):
     Char = File.RawCode[ChunkEnd]

     if Char == '#' and not in comment/string:
        switch on directive (#ifdef / #ifndef / #if / #elif / #else / #endif
                            / #include[禁] / #restrict usage allow|disallow)

     if bIfDefStackIsFalse and Char != whitespace:
        File.RawCode[ChunkEnd] = ' '       ★ 把被剪枝的字符就地替换为空格

     switch on Char:
        case 'c':  → 是不是 "class "?    若是 SubmitChunk(); ChunkType = Class
        case 's':  → 是不是 "struct "?
        case 'e':  → 是不是 "enum " / "event " / "delegate "?
        case 'i':  → 是不是 "import "?
        case 'U':  → 是不是 "UPROPERTY(" / "UFUNCTION(" / "UCLASS(" / "USTRUCT("
                                / "UENUM(" / "UMETA("?
        case 'd':  → 在 class 内的 "default "
        case 'n':  → "namespace " / n"" Name 字面量
        case 'f':  → f"" 格式字面量
        case '/':  → 注释开头
        case '*':  → 块注释结尾候选
        case '"':  → 进/出字符串
        case '{':  ScopeCount++; PrevCommentStart = -1
        case '}':  ScopeCount--; （若 EnumChunk: ParseEnumValue()；
                                   若 ScopeCount == ClassExitScope: SubmitChunk(true);
                                                                    ChunkType = Global）
        case '(': BracketCount++（macro 状态机）
        case ')': BracketCount--（macro 状态机）
        case ';': （macro 收尾候选）
        case '\n': LineNumber++

     ChunkEnd++

  SubmitChunk(false);                    ← 文件末尾的最后一个 global chunk
  if (IfDefStack 非空) → "missing #endif"
```

字符级状态机（而非词法器/AST）是这套预处理器的关键决策——每个分支都试图保留 `RawCode` 的字节位置不变，便于后续 `Macro.NameStartPos` / `Chunk.ChunkStartPos` 这类绝对偏移有用武之地。

### 3.2 Chunk 切分边界

类型是 `FChunk::EChunkType` 之一：`Global` / `Class` / `Struct` / `Enum`。切分点：

- **进入** Class/Struct/Enum：识别到关键字 + top-level 作用域 + `IsStartOfIdentifier()`，先 `SubmitChunk(false)` 把当前 global chunk 收口，再开新 chunk。
- **退出** Class/Struct/Enum：`}` 撞上之前记下的 `ClassExitScope`，`SubmitChunk(true)` 把整个 `class { ... }` 收成一个 chunk，再回到 `ChunkType = Global`。

```cpp
// 节选自 ParseIntoChunks 内 'c' / class 分支
if ((RawSize - ChunkEnd) >= 6
    && FCString::Strncmp(&File.RawCode[ChunkEnd], TEXT("class"), 5) == 0
    && IsWhitespace(File.RawCode[ChunkEnd+5])
    && IsTopLevelScope() && !bInString && !bInComment
    && IsStartOfIdentifier())
{
    SubmitChunk(false);                              // ★ 先把前面的 global chunk 提交
    if (PrevCommentStart != -1)                      // ★ 把紧邻注释挂到下一个 chunk
    {
        ChunkComment = File.RawCode.Mid(PrevCommentStart, PrevCommentEnd-PrevCommentStart);
        PrevCommentStart = -1;
    }
    ClassIfDefs   = IfDefStack;                      // 记录 class 整体处于哪些 #if 下
    ChunkType     = EChunkType::Class;
    ChunkLine     = LineNumber;
    ClassExitScope = ScopeCount;                     // 等 ScopeCount 回到这个值时收 chunk
}
```

### 3.3 `SubmitChunk` 的语义

```cpp
// 节选自 ParseIntoChunks 内的 SubmitChunk lambda
auto SubmitChunk = [&](bool bIncludeCurrentCharInChunk)
{
    int SubmitEnd = ChunkEnd + (bIncludeCurrentCharInChunk ? 1 : 0);
    if (ChunkStart == SubmitEnd) return;

    int32 ChunkIndex = File.ChunkedCode.Add();
    FChunk& Chunk = File.ChunkedCode[ChunkIndex];
    Chunk.Type           = ChunkType;
    Chunk.Content        = File.RawCode.Mid(ChunkStart, SubmitEnd-ChunkStart);
    Chunk.FileLineNumber = ChunkLine;
    Chunk.ChunkStartPos  = ChunkStart;
    Chunk.ChunkEndPos    = SubmitEnd;

    Chunk.Namespace = NamespaceStack.Num() > 0
        ? TOptional<FString>(FString::Join(NamespaceStack, TEXT("::")))
        : TOptional<FString>();

    Chunk.Comment = ChunkComment.TrimStartAndEnd();
    ChunkComment.Reset();

    // 把 PendingMacros / Replaces / Defaults 的偏移从 file-绝对 转成 chunk-相对
    Chunk.Macros.Append(PendingMacros);
    for (auto& Macro : Chunk.Macros)
    {
        Macro.NameStartPos  -= ChunkStart;
        Macro.NameEndPos    -= ChunkStart;
        Macro.MacroStartPos -= ChunkStart;
        Macro.MacroEndPos   -= ChunkStart;
    }
    PendingMacros.Empty();
    Chunk.Replacements.Append(PendingReplaces); PendingReplaces.Empty();
    Chunk.Defaults.Append(PendingDefaults);     PendingDefaults.Empty();

    ChunkStart = SubmitEnd;
};
```

注意几点：

- `Content` 是 RawCode 的拷贝切片——后续 chunk 可以独立修改自己的内容（`ReplaceWithBlank` / `ReplaceInChunk`），不影响 RawCode。
- 偏移转换是关键：宏在 RawCode 里记的是绝对偏移，落到 Chunk 后转为相对偏移，便于 `Chunk.Content[Macro.NameStartPos]` 直接索引。
- `Namespace` 是当前 namespace 栈的 `::` 拼接快照——chunk 里所有声明都默认带这个命名空间。

### 3.4 Macro 识别与 `bIsParsingMacro` 子状态

`UPROPERTY(...)` / `UFUNCTION(...)` 这类宏跨多行，状态机用一个嵌入子状态机处理：

```cpp
// 节选自 ParseIntoChunks 内 'U' 分支
if (FCString::Strncmp(&File.RawCode[ChunkEnd], TEXT("UPROPERTY("), 10) == 0
    && (ChunkType == EChunkType::Class || ChunkType == EChunkType::Struct))
{
    bIsParsingMacro = true;
    ParsingMacro.Type = EMacroType::Property;
}
else if (FCString::Strncmp(&File.RawCode[ChunkEnd], TEXT("UFUNCTION("), 10) == 0
    && (ChunkType != EChunkType::Enum))
{
    bIsParsingMacro = true;
    ParsingMacro.Type = EMacroType::Function;
}
else if (ChunkType == EChunkType::Global)
{
    bool bIsClass  = FCString::Strncmp(&File.RawCode[ChunkEnd], TEXT("UCLASS("),  7) == 0;
    bool bIsStruct = FCString::Strncmp(&File.RawCode[ChunkEnd], TEXT("USTRUCT("), 8) == 0;
    bool bIsEnum   = FCString::Strncmp(&File.RawCode[ChunkEnd], TEXT("UENUM("),   6) == 0;
    if (bIsClass || bIsStruct)
    {
        // ★ 立即就地解析括号里的 specifiers，因为后面跟着的是类名
        PendingClassMacro = FMacro();
        PendingClassMacro.Type = EMacroType::Class;
        int32 Offset = bIsClass ? 7 : 8;
        int32 CloseBracket = FindScopeCloseBracket(File.RawCode, ChunkEnd + Offset - 1);
        if (CloseBracket != -1)
        {
            PendingClassMacro.MacroStartPos = ChunkEnd;
            PendingClassMacro.MacroEndPos   = CloseBracket + 1;
            PendingClassMacro.Arguments     = File.RawCode.Mid(ChunkEnd + Offset,
                                                                CloseBracket - ChunkEnd - Offset);
            bHasClassMacro = true;
            ChunkEnd = CloseBracket;
        }
    }
}
```

`UPROPERTY` / `UFUNCTION` 走"延迟收口"：进入子状态机后由 `(`、`)`、`;`、`=`、`{` 这些后续字符触发 `FinishMacro()` 把名字、类型从宏后面的代码反向读出来。`UCLASS` / `USTRUCT` / `UENUM` 走"立即就地解析"：因为下一个 token 是类名而非完整声明，立即用 `FindScopeCloseBracket` 把括号匹配掉就好。

### 3.5 命名空间栈

```cpp
// 节选自 'n' 分支：namespace 关键字
const FString NamespaceName = ...;                               // 解析出名字
const int32 NamespaceTokenPos = FindNextNamespaceToken(NamespaceIdentifierEnd);
if (NamespaceName.IsEmpty() || NamespaceTokenPos == INDEX_NONE
    || File.RawCode[NamespaceTokenPos] != '{')
{
    LineError(File, LineNumber, TEXT("Invalid namespace declaration, expected '{' after namespace name."));
    bHasError = true;
}
else
{
    NamespaceStack.Add(NamespaceName);                           // ★ 入栈
}
// ...同步处 '}' 配对时 NamespaceStack.Pop()
```

栈深度由 `IsTopLevelScope()` 判定时纳入考虑：`ScopeCount <= NamespaceStack.Num()` 才算 top-level——也就是 `namespace Foo { class Bar { } }` 的 `class Bar` 仍然被识别为 top-level chunk，但其 `Chunk.Namespace = "Foo"`。

---

## 四、条件编译：`#if` 系列与 `IfDefStack`

### 4.1 支持的指令清单

| 指令 | 形式 | 行为 |
|------|------|------|
| `#ifdef X` | flag X 是否被定义为 `true` | 字符就地变空格策略剪枝 |
| `#ifndef X` | flag X 是否未定义/为 `false` | 同上 |
| `#if EXPR` | 调用 `ParsePreProc` 解析（支持 `!flag`） | 同上 |
| `#elif EXPR` | 当前 `#if` 链的 else-if | 同上 |
| `#else` | 当前 `#if` 链的 else | 同上 |
| `#endif` | 关闭最近的 `#if/ifdef/ifndef` | 弹栈 |
| `#include` | 不支持，立即 `LineError` | 沉默剪掉本行后报错 |
| `#restrict usage allow PATTERN` | 只允许 `PATTERN` 匹配的模块导入本模块（仅 WITH_EDITOR） | 写入 `Module->UsageRestrictions[]` |
| `#restrict usage disallow PATTERN` | 反向 | 同上 |

`PreprocessorFlags` 的来源：

```cpp
// ============================================================================
// 文件: Preprocessor/AngelscriptPreprocessor.cpp
// 函数: FAngelscriptPreprocessorContext::CreateFromCurrentEngineContext
// 性质: 全局 flag 注入入口——决定哪些 #ifdef/X 为真
// ============================================================================
Context.PreprocessorFlags.Add(TEXT("EDITOR"),          FAngelscriptEngine::ShouldUseEditorScriptsForCurrentContext());
Context.PreprocessorFlags.Add(TEXT("EDITORONLY_DATA"), WITH_EDITORONLY_DATA && (!IsRunningGame() && !IsRunningDedicatedServer())
                                                       || FAngelscriptEngine::ShouldUseEditorScriptsForCurrentContext());
Context.PreprocessorFlags.Add(TEXT("COOK_COMMANDLET"), IsRunningCookCommandlet());
Context.PreprocessorFlags.Add(TEXT("RELEASE"),         UE_BUILD_SHIPPING || UE_BUILD_TEST);
Context.PreprocessorFlags.Add(TEXT("TEST"),            !UE_BUILD_SHIPPING);
Context.PreprocessorFlags.Add(TEXT("WITH_SERVER_CODE"),WITH_SERVER_CODE);

const UAngelscriptSettings* AngelscriptSettings = GetDefault<UAngelscriptSettings>();
// ... 还可以从 FAngelscriptEngine::ConfigSettings 改写
for (auto& Flag : AngelscriptSettings->PreprocessorFlags)
    Context.PreprocessorFlags.Add(Flag, true);                 // ★ 项目自定义 flag
```

也就是说：

- `#ifdef EDITOR` 在编辑器构建里为真，cook commandlet / dedicated server / shipping 里默认为假。
- 项目可在 `UAngelscriptSettings::PreprocessorFlags` 里追加自己的 flag（如 `MULTIPLAYER`、`DEBUG_BUILD`），这些 flag 一旦出现即为 `true`。
- 没有任何 AS-side `#define` 语法——`#define` **完全不支持**。注入 flag 的唯一入口是 C++ 配置。

### 4.2 `ParsePreProc`：极简表达式解析

```cpp
// ============================================================================
// 文件: Preprocessor/AngelscriptPreprocessor.cpp
// 函数: FAngelscriptPreprocessor::ParsePreProc
// 性质: 仅支持单 token + 可选 '!' 前缀
// ============================================================================
bool FAngelscriptPreprocessor::ParsePreProc(FFile& File, int32 LineNumber, const FString& PreProc)
{
    FString Lookup = PreProc;
    bool bNegate = false;
    if (PreProc.Len() != 0 && PreProc[0] == '!')
    {
        bNegate = true;
        Lookup = Lookup.Mid(1);
    }

    bool* bValue = PreprocessorFlags.Find(Lookup);
    if (bValue == nullptr)
    {
        LineError(File, LineNumber, FString::Printf(TEXT("Invalid preprocessor condition: %s"), *PreProc));
        bHasError = true;
        return false;
    }
    return *bValue != bNegate;
}
```

关键限制：

- **不支持 `&&` / `||` / `()` 等组合表达式**——一个 `#if` 只能查一个 flag。
- **不支持数值比较**（如 `#if VERSION >= 2`）。
- 未声明的 flag 直接报错，不会被当作 `false` 沉默处理（这与 C 预处理器不同）。

### 4.3 `IfDefStack` 与字符就地擦除

```cpp
// 节选自 ParseIntoChunks 内 #ifdef 分支
FString Identifier = ReadIdentifier(File, ChunkEnd + 6);
const bool bValue = !HasInactiveIfDef(false) && PreprocessorFlags.FindRef(Identifier);
KillRawLine(File, ChunkEnd);                                  // ★ 把整行变空格
IfDefStack.Push({bValue, bValue, false, LineNumber, Identifier});
UpdateIfDefStack();
UpdateEditorBlockLines();
```

- `bValue = !HasInactiveIfDef(false) && ...`：嵌套 `#if` 时若**外层**已经是 `false`，内层不评估、视为 `false`，但仍然入栈以保持平衡。这避免了"无效分支里的 flag 不存在导致报错"。
- `KillRawLine` 把当前指令行从 `#` 写到 `\n` 之间所有非空白字符替换为空格，使得 AS 内核看到的是空白，但**行号不变**——这样后续诊断时报告的 `LineNumber` 仍然指向原始 `.as` 的物理行。
- `bIfDefStackIsFalse` 由 `UpdateIfDefStack` 维护。一旦它为真，主循环每读一个非空白字符就立即把它就地改成空格——**剪枝就是擦除**，被剪掉的代码不会出现在任何 chunk 里。

### 4.4 `#elif` / `#else` 的"已选分支"语义

```cpp
// 节选自 #elif 分支
const bool bShouldEvaluate = !IfDefStack.Last().bAnyBranchTaken && !HasInactiveIfDef(true);
const bool bValue = bShouldEvaluate && ParsePreProc(File, LineNumber, PreProc);
IfDefStack.Last().DirectiveLine = LineNumber;
IfDefStack.Last().Condition     = PreProc;

if (IfDefStack.Last().bAnyBranchTaken)
    IfDefStack.Last().bValue = false;        // ★ 之前已有分支选过，本分支强制 false
else
{
    IfDefStack.Last().bValue = bValue;
    if (bValue) IfDefStack.Last().bAnyBranchTaken = true;
}
```

`bAnyBranchTaken` 是 C 风格 `#if/#elif/#else` 的标准实现：`#if A` 真则后续 `#elif`/`#else` 全部为假；`#if A` 假但 `#elif B` 真则继续后续 `#elif`/`#else` 全部为假。`#endif` 把整个栈条目弹掉。

### 4.5 `EDITOR` / `EDITORONLY_DATA` 的特殊待遇

```cpp
// 节选自 ParseIntoChunks 内的 UpdateEditorBlockLines lambda
bool bHasEditorIfDef = false;
for (auto& IfDef : IfDefStack)
{
    if (IfDef.Condition == TEXT("EDITOR") || IfDef.Condition == TEXT("EDITORONLY_DATA"))
    {
        bHasEditorIfDef = true;
        break;
    }
}
if (bHasEditorIfDef && !bIsEditorOnlyBlock)
{
    bIsEditorOnlyBlock = true;
#if WITH_EDITOR
    File.Module->EditorOnlyBlockLines.Add(TPair<int,int>(LineNumber, -1));
#endif
}
else if (!bHasEditorIfDef && bIsEditorOnlyBlock)
{
    bIsEditorOnlyBlock = false;
#if WITH_EDITOR
    File.Module->EditorOnlyBlockLines.Last().Value = LineNumber;
#endif
}
```

每次 `IfDefStack` 变化时检查是否进入/离开 EDITOR 块，把 `(开始行, 结束行)` 记到 `Module->EditorOnlyBlockLines`，之后传给 AS 内核 `builder->SetEditorOnlyBlockLinePositions`，让 AS 编译器知道哪些行是 editor-only——便于在非 editor 构建里发出"editor-only 代码引用了非 editor 类"这类警告。

另一个细节是 `UPROPERTY` / `UFUNCTION` 不允许放在 `EDITOR/EDITORONLY_DATA` 之外的任意 `#if` 分支里：

```cpp
// 节选自 'U' 分支收尾处
for (int i = ClassIfDefs.Num(), Count = IfDefStack.Num(); i < Count; ++i)
{
    const bool bIsEditorData       = IfDefStack[i].Condition == TEXT("EDITOR") || IfDefStack[i].Condition == TEXT("EDITORONLY_DATA");
    const bool bIsPreprocessorFlag = PreprocessorFlags.Contains(IfDefStack[i].Condition) && PreprocessorFlags[IfDefStack[i].Condition];
    if (!bIsEditorData && !bIsPreprocessorFlag)
    {
        LineError(File, LineNumber, TEXT("Cannot put a UPROPERTY or UFUNCTION inside preprocessor conditions other than EDITOR or flags declared in configuration."));
        bHasError = true;
    }
}
```

原因：UPROPERTY/UFUNCTION 反映到 UE 反射树后会影响 CDO 布局；如果反射结构在不同 build 间漂移，会击穿网络复制 / 序列化的 schema 一致性。

---

## 五、注释收集与 ToolTip 归一化

### 5.1 收集策略

`ParseIntoChunks` 在每次进入注释时记下 `PrevCommentStart`、退出时记下 `PrevCommentEnd`：

```cpp
// 节选自主循环的 '/' 与 '*' 分支
case '/':
    if ((RawSize - ChunkEnd) >= 2 && !bInComment && !bInString)
    {
        if (NextChar == '/') { bInLineComment = true; bInComment = true; PrevCommentStart = ChunkEnd; }
        else if (NextChar == '*') { bInBlockComment = true; bInComment = true; PrevCommentStart = ChunkEnd; }
    }
break;
case '*':
    if (bInBlockComment && NextChar == '/')
    { bInBlockComment = false; bInComment = false; PrevCommentEnd = ChunkEnd + 2; }
break;
case '\n':
    if (bInLineComment) { bInLineComment = false; bInComment = false; PrevCommentEnd = ChunkEnd; }
    ++LineNumber;
break;
```

注释一旦"消费"——挂到下一个 chunk / macro / enum value——`PrevCommentStart = -1` 清零。**任何会"中断"的字符（`{` 进入新作用域、`;` 语句结束）都会清掉 `PrevCommentStart`**：

```cpp
case '{': PrevCommentStart = -1; ScopeCount++; break;
case ';': PrevCommentStart = -1; if (bIsParsingMacro && ...) FinishMacro(); break;
```

这意味着只有"紧邻"目标的注释才会被认领——中间有空语句/分号就会丢失。

### 5.2 `///` vs `/* */` vs `/** */`

`Helper_CommentFormat.h::FormatCommentForToolTip` 抄自 `FHeaderParser`，能处理三种风格并归一化：

```cpp
// ============================================================================
// 文件: Preprocessor/Helper_CommentFormat.h
// 函数: FormatCommentForToolTip（节选）
// 性质: 与 UE C++ HeaderTool 同源的注释清洗，确保编辑器 tooltip 行为一致
// ============================================================================
const bool bJavaDocStyle = Result.Contains(TEXT("/**"));
const bool bCStyle       = Result.Contains(TEXT("/*"));
const bool bCPPStyle     = Result.StartsWith(TEXT("//"));

if (bJavaDocStyle || bCStyle)
{
    Result = Result.Replace(TEXT("/**"), TEXT(""));
    Result = Result.Replace(TEXT("/*"),  TEXT(""));
    Result = Result.Replace(TEXT("*/"),  TEXT(""));
}
if (bCPPStyle)
{
    // 把三斜杠 /// 视作两斜杠 //，再统一去掉
    Result = Result.Replace(TEXT("///"), TEXT("//")).Replace(TEXT("//"), TEXT(""));
    Result = Result.Replace(TEXT("(cpptext)"), TEXT(""));
}
// 去 \r、tab 转空格、剥 javadoc 行首 *、首行最大空白对齐... (略)
```

特别处理两类注释**会被丢弃而不是保留**：

- `/*~ ... */` 块注释（"忽略我"标记）
- `//~ ...` 行注释

这与 UE C++ HeaderTool 行为完全一致——文档作者在 .as 里加 `//~ FIXME: ...` 不会污染编辑器 tooltip。

### 5.3 注释最终落到哪里

| 位置 | 字段 | 何处使用 |
|------|------|---------|
| Chunk.Comment | 紧邻 class/struct/enum 关键字之前的注释 | `DetectClasses` 里 `ClassDesc->Meta.Add(ToolTip, ...)`（仅 WITH_EDITOR） |
| Macro.Comment | 紧邻 UPROPERTY/UFUNCTION 之前的注释 | `ProcessFunctionMacro` / `ProcessPropertyMacro` 写入 `FunctionDesc->Meta` / `PropDesc->Meta` |
| Macro.Comment（EnumValue 类型） | 紧邻枚举值之前的注释 | `ParseEnumValue` 里建一个 `EMacroType::EnumValue` 假宏，最终成为该 enum value 的 tooltip |

注意：注释**不**进 `asITypeInfo::SetUserData` 之类的 AS 内核字段——它们只通过 UE 反射的 `Meta` 元数据系统消费。这是有意的：AS 内核完全不关心文档，只关心字节码。

### 5.4 文档注释的边界

- 没有 Doxygen 风格的 `@param` / `@return` 等指令解析——`FormatCommentForToolTip` 只做"清洁文本"。
- 没有跨语言的脚本反查——AS 脚本端**不能**在运行时拿到自己的注释字符串。注释的最终消费者是编辑器 UI（属性面板的 tooltip、蓝图节点的描述等），通过 UE 反射 `Meta[ToolTip]` 系统。

---

## 六、`import` 解析与拓扑排序

### 6.1 `import` 关键字识别

```cpp
// 节选自 ParseIntoChunks 内 'i' / import 分支
auto IsModuleIdentifierChar = [](TCHAR Char) -> bool
{
    return (Char >= 'a' && Char <= 'z') || (Char >= 'A' && Char <= 'Z')
        || (Char >= '0' && Char <= '9') || Char == '_' || Char == '.';
};
int32 ModuleStart = ChunkEnd + 7;
while (ModuleStart < File.RawCode.Len() && IsWhitespace(File.RawCode[ModuleStart])) ModuleStart += 1;
int32 ModuleEnd = ModuleStart;
while (ModuleEnd < File.RawCode.Len() && IsModuleIdentifierChar(File.RawCode[ModuleEnd])) ModuleEnd += 1;

const int32 SemicolonPos = ModuleEnd > ModuleStart
    ? FindSemicolonDirectlyAfter(File.RawCode, ModuleEnd - 1) : -1;

if (SemicolonPos != -1)
{
    FImport ImportDesc;
    ImportDesc.StartPosInChunk = ChunkEnd - ChunkStart;
    ImportDesc.EndPosInChunk   = SemicolonPos - ChunkStart;
    ImportDesc.ChunkIndex      = File.ChunkedCode.Num();        // ← 即将进入的下一个 chunk
    ImportDesc.ModuleName      = File.RawCode.Mid(ModuleStart, ModuleEnd-ModuleStart).TrimStartAndEnd();
    ImportDesc.FileLineNumber  = LineNumber;
    File.Imports.Add(ImportDesc);
}
```

注意：还存在另一种 `import "..." from "..."` 形态用于声明外部 C 函数（`bLooksLikeDeclaredFunctionImport`），此处仅检测但不构造 `FImport`，让 AS 内核自己处理。

### 6.2 `ProcessImports`：拓扑排序与循环检测

```cpp
// ============================================================================
// 文件: Preprocessor/AngelscriptPreprocessor.cpp
// 函数: FAngelscriptPreprocessor::ProcessImports
// 性质: DFS 拓扑排序 + 循环检测；只在 manual import 模式下重排文件
// ============================================================================
void FAngelscriptPreprocessor::ProcessImports(FFile& File, TArray<FFile>& OutSortedFiles, FImportChain* PrevChain)
{
    if (File.bImportsResolved) return;

    if (File.bIsResolvingImports)
    {
        FileWideError(File, FString::Printf(TEXT("Detected circular import of module %s. Import chain:"), *File.Module->ModuleName));
        while (PrevChain != nullptr)
        {
            FileWideError(File, FString::Printf(TEXT("   => %s"), *PrevChain->File->Module->ModuleName));
            PrevChain = PrevChain->Previous;
        }
        bHasError = true; return;
    }

    FImportChain Chain { &File, PrevChain };
    File.bIsResolvingImports = true;

    for (FImport& ImportDesc : File.Imports)
    {
        FFile* ProcessingModule = nullptr;
        for (FFile& OtherFile : Files)
            if (OtherFile.Module->ModuleName == ImportDesc.ModuleName)
            { ProcessingModule = &OtherFile; break; }

        if (ProcessingModule != nullptr)
            ProcessImports(*ProcessingModule, OutSortedFiles, &Chain);    // ★ 递归

        File.Module->ImportedModules.AddUnique(ImportDesc.ModuleName);
        ReplaceWithBlank(File.ChunkedCode[ImportDesc.ChunkIndex],
                         ImportDesc.StartPosInChunk, ImportDesc.EndPosInChunk + 1);  // ★ 抠掉 import 行
    }

    File.bImportsResolved = true;
    OutSortedFiles.Add(File);
}
```

特点：

- **循环检测**用 DFS 染色（`bIsResolvingImports`），错误时把整条链路依次报告。
- **import 行原地擦除**（变空格）——AS 内核看不到 `import` 关键字，模块依赖只通过 `Module->ImportedModules` 字符串数组表达，由后续编译阶段调用 `asIScriptModule::SetImports(...)` 设置。
- **没有深度限制**——栈深由文件数量决定；递归深度过深可能 stack overflow，但实际项目数千 .as 也不至于触发。

### 6.3 manual vs automatic import 模式

```cpp
// 节选自 Preprocess()
if (!bUseAutomaticImportMethod)
{
    // 显式模式：根据 import 语句重排文件，使依赖方在被依赖方之后处理
    TArray<FFile> SortedFiles;
    for (FFile& File : Files) ProcessImports(File, SortedFiles, nullptr);
    Files = SortedFiles;
}
else
{
    // 自动模式：不重排文件顺序，但仍要走一遍 ProcessImports 来收集警告/兼容元数据/擦除 import 行
    TArray<FFile> CompatibilityPassScratch;
    for (FFile& File : Files) ProcessImports(File, CompatibilityPassScratch, nullptr);
}
```

| 模式 | 触发条件 | 行为 |
|------|---------|------|
| Manual | `UAngelscriptSettings::bUseAutomaticImportMethod = false` | 必须显式 `import X;`，文件被重排 |
| Automatic | `bUseAutomaticImportMethod = true`（默认更现代化） | 编译器后续自动推断依赖；显式 `import` 语句被警告 + 擦除 |

自动模式下出现显式 `import` 时：

```cpp
if (bUseAutomaticImportMethod && bWarnOnManualImportStatements)
    LineWarning(File, ImportDesc.FileLineNumber, TEXT("Automatic imports are active, import statements will be ignored."));
```

无论哪种模式，`Module->ImportedModules` 都会被填充——HotReload 的反向依赖图（"谁 import 了我？"）依赖这个字段，详见 `RT_HotReload.md`。

---

## 七、模块描述符 `FAngelscriptModuleDesc` 的字段语义

### 7.1 字段全景

```cpp
// ============================================================================
// 文件: Core/AngelscriptEngine.h
// 节选: FAngelscriptModuleDesc（位于行 1275–1397，共 ~120 行，关键字段如下）
// ============================================================================
struct FAngelscriptModuleDesc
{
    FString ModuleName;                              // ★ "Gameplay.Combat.Weapon"

    struct FCodeSection { FString RelativeFilename; FString AbsoluteFilename;
                          FString Code;             int64 CodeHash; };
    TArray<FCodeSection> Code;                       // ★ 处理后字符代码（一个 Module 一段）

    int64 CodeHash               = 0;                // 本模块代码的 XXH64
    int64 CombinedDependencyHash = 0;                // 自身 + 所有 import 链上模块的复合 hash

    TArray<TSharedRef<FAngelscriptClassDesc>>    Classes;
    TArray<TSharedRef<FAngelscriptEnumDesc>>     Enums;
    TArray<TSharedRef<FAngelscriptDelegateDesc>> Delegates;

    TArray<FString> ImportedModules;                 // ★ 拓扑依赖
    TArray<FString> PostInitFunctions;               // 后初始化钩子函数名

#if WITH_EDITOR
    struct FUsageRestriction { bool bIsAllow; FString Pattern; };
    TArray<FUsageRestriction>  UsageRestrictions;    // #restrict usage 收集到这里
    TArray<TPair<int,int>>     EditorOnlyBlockLines; // [(start, end)] 方便诊断 EDITOR 块
#endif

    class asCModule* ScriptModule = nullptr;         // 编译后绑回 AS 内核模块
    const struct FAngelscriptPrecompiledModule* PrecompiledData = nullptr;
    bool bCompileError = false;
    bool bLoadedPrecompiledCode = false;
    bool bModuleSwapInError = false;

    TMap<FString, FAngelscriptTestDesc> UnitTestFunctions;
    TMap<FString, FAngelscriptTestDesc> IntegrationTestFunctions;

    TSharedPtr<FAngelscriptClassDesc> GetClass(const FString& ClassName);
    TSharedPtr<FAngelscriptClassDesc> GetClass(asITypeInfo* Type);
    TSharedPtr<FAngelscriptEnumDesc>  GetEnum(const FString& EnumName);
    // ... 测试相关查找略
};
```

### 7.2 字段填充时机

| 字段 | 填充阶段 | 填充者 |
|------|---------|--------|
| `ModuleName` | `AddFile` 时 | `FilenameToModuleName` |
| `Code[]` | `Preprocess` 收尾 | `Preprocess()` 末尾 emplace `FCodeSection` + `XXH64` |
| `CodeHash` / `CombinedDependencyHash` | 同上（CodeHash） / 编译阶段（Combined） | `Preprocess` / 编译流水线 |
| `Classes` / `Enums` / `Delegates` | `DetectClasses` / `DetectEnum` / `ProcessDelegates` | 内部 chunk 扫描 |
| `ImportedModules` | `ProcessImports` | DFS 拓扑序 |
| `UsageRestrictions` | `#restrict usage` 指令 | `ParseIntoChunks` 内 `#` 分支 |
| `EditorOnlyBlockLines` | `UpdateEditorBlockLines` lambda | 每次 IfDefStack 变化 |
| `ScriptModule` | `CompileModule_Types_Stage1` | AS 内核 `asCEngine::GetModule` 后绑回 |
| `bCompileError` | 编译四阶段任一失败 | 编译流水线 |
| `UnitTestFunctions` / `IntegrationTestFunctions` | `DiscoverUnitTests` / `DiscoverIntegrationTests`（仅 WITH_EDITOR） | 编译完成后扫描全模块函数 |

预处理阶段产出的字段就是 §概览图右侧那个方框里列出的——AS 端 `ScriptModule`、`bCompileError` 等都是后续阶段填的。

### 7.3 `Code` 与 `GeneratedCode` 的关系

`FFile::GeneratedCode` 是 chunk 之外、由预处理器**合成**追加的代码（如自动生成的 `Spawn` / `Get` 静态方法、event/delegate 的 `struct` 包装体、`StaticType` 全局变量等）。`CondenseFromChunks` 将其拼到 `ProcessedCode` 末尾：

```cpp
// ============================================================================
// 文件: Preprocessor/AngelscriptPreprocessor.cpp
// 函数: FAngelscriptPreprocessor::CondenseFromChunks
// 性质: 把所有 chunk + 所有 GeneratedCode 一起拼成 File.ProcessedCode
// ============================================================================
void FAngelscriptPreprocessor::CondenseFromChunks(FFile& File)
{
    int32 CodeSize = 0;
    for (auto& Chunk : File.ChunkedCode) CodeSize += Chunk.Content.Len();
    for (FString& Generated : File.GeneratedCode) CodeSize += Generated.Len() + 2;
    File.ProcessedCode.Reset(CodeSize);

    for (auto& Chunk : File.ChunkedCode)
    {
        ProcessReplacements(File, Chunk);          // ★ 应用所有 stream replace
        File.ProcessedCode += Chunk.Content;
    }

    for (FString& Generated : File.GeneratedCode)
    {
        File.ProcessedCode += TEXT("\n\n");
        File.ProcessedCode += Generated;
    }
}
```

之后在 `Preprocess()` 末尾：

```cpp
for (FFile& File : Files)
{
    FAngelscriptModuleDesc::FCodeSection Section;
    Section.RelativeFilename = File.RelativeFilename;
    Section.AbsoluteFilename = File.AbsoluteFilename;
    Section.Code             = File.ProcessedCode;
    Section.CodeHash         = 0;
    if (File.ProcessedCode.Len() > 0)
    {
        Section.CodeHash      = XXH64(&File.ProcessedCode[0], File.ProcessedCode.Len() * sizeof(TCHAR), 0);
        File.Module->CodeHash ^= Section.CodeHash;
    }
    File.Module->Code.Emplace(MoveTemp(Section));
}
```

`CodeHash` 用 XXH64 算 `ProcessedCode` 的字节哈希——**包含 GeneratedCode**。HotReload 会用它判定"语义上是否真的变了"，避免做无意义的重编译。

### 7.4 处理后代码进入 AS 内核

```cpp
// ============================================================================
// 文件: Core/AngelscriptEngine.cpp
// 节选自: FAngelscriptEngine::CompileModule_Types_Stage1
// 性质: 唯一一处把预处理产物喂给 AS 内核的入口
// ============================================================================
for (auto& Section : Module->Code)
{
    ScriptModule->AddScriptSection(
        TCHAR_TO_ANSI(*Section.AbsoluteFilename),
        TCHAR_TO_UTF8(*Section.Code),
        0, 0);                              // ★ 无 lineOffset、无 codeLength
}
```

注意点：

- **AbsoluteFilename 用 ANSI**——AS 内核内部用作 section name，进而显示在编译错误里。Unicode 路径会乱码（这是 AS 的限制）。
- **代码用 UTF-8**——AS 编译器底层处理 UTF-8。`FString` 是 UTF-16，转换由 `TCHAR_TO_UTF8` 完成。
- **`lineOffset = 0`**——因为预处理时所有指令行都被替换为空白而非删除，行号天然守恒，不需要做偏移补偿。这是 §4 那个"`KillRawLine` 字符就地变空格"设计的核心收益。

---

## 八、错误回流到 Diagnostics

预处理器**不抛异常**。所有错误都通过 6 个 helper 写入 `FAngelscriptEngine::ScriptCompileError`：

```cpp
// ============================================================================
// 文件: Preprocessor/AngelscriptPreprocessor.cpp
// 函数: FAngelscriptPreprocessor::LineError（其余 5 个变体类似）
// 性质: 与 AS 编译错误共享同一个 ScriptCompileError 通道
// ============================================================================
void FAngelscriptPreprocessor::LineError(FFile& File, int32 Line, const FString& Message)
{
    FAngelscriptEngine::FDiagnostic Diagnostic;
    Diagnostic.Message  = Message;
    Diagnostic.Row      = Line;
    Diagnostic.Column   = 1;
    Diagnostic.bIsError = true;
    Diagnostic.bIsInfo  = false;
    FAngelscriptEngine::Get().ScriptCompileError(File.AbsoluteFilename, Diagnostic);
}
```

| Helper | Row | Column | 典型用例 |
|--------|-----|--------|---------|
| `FileWideError` | 1 | 1 | 循环 import |
| `FileWideWarning` | 1 | 1 | （罕用） |
| `LineError` | 给定 | 1 | `#if X` 中 X 未定义、`#include` 出现、`import` 缺分号 |
| `LineWarning` | 给定 | 1 | 自动模式下出现显式 `import`、自动模式 import 语句的兼容警告 |
| `ChunkError` | Chunk.FileLineNumber | 1 | 重复 class 名、struct 试图继承 |
| `MacroError` | Macro.FileLineNumber | 1 | UFUNCTION specifier 冲突、global UFUNCTION 不能 BlueprintEvent 等 |

错误一经记录，`bHasError` 即置位，但**预处理不会立即 return**——会继续走完所有阶段以收集尽可能多的错误（除非 `Preprocess()` 在每个 step 之间显式 `if (bHasError) return false`）。这种"尽量批量上报"的设计使一次重编译能让作者看到所有问题，而不是一行一改。

具体 step 间检查：

```cpp
// 节选自 Preprocess()
if (bHasError) return false;                            // import 阶段失败
for (...) DetectClasses(File);
if (bHasError) return false;                            // detect 阶段失败
for (...) AnalyzeClasses(File);
for (...) ProcessMacros(File);
for (...) ProcessDelegates(File);
// ...
for (...) for(...) ProcessDefaults(File, Chunk);
if (bHasError) return false;                            // ★ 宏/默认值失败 fail-closed
                                                        //    避免后续阶段消费坏 ClassDesc
for (...) CondenseFromChunks(File);
```

`if (bHasError) return false;` 出现在三个关键检查点——它们的位置经过仔细考虑：早期出错（ProcessImports / DetectClasses）就停，避免后续阶段在错误状态下产出垃圾描述符；ProcessMacros / ProcessDefaults 失败也要 fail-closed，因为下游 CodeGen 会基于 `ClassDesc->Methods` / `Properties` / `DefaultsCode` 生成代码。

错误最终通过 `Arch_ErrorDiagnostics.md` 描述的链路飘到编辑器面板与日志。

---

## 九、增量预处理与 HotReload

### 9.1 单文件改动如何触发什么

```cpp
// ============================================================================
// 文件: Core/AngelscriptEngine.cpp
// 函数: FAngelscriptEngine::PerformHotReload（节选）
// ============================================================================
FAngelscriptPreprocessor Preprocessor;

TSet<FFilenamePair> FilesToHotReload;
// ... 1. 把直接修改的文件加入 FilesToHotReload
// ... 2. 通过 ScriptModule->moduleDependencies 反向追溯所有依赖于这些文件的模块
// ... 3. 把这些被传染的模块对应的全部 FCodeSection 文件加入 FilesToHotReload

for (const auto& PathPair : FilesToHotReload)
{
    const bool bTreatAsDeleted = AlreadyDeletedFiles.Contains(PathPair);
    Preprocessor.AddFile(PathPair.RelativePath, PathPair.AbsolutePath, bTreatAsDeleted);
}

bool bPreprocessSuccess = Preprocessor.Preprocess();
if (!bPreprocessSuccess)
{
    UE_LOG(Angelscript, Error, TEXT("Hot reload failed in preprocessing. Keeping all old angelscript code."));
    PreviouslyFailedReloadFiles.Append(FileList);
    EmitDiagnostics();
    return false;
}

TArray<TSharedRef<FAngelscriptModuleDesc>> CompiledModules;
ECompileResult Result = CompileModules(CompileType, Preprocessor.GetModulesToCompile(), CompiledModules);
```

关键观察：

- **`FAngelscriptPreprocessor` 实例每次 HotReload 都是新建的**——没有跨次调用复用的状态。这避免了"上次预处理留下的 Files / PreprocessingClasses 污染下次"。
- **不是全量重处理**——只处理被改动文件 + 它们的反向依赖闭包。哪些算反向依赖详见 `RT_HotReload.md` §四。
- **失败时全失败**——一个文件错误导致整个 HotReload 中断，旧的 AS 代码继续运行；失败的文件标记到 `PreviouslyFailedReloadFiles`，下次 HotReload 自动一起重试。

### 9.2 与 Precompiled Data 的关系

```cpp
// 节选自 InitialCompile（行 2282）
if (PrecompiledData != nullptr && bUsePrecompiledData && !bScriptDevelopmentMode)
{
    bUsedPrecompiledDataForPreprocessor = true;
    ModulesToCompile = PrecompiledData->GetModulesToCompile();   // ★ 跳过预处理器
}
else
{
    AllRootPaths = MakeAllScriptRoots();
    FAngelscriptPreprocessor Preprocessor;
    FindAllScriptFilenames(Filenames);
    for (FFilenamePair& Filename : Filenames)
        Preprocessor.AddFile(Filename.RelativePath, Filename.AbsolutePath);
    bSuccess = Preprocessor.Preprocess();
    ModulesToCompile = Preprocessor.GetModulesToCompile();
}
```

Shipping/Test build + `PrecompiledScript_Shipping.Cache` 存在时，**预处理器整个被跳过**——`FAngelscriptModuleDesc` 直接从 `.Cache` 文件反序列化。这意味着：

- Shipping 构建运行时**不存在** `.as` 文件解析、不存在条件编译、不存在 import 拓扑——所有这些都在 cook 阶段完成。
- Shipping 构建里 `FAngelscriptPreprocessor` 仍然是链接进来的代码（不会因 `WITH_EDITOR` 被排除），但运行时不被实例化。
- Shipping 构建一旦缺 `.Cache` 会回退走预处理器 + 全量编译——通常这意味着开发模式 / 调试构建。

---

## 十、关键设计决策与边界

### 10.1 为什么不复用 AS 上游的 `CScriptBuilder`？

| 需求 | `CScriptBuilder` 是否提供 | 本预处理器的回应 |
|------|--------------------------|------------------|
| UE 风格 `UPROPERTY()` / `UFUNCTION()` 元数据 | 无（它用 `[name = value]`） | 自己实现 specifier parser |
| `EditorOnlyBlockLines` 行号集 | 无 | 自己跟踪 IfDefStack |
| `default Foo = X` 语法 | 无（这是插件自创语法） | 自己捕获 + 后处理 |
| `event` / `delegate` 关键字 → 包装 struct | 无 | 自己生成 GeneratedCode |
| `f"..."` 格式串 / `n"..."` Name 字面量 | 无 | 自己后处理 |
| Range-based for / `asset Foo of T` | 无 | 自己后处理 |
| 与 UE FProperty 反射对接 | 无 | 自己生成 `FAngelscriptClassDesc` |

`CScriptBuilder` 的设计目标是 standalone 嵌入式脚本宿主，与 UE 反射、UPROPERTY、HotReload 这些都没关系。把它"扩展"到能做这些事，工作量与重写一个不相上下，并且会引入"上游升级时同步改动"的维护负担。所以选择**整体替代**。

### 10.2 字符就地擦除策略

被剪枝的代码（被 `#if false` 包围、被识别为 import 行、被识别为 UMETA 包装）一律就地变空格，而非从字符串里删除。代价：

- **优点**：所有后续偏移、行号、AS 编译错误的 row 号都自动准确，不需要任何 lineOffset 表。
- **缺点**：处理后的字符数量与原始文件持平，内存占用偏高（数百 KB 文件 × 数千文件 ≈ 数百 MB）。
- **缓解**：只在内存中临时存在，`Preprocess()` 完成后 `FFile::RawCode` 字段不再被使用（`Module->Code` 已经持有 `ProcessedCode` 的副本，预处理器实例可被销毁）。

### 10.3 一文件 = 一模块

每个 `.as` 即一个 AS 模块。这与 AS 上游"多个 section 可以放进一个 module"的能力相反。代价：

- **优点**：HotReload 颗粒度天然就是文件级；模块依赖图天然由 `import` 表达；模块名与文件路径一一对应、便于工具链。
- **缺点**：跨文件共享不暴露给当前文件的 helper 函数会产生"额外 import"——但 automatic import 模式可以隐藏这一点。

### 10.4 预处理器**不**做的事

- **不做语法验证**：除了极少数 chunk 边界（`}` 配对、`;` 缺失）和 specifier 相关错误，绝大部分语法错误（缺分号、错误的表达式、类型不匹配）都交给 AS 内核去发现。
- **不做类型推断**：`ResolveSuperClass` 仅做"基类字符串 → `UClass*`"的映射；变量、表达式类型是 AS 编译器的活。
- **不做绑定数据库查询**：`FAngelscriptType::GetByAngelscriptTypeName` 是只读消费——预处理器从不写入 type database。
- **不做后端代码生成**：`AngelscriptClassGenerator` 才是把 `FAngelscriptClassDesc` 翻译成 `UClass*` 的人；预处理器只填充 ClassDesc 字段。

---

## 附录 A：预处理指令与关键字速查

### A.1 条件编译

| 指令 | 形式 | 备注 |
|------|------|------|
| `#ifdef X` | `#ifdef EDITOR` | flag 定义为 true |
| `#ifndef X` | `#ifndef RELEASE` | flag 未定义/为 false |
| `#if X` | `#if !RELEASE` | 支持 `!flag`，**不**支持 `&&` `\|\|` `()` |
| `#elif X` | `#elif TEST` | 同 `#if`，已选分支后均为 false |
| `#else` | `#else` | 配对最近一个未关闭的 `#if/ifdef/ifndef` |
| `#endif` | `#endif` | 弹栈 |
| `#include` | **禁用** | 报错 `Use import or automatic imports instead.` |
| `#restrict usage allow PATTERN` | 仅限 EDITOR 编译 | 写入 `Module->UsageRestrictions` |
| `#restrict usage disallow PATTERN` | 同上 | 反向限制 |

### A.2 内置 flag

| Flag | 触发条件 |
|------|---------|
| `EDITOR` | 编辑器构建 + 当前 context 启用 editor scripts |
| `EDITORONLY_DATA` | `WITH_EDITORONLY_DATA` + 非游戏 / 非 dedicated server |
| `COOK_COMMANDLET` | `IsRunningCookCommandlet()` |
| `RELEASE` | `UE_BUILD_SHIPPING \|\| UE_BUILD_TEST` |
| `TEST` | `!UE_BUILD_SHIPPING` |
| `WITH_SERVER_CODE` | `WITH_SERVER_CODE` |
| `<自定义>` | `UAngelscriptSettings::PreprocessorFlags` 列出的全部 flag 自动为 true |

### A.3 UE 反射宏

| 宏 | 上下文 | 必须紧跟 |
|------|------|---------|
| `UCLASS(...)` | 全局作用域 | `class XxxClass : Yyy` |
| `USTRUCT(...)` | 全局作用域 | `struct XxxStruct` |
| `UENUM(...)` | 全局作用域 | `enum XxxEnum` |
| `UPROPERTY(...)` | class/struct 内 | 字段声明 |
| `UFUNCTION(...)` | class/struct/global | 函数声明 |
| `UMETA(...)` | enum 内 | 枚举值之后 |

### A.4 自定义关键字

| 关键字 | 上下文 | 含义 |
|------|------|------|
| `import Mod.Sub.Name;` | 全局 | 显式模块导入；automatic 模式下被警告 |
| `default Foo = X;` | class 内 | 写入 `ClassDesc->DefaultsCode`，CDO 阶段执行 |
| `delegate ReturnType DelegateName(...)` | 全局 | 单播代理 |
| `event ReturnType EventName(...)` | 全局 | 多播代理 |
| `f"... {expr} ..."` | 任意 | 格式字符串，后处理展开 |
| `n"name"` | 任意 | `FName` 字面量 |
| `for (Type x : container)` | 任意 | 后处理展开为 Iterator 循环 |
| `asset Foo of UTexture2D` | 全局 | 后处理生成 `TSoftObjectPtr<>` |

### A.5 内部数据结构

| 结构 | 字段（关键） | 作用 |
|------|------------|------|
| `FAngelscriptPreprocessor::FFile` | RawCode / ChunkedCode / Imports / Delegates / GeneratedCode / ProcessedCode / Module | 一文件一份 |
| `FAngelscriptPreprocessor::FChunk` | Type / Content / Comment / Macros / Replacements / Defaults / ClassDesc / Namespace | class/struct/enum/global 切片 |
| `FAngelscriptPreprocessor::FMacro` | Type / Comment / Arguments / Name / SubjectType / NameStartPos / FileLineNumber / bEditorOnly | UPROPERTY / UFUNCTION / UCLASS / UMETA / EnumValue |
| `FAngelscriptPreprocessor::FImport` | ModuleName / ChunkIndex / FileLineNumber | 一条 `import` 语句 |
| `FAngelscriptPreprocessor::FDelegateDesc` | bIsMulticast / ChunkIndex / BracketPos / FileLineNumber | 一条 `delegate` / `event` |
| `FAngelscriptPreprocessor::FStreamReplace` | StartPos / EndPos / Replacement | 字符串级替换队列（f-string、name literal） |
| `FAngelscriptPreprocessor::FDefaultsCode` | StartPos / NewStartPos | `default Foo = X` 行的位置 |

---

## 附录 B：调试与避坑

### B.1 grep 起点

| 想搞清楚什么 | grep 入口 |
|------------|---------|
| 一个 `#ifdef` 是怎么剪枝的 | `KillRawLine` / `bIfDefStackIsFalse` / `IfDefStack` |
| 一个 `import X.Y` 是怎么解析的 | `case 'i':` 在 `ParseIntoChunks` / `ProcessImports` |
| `///` 注释最终去哪 | `PrevCommentStart` / `ChunkComment` / `Macro.Comment` / `FormatCommentForToolTip` |
| 一段 `event` 被翻译成什么 | `ProcessDelegates` |
| 一个 `default Foo = X` 怎么被收集的 | `case 'd':` + `default` 关键字 / `ProcessDefaults` |
| 报错信息为什么报到这一行 | 从 `LineError` / `ChunkError` / `MacroError` 反追 row 字段来源 |
| 自动注入的 `Spawn` / `Get` 是从哪冒出来的 | `AnalyzeClasses` 内 `IsChildOf(AActor::StaticClass())` 等分支 |
| `FCodeSection` 的 hash 怎么算 | `Preprocess()` 末尾的 `XXH64(...)` |
| `EditorOnlyBlockLines` 怎么填的 | `UpdateEditorBlockLines` lambda |
| `#include` 报错改不改 | `case '#': ... TEXT("#include") ...` |

### B.2 常见坑

1. **新增 flag 不生效**：必须加到 `UAngelscriptSettings::PreprocessorFlags`（项目设置）或 `FAngelscriptPreprocessorContext::CreateFromCurrentEngineContext`（C++ 内置 flag），AS 端无任何注入入口。
2. **`#if FLAG && OTHER` 永远报错**：表达式只支持单 token + 可选 `!`；要组合得拆成嵌套 `#if`。
3. **未声明 flag 在 `#if` 中是错而非假**：会触发 `Invalid preprocessor condition: X`，必须在 settings 里登记或写成 `#ifdef X`（`#ifdef` 容忍未声明，按 false 处理）。
4. **`UPROPERTY` 放进自定义 flag `#if` 是允许的，但放进任意条件不行**：限制是"必须是 EDITOR/EDITORONLY_DATA 或 settings 中显式 true 的 flag"。
5. **注释与目标之间不能有空 `;`**：分号会清掉 `PrevCommentStart`，注释丢失。
6. **`/*~ ... */` 与 `//~ ...` 是 ignore 注释**：会被 `FormatCommentForToolTip` 整段剥离；要写"自己看的备注又不进 tooltip"用这个。
7. **CJK 注释会被丢弃**：`FormatCommentForToolTip` 检查 `IsAlnum || (Char > 0xFF)`——只有非字母数字 + 非高位 unicode 时返回空字符串。纯中文注释会保留（高位 unicode），但 CJK 标点 + ASCII 标点的混合可能不稳定。
8. **`import X;` 缺分号会报错**：但 `import "lib.dll" from "module";` 这种声明 C 函数的形态会被识别为 `bLooksLikeDeclaredFunctionImport` 而免责。
9. **同名 class 跨模块冲突**：`DetectClasses` 检查 `FAngelscriptEngine::GetClass(ClassName)`，已存在则报错并把这个 class 从 Module 中移除。HotReload 自旧模块换出时这一检查放行（属于"重新预处理同一个模块"）。
10. **HotReload 失败导致一组文件全军覆没**：预处理失败会把整批 reload 文件标记 `PreviouslyFailedReloadFiles`，下次任意一个 .as 改动都会一起重试。
11. **`FilenameToModuleName` 大小写敏感**：Windows 文件系统不敏感，但 `ModuleName` 在 AS 内核里大小写敏感。`Foo/Bar.as` 与 `foo/bar.as` 会被当成不同模块——确保 `import` 语句与文件路径大小写一致。
12. **预处理后用 `XXH64` 哈希包含 GeneratedCode**：所以仅改注释不会变 hash（注释最终落 Meta 而不进 ProcessedCode），但改 UPROPERTY specifier 会变 hash。
13. **Shipping 跳过预处理**：调试 .as 编译问题时如果用 Shipping + 已有 `.Cache`，必须删 `.Cache` 或加 `-as-development-mode` 才能复现。
14. **跨 .as 引用的 helper 函数自动可见**：automatic import 模式下，AS 编译器从 type database 自动找——所以"我没写 import 怎么也能用"是预期行为。
15. **`#restrict usage` 是 WITH_EDITOR 限定**：Shipping 构建里这两条指令完全不写入 `UsageRestrictions`，限制只在编辑期生效。

### B.3 性能粗略估算

| 阶段 | 复杂度 | 备注 |
|------|------|------|
| 文件读盘 | O(总字节数) | 同步路径有重试；异步路径并行 |
| `ParseIntoChunks` | O(总字节数) | 单遍线性扫描 |
| `ProcessImports` | O(文件数 × 平均 import 数) | DFS + 染色 |
| `DetectClasses` / `AnalyzeClasses` | O(class 数 × 平均 chunk 长度) | 包含正则匹配 |
| `ProcessMacros` | O(宏数 × 平均 specifier 数) | specifier 解析是字符级 |
| `CondenseFromChunks` | O(总字节数) | replacements 排序 + 拼接 |
| `PostProcess*` | O(ProcessedCode 总长) | regex 全文扫描 |

实测在中型项目（数千 `.as` ≈ 数十 MB）上预处理总耗时通常 < 2s（`FAngelscriptScopeTimer(TEXT("preprocessing"))` 包了整个 `Preprocess()`）。HotReload 单文件改动一般 < 100ms。

---

## 小结

- **整体替换 `CScriptBuilder` add-on**：UE 风格 `UPROPERTY/UFUNCTION` + `EDITOR` 块 + `default` 语法 + `delegate/event` + 注释 → ToolTip 等需求一律由本预处理器自实现，AS 内核只接收一段干净的字符代码。
- **字符级单遍状态机**：`ParseIntoChunks` 一次扫描完成 chunk 切分、注释收集、import/delegate 识别、宏 + specifier 解析、namespace 跟踪——所有分支共享同一份 IfDefStack / ScopeCount / BracketCount / NamespaceStack 状态。
- **就地擦除策略**：被剪枝的字符替换为空格而非删除——行号、列号、字符位置全部守恒，AS 编译错误的报告位置直接复用原始 `.as` 行号，无需 lineOffset 表。
- **一文件一模块**：`FilenameToModuleName(rel)` 把相对路径变成模块名，每个 .as 对应一个 `FAngelscriptModuleDesc`；HotReload 颗粒度天然是文件级；模块依赖图来自 `import` 关键字的 DFS 拓扑序。
- **错误尽量批量上报**：6 个 helper 把所有错误写入 `ScriptCompileError`，`bHasError` 在三个关键检查点检查 fail-closed，避免坏描述符飘到 ClassGeneration 阶段。
- **Precompiled 路径直接跳过本层**：Shipping/Test 加上 `.Cache` 时预处理器整个不实例化——所以 Shipping 构建里看到的 .as 调试问题往往要回到 cook 阶段去复现。
