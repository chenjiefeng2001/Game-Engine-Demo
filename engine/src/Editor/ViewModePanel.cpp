#include "Engine/Editor/ViewModePanel.h"
#include "Engine/Core/ViewMode.h"
#include "Engine/Core/IRenderContext.h"
#include "Engine/Core/Log.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <cstdio>

namespace Engine {

    void ViewModePanel::OnImGui() {
        if (!m_Visible) return;

        ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse;
        if (!ImGui::Begin("View Mode Debug", &m_Visible, flags)) {
            ImGui::End();
            return;
        }

        // ── 当前选择指示 ──
        ImGui::TextColored(ImVec4(0.5f, 0.9f, 1.0f, 1.0f), "Current: %s",
                           ViewModeToString(m_CurrentMode));
        ImGui::TextDisabled("Group: %s", ViewModeGroupName(GetViewModeGroup(m_CurrentMode)));

        ImGui::Separator();

        // ═══════════════════════════════════════════════════════════
        // 按分组渲染所有模式
        // ═══════════════════════════════════════════════════════════

        // ── 1. Basic ──
        {
            ViewMode basicModes[] = {
                ViewMode::Normal,
                ViewMode::Unlit,
                ViewMode::Wireframe,
                ViewMode::ShadedWireframe
            };
            if (ImGui::TreeNodeEx("Basic Rendering", ImGuiTreeNodeFlags_DefaultOpen)) {
                // 2x2 网格
                int itemsPerRow = 2;
                for (int i = 0; i < IM_ARRAYSIZE(basicModes); ++i) {
                    if (i > 0 && (i % itemsPerRow) != 0) ImGui::SameLine();
                    DrawModeButton(basicModes[i], ViewModeToShortString(basicModes[i]));
                }
                ImGui::TreePop();
            }
        }

        // ── 2. G-Buffer Debug ──
        {
            if (ImGui::TreeNodeEx("G-Buffer Visualization", ImGuiTreeNodeFlags_DefaultOpen)) {
                struct GBMode { ViewMode mode; const char* label; };
                GBMode gbModes[] = {
                    {ViewMode::GBuffer_Normal,   "Normal"},
                    {ViewMode::GBuffer_Depth,    "Depth"},
                    {ViewMode::GBuffer_Albedo,   "Albedo"},
                    {ViewMode::GBuffer_Roughness,"Rough"},
                    {ViewMode::GBuffer_Metallic, "Metal"},
                    {ViewMode::GBuffer_Specular, "Spec"},
                    {ViewMode::GBuffer_AO,       "AO"},
                    {ViewMode::GBuffer_Velocity, "Velocity"},
                };
                int itemsPerRow = 4;
                for (int i = 0; i < IM_ARRAYSIZE(gbModes); ++i) {
                    if (i > 0 && (i % itemsPerRow) != 0) ImGui::SameLine();
                    DrawModeButton(gbModes[i].mode, gbModes[i].label);
                }
                ImGui::TreePop();
            }
        }

        // ── 3. Lighting Analysis ──
        {
            if (ImGui::TreeNodeEx("Lighting Analysis", ImGuiTreeNodeFlags_DefaultOpen)) {
                struct LightMode { ViewMode mode; const char* label; };
                LightMode lightModes[] = {
                    {ViewMode::LightingOnly,    "Lighting Only"},
                    {ViewMode::LightmapDensity, "LM Density"},
                    {ViewMode::Overdraw,        "Overdraw"},
                };
                int itemsPerRow = 3;
                for (int i = 0; i < IM_ARRAYSIZE(lightModes); ++i) {
                    if (i > 0 && (i % itemsPerRow) != 0) ImGui::SameLine();
                    DrawModeButton(lightModes[i].mode, lightModes[i].label);
                }
                ImGui::TreePop();
            }
        }

        // ── 4. Diagnostic ──
        {
            if (ImGui::TreeNodeEx("Diagnostic Views", ImGuiTreeNodeFlags_DefaultOpen)) {
                struct DiagMode { ViewMode mode; const char* label; };
                DiagMode diagModes[] = {
                    {ViewMode::ShaderComplexity, "Complexity"},
                    {ViewMode::LODColoration,    "LOD Color"},
                    {ViewMode::CollisionDebug,   "Collision"},
                    {ViewMode::UVOverlap,        "UV Check"},
                    {ViewMode::VertexOverdraw,   "Vtx Density"},
                };
                int itemsPerRow = 3;
                for (int i = 0; i < IM_ARRAYSIZE(diagModes); ++i) {
                    if (i > 0 && (i % itemsPerRow) != 0) ImGui::SameLine();
                    DrawModeButton(diagModes[i].mode, diagModes[i].label);
                }
                ImGui::TreePop();
            }
        }

        ImGui::Separator();

        // ── 描述信息 ──
        ImGui::TextWrapped("Description:");
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "%s",
                           ViewModeToString(m_CurrentMode));

