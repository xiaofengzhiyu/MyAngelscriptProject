# 工作区 / Worktree / OpenSpec 盘点

> 最后更新：2026-05-24（worktree 全量清理后）  
> 用途：主 workspace 与 OpenSpec 变更、子模块分支的一页式对照表。  
> 维护：每次新建/删除 worktree 或归档 OpenSpec 变更后同步更新。

---

## 1. 主 Workspace（`D:/Workspace/AngelscriptProject`）

| 项 | 状态 |
|----|------|
| 分支 | `main` @ `5a926fb` |
| Worktree | **仅 main**（无额外 worktree） |
| 相对 origin | ahead 53（未 push） |
| 子模块 `Plugins/Angelscript` | `679704f` — test-coverage + direct-bind WIP 已合入 |
| 子模块 `Plugins/UnrealEvent` | `caf1e9a` — GMP runtime layout pruning WIP 已合入 |
| 子模块 `Plugins/AngelscriptGAS` | `2120a8f`（未改动） |

### 2026-05-24 清理摘要

已从以下 worktree 合入 WIP 并删除全部 worktree：

| 来源 | 合入内容 |
|------|----------|
| `openspec-test-coverage-pack-v2` | Bindings/Functional 测试、`Bind_FString.cpp`、Memory/GC 测试、Test 文档 |
| `improve-as-direct-bind-coverage-fix` | Syntax/StaticJIT 测试增量 |
| `refactor-unrealevent-gmp-runtime-layout` | UnrealEvent GMP runtime 文件大规模 prune |

**有意跳过**：`refactor-as-gameplaytags-optional-plugin`（子模块 git 损坏、基于旧提交，风险过高）。OpenSpec proposal 仍保留，后续可从 main 重建 worktree。

**Patch 备份**：`Saved/Patches/worktree-cleanup-20260524/`（本地，不提交）

### Deferred（可后续慢慢修）

- v2 WIP 基于 `0fa6d02` 合入到 `24d1572`（clone-removal）之上，部分文件通过直接复制合入；构建/测试验证待做
- `refactor-as-engine-clone-removal` OpenSpec tasks 仍 0/42，但子模块 clone-removal 已实现 → 需核对 tasks 后归档
- `Plugins/AngelscriptGAS` 本地 checkout 仍在 `improve-as-direct-bind-coverage-fix` 分支，父 gitlink 未变

---

## 2. Git Worktree

| Worktree | 状态 |
|----------|------|
| `D:/Workspace/AngelscriptProject` | **唯一 active workspace** |

无 `.worktrees/` 目录。需要隔离开发时，从 main 新建单个 worktree 即可。

---

## 3. OpenSpec 活跃变更（`openspec/changes/`，不含 archive）

### 3.1 进行中

| 变更 | tasks | 说明 |
|------|-------|------|
| `test-bindings-gap-closure` | 0/4 | Bindings 测试覆盖（WIP 已合入 main 子模块，tasks 待勾选） |
| `test-functional-runtime-coverage` | 0/5 | Functional 主题 placeholder → 可执行断言 |
| `test-editor-and-runtime-diagnostics-coverage` | 0/8 | Editor/Runtime 诊断覆盖 |
| `refactor-unrealevent-prune-gmp-editor-modules` | 2/10 | UnrealEvent editor 模块 prune |

### 3.2 待核对/归档

| 变更 | 说明 |
|------|------|
| `refactor-as-engine-clone-removal` | 子模块已实现，tasks 文档 stale |

### 3.3 Proposal-only（排队中）

按建议优先级：

1. `refactor-as-gameplaytags-optional-plugin`
2. `refactor-as-audit-remove-with-angelscript-haze`
3. `improve-as-direct-bind-coverage`
4. `refactor-as-bind-eliminate-previously-bound-function`
5. `refactor-debugger-protocol-v2`（需复核 scope）
6. 其余：`refactor-code-coverage-data-export`、`refactor-angelscript-test-helper-api`、`refactor-as-library-full-namespaces`、`feature-commandflow-plugin`、`docs-wiki-repository-publishing-plan`

### 3.4 近期已归档（2026-05-23）

- `improve-static-jit-diagnostics`
- `refactor-as-engine-owned-hooks`
- `refactor-as-engine-extension-hooks`

---

## 4. 推荐开发入口

直接在 **main workspace** 继续以下变更：

- `test-bindings-gap-closure` + `test-functional-runtime-coverage` + `test-editor-and-runtime-diagnostics-coverage`（test-coverage 三连）
- `refactor-unrealevent-prune-gmp-editor-modules`

需要隔离时再执行：

```powershell
git worktree add .worktrees/<change-name> -b <branch-name>
powershell.exe -NoProfile -ExecutionPolicy Bypass -File Tools\Bootstrap\powershell\BootstrapWorktree.ps1 -EngineRoot "<EngineRoot>" -NoPrewarm
```

详见 [`SubmoduleWorktreeWorkflow.md`](SubmoduleWorktreeWorkflow.md)。
