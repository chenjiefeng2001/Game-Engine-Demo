// P1: ViewportSerializer 测试
//
// 对应 engine/include/Engine/Editor/ViewportSerializer.h（103 行头）
// 与 engine/src/Editor/ViewportSerializer.cpp（365 行实现），此前零直接测试。
//
// 依赖准入结论（审计确认）：传递闭包只有
//     ViewportSerializer.h -> ViewportConfig.h -> Types.h + ViewMode.h
//     ViewportSerializer.cpp -> Log.h + <fstream> + nlohmann/json
// 无窗口系统 / OpenGL / GLFW / Avalonia / 设备上下文 / 资源管理器依赖，
// 全部入口都是 static 且按参数收发数据 —— 因此可在 headless 下**原样**测试，
// 不需要为可测性做任何重构。
//
// 覆盖面：
//   - ViewportConfig <-> JSON 的结构、默认值与往返
//   - 多视口布局 SerializeLayout / DeserializeLayout
//   - 预设库（显式路径）：保存 / 加载 / 内置预设
//   - EditorSettings 的纯序列化（SerializeSettings / DeserializeSettings）
//   - 显式路径文件 I/O 的失败契约
//
// 刻意排除 SaveEditorSettings / LoadEditorSettings：
//   它们写死相对路径 "engine/editor_settings.json"，把当前工作目录变成
//   隐式进程状态（同 test_pick_transport 的 manifest 问题）。
//   不为了制造确定性而在本单元测试里操纵 CWD；该依赖记为 observation。
//
// 关于 json.value() 的类型行为：
//   下方"标量类型校验契约"一节的用例全部使用 EXPECT_FALSE —— 即修复后的
//   正式契约。修复前三个 loader 只捕获 parse_error，Deserialize 的
//   json.value(key, default) 在"键存在但类型不符"时会抛 type_error 并穿出
//   loader；缺失 "Camera" 更会走 const operator[] 进入未定义行为。
//   下面只对**已定义**的行为写断言：Deserialize 自身在缺失 Camera 时退回
//   CameraConfig 结构体默认值（无 UB），失败信号由 LoadFromFile 承担。
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "Engine/Editor/ViewportSerializer.h"

using namespace Engine;

namespace {

class TempViewportFile {
public:
    explicit TempViewportFile(const char* tag) {
        const auto dir = std::filesystem::temp_directory_path() / "engine_viewport_tests";
        std::filesystem::create_directories(dir);
        m_Path = (dir / (std::string(tag) + ".json")).string();
        std::error_code ec;
        std::filesystem::remove(m_Path, ec);
    }
    ~TempViewportFile() {
        std::error_code ec;
        std::filesystem::remove(m_Path, ec);
    }
    const std::string& path() const { return m_Path; }
    bool exists() const { return std::filesystem::exists(m_Path); }
    void writeText(const std::string& t) const {
        std::ofstream out(m_Path, std::ios::binary | std::ios::trunc);
        out << t;
    }

private:
    std::string m_Path;
};

// 始终带上 "Camera"：Deserialize 对缺失该键是未定义行为，本文件不进入该路径
nlohmann::json JsonWithCamera(const nlohmann::json& rest = nlohmann::json::object()) {
    nlohmann::json j = rest;
    if (!j.contains("Camera")) j["Camera"] = nlohmann::json::object();
    return j;
}

ViewportConfig MakeConfig(const std::string& name) {
    ViewportConfig c;
    c.Name = name;
    return c;
}

} // namespace

// ── Serialize 结构 ─────────────────────────────────────────────────────
TEST(ViewportSerializerTest, Serialize_EmitsSeventeenTopLevelKeys)
{
    const auto j = ViewportSerializer::Serialize(ViewportConfig{});
    EXPECT_EQ(j.size(), 17u);
    EXPECT_TRUE(j.contains("Name"));
    EXPECT_TRUE(j.contains("Camera"));
    EXPECT_TRUE(j.contains("ShowGrid"));
    EXPECT_TRUE(j.contains("CurrentMode"));
    EXPECT_TRUE(j.contains("VisibilityMask"));
    EXPECT_TRUE(j.contains("GridSubdivision"));
}

