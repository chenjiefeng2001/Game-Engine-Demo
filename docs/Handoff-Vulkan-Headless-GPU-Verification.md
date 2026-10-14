# 交接：headless GPU 像素验证 —— Vulkan 路线现状

**Owner**：任何希望让 Linux CI 获得真实 GPU 像素证据的人
**分支**：`avalonia` @ `7b6b7f1`
**起点**：接续 `docs/Handoff-CI-Remaining-Failures.md` 中记录的 GPU 限制

---

## 0. 像素已验证通过 —— 真正的根因是**正面绕序**

**接手前请先读这一节。**

在 WSL（lavapipe）上取得首个运行时证据后，结论发生了根本变化：

```
[H] device = llvmpipe (LLVM 20.1.2, 256 bits)
[H] graphics pipeline created
[H] centre pixel = 64 128 191 255
[H] pixels with any non-zero channel = 4096 / 4096
[H] PASS: both fixes work end to end
```

**此前「视口是根因」的判断是错的。** 视口确实从未设置（真实缺陷，已修并验证），
但它不是像素全 0 的原因 —— 静态视口同样不出像素。真正的根因是：

```
VulkanPipelineState.cpp：cullMode = BACK 配 frontFace = COUNTER_CLOCKWISE
```

Vulkan 对三角形正/负面积的约定与 OpenGL 相反。按 OpenGL 习惯逆时针书写的三角形，
在 `frontFace = COUNTER_CLOCKWISE` 下会被**整体剔除**，画面只剩 clear 的颜色 ——
且 Vulkan 不为「被剔除」报任何错误。改为 `VK_FRONT_FACE_CLOCKWISE` 后保留背面剔除
的同时光栅化恢复正常。

### 三个缺陷的最终定论

| 缺陷 | 是否是像素全 0 的原因 | 现状 |
|---|---|---|
动态视口/裁剪未设置 | **否**（真实缺陷，但改静态视口仍失败） | 已修并验证 |
`att.clearValue` 未赋值 | **否**（clear 一直生效，只是清成 (0,0,0,0)） | 已修并验证 |
`frontFace` 绕序与 `cullMode` 不匹配 | **是，唯一根因** | 已修并验证 |

> 教训：前两者都是真实缺陷且都必须修，但它们都只产生 0，因此**症状无法区分**
> 「视口不对」「clear 颜色被丢弃」「几何被剔除」。在没有运行时反馈的情况下，
> 按代码阅读收敛根因是不可靠的 —— 本次靠 WSL 逐项二分才定位。

### 已确认的缺陷一：动态视口从未设置（已修，已验证非根因）

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

**已在 WSL/lavapipe 实测**：改用静态视口仍不出像素，因此它不是根因，但确属缺陷。

### 已确认的缺陷二：`att.clearValue` 从未赋值（已修，已验证非根因）

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

### 真正的根因：正面绕序与剔除模式不匹配（`7017d17`）

```cpp
rasterizer.cullMode   = VK_CULL_MODE_BACK_BIT;
rasterizer.frontFace  = VK_FRONT_FACE_COUNTER_CLOCKWISE;   // ← 这里
```

Vulkan 判定三角形朝向用的有符号面积

```
a = 0.5 · Σ (x_i·y_{i+1} − x_{i+1}·y_i)      // 帧缓冲坐标，y 轴向下
```

与 OpenGL 的正负约定相反。离屏三角形的 NDC 顶点为 `(-1,-1) (3,-1) (-1,3)`，
按此公式 `a > 0`，在 `frontFace = COUNTER_CLOCKWISE` 下被判为**背面并剔除**，
于是整张图只剩 clear 的颜色。

修复：`frontFace = VK_FRONT_FACE_CLOCKWISE`，**保留背面剔除**（不是关掉剔除）：

```
frontFace=COUNTER_CLOCKWISE  → centre = 0 0 0 64   （仅 clear）
frontFace=CLOCKWISE          → centre = 64 128 191 255   （端到端通过）
```

