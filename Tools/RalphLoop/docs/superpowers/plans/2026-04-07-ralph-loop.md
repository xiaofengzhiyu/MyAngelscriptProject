# Ralph Loop Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Extend the current PowerShell Ralph loop into a provider-driven runner that supports `codex` and `opencode`, stable reference clones, and opt-in real CLI tests.

**Architecture:** Keep orchestration in `ralph-loop.ps1`, add a small data-driven provider profile layer, preserve the existing stop-hook contract, and keep batch logic thin. Store SSH-cloned references in a stable `references/` area and keep real CLI tests separate from the default mock smoke path.

**Tech Stack:** PowerShell 5+/7, Windows batch, Codex CLI, OpenCode CLI

---

### Task 1: Introduce provider profiles

**Files:**
- Create: `agents/profiles.psd1`
- Modify: `ralph-loop.ps1`
- Modify: `tests/test-ralph-loop.ps1`

- [x] **Step 1: Add failing smoke coverage for default `codex` resolution and explicit provider selection**
- [x] **Step 2: Run `powershell -NoProfile -ExecutionPolicy Bypass -File tests/test-ralph-loop.ps1` and confirm the new assertions fail first**
- [x] **Step 3: Implement a minimal `AgentProfile` contract with command template, console prefix, and home-resolution fields**
- [x] **Step 4: Re-run the smoke suite and keep backward-compatible codex aliases working**

### Task 2: Keep wrappers and prompt flow provider-neutral

**Files:**
- Modify: `run-ralph-loop.bat`
- Modify: `prompts/loop.txt`
- Modify: `ralph-loop.ps1`

- [x] **Step 1: Add generic parameters such as `-Agent`, `-AgentCommand`, and `-AgentHome` while keeping existing codex entrypoints valid**
- [x] **Step 2: Keep `.bat` behavior thin and limited to shell selection plus argument forwarding**
- [x] **Step 3: Keep prompt rendering and iteration artifacts stable across providers**
- [x] **Step 4: Verify direct PowerShell and batch entrypoints still exercise the same loop contract**

### Task 3: Add stable reference sync support

**Files:**
- Create: `references/manifest.psd1`
- Create: `sync-references.ps1`
- Create: `tests/test-reference-sync.ps1`

- [x] **Step 1: Define SSH clone targets for `vercel-labs/ralph-loop-agent`, the Shanselman gist mirror, and `iannuttall/ralph`**
- [x] **Step 2: Implement idempotent clone-or-fetch behavior into stable `references/` subdirectories**
- [x] **Step 3: Prove the helper never writes clones into `.codexloop/` or `tests/.tmp/`**

### Task 4: Split default smoke tests from real CLI tests

**Files:**
- Modify: `tests/test-ralph-loop.ps1`
- Create: `tests/test-real-agents.ps1`
- Create: `tests/test-helpers.ps1`

- [x] **Step 1: Keep the existing mock suite as the default fast path**
- [x] **Step 2: Gate real `codex` and `opencode` runs behind explicit environment flags and command-availability checks**
- [x] **Step 3: Assert artifacts, exit codes, timeouts, and stop conditions instead of exact model wording**
- [x] **Step 4: Prefer `pwsh` for wrapper-level integration coverage on Windows**

### Task 5: Keep docs aligned with the real target

**Files:**
- Modify: `Agents.md`
- Modify: `tests/AGENTS.md`
- Modify: `docs/superpowers/AGENTS.md`
- Modify: `docs/superpowers/specs/2026-04-07-ralph-loop-design.md`
- Modify: `docs/superpowers/plans/2026-04-07-ralph-loop.md`

- [x] **Step 1: Remove codex-only wording from design and plan docs**
- [x] **Step 2: Document the provider-profile boundary, reference-clone location, and real-test split**
- [x] **Step 3: Keep future edits provider-neutral unless a file is intentionally provider-specific**
