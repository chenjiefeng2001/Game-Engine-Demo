#pragma once
/**
 * @file GameHUD.h
 * @brief 游戏内 HUD（平视显示器）
 *
 * 在屏幕空间叠加显示信息：FPS、物体数、图元类型、坐标等。
 * 使用 TextRenderer + PrimitiveBatch 渲染，不依赖 ImGui。
 * 支持显示/隐藏切换。
 */

#include "Engine/Types.h"
#include "Engine/Core/RHI/MathTypes.h"
#include <memory>
#include <string>
#include <vector>

namespace Engine {

    class IPrimitiveBatch;
    class IRenderContext;
    class IGraphicsFactory;
    class Shader;

    class TextRenderer;

    /// HUD 元素类型
    enum class HUDTextFlags : uint8 {
        None        = 0,
        FPS         = 1 << 0,
        Position    = 1 << 1,
        Objects     = 1 << 2,
        DrawCalls   = 1 << 3,
        Memory      = 1 << 4,
        All         = 0xFF,
    };
    inline HUDTextFlags operator|(HUDTextFlags a, HUDTextFlags b) {
        return static_cast<HUDTextFlags>(static_cast<uint8>(a) | static_cast<uint8>(b));
    }
    inline bool operator&(HUDTextFlags a, HUDTextFlags b) {
        return (static_cast<uint8>(a) & static_cast<uint8>(b)) != 0;
    }

    class GameHUD {
    public:
        GameHUD();
        ~GameHUD();

        GameHUD(const GameHUD&) = delete;
        GameHUD& operator=(const GameHUD&) = delete;

        bool Initialize(IGraphicsFactory& factory, IRenderContext& context);

        void SetVisible(bool v) { m_Visible = v; }
        bool IsVisible() const { return m_Visible; }
        void Toggle() { m_Visible = !m_Visible; }

        // ── 数据更新 ──
        void SetFPS(float fps) { m_FPS = fps; }
        void SetFrameTime(float ms) { m_FrameTime = ms; }
        void SetDrawCalls(uint32 dc) { m_DrawCalls = dc; }
        void SetObjectCount(uint32 count) { m_ObjectCount = count; }
        void SetVisibleCount(uint32 count) { m_VisibleCount = count; }
        void SetMemoryUsage(size_t bytes) { m_MemoryBytes = bytes; }
        void SetPosition(const Vec3& pos) { m_Position = pos; }
        void SetCustomText(const std::string& text) { m_CustomText = text; }

        // ── 渲染 ──
        void Render(IPrimitiveBatch& batch);

        void SetFlags(HUDTextFlags flags) { m_Flags = flags; }
        HUDTextFlags GetFlags() const { return m_Flags; }

        // ── 访问子对象（供外部绑定纹理等） ──
        TextRenderer& GetTextRenderer() { return *m_Text; }
        Shader* GetShader() const { return m_Shader.get(); }

        /** 更新屏幕尺寸（窗口 resize 时调用） */
        void UpdateScreenSize(float w, float h);

    private:
        bool m_Visible = true;
        float m_FPS = 0;
        float m_FrameTime = 0;
        uint32 m_DrawCalls = 0;
        uint32 m_ObjectCount = 0;
        uint32 m_VisibleCount = 0;
        size_t m_MemoryBytes = 0;
        Vec3 m_Position;
        std::string m_CustomText;

        HUDTextFlags m_Flags = HUDTextFlags::All;
        std::unique_ptr<TextRenderer> m_Text;
        std::shared_ptr<Shader> m_Shader;
        float m_ScreenWidth = 1280;
        float m_ScreenHeight = 720;
    };

} // namespace Engine
