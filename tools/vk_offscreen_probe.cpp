/**
 * Vulkan 离屏渲染探针 —— 真正光栅化 + 回读像素
 *
 * 目的：在动引擎之前，先证明"lavapipe 上能真正跑通离屏 Vulkan 渲染"这一条
 * 链路。上一支 vk_capability_probe 只证明了设备/队列/格式可用，但那仍然可能
 * 一像素都画不出来。本探针补上缺失的环节：
 *
 *   image → image view → render pass → framebuffer
 *          → graphics pipeline → draw → image→buffer copy → map 读回
 *
 * 并用**像素断言**收尾：渲染 pass 用黑色 clear，片元着色器输出固定的
 * (0.25, 0.5, 0.75)。若中心像素等于该颜色，说明光栅化与回读都真实发生；
 * 若仍是黑色，说明只有 clear 生效，绘制没跑起来。
 *
 * 只依赖 Vulkan loader + glslangValidator 产出的 SPIR-V，不链接引擎。
 * 用法：vk_offscreen_probe <vert.spv> <frag.spv> [width height]
 */
#include <vulkan/vulkan.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

struct Ctx {
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice phys = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    uint32_t queueFamily = 0;
    VkCommandPool cmdPool = VK_NULL_HANDLE;
    VkPhysicalDeviceMemoryProperties memProps{};
};

int Fail(const char* what, VkResult r)
{
    std::printf("[VKOFF] %s FAILED VkResult=%d\n", what, static_cast<int>(r));
    return 2;
}

std::vector<uint32_t> LoadSpirv(const char* path, bool& ok)
{
    ok = false;
    std::FILE* f = std::fopen(path, "rb");
    if (!f) { std::printf("[VKOFF] cannot open %s\n", path); return {}; }
    std::fseek(f, 0, SEEK_END);
    long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (n <= 0 || (n % 4) != 0) { std::fclose(f); std::printf("[VKOFF] bad SPIR-V size\n"); return {}; }
    std::vector<uint32_t> words(static_cast<size_t>(n) / 4);
    size_t got = std::fread(words.data(), 1, static_cast<size_t>(n), f);
    std::fclose(f);
    ok = (got == static_cast<size_t>(n));
    if (!ok) std::printf("[VKOFF] short read on %s\n", path);
    return words;
}

uint32_t FindMemoryType(Ctx& c, uint32_t typeBits, VkMemoryPropertyFlags want)
{
    for (uint32_t i = 0; i < c.memProps.memoryTypeCount; ++i) {
        if ((typeBits & (1u << i)) &&
            (c.memProps.memoryTypes[i].propertyFlags & want) == want) {
            return i;
        }
    }
    return UINT32_MAX;
}

VkCommandBuffer BeginOneShot(Ctx& c)
{
    VkCommandBufferAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool = c.cmdPool;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;
    VkCommandBuffer cb = VK_NULL_HANDLE;
    vkAllocateCommandBuffers(c.device, &ai, &cb);
    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cb, &bi);
    return cb;
}

VkShaderModule MakeModule(Ctx& c, const std::vector<uint32_t>& code)
{
    if (code.empty()) return VK_NULL_HANDLE;
    VkShaderModuleCreateInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    si.codeSize = code.size() * sizeof(uint32_t);
    si.pCode = code.data();
    VkShaderModule m = VK_NULL_HANDLE;
    VkResult r = vkCreateShaderModule(c.device, &si, nullptr, &m);
    if (r != VK_SUCCESS) std::printf("[VKOFF] vkCreateShaderModule FAILED VkResult=%d\n", static_cast<int>(r));
    return m;
}

bool SubmitAndWait(Ctx& c, VkCommandBuffer cb)
{
    if (vkEndCommandBuffer(cb) != VK_SUCCESS) return false;
    VkFenceCreateInfo fi{};
    fi.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    VkFence fence = VK_NULL_HANDLE;
    vkCreateFence(c.device, &fi, nullptr, &fence);
    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cb;
    VkResult r = vkQueueSubmit(c.queue, 1, &si, fence);
    if (r == VK_SUCCESS) r = vkWaitForFences(c.device, 1, &fence, VK_TRUE, UINT64_MAX);
    vkDestroyFence(c.device, fence, nullptr);
    if (r != VK_SUCCESS) { std::printf("[VKOFF] submit/wait VkResult=%d\n", static_cast<int>(r)); return false; }
    return true;
}

} // namespace

