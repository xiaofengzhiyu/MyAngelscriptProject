# `Temp/shanselman-ralph-gist`：如何支持 loop

## 对象定位

- 上游是 Scott Hanselman 的公开 gist：`shanselman/aa5e34a74b36404123dabed9394655f3`。
- gist 描述指向一个非常直接的目标：为 GitHub Copilot CLI 提供 PowerShell 版 Ralph loop，并继承 David Fowler 的 bash 思路与 Geoffrey Huntley 的 Ralph pattern。
- 本地缓存只有一个脚本文件：`Temp/shanselman-ralph-gist/ralph-loop.ps1`。

## 它的 loop 结构非常“薄”

- 参数入口只有一个必填 `JobName`，见 `Temp/shanselman-ralph-gist/ralph-loop.ps1:5`。
- 配置全部来自环境变量或默认值，包括 `MAX_ITERATIONS`、`MODEL_NAME`、`COPILOT_ARGS`、`JOB_DIR`，见 `Temp/shanselman-ralph-gist/ralph-loop.ps1:10` 到 `Temp/shanselman-ralph-gist/ralph-loop.ps1:14`。
- 它先创建一个 job 目录结构，并把 prompt、stop hook、sessions 路径固定下来，见 `Temp/shanselman-ralph-gist/ralph-loop.ps1:19` 到 `Temp/shanselman-ralph-gist/ralph-loop.ps1:27`。

## 它如何启动和继续 loop

- 如果 `prompt.md` 还不存在，脚本不会直接进入循环，而是先调用 `copilot -i` 帮用户把 job 框架建出来，见 `Temp/shanselman-ralph-gist/ralph-loop.ps1:29` 到 `Temp/shanselman-ralph-gist/ralph-loop.ps1:57`。这相当于把“首次建任务上下文”也纳入 loop 工作流。
- 它通过统计已有 `sessions/iteration-*.md` 文件来恢复进度，见 `Temp/shanselman-ralph-gist/ralph-loop.ps1:60` 到 `Temp/shanselman-ralph-gist/ralph-loop.ps1:63`。
- 主循环是 `while ($iteration -lt $maxIteration)`，见 `Temp/shanselman-ralph-gist/ralph-loop.ps1:88`。每轮做的事情很简单：
  - 生成新的 session 文件名，见 `Temp/shanselman-ralph-gist/ralph-loop.ps1:96` 到 `Temp/shanselman-ralph-gist/ralph-loop.ps1:99`；
  - 读取 `prompt.md`，见 `Temp/shanselman-ralph-gist/ralph-loop.ps1:104` 到 `Temp/shanselman-ralph-gist/ralph-loop.ps1:105`；
  - 拼出 `copilot -p ... --share <session>` 命令执行一轮，见 `Temp/shanselman-ralph-gist/ralph-loop.ps1:107` 到 `Temp/shanselman-ralph-gist/ralph-loop.ps1:117`。

## 它如何决定结束

- 这个版本没有内置 completion promise，也不读任务文件内容来判断完成，而是把判断权交给 `stop-hook.ps1`。
- stop hook 约定在脚本初始化提示里就写明了：`exit 0` 代表完成，`exit 1` 代表还要继续，见 `Temp/shanselman-ralph-gist/ralph-loop.ps1:44` 到 `Temp/shanselman-ralph-gist/ralph-loop.ps1:50`。
- 每轮结束后，主脚本会执行 stop hook，见 `Temp/shanselman-ralph-gist/ralph-loop.ps1:129` 到 `Temp/shanselman-ralph-gist/ralph-loop.ps1:139`。
- 如果 hook 返回 `0`，整个 loop 立即成功退出，见 `Temp/shanselman-ralph-gist/ralph-loop.ps1:141` 到 `Temp/shanselman-ralph-gist/ralph-loop.ps1:147`；否则打印“继续下一轮”的提示，见 `Temp/shanselman-ralph-gist/ralph-loop.ps1:148` 到 `Temp/shanselman-ralph-gist/ralph-loop.ps1:155`。
- 达到最大轮次仍未完成时，脚本以失败退出，见 `Temp/shanselman-ralph-gist/ralph-loop.ps1:161` 到 `Temp/shanselman-ralph-gist/ralph-loop.ps1:166`。

## 它把哪些状态落盘

- job 目录是这个实现的状态边界：
  - `prompt.md`：主任务说明；
  - `stop-hook.ps1`：完成判定；
  - `sessions/iteration-###.md`：每一轮的共享记录。
- 这些路径都在脚本里显式声明，见 `Temp/shanselman-ralph-gist/ralph-loop.ps1:20` 到 `Temp/shanselman-ralph-gist/ralph-loop.ps1:23`。

## 为什么它对当前仓库有参考价值

- 它说明了一个 PowerShell-native loop 壳层可以非常薄：读 prompt、调 CLI、调 verify hook、决定继续或停止。
- 它把“完成标准”外包给脚本 hook，而不是嵌进 agent 逻辑本身；这和当前仓库的 `stop-hook.ps1` 合同天然同向。
- 它还展示了 Windows 语境下显式日志与显式目录布局的价值：session 文件天然可恢复、可审计、可继续。

## 一句话总结

`shanselman-ralph-gist` 用最少的 PowerShell 代码证明了 Ralph loop 的核心：持续执行 agent、调用外部 stop hook、命中完成条件前绝不退出。