TEST(ViewportSerializerTest, Serialize_CameraSectionEmitsTenKeys)
{
    const auto cam = ViewportSerializer::Serialize(ViewportConfig{})["Camera"];
    EXPECT_EQ(cam.size(), 10u);
    EXPECT_TRUE(cam.contains("FOV"));
    EXPECT_TRUE(cam.contains("NearClip"));
    EXPECT_TRUE(cam.contains("FarClip"));
    EXPECT_TRUE(cam.contains("Type"));
    EXPECT_TRUE(cam.contains("Distance"));
}

TEST(ViewportSerializerTest, Serialize_StoresProjectionTypeAsInteger)
{
    ViewportConfig c;
    c.Camera.Type = ProjectionType::Orthographic;
    const auto j = ViewportSerializer::Serialize(c);
    EXPECT_TRUE(j["Camera"]["Type"].is_number_integer());
    EXPECT_EQ(j["Camera"]["Type"].get<int>(), static_cast<int>(ProjectionType::Orthographic));
}

TEST(ViewportSerializerTest, Serialize_StoresVisibilityMaskAsUnsigned)
{
    ViewportConfig c;
    c.VisibilityMask = 0x00000005u;
    const auto j = ViewportSerializer::Serialize(c);
    EXPECT_EQ(j["VisibilityMask"].get<uint32>(), 0x00000005u);
}

TEST(ViewportSerializerTest, Serialize_DefaultCameraMatchesStructDefaults)
{
    const auto cam = ViewportSerializer::Serialize(ViewportConfig{})["Camera"];
    EXPECT_FLOAT_EQ(cam["FOV"].get<float>(), 60.0f);
    EXPECT_FLOAT_EQ(cam["NearClip"].get<float>(), 0.1f);
    EXPECT_FLOAT_EQ(cam["FarClip"].get<float>(), 1000.0f);
    EXPECT_FLOAT_EQ(cam["PositionY"].get<float>(), 5.0f);
    EXPECT_FLOAT_EQ(cam["PositionZ"].get<float>(), 10.0f);
    EXPECT_FLOAT_EQ(cam["Pitch"].get<float>(), -30.0f);
    EXPECT_FLOAT_EQ(cam["Yaw"].get<float>(), -45.0f);
    EXPECT_FLOAT_EQ(cam["Distance"].get<float>(), 10.0f);
}

// ── Deserialize 默认值 ─────────────────────────────────────────────────
TEST(ViewportSerializerTest, Deserialize_EmptyCameraObject_UsesDocumentedDefaults)
{
    const auto c = ViewportSerializer::Deserialize(JsonWithCamera());
    EXPECT_EQ(c.Name, "Viewport");
    EXPECT_TRUE(c.ShowGrid);
    EXPECT_TRUE(c.ShowGizmos);
    EXPECT_TRUE(c.ShowPostProcessing);
    EXPECT_TRUE(c.ShowGridAxis);
    EXPECT_TRUE(c.ShowSelectionOutline);
    EXPECT_EQ(c.CurrentMode, ViewMode::Normal);
    EXPECT_EQ(c.VisibilityMask, 0xFFFFFFFFu);
    EXPECT_FALSE(c.GizmoLocal);
    EXPECT_EQ(c.GizmoMode, 0);
    EXPECT_FALSE(c.SnapEnabled);
    EXPECT_FLOAT_EQ(c.SnapValue, 0.5f);
    EXPECT_FLOAT_EQ(c.CameraFlySpeed, 5.0f);
    EXPECT_FLOAT_EQ(c.GridSize, 20.0f);
    EXPECT_FLOAT_EQ(c.GridCellSize, 1.0f);
    EXPECT_EQ(c.GridSubdivision, 1);
}

TEST(ViewportSerializerTest, Deserialize_EmptyCamera_UsesCameraStructDefaults)
{
    const auto c = ViewportSerializer::Deserialize(JsonWithCamera());
    EXPECT_FLOAT_EQ(c.Camera.FOV, 60.0f);
    EXPECT_FLOAT_EQ(c.Camera.NearClip, 0.1f);
    EXPECT_FLOAT_EQ(c.Camera.FarClip, 1000.0f);
    EXPECT_EQ(c.Camera.Type, ProjectionType::Perspective);
    EXPECT_FLOAT_EQ(c.Camera.PositionX, 0.0f);
    EXPECT_FLOAT_EQ(c.Camera.PositionY, 5.0f);
    EXPECT_FLOAT_EQ(c.Camera.PositionZ, 10.0f);
    EXPECT_FLOAT_EQ(c.Camera.Pitch, -30.0f);
    EXPECT_FLOAT_EQ(c.Camera.Yaw, -45.0f);
    EXPECT_FLOAT_EQ(c.Camera.Distance, 10.0f);
}

