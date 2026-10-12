# 交接：headless GPU 像素验证 —— Vulkan 路线现状

**Owner**：任何希望让 Linux CI 获得真实 GPU 像素证据的人
**分支**：`avalonia` @ `7b6b7f1`
**起点**：接续 `docs/Handoff-CI-Remaining-Failures.md` 中记录的 GPU 限制

---

## 0. 最重要的一件事：图形管线路径完全不可用

**接手前请先读这一节。**

`VulkanDevice::CreateGraphicsPSO` 传两个空句柄作为着色器模块：

```cpp
// VulkanDevice.cpp:333
auto* pso = CreateGraphicsPipeline(m_Impl->device, layout, desc,
                                  VK_NULL_HANDLE, VK_NULL_HANDLE);
```

而 `CreateGraphicsPipeline` 对空模块的处理是直接放弃：

```cpp
if (vsModule != VK_NULL_HANDLE) { ... }   // 跳过
if (fsModule != VK_NULL_HANDLE) { ... }   // 跳过
if (stages.empty()) { "no shader stages provided"; return nullptr; }   // 必然命中
```

**因此 `CreateGraphicsPSO()` 永远返回 nullptr。**

`GraphicsPSODesc::vertexShader` / `pixelShader`（`StringID`）在整个
`engine/src/Vulkan/` 中**从未被解析成 SPIR-V** —— 对这两个字段全目录搜索，
**零命中**。着色器 ID 到 SPIR-V 的解析子系统**根本不存在**。

连带：`GraphicsPSODesc` 也没有顶点输入描述字段（无 vertex buffer / attribute
布局），即使着色器解决了，也没有表达带属性顶点流的方式。

### 这意味着什么

引擎的 Vulkan 后端**无法创建任何图形管线**，因此无法绘制任何东西。
本路线剩下的工作不是「把线接上」，而是「造一条线」——具体来说是：

1. 着色器注册表/缓存（GLSL → SPIR-V，按 StringID 存取）
2. `CreateGraphicsPSO` 接入它，替换两个 `VK_NULL_HANDLE`
3. `GraphicsPSODesc` 的顶点输入契约（该结构被 GL / D3D12 后端共用，会牵动跨后端设计）

### 一处需要更正的判断

此前的工作笔记曾把该后端描述为「比预想更完整，只需补呈现层」。**这个判断是错的。**
它基于代码看起来完整（有 `vkCreateGraphicsPipelines`、有 dynamic rendering 配置），
而不是基于它能工作。实际上从着色器解析到管线创建，主链在中间断开。

已完成的其它部分（环境可行、可编译、离屏设施、命令池）不受此影响，依然成立。

---

## 0.1 为什么有这份文档

Linux runner 提供不了 OpenGL 4.6（引擎的产品承诺），而 `llvmpipe` 最高只有 4.5、
`Zink` 在 lavapipe 上无法初始化。因此 4 个 `GPUSkinningPixelTest` 用例在 CI 上
长期处于**显式 SKIP** 状态，CI 持续输出：

```
GPU-SKINNING-STATUS: NOT VERIFIED on this runner (explicit skip above).
```

本轮沿 Vulkan 路线推进到「技术前提全部用证据建立」为止，随后收手。
**不是路线失败，是工作量已超出「给 Animation GPU 蒙皮补 CI 证据」这个原始目标。**

---

## 1. 已验证（可复现）

### 1.1 环境侧：lavapipe 能真实离屏光栅化 ✅

run `38308346989` 的 `gl-capability-probe` job。这是**像素断言**，不是「退出码为 0」：

```
[VKPROBE] device: llvmpipe (LLVM 20.1.2, 256 bits)
[VKPROBE]   apiVersion = 1.4.318  type = 4 (CPU)
[VKPROBE]   graphics queue family = 0
[VKPROBE]   R8G8B8A8_UNORM color attachment = YES
[VKPROBE] logical device OK -> offscreen Vulkan rendering is feasible here
```

`tools/vk_offscreen_probe.cpp`（独立、不链接引擎）跑通完整链路
image → view → render pass → framebuffer → pipeline → draw → copy → map：

```
[VKOFF] centre pixel = 64 128 191 255
[VKOFF] PASS: fragment shader output reached the readback
```

clear 为黑、片元着色器输出 `vec4(0.25,0.5,0.75,1.0)` = `64,128,191,255`，实测一致。
**结论：headless 软件 Vulkan 渲染在本 runner 上真实可用。**

### 1.2 引擎后端侧：15/15 文件可在 Linux 编译 ✅

