# Batch Wrapper Usage

## Purpose

This repository provides a small batch-wrapper layer on top of `ralph-loop.ps1`.

The design is intentionally simple:

- `PowerShell` owns the real loop logic.
- `.bat` files only collect arguments, choose the provider, and forward them.
- The prompt text controls the work strategy.

If you only remember one thing, remember this:

- Use `run-ralph-loop.bat` when you want to choose the provider explicitly.
- Use `run-codex-loop.bat` when you always want `codex`.
- Use `run-opencode-loop.bat` when you always want `opencode`.

## Available Wrappers

### `run-ralph-loop.bat`

Generic entrypoint.

Use this when you want to choose the provider with `-Agent`.

Example:

```bat
run-ralph-loop.bat -Prompt "Reply with hello world." -Agent codex -MaxIterations 1
run-ralph-loop.bat -Prompt "Continue implementing this feature." -Agent opencode -MaxIterations 3
```

### `run-codex-loop.bat`

Convenience wrapper that always pins `-Agent codex`.

Example:

```bat
run-codex-loop.bat -Prompt "Reply with hello world." -MaxIterations 1
```

### `run-opencode-loop.bat`

Convenience wrapper that always pins `-Agent opencode`.

Example:

```bat
run-opencode-loop.bat -Prompt "Reply with hello world." -MaxIterations 1
```

## Arguments

All wrapper arguments are named arguments.

### Required

- `-Prompt "text"`
  - Main prompt text sent into the loop.
  - This is the primary user input.

### Optional

- `-MaxIterations N`
  - Maximum number of loop rounds.
  - Default: `5`

- `-Agent codex|opencode`
  - Only used with `run-ralph-loop.bat`.
  - Selects which provider profile to use.

- `-AgentHome PATH`
  - Overrides the home/config path used by the provider.

- `-PromptFile PATH`
  - Overrides the prompt template file.
  - Default template: `prompts\loop.txt`

- `-TimeoutSeconds N`
  - Per-agent-command timeout.
  - `0` means disabled.

- `-AgentCommand "command string"`
  - Overrides the provider's default command template.
  - Mostly useful for testing or advanced customization.

- `-VerifyCommand "command string"`
  - Optional verification command used by `stop-hook.ps1`.
  - Exit contract:
    - `0` = stop the loop
    - `1` = continue looping
    - `>1` = fail the run

- `-RunsRoot PATH`
  - Overrides the output root directory.
  - Default: `.codexloop\runs`

- `-StreamAgentOutput`
  - Off by default.
  - When omitted, terminal output shows the summary block and compact status lines.
  - When enabled, raw provider output is also streamed to the terminal.

- `-Help`
  - Prints usage information and exits.

## Recommended Usage Patterns

### 1. Simple one-shot reply

```bat
run-codex-loop.bat -Prompt "Reply with hello world only." -MaxIterations 1
```

### 2. Multi-round task

```bat
run-opencode-loop.bat -Prompt "Continue working on this feature until the verification command passes." -MaxIterations 3
```

### 3. Explicit provider choice

```bat
run-ralph-loop.bat -Prompt "Analyze this directory." -Agent codex -MaxIterations 2
run-ralph-loop.bat -Prompt "Refactor this module." -Agent opencode -MaxIterations 2
```

### 4. Show raw provider output

```bat
run-codex-loop.bat -Prompt "Reply with hello world only." -MaxIterations 1 -StreamAgentOutput
```

### 5. Use a custom output root

```bat
run-codex-loop.bat -Prompt "Generate a comparison report." -MaxIterations 3 -RunsRoot "D:\Logs\RalphLoop"
```

## Default Terminal Output

By default, the terminal shows:

- Provider name
- Output directory
- Working directory
- Prompt template path
- Agent home
- Verify mode
- Max rounds
- Stream mode
- Timeout mode
- Per-iteration log paths
- Compact status lines such as:

