# 交接：headless GPU 像素验证 —— Vulkan 路线现状

**Owner**：任何希望让 Linux CI 获得真实 GPU 像素证据的人
**分支**：`avalonia` @ `7b6b7f1`
**起点**：接续 `docs/Handoff-CI-Remaining-Failures.md` 中记录的 GPU 限制

---

## 0. 当前唯一阻塞：三角形像素为全 0（**根因范围已缩小**）

**接手前请先读这一节。**

管线创建已经打通（见 §1.4），后端能跑、能建管线、能回读数据 —— 但**回读到的
像素全是 0**，连 clear 写入的 alpha=255 都没有。

```
[VKTRI] shaders registered (registry holds 2)
[VKTRI] device created: llvmpipe (LLVM 20.1.2, 256 bits)
[VKTRI] graphics pipeline created
[VKTRI] centre pixel = 0 0 0 0 (pitch=256)
[VKTRI] FAIL: centre pixel is not the shader colour
```

### 已确认的缺陷一：动态视口从未设置（已修，未验证）

`VulkanPipelineState.cpp:127` 把视口与裁剪声明为**动态状态**：

```cpp
VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
```

即管线要求绘制前动态设置视口/裁剪。而 `sandbox/src/VulkanOffscreenTest/main.cpp`
走的是裸 Vulkan 命令（`VulkanCommandList` 不暴露渲染作用域 API），因此从未调用
`vkCmdSetViewport` / `vkCmdSetScissor` → 视口未定义 → 绘制被整体裁掉，
**且 Vulkan 不会因此报任何错误**。

已在 `VulkanOffscreenTarget::BeginRendering` 末尾补上按目标尺寸设置视口与裁剪
（放在设施里而非测试里，因为所有调用方都有同一要求；管线若用静态视口，多调用一次也合法）。

**该修复未验证** —— 验证需要推送并触发 CI。

### 已确认的根因二：`att.clearValue` 从未赋值（已修，未验证）

```cpp
VkClearValue clear{};
std::memcpy(&clear.color, clearColor, sizeof(clear.color));   // clear 被填充…
...
att.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
// …然后 clear 再无任何使用，att.clearValue 保持零初始化
```

`loadOp = CLEAR` 读的是 **`att.clearValue`**，而不是那个局部变量。于是每次都清成
`(0,0,0,0)`，调用方传入的清除色被静默丢弃 —— 且没有任何 API 错误。

这解释了实测的 `0 0 0 0`：**clear 其实生效了，只是被清成了透明黑**。

### 两个缺陷叠加，才产生「全 0」

| # | 缺陷 | 单独存在时的效果 |
|---|---|---|
1 | `att.clearValue` 未赋值 | 图像被清成 (0,0,0,0)，请求的颜色被丢弃 |
2 | 动态视口/裁剪未设置 | 绘制被整体裁掉，什么都不写 |

两者都只产生 0，因此**只修其中一个，像素仍然是 0**。这是此前排查绕远的原因：
症状无法区分「clear 用了错误的颜色」与「clear 和 draw 都没生效」。

> 教训：`VkClearValue clear` 被 memcpy 填充过，编译器不会报 unused，
> 静默丢弃一个「已经用过的」局部变量极易逃过 review。

已逐项核对并排除的其它候选（读代码确认均正确）：
- 剔除：`CULL_MODE_BACK` + `COUNTER_CLOCKWISE`，该三角形有符号面积 > 0 → 前面，不被剔除
- 深度/混合：测试显式关闭深度测试，`colorWriteMask` 含 alpha
- 格式一致：附件 `R8G8B8A8_UNORM` 与管线 `rtvFormats[0]` 两侧一致
- `rtvCount = 1` 与单个颜色附件匹配
- 回读缓冲区 `HOST_VISIBLE | HOST_COHERENT`，无需 invalidate
- 回读 barrier：`ATTACHMENT_OPTIMAL → TRANSFER_SRC_OPTIMAL`，stage/access 均正确
- 设备已启用 `VK_KHR_dynamic_rendering` 且 `dynamicRendering = VK_TRUE`
- `VulkanLoader` 直接链接 `vulkan-1`，`vkCmdBeginRendering` 是真实符号、非空桩

> 顺带纠正一个容易踩的坑：`VkBufferImageCopy` **没有** `imageLayout` 成员
> （那是 `VkImageCopy` 才有的）。拷贝布局由 `vkCmdCopyImageToBuffer` 的参数传入，
> 本工程已正确传 `TRANSFER_SRC_OPTIMAL`。

### 对照：独立探针为何从未暴露这些问题