定位方式：把 `tools/vk_offscreen_probe.cpp` 在同一环境跑通作为基线
（`centre pixel = 64 128 191 255`），再把引擎管线逐项对齐探针配置并逐项还原，
每次只改一个字段。绕序是唯一能翻转结果的变量。

逐项**实测**排除（不是读代码推断）：
- 片元/顶点模块均已在注册表中找到，且都已进入管线
- 附件格式与管线声明的 `colorAttachmentFormats` 一致
- `dsvFormat` 默认 `Unknown` → `FormatToVk` 返回 `VK_FORMAT_UNDEFINED`，未声明深度附件
- 管线布局 `setLayoutCount = 0`，不要求绑定描述符集
- `topology` 默认为 `TriangleList`，映射正确
- 动态视口、静态视口、`pDepthStencilState` 置空、`cullMode = NONE` 均**不能**翻转结果

> 教训：`VkClearValue clear` 被 memcpy 填充过，编译器不会报 unused，
> 静默丢弃一个「已经用过的」局部变量极易逃过 review。
>
> **更大的教训**：前两个缺陷都是真实缺陷、都必须修，但症状相同（都是 0）。
> 在没有运行时反馈时按代码阅读收敛根因不可靠 —— 本次靠 WSL 逐项二分才定位。
> 这也是为什么 validation layer 抓不到这类问题：被剔除的绘制是完全合法的 API 调用。

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

### 1.1 环境侧：lavapipe 能真实离屏光栅化 <span class="material-symbols-outlined">check_circle</span> 已验证

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

### 1.2 引擎后端侧：15/15 文件可在 Linux 编译 <span class="material-symbols-outlined">check_circle</span> 已验证

run `38316605255` 的 `vulkan-backend-compile` job，`compile-all-objects rc = 0`，0 error。

**此前该后端从未被任何 CI 编译过** —— 主构建 job 从不安装 `libvulkan-dev`，
`find_package(Vulkan)` 恒失败。诊断 job 打开后立刻暴露：

```
engine/src/Vulkan/VulkanDevice.cpp:19:10: fatal error: windows.h: No such file or directory
```

`7b6b7f1` 已修（见 §3）。现在 15 个文件全部编译通过，含新增的 `VulkanOffscreen.cpp`。

> **编译通过 ≠ 后端可用。** 请务必连同 §0 一起读：本节只说明「能构建」，
> 而图形管线路径在运行时是完全断开的。

### 1.3 离屏设施：已写好、编译通过并**首次运行** <span class="material-symbols-outlined">check_circle</span> 已验证

`engine/include/Engine/Vulkan/VulkanOffscreen.h` + `engine/src/Vulkan/VulkanOffscreen.cpp`

**关键设计更正**：最初按「render pass + framebuffer」写，随后发现这是错的 ——
`VulkanPipelineState.cpp:133` 用 `VkPipelineRenderingCreateInfo` 挂到
`VkGraphicsPipelineCreateInfo::pNext`，即 **dynamic rendering**，管线不带 render pass。
两者互不兼容。已改为提供真正缺的那一半：颜色附件 image +
`vkCmdBeginRendering`/`vkCmdEndRendering` + 自带屏障/copy/submit/map 的回读。

只用 `VulkanDevice` 已公开的句柄，**不改 `IRHIDevice`**，不影响 GL / D3D12。

---

### 1.4 引擎后端像素验证：**已通过**（run `38485651755`）

```
[VKTRI] shaders registered (registry holds 2)
[VKTRI] device created: llvmpipe (LLVM 20.1.2, 256 bits)
[VKTRI] graphics pipeline created
[VKTRI] centre pixel = 64 128 191 255 (pitch=256)      <- 通过
[VKTRI] pixels with any non-zero channel = 4096 / 4096
[VKTRI] PASS: fragment shader output reached the readback
```

这是该后端**首个真实的像素级运行证据**。validation layer 同时启用
（`validation layer manifest present`，CI 安装了 `vulkan-validationlayers`），
全程**无任何 validation 报错**。