TEST(ViewportSerializerTest, Deserialize_PartialObject_KeepsDefaultsForAbsentKeys)
{
    nlohmann::json j;
    j["Name"] = "partial";
    j["ShowGrid"] = false;
    j["Camera"] = nlohmann::json::object();
    j["Camera"]["FOV"] = 90.0f;

    const auto c = ViewportSerializer::Deserialize(j);
    EXPECT_EQ(c.Name, "partial");
    EXPECT_FALSE(c.ShowGrid);
    EXPECT_FLOAT_EQ(c.Camera.FOV, 90.0f);
    // 未提供的键仍为默认
    EXPECT_TRUE(c.ShowGizmos);
    EXPECT_FLOAT_EQ(c.GridSize, 20.0f);
}

// ── 往返 ───────────────────────────────────────────────────────────────
TEST(ViewportSerializerTest, RoundTrip_AllFieldsPreserved)
{
    ViewportConfig src;
    src.Name = "full";
    src.ShowGrid = false;
    src.ShowGizmos = false;
    src.ShowPostProcessing = false;
    src.ShowGridAxis = false;
    src.ShowSelectionOutline = false;
    src.CurrentMode = ViewMode::Wireframe;
    src.VisibilityMask = 0x000000FFu;
    src.GizmoLocal = true;
    src.GizmoMode = 2;
    src.SnapEnabled = true;
    src.SnapValue = 0.25f;
    src.CameraFlySpeed = 12.5f;
    src.GridSize = 64.0f;
    src.GridCellSize = 2.0f;
    src.GridSubdivision = 4;
    src.Camera.FOV = 75.0f;
    src.Camera.NearClip = 0.05f;
    src.Camera.FarClip = 5000.0f;
    src.Camera.Type = ProjectionType::Orthographic;
    src.Camera.PositionX = 1.0f;
    src.Camera.PositionY = 2.0f;
    src.Camera.PositionZ = 3.0f;
    src.Camera.Pitch = -15.0f;
    src.Camera.Yaw = 45.0f;
    src.Camera.Distance = 33.0f;

    const auto c = ViewportSerializer::Deserialize(ViewportSerializer::Serialize(src));
    EXPECT_EQ(c.Name, "full");
    EXPECT_FALSE(c.ShowGrid);
    EXPECT_FALSE(c.ShowGizmos);
    EXPECT_FALSE(c.ShowPostProcessing);
    EXPECT_FALSE(c.ShowGridAxis);
    EXPECT_FALSE(c.ShowSelectionOutline);
    EXPECT_EQ(c.CurrentMode, ViewMode::Wireframe);
    EXPECT_EQ(c.VisibilityMask, 0x000000FFu);
    EXPECT_TRUE(c.GizmoLocal);
    EXPECT_EQ(c.GizmoMode, 2);
    EXPECT_TRUE(c.SnapEnabled);
    EXPECT_FLOAT_EQ(c.SnapValue, 0.25f);
    EXPECT_FLOAT_EQ(c.CameraFlySpeed, 12.5f);
    EXPECT_FLOAT_EQ(c.GridSize, 64.0f);
    EXPECT_FLOAT_EQ(c.GridCellSize, 2.0f);
    EXPECT_EQ(c.GridSubdivision, 4);
    EXPECT_FLOAT_EQ(c.Camera.FOV, 75.0f);
    EXPECT_FLOAT_EQ(c.Camera.NearClip, 0.05f);
    EXPECT_FLOAT_EQ(c.Camera.FarClip, 5000.0f);
    EXPECT_EQ(c.Camera.Type, ProjectionType::Orthographic);
    EXPECT_FLOAT_EQ(c.Camera.PositionX, 1.0f);
    EXPECT_FLOAT_EQ(c.Camera.PositionY, 2.0f);
    EXPECT_FLOAT_EQ(c.Camera.PositionZ, 3.0f);
    EXPECT_FLOAT_EQ(c.Camera.Pitch, -15.0f);
    EXPECT_FLOAT_EQ(c.Camera.Yaw, 45.0f);
    EXPECT_FLOAT_EQ(c.Camera.Distance, 33.0f);
}

