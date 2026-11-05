# Project-Gap-Register.md — 全部缺口统一登记台账（P6 总入口）

> 规则：本项目所有已证明的缺口集中于此。每项最终只能收敛为：
> `IMPLEMENTED + VERIFIED` / `VERIFIED — ACCEPTED LIMITATION` /
> `DEFERRED — EXPLICIT DECISION` / `NOT APPLICABLE — WITH REASON`。
> 初始状态一律 `OPEN`（-附子状态）。禁止无负责项与退出标准的 OPEN 长期滞留。
> 历史阶段记录（docs/）保持不可变；本台账只登记当前事实。

## A. 资产与打包（P6-1 / P6-6）

| ID | 子系统 | 现状（a9daee4 基线证据） | WP | 本轮目标 | 状态 | 证据 |
|---|---|---|---|---|---|---|
| ASSET-01 | 模型导入 | 无任何 glTF/GLB/OBJ 解码器；扩展映射已存在（AssetType/ResourceType Mesh=5），但 `Content::AssetType` 仅 Texture/Script | P6-1 | glTF/GLB/OBJ 导入管线 | OPEN | audit §二；`docs/P6-P1A-Asset-Import-Contract.md` §1/§2 |
| ASSET-02 | 纹理预处理 | `TexturePreprocessRule` 为 no-op 占位（声称输出不写文件）；GL 上传全线性（无 sRGB internal format） | P6-1 | 真实预处理 + sRGB/Linear 分类 + 运行时 sRGB 接入 | OPEN | AssetPipeline.cpp:121-147; OpenGLTexture.cpp |
| ASSET-03 | 材质/GPU 上传 | PBRMaterialParams(80B std140) 存在未接入 MeshRenderer；MasterMaterial/MaterialInstance 与 GL path 分离 | P6-1 (runtime 子项) | v1 材质绑定（pipes §6） | OPEN | PBRTypes.h |
| PACK-01 | Pak | 只读 PakFile（stored 无解压、无写侧） | P6-6 | Pak Writer/Reader + 原子替换 + 损坏检测 | OPEN | Core/Content/PakFile |
| PACK-02 | 打包分发 | 无 portable 包/no 构建/验证链 | P6-6 | Windows Portable Demo + clean-machine 起动 | OPEN | — |

## B. 编辑器（P6-2）

| ID | 子系统 | 现状 | WP | 本轮目标 | 状态 | 证据 |
|---|---|---|---|---|---|---|
| EDITOR-01 | Transform 契约 | ABI 场景契约 position-only；TransformComponent 内部有 quat 但未暴露托管/Dispatch | P6-2 | additive TRS API（不改布局） | OPEN | capi.h v1.9; SceneSerializerV1（仅 position） |
| EDITOR-02 | Undo/Redo | 托管侧无 undo（native 侧 imgui UndoSystem 有，未导出）；无 Ctrl+Z/Redo UI | P6-2 | 托管命令历史（含 Coalescing） | OPEN | MainWindow.axaml.cs:661 |
| EDITOR-03 | 放置 | 无点击放置/拖放；无 ray 触发的实体创建 | P6-2 | Viewport click placement（含 DPI/letterbox 处理） | OPEN | — |
| EDITOR-04 | Gizmo | gizmo API/工具栏 legacy-only，EngineHost 不可用 | P6-2 | Move/Rotate/Scale Gizmo（单选 v1） | OPEN | ViewportPanel; capi.h Phase7 注释 |
| PICK-01 | Pick transport | `EditorHostAbiTest.D3PickRequestPumpFetchHappyPath` 双配置稳定 FAIL（8/9；HIT 预期落空） | P6-2 | 修复并获 UIA 级/validation double-run 证据 | **OPEN — EVIDENCE CAPTURED (known RED restored verbatim)** | tests/test_bridge/test_pick_transport.cpp:198-234; P6-0 record §2.1 |
| BUILD-01 | 构建注册 | test_pick_transport 缺 EngineCore usage-requirements（C1083，历史残留侥幸编译） | P6-0 | 修复 | **FIXED（待 P6-0 提交）** | tests/CMakeLists.txt（本轮 diff） |
| EDITOR-05 | Release UIA / 真窗口像素对比 | 仅 Debug UIA 真机验证过；Release UIA 缺口；Direct↔Graph 真实窗口像素级对比未做 | P6-2/P6-7 | Release UIA 基本流程 + 像素级对比（P6-4 Golden 支撑） | OPEN | P5-F1M-U1 §4 |
| EDITOR-06 | 多显示器 | D10 停靠 robustness 仅部分验证（E2/E3/E4 未跑） | P6-2/P6-7 | 有双屏硬件时波浪式验证 | OPEN | ADR-011 |