此前的历史观测（run `38328385429`）是 `centre pixel = 0 0 0 0`，根因见 §0。

shaderc 编译、注册表解析、管线创建、离屏目标、回读这五个环节在此之前从未运行过。
顺带修正：主构建 job 此前**不安装** `libvulkan-dev`，Vulkan 后端在主构建里一直是
关闭的，`ShaderRegistry` 的编译路径从未被编译过。
### 1.5 Validation layer：已接线（env 门控）<span class="material-symbols-outlined">check_circle</span> 已验证

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

### 1.7 顶点输入契约：已实现，**光栅化未验证**

`GraphicsPSODesc` 此前无法描述顶点输入，带顶点缓冲的网格因此无法绘制。契约已补齐
（`VertexInputDesc` / `VertexBufferBinding` / `VertexInputAttribute`，并参与 `GetHash` ——
否则顶点布局不同的两个描述符会命中同一条 PSOCache 记录，拿到顶点绑定错误的管线）。
CI 确认契约真正到达 Vulkan：

```
pipeline created with 1 binding(s), 2 attribute(s)
```

但**绘制阶段在 lavapipe 上段错误**（exit 139），且**与本引擎无关**：

| 场景 | 结果 |
|---|---|
| 同一二进制，不绑定顶点缓冲 | 正常跑完（无几何，符合预期） |
| 同一二进制，绑定顶点缓冲 | libvulkan_lvp.so 段错误 |
| 完全不含引擎代码的独立 Vulkan 程序 | 同样段错误 |

判别依据：着色器仍声明 2 个输入，唯一分界点就是绑不绑顶点缓冲。

该测试已改为 `VKTRI_VERTEX_INPUT=1` 门控，默认跳过并打印原因 —— 否则段错误会带走
整个进程，连 §1.4 的已验证结果一并丢失。

**结论**：契约已实现且可编译；「从顶点缓冲光栅化」在此软件驱动上**未验证**。

### 1.8 只有真正跑 CI 才会暴露的四个缺陷

这些全部由本轮改动引入或暴露，且都**不在**本地编译检查的覆盖范围内：

1. **`IRHITexture` 未限定命名空间** —— `RHIDemoApp` 位于 `namespace Engine` 而非
   `Engine::RHI`。该文件从未被编译过（GLFW 未 vendored，`vulkan-backend-compile`
   只编引擎源），Ubuntu 构建第一次编到它就报错。
2. **`RunVertexInputPhase` 同样未限定** —— 位于全局作用域的匿名 `namespace` 内。
   我的本地检查报了**假阳性**：shell 管道以 `head` 收尾，被检查的退出码属于 `head`
   而非编译器。此后所有检查改为不经管道。
3. **`VertexAttribute` 与既有类型重名** —— `Engine::VertexAttribute` 已存在于
   `RenderResources/VertexArray.h`；同时导入两个命名空间的 TU 出现二义性，
   `tests/test_renderer` 编译失败。已重命名为 `VertexInputAttribute`。
4. **validation layer 缺少 `VK_EXT_validation_features` 扩展** ——
   `VkValidationFeaturesEXT` 挂在 `pNext` 上，必须同时启用该实例扩展；layer 存在
   也救不了。CI 表现为「validation layer enabled」紧跟「Failed to create instance」、
   三角形 exit 134。同时把 `vkCreateInstance` 的固定失败文案改为打印 `VkResult` 与
   layer/扩展数量 —— 正是这句无信息的文案让排查多花了一轮 CI。

附带发现并修复：`MemoryTypes.h` 缺 `<type_traits>`、`Log.cpp` 缺 `<iostream>`
（二者都只因被其它头间接引入才编过，单独包含即失败）；`VulkanOffscreen.cpp` 的
`Failed()` 无条件打印失败且恒返回 `false`，导致成功时误报、真实失败时不返回
`nullptr` 而带着空句柄继续执行。

