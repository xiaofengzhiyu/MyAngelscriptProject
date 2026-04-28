# Codex PowerShell 测试记录

## 背景

这份文档记录了 2026 年 4 月 7 日在 Windows 环境下测试 `codex exec` 时观察到的行为差异，重点是 PowerShell 7 和 Windows PowerShell 5.1 在重定向原生命令输出时的区别。

## 结论

- 在 PowerShell 7.6.0 中，直接运行 `codex exec "hello 你好" --skip-git-repo-check *> 1.txt` 是正常的。
- 之前测试里出现的异常现象，核心原因不是 `codex` 本身，而是宿主 shell 不同。
- 使用 `powershell.exe`（Windows PowerShell 5.1）时，原生命令的 stderr 更容易被包装成 `NativeCommandError` 风格的记录。
- 使用 `pwsh`（PowerShell 7）时，这个场景下的重定向行为更符合预期。

## 已确认可用的命令

请在 PowerShell 7 中使用：

```powershell
codex exec "hello 你好" --skip-git-repo-check *> 1.txt
```

这就是你之前手工验证通过的命令。

## 为什么之前的包装测试看起来不对

- 之前的包装脚本调用的是 `powershell.exe`，不是 `pwsh`。
- 这导致 `codex` 的 stderr 在经过 PowerShell 5.1 管道或重定向时，表现和你手工测试时不一致。
- 另外，之前有一个测试脚本把中文字面量直接写进了 `.ps1` 文件，而文件内容本身已经被错误编码保存了。
- 这个问题后来通过避免直接写中文字面量、改用 ASCII 安全方式构造字符串后已经修复。

## 关于中文参数乱码的根因

这次已经确认：

- `bat -> powershell -> 参数传递` 这条链路本身不一定先坏掉。
- 真正出问题的一次，是测试脚本文件里的中文字面量在落盘时就已经变成了乱码。
- 当脚本里本身保存的是错误文本时，后续传给 `codex` 的自然也是乱码。

当前最稳妥的做法：

- 直接在 PowerShell 7 里手工运行中文命令。
- 如果必须通过包装脚本运行，优先使用 `pwsh`。

## 当前包装脚本状态

- `hello-codex.ps1` 和 `run-hello-codex.bat` 已经建立，用于最小化测试 `bat -> PowerShell -> codex exec` 调用链。
- 这套脚本已经能证明调用链可以成功启动，退出码也正常。
- 但如果希望包装脚本的行为和你手工在 PowerShell 7 中运行的结果完全一致，应该把包装脚本改成优先使用 `pwsh`。

## 重启后的建议验证步骤

重启后，先打开 PowerShell 7，再执行：

```powershell
codex exec "hello 你好" --skip-git-repo-check *> 1.txt
```

如果这条命令仍然正常，那么可以确认：

- `codex` 本身没有问题
- 你的 `codez` provider 配置没有问题
- 剩余差异只在包装脚本使用的 shell 选择上

## 后续建议

如果重启后验证结果仍然正常，下一步建议做的事情只有一项：

- 把现有 `.bat` 包装统一改成优先调用 `pwsh`

这样包装脚本的行为会更接近你平时手工执行 `codex exec` 时的行为。
