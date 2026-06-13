# Retrospective — feature-as-virtual-script-paths

> 开发过程与经验记录。本文件随 change 一起归档，作为后续同类工作（命名重构、
> 跨子模块改动、虚拟路径扩展）的参考。

## 1. 这个 change 做了什么

给 AngelScript 脚本源引入了一层 UE 风格的虚拟源身份（`/Angelscript/...`），
取代旧的 `as://...` URI 方案。核心交付：

- 三类规范路径：`/Angelscript/Game/...`、`/Angelscript/Plugin/<Name>/...`、
  `/Angelscript/Memory/<Provider>/...`，其中 `/Memory/Immediate` 预留给未来实时片段。
- 值类型层（`AngelscriptSource.h`）：`FAngelscriptVirtualPath`、`FAngelscriptSource`、
  `FAngelscriptSourceRoot`、`EAngelscriptSourceKind`。
- 描述符感知的入口：`FAngelscriptPreprocessor::AddSource()`、
  `FAngelscriptEngine::FindAllScriptSources()`，旧的 `AddFile()` /
  `FindAllScriptFilenames()` 退化为兼容适配器。
- 可观测性：`VirtualPath` 贯穿 file summary、module code section，
  内存源用虚拟路径作为编译器 section 名。
- 编辑器热重载队列从同一套有效根描述符派生虚拟路径，保住插件身份。

## 2. 关键设计决策（为什么这样做）

- **用 `/Angelscript` 专用根，而非伪装成 `/Game` 包路径。** 它只是 AS 源身份，
  不注册 `FPackageName` mount point，避免和 UE 包系统语义冲突。
- **v1 刻意保守，只做源身份，不做片段执行。** `/Memory/Immediate` 占坑但不暴露
  执行 API，把风险留给独立的后续 change。
- **模块名兼容性优先。** 插件源在 v1 仍用根相对模块名（插件名不进模块名），
  避免破坏已扫描插件 `Script/` 根的 import。插件前缀迁移是单独的 migration-aware change。
- **加法式改动，旧 API 全部保留为适配器。** 62+ 处测试的 `AddFile` 调用零修改，
  `FindAllScriptFilenames` 自动获得 `VirtualPath` 字段。

## 3. 命名重构经验（本次会话的主线）

初版类型名有 `Script` 词根冗余：前缀 `Angelscript` 已含 "script"，再叠 `Script`
读作 "Angel-script script source"。统一去冗余：

| 旧名 | 新名 |
|------|------|
| `EAngelscriptScriptSourceKind` | `EAngelscriptSourceKind` |
| `FAngelscriptScriptSource` | `FAngelscriptSource` |
| `FAngelscriptVirtualScriptPath` | `FAngelscriptVirtualPath` |
| `FAngelscriptScriptRoot` | `FAngelscriptSourceRoot` |

随后文件名与命名空间也对齐：`AngelscriptScriptSource.{h,cpp}` →
`AngelscriptSource.{h,cpp}`，`AngelscriptScriptSource_Private` → `AngelscriptSource_Private`。

### 教训与方法

- **标识符 vs 自然语言要分清。** 改类型名时，错误字符串里的 "Virtual script path
  must end with .as"、测试套件主题 `VirtualScriptPaths`、feature 名
  `feature-as-virtual-script-paths` 都是**概念性自然语言**，与类型名无关，
  保留才一致。只改标识符。
- **用全词边界（`\b`）替换，避免误伤更长标识符。** `FAngelscriptScriptRoot` 是测试类名
  `FAngelscriptScriptRootDiscoveryProjectRoot...Test` 的前缀；`\bFAngelscriptScriptRoot\b`
  后接 `D` 不构成词边界，所以测试类名被正确保留——这些是独立标识符，且和套件主题名一致。
- **改名前先 dry-run 命中数 + 确认新名未被占用。** 替换后再 grep 残留旧名 + 双重替换
  错误（`SourceSource`/`VirtualVirtual`），双向验证。
- **文件改名用 `git mv` 保留历史。** 自引用 include 和命名空间在 mv 后单独改。

### 刻意未改的项（保持命名节奏）

- 方法名 `FromMemorySource`、`FindAllScriptSources`：对应 `FromGameFile`/`FromPluginFile`
  的命名节奏（Memory 源无 File 概念），改了反而打乱对称。

## 4. 提交策略经验

- **hardening 改动先单独提交，让 rename 的 diff 保持纯净。** 本地预存的
  `TryFromMemorySource` memory-root 校验（task 6.2）先单独入库，再做 rename。
- **子模块在前，父仓 gitlink 在后。** 双仓改动的铁律：先提交 `Plugins/Angelscript`，
  再在父仓提交 gitlink bump。
- **rename 分两笔：类型名一笔，文件名一笔。** 每笔独立构建验证，diff 清晰可回滚。
- **只提交任务相关文件。** 父仓里会话前就存在的 `.gitignore`/`AGENTS.md` 等改动不碰。

本 change 相关提交链（子模块）：

```
<file-rename>  Refactor: rename AngelscriptScriptSource files to AngelscriptSource
<type-rename>  Refactor: drop redundant Script from virtual source type names
<hardening>    Test: fail closed when memory source uses non-Memory virtual path
```

## 5. 验证

- 每次 rename 后都跑 `Tools\RunBuild.ps1` 完整重建编辑器目标，
  确认 `Result: Succeeded` + 退出码 0（5 个模块 DLL 全链接）。
- 焦点测试前缀：`Angelscript.TestModule.{FileSystem,Preprocessor,Compiler}.VirtualScriptPaths.*`、
  `Angelscript.Editor.DirectoryWatcher.*`。

### 踩坑

- **后台构建用相对脚本路径会失败。** PowerShell 工具的 cwd 可能停在子模块
  `Plugins/Angelscript`，`-File Tools\RunBuild.ps1` 找不到。
  **用绝对路径** `D:\Workspace\AngelscriptProject\Tools\RunBuild.ps1`。

## 6. 已知局限与后续工作（design.md trade-off）

- 插件模块名还没反映插件前缀 → 不同插件下同相对路径脚本模块名会撞。
  需独立的 migration-aware change。
- `/Angelscript/Memory/...` 还不能反查回 provider buffer → 内存源无法"跳转到文件"。
- `/Angelscript/Memory/Immediate` 实时片段执行：源身份/诊断/调试器 section 已铺好，
  是最有价值的下一步。
