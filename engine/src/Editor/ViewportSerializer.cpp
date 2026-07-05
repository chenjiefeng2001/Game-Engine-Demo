#include "Engine/Editor/ViewportSerializer.h"
#include "Engine/Core/Log.h"
#include <fstream>

namespace {
    Engine::Logger s_Log("ViewportSerializer");

    // 默认编辑器设置文件路径
    constexpr const char* kEditorSettingsPath = "engine/editor_settings.json";
    constexpr const char* kPresetsPath        = "engine/editor_presets.json";
}

namespace Engine {

    // ============================================================
    // CameraConfig ↔ JSON
    // ============================================================

    nlohmann::json ViewportSerializer::SerializeCameraConfig(const CameraConfig& cam) {
        return nlohmann::json{
            {"FOV",          cam.FOV},
            {"NearClip",     cam.NearClip},
            {"FarClip",      cam.FarClip},
            {"Type",         static_cast<int>(cam.Type)},
            {"PositionX",    cam.PositionX},
            {"PositionY",    cam.PositionY},
            {"PositionZ",    cam.PositionZ},
            {"Pitch",        cam.Pitch},
            {"Yaw",          cam.Yaw},
            {"Distance",     cam.Distance},
        };
    }

    CameraConfig ViewportSerializer::DeserializeCameraConfig(const nlohmann::json& json) {
        CameraConfig cam;
        cam.FOV       = json.value("FOV",       60.0f);
        cam.NearClip  = json.value("NearClip",  0.1f);
        cam.FarClip   = json.value("FarClip",   1000.0f);
        cam.Type      = static_cast<ProjectionType>(json.value("Type", 0));
        cam.PositionX = json.value("PositionX", 0.0f);
        cam.PositionY = json.value("PositionY", 5.0f);
        cam.PositionZ = json.value("PositionZ", 10.0f);
        cam.Pitch     = json.value("Pitch",     -30.0f);
        cam.Yaw       = json.value("Yaw",       -45.0f);
        cam.Distance  = json.value("Distance",  10.0f);
        return cam;
    }

    // ============================================================
    // ViewportConfig ↔ JSON
    // ============================================================

    nlohmann::json ViewportSerializer::Serialize(const ViewportConfig& config) {
        return nlohmann::json{
            {"Name",              config.Name},
            {"Camera",            SerializeCameraConfig(config.Camera)},
            {"ShowGrid",          config.ShowGrid},
            {"ShowGizmos",        config.ShowGizmos},
            {"ShowPostProcessing", config.ShowPostProcessing},
            {"ShowGridAxis",      config.ShowGridAxis},
            {"ShowSelectionOutline", config.ShowSelectionOutline},
            {"CurrentMode",       static_cast<int>(config.CurrentMode)},
            {"VisibilityMask",    config.VisibilityMask},
            {"GizmoLocal",        config.GizmoLocal},
            {"GizmoMode",         config.GizmoMode},
            {"SnapEnabled",       config.SnapEnabled},
            {"SnapValue",         config.SnapValue},
            {"CameraFlySpeed",    config.CameraFlySpeed},
            {"GridSize",          config.GridSize},
            {"GridCellSize",      config.GridCellSize},
            {"GridSubdivision",   config.GridSubdivision},
        };
    }

    ViewportConfig ViewportSerializer::Deserialize(const nlohmann::json& json) {
        ViewportConfig config;
        config.Name               = json.value("Name", "Viewport");
        config.Camera             = DeserializeCameraConfig(json["Camera"]);
        config.ShowGrid           = json.value("ShowGrid",            true);
        config.ShowGizmos         = json.value("ShowGizmos",          true);
        config.ShowPostProcessing = json.value("ShowPostProcessing",  true);
        config.ShowGridAxis       = json.value("ShowGridAxis",        true);
        config.ShowSelectionOutline = json.value("ShowSelectionOutline", true);
        config.CurrentMode        = static_cast<ViewMode>(json.value("CurrentMode", 0));
        config.VisibilityMask     = json.value("VisibilityMask",      0xFFFFFFFFu);
        config.GizmoLocal         = json.value("GizmoLocal",          false);
        config.GizmoMode          = json.value("GizmoMode",           0);
        config.SnapEnabled        = json.value("SnapEnabled",         false);
        config.SnapValue          = json.value("SnapValue",           0.5f);
        config.CameraFlySpeed     = json.value("CameraFlySpeed",      5.0f);
        config.GridSize           = json.value("GridSize",            20.0f);
        config.GridCellSize       = json.value("GridCellSize",        1.0f);
        config.GridSubdivision    = json.value("GridSubdivision",     1);
        return config;
    }

