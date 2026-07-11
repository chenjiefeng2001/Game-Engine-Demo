# D3D12 集成情况分析报告

> **生成日期**: 2026-07-08  
> **分析范围**: `engine/include/Engine/Core/RHI/D3D12IRHIDevice.h`、`engine/src/D3D12/D3D12Device.cpp`、`engine/CMakeLists.txt`、`third_party/d3d12ma/`

---

## 一、D3D12 当前集成状态

### 1.1 现有代码统计

| 文件 | 行数 | 状态 |
|------|------|------|
| `engine/include/Engine/Core/RHI/D3D12IRHIDevice.h` | 164 | **完整声明** — 所有 IRHIDevice/IRHICommandList 方法覆盖 |
| `engine/src/D3D12/D3D12Device.cpp` | 777 | **完整实现** — 非 stub，含 D3D12MA/BindlessHeap/SwapChain |
| `third_party/d3d12ma/include/D3D12MemAlloc.h` | 3.5k+ | 完整 VMA for D3D12 第三方库 |
| `third_party/d3d12ma/src/D3D12MemAlloc.cpp` | 7k+ | 完整实现 |
| `engine/CMakeLists.txt` (相关行) | 33-101 | **构建集成完整** — 条件编译、d3d12.lib/dxgi.lib/dxguid.lib 链接 |

### 1.2 已实现的功能

D3D12Device.cpp 777 行包含以下完整实现：

| 功能 | 实现细节 |
|------|----------|
| **设备创建** | `CreateD3D12Device()` — 枚举 Adapter、Feature Level、D3D12MA 初始化 |
| **命令列表** | `D3D12CommandList` — Begin/End/Draw/DrawIndexed/Barrier/Viewport/Scissor |
| **交换链** | `D3D12SwapChain` — 双缓冲/三缓冲 Present、Resize |
| **内存分配** | D3D12MA — Upload/Default/Buffer/Texture 堆 |
| **全局根签名** | CBV (Space0) + DescriptorTable (Space1) + PushConstant (Space2) |
| **Bindless 堆** | 65536 entry 着色器可见描述符堆 |
| **管线状态** | `D3D12PipelineState` — CreateGraphics/ComputePipelineState |

### 1.3 构建集成

```cmake
# engine/CMakeLists.txt 第33-101行
if(WIN32 AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/D3D12")
    file(GLOB D3D12_SOURCES CONFIGURE_DEPENDS src/D3D12/*.cpp)
    list(APPEND ENGINE_SOURCES ${D3D12_SOURCES})
endif()

# ... 链接系统库
target_link_libraries(EngineCore PRIVATE d3d12.lib dxgi.lib dxguid.lib)
target_compile_definitions(EngineCore PUBLIC ENGINE_HAS_D3D12)

# D3D12MA 实现文件
target_sources(EngineCore PRIVATE "${PROJECT_SOURCE_DIR}/third_party/d3d12ma/src/D3D12MemAlloc.cpp")
```

---

## 二、为什么 D3D12 不是当前重点？

### 2.1 核心原因：跨平台策略选择

| 维度 | Vulkan 1.3 | D3D12 |
|------|-----------|-------|
| **平台覆盖** | Windows / Linux / macOS (MoltenVK) / Android | **仅 Windows** |
| **行业趋势** | AAA 引擎首选 (UE5/Unity 6) | 仅 Windows 平台 |
| **调试工具** | RenderDoc / Vulkan Validation Layers | PIX / VS Graphics Debugger |
| **学习价值** | 理解现代 GPU 架构通用概念 | 仅 Windows 生态 |

**结论**: 项目的学习目标和架构设计已经明确选择 Vulkan 作为主要现代图形 API。D3D12 虽然功能完备但维护成本/收益比不划算。

### 2.2 D3D12 无沙盒测试

已完成的 D3D12Device.cpp (777行) 没有任何对应的沙盒测试或渲染文档。相比之下：

- OpenGL 4.6: 有完整的 `SpriteBatchTest`、`_3DTest`、物理调试绘制
- Vulkan 1.3: 有完整的 `VulkanPipelineState`、`BindlessAllocator`、Tracy D3D12 profiling

D3D12 代码虽然"能编译"但缺乏任何使用验证。

### 2.3 维护成本过高

**假设要带 D3D12 到 Vulkan 同等水平 (95%)，需要：**

