#pragma once

/**
 * @file IDBufferPass.h
 * @brief ID-Buffer 拾取 Pass — 将 EntityID 写入 R32_UINT 渲染目标
 *
 * 鼠标点击视口时：
 *   1. 读取鼠标坐标处的像素值
 *   2. 映射回 EntityID
 *   3. 在编辑器中高亮对应的物体
 *
 * 使用方式：
 *   @code
 *     IDBufferPass picker;
 *     picker.Initialize(device, width, height);
 *     
 *     // 在 GBuffer 阶段调用：
 *     picker.Begin(cmdList);
 *     // ... 绘制所有物体，写入 EntityID ...
 *     picker.End(cmdList);
 *     
 *     // 鼠标点击时：
 *     uint32_t entityID = picker.ReadbackPixel(mouseX, mouseY);
 *   @endcode
 */

#include "Engine/Core/RHI/IRHIDevice.h"
#include "Engine/Core/RHI/IRHICommandList.h"
#include "Engine/Core/RHI/PSODesc.h"
#include "Engine/Core/RHI/PSOCache.h"
#include "Engine/Core/StringID.h"

namespace Engine {
namespace Rendering {

    class IDBufferPass {
    public:
        IDBufferPass() = default;
        ~IDBufferPass();

        IDBufferPass(const IDBufferPass&) = delete;
        IDBufferPass& operator=(const IDBufferPass&) = delete;

        /**
         * @brief 初始化 ID-Buffer
         * 
         * @param device RHI 设备
         * @param width  视口宽度
         * @param height 视口高度
         * @return true  成功
         */
        bool Initialize(RHI::IRHIDevice& device, uint32_t width, uint32_t height);

        /**
         * @brief 开始 ID-Buffer Pass
         * 
         * 应该作为 GBuffer 的额外渲染目标绑定
         */
        void Begin(RHI::IRHICommandList& cmdList);

        /** @brief 结束 ID-Buffer Pass */
        void End(RHI::IRHICommandList& cmdList);

        /**
         * @brief 异步读回指定像素的 Entity ID
         * 
         * 通过 staging buffer 读回 ID-Buffer 的像素。
         * 返回值在调用后下一帧才有效（需要 GPU 完成拷贝）。
         * 
         * @param x 屏幕 X 坐标
         * @param y 屏幕 Y 坐标
         * @return uint32 EntityID（0 = 未选中）
         */
        uint32_t ReadbackPixel(uint32_t x, uint32_t y);

        /**
         * @brief 获取 ID-Buffer 纹理
         */
        RHI::IRHITexture* GetIDTexture() const noexcept { return m_IDTexture; }

        /** @brief 获取当前选中的 EntityID */
        uint32_t GetSelectedEntityID() const noexcept { return m_SelectedEntityID; }

        /** @brief 清除选中状态 */
        void ClearSelection() noexcept { m_SelectedEntityID = 0; }

    private:
        // ── GPU 资源 ──
        RHI::IRHITexture*       m_IDTexture     = nullptr;  // R32_UINT 渲染目标
        RHI::IRHIBuffer*        m_ReadbackBuffer = nullptr;  // Readback buffer
        RHI::IRHIPipelineState* m_IDPSO          = nullptr;  // ID 写入 PSO
        RHI::IRHIBuffer*        m_StagingBuffer  = nullptr;  // 像素读回 staging

        // ── 状态 ──
        uint32_t m_Width  = 0;
        uint32_t m_Height = 0;
        uint32_t m_SelectedEntityID = 0;
        bool     m_Initialized = false;

        // ── ID-Buffer 着色器名称 ──
        static constexpr const char* kIDVertexName   = "id_buffer.vert";
        static constexpr const char* kIDFragmentName = "id_buffer.frag";
    };

} // namespace Rendering
} // namespace Engine