    // ============================================================
    // 文件 I/O
    // ============================================================

    bool ViewportSerializer::SaveToFile(const ViewportConfig& config,
                                         const std::string& filePath) {
        nlohmann::json root = Serialize(config);
        std::ofstream file(filePath);
        if (!file.is_open()) {
            s_Log.Error("Failed to write: {}", filePath);
            return false;
        }
        file << root.dump(4);
        file.close();
        s_Log.Info("ViewportConfig saved: {}", filePath);
        return true;
    }

    bool ViewportSerializer::LoadFromFile(ViewportConfig& config,
                                           const std::string& filePath) {
        std::ifstream file(filePath);
        if (!file.is_open()) {
            s_Log.Error("Failed to read: {}", filePath);
            return false;
        }

        nlohmann::json root;
        try {
            file >> root;
        } catch (const nlohmann::json::parse_error& e) {
            s_Log.Error("Parse error: {}", e.what());
            return false;
        }

        config = Deserialize(root);
        return true;
    }

    // ============================================================
    // 多视口布局序列化
    // ============================================================

    nlohmann::json ViewportSerializer::SerializeLayout(
        const std::vector<ViewportConfig>& viewports) {
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& vp : viewports) {
            arr.push_back(Serialize(vp));
        }
        return arr;
    }

    std::vector<ViewportConfig> ViewportSerializer::DeserializeLayout(
        const nlohmann::json& json) {
        std::vector<ViewportConfig> result;
        if (!json.is_array()) return result;

        for (const auto& item : json) {
            result.push_back(Deserialize(item));
        }
        return result;
    }

    // ============================================================
    // 预设系统
    // ============================================================

    bool ViewportSerializer::SavePresetsToFile(const PresetMap& presets,
                                                const std::string& filePath) {
        nlohmann::json root;
        for (const auto& [name, config] : presets) {
            root[name] = Serialize(config);
        }

        std::ofstream file(filePath);
        if (!file.is_open()) {
            s_Log.Error("Failed to write presets: {}", filePath);
            return false;
        }
        file << root.dump(4);
        file.close();
        s_Log.Info("Presets saved ({} entries) to {}", presets.size(), filePath);
        return true;
    }

    ViewportSerializer::PresetMap ViewportSerializer::LoadPresetsFromFile(
        const std::string& filePath) {
        PresetMap presets;
        std::ifstream file(filePath);
        if (!file.is_open()) {
            s_Log.Warn("No presets file found: {}", filePath);
            return presets;
        }

        nlohmann::json root;
        try {
            file >> root;
        } catch (const nlohmann::json::parse_error& e) {
            s_Log.Error("Parse error: {}", e.what());
            return presets;
        }

        for (auto it = root.begin(); it != root.end(); ++it) {
            presets[it.key()] = Deserialize(it.value());
        }

        s_Log.Info("Presets loaded ({} entries) from {}", presets.size(), filePath);
        return presets;
    }

    ViewportSerializer::PresetMap ViewportSerializer::GetDefaultPresets() {
        PresetMap presets;

        // ── Preset: Level Design（默认设置，实时光照 + 后期） ──
        {
            ViewportConfig cfg;
            cfg.Name = "Level Design";
            cfg.ShowGrid = true;
            cfg.ShowGizmos = true;
            cfg.ShowPostProcessing = true;
            cfg.CurrentMode = ViewMode::Normal;
            cfg.Camera.PositionX = 0.0f;
            cfg.Camera.PositionY = 10.0f;
            cfg.Camera.PositionZ = 15.0f;
            cfg.Camera.Pitch = -30.0f;
            cfg.Camera.Yaw = -45.0f;
            presets["Level Design"] = std::move(cfg);
        }

        // ── Preset: Physics Debug（线框 + 碰撞体可视化） ──
        {
            ViewportConfig cfg;
            cfg.Name = "Physics Debug";
            cfg.ShowGrid = true;
            cfg.ShowGizmos = false;
            cfg.ShowPostProcessing = false;
            cfg.CurrentMode = ViewMode::Wireframe;
            // 显示碰撞体层级
            cfg.SetLayerVisible(ViewportLayer::CollisionDebug, true);
            cfg.SetLayerVisible(ViewportLayer::StaticGeometry, true);
            cfg.SetLayerVisible(ViewportLayer::SkeletonDebug, false);
            cfg.SetLayerVisible(ViewportLayer::Particles, false);
            presets["Physics Debug"] = std::move(cfg);
        }

        // ── Preset: Lighting Only（纯光照调试） ──
        {
            ViewportConfig cfg;
            cfg.Name = "Lighting Only";
            cfg.ShowGrid = false;
            cfg.ShowPostProcessing = false;
            cfg.CurrentMode = ViewMode::LightingOnly;
            cfg.Camera.PositionY = 5.0f;
            cfg.Camera.Pitch = -15.0f;
            presets["Lighting Only"] = std::move(cfg);
        }

        // ── Preset: Top-Down View（顶视图） ──
        {
            ViewportConfig cfg;
            cfg.Name = "Top-Down View";
            cfg.ShowGrid = true;
            cfg.Camera.PositionX = 0.0f;
            cfg.Camera.PositionY = 50.0f;
            cfg.Camera.PositionZ = 0.0f;
            cfg.Camera.Pitch = -89.0f;  // 几乎垂直向下
            cfg.Camera.Yaw = 0.0f;
            cfg.Camera.Distance = 50.0f;
            presets["Top-Down View"] = std::move(cfg);
        }

        // ── Preset: Full Overlays（全部开关打开） ──
        {
            ViewportConfig cfg;
            cfg.Name = "Full Debug";
            cfg.ShowGrid = true;
            cfg.ShowGizmos = true;
            cfg.ShowPostProcessing = true;
            cfg.ShowGridAxis = true;
            cfg.ShowSelectionOutline = true;
            cfg.VisibilityMask = 0xFFFFFFFF;
            cfg.SetLayerVisible(ViewportLayer::CollisionDebug, true);
            cfg.SetLayerVisible(ViewportLayer::SkeletonDebug, true);
            presets["Full Debug"] = std::move(cfg);
        }

        // ── Preset: Minimap（小地图，远距离俯瞰） ──
        {
            ViewportConfig cfg;
            cfg.Name = "Minimap";
            cfg.ShowGrid = false;
            cfg.ShowGizmos = false;
            cfg.ShowPostProcessing = false;
            cfg.ShowSelectionOutline = false;
            cfg.Camera.PositionX = 0.0f;
            cfg.Camera.PositionY = 100.0f;
            cfg.Camera.PositionZ = 0.0f;
            cfg.Camera.Pitch = -90.0f;
            cfg.Camera.Yaw = 0.0f;
            cfg.Camera.Distance = 100.0f;
            cfg.Camera.FarClip = 5000.0f;
            cfg.VisibilityMask = 0x00000001; // 只显示静态几何体
            presets["Minimap"] = std::move(cfg);
        }

        return presets;
    }

    // ============================================================
    // 编辑器设置文件管理
    // ============================================================

    nlohmann::json ViewportSerializer::SerializeSettings(
        const EditorSettings& settings) {
        return nlohmann::json{
            {"viewports",   SerializeLayout(settings.viewports)},
            {"activePreset", settings.activePreset},
            {"uiScale",     settings.uiScale},
        };
    }

    ViewportSerializer::EditorSettings ViewportSerializer::DeserializeSettings(
        const nlohmann::json& json) {
        EditorSettings settings;
        settings.viewports   = DeserializeLayout(json.value("viewports", nlohmann::json::array()));
        settings.activePreset = json.value("activePreset", std::string("Level Design"));
        settings.uiScale     = json.value("uiScale", 1.0f);
        return settings;
    }

    bool ViewportSerializer::SaveEditorSettings(const EditorSettings& settings) {
        nlohmann::json root = SerializeSettings(settings);
        std::ofstream file(kEditorSettingsPath);
        if (!file.is_open()) {
            s_Log.Error("Failed to save editor settings: {}", kEditorSettingsPath);
            return false;
        }
        file << root.dump(4);
        file.close();
        s_Log.Info("Editor settings saved to {}", kEditorSettingsPath);
        return true;
    }

    ViewportSerializer::EditorSettings ViewportSerializer::LoadEditorSettings() {
        std::ifstream file(kEditorSettingsPath);
        if (!file.is_open()) {
            s_Log.Info("No editor settings found, using defaults");
            EditorSettings defaults;
            defaults.viewports.push_back(ViewportConfig{}); // 一个默认视口
            defaults.viewports.back().Name = "Viewport";
            defaults.activePreset = "Level Design";
            defaults.uiScale = 1.0f;
            return defaults;
        }

        nlohmann::json root;
        try {
            file >> root;
        } catch (const nlohmann::json::parse_error& e) {
            s_Log.Error("Parse error in editor settings: {}", e.what());
            EditorSettings defaults;
            defaults.viewports.push_back(ViewportConfig{});
            defaults.viewports.back().Name = "Viewport";
            return defaults;
        }

        return DeserializeSettings(root);
    }

} // namespace Engine