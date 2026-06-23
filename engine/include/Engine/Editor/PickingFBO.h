#pragma once

/**
 * @file PickingFBO.h
 * @brief 物体拾取帧缓冲 — ID Buffer 方案
 *
 * 原理：
 *   1. 每帧额外渲染一个 Pass，将每个 GameObject 的唯一 uint32 ID 编码为 RGB 颜色
 *      （ID 0 = 背景，ID 1~N = 对应 GameObject）
 *   2. 鼠标点击视口时，读取该像素的颜色值，解码回 ID
 *   3. 通过 Scene::FindByID() 定位到目标对象
 *
 * 使用方式：
 * @code
 *   PickingFBO picker(gl);
 *   picker.Resize(viewportW, viewportH);
 *
 *   // 渲染 pass
 *   picker.Bind();
 *   for (auto& obj : scene) {
 *       uint32 id = obj->GetID();
 *       picker.RenderID(id, obj->GetTransform().GetWorldMatrix());
 *   }
 *   picker.Unbind();
 *
 *   // 读取
 *   uint32 pickedID = picker.ReadPixel(mouseX, mouseY);
 * @endcode
 */

#include "Engine/Types.h"

struct GladGLContext;

namespace Engine {

    class PickingFBO {
    public:
        PickingFBO(GladGLContext& gl);
        ~PickingFBO();

        PickingFBO(const PickingFBO&) = delete;
        PickingFBO& operator=(const PickingFBO&) = delete;

        PickingFBO(PickingFBO&& other) noexcept;
        PickingFBO& operator=(PickingFBO&& other) noexcept;

        // ── 生命周期 ──
        void Resize(uint32 width, uint32 height);
        uint32 GetWidth() const { return m_Width; }
        uint32 GetHeight() const { return m_Height; }

        // ── 绑定 ──
        void Bind();
        void Unbind();

        // ── 编码与读取 ──
        /** 将 uint32 ID 编码为 RGB 颜色（ID 0 = 黑色背景） */
        static void EncodeID(uint32 id, float& r, float& g, float& b);

        /** 读取鼠标位置像素，解码回 ID */
        uint32 ReadPixel(int32 mouseX, int32 mouseY);

        /** 检查有效性 */
        bool IsValid() const { return m_FBO != 0; }

    private:
        void Destroy();

        GladGLContext& m_GL;

        uint32 m_FBO      = 0;
        uint32 m_ColorTexture = 0;  // R32UI 纹理
        uint32 m_DepthBuffer = 0;   // 深度 Renderbuffer

        uint32 m_Width  = 0;
        uint32 m_Height = 0;
    };

} // namespace Engine