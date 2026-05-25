#pragma once

/**
 * @file Font.h
 * @brief 字体资源类 — ImFont 的 Resource 包装
 *
 * Font 继承自 Resource 基类，包装 ImGui 的 ImFont* 指针。
 * 字体资源通过 UIManager 加载并注册到 ResourceManager，
 * 与其他资源类型享受统一的缓存和生命周期管理。
 *
 * 注意：Font 不持有 ImFont 的所有权（ImGui 的字体图集管理生命周期），
 * Font 析构不会删除 ImFont*。
 *
 * 使用示例：
 * @code
 *   // 通过 UIManager 加载（自动注册到 ResourceManager）
 *   UIManager::Get()->LoadFont("assets/fonts/noto.ttf", 18.0f);
 *
 *   // 通过 ResourceManager 查询
 *   auto font = ResourceManager::Get()->Load<Font>("assets/fonts/noto.ttf");
 *   if (font && font->GetImFont()) {
 *       ImGui::PushFont(font->GetImFont());
 *       // ... UI 渲染 ...
 *       ImGui::PopFont();
 *   }
 * @endcode
 */

#include "Engine/Core/Resources/Resource.h"
#include "Engine/Types.h"

// 前向声明 ImFont（避免引入 imgui.h 到公共头文件）
struct ImFont;

namespace Engine {

class Font : public Resource {
public:
    /**
     * @brief 构造字体资源
     * @param path 字体文件路径（作为资源唯一标识）
     * @param fontSize 字体大小（像素）
     *
     * 构造后 ImFont* 为空，需通过 SetImFont() 设置实际的 ImGui 字体指针。
     * 通常由 UIManager 在构建字体图集后调用 SetImFont() 完成绑定。
     */
    explicit Font(std::string path, float32 fontSize = 16.0f)
        : Resource(std::move(path), ResourceType::Font)
        , m_FontSize(fontSize) {}

    ~Font() override = default;

    // ── ImFont 访问 ──

    /** 获取底层 ImGui 字体指针（可能为 nullptr，若字体尚未构建） */
    ImFont* GetImFont() const noexcept { return m_ImFont; }

    /** 设置底层 ImGui 字体指针（由 UIManager 在构建图集后调用） */
    void SetImFont(ImFont* font) noexcept { m_ImFont = font; }

    // ── 字体属性 ──

    /** 获取字体大小（像素） */
    float32 GetFontSize() const noexcept { return m_FontSize; }

    /** 设置字体大小（像素） */
    void SetFontSize(float32 size) noexcept { m_FontSize = size; }

    // ── Resource 重写 ──

    /** 字体资源的内存估算（近似值，含图集纹理） */
    size_t EstimatedMemoryBytes() const noexcept override {
        // ImFont 的图集纹理通常为 512x512 RGBA = 1MB
        return m_ImFont ? 1048576 : 0;
    }

private:
    ImFont* m_ImFont = nullptr;
    float32 m_FontSize = 16.0f;
};

} // namespace Engine