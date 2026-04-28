# `Temp/vercel-labs-ralph-loop-agent`：如何支持 loop

## 对象定位

- 上游来源是 `vercel-labs/ralph-loop-agent`，在本地 manifest 中登记为 `references/manifest.psd1:3`。
- README 直接把它定义成 AI SDK 的“Continuous Autonomy”，并把 Ralph 描述成“outer loop + inner tool loop”的双层结构，见 `Temp/vercel-labs-ralph-loop-agent/README.md:3`、`Temp/vercel-labs-ralph-loop-agent/README.md:25` 到 `Temp/vercel-labs-ralph-loop-agent/README.md:32`。

## 它的 loop 核心模型

- 核心类是 `RalphLoopAgent`，主逻辑在 `Temp/vercel-labs-ralph-loop-agent/packages/ralph-loop-agent/src/ralph-loop-agent.ts:220`。
- 外层循环就是 `while (true)`，定义在 `Temp/vercel-labs-ralph-loop-agent/packages/ralph-loop-agent/src/ralph-loop-agent.ts:252`。每轮都会：
  - 组装消息和上下文；
  - 调用一次 `generateText`；
  - 聚合 usage；
  - 检查 stop conditions；
  - 调用 `verifyCompletion` 决定是否进入下一轮。
- 内层 tool loop 不是自己手写的 while，而是交给 AI SDK 的 `generateText(... stopWhen: stepCountIs(20))`，见 `Temp/vercel-labs-ralph-loop-agent/packages/ralph-loop-agent/src/ralph-loop-agent.ts:387` 到 `Temp/vercel-labs-ralph-loop-agent/packages/ralph-loop-agent/src/ralph-loop-agent.ts:392`。
- 从第二轮开始，它会自动插入 continuation prompt：`Continue working on the task. The previous attempt was not complete.`，见 `Temp/vercel-labs-ralph-loop-agent/packages/ralph-loop-agent/src/ralph-loop-agent.ts:320` 到 `Temp/vercel-labs-ralph-loop-agent/packages/ralph-loop-agent/src/ralph-loop-agent.ts:331`。

## 它如何控制停止

- stop condition 是可组合的纯函数，定义在 `Temp/vercel-labs-ralph-loop-agent/packages/ralph-loop-agent/src/ralph-stop-condition.ts:285` 到 `Temp/vercel-labs-ralph-loop-agent/packages/ralph-loop-agent/src/ralph-stop-condition.ts:389`：
  - `iterationCountIs`：按轮次停止；
  - `tokenCountIs` / `inputTokenCountIs` / `outputTokenCountIs`：按 token 停止；
  - `costIs`：按估算成本停止；
  - `isRalphStopConditionMet`：并行求值，任一条件为真即停。
- 外层循环在每轮结束后先检查 stop conditions，见 `Temp/vercel-labs-ralph-loop-agent/packages/ralph-loop-agent/src/ralph-loop-agent.ts:428` 到 `Temp/vercel-labs-ralph-loop-agent/packages/ralph-loop-agent/src/ralph-loop-agent.ts:439`。
- 真正的“任务是否完成”由 `verifyCompletion` 决定，它的接口定义在 `Temp/vercel-labs-ralph-loop-agent/packages/ralph-loop-agent/src/ralph-loop-agent-evaluator.ts:3` 到 `Temp/vercel-labs-ralph-loop-agent/packages/ralph-loop-agent/src/ralph-loop-agent-evaluator.ts:50`。
- 如果 `verifyCompletion` 返回 `{ complete: false, reason }`，这个 `reason` 会被注入到下一轮作为 `Feedback: ...`，见 `Temp/vercel-labs-ralph-loop-agent/packages/ralph-loop-agent/src/ralph-loop-agent.ts:442` 到 `Temp/vercel-labs-ralph-loop-agent/packages/ralph-loop-agent/src/ralph-loop-agent.ts:475`。

## 它如何处理长上下文和长期任务

