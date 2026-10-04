#include "Engine/Editor/ViewportSerializer.h"
#include "Engine/Core/Log.h"
#include <fstream>

namespace {
    Engine::Logger s_Log("ViewportSerializer");

    // 默认编辑器设置文件路径
    constexpr const char* kEditorSettingsPath = "engine/editor_settings.json";
    constexpr const char* kPresetsPath        = "engine/editor_presets.json";

    // ── 结构与类型校验 ──────────────────────────────────────────────
    //
    // 加载器必须在提取之前先校验：键存在但类型不符属于**坏输入**，
    // 既不能静默沿用默认值（会把坏文件伪装成一次成功加载），也不应
    // 放任 nlohmann::json::type_error 穿出到调用方。
    bool RequireObject(const nlohmann::json& parent, const char* key) {
        return parent.contains(key) && parent.at(key).is_object();
    }

    bool TypeOk(const nlohmann::json& parent, const char* key, nlohmann::json::value_t t) {
        return !parent.contains(key) || parent.at(key).type() == t;
    }

    // 数值字段接受任意 number 类型。刻意**不**用精确 value_t 比较：
    // nlohmann 的文本往返会把非负整数从 number_integer 重新解析成
    // number_unsigned（static_cast<int>(ViewMode::Normal)==0 即 dump 成 "0"），
    // 按精确类型判断会把自家 SaveToFile 写出的合法文件判为坏输入。
    bool NumOk(const nlohmann::json& parent, const char* key) {
        return !parent.contains(key) || parent.at(key).is_number();
    }

    // 整数字段：signed / unsigned 皆可，但仍拒绝浮点与其它类型
    bool IntOk(const nlohmann::json& parent, const char* key) {
        if (!parent.contains(key)) return true;
        const auto& v = parent.at(key);
        return v.is_number_integer() || v.is_number_unsigned();
    }

    // "Camera" 是必需键：缺失时旧代码走 const operator[]，属未定义行为。
    bool ValidateConfig(const nlohmann::json& json) {
        if (!json.is_object()) {
            s_Log.Error("Viewport config root is not an object");
            return false;
        }
        if (!RequireObject(json, "Camera")) {
            s_Log.Error("Viewport config missing object field 'Camera'");
            return false;
        }
        if (!TypeOk(json, "Name", nlohmann::json::value_t::string) ||
            !TypeOk(json, "ShowGrid", nlohmann::json::value_t::boolean) ||
            !TypeOk(json, "ShowGizmos", nlohmann::json::value_t::boolean) ||
            !TypeOk(json, "ShowPostProcessing", nlohmann::json::value_t::boolean) ||
            !TypeOk(json, "ShowGridAxis", nlohmann::json::value_t::boolean) ||
            !TypeOk(json, "ShowSelectionOutline", nlohmann::json::value_t::boolean) ||
            !TypeOk(json, "GizmoLocal", nlohmann::json::value_t::boolean) ||
            !TypeOk(json, "SnapEnabled", nlohmann::json::value_t::boolean) ||
            !IntOk(json, "CurrentMode") ||
            !IntOk(json, "GizmoMode") ||
            !IntOk(json, "GridSubdivision") ||
            !IntOk(json, "VisibilityMask") ||
            !NumOk(json, "SnapValue") ||
            !NumOk(json, "CameraFlySpeed") ||
            !NumOk(json, "GridSize") ||
            !NumOk(json, "GridCellSize")) {
            s_Log.Error("Viewport config has a field with unexpected type");
            return false;
        }

        const auto& cam = json.at("Camera");
        if (!IntOk(cam, "Type") ||
            !NumOk(cam, "FOV") ||
            !NumOk(cam, "NearClip") ||
            !NumOk(cam, "FarClip") ||
            !NumOk(cam, "PositionX") ||
            !NumOk(cam, "PositionY") ||
            !NumOk(cam, "PositionZ") ||
            !NumOk(cam, "Pitch") ||
            !NumOk(cam, "Yaw") ||
            !NumOk(cam, "Distance")) {
            s_Log.Error("Viewport camera has a field with unexpected type");
            return false;
        }
        return true;
    }
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
        // 旧写法 json["Camera"] 在 const json 上对缺失键是未定义行为。
        // 这里做显式存在性检查：缺失时退回 CameraConfig 的结构体默认值。
        // 注意：加载路径上的"缺失 Camera → 失败"契约由 ValidateConfig
        // 负责（见 LoadFromFile），本函数没有失败返回通道。
        config.Camera             = DeserializeCameraConfig(
            json.contains("Camera") && json.at("Camera").is_object()
                ? json.at("Camera") : nlohmann::json::object());
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

        // 类型错误不是"缺省值"级别的轻微问题：拒绝加载，而不是伪造一份配置
        if (!ValidateConfig(root)) {
            return false;
        }

        try {
            config = Deserialize(root);
        } catch (const nlohmann::json::type_error&) {
            // ValidateConfig 之后的兜底：语义上已不可达，
            // 但保证 type_error 永远不会穿出到调用方
            s_Log.Error("Unexpected type error while deserializing viewport config");
            return false;
        }
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
            // 逐条校验：畸形 preset 被跳过并记日志，而不是抛异常或静默写成默认值
            if (!ValidateConfig(it.value())) {
                s_Log.Warn("Skipping malformed preset: {}", it.key());
                continue;
            }
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
        } catch (const nlohmann::json::type_error& e) {
            // 语法合法但字段类型不符：与 parse_error 同等对待
            s_Log.Error("Type error in editor settings: {}", e.what());
            EditorSettings defaults;
            defaults.viewports.push_back(ViewportConfig{});
            defaults.viewports.back().Name = "Viewport";
            return defaults;
        }

        return DeserializeSettings(root);
    }

} // namespace Engine