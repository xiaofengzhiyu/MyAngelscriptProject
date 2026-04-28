# Ralph Loop 差距 Todo

**目标：** 说明当前 `ralph-loop` 工具距离预期的“薄壳式 provider 驱动循环器”目标还有多远，并把剩余差距整理成一份可执行的 todo 清单。

**当前状态：** 这个仓库现在已经具备项目真正需要的薄壳式 provider-driven loop：`ralph-loop.ps1` 负责重复调用 agent，`.bat` 负责传入提示词和运行参数，`codex` / `opencode` 通过 provider profile 接入。当前主要剩余项是在具备真实 CLI 和凭据的环境里做一次真实端到端验证。

**技术栈：** PowerShell 5+/7、Windows batch、Codex CLI、OpenCode CLI

---

## 快照

### 已完成

- `ralph-loop.ps1` 已经能够执行迭代循环、渲染 prompt、捕获 stdout/stderr、持久化 run state，并在 verify 成功后停止。
- `stop-hook.ps1` 保持了 agent-neutral 的 verify 契约：`0` 停止、`1` 继续、`>1` 报错。
- `run-ralph-loop.bat` 仍然是薄 wrapper，并且已经优先使用 `pwsh`。
- `tests/test-ralph-loop.ps1` 已经用 mocks 覆盖了当前 codex 风格的 MVP。
- 超时行为已经实现并验证过，包括进程树终止和退出码 `124`。
- 最终日志文件可以保持 UTF-8 内容，即使控制台路径本身并不总是可靠。

### 部分完成

- 真实 agent 验证仍然保持 opt-in，因为它依赖本地环境里已安装并已鉴权的 CLI。

### 仍然缺失

- 在当前工作区里以 `RALPH_TEST_REAL_AGENTS=1` 实际执行一次真实 `codex` / `opencode` 验证。

## 能力矩阵

| 能力 | 状态 | 证据 |
|------------|--------|----------|
| Ralph loop 核心引擎 | 已完成 | `ralph-loop.ps1` |
| Verify/continue/stop 契约 | 已完成 | `stop-hook.ps1` |
| 薄 Windows wrapper | 已完成 | `run-ralph-loop.bat` |
| Mock smoke coverage | 已完成 | `tests/test-ralph-loop.ps1` |
| Timeout 安全性 | 已完成 | `ralph-loop.ps1`、`tests/test-ralph-loop.ps1` |
| UTF-8 文件日志 | 已完成 | `ralph-loop.ps1`、`hello-codex.ps1` |
| Provider abstraction | 已完成 | `agents/profiles.psd1`、通用 `-Agent` / `-AgentCommand` / `-AgentHome` 流程 |
| `opencode` 支持 | 已完成 | 已有 `opencode` profile 和 provider-selection smoke coverage |
| 稳定 reference sync | 已完成 | `references/manifest.psd1`、`sync-references.ps1` 和已填充的 `references/` clones |
| 真实 CLI 测试 | 已完成 | `tests/test-real-agents.ps1` 已存在，且由环境变量 gate 住 |
| 通用参数迁移 | 已完成 | 通用命名已成为主路径，同时保留兼容别名 |
| Provider-neutral prompt metadata | 已完成 | prompt 现在渲染 `AGENT_HOME` 和选中的 agent |

## 测试覆盖现状

### 已有覆盖

- `tests/test-ralph-loop.ps1` 已经覆盖了 direct PowerShell execution、batch wrapper execution、自定义 home 传递、extra prompt 传递、early-stop 行为，以及 timeout 退出码 `124`。
- `tests/mock-agent.ps1`、`tests/mock-verify.ps1` 和 `tests/mock-slow-agent.ps1` 提供了确定性的 mock 测试基座。
- 编码与 shell-specific repro helpers 已经存在于 `tests/pwsh-file-repro*.ps1`。

### 部分覆盖

- Stdout/stderr artifact capture 已经被执行到，但除了 timeout 场景之外，没有更深入的验证。
- `state.json` 和 verify log 内容已经会生成，但没有被完整断言。
- Stop-hook 对 `0` 和 `1` 的行为已经覆盖，但对 `>1` 的 verification failure 还没有覆盖。

### 缺失覆盖

- 还没有在这个工作区里实际执行过一轮真实 agent 测试，因为该套件被刻意放在 `RALPH_TEST_REAL_AGENTS=1` 后面，并且依赖已安装的 CLI。

## 参考仓库带来的启发

`Temp/` 里的参考仓库说明，目标不是简单的“再多跑一个命令”，而是做出一个更干净、可复用的框架。

- `Temp/vercel-labs-ralph-loop-agent` 强化了对 composable stop conditions、template-driven iteration context，以及 provider-neutral orchestration layer 的需求。
- `Temp/iannuttall-ralph` 强化了对 agent registry、按 provider 分开的 command templates，以及更清晰的 reference/config 区域的需求。
- `Temp/shanselman-ralph-gist` 强化了 PowerShell-native control loop 的价值，同时也说明了为什么 shell behavior 和 logging 在 Windows 上必须保持显式且可测试。

