#include "Engine/SceneHierarchyPanel.h"
#include "Engine/Core/Scene/Scene.h"
#include "Engine/Core/Scene/SceneManager.h"
#include "Engine/Core/Level/Level.h"
#include "Engine/Core/Level/LevelManager.h"
#include "Engine/Core/GameObject/GameObject.h"
#include "Engine/Core/Log.h"
#include "Engine/Editor/IconsFontAwesome6.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <set>

namespace Engine {

    SceneHierarchyPanel::SceneHierarchyPanel() {
        m_SearchBuffer[0] = '\0';
    }

    SceneHierarchyPanel::SceneHierarchyPanel(Scene* scene)
        : m_Scene(scene) { m_SearchBuffer[0] = '\0'; }

    void SceneHierarchyPanel::SetScene(Scene* scene) {
        m_Scene = scene;
        m_NodeData.clear();
        m_MemCache.objectMemoryKB.clear();
    }

    void SceneHierarchyPanel::SetSelected(GameObject* obj) {
        m_Selected = obj;
        if (m_SelectionCallback) m_SelectionCallback(obj);
    }

    void SceneHierarchyPanel::ClearSelection() {
        m_Selected = nullptr;
        m_MultiSelected.clear();
        m_MultiSelectActive = false;
        if (m_SelectionCallback) m_SelectionCallback(nullptr);
    }

    // ═══════════════════════════════════════════════════════════════
    // OnImGui
    // ═══════════════════════════════════════════════════════════════

    void SceneHierarchyPanel::OnImGui() {
        if (!m_Visible) return;

        ImGui::SetNextWindowSize(ImVec2(320, 500), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Scene Hierarchy", &m_Visible)) {
            ImGui::End();
            return;
        }

        // Toolbar
        {
            ImGui::PushItemWidth(-1);
            ImGui::InputTextWithHint("##HierSearch", ICON_FA_SEARCH " Search (t:Light s:hidden)...",
                                     m_SearchBuffer, sizeof(m_SearchBuffer));
            ImGui::PopItemWidth();

            if (m_SearchBuffer[0] != '\0') ParseSearchText();
            else m_Filter = HierarchyFilter{};
        }

        // Button row
        {
            if (ImGui::SmallButton(ICON_FA_MINUS " Collapse")) { m_CollapsedScenes.clear(); }
            ImGui::SameLine();
            if (ImGui::SmallButton(ICON_FA_PLUS " Expand")) {}
            ImGui::SameLine();
            if (ImGui::SmallButton("Clear")) { ClearSelection(); }

            // Filter toggles
            ImGui::SameLine(ImGui::GetContentRegionAvail().x - 60);
            ImGui::PushStyleColor(ImGuiCol_Text, m_Filter.showHidden ? ImVec4(1,1,1,1) : ImVec4(0.4f,0.4f,0.4f,1));
            if (ImGui::SmallButton(ICON_FA_EYE)) m_Filter.showHidden = !m_Filter.showHidden;
            ImGui::PopStyleColor();
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Text, m_Filter.showDisabled ? ImVec4(1,1,1,1) : ImVec4(0.4f,0.4f,0.4f,1));
            if (ImGui::SmallButton("D")) m_Filter.showDisabled = !m_Filter.showDisabled;
            ImGui::PopStyleColor();
        }

        ImGui::Separator();

        // Hierarchy tree
        ImGui::BeginChild("TreeArea", ImVec2(0, -ImGui::GetFrameHeightWithSpacing()));
        {
            m_FrameCount++;
            if (m_FrameCount - m_MemCache.lastUpdateFrame > 60) {
                UpdateMemoryCache();
                m_MemCache.lastUpdateFrame = m_FrameCount;
            }

            // SceneManager scenes
            auto loaded = SceneManager::GetLoadedSceneNames();
            for (const auto& n : loaded) {
                Level* lv = SceneManager::FindLevel(n);
                if (lv) DrawSceneNode(n, lv);
            }
            if (!loaded.empty()) ImGui::Separator();

            // Editor scene
            if (m_Scene) {
                ImGui::TextColored(ImVec4(0.2f,1.0f,0.2f,1.0f), "%s %s",
                                   ICON_FA_CUBE, m_Scene->GetName().c_str());
                ImGui::SameLine(ImGui::GetContentRegionAvail().x - 50);
                ImGui::TextDisabled("Active");

                bool force = (m_SearchBuffer[0] != '\0');
                for (const auto& obj : m_Scene->GetObjects()) {
                    if (obj) DrawEntityNode(obj.get(), 0, force);
                }
            }

            // Empty state
            if (!m_Scene && loaded.empty()) {
                ImGui::TextColored(ImVec4(0.6f,0.6f,0.6f,1.0f), "  No scene loaded");
            }
        }
        ImGui::EndChild();