- `RalphContextManager` 负责把老轮次压缩成摘要，再把摘要与 change log 注入下一轮，核心接口在 `Temp/vercel-labs-ralph-loop-agent/packages/ralph-loop-agent/src/ralph-context-manager.ts:444` 到 `Temp/vercel-labs-ralph-loop-agent/packages/ralph-loop-agent/src/ralph-context-manager.ts:521`。
- `buildContextInjection()` 会生成自动管理的 Agent Context，见 `Temp/vercel-labs-ralph-loop-agent/packages/ralph-loop-agent/src/ralph-context-manager.ts:444` 到 `Temp/vercel-labs-ralph-loop-agent/packages/ralph-loop-agent/src/ralph-context-manager.ts:460`。
- `prepareMessagesForIteration()` 会在 token 接近预算上限时总结旧轮次，只保留最近消息，见 `Temp/vercel-labs-ralph-loop-agent/packages/ralph-loop-agent/src/ralph-context-manager.ts:467` 到 `Temp/vercel-labs-ralph-loop-agent/packages/ralph-loop-agent/src/ralph-context-manager.ts:520`。

## CLI 示例如何把 loop 用起来

- CLI 示例不是另一个 loop 引擎，而是对 `RalphLoopAgent` 的封装，入口在 `Temp/vercel-labs-ralph-loop-agent/examples/cli/index.ts`。
- 它支持中断菜单和恢复执行：首次 `SIGINT` 先 abort 当前 loop，再让用户选择继续、跟进、保存或退出，见 `Temp/vercel-labs-ralph-loop-agent/examples/cli/index.ts:383` 到 `Temp/vercel-labs-ralph-loop-agent/examples/cli/index.ts:445`。
- CLI 自己还有一层“外部恢复循环”，当 `agent.loop()` 因 abort 结束时，会根据用户选择决定是否 `preserveContext=true` 后继续，见 `Temp/vercel-labs-ralph-loop-agent/examples/cli/index.ts:800` 到 `Temp/vercel-labs-ralph-loop-agent/examples/cli/index.ts:860`。
- 计划模式 `plan-mode.ts` 也体现了 loop 思维：先探索、再产出 plan、再允许反复 refine，见 `Temp/vercel-labs-ralph-loop-agent/examples/cli/lib/plan-mode.ts:151` 到 `Temp/vercel-labs-ralph-loop-agent/examples/cli/lib/plan-mode.ts:319`。

## 它怎么证明自己能工作

- README 的最小例子就要求调用方提供 `stopWhen` 与 `verifyCompletion`，见 `Temp/vercel-labs-ralph-loop-agent/README.md:72` 到 `Temp/vercel-labs-ralph-loop-agent/README.md:92`。
- 单元测试覆盖了“首轮完成”“多轮后完成”“达到最大轮次”“反馈注入下一轮”等核心路径，见：
  - `Temp/vercel-labs-ralph-loop-agent/packages/ralph-loop-agent/src/ralph-loop-agent.test.ts:13`
  - `Temp/vercel-labs-ralph-loop-agent/packages/ralph-loop-agent/src/ralph-loop-agent.test.ts:36`
  - `Temp/vercel-labs-ralph-loop-agent/packages/ralph-loop-agent/src/ralph-loop-agent.test.ts:66`
  - `Temp/vercel-labs-ralph-loop-agent/packages/ralph-loop-agent/src/ralph-loop-agent.test.ts:157`

## 为什么它对当前仓库有参考价值

- 它把“继续重试直到完成”抽象成一个可复用库，而不是绑定某个 CLI，这强化了当前仓库的 provider-neutral 方向。
- 它证明了 stop logic 最好拆成两层：
  - 一层是硬安全阈值（iterations/tokens/cost）；
  - 一层是任务完成验证（verifyCompletion）。
- 它还展示了长期 loop 必须考虑上下文预算、摘要压缩、abort/resume 和人工 review 插口，而不仅仅是 `while (true)`。

## 一句话总结

`vercel-labs-ralph-loop-agent` 支持 loop 的方式，是把“AI SDK 的单轮 tool run”包上一层可验证、可恢复、可限流的自治循环框架。
