# Codex CLI 命令行调用指南

## 1. 基本调用模式

### 交互模式

```bash
codex
```

启动 TUI（终端界面），可以来回对话。

### 非交互模式（`exec`）

```bash
codex exec "你的任务描述"
```

一次性执行，跑完退出。适合脚本自动化、CI/CD。

### 工作目录

Codex 默认在当前目录运行，且**要求当前目录是 git 仓库**。

```bash
# 方式 1：先 cd 再运行
cd /d "J:\UnrealEngine\AngelscriptProject"
codex exec "任务描述"

# 方式 2：用 --cd 指定
codex exec --cd "J:\UnrealEngine\AngelscriptProject" "任务描述"

# 方式 3：不在 git 仓库时加 --skip-git-repo-check
codex exec --skip-git-repo-check "任务描述"
```

## 2. Prompt 传入方式

| 方式 | 命令 | 适用场景 |
|------|------|----------|
| 命令行参数 | `codex exec "prompt"` | 短 prompt、纯 ASCII |
| stdin 管道 | `echo prompt \| codex exec` | 简单场景 |
| stdin 占位符 | `codex exec -` | 显式声明从 stdin 读取 |
| **文件重定向** | `codex exec - < prompt.txt` | **推荐**，最稳定 |

> **Windows 注意**：直接传参数时，codex 可能额外尝试读 stdin 导致挂起。推荐用文件重定向。

## 3. 常用命令行选项

| 选项 | 作用 |
|------|------|
| `--cd, -C <path>` | 指定工作目录 |
| `--full-auto` | `workspace-write` 沙箱 + `on-request` 审批 |
| `--sandbox, -s <mode>` | `read-only` / `workspace-write` / `danger-full-access` |
| `-m, --model <model>` | 覆盖模型（如 `gpt-5.4`） |
| `-c, --config <k=v>` | 覆盖 `config.toml` 中的任意配置项 |
| `--color <mode>` | `always` / `never` / `auto` |
| `-o, --output-last-message <file>` | 将最终回复写入文件（干净无杂信息） |
| `--json` | stdout 输出 JSONL 事件流 |
| `--skip-git-repo-check` | 允许在非 git 目录下运行 |
| `--ephemeral` | 不持久化 session 文件 |
| `-p, --profile <name>` | 使用 `config.toml` 中的命名 profile |
| `--skip-git-repo-check` | 非 git 仓库目录下运行 |

## 4. 认证与密钥

### 认证方式

Codex CLI 支持两种认证：

- **ChatGPT 登录**：浏览器 OAuth 流程（默认）
- **API Key**：用量计费，适合自动化

### 登录命令

```bash
# 浏览器登录（默认）
codex login

# API Key 登录（通过 stdin 传入，无直接 --api-key 参数）
echo sk-xxxxx | codex login --with-api-key

# 环境变量方式
printenv OPENAI_API_KEY | codex login --with-api-key

# 检查登录状态
codex login status

# 登出
codex logout
```

> **v0.35.0+**：不再支持隐式环境变量登录，必须显式执行 `codex login`。

### 凭据存储

凭据缓存在 `~/.codex/auth.json`（明文 JSON），格式：

```json
{
  "OPENAI_API_KEY": "sk-xxxxxxxx"
}
```

通过 `config.toml` 控制存储位置：

```toml
# file | keyring | auto
cli_auth_credentials_store = "file"
```

- `file` — 存到 `~/.codex/auth.json`
- `keyring` — 存到 OS 凭据管理器
- `auto` — 优先 OS 凭据管理器，不可用时回退文件

### 切换不同 API Key

Codex **没有** `--api-key` 命令行参数。切换密钥有三种方式：

| 方案 | 命令 | 影响范围 |
|------|------|----------|
| **`CODEX_HOME` 环境变量** | `set CODEX_HOME=C:\path\to\alt_codex_home` | 仅当前进程，最安全 |
| **`codex login`** | `echo sk-xxx \| codex login --with-api-key` | 覆盖当前 `auth.json` |
| **手动替换** | 拷贝不同的 `auth.json` | 全局生效 |

#### CODEX_HOME 方案示例

创建一个替代配置目录，包含不同的 `auth.json` 和 `config.toml`：

```
C:\Users\me\.codex_alt\
├── auth.json       # 不同的 API Key
└── config.toml     # 可从 ~/.codex/config.toml 复制
```

在 PowerShell 中使用（仅当前进程生效，不影响全局）：

```powershell
$env:CODEX_HOME = "C:\Users\me\.codex_alt"
codex exec "任务描述"
```

在 `.bat` 中使用：

```bat
set CODEX_HOME=C:\Users\me\.codex_alt
codex exec "任务描述"
```

验证是否影响全局（应返回空）：

```powershell
[System.Environment]::GetEnvironmentVariable("CODEX_HOME", "User")
[System.Environment]::GetEnvironmentVariable("CODEX_HOME", "Machine")
```

### 项目工具集成

项目内的 Codex 调用工具统一从 `AgentConfig.ini` 的 `[Codex]` 节读取 `CodexHome`：

```ini
[Codex]
; 为空时使用默认 ~/.codex；指定路径时切换 API Key / config
CodexHome=C:\Users\me\.codex_alt
```