TEST(ViewportSerializerTest, RoundTrip_VisibilityMaskBitPattern)
{
    ViewportConfig src;
    src.VisibilityMask = 0;
    src.SetLayerVisible(ViewportLayer::StaticGeometry, true);
    src.SetLayerVisible(ViewportLayer::CollisionDebug, true);
    const auto c = ViewportSerializer::Deserialize(ViewportSerializer::Serialize(src));
    EXPECT_TRUE(c.IsLayerVisible(ViewportLayer::StaticGeometry));
    EXPECT_TRUE(c.IsLayerVisible(ViewportLayer::CollisionDebug));
    EXPECT_FALSE(c.IsLayerVisible(ViewportLayer::Particles));
}

TEST(ViewportSerializerTest, RoundTrip_HighViewModeValue)
{
    ViewportConfig src;
    src.CurrentMode = ViewMode::CollisionDebug;  // 静态值 32
    const auto c = ViewportSerializer::Deserialize(ViewportSerializer::Serialize(src));
    EXPECT_EQ(c.CurrentMode, ViewMode::CollisionDebug);
}

TEST(ViewportSerializerTest, RoundTrip_DefaultConfig)
{
    ViewportConfig src;
    const auto c = ViewportSerializer::Deserialize(ViewportSerializer::Serialize(src));
    EXPECT_FLOAT_EQ(c.Camera.FOV, 60.0f);
    EXPECT_EQ(c.VisibilityMask, 0xFFFFFFFFu);
}

// ── 多视口布局 ─────────────────────────────────────────────────────────
TEST(ViewportSerializerTest, SerializeLayout_EmptyVectorProducesEmptyArray)
{
    const auto arr = ViewportSerializer::SerializeLayout({});
    EXPECT_TRUE(arr.is_array());
    EXPECT_EQ(arr.size(), 0u);
}

TEST(ViewportSerializerTest, SerializeLayout_PreservesCountAndOrder)
{
    std::vector<ViewportConfig> v;
    v.push_back(MakeConfig("a"));
    v.push_back(MakeConfig("b"));
    v.push_back(MakeConfig("c"));
    const auto arr = ViewportSerializer::SerializeLayout(v);
    ASSERT_EQ(arr.size(), 3u);
    EXPECT_EQ(arr[0]["Name"].get<std::string>(), "a");
    EXPECT_EQ(arr[1]["Name"].get<std::string>(), "b");
    EXPECT_EQ(arr[2]["Name"].get<std::string>(), "c");
}

TEST(ViewportSerializerTest, DeserializeLayout_NonArray_ReturnsEmpty)
{
    nlohmann::json notArray = nlohmann::json::object();
    notArray["x"] = 1;
    EXPECT_TRUE(ViewportSerializer::DeserializeLayout(notArray).empty());
}

TEST(ViewportSerializerTest, DeserializeLayout_RoundTripsAllViewportsIndependently)
{
    std::vector<ViewportConfig> v;
    ViewportConfig a = MakeConfig("alpha");
    a.GridSize = 11.0f;
    ViewportConfig b = MakeConfig("beta");
    b.GridSize = 22.0f;
    v.push_back(a);
    v.push_back(b);

    const auto out = ViewportSerializer::DeserializeLayout(ViewportSerializer::SerializeLayout(v));
    ASSERT_EQ(out.size(), 2u);
    EXPECT_EQ(out[0].Name, "alpha");
    EXPECT_FLOAT_EQ(out[0].GridSize, 11.0f);
    EXPECT_EQ(out[1].Name, "beta");
    EXPECT_FLOAT_EQ(out[1].GridSize, 22.0f);
}

TEST(ViewportSerializerTest, DeserializeLayout_EmptyArrayYieldsEmptyVector)
{
    EXPECT_TRUE(ViewportSerializer::DeserializeLayout(nlohmann::json::array()).empty());
}

