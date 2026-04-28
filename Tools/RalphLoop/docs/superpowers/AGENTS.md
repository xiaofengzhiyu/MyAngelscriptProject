# DOCS KNOWLEDGE BASE

## OVERVIEW

`docs/superpowers/` stores the living design and execution plan for this repo.

## WHERE TO LOOK

| Task | Location | Notes |
|------|----------|-------|
| Architecture target | `docs/superpowers/specs/2026-04-07-ralph-loop-design.md` | Describe target state, not just current MVP |
| Implementation roadmap | `docs/superpowers/plans/2026-04-07-ralph-loop.md` | Break work into exact files, commands, and verification steps |

## CONVENTIONS

- Keep spec and plan in sync with the actual repo target.
- Use provider-neutral wording when the repo goal is multi-agent support.
- Name exact files to create or modify.
- Include concrete PowerShell or batch commands for verification.
- When scope changes, update spec first, then plan.

## ANTI-PATTERNS

- Do not leave docs in a codex-only state when the target is `codex` + `opencode`.
- Do not write vague plan steps such as “add support” or “improve tests”.
- Do not document reference clones under runtime directories.
- Do not describe real CLI tests as default smoke coverage.
