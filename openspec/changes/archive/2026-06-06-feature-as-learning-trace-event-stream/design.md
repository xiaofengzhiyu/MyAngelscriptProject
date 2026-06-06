## Context

The deleted v1 exporter coupled snapshot data, hand-authored visualizer hints, and runtime probes into one file. v2 separates the layers cleanly: framework / phase-taps / examples / exporter / commandlet, each in its own folder. This makes adding future phase taps (Parser, Builder, ClassGen, VM) a localized change instead of a cross-cutting refactor.

## Goals / Non-Goals

**Goals:**

- One coherent JSON contract: every event has `{ seq, phase, type, pos?, data }` shape.
- High-fidelity tokenizer trace (real `asCTokenizer` helpers, not a re-implementation).
- Drift-resistant: a regression test asserts `FTokenizerTap` produces the same final tokens as the unmodified `asCTokenizer` across the entire example set.
- Phase-tap interface (`ILearningTracePhaseTap`) future-proof enough that adding Parser / Builder / ClassGen / VM taps requires only new files under `Phases/` and registry entries — no changes to Core or Exporter.
- Curated example set covers all interesting tokenizer decision branches (whitespace, comments oneline/multiline, numeric constants in multiple radices, string constants with/without escapes, keywords vs identifiers, edge cases like unclosed strings).

**Non-Goals:**

- Web-side animation, rendering, or interaction.
- Arbitrary user-input AS via commandlet UI (curated set only for now).
- Parser/Builder/ClassGen/VM phase taps in this change (architecture supports them, but each is its own future change).
- Reviving any deleted v1 tooling, including the static visualizer's data feed.

## Decisions

- **Subclass + reuse, not subclass + copy.** Earlier exploration considered mirroring all 5 `IsXxx` helpers verbatim (with manual sync against AS upstream). Reading the actual `asCTokenizer` source revealed that `GetToken` → `ParseToken` is just a 5-call dispatch; the helpers themselves don't need to be copied. The tap subclasses `asCTokenizer`, calls the original protected helpers (which inheritance grants access to), and only mirrors the 5-line dispatch order. Drift surface ≈ 5 lines, not ~400.

- **Bare `asCScriptEngine` for tokenizer access.** `asCTokenizer` reads `engine->ep.allowUnicodeIdentifiers` (and conditionally `engine->ep.scanner`). The exporter creates a bare engine via `asCreateScriptEngine()` and assigns it to the tap, avoiding any dependency on `FAngelscriptEngine` or test-module helpers. Lifetime: created at exporter start, released at exporter teardown.

- **Per-example JSON file, not one big file.** Each curated example produces its own `<example-id>.json` under `Saved/LearningTrace/`. Easier for the future web layer to load incrementally and easier to diff in code review. The exporter also writes an `index.json` listing all examples.

- **Event sequence numbers are per-example, not global.** Each example's event stream restarts at `seq: 0`. Simplifies the web side (one example = one timeline).

- **Drift guard runs against every example.** The drift test loops over the registry and asserts `FTokenizerTap` emits the exact same final token sequence as the unmodified `asCTokenizer`. This is the strongest available signal that the mirror is faithful, and it scales as the example set grows.

- **No commandlet input flexibility for v2.0.** The commandlet only takes `OutputDir=` (defaults to `Saved/LearningTrace/`). No `Scenario=` / `Source=` / etc. The example set is the contract; mutating it requires C++ edits, which is the right gate for "what gets taught."

- **Phase tap interface stays minimal.** `ILearningTracePhaseTap` only declares a `Run(const FLearningTraceExample&, FLearningTraceEventStream&)` method plus a `GetPhaseName()` accessor. No multi-phase orchestration. The Exporter loops examples × taps; each tap is independent.

## Risks / Trade-offs

- **AS upstream merge breaks the mirror.** Mitigation: drift test fires on any final-token divergence. Header annotation `// MIRRORED FROM as_tokenizer.cpp@<HEAD-SHA>` reminds reviewers to re-check after merges. Worst case: dispatch order changes upstream → drift test fails on at least one example → manual sync.

- **Event volume.** A 30-line example might produce hundreds of events (per character try-X attempts). For curated examples this is fine; the JSON files are small (<100KB each). Future arbitrary-input mode would need event budgeting.

- **`asCScriptEngine` direct access is a private-header peek.** This is consistent with how the deleted v1 exporter worked (`StartAngelscriptHeaders.h` pattern). It locks the new code to the AS fork; not portable. Acceptable because the fork is the project's baseline.

- **Tokenizer-only first delivery means the web demo will be limited.** Acceptable because the deliverable is the framework + JSON contract, not the demo. The first follow-up change adds Parser, doubling teaching value.

## Future Notes

When future phase taps are added:

- Parser tap: subclass `asCParser`, walk the AST tree post-`ParseScript`, emit per-node events with parent links.
- Builder tap: peek private state at compile-phase boundaries via `StartAngelscriptHeaders.h`. Phase-boundary observer pattern, no logic copy.
- ClassGen tap: bind to public `FAngelscriptClassGenerator::OnClassReload` / `OnEnumCreated` / `OnStructReload` / `OnFullReload` / `OnPostReload` multicast delegates. Pure listener — no source modification.
- VMContext tap: wrap `asIScriptContext::SetInstructionCallback` with event recording. Per-instruction before/after via `asSVMInstructionInfo`.

Each new tap implements `ILearningTracePhaseTap` and registers with the Exporter. No edits to existing taps or Core needed.
