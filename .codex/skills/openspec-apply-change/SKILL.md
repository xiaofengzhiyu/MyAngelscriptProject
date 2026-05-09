---
name: openspec-apply-change
description: Use when the user wants to implement, continue, verify, or work through tasks from an OpenSpec change in AngelscriptProject, including /opsx:apply or /openspec:apply.
---

# OpenSpec Apply With Superpowers Adapter

Implement tasks from an OpenSpec change. OpenSpec remains the task source; Superpowers provides execution discipline when its trigger conditions apply.

<!-- SUPERPOWER-BEGIN: superpowers-dispatch-rule -->
Use the current installed Superpowers skills by name instead of copying their instructions here. Load and follow the current skill at the moment it applies.

Phase mapping:
- Use `superpowers:test-driven-development` for tasks marked `<!-- TDD -->` or for new behavior, bug fixes, behavior changes, and complex logic.
- Use `superpowers:systematic-debugging` for unexpected test failures, build failures, runtime bugs, or unclear behavior.
- Use `superpowers:verification-before-completion` before claiming a task, change, build, or test run is complete or passing.
- Use `superpowers:receiving-code-review` before implementing review feedback from a user or reviewer.
- Use `superpowers:requesting-code-review` when a major feature, shared behavior, or merge-ready change needs review and the environment can support the review workflow.
- Use `superpowers:finishing-a-development-branch` only after implementation is complete and fresh verification evidence exists.
<!-- SUPERPOWER-END: superpowers-dispatch-rule -->

## Input

The user may specify a change name. If omitted, infer only when unambiguous. If multiple active changes exist, ask the user to choose.

## Steps

1. **Select and inspect the OpenSpec change**

   Run:

```powershell
openspec status --change "<name>" --json
openspec instructions apply --change "<name>" --json
```

   Use the returned schema, task progress, dynamic instruction, and `contextFiles`. If the apply instruction reports a blocked or missing-artifact state, stop and report the missing artifacts.

2. **Read OpenSpec context files**

   Read every file listed in `contextFiles`. Do not assume file names; OpenSpec schema output is authoritative.

<!-- SUPERPOWER-BEGIN: plan-as-tasks-md -->
`tasks.md` is the only implementation plan. Do not invoke `superpowers:writing-plans`, and do not create `docs/plans` or `docs/superpowers/plans`.

If implementation reveals a design or requirement problem, update the relevant OpenSpec artifact for the current change, then continue from the updated task list.
<!-- SUPERPOWER-END: plan-as-tasks-md -->

3. **Analyze tasks before editing code**

<!-- SUPERPOWER-BEGIN: execution-mode-selection -->
Before implementation, summarize pending tasks and classify them as TDD or non-TDD from `tasks.md` markers and task content.

Recommend execution mode:
- `agent` for up to 5 mostly sequential tasks or tightly coupled changes.
- `subagent` only for clearly independent tasks or when the user explicitly wants delegated agent work.

Stop for user selection before spawning subagents. In Codex, use subagents only when explicitly selected or requested by the user.
<!-- SUPERPOWER-END: execution-mode-selection -->

4. **Execute tasks from `tasks.md`**

<!-- SUPERPOWER-BEGIN: tdd-discipline -->
For TDD tasks, use `superpowers:test-driven-development` and execute in order:
1. Write the failing test.
2. Run the listed command and confirm it fails for the expected reason.
3. Write the smallest implementation that can pass.
4. Run the listed command and confirm it passes.
5. Refactor while keeping verification green.

Do not write implementation code for a TDD task before observing the failing test.
<!-- SUPERPOWER-END: tdd-discipline -->

<!-- SUPERPOWER-BEGIN: non-tdd-discipline -->
For non-TDD tasks, execute the stated change, run the listed verification command, and check affected references before marking the task complete.
<!-- SUPERPOWER-END: non-tdd-discipline -->

<!-- SUPERPOWER-BEGIN: debugging-discipline -->
When any verification, build, test, or runtime behavior is unexpected, use `superpowers:systematic-debugging` before attempting fixes. Do not guess at fixes or stack patches without root cause evidence.
<!-- SUPERPOWER-END: debugging-discipline -->

5. **Update task checkboxes carefully**

   Update `tasks.md` only after the corresponding task or subtask is actually complete. Prefer a minimal patch that changes only `[ ]` to `[x]` on the relevant line. Do not add logs, review notes, or execution commentary to `tasks.md`.

6. **Verify before completion claims**

<!-- SUPERPOWER-BEGIN: verification-gate -->
Use `superpowers:verification-before-completion` before claiming success.

Fresh evidence must come from the task's verification command. For this repository, use the standard helper scripts when relevant:
- `powershell.exe -NoProfile -ExecutionPolicy Bypass -File Tools\RunBuild.ps1 -Label <label> -TimeoutMs 180000`
- `powershell.exe -NoProfile -ExecutionPolicy Bypass -File Tools\RunTests.ps1 -TestPrefix "<prefix>" -Label <label> -TimeoutMs 600000`
- `powershell.exe -NoProfile -ExecutionPolicy Bypass -File Tools\RunTestSuite.ps1 -Suite <suite> -LabelPrefix <label> -TimeoutMs 600000`

Do not use PreCI as a default requirement in this project. If a future environment provides a PreCI skill or command, treat it as optional unless the user explicitly enables it.
<!-- SUPERPOWER-END: verification-gate -->

7. **Handle review feedback with technical verification**

<!-- SUPERPOWER-BEGIN: review-policy -->
When receiving review feedback, use `superpowers:receiving-code-review` before implementing suggestions. Verify the feedback against this codebase, ask when unclear, and push back with technical evidence when a suggestion is wrong or out of scope.

Use `superpowers:requesting-code-review` for major or shared changes when review delegation is available. If no reviewer workflow is available, do a local review pass against OpenSpec requirements, changed files, tests, and integration risks, then report that no external reviewer was used.
<!-- SUPERPOWER-END: review-policy -->

8. **Finish or pause**

   If all tasks are done, run final verification and then use `superpowers:finishing-a-development-branch` to present branch completion options. If blocked, report the blocker, current progress, and the next decision needed.

<!-- SUPERPOWER-BEGIN: safe-rollback -->
Never run broad rollback commands such as `git checkout -- .` or `git reset --hard` as part of this workflow. If rollback is requested, show `git status --short` and `git diff --stat`, ask which paths to revert, and only revert explicitly confirmed paths.
<!-- SUPERPOWER-END: safe-rollback -->

## Guardrails

- Keep changes scoped to the current OpenSpec change.
- Do not skip task verification.
- Do not mark tasks complete based only on expectation or partial evidence.
- Do not depend on `plan-eng-review-codebuddy`, `proposal-challenger`, or `preci-code-check`.