`tools/vk_offscreen_probe.cpp` 用的是**静态**视口（`pViewports = &vp`，无动态状态），
所以它不需要 `vkCmdSetViewport` 就正确；而它用显式 render pass，
在 `VkAttachmentDescription` 里直接给出 `clearValue`。两个坑一个都不碰。

### 让一次 CI 运行定位到具体层

测试已改为自带定位能力：

- 清除色 alpha 改为 **0.25**（哨兵值），与片元 alpha（1.0）区分开
- 统计全缓冲区「非零像素数」与「出现的 alpha 取值个数」

据此单看输出即可分层：

| 输出 | 结论 |
|---|---|
`alpha 0`，非零像素 0 | 提交/拷贝/布局层未工作 |
`alpha 64`，非零像素 > 0 | clear 生效（哨兵值正确回传），draw 没生效 → 光栅层 |
`64 128 191 255` | 端到端正常 |

同时 validation layer 已接线（§1.5），可覆盖 barrier / layout / 命令缓冲生命周期
这一类同步问题 —— 它抓不到「忘记设置动态视口」。


## 0.1 为什么有这份文档

Linux runner 提供不了 OpenGL 4.6（引擎的产品承诺），而 `llvmpipe` 最高只有 4.5、
`Zink` 在 lavapipe 上无法初始化。因此 4 个 `GPUSkinningPixelTest` 用例在 CI 上
长期处于**显式 SKIP** 状态，CI 持续输出：

```
GPU-SKINNING-STATUS: NOT VERIFIED on this runner (explicit skip above).
```

本轮沿 Vulkan 路线推进，目前**只剩一个渲染状态问题**（§0），且根因已定位。
此前认为「工作量已超出目标」而收手的判断，是在还不知道管线创建能否打通时作出的；
现在管线已能创建（§1.4），剩余部分比当时的估计小得多。

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

### 1.3 离屏设施：已写好、编译通过并**首次运行** ✅

`engine/include/Engine/Vulkan/VulkanOffscreen.h` + `engine/src/Vulkan/VulkanOffscreen.cpp`

**关键设计更正**：最初按「render pass + framebuffer」写，随后发现这是错的 ——
`VulkanPipelineState.cpp:133` 用 `VkPipelineRenderingCreateInfo` 挂到
`VkGraphicsPipelineCreateInfo::pNext`，即 **dynamic rendering**，管线不带 render pass。
两者互不兼容。已改为提供真正缺的那一半：颜色附件 image +
`vkCmdBeginRendering`/`vkCmdEndRendering` + 自带屏障/copy/submit/map 的回读。

只用 `VulkanDevice` 已公开的句柄，**不改 `IRHIDevice`**，不影响 GL / D3D12。

---

### 1.4 引擎后端首次运行：管线创建已打通 ✅

run `38328385429`（gcc 与 clang 两个 job 都跑到这一步）：

```
[VKTRI] shaders registered (registry holds 2)      ← ShaderRegistry 首次运行
[VKTRI] device created: llvmpipe (LLVM 20.1.2)     ← 设备首次在 lavapipe 初始化成功
[VKTRI] graphics pipeline created                  ← CreateGraphicsPSO 首次返回非 null
[VKTRI] centre pixel = 0 0 0 0                     ← 像素仍失败，见 §0
```

这条链路上此前**从未运行过**的环节，现在全部跑通：shaderc 编译（诊断 job 里一直
只编到 stub 分支）、注册表解析、管线创建、离屏目标、回读。

顺带修正：主构建 job 此前**不安装** `libvulkan-dev`，因此 Vulkan 后端在主构建里
一直是关闭的，`ShaderRegistry` 的编译路径从未被编译过。现在装上了。

### 1.5 Validation layer：已接线（env 门控）✅

此前 `VulkanDeviceInternal.h` 里就有 `debugMessenger` / `enableValidation` 字段，
`GetRequiredInstanceExtensions(bool enableValidation)` 也会加 `VK_EXT_debug_utils`
—— 但该头文件**没有被任何编译单元 include**，且这两个函数**零调用者**。
即：validation 被脚手架化过，从未接线。

现在接到真正的 `Impl`（定义在 `VulkanDevice.cpp` 内部）：

| 环境变量 | 作用 |
|---|---|
`ENGINE_VULKAN_VALIDATION=1` | 请求 `VK_LAYER_KHRONOS_validation` + `VK_EXT_debug_utils` |
`ENGINE_VULKAN_VALIDATION_VERBOSE=1` | 追加 VERBOSE/INFO 级别 |
`ENGINE_VULKAN_SYNC_VALIDATION=1` | 追加 `VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT` |

