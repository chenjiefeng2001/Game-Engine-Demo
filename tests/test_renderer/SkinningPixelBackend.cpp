/**
 * @file SkinningPixelBackend.cpp
 * @brief 后端挑选：先 GL 4.6，再 Vulkan
 *
 * 挑选顺序是刻意的：GL 优先，因为只有它能跑第 4 个用例（产品绘制分支），
 * 而 GL 4.6 可用的机器通常是开发机。CI 上 GL 建不起来，会自然退到 Vulkan。
 *
 * 两个后端都失败时，report 里必须同时给出**两次尝试各自的实测原因**。只报一个
 * 原因会让人以为是某一边坏了，而实际可能是两边都没有能力。
 */
#include "SkinningPixelRig.h"

#include <cstdio>
#include <string>

namespace Engine {
namespace Test {

std::unique_ptr<ISkinningPixelBackend> SelectSkinningPixelBackend(std::string* report)
{
    std::string glWhy;
    if (auto gl = TryCreateGLSkinningBackend(&glWhy)) {
        std::printf("[skin-pixel] backend: %s\n", gl->Name());
        if (report) *report = std::string("selected ") + gl->Name();
        return gl;
    }

    std::string vkWhy;
    if (auto vk = TryCreateVulkanSkinningBackend(&vkWhy)) {
        std::printf("[skin-pixel] backend: %s (GL unavailable: %s)\n", vk->Name(), glWhy.c_str());
        if (report) {
            *report = std::string("selected ") + vk->Name() + "; GL unavailable because: " + glWhy;
        }
        return vk;
    }

    std::printf("[skin-pixel] no backend: GL: %s | Vulkan: %s\n", glWhy.c_str(), vkWhy.c_str());
    if (report) {
        *report = "no backend could render the skinning rig. OpenGL: " + glWhy +
                  " | Vulkan: " + vkWhy;
    }
    return nullptr;
}

} // namespace Test
} // namespace Engine