这意味着当前缺的并不只是功能本身，也包括边界收口工作：

- [x] 把 provider differences 从核心 loop body 里移出去
- [x] 把 reference management 从 ad-hoc 的手动 clone 方式里移出去
- [x] 把 real-agent verification 从默认 smoke path 里移出去

## 距离目标还有多远

当前工具已经 **在这个“薄壳循环器”目标上基本达标，只是把真实 CLI 执行刻意做成了 opt-in**。

- **基础层基本已经完成**：loop orchestration、logging、stop behavior、batch entrypoint、timeout protection、默认 smoke tests 都已就位。
- **核心目标现在已经补齐**：当前仓库已经是一个 provider-driven 的薄壳工具，支持 `codex` + `opencode`，并允许 `.bat` 层传入提示词和参数。
- 更实际一点的说法是：**核心循环器已经实现，剩下的是在有凭据的环境里做可选的真实 agent 验证。**

## 剩余工作流

### 工作流 1：Provider Abstraction

**结果目标：** 用一条 provider 选择路径替换 codex hardcoding。

- [x] 创建 `agents/profiles.psd1`
- [x] 至少定义 `codex` 和 `opencode` 两个 profile
- [x] 在 `ralph-loop.ps1` 中加入 `-Agent`、`-AgentCommand`、`-AgentHome`
- [x] 保留 `-CodexCommand` 和现有 `CODEX_HOME` 行为作为兼容别名
- [x] 将 console prefix 与 env wiring 收口到选中的 profile 后面

### 工作流 2：`opencode` 支持

**结果目标：** 让 `opencode` 成为真正的一等 provider，而不只是文档里的未来计划。

- [x] 添加 `opencode` command template
- [x] 在 provider layer 后面对它的 non-interactive 行为做归一化
- [x] 确认 `opencode` 与 `codex` 之间的 prompt transport 策略
- [x] 增加 smoke assertions，验证显式 provider selection 确实能切换行为

### 工作流 3：稳定的 Reference Sync

**结果目标：** 把临时性的 reference clones 变成可预测、可重复的本地 reference system。

- [x] 创建 `references/manifest.psd1`
- [x] 创建 `sync-references.ps1`
- [x] 从临时的 ad-hoc clones 迁移到稳定的 `references/` 子目录
- [x] 保证 clone / update 逻辑具备幂等性
- [x] 增加测试，证明 references 不会落到 `.codexloop/` 或 `tests/.tmp/`

### 工作流 4：真实 CLI 测试层

**结果目标：** 保持默认测试快速，同时补上可选的真实 agent 信心层。

- [x] 创建 `tests/test-real-agents.ps1`
- [x] 创建 `tests/test-helpers.ps1`
- [x] 用显式环境变量开关 gate 住真实 CLI tests
- [x] 在运行 `codex` 或 `opencode` 前先检查命令可用性
- [x] 断言 artifacts、exit codes、timeouts 和 stop behavior，而不是模型原文

### 工作流 5：Provider-Neutral 命名收口

**结果目标：** 让实现层命名与预期架构保持一致。

- [x] 替换那些会阻碍 multi-provider support 的内部 codex-only 命名
- [x] 把 prompt metadata 从 `CODEX_HOME` 更新为更通用的 provider-home 表述
- [x] 保持现有 wrapper 和 test entrypoints 的向后兼容
- [x] 更新 mock agent snapshots，使其同时理解 generic home naming 和 legacy codex naming

## 建议执行顺序

1. **先做 provider abstraction** —— 其他能力都依赖它。
2. **再接 `opencode` provider** —— 用它验证 abstraction 是真的，而不是只改名。
3. **第三做 real CLI test layer** —— 验证两个 provider 都能工作，同时不拖慢默认 smoke tests。
4. **第四做 stable reference sync** —— 对工作流很有帮助，但不是让 multi-provider loop 跑起来的前置条件。
5. **最后做 naming cleanup** —— 等行为稳定之后再收尾迁移。

## 目标完成定义

只有当下面这些条件全部满足时，当前仓库才算真正到达预期目标：

- [x] `ralph-loop.ps1` 通过 provider profile 选择行为，而不是硬编码 codex
- [x] `codex` 和 `opencode` 都通过同一套 loop contract 运行
- [x] `run-ralph-loop.bat` 保持薄 wrapper，同时能转发所需的通用参数
- [x] `references/` 拥有基于 manifest 的 sync flow
- [x] 默认 mock smoke tests 依然能快速通过
- [x] 真实 CLI tests 存在、为 opt-in、并且断言的是 artifacts 而不是精确文案
- [x] 文档、测试和实现三者描述的是同一个目标状态

## 备注

- `Temp/` 里现在仍保留着便于立即参考的缓存 clone，而 `references/` 已经成为稳定的 sync 目标。
- 当前 codex MVP 已经有实际价值；剩余工作主要是把它变成预期中的可复用 multi-provider 工具。
- 老的实现计划 `docs/superpowers/plans/2026-04-07-ralph-loop.md` 依然是执行骨架，而这份文档更适合作为进度 / 差距视图。