run `38316605255` 的 `vulkan-backend-compile` job，`compile-all-objects rc = 0`，0 error。

**此前该后端从未被任何 CI 编译过** —— 主构建 job 从不安装 `libvulkan-dev`，
`find_package(Vulkan)` 恒失败。诊断 job 打开后立刻暴露：

```
engine/src/Vulkan/VulkanDevice.cpp:19:10: fatal error: windows.h: No such file or directory
```

`7b6b7f1` 已修（见 §3）。现在 15 个文件全部编译通过，含新增的 `VulkanOffscreen.cpp`。

> **编译通过 ≠ 后端可用。** 请务必连同 §0 一起读：本节只说明「能构建」，
> 而图形管线路径在运行时是完全断开的。

### 1.3 离屏设施：已写好并编译通过 ✅

`engine/include/Engine/Vulkan/VulkanOffscreen.h` + `engine/src/Vulkan/VulkanOffscreen.cpp`

**关键设计更正**：最初按「render pass + framebuffer」写，随后发现这是错的 ——
`VulkanPipelineState.cpp:133` 用 `VkPipelineRenderingCreateInfo` 挂到
`VkGraphicsPipelineCreateInfo::pNext`，即 **dynamic rendering**，管线不带 render pass。
两者互不兼容。已改为提供真正缺的那一半：颜色附件 image +
`vkCmdBeginRendering`/`vkCmdEndRendering` + 自带屏障/copy/submit/map 的回读。

只用 `VulkanDevice` 已公开的句柄，**不改 `IRHIDevice`**，不影响 GL / D3D12。

---

## 2. 尚未完成（运行时空洞，编译期暴露不出）

### 2.0 着色器解析缺失 —— 见 §0，首要阻塞

`CreateGraphicsPSO()` 恒返回 nullptr。详见 §0。

### 2.1 ~~`GetOrCreateThreadCommandPool()` 是桩~~ ✅ 已修复（`ad8f97b`）

```
VulkanDevice.cpp:387  VkCommandPool VulkanDevice::GetOrCreateThreadCommandPool() { return VK_NULL_HANDLE; }
```

`VulkanOffscreenTarget::Readback()` 依赖它分配 command buffer，返回空句柄必然失败。

已修复：`Impl` 增加设备级 `cmdPool`，`Initialize` 创建（与帧资源并列）、`Shutdown` 销毁，
函数返回它。真正的 per-thread 池需要 `thread_local` 存储，但 headless 一次性录制只需
一个可重置的池。

同时修掉同源的第二处桩：`VulkanCommandList::Begin()` 原本要求 `cmdBuffer` 已存在，
却从无任何分配。现改为惰性从该池分配。
（`CreateCommandList` 本就从 frame resource 提供 cmdBuffer，那条路径不受影响。）

### 2.2 `VulkanTexture` 是空壳

`engine/src/Vulkan/VulkanTexture.cpp` 全文 **0 个 `vkCreateImage`**，只有 getter/setter，
持有 `VkImage` / `VmaAllocation` 字段。纹理创建路径尚未实现。
离屏目标自管 image，暂时绕开了它；但第 3 步的像素测试需要纹理。

### 2.3 尚未有任何渲染侧调用 `vkCmdBeginRendering`

后端能创建设备、buffer、pipeline 并录制 `vkCmdDrawIndexed`，
但**渲染作用域**这一环此前完全缺失（`VulkanOffscreen` 是第一个使用者）。
接线时应验证：`VulkanCommandList` 录制的命令在 `vkCmdBeginRendering` 作用域内是否可用
（dynamic rendering 下 renderPass/framebuffer 为空是合法的）。

### 2.4 `GetVulkanDeviceInfo()` 硬编码

```
VulkanDevice.cpp:406  std::string GetVulkanDeviceInfo() noexcept { return "Vulkan 1.3"; }
```

返回值与实际设备无关。应改为读 `VkPhysicalDeviceProperties`。

### 2.5 未验证：设备能否真正初始化

`CreateVulkanDevice()` 从未被执行过。`BackendTest`（`sandbox/src/BackendTest`，
**HRC-OWNED**）是唯一会调用它的 consumer，但它在 CI 里没跑过。

---

## 3. 已落地的改动

