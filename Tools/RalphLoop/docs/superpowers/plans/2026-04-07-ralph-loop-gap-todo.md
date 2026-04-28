# Ralph Loop Gap Todo

**Goal:** Clarify how far the current `ralph-loop` tool is from the intended thin provider-driven loop-shell target, then turn the remaining gap into a concrete execution todo list.

**Current Status:** The repo now has the thin provider-driven loop shell the project actually needs: `ralph-loop.ps1` repeats agent calls, `.bat` supplies prompt text and runtime parameters, and `codex` / `opencode` are supported through provider profiles. The main remaining gap is live end-to-end validation in environments where the real CLIs and credentials are available.

**Tech Stack:** PowerShell 5+/7, Windows batch, Codex CLI, OpenCode CLI

---

## Snapshot

### Already Working

- `ralph-loop.ps1` runs an iterative loop, renders prompts, captures stdout/stderr, persists run state, and stops on verify success.
- `stop-hook.ps1` keeps an agent-neutral verify contract: `0` stop, `1` continue, `>1` error.
- `run-ralph-loop.bat` stays thin and already prefers `pwsh`.
- `tests/test-ralph-loop.ps1` covers the current codex-style MVP with mocks.
- Timeout behavior is implemented and verified, including process-tree termination and exit code `124`.
- Final log files preserve UTF-8 content even when the console path is less reliable.
- Terminal output now defaults to a compact summary header and live status lines, with raw agent output available only through `-StreamAgentOutput`.

### Partially Done

- Real-agent validation remains opt-in because it depends on installed CLIs and available credentials in the local environment.

### Still Missing

- An actual local run of `tests/test-real-agents.ps1` with `RALPH_TEST_REAL_AGENTS=1` in an environment where both agent CLIs are available and authenticated.

## Capability Matrix

| Capability | Status | Evidence |
|------------|--------|----------|
| Core Ralph loop engine | Done | `ralph-loop.ps1` |
| Verify/continue/stop contract | Done | `stop-hook.ps1` |
| Thin Windows wrapper | Done | `run-ralph-loop.bat` |
| Mock smoke coverage | Done | `tests/test-ralph-loop.ps1` |
| Timeout safety | Done | `ralph-loop.ps1`, `tests/test-ralph-loop.ps1` |
| UTF-8 file logging | Done | `ralph-loop.ps1`, `hello-codex.ps1` |
| Provider abstraction | Done | `agents/profiles.psd1`, generic `-Agent` / `-AgentCommand` / `-AgentHome` flow |
| `opencode` support | Done | `opencode` profile and provider-selection smoke coverage |
| Stable reference sync | Done | `references/manifest.psd1`, `sync-references.ps1`, and populated `references/` clones |
| Real CLI tests | Done | `tests/test-real-agents.ps1` exists and is env-gated |
| Generic parameter migration | Done | compatibility aliases kept while generic names are primary |
| Provider-neutral prompt metadata | Done | prompt now renders `AGENT_HOME` and selected agent |

## Test Coverage Status

### Present Coverage

- `tests/test-ralph-loop.ps1` already covers direct PowerShell execution, batch wrapper execution, custom home propagation, extra prompt propagation, early-stop behavior, and timeout exit code `124`.
- `tests/mock-agent.ps1`, `tests/mock-verify.ps1`, and `tests/mock-slow-agent.ps1` provide a deterministic mock test harness.
- Encoding and shell-specific repro helpers already exist under `tests/pwsh-file-repro*.ps1`.

### Partial Coverage

- Stdout/stderr artifact capture is exercised, but not deeply validated beyond the timeout case.
- `state.json` and verify log contents are generated, but are not fully asserted.
- Stop-hook behavior is covered for `0` and `1`, but not for `>1` verification failures.

### Missing Coverage

- No verified real-agent run has been executed yet in this workspace because the suite is intentionally gated behind `RALPH_TEST_REAL_AGENTS=1` and installed CLIs.

## Reference-Derived Implications

The cloned references in `Temp/` suggest the target is not just “run another command,” but a cleaner reusable framework.

- `Temp/vercel-labs-ralph-loop-agent` reinforces the need for composable stop conditions, template-driven iteration context, and a provider-neutral orchestration layer.
- `Temp/iannuttall-ralph` reinforces the need for an agent registry, separate command templates per provider, and a clearer reference/config area.
- `Temp/shanselman-ralph-gist` reinforces the value of a PowerShell-native control loop, but also shows why shell behavior and logging need to stay explicit and testable on Windows.