```text
  [1/3] codex running... 00:00:07
  [1/3] codex verifying...
  [1/3] codex continuing
```

Raw provider output is hidden by default.

If you want to see raw provider output in the terminal, add `-StreamAgentOutput`.

## Default Output Files

Unless you override `-RunsRoot`, output goes under:

```text
.codexloop\runs\<timestamp-pid>\
```

Each iteration writes:

- `prompt.txt`
- `stdout.log`
- `stderr.log`
- `last-message.txt`
- `verify.stdout.log`
- `verify.stderr.log`

The run root also includes:

- `run.json`
- `state.json`

## How Provider Selection Works

Provider behavior is defined in `agents\profiles.psd1`.

Each profile owns:

- command template
- console prefix
- provider home environment variable
- default home path
- prompt transport mode

That means `run-ralph-loop.bat` does not need provider-specific branching beyond `-Agent` selection.

## How To Add More Wrappers

There are two common extension cases.

### Case 1: Add a convenience wrapper for an existing provider

If you already have a provider profile and only want a new convenience entrypoint, create a new `.bat` file next to the existing wrappers.

Example: `run-myteam-codex-loop.bat`

```bat
@echo off
setlocal

set "SCRIPT_DIR=%~dp0"
echo [run-myteam-codex-loop.bat] Provider: codex
call "%SCRIPT_DIR%run-ralph-loop.bat" %* -Agent codex
exit /b %ERRORLEVEL%
```

Use this pattern when you want a memorable entrypoint but the same underlying provider.

### Case 2: Add a new provider

If the provider itself is new, do this in order:

1. Add a new profile to `agents\profiles.psd1`
2. Test it through `run-ralph-loop.bat -Agent <new-provider>`
3. Optionally add a fixed wrapper like `run-<new-provider>-loop.bat`

Wrapper template:

```bat
@echo off
setlocal

set "SCRIPT_DIR=%~dp0"
echo [run-myprovider-loop.bat] Provider: myprovider
call "%SCRIPT_DIR%run-ralph-loop.bat" %* -Agent myprovider
exit /b %ERRORLEVEL%
```

## Wrapper Design Rules

When adding new wrappers, keep these rules:

- Do not duplicate loop logic in `.bat`
- Do not add provider-specific orchestration to the generic wrapper
- Do not hardcode runtime artifact paths inside custom wrappers unless that is the wrapper's explicit purpose
- Keep wrappers thin: forward args, optionally pin one or two defaults, then call `run-ralph-loop.bat`
- Put provider-specific behavior in `agents\profiles.psd1`, not in the wrapper body

## Recommended Extension Examples

### Fixed output root wrapper

```bat
@echo off
setlocal

set "SCRIPT_DIR=%~dp0"
call "%SCRIPT_DIR%run-ralph-loop.bat" %* -RunsRoot "D:\RalphRuns"
exit /b %ERRORLEVEL%
```

### Fixed prompt template wrapper

```bat
@echo off
setlocal

set "SCRIPT_DIR=%~dp0"
call "%SCRIPT_DIR%run-ralph-loop.bat" %* -PromptFile "%SCRIPT_DIR%prompts\review-loop.txt"
exit /b %ERRORLEVEL%
```

### Fixed provider + fixed output root

```bat
@echo off
setlocal

set "SCRIPT_DIR=%~dp0"
echo [run-codex-review-loop.bat] Provider: codex
call "%SCRIPT_DIR%run-ralph-loop.bat" %* -Agent codex -RunsRoot "D:\RalphRuns\codex"
exit /b %ERRORLEVEL%
```

## Notes

- Prefer `-Prompt` as the normal input.
- `-Task` still works as a compatibility alias in the PowerShell layer, but new usage should prefer `-Prompt`.
- Prefer `pwsh` when available.
- If you want the terminal to stay readable, keep `-StreamAgentOutput` off unless you are actively debugging provider output.