| 提交 | 内容 |
|---|---|
`62f531b` | 新增 Vulkan 能力探针；修正 `EditorDemoApp.h` 过期注释（原写「GPU 蒙皮未接通」） |
`4b379c5` | 探针 job 补装 `libvulkan-dev` |
`07f5ae7` | 修「探针自我掩盖崩溃」：管道取 `tee` 退出码 + 块缓冲丢失 → 改为重定向 + 显式退出码 + `stdbuf` |
`b1f704f` | 修 `vkGetPhysicalDeviceFormatProperties(..., nullptr)` 段错误 |
`f23619e` | 新增离屏探针（带像素断言）；着色器用 `printf` 而非 heredoc（heredoc 结束标记顶格会破坏 YAML 块标量） |
`67e6d28` | 修 `cb` 变量名冲突（color blend / command buffer） |
`4cb586f` | 修 `corner` 标量被下标访问 |
`017007b` | 新增 `VulkanOffscreen` 设施；新增 `vulkan-backend-compile` 诊断 job |
`03ef491` | 诊断 job 改为只编译 Vulkan 目标文件（全量构建被 runner SIGTERM，exit 143） |
`7b6b7f1` | **让 Vulkan 后端可在非 Windows 编译** |
`ad8f97b` | **实现真实命令池**：修 `GetOrCreateThreadCommandPool()` 桩 + `VulkanCommandList::Begin()` 的 cmdBuffer 分配 |

`7b6b7f1` 的四处改动（`engine/src/Vulkan/VulkanDevice.cpp`，该目录 clean）：

1. `windows.h` / `vulkan_win32.h` 加 `#ifdef _WIN32` 守卫，非 Windows 改为包含 `<dlfcn.h>`
2. `VK_KHR_win32_surface` 实例扩展的 push 同步守卫
3. Win32 surface 创建（`VkWin32SurfaceCreateInfoKHR`）同步守卫
4. `HasVulkanSupport()` 改跨平台：Windows 用 `LoadLibraryA`，POSIX 用 `dlopen("libvulkan.so.1")`

四处都只服务**带窗口的呈现路径**，headless 离屏渲染不需要。

---

## 4. 建议的接手路径

**先决条件**：§0 的着色器解析子系统。没有它，后面每一步都无法执行。

1. 实现着色器注册表：GLSL → SPIR-V，按 `StringID` 存取
   （`ShaderCompiler` 目前只是 asset pipeline 的一个规则，见 §0，不是运行时注册表）
2. 在 `VulkanDevice::CreateGraphicsPSO` 中用它替换两个 `VK_NULL_HANDLE`
3. 补 `GraphicsPSODesc` 的顶点输入契约 —— 注意该结构被 GL / D3D12 共用，
   属跨后端设计，最好先与其它后端 owner 对齐
4. 在 `sandbox/` 新增 headless 三角形目标：创建设备 → 离屏目标 → 建管线 →
   `vkCmdBeginRendering` → 绘制 → 回读 → **断言像素**
   （`sandbox/CMakeLists.txt` clean，可直接加目标；用 `#ifdef ENGINE_HAS_VULKAN` 守卫）
5. 目标通过后，再把 `GPUSkinningPixelTest` 抽象为后端无关
6. 届时把 `GPU_SKINNING_NO_GL46=1` 从 Linux 测试步骤中移除

> 经验提示：这个后端有多个「代码看起来完整、实际不工作」的地方，且只能靠运行
> 逐个暴露。上一轮连续三轮各暴露一个空洞（command pool → PSO 着色器 → 顶点输入），
> 每轮都是一次 CI。动手前值得先通读整个 `engine/src/Vulkan/` 判断规模。

### CI 现状

- `vulkan-backend-compile` job：**独立、continue-on-error、always 上传日志**，只回答
  「Vulkan 后端能否编译」，不会 gate 主构建
- `gl-capability-probe` job：持续采集 GL 与 Vulkan 能力证据
- 主构建 job **未**启用 Vulkan（不装 `libvulkan-dev`），因此 `7b6b7f1` 的改动
  对主构建零影响，无回归

### 所有权边界

| 路径 | 状态 |
|---|---|
`engine/src/Vulkan/`、`engine/include/Engine/Vulkan/` | clean ✅ |
`sandbox/CMakeLists.txt` | clean ✅ |
`.github/workflows/cmake-multi-platform.yml` | clean ✅ |
`engine/CMakeLists.txt` | **HRC-DIRTY**（含 `find_package(Vulkan)` 与 GLOB，**无需修改**）
`engine/src/RHI`、`sandbox/src/BackendTest` | **HRC-DIRTY** ❌ 不可触碰

注意 `engine/CMakeLists.txt` 用 `file(GLOB VULKAN_SOURCES CONFIGURE_DEPENDS src/Vulkan/*.cpp)`，
**新增 .cpp 会被自动纳入**，不需要（也不应该）去改那个文件。
