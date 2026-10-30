# T-7 Audio — Stage 0 Baseline

**状态**：`T-7 GATE 0 BASELINE — READ-ONLY`（Gate 0 裁决：`PASS — READ-ONLY COMPLETE`）
**日期**：2026-10-30
**后续**：Gate A 契约已冻结于 `docs/T-7-Audio-GateA-Contract.md`；本文件保留基线证据。
**范围**：T-7 Audio EFX / Thread Safety 的只读基线核对。
**未执行**：源码修改、构建、测试运行、设备/EFX 运行时探测、子模块更新/重置/清理、
commit/push/tag/merge。`third_party/openal-soft` 仅读取状态，未触碰。

---

## 1. Git 与工作树

| 项 | 观测 |
|---|---|
| 分支 | `avalonia` |
| HEAD | `74ad0999b35b1783ea9e58667966d3e84917dea8` |
| 提交时间 | 2026-10-29T22:12:10+08:00 |
| 提交标题 | `feat: commit T-2 Content v2 M1-A~F, T-1 RigidBody C/D/E/F, T-4 GPUProfiler` |
| engine/bridge/tests 差异 | `git diff --stat -- engine bridge tests` 为空 |
| audio 路径工作树状态 | `engine/src/Audio`、`engine/src/OpenAL`、音频头文件、`tests/test_audio` 无改动 |

已修改的受跟踪文件（均为 T-6-A / UI-H2 既有改动，不是 T-7 改动）：

- `editor_avalonia/App.axaml`
- `editor_avalonia/MainWindow.axaml`
- `editor_avalonia/MainWindow.axaml.cs`
- `editor_avalonia/Services/EditorHostService.cs`
- `editor_avalonia/ViewModels/MainViewModel.cs`

既有未跟踪交付物包括 T-6-A / UI-H2 的文档、证据、门禁服务与 runner，以及 `crashes/`；
本 Gate 0 仅新增本文件与 `docs/T-7-Audio-Gate0-Scan.md`，未修改任何既有未跟踪文件。

音频子系统最后一次提交：

| 项 | 值 |
|---|---|
| commit | `e9f6428d9b337bc750e4053c7b25fdbf976d4d29` |
| 时间 | 2026-10-05 14:11:03 +0800 |
| 标题 | `fix(audio): skip OpenAL buffer delete when no context is current` |

该提交只覆盖 `OpenALAudioBuffer` / `AudioClip` 的“无 context 时跳过删除”保护；
`OpenALAudioSource` 析构路径未获得同等保护（见扫描报告 R-02）。

---

## 2. 脏子模块与受保护资产

| 子模块 | 提交 | 工作树脏项 | 处置 |
|---|---|---|---|
| `third_party/d3d12ma` | `1d86c1130f61453634b1df85782e1fecfd59a525` | `D bin/D3D12Sample.exe` | 未触碰 |
| `third_party/openal-soft` | `9fb7bb38cb35f50c692edaba47bad13d2bb879ba` | `D build/.empty` | **未触碰、未更新、未重置、未清理** |
| `third_party/vma` | `3aa921224c154a0d2c43912bc88e1c42ce1f7607` | `D bin/Shader.frag.spv`、`D bin/Shader.vert.spv`、`D bin/VmaSample_Release_vs2026.exe` | 未触碰 |

`crashes/`：115 个产物，未触碰。共 17 个子模块，除上表 3 个外其余 14 个工作树状态未显示为脏。

---

## 3. T-2 关键文件哈希复核

与 `docs/T-1-Stage0-Baseline.md` §1 的基线逐项比较：

| 文件 | SHA-256 | 结果 |
|---|---|---|
| `engine/src/Core/Content/RevisionStore.cpp` | `d03bbe873959599fea0cc719a73e9210996f8cf4ad5bf222c7a437289368b1f2` | MATCH |
| `engine/include/Engine/Core/Content/RevisionStore.h` | `aa5c2d838c1f998ad2887d4540d205944c5fa84c4368e021eb2e26cdbd4f1ec0` | MATCH |
| `engine/src/Core/Content/AtomicFile.cpp` | `b7d109df1f05ee82673c4eff4d403aad26c7657d56949dbb42217adb86461eb9` | MATCH |
| `engine/src/Core/Content/ProjectPaths.cpp` | `0abfd67987ec4d2c0e29a3a715a3c2ffdb05df3e80289869827ae9050c857036` | MATCH |
| `engine/src/Core/Content/Utf8Path.cpp` | `dd67d4040b419d95369262cfd198303a58785ff9630f73a4730e7098c3521121` | MATCH |
| `bridge/include/editor_bridge/capi.h` | `02dbce3118a4fe7d1038f23efcff3c7e5b03dfdb44315c2226515c79604b2886` | MATCH |
| `docs/T-2-Closure-Record.md` | `355c513f0a22c2ca95d41b7ccfd6b5bd7ffbba85c453c3b489661d56a862222c` | MATCH |
| `tests/test_bridge/test_revision_commit.cpp` | `8c8fd870fe633c1d2021577f48c840770272d224ff64dd269eb8099cfae71b3a` | MATCH |

八项全部 MATCH；本 Gate 0 未改写上述文件。

---

## 4. 已关闭阶段状态（上下文，不在本轮重新裁决）