优先级：环境变量 `CODEX_HOME` > `AgentConfig.ini [Codex] CodexHome` > 默认 `~/.codex`。

已集成的工具：
- `Tools/IterationAnalysis/powershell/RunIterationAnalysis_Codex.ps1` — 读 ini 后设 `$env:CODEX_HOME`，Python `codex_runner.py` 透传到子进程
- `Tools/ReferenceComparison/powershell/RunReferenceComparison_Codex.ps1` — 读 ini 后设 `$env:CODEX_HOME`，`cmd.exe` 子进程继承

## 5. 配置文件

默认路径 `~/.codex/config.toml`，示例：

```toml
model = "gpt-5.4"
model_provider = "codez"
sandbox_mode = "danger-full-access"
approval_policy = "never"

[model_providers.codez]
base_url = "https://api.example.com/v1"
name = "CustomProvider"
requires_openai_auth = false
wire_api = "responses"
```

`-c` 参数可在命令行覆盖任意配置项：

```bash
codex exec -c model="o3" -c model_reasoning_effort="high" "任务描述"
```

## 6. 输出捕获

### `-o` 参数（推荐）

只保存 agent 最终回复文本，最干净：

```bash
codex exec -o result.txt "任务描述"
```

### `--json` 参数

stdout 输出 JSONL 事件流，适合程序解析：

```bash
codex exec --json "任务描述" > events.jsonl
```

### 重定向注意事项

Codex 的进度信息和 session 元数据走 **stderr**，不是 stdout。

| Shell | 只捕获 stdout | 捕获全部 |
|-------|---------------|----------|
| PowerShell | `>` | `*>` |
| cmd.exe | `>` | `> file.txt 2>&1` |

## 7. Windows PowerShell 编码问题

### 问题

PowerShell 5 存在两个编码陷阱：

1. **`>` / `*>` 重定向**写 UTF-16LE（`FF FE` 开头），无论 `$OutputEncoding` 怎么设
2. **管道传中文**给原生命令时编码会损坏

### 解决方案：文件重定向 + cmd.exe

```powershell
# 1. 用 .NET API 写 prompt 到临时文件（保证 UTF-8）
$promptFile = "$env:TEMP\codex_prompt.tmp"
$outFile    = "result.txt"
[System.IO.File]::WriteAllText($promptFile, '你的任务描述', [System.Text.Encoding]::UTF8)

# 2. 通过 cmd.exe 的 < 重定向传入（绕过 PowerShell 管道编码问题）
cmd.exe /d /c "codex exec --color never -o `"$outFile`" - < `"$promptFile`""

# 3. 清理临时文件
Remove-Item $promptFile -ErrorAction SilentlyContinue
```

### 为什么这样做

| 环节 | 问题 | 解法 |
|------|------|------|
| Prompt 传入 | PS 管道破坏中文编码 | `[System.IO.File]::WriteAllText` 写 UTF-8 文件 |
| 文件重定向 | PS 的 `<` 不支持原生命令 | 用 `cmd.exe /d /c "... < file"` |
| 输出捕获 | PS 的 `>` 写 UTF-16LE | 用 `-o` 拿结果，或 `Start-Process -RedirectStandardOutput` |
| 过程日志乱码 | stderr 中 Unicode 特殊字符（`→` 等）被 console codepage 截断 | 不影响结果，用 `Start-Process -RedirectStandardError` 写文件可解决 |

### 完整脚本模板

```powershell
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

[Console]::OutputEncoding = [System.Text.Encoding]::UTF8
$OutputEncoding = [System.Text.Encoding]::UTF8

$ProjectRoot = "J:\UnrealEngine\AngelscriptProject"
$promptFile  = Join-Path $env:TEMP "codex_prompt_$PID.tmp"
$outFile     = Join-Path $ProjectRoot "result.txt"
$logStdout   = Join-Path $env:TEMP "codex_stdout_$PID.tmp"
$logStderr   = Join-Path $env:TEMP "codex_stderr_$PID.tmp"

# 写 prompt（UTF-8）
[System.IO.File]::WriteAllText($promptFile, @'
你的多行
任务描述
'@, [System.Text.Encoding]::UTF8)

try {
    $process = Start-Process `
        -FilePath 'cmd.exe' `
        -ArgumentList @('/d', '/c', "codex exec --cd `"$ProjectRoot`" --color never -o `"$outFile`" - < `"$promptFile`"") `
        -NoNewWindow -PassThru `
        -RedirectStandardOutput $logStdout `
        -RedirectStandardError  $logStderr

    $process.WaitForExit()
    Write-Host "Exit code: $($process.ExitCode)"
}
finally {
    Remove-Item $promptFile, $logStdout, $logStderr -Force -ErrorAction SilentlyContinue
}
```

## 8. 其他子命令速查

| 命令 | 用途 |
|------|------|
| `codex` | 交互式 TUI |
| `codex exec` | 非交互执行（别名 `codex e`） |
| `codex login` | 认证 |
| `codex login status` | 检查登录状态 |
| `codex logout` | 登出 |
| `codex resume` | 继续上一个交互 session |
| `codex fork` | 从历史 session 分叉新对话 |
| `codex features` | 管理 feature flags |
| `codex completion <shell>` | 生成 shell 补全脚本 |
| `codex mcp` | 管理 MCP 服务器 |