### 1.9 Windows 链接失败：一个被误诊的自身缺陷（`5586e16`）

Windows 两个 job 长期在 Build 阶段失败，日志里有一段刺眼的
`could not find any instance of Visual Studio`，我据此归因为「runner 只有
VS 18、workflow 固定 VS 17 生成器」，并判定与本轮改动无关。**这个判断是错的。**

那段报错来自一个明确标注 `diagnostic complete (non-fatal)` 的探针步骤。真正的失败在
日志深处：

```
RHIDemoApp.obj : error LNK2019: unresolved external symbol
    VulkanDevice::CreateSwapChain(SwapChainDesc const &)
    VulkanSwapChain::AcquireNextImage()
    VulkanSwapChain::Present()
    VulkanSwapChain::GetBackBuffer(unsigned int) const
RHIDemoApp.obj : fatal error LNK1120: 4 unresolved externals
```

同时 configure 输出 `Vulkan SDK not found — Vulkan backend disabled`：Windows 未安装
Vulkan SDK，后端源码与 `ENGINE_HAS_VULKAN` **一同**被排除。而我加的
`RenderVulkanFrame()` 位于文件作用域、在守卫之外，于是照样编译并引用未链接的符号。
Linux 因安装了 `libvulkan-dev` 不受影响 —— 这也是为什么只有 Windows 抓到。

教训：**「日志里最显眼的报错」不一定是失败原因**。此处探针是 non-fatal 的，真正的
LNK2019 在八千行之后。应先定位失败步骤的产物，而不是抓住第一条错误信息。

修复后两个 Windows job 均通过构建，停在既有的 `Test (full suite)` 与
`Verify SPIR-V activation`。
## 2. 尚未完成（运行时空洞，编译期暴露不出）

### 2.0 ~~着色器解析缺失~~ <span class="material-symbols-outlined">check_circle</span> 已修复（`f65cd4c`）

见 §1.4。`ShaderRegistry` 已建并接入 `CreateGraphicsPSO`。

设计要点：`StringID` 是**单向哈希**（只有 `Value()`，无法反解回名字），所以
注册表必须是加载时填充、以 StringID 为键的运行时结构，不能事后从 ID 找文件。

### 2.1 ~~`GetOrCreateThreadCommandPool()` 是桩~~ <span class="material-symbols-outlined">check_circle</span> 已修复（`ad8f97b`）

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

### 2.2 ~~`VulkanTexture` 是空壳~~ 已补齐图像视图（`dcc65a6`、`2d25e21`）

`VulkanDevice::CreateTexture` 原先只创建 `VkImage`（经 VMA），**从不创建 image view**。
而 `vkCmdBeginRendering` 绑定的是 view，于是设备交出的每张纹理都无法作为渲染目标。
离屏三角形没暴露此问题，因为 `VulkanOffscreenTarget` 自建 view。

现在 `CreateTexture` 按图像自身格式创建 view 并写入纹理，命令列表的渲染路径得以用于离屏。

另有一处**有意泄漏**已修：`Impl` 原本不持有 `VkDevice`，析构里留了空块并注明
「view 由外部管理」，而该 owner 并不存在。现由 `VulkanTexture` 统一持有并在
销毁 image 之前先销毁 view；`~VulkanSwapChain` 相应不再销毁 backbuffer view，否则双重释放。

### 2.3 ~~`VulkanCommandList` 没有渲染作用域~~ 已补（`dcc65a6`）

`IRHICommandList` 已加入 `RenderTargetDesc` 与 `BeginRendering` / `EndRendering`，
VulkanCommandList 以 dynamic rendering 实现。离屏目标自带的同款逻辑暂未删除，
两条路径并存。

默认实现是**空操作而非纯虚**：立即模式后端（OpenGL）本就没有作用域概念，空操作正是
语义正确的默认值，且不会破坏任何既有后端实现。声明处已注明未覆盖的后端会静默忽略
作用域，调用方不应据此假定发生了切换。