// ── 预设库（显式路径）─────────────────────────────────────────────────
TEST(ViewportSerializerTest, SavePresetsToFile_WritesParseableJson)
{
    TempViewportFile f("presets_ok");
    ViewportSerializer::PresetMap p;
    p["one"] = MakeConfig("one");
    ASSERT_TRUE(ViewportSerializer::SavePresetsToFile(p, f.path()));
    std::ifstream in(f.path());
    nlohmann::json parsed;
    in >> parsed;
    ASSERT_TRUE(parsed.contains("one"));
    EXPECT_EQ(parsed["one"]["Name"].get<std::string>(), "one");
}

TEST(ViewportSerializerTest, SavePresetsToFile_UnwritablePath_ReturnsFalse)
{
    ViewportSerializer::PresetMap p;
    const std::string bad = (std::filesystem::temp_directory_path() /
                             "engine_viewport_tests" / "no_such_dir_xyz" / "p.json").string();
    std::error_code ec;
    std::filesystem::remove_all(std::filesystem::temp_directory_path() /
                                    "engine_viewport_tests" / "no_such_dir_xyz", ec);
    EXPECT_FALSE(ViewportSerializer::SavePresetsToFile(p, bad));
}

TEST(ViewportSerializerTest, LoadPresetsFromFile_MissingFile_ReturnsEmptyMap)
{
    TempViewportFile f("presets_missing");
    ASSERT_FALSE(f.exists());
    EXPECT_TRUE(ViewportSerializer::LoadPresetsFromFile(f.path()).empty());
}

TEST(ViewportSerializerTest, LoadPresetsFromFile_MalformedJson_ReturnsEmptyMap)
{
    TempViewportFile f("presets_bad");
    f.writeText("{ not json ]]");
    EXPECT_TRUE(ViewportSerializer::LoadPresetsFromFile(f.path()).empty());
}

TEST(ViewportSerializerTest, LoadPresetsFromFile_RoundTripsAllEntries)
{
    TempViewportFile f("presets_rt");
    ViewportSerializer::PresetMap src;
    ViewportConfig a = MakeConfig("level");
    a.GridSize = 33.0f;
    a.CurrentMode = ViewMode::Unlit;
    src["level"] = a;
    ViewportConfig b = MakeConfig("physics");
    b.VisibilityMask = 0x10u;
    src["physics"] = b;
    ASSERT_TRUE(ViewportSerializer::SavePresetsToFile(src, f.path()));

    const auto out = ViewportSerializer::LoadPresetsFromFile(f.path());
    ASSERT_EQ(out.size(), 2u);
    ASSERT_TRUE(out.contains("level"));
    EXPECT_EQ(out.at("level").Name, "level");
    EXPECT_FLOAT_EQ(out.at("level").GridSize, 33.0f);
    EXPECT_EQ(out.at("level").CurrentMode, ViewMode::Unlit);
    EXPECT_EQ(out.at("physics").VisibilityMask, 0x10u);
}

TEST(ViewportSerializerTest, GetDefaultPresets_ReturnsSixNamedPresets)
{
    const auto p = ViewportSerializer::GetDefaultPresets();
    EXPECT_EQ(p.size(), 6u);
    EXPECT_TRUE(p.contains("Level Design"));
    EXPECT_TRUE(p.contains("Physics Debug"));
    EXPECT_TRUE(p.contains("Lighting Only"));
    EXPECT_TRUE(p.contains("Top-Down View"));
    EXPECT_TRUE(p.contains("Full Debug"));
    EXPECT_TRUE(p.contains("Minimap"));
}

TEST(ViewportSerializerTest, GetDefaultPresets_ReturnsIndependentInstancesPerCall)
{
    auto a = ViewportSerializer::GetDefaultPresets();
    auto b = ViewportSerializer::GetDefaultPresets();
    a["Level Design"].GridSize = 999.0f;
    EXPECT_FLOAT_EQ(b.at("Level Design").GridSize, 20.0f);
}

TEST(ViewportSerializerTest, GetDefaultPresets_LevelDesign_ExpectedCameraAndToggles)
{
    const auto p = ViewportSerializer::GetDefaultPresets();
    const auto& c = p.at("Level Design");
    EXPECT_EQ(c.Name, "Level Design");
    EXPECT_TRUE(c.ShowGrid);
    EXPECT_TRUE(c.ShowGizmos);
    EXPECT_TRUE(c.ShowPostProcessing);
    EXPECT_FLOAT_EQ(c.Camera.PositionY, 10.0f);
    EXPECT_FLOAT_EQ(c.Camera.PositionZ, 15.0f);
    EXPECT_FLOAT_EQ(c.Camera.Pitch, -30.0f);
    EXPECT_FLOAT_EQ(c.Camera.Yaw, -45.0f);
}

