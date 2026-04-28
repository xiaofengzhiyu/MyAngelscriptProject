# Temp 参考对象索引

本目录整理了 `Temp/` 下每个顶层对象与 Ralph loop 的关系，方便后续回看“哪些参考对象直接实现 loop、哪些对象只是为 loop 提供配套支撑”。

## 已整理对象

- `Temp/iannuttall-ralph` → `docs/superpowers/references/iannuttall-ralph.md`
- `Temp/vercel-labs-ralph-loop-agent` → `docs/superpowers/references/vercel-labs-ralph-loop-agent.md`
- `Temp/shanselman-ralph-gist` → `docs/superpowers/references/shanselman-ralph-gist.md`
- `Temp/reference-sync-tests` → `docs/superpowers/references/reference-sync-tests.md`

## 结论概览

- `iannuttall-ralph`：把 loop 做成“文件驱动的 story 状态机”，每轮只处理一个 story，并用 `<promise>COMPLETE</promise>` 回写完成状态。
- `vercel-labs-ralph-loop-agent`：把 loop 抽象成一个 TypeScript 库，核心是“外层验证循环 + 内层 tool loop + 可组合 stop conditions”。
- `shanselman-ralph-gist`：展示了最薄的 PowerShell loop shell，依赖 `stop-hook.ps1` 的退出码来决定继续还是结束。
- `reference-sync-tests`：不是 agent loop 本身，而是 reference sync 的测试夹具；它保证参考仓库不会污染 `.codexloop/` 或 `tests/.tmp/`，间接支撑当前仓库的 loop 开发流程。

## 为什么这里有四份文档

`Temp/` 当前有四个顶层目录：`iannuttall-ralph`、`vercel-labs-ralph-loop-agent`、`shanselman-ralph-gist`、`reference-sync-tests`。前三个是直接可借鉴的 loop 参考对象；第四个不是 loop 引擎，但它确实是 `Temp/` 里的顶层对象，并且承担了 reference 基础设施的验证职责，因此也单独留档。