两处 API 细节值得记录，均由编译器纠正：`VkClearColorValue` 是 union，匿名成员
`color[]` 不支持下标，需经 `color.float32[]`；`VkRenderingInfo` **没有**
`depthAttachmentCount` 字段，是否附加深度由 `pDepthAttachment` 是否为空决定。

### 2.3b 交换链时序与 backbuffer 视图（`4834590`）

两处此前无法工作的接线：

1. **backbuffer 没有 image view** —— `vkCreateImageView` 全仓库只在离屏文件里出现过，
   任何由 `IRHITexture*` 构造的渲染附件都会被跳过，窗口路径永远画不出东西。
2. **`Present()` 时序不可用** —— 它内部才 `vkAcquireNextImageKHR`，却提交调用方
   **事先**录好的命令缓冲，于是录制时目标 backbuffer 尚未确定。该时序在原理上
   无法工作。现拆分为 `AcquireNextImage()`（等 fence + acquire）与 `Present()`
   （仅提交 + 呈现）。

`RHIDemo` 的 `RenderVulkanFrame()` 是这条路径在离屏测试之外的首次使用：
acquire → 经命令列表渲染作用域录制清除 → 提交并呈现。**尚不绘制任何几何**，
目的是先把 acquire / scope / submit / present 这四步接通，使后续失败可归因。

### 2.3e Validation layer 在 WSL 可用后抓到的三个真实缺陷（`e9bf326`）

此前 WSL 里**没有** Khronos validation layer，只有 `INTEL_nullhw` 与 `MESA_overlay`。
装上之后（`vulkan-validationlayers` 1.3.275），在真实映射窗口上跑出一批编译期与离屏测试
都不可能发现的缺陷。

**1. 每一次 present 都是非法调用（真实违规）**

```
VUID-VkPresentInfoKHR-pImageIndices-01430 (ERROR / SPEC): images passed to present must be in
layout VK_IMAGE_LAYOUT_PRESENT_SRC_KHR but is in VK_IMAGE_LAYOUT_UNDEFINED
```

`EndRendering()` 只调用 `vkCmdEndRendering` 就结束，**从未**把交换链图像转成
`PRESENT_SRC_KHR`。离屏路径因为调用方自带屏障、且离屏目标从不呈现，所以一直掩盖着这个
问题。

顺带还有一个：`BeginRendering` 把 `VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL` 当作动态渲染的
附件布局（`vkCmdBeginRendering` 不接受该值），而且只「记录」布局而**不发转换**。

修复：附件布局改 `COLOR_ATTACHMENT_OPTIMAL`；追踪布局不符时显式发转换；
`EndRendering` 把交换链图像转到 `PRESENT_SRC_KHR`。`VulkanTexture` 新增
`IsSwapChainImage()`，由 `VulkanDevice::CreateSwapChain` 标记 —— RHI 接口本身
不区分渲染目标与呈现目标。

**2. 扩展数组悬空指针**

`ppEnabledExtensionNames` 在 validation 分支 `push_back` **之前**就从
`instanceExts.data()` 取值，扩容后该指针失效，`vkCreateInstance` 在已释放内存上做
`strcmp` 直接崩溃。之所以此前从未暴露：validation 分支只在枚举到
`VK_EXT_validation_features` 时才进入，而本机 loader 并不会枚举到它。

**3. 日志说谎**

层通过 `VK_INSTANCE_LAYERS` 已经加载并在报错误，引擎却打印
`continuing without validation`。该扩展只承载逐项开关（同步校验），
**不是**启用 validation 的前提。现在缺它时降级并**具体说明缺什么**，而不是谎称 validation
已关闭。

**验证结果（真实映射窗口，10 帧 / 3 个交换链图像）**：

```
surface created via VK_KHR_xlib_surface
acquired 0, backbuffer 640x480, isSwapChainImage=1, trackedLayout=PRESENT_SRC_KHR
pImageIndices-01430 出现次数: 0
```

### 2.3g 最后一个「未解释」的现象：真实缺陷，已修复（`VulkanDevice::CreateSwapChain`）