| 项目 | 状态 |
|---|---|
| T-1 RigidBody | `CLOSED — SCOPE-BOUND` |
| T-2 Content Pipeline | `CLOSED — SCOPE-BOUND` |
| #141 Managed Import E2E | `PASS` |
| F2-S1 Collider Serialization | 独立未关闭，不阻塞 T-7 |
| T-4 GPUProfiler | `PASS — SCOPE-BOUND` |
| T-6-A Console | 已完成，行为契约冻结 |
| T-6-B Engine 日志接入 | 延期，独立任务 |
| UI-H1 | `FROZEN` |
| UI-H2 P0/P1/P2 | `CLOSED — SCOPE-BOUND` |

本轮只读扫描不重新执行这些阶段的测试，也不改变其状态。

---

## 5. 音频扫描输入指纹

以下是 Gate 0 实际阅读的核心实现与测试文件的 SHA-256；用于确认扫描期间输入未变化。

| 文件 | SHA-256 |
|---|---|
| `engine/src/Audio/AudioAssetManager.cpp` | `ef7145e266ba91ca1523f4b82e10dacd109d05d0e03d6f40464e38542ef51e50` |
| `engine/src/Audio/AudioBusManager.cpp` | `d0a856a50151214eafc71436b579b3d5c9c0104e11c940bc95554d0abc75adfd` |
| `engine/src/Audio/AudioClip.cpp` | `16d8d7024db81b3de122969df3988d4cef8bed7de7a3b2812b46fdf685f2d6e7` |
| `engine/src/Audio/AudioClipManager.cpp` | `8086e685a596473c2f681afe060be1ebd8c72b96fe1bb6c2ea11fba91de8d263` |
| `engine/src/Audio/AudioEffect.cpp` | `11993c73fff84951aaa2d633f04d88f1306efc206fb6ab4934a1a1082d397871` |
| `engine/src/Audio/AudioEngine.cpp` | `5398ab5535a38162af34572f34c426c2582c4ad3d56d85ac3cc214c64553d02f` |
| `engine/src/Audio/AudioLoader.cpp` | `d01d12e5c70210662d45f4b800036746e1ea2b66accc846495f23b3edad4db52` |
| `engine/src/Audio/AudioSource.cpp` | `3cc18084120914d156a97575956c8462ab77df6796329177e5280a556cad2511` |
| `engine/src/Audio/AudioSourceComponent.cpp` | `9c6db5ec0856f44975bd52a31c24cd895de68ed6fadaed882bdffdef6d6d325d` |
| `engine/src/Audio/AudioSystem.cpp` | `835f03fbe3a2425360a72f0e53a9d390c2bebce84cf46d98cbf4321bb7917b1a` |
| `engine/src/Audio/Listener.cpp` | `282f6c061432ca9baa1c011fb236389f42d74e31eb02fc04c102853a69475396` |
| `engine/src/Audio/SpatialAudioManager.cpp` | `023eb874a0b925d303a4afc77415e3a2823f7e0c0efbd89971e0f561ac3bb677` |
| `engine/src/OpenAL/OpenALAudioBuffer.cpp` | `ec0b5a0d0be1844cb4522ea40d7c724a74eea9c314ce4b70efb45ec3def7b9f1` |
| `engine/src/OpenAL/OpenALAudioEngine.cpp` | `ea379450634fb75512e03f370fdf71d5e251fd3cbb676ae07c8c05414a9bb70c` |
| `engine/src/OpenAL/OpenALAudioSource.cpp` | `1f71c9d2e8bf2cda1a549ffecf31beb0c0c11be4a46e0e2e4448a81ada5ad08f` |
| `tests/test_audio/AudioAssetManagerTest.cpp` | `e67de892ab9af1ff2c82709446958991b7767d20230b7e38d91e77d6778c0418` |
| `tests/test_audio/AudioBusManagerTest.cpp` | `95c02ecf49203964c6780176988ae789ec17be8047ee9a777c26851ad4a906f2` |
| `tests/test_audio/AudioLoaderTest.cpp` | `ecd2e197a1af5c304c1af2352b6642f0b16761f8a0d1c58bd3e50dc8804d0eb8` |
| `tests/test_audio/ListenerTest.cpp` | `50641cc6c0facd80c4a0207e97a8b115c1603fec5e4db7b94695d0a52a0509d1` |
| `tests/test_audio/SpatialAudioManagerTest.cpp` | `f3c30250d25285948afbc23f5595da56eb218596cb365fb1f23626b3819a31cc` |

接口与高层头文件（`IAudioEngine.h`、`IAudioSource.h`、`IAudioBuffer.h`、`AudioTypes.h`、
`AudioEngine.h`、`AudioSource.h`、`AudioClip.h`、`Listener.h`、OpenAL 三个头文件）
也已逐个哈希并阅读；此处不重复列出，扫描报告按行号引用。

---

## 6. Gate 0 边界

- 不构建、不运行测试、不启动音频设备、不查询 EFX 扩展、不运行 ASan/TSan。
- 不修改 `engine/`、`bridge/`、`tests/`、`sandbox/` 或 `third_party/`。
- 不更新、重置、清理或切换 `third_party/openal-soft`；其现有 `D build/.empty` 脏项保持原状。
- 不执行任何 Git 写入。
- 扫描结论只基于当前 HEAD 的源码、测试与已存文档；不宣称 OpenAL Soft 内部线程模型
  或运行时行为，除非另有实测证据。