        // Footer stats
        ImGui::Separator();
        if (m_Scene) {
            char buf[64];
            float tm = m_MemCache.totalSceneMemoryKB;
            if (tm > 1024.0f)
                std::snprintf(buf, sizeof(buf), "Objects: %zu | Mem: %.2f MB", m_Scene->GetTotalObjectCount(), tm/1024.0f);
            else
                std::snprintf(buf, sizeof(buf), "Objects: %zu | Mem: %.1f KB", m_Scene->GetTotalObjectCount(), tm);
            ImGui::TextColored(ImVec4(0.6f,0.6f,0.6f,1.0f), "%s", buf);
        }

        ImGui::End();
    }

    // ═══════════════════════════════════════════════════════════════
    // DrawSceneNode
    // ═══════════════════════════════════════════════════════════════

    void SceneHierarchyPanel::DrawSceneNode(const std::string& name, Level* level) {
        if (!level) return;

        bool isActive = (SceneManager::GetActiveSceneName() == name);
        bool collapsed = (m_CollapsedScenes.find(name) != m_CollapsedScenes.end());

        const char* icon = ICON_FA_CUBE;
        switch (level->GetInfo().category) {
            case LevelCategory::Persistent: icon = ICON_FA_COG; break;
            case LevelCategory::Streaming:  icon = ICON_FA_CIRCLE; break;  // no cloud icon, use circle
            case LevelCategory::Cinematic:  icon = ICON_FA_FILM; break;
            case LevelCategory::Debug:      icon = ICON_FA_BUG; break;
            default: break;
        }

        // State color
        ImVec4 sc(1,1,1,1);
        switch (level->GetState()) {
            case LevelState::Loading:    sc = ImVec4(0.2f,0.6f,1,1); break;
            case LevelState::Active:     sc = ImVec4(0.2f,1,0.2f,1); break;
            case LevelState::Failed:     sc = ImVec4(1,0.2f,0.2f,1); break;
            default: break;
        }

        ImGui::PushStyleColor(ImGuiCol_Text, sc);
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_DefaultOpen;
        if (collapsed) flags |= ImGuiTreeNodeFlags_DefaultOpen; // no collapse flag in old Dear ImGui
        if (isActive)  flags |= ImGuiTreeNodeFlags_Framed;

        char header[256];
        std::snprintf(header, sizeof(header), "%s %s###s_%s", icon, name.c_str(), name.c_str());
        bool open = ImGui::TreeNodeEx(header, flags);
        ImGui::PopStyleColor();

        // Context menu
        if (ImGui::BeginPopupContextItem()) {
            if (ImGui::MenuItem("Set Active")) SceneManager::SwitchScene(name, SceneContext{});
            if (ImGui::MenuItem("Unload"))     SceneManager::UnloadScene(name);
            ImGui::Separator();
            if (ImGui::MenuItem("Save Scene")) {}
            ImGui::EndPopup();
        }

        if (open) {
            if (level->HasScene()) {
                for (const auto& obj : level->GetScene()->GetObjects()) {
                    if (obj) DrawEntityNode(obj.get(), 1);
                }
            }
            ImGui::TreePop();
        }
    }

    // ═══════════════════════════════════════════════════════════════
    // DrawEntityNode — 核心递归绘制
    // ═══════════════════════════════════════════════════════════════

    void SceneHierarchyPanel::DrawEntityNode(GameObject* obj, int depth, bool forceDraw) {
        if (!obj) return;
        if (!forceDraw && !IsNodeVisible(obj)) return;

        HierarchyNodeData& data = GetNodeData(obj);
        const std::string& name = obj->GetName();
        const auto& children = obj->GetChildren();
        bool hasChildren = !children.empty();

        // Search filter
        if (forceDraw) {
            bool pass = PassesFilter(obj, name);
            if (!pass) {
                bool cm = false;
                for (const auto& c : children)
                    if (c && PassesFilter(c.get(), c->GetName())) { cm = true; break; }
                if (!cm) return;
            }
        }

        // Detect prefab
        if (data.prefabStatus == PrefabStatus::None)
            data.prefabStatus = DetectPrefabStatus(obj);

        // ── Push unique ID for the entire row ──
        ImGui::PushID((int)obj->GetID());

        // TreeNode flags
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
        if (!hasChildren) flags |= ImGuiTreeNodeFlags_Leaf;
        if (m_Selected == obj) flags |= ImGuiTreeNodeFlags_Selected;

        // Indent
        if (depth > 0) ImGui::Indent(depth * 12.0f);

        // ── Visibility Eye toggle (FontAwesome) ──
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0,0,0,0));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f,0.3f,0.3f,0.5f));
        if (ImGui::SmallButton(data.visible ? ICON_FA_EYE : ICON_FA_EYE_SLASH)) {
            data.visible = !data.visible;
            if (!data.visible) m_HiddenObjects.insert(obj->GetID());
            else m_HiddenObjects.erase(obj->GetID());
        }
        ImGui::PopStyleColor(2);
        ImGui::SameLine();

        // ── Lock toggle ──
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0,0,0,0));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f,0.3f,0.3f,0.5f));
        if (ImGui::SmallButton(data.locked ? "L" : ".")) {
            data.locked = !data.locked;
            if (data.locked) m_LockedObjects.insert(obj->GetID());
            else m_LockedObjects.erase(obj->GetID());
        }
        ImGui::PopStyleColor(2);
        ImGui::SameLine();

        // ── Main tree node label ──
        char label[512];
        std::snprintf(label, sizeof(label), "%s", name.c_str());
        bool open = ImGui::TreeNodeEx("##node", flags, "%s", label);

        // Click -> select
        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
            if (ImGui::GetIO().KeyCtrl) {
                if (m_MultiSelected.count(obj)) m_MultiSelected.erase(obj);
                else m_MultiSelected.insert(obj);
            } else {
                m_Selected = obj;
                if (m_SelectionCallback) m_SelectionCallback(obj);
            }
        }

        // Hover tooltip
        if (ImGui::IsItemHovered()) {
            std::string tip = "ID: " + std::to_string(obj->GetID());
            tip += "\nLayer: " + std::to_string(obj->GetLayer());
            if (data.estimatedMemoryKB > 0.0f) {
                char mb[32]; std::snprintf(mb, sizeof(mb), "\nMem: %.1f KB", data.estimatedMemoryKB);
                tip += mb;
            }
            if (data.isStatic) tip += "\nStatic";
            ImGui::SetTooltip("%s", tip.c_str());
        }

        // Context menu
        if (ImGui::BeginPopupContextItem()) {
            if (ImGui::MenuItem("Duplicate")) {}
            if (ImGui::MenuItem("Delete")) {}
            ImGui::Separator();
            if (ImGui::BeginMenu("Color Tag")) {
                static const char* tn[] = {"None","Red","Green","Blue","Yellow","Pink","Purple","Gray"};
                for (int i = 0; i < 8; ++i)
                    if (ImGui::MenuItem(tn[i], nullptr, data.colorTag == (uint32)i))
                        data.colorTag = (uint32)i;
                ImGui::EndMenu();
            }
            ImGui::MenuItem("Static", nullptr, &data.isStatic);
            ImGui::EndPopup();
        }

        // Prefab badge
        if (data.prefabStatus != PrefabStatus::None) {
            ImGui::SameLine();
            switch (data.prefabStatus) {
                case PrefabStatus::Instance:
                    ImGui::TextColored(ImVec4(0.4f,0.6f,1,1), "%s", ICON_FA_CUBE); break;
                case PrefabStatus::InstanceModified:
                    ImGui::TextColored(ImVec4(1,0.8f,0.2f,1), "%s", ICON_FA_EXCLAMATION_TRIANGLE); break;
                case PrefabStatus::InstanceMissing:
                    ImGui::TextColored(ImVec4(1,0.2f,0.2f,1), "%s", ICON_FA_TIMES); break;
                case PrefabStatus::Variant:
                    ImGui::TextColored(ImVec4(0.6f,0.4f,1,1), "V"); break;
                default: break;
            }
        }

        // Static badge
        if (data.isStatic) {
            ImGui::SameLine();
            ImGui::TextDisabled("S");
        }

        // Color tag stripe
        if (data.colorTag > 0 && data.colorTag < 8) {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            ImVec2 p = ImGui::GetItemRectMin();
            static const ImU32 tc[] = {0, IM_COL32(230,50,50,200), IM_COL32(50,200,50,200),
                IM_COL32(50,100,255,200), IM_COL32(255,200,50,200),
                IM_COL32(255,100,180,200), IM_COL32(150,80,255,200), IM_COL32(130,130,130,200)};
            dl->AddRectFilled(ImVec2(p.x-3,p.y), ImVec2(p.x-1,p.y+ImGui::GetItemRectSize().y), tc[data.colorTag]);
        }

        // Unindent
        if (depth > 0) ImGui::Unindent(depth * 12.0f);

        // Children + TreePop
        if (open) {
            if (hasChildren) {
                for (const auto& c : children)
                    if (c) DrawEntityNode(c.get(), depth + 1, forceDraw);
            }
            ImGui::TreePop();
        }

        // ── Pop unique ID ──
        ImGui::PopID();
    }

    // ═══════════════════════════════════════════════════════════════
    // Filter / Search
    // ═══════════════════════════════════════════════════════════════

    void SceneHierarchyPanel::ParseSearchText() {
        std::string raw(m_SearchBuffer);
        m_Filter = HierarchyFilter{};
        m_Filter.searchText = raw;

        size_t t = raw.find("t:");
        if (t != std::string::npos) {
            size_t e = raw.find(' ', t);
            m_Filter.typeFilter = raw.substr(t + 2, e - t - 2);
        }
        size_t s = raw.find("s:");
        if (s != std::string::npos) {
            size_t e = raw.find(' ', s);
            std::string st = raw.substr(s + 2, e - s - 2);
            if (st == "hidden") m_Filter.showHidden = false;
            else if (st == "disabled") m_Filter.showDisabled = false;
        }
        std::string np = raw;
        auto rm = [&](const std::string& p) {
            size_t pos = np.find(p);
            while (pos != std::string::npos) {
                size_t e = np.find(' ', pos);
                np.erase(pos, e - pos + 1);
                pos = np.find(p);
            }
        };
        rm("t:"); rm("s:");
        while (!np.empty() && np[0]==' ') np.erase(0,1);
        m_Filter.nameFilter = np;
    }

    bool SceneHierarchyPanel::PassesFilter(GameObject* obj, const std::string& name) const {
        if (m_SearchBuffer[0] == '\0') return true;
        bool np = m_Filter.nameFilter.empty();
        if (!m_Filter.nameFilter.empty()) {
            std::string lc = name; std::transform(lc.begin(),lc.end(),lc.begin(),::tolower);
            std::string lf = m_Filter.nameFilter; std::transform(lf.begin(),lf.end(),lf.begin(),::tolower);
            np = (lc.find(lf) != std::string::npos);
        }
        bool tp = m_Filter.typeFilter.empty();
        if (!m_Filter.typeFilter.empty()) {
            std::string lc = name; std::transform(lc.begin(),lc.end(),lc.begin(),::tolower);
            std::string lf = m_Filter.typeFilter; std::transform(lf.begin(),lf.end(),lf.begin(),::tolower);
            tp = (lc.find(lf) != std::string::npos);
        }
        return np && tp;
    }

    bool SceneHierarchyPanel::IsNodeVisible(GameObject* obj) const {
        if (!obj) return false;
        if (m_HiddenObjects.count(obj->GetID()) && !m_Filter.showHidden) return false;
        if (!obj->IsActive() && !m_Filter.showDisabled) return false;
        return true;
    }

    // ═══════════════════════════════════════════════════════════════
    // Helpers
    // ═══════════════════════════════════════════════════════════════

    HierarchyNodeData& SceneHierarchyPanel::GetNodeData(GameObject* obj) { return m_NodeData[obj->GetID()]; }

    float SceneHierarchyPanel::EstimateMemoryKB(GameObject* obj) {
        if (!obj) return 0.0f;
        float m = 0.256f;
        obj->ForEachComponent([&m](const Component&) { m += 0.128f; });
        for (const auto& c : obj->GetChildren())
            if (c) m += EstimateMemoryKB(c.get());
        return m;
    }

    void SceneHierarchyPanel::UpdateMemoryCache() {
        if (!m_Scene) return;
        m_MemCache.objectMemoryKB.clear();
        m_MemCache.totalSceneMemoryKB = 0.0f;
        for (const auto& o : m_Scene->GetObjects()) {
            if (o) {
                float m = EstimateMemoryKB(o.get());
                m_MemCache.objectMemoryKB[o->GetID()] = m;
                m_MemCache.totalSceneMemoryKB += m;
            }
        }
        for (auto& p : m_NodeData) {
            auto it = m_MemCache.objectMemoryKB.find(p.first);
            if (it != m_MemCache.objectMemoryKB.end()) p.second.estimatedMemoryKB = it->second;
        }
    }

    void SceneHierarchyPanel::UnloadScene(const std::string& n) { SceneManager::UnloadScene(n); }
    void SceneHierarchyPanel::SetActiveScene(const std::string& n) { SceneManager::SwitchScene(n, SceneContext{}); }

    PrefabStatus SceneHierarchyPanel::DetectPrefabStatus(GameObject* obj) const {
        if (!obj) return PrefabStatus::None;
        const std::string& n = obj->GetName();
        if (n.find("(Clone)") != std::string::npos) return PrefabStatus::Instance;
        if (n.find("_Variant") != std::string::npos) return PrefabStatus::Variant;
        if (n.find("Missing") != std::string::npos) return PrefabStatus::InstanceMissing;
        return PrefabStatus::None;
    }

} // namespace Engine