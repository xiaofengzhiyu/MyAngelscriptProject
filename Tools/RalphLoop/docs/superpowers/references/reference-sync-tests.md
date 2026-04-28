# `Temp/reference-sync-tests`：它如何支持 loop

## 先说结论

`Temp/reference-sync-tests` 不是一个 agent loop 参考实现，它不直接跑任何 AI agent。它的作用是给“参考仓库同步机制”提供本地测试夹具，间接支撑当前 loop 项目的开发流程。

## 为什么它会出现在 `Temp/`

- `sync-references.ps1` 的默认本地缓存根目录就是 `Temp`，见 `sync-references.ps1:1` 到 `sync-references.ps1:5`。
- `tests/test-reference-sync.ps1` 会专门创建 `Temp/reference-sync-tests/references` 作为一次临时同步目的地，见 `tests/test-reference-sync.ps1:31` 到 `tests/test-reference-sync.ps1:39`。

## 它具体验证了什么

- 测试脚本先在 `tests/.tmp/reference-sync/reference-sources` 里造两个本地 git 仓库，再把它们写入一个临时 manifest，见 `tests/test-reference-sync.ps1:31` 到 `tests/test-reference-sync.ps1:67`。
- 随后它执行 `sync-references.ps1`，要求第一次结果是 `cloned`，第二次结果是 `updated`，见 `tests/test-reference-sync.ps1:68` 到 `tests/test-reference-sync.ps1:83`。
- 它还故意构造 `..\escape-repo` 这种越界路径，验证脚本必须拒绝，见 `tests/test-reference-sync.ps1:84` 到 `tests/test-reference-sync.ps1:103`。

## 这和 loop 有什么关系

- 当前仓库需要参考外部 Ralph 实现来指导设计，但参考仓库本身不能和运行态 `.codexloop/` 混在一起。
- `sync-references.ps1` 会显式拒绝把同步目标放进 `.codexloop` 或 `tests/.tmp`，见 `sync-references.ps1:77` 到 `sync-references.ps1:86`。
- 真正同步每个 reference 时，脚本还会校验目标路径必须留在 `DestinationRoot` 内，并在 clone 失败时回退到 `Temp` 本地缓存，见 `sync-references.ps1:97` 到 `sync-references.ps1:126`。

换句话说，这个夹具不是为了“推进 loop 轮次”，而是为了保证参考材料的落盘位置可预测、可重复、不会污染 loop 运行目录。对当前仓库来说，这是一种基础设施层面的 loop 支撑。

## 为什么也值得单独留档

- `Temp/` 顶层现在不只有外部参考仓库，还有这个测试夹具。
- 如果不把它单独说明，后续很容易把它误认成第四个 loop 引擎参考。
- 单独写清楚后，后续维护者就能知道：`reference-sync-tests` 的价值在于保护 reference workflow，而不是提供新的 loop 模式。

## 一句话总结

`reference-sync-tests` 通过验证 reference sync 的安全边界和幂等行为，间接支持当前 loop 项目对外部参考仓库的稳定使用。