2.3e / 2.3f 末尾记录的残留 —— 每个交换链图像**首次**使用时报一条
`DrawState-InvalidImageLayout`，之后不再出现 —— **不是 validation 层的记账现象，
而是我们的真实缺陷**。此前判断错误，现已定位并修复。

`VulkanDevice::CreateSwapChain` 为每个 backbuffer 设置初始布局：

```cpp
// 初始 layout：交换链图像创建后处于 PRESENT_SRC_KHR
tex->SetLayout(VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
```

**这个注释与 Vulkan 规范相反**：新创建的交换链图像，其内容与布局都是**未定义的**。

于是首次使用该图像时，`BeginRendering` 会发出一条
`oldLayout = PRESENT_SRC_KHR → COLOR_ATTACHMENT_OPTIMAL` 的屏障，
而 `oldLayout` 与实际布局不符。validation layer 报出的正是这个矛盾：

```
expects VkImage to be in layout VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
-- instead, current layout is VK_IMAGE_LAYOUT_UNDEFINED
```

**为何只在首次出现**：一次错误布局记账被我们的屏障「固化」后，后续帧的 `oldLayout`
就与 validation 的记账一致了，所以不再报错。这解释了「每个图像恰好一次」的现象，
也解释了为何帧数从 4 增到 10 时错误数恒为 3。

定位方法：打印每个 backbuffer 的 `VkImage` 与追踪布局，与 validation 消息里的 handle
逐一比对，确认三者一致、且初始布局已是 `PRESENT_SRC_KHR`（`1000001002`）。

修复：初始布局改为 `VK_IMAGE_LAYOUT_UNDEFINED`。

**结果**：窗口路径在 Khronos validation layer 下**零发现**，离屏路径仍零发现，
三角形回读仍为 `64 128 191 255`，15/15 编译通过。

**教训**：把残留的 validation 消息归因于「工具的记账问题」是本次最容易犯的误判。
残留消息应当先假设是自己的 bug，直到证据指向别处。

**回归**：15/15 Vulkan 编译单元通过；离屏三角形回读仍为 `64 128 191 255`；
5 个改动文件通过 MSVC 语法检查。

### 2.3f validation 真正启用后，CI 又暴露的三个缺陷（`bfc5aed`）

关键在于**层此前是被环境强制加载的，而引擎以为它没开**（见 2.3e 第 3 点）。修正后引擎
自己启用层，CI 从「零错误」变成报出 3 条 —— 那 3 条是真实缺陷，只是此前一直没被检查。

**1. 离屏目标重复了同一个布局错误（`VulkanOffscreen.cpp`）**

`VulkanOffscreenTarget` 把 `VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL` 声明为动态渲染附件
布局，随后又从同一布局做转换。`vkCmdBeginRendering` 不接受该值；而屏障一旦写了它就触发
`VUID-VkImageMemoryBarrier-synchronization2-07793`（该布局需要 `synchronization2`
特性），**屏障被丢弃**，图像留在 `UNDEFINED`，于是 `vkCmdBeginRendering` 拿到错误布局。

这与命令列表里已修的是同一个错误，在离屏路径上独立犯了一次。

**2. 只声明布局、从不转换**

`BeginRendering` 声明 `COLOR_ATTACHMENT_OPTIMAL` 却从不转换进去。图像创建后是
`UNDEFINED`，validation 直接报 `expects COLOR_ATTACHMENT_OPTIMAL / current UNDEFINED`。
现已补上转换并追踪布局（`mutable m_ColorLayout`），避免重复帧发出 `oldLayout` 失配的屏障。

**3. 每个 PSO 泄漏一个 `VkPipelineLayout`**

`VulkanPipelineState` 只销毁 pipeline、不销毁 layout，`vkDestroyDevice` 时报
`VUID-vkDestroyDevice-device-05137`。

