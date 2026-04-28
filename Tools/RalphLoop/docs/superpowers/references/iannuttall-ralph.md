# `Temp/iannuttall-ralph`：如何支持 loop

## 对象定位

- 上游来源是 `iannuttall/ralph`，在本地 manifest 中登记为 `references/manifest.psd1:8`。
- 项目自述把它定义成“minimal, file-based agent loop”，并明确说明“每次 iteration 都从磁盘状态重新开始”，核心状态落在 `.ralph/`，见 `Temp/iannuttall-ralph/README.md:5`、`Temp/iannuttall-ralph/README.md:9`、`Temp/iannuttall-ralph/README.md:34`。

## 它的 loop 是怎么搭起来的

- CLI 入口是 `Temp/iannuttall-ralph/bin/ralph:585`，当命令是 `build` 或默认命令时，它先解析 PRD，再通过 `spawnSync(loopPath, loopArgs, ...)` 把控制权交给 Bash loop 脚本，见 `Temp/iannuttall-ralph/bin/ralph:599`。
- 真正的循环体在 `Temp/iannuttall-ralph/.agents/ralph/loop.sh:845`：`for i in $(seq 1 "$MAX_ITERATIONS")`。这说明它不是长驻进程，而是一个受最大轮次约束的 shell 驱动器。
- 每轮开始前，它会从 PRD 里挑一个可执行 story。挑选逻辑在 `Temp/iannuttall-ralph/.agents/ralph/loop.sh:412`，会：
  - 读取 `.agents/tasks/prd.json`；
  - 只选择 `status=open` 的 story，见 `Temp/iannuttall-ralph/.agents/ralph/loop.sh:503`；
  - 检查依赖是否全部完成，见 `Temp/iannuttall-ralph/.agents/ralph/loop.sh:505` 和 `Temp/iannuttall-ralph/.agents/ralph/loop.sh:508`；
  - 可选地把超时卡住的 `in_progress` story 重新打开，见 `Temp/iannuttall-ralph/.agents/ralph/loop.sh:485` 到 `Temp/iannuttall-ralph/.agents/ralph/loop.sh:498`。
- 选中 story 后，脚本会渲染 prompt 文件、执行 agent 命令、记录日志并回写状态，关键路径见 `Temp/iannuttall-ralph/.agents/ralph/loop.sh:877` 到 `Temp/iannuttall-ralph/.agents/ralph/loop.sh:919`。

## 它如何判断“继续下一轮”还是“结束”

- 这个实现把“完成信号”直接嵌进 prompt 规范里。`Temp/iannuttall-ralph/.agents/ralph/PROMPT_build.md:95` 到 `Temp/iannuttall-ralph/.agents/ralph/PROMPT_build.md:100` 要求 agent 只有在 story 真正完成且验证通过时才输出：

  ```xml
  <promise>COMPLETE</promise>
  ```

- loop 本身只负责检测这个标记。检测逻辑在 `Temp/iannuttall-ralph/.agents/ralph/loop.sh:926` 到 `Temp/iannuttall-ralph/.agents/ralph/loop.sh:937`：
  - 如果 agent 非零退出，story 重置回 `open`；
  - 如果日志里出现 `<promise>COMPLETE</promise>`，story 标记为 `done`；
  - 否则同样回到 `open`，等待下一轮再试。
- 退出条件有三类：
  - 没有剩余 story，直接 `exit 0`，见 `Temp/iannuttall-ralph/.agents/ralph/loop.sh:864` 到 `Temp/iannuttall-ralph/.agents/ralph/loop.sh:866` 与 `Temp/iannuttall-ralph/.agents/ralph/loop.sh:940` 到 `Temp/iannuttall-ralph/.agents/ralph/loop.sh:942`；
  - 没有可执行 story（都被依赖或锁住），直接 `exit 0`，见 `Temp/iannuttall-ralph/.agents/ralph/loop.sh:870` 到 `Temp/iannuttall-ralph/.agents/ralph/loop.sh:872`；
  - 达到最大轮次后按是否出现错误决定 `exit 0/1`，见 `Temp/iannuttall-ralph/.agents/ralph/loop.sh:951` 到 `Temp/iannuttall-ralph/.agents/ralph/loop.sh:955`。

## 它靠什么保存 loop 状态

- README 把状态目录和模板目录的边界讲得很清楚：模板在 `.agents/ralph/`，状态和日志永远落在 `.ralph/`，见 `Temp/iannuttall-ralph/README.md:27` 到 `Temp/iannuttall-ralph/README.md:35`。
- `.ralph/` 里包含 `progress.md`、`guardrails.md`、`activity.log`、`errors.log` 和 `runs/`，见 `Temp/iannuttall-ralph/README.md:150` 到 `Temp/iannuttall-ralph/README.md:157`。
- 每轮还会单独落盘 prompt、story 快照、原始日志和 run summary，见 `Temp/iannuttall-ralph/.agents/ralph/loop.sh:856` 到 `Temp/iannuttall-ralph/.agents/ralph/loop.sh:880`。

## 它如何支持多 agent / 多 provider

- `Temp/iannuttall-ralph/.agents/ralph/agents.sh:4` 到 `Temp/iannuttall-ralph/.agents/ralph/agents.sh:15` 把 `codex`、`claude`、`droid`、`opencode` 的命令模板集中在一起，loop 主体只消费 `AGENT_CMD`，而不关心 provider 差异。
- README 也暴露了同样的切换模型，允许通过 `AGENT_CMD` 或 `--agent` 覆盖，见 `Temp/iannuttall-ralph/README.md:120` 到 `Temp/iannuttall-ralph/README.md:139`。

## 为什么它对当前仓库有参考价值

- 它把“provider 差异”隔离到了命令模板层，而不是塞进循环主逻辑，这和当前仓库把 provider 差异收口到 profile/wrapper 的方向一致。
- 它展示了一个非常实用的完成判定模式：loop 本身不理解业务，只认 `<promise>COMPLETE</promise>`，这与当前 `/ralph-loop` 命令里的 completion promise 语义高度一致。
- 它证明了“文件即状态、git 即记忆”的 loop 设计可以工作：PRD 负责调度，`.ralph/` 负责落盘，shell 负责推进轮次。

## 一句话总结

`iannuttall-ralph` 支持 loop 的关键不是“多跑几次 agent”，而是把一次长期任务拆成 story 状态机：每轮选一个 story、执行、检查 promise、回写状态，再决定是否继续下一轮。
