#pragma once

/**
 * @file OpenGLFramebuffer.h
 * @brief OpenGL 帧缓冲对象（FBO）— 离屏渲染 + Entity ID Picking
 *
 * 创建三个附件：
 *   1. 颜色附件（RGBA8 纹理）— 供 ImGui::Image() 显示
 *   2. 深度模板附件（DEPTH24_STENCIL8 渲染缓冲）
 *   3. 拾取附件（R32I 纹理）— 存储每个像素的 GameObject ID
 *
 * 拾取使用方式：
 * @code
 *   // 渲染场景时，用拾取 Shader 将 GameObject ID 写入 Pick Texture
 *   // 鼠标点击时（HRC-3 D3：结构化结果，失败与未命中必须可区分）：
 *   auto pick = fbo.ReadPickPixel(mouseX, mouseY, viewportMinX, viewportMinY);
 *   if (pick.status == OpenGLFramebuffer::PickReadbackStatus::Hit) {
 *       GameObject* obj = scene->FindByID(pick.entityId);  // entityId >= 1
 *   } else if (pick.status == OpenGLFramebuffer::PickReadbackStatus::NoHit) {
 *       // 正常未命中（sentinel -1），**不是错误**
 *   }
 * @endcode
 */

#include "Engine/Types.h"
#include <thread>

struct GladGLContext;

namespace Engine {

    class OpenGLFramebuffer {
    public:
        OpenGLFramebuffer(GladGLContext& gl);
        ~OpenGLFramebuffer();

        OpenGLFramebuffer(const OpenGLFramebuffer&) = delete;
        OpenGLFramebuffer& operator=(const OpenGLFramebuffer&) = delete;

        // 移动语义
        OpenGLFramebuffer(OpenGLFramebuffer&& other) noexcept;
        OpenGLFramebuffer& operator=(OpenGLFramebuffer&& other) noexcept;

        // ── 生命周期 ──
        /**
         * 创建或重新分配 FBO 为指定尺寸（若尺寸没变则跳过）。
         *
         * Slice E 契约（fail-closed）：
         *   - **零尺寸被拒绝**：不再"静默 max(w,1)"。零/负尺寸直接返回 false，
         *     旧 FBO **原样保留**（不销毁、不创建 1x1 的伪装帧）。
         *   - **创建失败返回 false**：此时旧资源已被销毁，FBO 处于无效状态，
         *     调用方必须据此**不发布任何帧**（不得把旧内容标成新尺寸）。
         *   - 成功返回 true，且 `GetWidth()/GetHeight()` 等于请求尺寸。
         *
         * 注意：重建是"先销毁后创建"，所以失败**不能**恢复旧 FBO —— 这是
         * 已知的、被 E-C6d 明确接受的行为（失败后 host 停止发布帧，
         * 直到重建成功）。
         */
        bool Resize(uint32 width, uint32 height);

        /** 获取当前 FBO 宽度 */
        uint32 GetWidth() const { return m_Width; }

        /** 获取当前 FBO 高度 */
        uint32 GetHeight() const { return m_Height; }

        // ── 绑定 ──
        /** 绑定 FBO 为当前渲染目标 */
        void Bind();

        /** 解绑 FBO，恢复到默认帧缓冲 */
         void Unbind();
         void Clear(float red, float green, float blue, float alpha);
         bool ReadColorPixel(uint32 x, uint32 y, uint8_t out[4]) const;
         void Abandon();
         bool IsOwnerThread() const {
             return m_OwnerThreadId == std::this_thread::get_id();
         }

        // ── 纹理访问 ──
        /** 获取颜色附件的 OpenGL 纹理 ID（用于 ImGui::Image） */
        uint32 GetColorTextureID() const { return m_ColorTexture; }

        /** 检查 FBO 是否有效 */
         bool IsValid() const { return m_FBO != 0; }
         uint32 GetFramebufferID() const { return m_FBO; }

    public:
        // ── ID 拾取 ──
        /**
         * @brief HRC-3 D3：结构化 pick readback 结果
         *
         * D3-C6 要求 **readback 失败 ≠ no-hit**。旧的 ReadPixelID 把两者都返回 0，
         * 因此被废止（Step 2 废止条款 1）。
         */
        enum class PickReadbackStatus {
            Hit,              ///< 命中，entityId >= 1（D3-C2 有效区间）
            NoHit,            ///< 正常未命中：像素为 sentinel -1
            InvalidCoordinate,///< 坐标非有限值
            OutsideViewport,  ///< 越界 —— 显式拒绝，**绝不 clamp**（D3-C3）
            NotReady          ///< 无拾取纹理 / 非 owner thread / 无 current context
        };

        struct PickReadback {
            PickReadbackStatus status = PickReadbackStatus::NotReady;
            uint32 entityId = 0;   ///< 仅 status == Hit 时有意义
        };

        /**
         * @param mouseX 绝对屏幕 X（物理像素）
         * @param mouseY 绝对屏幕 Y（物理像素）
         * @param viewportMinX 视口左上角绝对 X
         * @param viewportMinY 视口左上角绝对 Y
         * @return 结构化结果，区分 Hit / NoHit / InvalidCoordinate /
         *         OutsideViewport / NotReady
         *
         * D3-C2：sentinel 为 **-1**；**不得**用 max(0, value) 之类的 clamp，
         * 那会把 -1 变成 0 并摧毁 sentinel（Step 2 废止条款 2）。
         * D3-C3：越界一律拒绝，禁止 clamp 到边缘像素（否则变成假命中）。
         */
        PickReadback ReadPickPixel(float mouseX, float mouseY,
                                   float viewportMinX, float viewportMinY) const;

        /** 获取拾取纹理的 OpenGL ID（用于多渲染目标绑定） */
        uint32 GetPickTextureID() const { return m_PickTexture; }

        /** 是否支持 ID 拾取 */
        bool HasPickTexture() const { return m_PickTexture != 0; }

        /** 绑定拾取纹理为第二个颜色输出（在场景渲染前设置） */
        void BindForPickRendering();

    private:
        /** 销毁所有 GL 资源 */
        void Destroy();

        GladGLContext& m_GL;
        std::thread::id m_OwnerThreadId;

        uint32 m_FBO           = 0;   // 帧缓冲对象
        uint32 m_ColorTexture  = 0;   // 颜色附件（RGBA8 纹理）
        uint32 m_DepthStencil  = 0;   // 深度模板附件（Renderbuffer）
        uint32 m_PickTexture   = 0;   // 拾取附件（R32I 纹理）

        uint32 m_Width  = 0;
        uint32 m_Height = 0;
    };

} // namespace Engine