## C. RenderGraph（P6-3）

| ID | 子系统 | 现状 | WP | 本轮目标 | 状态 | 证据 |
|---|---|---|---|---|---|---|
| RG-01 | DAG 正确性 | SG6 已知未验证项（RAW/WAR/WAW/Cycle/Topology/Lifetime/Mutation/Handles/FailurePropagation） | P6-3 | 完整套件（Correctness Only） | OPEN | P4-D-RenderGraph-SG6-Closure-Record |
| RG-02 | ExecuteParallel | 并行 path 已实现未全面验证；生产接线为明确非目标 | P6-3 | 并行 correctness 验证（不接线） | OPEN | 同上 |
| RG-03 | Transient Texture | TransientHeap 只有 buffer 视角；无 tex descriptor/alias 证据 | P6-3 | Texture alias 语义实现+验证 | OPEN | TransientHeap.cpp |
| RG-04 | 测试覆盖 | RenderGraph 目标仅 8 测试 | P6-3/P6-4 | 扩充 DAG/E2E 网格 | OPEN | tests 分册 |

## D. 质量自动化（P6-4）

| ID | 子系统 | 现状 | WP | 本轮目标 | 状态 | 证据 |
|---|---|---|---|---|---|---|
| QA-01 | 黄金图像 | 无像素黄金图回归；全部为语义断言 | P6-4 | 首批 8 类场景 + 容差比较 + CI 上传 | OPEN | — |
| QA-02 | Fuzzing | 无 fuzzer；仅 3 条 SceneAdversarial | P6-4 | scene/manifest/importer 三 fuzzer（libFuzzer+ASan CI 分层） | OPEN | — |
| QA-03 | ASan suppressions | `asan_suppressions.txt` 存在但未被 CTest env 注入 | P6-4 | CI 自动注入 + 每条有 issue/owner | OPEN | root CMakeLists / third_party 注释 |
| QA-04 | Coverage | `ENABLE_COVERAGE` 死开关；无实现 | P6-4 | 实现或移除（先实现 GCC/Clang 路径） | OPEN | root CMakeLists.txt |
| QA-05 | 性能回归 | P5-F2 基准存在（~1.02×）；无 CI 门禁/INCONCLUSIVE 语义 | P6-4 | 双层 CI：PR 报告 + 稳定 Runner 门禁（1.10× 预算初设） | OPEN | P5-F2-RenderGraph-Performance-Record |
| QA-06 | 弱覆盖模块 | ECS 21/Job 4/Math(V3+M4)/RG 8 | P6-4 | Invarian 测试扩充（禁止水分测试） | OPEN | P6-0 record+audit |
| QA-07 | Console 五项 | T-6 五项 KNOWN UNVERIFIED（GL loss、protocol 违规、10k 行性能、partial-failure、Inconclusive-as-PASS 风险） | P6-5(证据任务) | 逐项结论（含 DEFERRED 收口） | OPEN | T-6-Overall-Completion-Audit §9.2 |

## E. 运行时能力（P6-5）