设计要点：
- layer 与扩展**都真实存在才请求**，否则降级并打印原因 —— 缺 layer 时若照请求，
  `vkCreateInstance` 会以 `VK_ERROR_LAYER_NOT_PRESENT` 直接失败。
- messenger 在实例创建后立即挂上，且在 `vkDestroyInstance` **之前**销毁。
- 回调只打 VALIDATION / PERFORMANCE 两类，过滤 GENERAL 以压噪。
- CI 侧安装 `vulkan-validationlayers`，并显式检查 layer manifest 是否存在，
  避免「以为开着其实没开」。三角形步骤已开启上述两个变量。

**边界（重要）**：layer 抓不到「忘记设置动态视口」—— 未设置动态状态是合法 API
调用，不会报错。它的价值在于能一次性排除回读路径上的同步类根因。

## 2. 尚未完成（运行时空洞，编译期暴露不出）

### 2.0 ~~着色器解析缺失~~ ✅ 已修复（`f65cd4c`）

见 §1.4。`ShaderRegistry` 已建并接入 `CreateGraphicsPSO`。

设计要点：`StringID` 是**单向哈希**（只有 `Value()`，无法反解回名字），所以
注册表必须是加载时填充、以 StringID 为键的运行时结构，不能事后从 ID 找文件。

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

### 2.3 渲染作用域：已首次使用，但像素仍为全 0

`VulkanOffscreenTarget` 是第一个调用 `vkCmdBeginRendering` 的地方，作用域本身
建立成功（见 §1.4），但回读像素为 0。根因排查见 §0。

注意：`VulkanCommandList` **仍然没有**渲染作用域 API。三角形测试因此用裸 Vulkan
命令填充一次性 command buffer，只借用引擎的管线创建。若要把这条路径产品化，
命令列表需要补 BeginRendering/EndRendering。

### 2.4 `GetVulkanDeviceInfo()` 硬编码

```
VulkanDevice.cpp:406  std::string GetVulkanDeviceInfo() noexcept { return "Vulkan 1.3"; }
```

返回值与实际设备无关。应改为读 `VkPhysicalDeviceProperties`。

### 2.5 ~~未验证：设备能否真正初始化~~ ✅ 已验证（见 §1.4）

lavapipe 上设备初始化成功，`GetDeviceName()` 返回真实名称。

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
`f65cd4c` | **实现 `ShaderRegistry`** 并接入 `CreateGraphicsPSO`，替换两个 `VK_NULL_HANDLE` |
`7e12c18` | 新增 headless 三角形 sandbox 目标与 CI 步骤；离屏目标补一次性命令缓冲 |
`c973059` | 修正 CI 改动落位（此前误放进 `jolt-link-diagnose`），Vulkan 依赖装进主构建 job |
`03ad678` | 修 `ShaderStageType` → 编译器私有 `ShaderStage` 枚举的映射 |

`7b6b7f1` 的四处改动（`engine/src/Vulkan/VulkanDevice.cpp`，该目录 clean）：

1. `windows.h` / `vulkan_win32.h` 加 `#ifdef _WIN32` 守卫，非 Windows 改为包含 `<dlfcn.h>`
2. `VK_KHR_win32_surface` 实例扩展的 push 同步守卫
3. Win32 surface 创建（`VkWin32SurfaceCreateInfoKHR`）同步守卫
4. `HasVulkanSupport()` 改跨平台：Windows 用 `LoadLibraryA`，POSIX 用 `dlopen("libvulkan.so.1")`

四处都只服务**带窗口的呈现路径**，headless 离屏渲染不需要。

---

## 4. 建议的接手路径
1. **推送 §0 的视口修复并验证三角形像素**（已本地改好，未提交）。
   预期中心像素变为 `64 128 191 255`。
2. 若仍为 0：先看 §1.5 的 validation 输出。若 layer 报出 barrier / layout 问题，
   按报告修；若 layer 干净，则排除同步类根因，回到 §0 的候选清单。
3. 补 `GraphicsPSODesc` 的顶点输入契约 —— 注意该结构被 GL / D3D12 共用，
   属跨后端设计，最好先与其它后端 owner 对齐
4. 给 `VulkanCommandList` 补渲染作用域 API（BeginRendering/EndRendering），
   目前它只能在外部用裸命令驱动（见 §2.3）
5. 三角形通过后，把 `GPUSkinningPixelTest` 抽象为后端无关
6. 届时把 `GPU_SKINNING_NO_GL46=1` 从 Linux 测试步骤中移除，
   4 个 GPU 像素用例转为真实执行
7. 补 `VulkanTexture` 的图像创建（§2.2）—— 像素测试需要纹理
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