TEST(ViewportSerializerTest, GetDefaultPresets_PhysicsDebug_TogglesAndLayerMask)
{
    const auto p = ViewportSerializer::GetDefaultPresets();
    const auto& c = p.at("Physics Debug");
    EXPECT_EQ(c.CurrentMode, ViewMode::Wireframe);
    EXPECT_TRUE(c.ShowGrid);
    EXPECT_FALSE(c.ShowGizmos);
    EXPECT_FALSE(c.ShowPostProcessing);
    EXPECT_TRUE(c.IsLayerVisible(ViewportLayer::CollisionDebug));
    EXPECT_TRUE(c.IsLayerVisible(ViewportLayer::StaticGeometry));
    EXPECT_FALSE(c.IsLayerVisible(ViewportLayer::SkeletonDebug));
    EXPECT_FALSE(c.IsLayerVisible(ViewportLayer::Particles));
}

TEST(ViewportSerializerTest, GetDefaultPresets_TopDown_HasVerticalCamera)
{
    const auto p = ViewportSerializer::GetDefaultPresets();
    const auto& c = p.at("Top-Down View");
    EXPECT_FLOAT_EQ(c.Camera.PositionY, 50.0f);
    EXPECT_FLOAT_EQ(c.Camera.PositionZ, 0.0f);
    EXPECT_FLOAT_EQ(c.Camera.Pitch, -89.0f);
    EXPECT_FLOAT_EQ(c.Camera.Yaw, 0.0f);
    EXPECT_FLOAT_EQ(c.Camera.Distance, 50.0f);
}

TEST(ViewportSerializerTest, GetDefaultPresets_Minimap_SingleLayerAndFarClip)
{
    const auto p = ViewportSerializer::GetDefaultPresets();
    const auto& c = p.at("Minimap");
    EXPECT_EQ(c.VisibilityMask, 0x00000001u);
    EXPECT_FLOAT_EQ(c.Camera.FarClip, 5000.0f);
    EXPECT_FLOAT_EQ(c.Camera.Pitch, -90.0f);
    EXPECT_FALSE(c.ShowGrid);
    EXPECT_FALSE(c.ShowSelectionOutline);
}

TEST(ViewportSerializerTest, GetDefaultPresets_LightingOnly_HasNoGrid)
{
    const auto p = ViewportSerializer::GetDefaultPresets();
    const auto& c = p.at("Lighting Only");
    EXPECT_FALSE(c.ShowGrid);
    EXPECT_FALSE(c.ShowPostProcessing);
    EXPECT_EQ(c.CurrentMode, ViewMode::LightingOnly);
    EXPECT_FLOAT_EQ(c.Camera.Pitch, -15.0f);
}

TEST(ViewportSerializerTest, GetDefaultPresets_AreThemselvesSerializable)
{
    for (const auto& [name, cfg] : ViewportSerializer::GetDefaultPresets()) {
        const auto back = ViewportSerializer::Deserialize(ViewportSerializer::Serialize(cfg));
        EXPECT_EQ(back.Name, name) << "preset " << name;
    }
}

// ── EditorSettings（纯序列化，不含硬编码路径）─────────────────────────
TEST(ViewportSerializerTest, SerializeSettings_EmitsThreeKeys)
{
    ViewportSerializer::EditorSettings s;
    s.activePreset = "Physics Debug";
    s.uiScale = 1.5f;
    const auto j = ViewportSerializer::SerializeSettings(s);
    EXPECT_EQ(j.size(), 3u);
    EXPECT_TRUE(j.contains("viewports"));
    EXPECT_TRUE(j.contains("activePreset"));
    EXPECT_TRUE(j.contains("uiScale"));
    EXPECT_EQ(j["activePreset"].get<std::string>(), "Physics Debug");
    EXPECT_FLOAT_EQ(j["uiScale"].get<float>(), 1.5f);
}