**当前状态**：离屏三角形在 Khronos validation layer 下**零发现**，回读仍为
`64 128 191 255`；15/15 编译通过；两个改动文件通过 MSVC 语法检查。窗口路径与前一致
（每图像一次的首次使用消息仍在，present 违规仍为 0）。

**顺带发现的 API 不一致**（未改）：`CreateCommandList` 返回 `unique_ptr`，而
`CreateGraphicsPSO` 返回**裸指针**，所有权无处安放 —— 测试代码最初就是这样漏掉了 PSO 的
销毁。

### 2.3c 生产侧接线缺口（`0e76957`、`ab64696`）

- `CreateRHIDevice(Backend, ...)`：此前 `CreateVulkanDevice()` 在 `engine/src` 内
  **零调用者**，`RHIWindow` 也不持有设备，引擎运行时无法选择任何后端。
- `RHIBackend.h` 中 `CreateRHI` / `IsBackendAvailable` / `GetDefaultBackend`
  **只声明未定义**，任何调用者都会链接失败。已补上后两个；`CreateRHI` 返回
  `IGraphicsFactory`，与 `IRHIDevice` 不是同一套抽象，未实现。
- 着色器注册表的生产来源：`ShaderCompileRule::Execute` 现按扩展名推断 stage 并登记
  到 `ShaderRegistry`。此前只有 sandbox 测试这么做，生产路径上 `CreateGraphicsPSO`
  必然查不到模块。该规则**仍有一半是占位**：它写出了 `.spv` 文件名却未真正产出该文件。

**仍然阻塞**：`RHIWindow` 持有设备属 HRC-owned（`RHIWindow.h/.cpp`），
接缝已就位，剩余是那边加一个成员并调用 `CreateRHIDevice`。

### 2.4 `GetVulkanDeviceInfo()` 硬编码

```
VulkanDevice.cpp:406  std::string GetVulkanDeviceInfo() noexcept { return "Vulkan 1.3"; }
```

返回值与实际设备无关。应改为读 `VkPhysicalDeviceProperties`。

### 2.5 ~~未验证：设备能否真正初始化~~ <span class="material-symbols-outlined">check_circle</span> 已验证

（见 §1.4）

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
`b05c894` | 离屏目标补 `vkCmdSetViewport` / `vkCmdSetScissor`（动态状态，从未设置） |
`7d33249` | validation layer 接线（env 门控）+ CI 装 layer 包并显式检查 manifest |
`7a4e5d3` | 三角形测试自带定位能力：清除色哨兵 alpha + 全缓冲区非零像素统计 |
`d30cdb5` | **`att.clearValue` 从未赋值** —— clear 一直被清成 (0,0,0,0) |
`cd7af4b` | RHIDemo 设备存于局部变量，函数返回即析构，却打印成功标记 |
`0e76957` | 新增 `CreateRHIDevice`；补上只声明未定义的 `IsBackendAvailable`/`GetDefaultBackend` |
`dcc65a6` | `IRHICommandList` 补渲染作用域 + Vulkan 实现；`VulkanTexture` 补 image view 访问器 |
`4834590` | backbuffer 建 image view；拆分 `AcquireNextImage()` 与 `Present()` |
`2d25e21` | `CreateTexture` 建 image view；view 泄漏修复（纹理统一持有） |
`ab64696` | `ShaderCompileRule` 接入 `ShaderRegistry`，按扩展名推断 stage |
### WSL 验证与根因修正（`7017d17`）

`7017d17` | 修正 `frontFace` 绕序（**真正的根因**）；`Failed()` 误报修复；
新增 X11/XCB/Wayland surface 支持；补 `MemoryTypes.h` / `Log.cpp` 缺失的 include

> **以上 11 个提交全部未推送、全部未经运行时验证。** 其中 `4834590`、`dcc65a6`、
> `2d25e21` 涉及帧循环、交换链时序与纹理生命周期三处结构性修改，
> 风险高于纯修复类提交。

`7b6b7f1` 的四处改动（`engine/src/Vulkan/VulkanDevice.cpp`，该目录 clean）：

