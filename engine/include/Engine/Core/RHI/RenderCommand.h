#pragma once

#include "Engine/Core/RenderResources/Texture.h"
#include <memory>

namespace Engine {

    /**
     * @brief 渲染命令 — 纯数据结构，不依赖任何第三方数学库
     *
     * RHI 原则：矩阵通过 float[16] 传递，彻底隔离 glm 等第三方库。
     * 纹理通过抽象基类 Texture 指针传递，不暴露具体 API 实现。
     *
     * C++20 指定初始化器使用示例：
     * @code
     *   RenderCommand cmd{
     *       .worldMatrix = { // 4x4 变换矩阵
     *           1.0f, 0.0f, 0.0f, 0.0f,
     *           0.0f, 1.0f, 0.0f, 0.0f,
     *           0.0f, 0.0f, 1.0f, 0.0f,
     *           0.0f, 0.0f, 0.0f, 1.0f
     *       },
     *       .uv          = { 0.0f, 0.0f, 1.0f, 1.0f },
     *       .color       = { 1.0f, 0.0f, 0.0f, 1.0f },
     *       .texture     = tex,
     *   };
     * @endcode
     */
    struct RenderCommand {
        /** 4×4 世界变换矩阵（列主序），默认单位矩阵 */
        float worldMatrix[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 1.0f
        };

        /** UV 坐标 [u, v, w, h]，默认全 UV */
        float uv[4] = { 0.0f, 0.0f, 1.0f, 1.0f };

        /** 颜色 [r, g, b, a]，默认白色 */
        float color[4] = { 1.0f, 1.0f, 1.0f, 1.0f };

        /** 纹理（抽象接口，不依赖具体 API） */
        std::shared_ptr<Texture> texture;

        /** 排序层级 */
        int sortingLayer = 0;

        /** 层级内序号 */
        int orderInLayer = 0;

        /** 可见性层级（位掩码对应 ViewportConfig::VisibilityMask） */
        uint32 layerMask = 0xFFFFFFFF;
    };

} // namespace Engine