TEST(ViewportSerializerTest, DeserializeSettings_UsesDocumentedDefaults)
{
    const auto s = ViewportSerializer::DeserializeSettings(nlohmann::json::object());
    EXPECT_TRUE(s.viewports.empty());
    EXPECT_EQ(s.activePreset, "Level Design");
    EXPECT_FLOAT_EQ(s.uiScale, 1.0f);
}

TEST(ViewportSerializerTest, RoundTrip_Settings)
{
    ViewportSerializer::EditorSettings src;
    src.viewports.push_back(MakeConfig("one"));
    ViewportConfig two = MakeConfig("two");
    two.GridSize = 48.0f;
    src.viewports.push_back(two);
    src.activePreset = "Minimap";
    src.uiScale = 2.0f;

    const auto s = ViewportSerializer::DeserializeSettings(
        ViewportSerializer::SerializeSettings(src));
    ASSERT_EQ(s.viewports.size(), 2u);
    EXPECT_EQ(s.viewports[0].Name, "one");
    EXPECT_EQ(s.viewports[1].Name, "two");
    EXPECT_FLOAT_EQ(s.viewports[1].GridSize, 48.0f);
    EXPECT_EQ(s.activePreset, "Minimap");
    EXPECT_FLOAT_EQ(s.uiScale, 2.0f);
}

// ── 显式路径文件 I/O ──────────────────────────────────────────────────
TEST(ViewportSerializerTest, SaveToFile_WritesParseableJson)
{
    TempViewportFile f("vp_ok");
    ViewportConfig c = MakeConfig("saved");
    ASSERT_TRUE(ViewportSerializer::SaveToFile(c, f.path()));
    std::ifstream in(f.path());
    nlohmann::json parsed;
    in >> parsed;
    EXPECT_EQ(parsed["Name"].get<std::string>(), "saved");
}

TEST(ViewportSerializerTest, SaveToFile_UnwritablePath_ReturnsFalse)
{
    const std::string bad = (std::filesystem::temp_directory_path() /
                             "engine_viewport_tests" / "no_such_dir_xyz" / "v.json").string();
    std::error_code ec;
    std::filesystem::remove_all(std::filesystem::temp_directory_path() /
                                    "engine_viewport_tests" / "no_such_dir_xyz", ec);
    EXPECT_FALSE(ViewportSerializer::SaveToFile(ViewportConfig{}, bad));
}

TEST(ViewportSerializerTest, LoadFromFile_MissingFile_ReturnsFalse_AndLeavesConfigUnchanged)
{
    TempViewportFile f("vp_missing");
    ASSERT_FALSE(f.exists());
    ViewportConfig c = MakeConfig("untouched");
    EXPECT_FALSE(ViewportSerializer::LoadFromFile(c, f.path()));
    EXPECT_EQ(c.Name, "untouched");
}

TEST(ViewportSerializerTest, LoadFromFile_MalformedJson_ReturnsFalse)
{
    TempViewportFile f("vp_bad");
    f.writeText("{ broken ]]");
    ViewportConfig c;
    EXPECT_FALSE(ViewportSerializer::LoadFromFile(c, f.path()));
}

TEST(ViewportSerializerTest, LoadFromFile_ValidFile_ReturnsTrueAndPopulates)
{
    TempViewportFile f("vp_load");
    ViewportConfig src = MakeConfig("loaded");
    src.GridCellSize = 8.0f;
    ASSERT_TRUE(ViewportSerializer::SaveToFile(src, f.path()));

    ViewportConfig dst;
    ASSERT_TRUE(ViewportSerializer::LoadFromFile(dst, f.path()));
    EXPECT_EQ(dst.Name, "loaded");
    EXPECT_FLOAT_EQ(dst.GridCellSize, 8.0f);
}

TEST(ViewportSerializerTest, LoadFromFile_ValidTypes_StillSucceed)
{
    // 反向对照：修复后必须仍然接受合法输入
    TempViewportFile f("vp_right");
    f.writeText(R"({"Name":"ok","Camera":{"FOV":70.0},"ShowGrid":false,"GridSubdivision":3})");
    ViewportConfig c;
    ASSERT_TRUE(ViewportSerializer::LoadFromFile(c, f.path()));
    EXPECT_EQ(c.Name, "ok");
    EXPECT_FLOAT_EQ(c.Camera.FOV, 70.0f);
    EXPECT_FALSE(c.ShowGrid);
    EXPECT_EQ(c.GridSubdivision, 3);
}

