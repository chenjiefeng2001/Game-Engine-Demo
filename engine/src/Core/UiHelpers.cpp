#include "Engine/UiHelpers.h"
#include <cstdio>

namespace Engine { namespace Ui {

bool DrawVec3Control(const char* label, float values[3],
                     float speed, float min, float max,
                     const char* format)
{
    bool modified = false;

    ImGui::AlignTextToFramePadding();
    ImGui::Text("%s", label);
    ImGui::SameLine();

    ScopedID id(label);
    ScopedItemWidth itemWidth(ImGui::GetContentRegionAvail().x);

    // 三个轴共享的单项宽度
    float itemW = (ImGui::GetContentRegionAvail().x - 8.0f) / 3.0f;

    // X 轴（红色）
    {
        ScopedStyleColor color(ImGuiCol_Text, IM_COL32(255, 100, 100, 255));
        ImGui::Text("X"); ImGui::SameLine();
    }
    {
        ScopedItemWidth w(itemW);
        if (ImGui::DragFloat("##X", &values[0], speed, min, max, format))
            modified = true;
    }
    ImGui::SameLine();

    // Y 轴（绿色）
    {
        ScopedStyleColor color(ImGuiCol_Text, IM_COL32(100, 255, 100, 255));
        ImGui::Text("Y"); ImGui::SameLine();
    }
    {
        ScopedItemWidth w(itemW);
        if (ImGui::DragFloat("##Y", &values[1], speed, min, max, format))
            modified = true;
    }
    ImGui::SameLine();

    // Z 轴（蓝色）
    {
        ScopedStyleColor color(ImGuiCol_Text, IM_COL32(100, 150, 255, 255));
        ImGui::Text("Z"); ImGui::SameLine();
    }
    {
        ScopedItemWidth w(itemW);
        if (ImGui::DragFloat("##Z", &values[2], speed, min, max, format))
            modified = true;
    }

    return modified;
}

void DrawComponentSection(const char* label, bool defaultOpen,
                          std::function<void()> drawFn)
{
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_None;
    if (defaultOpen)
        flags |= ImGuiTreeNodeFlags_DefaultOpen;

    if (!ImGui::CollapsingHeader(label, flags))
        return;

    ScopedIndent indent;
    if (drawFn)
        drawFn();
}

void DrawTooltip(const char* text)
{
    ImGui::BeginTooltip();
    ImGui::TextUnformatted(text);
    ImGui::EndTooltip();
}

bool DrawButtonWithTooltip(const char* label, bool disabled,
                           const char* tooltip, const ImVec2& size)
{
    ScopedDisabled disable(disabled);
    bool clicked = ImGui::Button(label, size);

    if (disabled && ImGui::IsItemHovered())
        DrawTooltip(tooltip);

    return clicked;
}

}} // namespace Engine::Ui