        // 补充诊断说明
        const char* hint = "";
        switch (m_CurrentMode) {
            case ViewMode::Normal:           hint = "Full shading with lighting."; break;
            case ViewMode::Unlit:            hint = "Shows only base color/albedo, no lighting."; break;
            case ViewMode::Wireframe:        hint = "Triangle wireframe overlay."; break;
            case ViewMode::ShadedWireframe:  hint = "Colored wireframe + semi-transparent shaded interior."; break;

            case ViewMode::GBuffer_Normal:   hint = "World-space normals mapped to RGB. Red=X, Green=Y, Blue=Z."; break;
            case ViewMode::GBuffer_Depth:    hint = "Linear depth: white=near, black=far. Check for inverted or z-fighting."; break;
            case ViewMode::GBuffer_Albedo:   hint = "Base color channel without lighting."; break;
            case ViewMode::GBuffer_Roughness:hint = "0.0 (black) = smooth / mirror, 1.0 (white) = rough / diffuse."; break;
            case ViewMode::GBuffer_Metallic: hint = "0.0 (black) = dielectric, 1.0 (white) = metal. Half-values are rare."; break;
            case ViewMode::GBuffer_Specular: hint = "Specular highlight intensity visualization."; break;
            case ViewMode::GBuffer_AO:       hint = "Ambient occlusion: darker areas receive less indirect light."; break;
            case ViewMode::GBuffer_Velocity: hint = "Motion vectors: red/blue channels encode screen-space movement."; break;

            case ViewMode::LightingOnly:     hint = "Gray diffuse + specular highlights. Check light coverage."; break;
            case ViewMode::LightmapDensity:  hint = "Checkered overlay: large squares = low resolution, small = high."; break;
            case ViewMode::Overdraw:         hint = "Heatmap of pixel overdraw: red = many layers of transparency."; break;

            case ViewMode::ShaderComplexity: hint = "Red = expensive shaders, green = simple. Helps identify GPU bottlenecks."; break;
            case ViewMode::LODColoration:    hint = "Red = high-poly LOD0, Green = LOD1, Blue = LOD2, Gray = LOD3+."; break;
            case ViewMode::CollisionDebug:   hint = "Renders physics collision shapes overlaid on geometry."; break;
            case ViewMode::UVOverlap:        hint = "Highlights UV shell overlap in red — indicates texture seam issues."; break;
            case ViewMode::VertexOverdraw:   hint = "Heatmap of vertex density per pixel. Hotspots indicate over-tessellation."; break;

            default: break;
        }
        if (hint[0] != '\0') {
            ImGui::TextWrapped("%s", hint);
        }

        ImGui::Separator();

        // ── 重置 ──
        if (ImGui::Button("Reset to Normal", ImVec2(140, 28))) {
            m_CurrentMode = ViewMode::Normal;
            if (m_ModeChangeCallback) m_ModeChangeCallback(m_CurrentMode);
        }

        ImGui::End();
    }

    // ═══════════════════════════════════════════════════════════════
    // 模式按钮
    // ═══════════════════════════════════════════════════════════════

    void ViewModePanel::DrawModeButton(ViewMode mode, const char* label) {
        bool isSelected = (m_CurrentMode == mode);

        // 选中状态高亮
        ImGui::PushStyleColor(ImGuiCol_Button,
            isSelected ? ImVec4(0.3f, 0.6f, 0.9f, 1.0f)
                       : ImVec4(0.18f, 0.18f, 0.2f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
            isSelected ? ImVec4(0.4f, 0.7f, 1.0f, 1.0f)
                       : ImVec4(0.25f, 0.25f, 0.3f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,
            ImVec4(0.2f, 0.5f, 0.8f, 1.0f));

        if (ImGui::Button(label, ImVec2(100, 24))) {
            if (m_CurrentMode != mode) {
                m_CurrentMode = mode;
                if (m_ModeChangeCallback) m_ModeChangeCallback(mode);
                ENGINE_LOG_INFO("ViewMode", "Switched to {} ({})",
                                ViewModeToString(mode), ViewModeGroupName(GetViewModeGroup(mode)));
            }
        }

        ImGui::PopStyleColor(3);
    }

} // namespace Engine