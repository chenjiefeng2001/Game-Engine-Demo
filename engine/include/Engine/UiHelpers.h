#pragma once

/**
 * @file UiHelpers.h
 * @brief ImGui 辅助工具 — RAII 作用域类 + 常用控件封装
 *
 * 使用示例：
 * @code
 *   {
 *       Ui::ScopedID id("Transform");
 *       Ui::ScopedIndent indent;
 *       Ui::DrawVec3Control("Position", pos, 0.1f);
 *   }
 * @endcode
 */

#include "Engine/Types.h"
#include "Engine/Core/RHI/MathTypes.h"
#include <imgui.h>
#include <string>
#include <functional>

namespace Engine { namespace Ui {

// ============================================================
// RAII 作用域辅助类
// ============================================================

/// 自动 PushID / PopID
struct ScopedID {
    explicit ScopedID(const char* id)  { ImGui::PushID(id); }
    explicit ScopedID(int id)          { ImGui::PushID(id); }
    explicit ScopedID(void* id)        { ImGui::PushID(id); }
    ~ScopedID()                        { ImGui::PopID(); }
    ScopedID(const ScopedID&) = delete;
    ScopedID& operator=(const ScopedID&) = delete;
};

/// 自动 Indent / Unindent
struct ScopedIndent {
    ScopedIndent(float width = 0.0f)  { ImGui::Indent(width); }
    ~ScopedIndent()                    { ImGui::Unindent(); }
    ScopedIndent(const ScopedIndent&) = delete;
    ScopedIndent& operator=(const ScopedIndent&) = delete;
};

/// 自动 PushItemWidth / PopItemWidth
struct ScopedItemWidth {
    explicit ScopedItemWidth(float width) { ImGui::PushItemWidth(width); }
    ~ScopedItemWidth()                    { ImGui::PopItemWidth(); }
    ScopedItemWidth(const ScopedItemWidth&) = delete;
    ScopedItemWidth& operator=(const ScopedItemWidth&) = delete;
};

/// 自动 PushStyleColor / PopStyleColor
struct ScopedStyleColor {
    explicit ScopedStyleColor(ImGuiCol idx, ImU32 color) {
        ImGui::PushStyleColor(idx, color);
        m_Count = 1;
    }
    explicit ScopedStyleColor(ImGuiCol idx, const ImVec4& color) {
        ImGui::PushStyleColor(idx, color);
        m_Count = 1;
    }
    ~ScopedStyleColor() { ImGui::PopStyleColor(m_Count); }
    ScopedStyleColor(const ScopedStyleColor&) = delete;
    ScopedStyleColor& operator=(const ScopedStyleColor&) = delete;
private:
    int m_Count = 0;
};

/// 自动 PushStyleVar / PopStyleVar
struct ScopedStyleVar {
    explicit ScopedStyleVar(ImGuiStyleVar idx, float val) {
        ImGui::PushStyleVar(idx, val);
        m_Count = 1;
    }
    explicit ScopedStyleVar(ImGuiStyleVar idx, const ImVec2& v) {
        ImGui::PushStyleVar(idx, v);
        m_Count = 1;
    }
    ~ScopedStyleVar() { ImGui::PopStyleVar(m_Count); }
    ScopedStyleVar(const ScopedStyleVar&) = delete;
    ScopedStyleVar& operator=(const ScopedStyleVar&) = delete;
private:
    int m_Count = 0;
};

/// 自动 PushFont / PopFont
struct ScopedFont {
    explicit ScopedFont(ImFont* font) { ImGui::PushFont(font); }
    ~ScopedFont()                     { ImGui::PopFont(); }
    ScopedFont(const ScopedFont&) = delete;
    ScopedFont& operator=(const ScopedFont&) = delete;
};

/// 自动 BeginDisabled / EndDisabled
struct ScopedDisabled {
    explicit ScopedDisabled(bool disabled) { ImGui::BeginDisabled(disabled); }
    ~ScopedDisabled()                      { ImGui::EndDisabled(); }
    ScopedDisabled(const ScopedDisabled&) = delete;
    ScopedDisabled& operator=(const ScopedDisabled&) = delete;
};

// ============================================================
// 控件封装函数
// ============================================================

/**
 * @brief 带标签和颜色标记的三轴拖拽控件
 *
 * 效果：[X: 0.0] [Y: 0.0] [Z: 0.0]
 *
 * @param label    字段标签
 * @param values   三维数组（将被修改）
 * @param speed    拖拽步长
 * @param min      最小值（0 表示无限制）
 * @param max      最大值（0 表示无限制）
 * @param format   显示格式
 * @return true    如果值被修改
 */
bool DrawVec3Control(const char* label, float values[3],
                     float speed = 0.1f,
                     float min = 0.0f, float max = 0.0f,
                     const char* format = "%.2f");

/// Vec3 重载
inline bool DrawVec3Control(const char* label, Vec3& vec,
                            float speed = 0.1f,
                            float min = 0.0f, float max = 0.0f,
                            const char* format = "%.2f") {
    float v[3] = { vec.x, vec.y, vec.z };
    if (DrawVec3Control(label, v, speed, min, max, format)) {
        vec = Vec3(v[0], v[1], v[2]);
        return true;
    }
    return false;
}

/**
 * @brief 绘制带折叠标题的组件区块
 * @param label    标题
 * @param defaultOpen 是否默认展开
 * @param drawFn   区块内绘制回调
 */
void DrawComponentSection(const char* label, bool defaultOpen,
                          std::function<void()> drawFn);

/**
 * @brief 绘制悬浮工具提示
 */
void DrawTooltip(const char* text);

/**
 * @brief 绘制带提示的禁用按钮
 * @return true 如果按钮被点击（disabled=false 时）
 */
bool DrawButtonWithTooltip(const char* label, bool disabled,
                           const char* tooltip, const ImVec2& size = ImVec2(0, 0));

}} // namespace Engine::Ui