| ID | 子系统 | 现状 | WP | 本轮目标 | 状态 | 证据 |
|---|---|---|---|---|---|---|
| PHYS2D-01 | 2D 序列化 | Box2D body serialization TODO；Bare2D 未对齐 | P6-5 | Body serialization + 双后端回归 | OPEN | Box2DPhysicsBody & Core/Physics TODO |
| PHYS3D-01 | DrawText3D | 文本/LOD no-op（只画 marker） | P6-5 | cam pos + LOD + 可见验证（不重写 DebugRenderer） | OPEN | JoltDebugRenderer.cpp |
| AUDIO-01 | Streaming | stream 为 flag 无 chunked queue | P6-5(第2轮) | Streaming runtime（underflow/EOF/device loss） | OPEN | OpenALAudioSource/AudioAssetManager |
| AUDIO-02 | Codec | WAV+OGG supporteed；MP3/FLAC 无 | P6-5(第2轮) | 决策 +（若批）接入 | OPEN | AudioClip 加载器 |
| ANIM-01 | GPU Skinning | CPU 完备；GPU 有上传路径无 compute pass | P6-5(第2轮) | CPU正确性→GPU path→compute→差异比较 | OPEN | SkinningComponent / ShaderCompileRule pipeline |
| SHADER-01 | ShaderGraph | 生成 HLSL 无编译/链接闭环（无 dxc/HLSL→SPIRV） | P6-5(第2轮) | compile→reflection→pipeline→像素证 | OPEN | ShaderNodeFactory GenerateHLSL |
| VFX-01 | VFXGraph | 面板+runtime 编译器早期 | P6-5(第2轮) | 最小粒子 runtime 闭环 | OPEN | VFXGraphPanel/VFXRuntime |
| RENDER-01 | 光源 | MeshRenderer 4 点光源上限，deferred light UBO TODO | P6-5 | 评估真实架构限制 + 独立 Lighting Gate | OPEN | MeshRenderer.cpp:159-199; DeferredLightingPass TODO |
| RENDER-02 | Hi-Z | `hi_z_comp.comp` 未接线（OcclusionPass? 未） | P6-5 | 实际 compute/culling 使用或 NOT APPLICABLE 关闭 | OPEN | assets/shaders/hi_z_comp.comp |
| RENDER-03 | IBLBaker / CompactGBuffer | header-only 无实现 | P6-5(第2轮) | 独立实现或 DEFERRED-AUTHORIZED | OPEN | include 头文件 |
| RENDER-04 | GPU 物理 | compute-only 粒子 demo | (backlog) | 通用 GPU solver = 明确非目标 | **DEFERRED — EXPLICIT** | GPUPhysicsEngine |
| VULKAN-01 | Timestamp | GPUProfiler.cpp 4 处 TODO；GL 有实现 | P6-5 | Vulkan timestamp 演示级实现 | OPEN | RHI/GPUProfiler.cpp |
| D3D12-01 | 后端 | Alpha (single file, no CSM/post/AA) | (backlog) | 生产化 = 非目标 | **DEFERRED — EXPLICIT** | D3D12Device.cpp |
| PLAT-01 | 手柄 | 键鼠 only；无 gamepad/热插拔 | P6-5(第2轮) | Input abstraction（Windows 首发） | OPEN | GlfwInput |
| SCRIPT-01 | 脚本做法 | Lua 手写绑定；sandbox 已有 | (backlog) | 高级绑定/sol2 = 非目标本轮 | OPEN | ScriptAPI/GameplayAPI |
| MISC-01 | 材质即时标签 | ResourceGeometry pipeline 纹理 type-sRGB bools 未消费 | P6-1 | 并入 ASSET-02 | OPEN | AssetTypes.h |
| ASSET-04 | submesh 渲染 | Mesh 无 submesh；MeshRenderer 单材质绑定 | P6-1 后续 | runtime 多图元/多材质绘制 | OPEN | Mesh.h; P6-P1A §2 |
| ASSET-05 | Normal mip 归一化 | mip 生成在 GL 层按普通颜色平均，normal 通道未按向量规整 | P6-1 后续 | 专用 mip 策略或 NOT APPLICABLE 关闭 | OPEN | P6-P1A §4.5 |

## F. 文档/仓库/调试（P6-0 / P6-7）

| ID | 子系统 | 现状 | WP | 本轮目标 | 状态 | 证据 |
|---|---|---|---|---|---|---|
| REPO-01 | dirty submodules | 3 个 sample-residue 删除；gitlinks 未漂移 | P6-0 | 分类而且予以保留（Status 2） | **CLOSED — RETENTION POLICY RECORDED** | P6-0 record §3.1 |
| REPO-02 | crashes/ | 115 项 ~1.04GB 单日 (2026-09-30) 事件 | P6-0 | 只盘点不删（放 P6-7 定归档规则） | **CLOSED — INVENTORY ONLY (policy)** | P6-0 record §3.2 |
| DOC-01 | README 漂移 | Vulkan "95% 主后端"、VMA "3.x"(实为 v3.4.0)、Box2D "3.0"（实为 v3.1.1）、sandbox 21/33 等 | P6-7 | 与代码一致（含脚本生成清单） | OPEN | audit §6. 註記 |
| DOC-02 | CI 记录准确性 | CI 矩阵与本地入口差距 | P6-7 | README CI 段改写 | OPEN | .github/workflows/* |
| DEBUG-01 | Crash 快照覆盖 | 快照单日（crashes/）+跟踪冲程无 | P6-7 | 长期保留策略（配合 REPO-02） | OPEN | P6-0 record §3.2 |
| DRAFTS-01 | `#if 0` 测试草稿 | 3 段（SolidFillReadback/WireframeNoFill/P5F3_Sanity）排除计数中 | P6-4 | 清理或正式化（并入黄金图工作流的证据链） | OPEN | test_debugdraw.cpp; Source alignment record |

## G. 已划为非目标（不阻塞 P6，保留原文依据）

- Vulkan 1.3 生产主后端化（保持 Demo 后端定位） — 否目标（P6-24 note）
- D3D12 生产化 — 否目标
- 全量 Renderer 迁移 / ExecuteParallel 生产接线 / 通用 GPU Physics Solver / 网络多机能力 / Unity-Unreal 功能对等 / 全平台安装发布（Windows 之外） — 非目标
EOF