1. `windows.h` / `vulkan_win32.h` 加 `#ifdef _WIN32` 守卫，非 Windows 改为包含 `<dlfcn.h>`
2. `VK_KHR_win32_surface` 实例扩展的 push 同步守卫
3. Win32 surface 创建（`VkWin32SurfaceCreateInfoKHR`）同步守卫
4. `HasVulkanSupport()` 改跨平台：Windows 用 `LoadLibraryA`，POSIX 用 `dlopen("libvulkan.so.1")`

四处都只服务**带窗口的呈现路径**，headless 离屏渲染不需要。

---

## 4. 建议的接手路径

1. **§1.4 的像素验证已完成**（run `38485651755`，lavapipe + validation layer，
   全程零 validation 报错）。下列各项都建立在「这条链路确实能跑」之上，现在成立。
2. ~~X11 / Wayland surface~~ **Xlib 路径已在真实映射窗口上验证**（见 §2.3d 与 §2.3e）。
   Wayland 路径仍仅编译验证。
3. **让 `RHIDemo` 的 Vulkan 路径真正出画面** —— `RenderVulkanFrame()` 目前只清除
   不绘制（见 §2.3b），需要接上真实几何与材质。
4. **补 `GraphicsPSODesc` 的顶点输入契约** —— 契约已实现（§1.7），但
   **顶点缓冲光栅化在 lavapipe 上未验证**。若换用真实 GPU 或能正常处理顶点取数的
   驱动，设置 `VKTRI_VERTEX_INPUT=1` 即可验证。
5. **删除 `VulkanOffscreenTarget` 自带的渲染作用域逻辑**，改走命令列表
   （新契约见 §2.3，两条路径目前并存）。
6. **补 `ShaderCompileRule` 的真实产出**：它目前只写出 `.spv` 文件名却不生成该文件
   （见 §2.3c）。
7. ~~Windows 构建在 Build 阶段失败~~ **已修复**（`5586e16`）。此前我把它归因于
   「runner 只有 VS 18 而 workflow 固定 VS 17 生成器」——**那是错的**：那段探针报错
   来自一个被正确标记为 non-fatal 的诊断步骤。真实原因是本轮引入的
   `RenderVulkanFrame` 位于文件作用域、在 `ENGINE_HAS_VULKAN` 守卫之外，Windows
   未安装 Vulkan SDK 时它照样编译并引用未链接的符号（4 个 LNK2019 / LNK1120）。
   两个 Windows job 现已通过构建，分别停在既有的 `Test (full suite)` 与
   `Verify SPIR-V activation`。
8. 接下来把 `GPUSkinningPixelTest` 抽象为后端无关；届时把
   `GPU_SKINNING_NO_GL46=1` 从 Linux 测试步骤中移除。

### CI 现状

- `vulkan-backend-compile` job：**独立、continue-on-error、always 上传日志**，只回答
  「Vulkan 后端能否编译」，不会 gate 主构建
- `gl-capability-probe` job：持续采集 GL 与 Vulkan 能力证据
- 主构建 job **未**启用 Vulkan（不装 `libvulkan-dev`），因此 `7b6b7f1` 的改动
  对主构建零影响，无回归

### 所有权边界

| 路径 | 状态 |
|---|---|
`engine/src/Vulkan/`、`engine/include/Engine/Vulkan/` | clean，未修改 |
`sandbox/CMakeLists.txt` | clean，未修改 |
`.github/workflows/cmake-multi-platform.yml` | clean，未修改 |
`engine/CMakeLists.txt` | **HRC-DIRTY**（含 `find_package(Vulkan)` 与 GLOB，**无需修改**）
`engine/src/RHI`、`sandbox/src/BackendTest` | **HRC-DIRTY**，不可触碰 |

注意 `engine/CMakeLists.txt` 用 `file(GLOB VULKAN_SOURCES CONFIGURE_DEPENDS src/Vulkan/*.cpp)`，
**新增 .cpp 会被自动纳入**，不需要（也不应该）去改那个文件。
