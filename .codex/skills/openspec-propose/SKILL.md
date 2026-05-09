---
name: openspec-propose
description: Use when the user starts or updates an OpenSpec change proposal in AngelscriptProject, including /opsx:propose or /openspec:proposal, and needs Superpowers-backed thinking before OpenSpec artifacts are generated.
---

# OpenSpec Propose With Superpowers Adapter

Create or continue an OpenSpec change. In this project, propose is a design and artifact workflow, not implementation.

<!-- SUPERPOWER-BEGIN: superpowers-dispatch-rule -->
Use the current installed Superpowers skills by name at the point where they apply. Do not vendor or copy Superpowers instructions into this skill; let Superpowers updates take effect through the active installed skill.

For Codex, use the platform's skill-loading behavior. If a direct skill activation tool is unavailable, load the current `SKILL.md` for the named Superpowers skill from the session skill metadata before following it.
<!-- SUPERPOWER-END: superpowers-dispatch-rule -->

<!-- SUPERPOWER-BEGIN: propose-thinking-flow -->
Before creating or editing OpenSpec artifacts, use `superpowers:brainstorming` for requirement discovery and design confirmation.

Apply only the thinking portion of brainstorming:
- Explore the project context first.
- Ask focused questions until the goal, scope, constraints, and success criteria are clear.
- Present 2-3 viable approaches with tradeoffs and a recommendation when meaningful.
- Present the chosen design and wait for explicit user confirmation before writing artifacts.

OpenSpec overrides for brainstorming:
- Do not write `docs/superpowers/specs`.
- Do not invoke `superpowers:writing-plans`.
- Do not create `docs/plans` or `docs/superpowers/plans`.
- Store the approved design in OpenSpec artifacts instead.
<!-- SUPERPOWER-END: propose-thinking-flow -->

## Input

The user should provide either a kebab-case change name or a description of what they want to build or fix. If the request is unclear, ask what change they want and stop until they answer.

## Steps

1. **Clarify and confirm the design**

   Follow the `propose-thinking-flow` adapter block. Do not create files until the user has confirmed the design direction.

2. **Check worktree safety**

<!-- SUPERPOWER-BEGIN: worktree-check -->
Prefer proposing from an isolated feature worktree. Check the current Git context with non-mutating commands such as:

```powershell
git rev-parse --show-toplevel
git rev-parse --git-dir
git rev-parse --git-common-dir
git worktree list --porcelain
```

If the current directory is the main repository working tree, stop and offer:
- create or switch to a feature worktree,
- continue in the current directory with explicit user approval,
- cancel.

If the user chooses to create a worktree, use `superpowers:using-git-worktrees` as guidance, but do not silently edit `.gitignore`, commit changes, or move work without explicit confirmation.
<!-- SUPERPOWER-END: worktree-check -->

3. **Create or continue the OpenSpec change**

<!-- SUPERPOWER-BEGIN: openspec-cli-compat -->
OpenSpec CLI metadata is authoritative. Do not create a bare `openspec/changes/<name>` directory by hand.

For a new change, run:

```powershell
openspec new change "<name>"
```

Then inspect artifact state:

```powershell
openspec status --change "<name>" --json
```

For each ready artifact, get current schema instructions:

```powershell
openspec instructions <artifact-id> --change "<name>" --json
```

Use the returned `outputPath`, `template`, `instruction`, `context`, `rules`, and dependency list. Read dependency artifacts before generating dependent artifacts. Preserve `.openspec.yaml` and all OpenSpec CLI state.
<!-- SUPERPOWER-END: openspec-cli-compat -->

4. **Generate artifacts until apply-ready**

   Continue in dependency order until every artifact required by `applyRequires` is complete. Re-run `openspec status --change "<name>" --json` after each artifact.

5. **Use `tasks.md` as the implementation plan**

<!-- SUPERPOWER-BEGIN: plan-as-tasks-md -->
OpenSpec `tasks.md` is the only implementation plan for this project. Do not invoke `superpowers:writing-plans`, and do not create `docs/plans` or `docs/superpowers/plans`.

The plan is complete when:
- OpenSpec apply-required artifacts are generated through the OpenSpec CLI flow.
- `tasks.md` has explicit, runnable verification commands for each implementation task.
- TDD and non-TDD tasks are distinguishable.
- The user has enough context to review and run `/opsx:apply`.
<!-- SUPERPOWER-END: plan-as-tasks-md -->

<!-- SUPERPOWER-BEGIN: tasks-template -->
When creating implementation tasks, shape them for Superpowers-backed execution without copying Superpowers docs.

Use TDD tasks for new behavior, bug fixes, behavior changes, and complex logic:

```markdown
- [ ] N.M <task description>  <!-- TDD -->
  - [ ] N.M.1 Write a failing test: `<test file or test name>`
  - [ ] N.M.2 Run verification and confirm the failure is expected: `<command>`
  - [ ] N.M.3 Implement the smallest change: `<implementation file>`
  - [ ] N.M.4 Run verification and confirm it passes: `<command>`
  - [ ] N.M.5 Refactor while keeping verification green
```

Use non-TDD tasks for documentation, mechanical config, dependency updates, generated files, and other work where a failing behavioral test is not appropriate:

```markdown
- [ ] N.M <task description>  <!-- Non-TDD -->
  - [ ] N.M.1 Execute the change: `<file path>`
  - [ ] N.M.2 Verify no regression: `<command>`
  - [ ] N.M.3 Check completeness and affected references
```

For this Unreal project, prefer existing repository verification entry points when relevant:
- `powershell.exe -NoProfile -ExecutionPolicy Bypass -File Tools\RunBuild.ps1 -Label <label> -TimeoutMs 180000`
- `powershell.exe -NoProfile -ExecutionPolicy Bypass -File Tools\RunTests.ps1 -TestPrefix "<prefix>" -Label <label> -TimeoutMs 600000`
- `powershell.exe -NoProfile -ExecutionPolicy Bypass -File Tools\RunTestSuite.ps1 -Suite <suite> -LabelPrefix <label> -TimeoutMs 600000`
<!-- SUPERPOWER-END: tasks-template -->

6. **Finish propose**

   Show the change name, artifact paths, and current OpenSpec status. Tell the user to review the artifacts and run `/opsx:apply <name>` when ready.

## Guardrails

- Propose must not edit application code.
- Keep all generated artifacts under OpenSpec CLI output paths.
- Do not require `plan-eng-review-codebuddy`, `proposal-challenger`, or `preci-code-check`.
- If a user asks to implement while still proposing, explain that implementation starts with `/opsx:apply` after artifacts are ready.
