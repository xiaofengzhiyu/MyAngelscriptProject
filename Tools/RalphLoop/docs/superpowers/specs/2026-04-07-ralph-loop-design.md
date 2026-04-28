# Ralph Loop Design

## Goal

Implement a Windows-oriented PowerShell loop shell that can repeatedly invoke provider profiles, preserves per-run state on disk, and lets the caller drive prompt text and runtime parameters from the batch layer.

## Scope

- `ralph-loop.ps1` stays the single loop engine and resolves one provider profile before the iteration loop starts.
- Provider profiles own command templates, console labels, home env/path rules, and any extra provider environment.
- `stop-hook.ps1` keeps the verification contract agent-neutral and optional.
- `run-ralph-loop.bat` stays a thin Windows entrypoint and is responsible only for forwarding prompt-related inputs and runtime arguments.
- `prompts\loop.txt` stays the reusable prompt skeleton.
- `references/` and real CLI tests are supporting tooling for development and validation, not the core loop contract.

## Behavior

- Resolve provider selection from explicit parameter, then environment, then default to `codex`.
- Resolve provider home from explicit parameter, then provider environment, then provider default home path.
- Create a timestamped run folder under `.codexloop\runs` unless overridden.
- For each iteration, render a prompt file from the caller-supplied task and prompt context, run the selected provider command, persist stdout/stderr, and call the stop hook.
- Print a compact summary header plus iteration log paths and live status timing to the terminal by default; only mirror raw agent stdout/stderr to the terminal when `-StreamAgentOutput` is enabled.
- Keep iteration artifacts stable across providers so verification and debugging stay consistent.
- Stop early when verification succeeds; otherwise stop at `MaxIterations`.
- Keep workflow strategy inside the prompt text rather than hardcoding task-specific branches into PowerShell.

## Testing

- Keep mock-based PowerShell smoke tests as the default fast suite.
- Add opt-in real CLI tests for `codex` and `opencode` behind explicit environment gates and command-availability checks.
- Verify provider selection, provider-home propagation, prompt propagation, timeout behavior, and early-stop behavior.
- Verify default status-only terminal output and opt-in raw-output streaming mode.
- Prefer `pwsh` when wrapper behavior and encoding consistency matter.
- Assert artifacts and exit codes before exact model text.

## Notes

- `codex` and `opencode` do not share the same non-interactive CLI contract; normalize differences inside provider profiles or wrappers.
- The repo started as a codex-only MVP. Future edits must preserve backward compatibility while moving docs and parameters toward provider-neutral names.
- The intended shape is a thin loop shell: `.bat` passes prompt text and parameters, the shell repeats the call, and the prompt itself defines what each round should try to achieve.