// ── 标量类型校验契约 ──────────────────────────────────────────────────
//
// 三个 loader 过去只捕获 file >> root 的 parse_error，而 Deserialize 用
// json.value(key, default) 取值 —— 键存在但类型不符时抛 type_error，且
// 调用发生在 catch 之外，于是异常直接穿出 loader 抛给调用方。
// 修复后的契约与 JsonSerializer（cf1570c）一致：
//     JSON 语法错误 → false
//     标量类型错误 → false（缺失 Camera 同样 → false）
//     合法 JSON     → 正常加载
// 不做 std::exception 一网打尽，也不把"存在但类型错"的字段静默换成本地值。

TEST(ViewportSerializerTest, LoadFromFile_WrongTypedName_ReturnsFalse)
{
    TempViewportFile f("wrong_name");
    f.writeText(R"({"Name":123,"Camera":{}})");
    ViewportConfig c;
    EXPECT_FALSE(ViewportSerializer::LoadFromFile(c, f.path()));
}

TEST(ViewportSerializerTest, LoadFromFile_WrongTypedBool_ReturnsFalse)
{
    TempViewportFile f("wrong_bool");
    f.writeText(R"({"ShowGrid":"yes","Camera":{}})");
    ViewportConfig c;
    EXPECT_FALSE(ViewportSerializer::LoadFromFile(c, f.path()));
}

TEST(ViewportSerializerTest, LoadFromFile_WrongTypedCameraField_ReturnsFalse)
{
    TempViewportFile f("wrong_cam");
    f.writeText(R"({"Camera":{"FOV":"wide"}})");
    ViewportConfig c;
    EXPECT_FALSE(ViewportSerializer::LoadFromFile(c, f.path()));
}

TEST(ViewportSerializerTest, LoadFromFile_MissingCamera_ReturnsFalse)
{
    // 旧代码走 const operator[] 取缺失键，属未定义行为；
    // 契约是明确的失败，而不是编造一份默认相机配置。
    TempViewportFile f("no_camera");
    f.writeText(R"({"Name":"no_cam"})");
    ViewportConfig c = MakeConfig("untouched");
    EXPECT_FALSE(ViewportSerializer::LoadFromFile(c, f.path()));
    EXPECT_EQ(c.Name, "untouched");
}

TEST(ViewportSerializerTest, LoadFromFile_CameraWrongType_ReturnsFalse)
{
    TempViewportFile f("cam_not_object");
    f.writeText(R"({"Camera":42})");
    ViewportConfig c;
    EXPECT_FALSE(ViewportSerializer::LoadFromFile(c, f.path()));
}

TEST(ViewportSerializerTest, LoadFromFile_RootNotAnObject_ReturnsFalse)
{
    TempViewportFile f("root_array");
    f.writeText(R"([1,2,3])");
    ViewportConfig c;
    EXPECT_FALSE(ViewportSerializer::LoadFromFile(c, f.path()));
}

TEST(ViewportSerializerTest, Deserialize_MissingCamera_IsDefinedAndDoesNotCrash)
{
    // Deserialize 本身没有失败返回通道，因此缺失 Camera 时必须至少是
    // **已定义**行为（退回 CameraConfig 结构体默认值），绝不能是 UB。
    const auto c = ViewportSerializer::Deserialize(nlohmann::json::object());
    EXPECT_EQ(c.Name, "Viewport");
    EXPECT_FLOAT_EQ(c.Camera.FOV, 60.0f);
}

TEST(ViewportSerializerTest, LoadPresetsFromFile_WrongTypedEntry_IsSkippedNotFatal)
{
    // LoadPresetsFromFile 返回 PresetMap 而非 bool，没有失败通道；
    // 因此畸形条目被跳过并记日志，而不是抛异常。
    TempViewportFile f("wrong_preset");
    f.writeText(R"({"bad":{"Name":42,"Camera":{}},"good":{"Name":"ok","Camera":{}}})");
    const auto p = ViewportSerializer::LoadPresetsFromFile(f.path());
    EXPECT_EQ(p.size(), 1u);
    ASSERT_TRUE(p.contains("good"));
    EXPECT_EQ(p.at("good").Name, "ok");
}