That means the missing work is not only feature work; it is also boundary work:

- [x] Move provider differences out of the core loop body
- [x] Move reference management out of ad-hoc manual cloning
- [x] Move real-agent verification out of the default smoke path

## Distance To Target

The current tool is **functionally at the intended thin-shell target, with live CLI execution intentionally gated**.

- The **foundation layer is mostly done**: loop orchestration, logging, stop behavior, batch entrypoint, timeout protection, and default smoke tests are all in place.
- The **core target is now in place**: the repo has a thin provider-driven tool with `codex` + `opencode`, `.bat`-driven parameters, and a reusable PowerShell loop shell.
- The practical reading is: **the core loop is done; what remains is optional live-agent proof in a credentialed environment**.

## Remaining Workstreams

### Workstream 1: Provider Abstraction

**Outcome:** Replace codex hardcoding with one provider selection path.

- [x] Create `agents/profiles.psd1`
- [x] Define at least `codex` and `opencode` profiles
- [x] Add `-Agent`, `-AgentCommand`, and `-AgentHome` to `ralph-loop.ps1`
- [x] Keep `-CodexCommand` and current `CODEX_HOME` behavior as compatibility aliases
- [x] Move console prefix and env wiring behind the selected profile

### Workstream 2: `opencode` Support

**Outcome:** Make `opencode` a first-class provider rather than a future note in docs.

- [x] Add an `opencode` command template
- [x] Normalize its non-interactive behavior behind the provider layer
- [x] Confirm prompt transport strategy for `opencode` vs `codex`
- [x] Add smoke assertions that explicit provider selection switches behavior correctly

### Workstream 3: Stable Reference Sync

**Outcome:** Turn ad-hoc reference clones into a predictable, repeatable local reference system.

- [x] Create `references/manifest.psd1`
- [x] Create `sync-references.ps1`
- [x] Move from temporary ad-hoc clones toward stable `references/` subdirectories
- [x] Keep clone/update logic idempotent
- [x] Add tests proving references never end up in `.codexloop/` or `tests/.tmp/`

### Workstream 4: Real CLI Test Layer

**Outcome:** Keep default tests fast, while adding optional real-agent confidence.

- [x] Create `tests/test-real-agents.ps1`
- [x] Create `tests/test-helpers.ps1`
- [x] Gate real CLI tests behind explicit environment flags
- [x] Check command availability before running `codex` or `opencode`
- [x] Assert artifacts, exit codes, timeouts, and stop behavior instead of model wording

### Workstream 5: Provider-Neutral Naming Cleanup

**Outcome:** Align implementation naming with the intended architecture.

- [x] Replace internal codex-only naming where it blocks multi-provider support
- [x] Update prompt metadata from `CODEX_HOME` toward generic provider-home wording
- [x] Keep backward compatibility for existing wrapper and test entrypoints
- [x] Update mock agent snapshots to understand generic home naming alongside legacy codex naming

## Suggested Execution Order

1. **Provider abstraction first** - everything else depends on it.
2. **`opencode` provider second** - validates the abstraction is real, not cosmetic.
3. **Real CLI test layer third** - proves both providers behave correctly without slowing default smoke tests.
4. **Stable reference sync fourth** - useful for workflow, but not required to make the multi-provider loop run.
5. **Naming cleanup last** - finish the migration after behavior is stable.

## Definition Of Done For The Target

The current repo can be considered at the intended target only when all of the following are true:

- [x] `ralph-loop.ps1` selects a provider profile instead of hardcoding codex behavior
- [x] `codex` and `opencode` both run through the same loop contract
- [x] `run-ralph-loop.bat` stays thin while still forwarding the needed generic arguments
- [x] `references/` contains a manifest-driven sync flow
- [x] Default mock smoke tests still pass quickly
- [x] Real CLI tests exist, are opt-in, and verify artifacts rather than exact wording
- [x] Docs, tests, and implementation all describe the same target state

## Notes

- `Temp/` still contains useful cached references for immediate study, while `references/` is now the stable sync target.
- The current codex MVP is already valuable; the remaining work is mostly about turning it into the intended reusable multi-provider tool.
- The old implementation plan in `docs/superpowers/plans/2026-04-07-ralph-loop.md` remains the execution backbone, but this document is the clearer progress/gap view.