int main(int argc, char** argv)
{
    std::printf("[VKOFF] === offscreen Vulkan render probe ===\n");
    if (argc < 3) { std::printf("[VKOFF] usage: %s <vert.spv> <frag.spv>\n", argv[0]); return 2; }
    const uint32_t W = (argc > 3) ? static_cast<uint32_t>(std::atoi(argv[3])) : 64u;
    const uint32_t H = (argc > 4) ? static_cast<uint32_t>(std::atoi(argv[4])) : 64u;

    Ctx c;

    VkApplicationInfo app{};
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "vkoffscreen";
    app.apiVersion = VK_API_VERSION_1_1;
    VkInstanceCreateInfo ici{};
    ici.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ici.pApplicationInfo = &app;
    VkResult r = vkCreateInstance(&ici, nullptr, &c.instance);
    if (r != VK_SUCCESS) return Fail("vkCreateInstance", r);

    uint32_t n = 0;
    vkEnumeratePhysicalDevices(c.instance, &n, nullptr);
    if (n == 0) { std::printf("[VKOFF] no physical device\n"); return 1; }
    std::vector<VkPhysicalDevice> pds(n);
    vkEnumeratePhysicalDevices(c.instance, &n, pds.data());
    for (VkPhysicalDevice pd : pds) {
        uint32_t qc = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(pd, &qc, nullptr);
        std::vector<VkQueueFamilyProperties> qs(qc);
        vkGetPhysicalDeviceQueueFamilyProperties(pd, &qc, qs.data());
        for (uint32_t i = 0; i < qc; ++i) {
            if (qs[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
                c.phys = pd; c.queueFamily = i; break;
            }
        }
        if (c.phys) break;
    }
    if (!c.phys) { std::printf("[VKOFF] no graphics queue family\n"); return 1; }
    vkGetPhysicalDeviceMemoryProperties(c.phys, &c.memProps);
    std::printf("[VKOFF] device + graphics queue family %u selected\n", c.queueFamily);

    float prio = 1.0f;
    VkDeviceQueueCreateInfo qci{};
    qci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    qci.queueFamilyIndex = c.queueFamily;
    qci.queueCount = 1;
    qci.pQueuePriorities = &prio;
    VkDeviceCreateInfo dci{};
    dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &qci;
    r = vkCreateDevice(c.phys, &dci, nullptr, &c.device);
    if (r != VK_SUCCESS) return Fail("vkCreateDevice", r);
    vkGetDeviceQueue(c.device, c.queueFamily, 0, &c.queue);

    VkCommandPoolCreateInfo cpi{};
    cpi.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    cpi.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    cpi.queueFamilyIndex = c.queueFamily;
    vkCreateCommandPool(c.device, &cpi, nullptr, &c.cmdPool);

    // ── 颜色附件 image ──
    const VkFormat COLOR = VK_FORMAT_R8G8B8A8_UNORM;
    VkImageCreateInfo ici2{};
    ici2.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ici2.imageType = VK_IMAGE_TYPE_2D;
    ici2.format = COLOR;
    ici2.extent = { W, H, 1 };
    ici2.mipLevels = 1;
    ici2.arrayLayers = 1;
    ici2.samples = VK_SAMPLE_COUNT_1_BIT;
    ici2.tiling = VK_IMAGE_TILING_OPTIMAL;
    ici2.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    ici2.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ici2.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkImage image = VK_NULL_HANDLE;
    r = vkCreateImage(c.device, &ici2, nullptr, &image);
    if (r != VK_SUCCESS) return Fail("vkCreateImage", r);

    VkMemoryRequirements mr{};
    vkGetImageMemoryRequirements(c.device, image, &mr);
    uint32_t mt = FindMemoryType(c, mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (mt == UINT32_MAX) mt = FindMemoryType(c, mr.memoryTypeBits, 0);
    if (mt == UINT32_MAX) { std::printf("[VKOFF] no memory type for image\n"); return 1; }
    VkMemoryAllocateInfo mai{};
    mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    mai.allocationSize = mr.size;
    mai.memoryTypeIndex = mt;
    VkDeviceMemory imgMem = VK_NULL_HANDLE;
    vkAllocateMemory(c.device, &mai, nullptr, &imgMem);
    vkBindImageMemory(c.device, image, imgMem, 0);

    VkImageViewCreateInfo ivci{};
    ivci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    ivci.image = image;
    ivci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    ivci.format = COLOR;
    ivci.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    VkImageView view = VK_NULL_HANDLE;
    r = vkCreateImageView(c.device, &ivci, nullptr, &view);
    if (r != VK_SUCCESS) return Fail("vkCreateImageView", r);

    // ── render pass + framebuffer ──
    VkAttachmentDescription att{};
    att.format = COLOR;
    att.samples = VK_SAMPLE_COUNT_1_BIT;
    att.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    att.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    att.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    att.finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    VkAttachmentReference ref{ 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
    VkSubpassDescription sub{};
    sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount = 1;
    sub.pColorAttachments = &ref;
    VkRenderPassCreateInfo rpci{};
    rpci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpci.attachmentCount = 1;
    rpci.pAttachments = &att;
    rpci.subpassCount = 1;
    rpci.pSubpasses = &sub;
    VkRenderPass rp = VK_NULL_HANDLE;
    r = vkCreateRenderPass(c.device, &rpci, nullptr, &rp);
    if (r != VK_SUCCESS) return Fail("vkCreateRenderPass", r);

    VkFramebufferCreateInfo fbi{};
    fbi.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fbi.renderPass = rp;
    fbi.attachmentCount = 1;
    fbi.pAttachments = &view;
    fbi.width = W;
    fbi.height = H;
    fbi.layers = 1;
    VkFramebuffer fb = VK_NULL_HANDLE;
    r = vkCreateFramebuffer(c.device, &fbi, nullptr, &fb);
    if (r != VK_SUCCESS) return Fail("vkCreateFramebuffer", r);

    // ── pipeline ──
    bool vok = false, fok = false;
    std::vector<uint32_t> vs = LoadSpirv(argv[1], vok);
    std::vector<uint32_t> fs = LoadSpirv(argv[2], fok);
    if (!vok || !fok) { std::printf("[VKOFF] SPIR-V load failed\n"); return 2; }
    VkShaderModule vmod = MakeModule(c, vs);
    VkShaderModule fmod = MakeModule(c, fs);
    if (!vmod || !fmod) return 1;

    VkPipelineLayoutCreateInfo plci{};
    plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    vkCreatePipelineLayout(c.device, &plci, nullptr, &layout);

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vmod;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fmod;
    stages[1].pName = "main";

    VkPipelineVertexInputStateCreateInfo vi{};
    vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    VkPipelineInputAssemblyStateCreateInfo ia{};
    ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkViewport vp{ 0.0f, 0.0f, static_cast<float>(W), static_cast<float>(H), 0.0f, 1.0f };
    VkRect2D scissor{ { 0, 0 }, { W, H } };
    VkPipelineViewportStateCreateInfo vps{};
    vps.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vps.viewportCount = 1;
    vps.pViewports = &vp;
    vps.scissorCount = 1;
    vps.pScissors = &scissor;
    VkPipelineRasterizationStateCreateInfo rs{};
    rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo ms{};
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendAttachmentState cba{};
    cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo cb{};
    cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.attachmentCount = 1;
    cb.pAttachments = &cba;
    VkGraphicsPipelineCreateInfo gpci{};
    gpci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    gpci.stageCount = 2;
    gpci.pStages = stages;
    gpci.pVertexInputState = &vi;
    gpci.pInputAssemblyState = &ia;
    gpci.pViewportState = &vps;
    gpci.pRasterizationState = &rs;
    gpci.pMultisampleState = &ms;
    gpci.pColorBlendState = &cb;
    gpci.layout = layout;
    gpci.renderPass = rp;
    gpci.subpass = 0;
    VkPipeline pipe = VK_NULL_HANDLE;
    r = vkCreateGraphicsPipelines(c.device, VK_NULL_HANDLE, 1, &gpci, nullptr, &pipe);
    if (r != VK_SUCCESS) return Fail("vkCreateGraphicsPipelines", r);
    std::printf("[VKOFF] graphics pipeline created\n");

    // ── 回读缓冲 ──
    const VkDeviceSize bytes = static_cast<VkDeviceSize>(W) * H * 4;
    VkBufferCreateInfo bci{};
    bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bci.size = bytes;
    bci.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VkBuffer buf = VK_NULL_HANDLE;
    vkCreateBuffer(c.device, &bci, nullptr, &buf);
    VkMemoryRequirements bmr{};
    vkGetBufferMemoryRequirements(c.device, buf, &bmr);
    uint32_t bmt = FindMemoryType(c, bmr.memoryTypeBits,
                                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (bmt == UINT32_MAX) { std::printf("[VKOFF] no host-visible memory\n"); return 1; }
    VkMemoryAllocateInfo bmai = mai;
    bmai.allocationSize = bmr.size;
    bmai.memoryTypeIndex = bmt;
    VkDeviceMemory bufMem = VK_NULL_HANDLE;
    vkAllocateMemory(c.device, &bmai, nullptr, &bufMem);
    vkBindBufferMemory(c.device, buf, bufMem, 0);

    // ── 录制并提交 ──
    VkCommandBuffer cmd = BeginOneShot(c);
    VkClearValue clear{};
    clear.color = { { 0.0f, 0.0f, 0.0f, 1.0f } };
    VkRenderPassBeginInfo rpb{};
    rpb.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpb.renderPass = rp;
    rpb.framebuffer = fb;
    rpb.renderArea = { { 0, 0 }, { W, H } };
    rpb.clearValueCount = 1;
    rpb.pClearValues = &clear;
    vkCmdBeginRenderPass(cmd, &rpb, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
    vkCmdDraw(cmd, 3, 1, 0, 0);   // 全屏三角形由 gl_VertexIndex 生成
    vkCmdEndRenderPass(cmd);

    VkBufferImageCopy copy{};
    copy.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    copy.imageExtent = { W, H, 1 };
    vkCmdCopyImageToBuffer(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buf, 1, &copy);

    if (!SubmitAndWait(c, cmd)) return 1;

    void* mapped = nullptr;
    vkMapMemory(c.device, bufMem, 0, bytes, 0, &mapped);
    if (!mapped) { std::printf("[VKOFF] vkMapMemory returned null\n"); return 1; }
    const uint8_t* px = static_cast<const uint8_t*>(mapped);
    const uint32_t cx = W / 2, cy = H / 2;
    const uint8_t* center = px + (static_cast<size_t>(cy) * W + cx) * 4;
    const uint8_t corner = px[0];

    std::printf("[VKOFF] readback %ux%u (RGBA8)\n", W, H);
    std::printf("[VKOFF] centre pixel = %u %u %u %u\n", center[0], center[1], center[2], center[3]);
    std::printf("[VKOFF] corner pixel = %u %u %u %u\n", corner[0], corner[1], corner[2], corner[3]);

    // 片元着色器固定输出 (0.25,0.5,0.75,1.0) → 64,128,191,255
    const bool drew = (center[0] == 64 && center[1] == 128 && center[2] == 191 && center[3] == 255);
    if (drew) {
        std::printf("[VKOFF] PASS: fragment shader output reached the readback; "
                    "offscreen rasterisation works on this device\n");
    } else {
        std::printf("[VKOFF] FAIL: centre pixel is not the shader colour "
                    "(black would mean only the clear happened)\n");
    }

    vkUnmapMemory(c.device, bufMem);
    vkDestroyBuffer(c.device, buf, nullptr);
    vkFreeMemory(c.device, bufMem, nullptr);
    vkDestroyPipeline(c.device, pipe, nullptr);
    vkDestroyPipelineLayout(c.device, layout, nullptr);
    vkDestroyShaderModule(c.device, vmod, nullptr);
    vkDestroyShaderModule(c.device, fmod, nullptr);
    vkDestroyFramebuffer(c.device, fb, nullptr);
    vkDestroyRenderPass(c.device, rp, nullptr);
    vkDestroyImageView(c.device, view, nullptr);
    vkFreeMemory(c.device, imgMem, nullptr);
    vkDestroyImage(c.device, image, nullptr);
    vkDestroyCommandPool(c.device, c.cmdPool, nullptr);
    vkDestroyDevice(c.device, nullptr);
    vkDestroyInstance(c.instance, nullptr);
    std::printf("[VKOFF] === probe complete ===\n");
    return drew ? 0 : 1;
}