| 模块 | 工作量估算 | 备注 |
|------|-----------|------|
| ~~着色器反射 (SPIRV-Cross → DXIL)~~ | ~~1-2 周~~ | **❌ 已废弃 — 见 2.3.1** |
| 管线状态对象缓存 (PSOCache D3D12) | 3-5 天 | |
| 描述符环缓冲区 (DescriptorRingBuffer) | 3-5 天 | |
| Compute Culling / IDBuffer Pass | 1 周 | |
| GPUProfiler (D3D12 Timestamp) | 2-3 天 | |
| 物理调试绘制 (OpenGLPhysicsDebugDraw) | 3-5 天 | |
| **总计** | **2-3 周** | 更现实的估算 |

### 2.3.1 ⚠️ 架构决策：废弃 SPIRV-Cross → DXIL 路线

在业界实践中，**SPIR-V 转 DXIL 是一个极其危险的反模式（Anti-pattern）**：
- SPIR-V 转 DXIL 会丢失大量高级语义（如 UAV 屏障、波形操作、子对象）
- 极易引发难以排查的 Bug，且性能不可控
- 主流引擎（UE5、Unity、自研跨平台引擎）均不采用此路线

**修正后策略：未来若需复活 D3D12，统一采用 HLSL + DXC 双端编译方案**

```
当前资产 (Shader Source)
│
├── GLSL (现有) ──→ shaderc ──→ SPIR-V ──→ Vulkan (保持现状)
│                                       │
│                                       └── SPIRV-Cross ──→ GLSL（回读，调试用）
│
└── 未来统一方案 (HLSL as Single Source of Truth)
    │
    ├── HLSL ──→ Microsoft DXC ──→ DXIL ──→ D3D12
    │
    └── HLSL ──→ Microsoft DXC ──→ SPIR-V ──→ Vulkan (DXC 官方支持 HLSL→SPIR-V)
```

**行动项**:
- 当前 SPIRV-Cross 在此引擎中的角色仅限 **Vulkan PipelineLayout 反射**（读取 UBO/Sampler/SSBO/PushConstant 布局），无需改动
- 未来 Roadmap 中彻底删除「SPIRV-Cross → DXIL」的规划项
- 若需 D3D12，重构 Shader 资产管线为 **HLSL 单一事实来源 + DXC 双端编译**

### 2.4 物理迭代与 D3D12 无关联

v4.0-v7.0 的物理系统迭代（四元数→碰撞管道→Batch API→CharacterController→Joint3D→CCT 固定步进）全部在 CPU/逻辑层，与图形 API 无关。

**D3D12 能带来的唯一差异化价值**：用 PIX 调试 DC 的正确性。但 Vulkan 的 RenderDoc + Validation Layers 已经满足此需求。

---

## 三、技术现状：建议策略

### 3.1 保留 D3D12 但不推动

```
当前状态: 代码完整但"休眠"
建议策略:
├── 保持 ENGINE_HAS_D3D12 构建宏
├── 不主动维护/新增 D3D12 feature
├── 当 D3D12 构建因 WinSDK 更新出现问题时才修复
└── 删除 D3D12 代码的时机: 当维护成本 > 代码价值时
```

### 3.2 何时重新考虑 D3D12

| 场景 | 优先级 |
|------|--------|
| Windows-only 游戏发布 | 高 |
| Xbox Series X/S 移植 | 高 |
| PIX GPU 性能调优需求 | 中 |
| 团队 Windows 开发环境占比 100% | 低 |
| 仅学习用途 | **不投入** |

### 3.3 README 更新建议

当前 README 的 D3D12 描述应调整为"已实现但休眠"状态，而非"预留"或"未完成"。

> **已执行**: README.md 中 D3D12 已标记为 `⏸️ 休眠状态 — 777 行完整 IRHIDevice + D3D12MA，基础架构已跑通但功能未对齐 Vulkan，暂不作为日常迭代重心`

---

## 四、总结

| 问题 | 答案 |
|------|------|
| D3D12 代码存在吗？ | **存在** — 777 行完整 IRHIDevice 实现 + D3D12MA 集成 |
| 为什么不是重点？ | **跨平台策略**: Vulkan 覆盖 4 平台而 D3D12 仅 Windows。**维护成本**: 2-3 周全模块人力（且须采用 HLSL+DXC 而非 SPIRV→DXIL）。**无沙盒验证**: 编译通过但无实际使用 |
| 带它到 95% 需要什么？ | PSOCache、DescriptorRingBuffer、Compute Pass、GPUProfiler、物理调试绘制，共约 2-3 周（不含着色器反射——已废弃 SPIRV→DXIL 路线，若需复活统一采用 HLSL + DXC 双端编译） |
| 当前策略正确吗？ | **正确**。Vulkan-first 是跨平台引擎的工业标准选择。D3D12 保持"休眠"状态，不投入主动维护。未来若需复活，需将 Shader 资产管线重构为 HLSL 单一事实来源 + DXC 双端编译方案 |
