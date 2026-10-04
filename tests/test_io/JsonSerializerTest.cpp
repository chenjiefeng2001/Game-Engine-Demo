// P1: Scene 序列化测试 —— Engine::JsonSerializer
//
// 对应 engine/include/Engine/Core/Scene/Serializer.h（128 行头 + 241 行实现），
// 此前零直接测试。它是 nlohmann/json 版的 Scene <-> JSON 双向序列化。
//
// **命名澄清（重要）**：本文件测的是 JsonSerializer，不是
// engine/include/Engine/Core/SceneSerializer.h。后者是同名的 YAML 版
// SceneSerializer，审计确认它是**孤儿声明**：
//   - SaveToFile / LoadFromFile / SerializeObject / DeserializeObject
//     全部只有声明，全仓库无任何定义
//   - 无任何 includer / 调用方
//   - 依赖 yaml-cpp，而 yaml-cpp 并未出现在任何 CMakeLists 中
// 因此它无法链接、无法测试，属于 dead header，不是本 slice 的目标。
//
// 断言集中在序列化层自身的语义：
//   - Serialize 产出的 JSON 结构（scene/name/properties/objects）
//   - SceneProperties 六个字段的完整往返
//   - GameObject：name / active / transform / children（递归）
//   - Component 往返：SpriteComponent（已注册工厂，"SpriteComponent"）
//   - LoadFromFile 的失败契约：文件不存在 / JSON 损坏
//   - Deserialize 是**追加**语义，不清空既有对象
//   - 未知组件类型被跳过但对象仍然入场景
//   - 数组字段的长度/类型防护
//
// 注意 API 实况（读源码确认，未猜测）：
//   - Serialize 只收录 comp.Serialize(json) 产出了 "type" 字段的组件
//   - 反序列化靠静态工厂表 RegisterComponentType<T>(name)；
//     全仓库仅注册了 SpriteComponent 与 PhysicsComponent
//   - DeserializeComponentForObject 失败时只记日志，
//     DeserializeGameObject 忽略其返回值并恒返回 true
//   - Deserialize 不调用 Scene::Clear()，是追加语义
//   - Transform 的 rotation 走欧拉角 <-> 四元数，故用 NEAR 而非精确相等
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>

#include <nlohmann/json.hpp>

#include "Engine/Core/Scene/Serializer.h"
#include "Engine/Core/GameObject/SpriteComponent.h"

using namespace Engine;

namespace {

class TempSceneFile {
public:
    explicit TempSceneFile(const char* tag) {
        const auto dir = std::filesystem::temp_directory_path() / "engine_jsonserializer_tests";
        std::filesystem::create_directories(dir);
        m_Path = (dir / (std::string(tag) + ".json")).string();
        std::error_code ec;
        std::filesystem::remove(m_Path, ec);
    }
    ~TempSceneFile() {
        std::error_code ec;
        std::filesystem::remove(m_Path, ec);
    }
    const std::string& path() const { return m_Path; }
    bool exists() const { return std::filesystem::exists(m_Path); }

    void writeText(const std::string& text) const {
        std::ofstream out(m_Path, std::ios::binary | std::ios::trunc);
        out << text;
    }

private:
    std::string m_Path;
};

// 造一个带 SpriteComponent 的对象
std::shared_ptr<GameObject> MakeSpriteObject(const std::string& name) {
    auto obj = std::make_shared<GameObject>(name);
    auto* sprite = obj->AddComponent<SpriteComponent>();
    sprite->SetColor(0.25f, 0.5f, 0.75f, 1.0f);
    sprite->SetSortingLayer(3);
    sprite->SetOrderInLayer(7);
    return obj;
}

} // namespace

// ── Serialize 结构 ─────────────────────────────────────────────────────
TEST(JsonSerializerTest, Serialize_EmptyScene_HasSceneNamePropertiesAndObjects)
{
    Scene scene;
    const auto j = JsonSerializer::Serialize(scene);
    ASSERT_TRUE(j.contains("scene"));
    const auto& s = j["scene"];
    ASSERT_TRUE(s.contains("name"));
    ASSERT_TRUE(s.contains("properties"));
    ASSERT_TRUE(s.contains("objects"));
    EXPECT_TRUE(s["objects"].is_array());
    EXPECT_EQ(s["objects"].size(), 0u);
}

TEST(JsonSerializerTest, Serialize_WritesSceneName)
{
    Scene scene;
    scene.SetName("level_alpha");
    const auto j = JsonSerializer::Serialize(scene);
    EXPECT_EQ(j["scene"]["name"].get<std::string>(), "level_alpha");
}

TEST(JsonSerializerTest, Serialize_WritesAllSixSceneProperties)
{
    Scene scene;
    auto& props = scene.GetProperties();
    props.ambientColor = Vec4(0.1f, 0.2f, 0.3f, 0.4f);
    props.gravity = Vec2(0.0f, -9.8f);
    props.fogDensity = 0.25f;
    props.fogColor = Vec4(0.5f, 0.6f, 0.7f, 0.8f);
    props.enableFog = true;
    props.renderingOrder = 42u;

    const auto p = JsonSerializer::Serialize(scene)["scene"]["properties"];
    EXPECT_FLOAT_EQ(p["ambientColor"][0].get<float>(), 0.1f);
    EXPECT_FLOAT_EQ(p["ambientColor"][3].get<float>(), 0.4f);
    EXPECT_FLOAT_EQ(p["gravity"][1].get<float>(), -9.8f);
    EXPECT_FLOAT_EQ(p["fogDensity"].get<float>(), 0.25f);
    EXPECT_FLOAT_EQ(p["fogColor"][2].get<float>(), 0.7f);
    EXPECT_TRUE(p["enableFog"].get<bool>());
    EXPECT_EQ(p["renderingOrder"].get<uint32>(), 42u);
}

TEST(JsonSerializerTest, Serialize_WritesNameActiveAndTransformPerObject)
{
    Scene scene;
    auto obj = std::make_shared<GameObject>("hero");
    obj->SetActive(false);
    obj->GetTransform().SetPosition(1.0f, 2.0f, 3.0f);
    obj->GetTransform().SetScale(2.0f);
    scene.AddObject(std::move(obj));

    const auto o = JsonSerializer::Serialize(scene)["scene"]["objects"][0];
    EXPECT_EQ(o["name"].get<std::string>(), "hero");
    EXPECT_FALSE(o["active"].get<bool>());
    EXPECT_FLOAT_EQ(o["transform"]["position"][0].get<float>(), 1.0f);
    EXPECT_FLOAT_EQ(o["transform"]["position"][2].get<float>(), 3.0f);
    EXPECT_FLOAT_EQ(o["transform"]["scale"][1].get<float>(), 2.0f);
}

TEST(JsonSerializerTest, Serialize_OmitsChildrenKey_WhenObjectHasNoChildren)
{
    Scene scene;
    scene.AddObject(std::make_shared<GameObject>("lonely"));
    const auto o = JsonSerializer::Serialize(scene)["scene"]["objects"][0];
    EXPECT_FALSE(o.contains("children"));
}

TEST(JsonSerializerTest, Serialize_EmitsChildrenKey_AndNestsRecursively)
{
    Scene scene;
    auto root = std::make_shared<GameObject>("root");
    auto mid = std::make_shared<GameObject>("mid");
    auto leaf = std::make_shared<GameObject>("leaf");
    mid->AddChild(std::move(leaf));
    root->AddChild(std::move(mid));
    scene.AddObject(std::move(root));

    const auto r = JsonSerializer::Serialize(scene)["scene"]["objects"][0];
    ASSERT_TRUE(r.contains("children"));
    ASSERT_EQ(r["children"].size(), 1u);
    EXPECT_EQ(r["children"][0]["name"].get<std::string>(), "mid");
    ASSERT_TRUE(r["children"][0].contains("children"));
    EXPECT_EQ(r["children"][0]["children"][0]["name"].get<std::string>(), "leaf");
    // 叶子自身没有 children 键
    EXPECT_FALSE(r["children"][0]["children"][0].contains("children"));
}

TEST(JsonSerializerTest, Serialize_RegisteredComponent_IsEmittedWithTypeField)
{
    Scene scene;
    scene.AddObject(MakeSpriteObject("spr"));
    const auto comps = JsonSerializer::Serialize(scene)["scene"]["objects"][0]["components"];
    ASSERT_TRUE(comps.is_array());
    ASSERT_EQ(comps.size(), 1u);
    EXPECT_EQ(comps[0]["type"].get<std::string>(), "SpriteComponent");
    EXPECT_FLOAT_EQ(comps[0]["data"]["color"][0].get<float>(), 0.25f);
    EXPECT_EQ(comps[0]["data"]["sortingLayer"].get<int32>(), 3);
    EXPECT_EQ(comps[0]["data"]["orderInLayer"].get<int32>(), 7);
}

TEST(JsonSerializerTest, Serialize_ObjectWithoutComponents_EmitsEmptyArray)
{
    Scene scene;
    scene.AddObject(std::make_shared<GameObject>("bare"));
    const auto o = JsonSerializer::Serialize(scene)["scene"]["objects"][0];
    ASSERT_TRUE(o.contains("components"));
    EXPECT_TRUE(o["components"].is_array());
    EXPECT_EQ(o["components"].size(), 0u);
}

// ── SaveToFile / LoadFromFile 契约 ─────────────────────────────────────
TEST(JsonSerializerTest, SaveToFile_WritesParseableJson)
{
    TempSceneFile f("save_ok");
    Scene scene;
    scene.SetName("saved_scene");
    ASSERT_TRUE(JsonSerializer::SaveToFile(scene, f.path()));
    ASSERT_TRUE(f.exists());

    std::ifstream in(f.path());
    nlohmann::json parsed;
    in >> parsed;
    EXPECT_EQ(parsed["scene"]["name"].get<std::string>(), "saved_scene");
}

TEST(JsonSerializerTest, SaveToFile_UnwritablePath_ReturnsFalse)
{
    Scene scene;
    const std::string bad =
        (std::filesystem::temp_directory_path() / "engine_jsonserializer_tests" /
         "no_such_dir_xyz" / "s.json").string();
    std::error_code ec;
    std::filesystem::remove_all(std::filesystem::temp_directory_path() /
                                    "engine_jsonserializer_tests" / "no_such_dir_xyz", ec);
    EXPECT_FALSE(JsonSerializer::SaveToFile(scene, bad));
}

TEST(JsonSerializerTest, LoadFromFile_MissingFile_ReturnsFalse)
{
    TempSceneFile f("missing");
    ASSERT_FALSE(f.exists());
    Scene scene;
    EXPECT_FALSE(JsonSerializer::LoadFromFile(scene, f.path()));
}

TEST(JsonSerializerTest, LoadFromFile_MalformedJson_ReturnsFalse)
{
    TempSceneFile f("bad_json");
    f.writeText("{ not valid json ]]");
    Scene scene;
    EXPECT_FALSE(JsonSerializer::LoadFromFile(scene, f.path()));
}

TEST(JsonSerializerTest, LoadFromFile_ValidFile_ReturnsTrueAndPopulates)
{
    TempSceneFile f("load_ok");
    Scene src;
    src.SetName("round_trip");
    src.AddObject(std::make_shared<GameObject>("only"));
    ASSERT_TRUE(JsonSerializer::SaveToFile(src, f.path()));

    Scene dst;
    ASSERT_TRUE(JsonSerializer::LoadFromFile(dst, f.path()));
    EXPECT_EQ(dst.GetName(), "round_trip");
    ASSERT_EQ(dst.GetObjects().size(), 1u);
    EXPECT_EQ(dst.GetObjects()[0]->GetName(), "only");
}

// ── 往返 ───────────────────────────────────────────────────────────────
TEST(JsonSerializerTest, RoundTrip_SceneProperties_ArePreserved)
{
    Scene src;
    auto& p = src.GetProperties();
    p.ambientColor = Vec4(0.1f, 0.2f, 0.3f, 0.4f);
    p.gravity = Vec2(1.5f, -9.8f);
    p.fogDensity = 0.125f;
    p.fogColor = Vec4(0.9f, 0.8f, 0.7f, 0.6f);
    p.enableFog = true;
    p.renderingOrder = 7u;

    const auto j = JsonSerializer::Serialize(src);
    Scene dst;
    ASSERT_TRUE(JsonSerializer::Deserialize(dst, j));
    const auto& q = dst.GetProperties();
    EXPECT_FLOAT_EQ(q.ambientColor.x, 0.1f);
    EXPECT_FLOAT_EQ(q.ambientColor.w, 0.4f);
    EXPECT_FLOAT_EQ(q.gravity.x, 1.5f);
    EXPECT_FLOAT_EQ(q.gravity.y, -9.8f);
    EXPECT_FLOAT_EQ(q.fogDensity, 0.125f);
    EXPECT_FLOAT_EQ(q.fogColor.x, 0.9f);
    EXPECT_TRUE(q.enableFog);
    EXPECT_EQ(q.renderingOrder, 7u);
}

TEST(JsonSerializerTest, RoundTrip_ObjectNameAndActiveFlag)
{
    Scene src;
    auto obj = std::make_shared<GameObject>("inactive_one");
    obj->SetActive(false);
    src.AddObject(std::move(obj));

    Scene dst;
    ASSERT_TRUE(JsonSerializer::Deserialize(dst, JsonSerializer::Serialize(src)));
    ASSERT_EQ(dst.GetObjects().size(), 1u);
    EXPECT_EQ(dst.GetObjects()[0]->GetName(), "inactive_one");
    EXPECT_FALSE(dst.GetObjects()[0]->IsActive());
}

TEST(JsonSerializerTest, RoundTrip_TransformPositionAndScale)
{
    Scene src;
    auto obj = std::make_shared<GameObject>("mover");
    obj->GetTransform().SetPosition(4.0f, 5.0f, 6.0f);
    obj->GetTransform().SetScale(Vec3(3.0f, 3.0f, 3.0f));
    src.AddObject(std::move(obj));

    Scene dst;
    ASSERT_TRUE(JsonSerializer::Deserialize(dst, JsonSerializer::Serialize(src)));
    ASSERT_EQ(dst.GetObjects().size(), 1u);
    const auto& t = dst.GetObjects()[0]->GetTransform();
    EXPECT_FLOAT_EQ(t.GetPosition().x, 4.0f);
    EXPECT_FLOAT_EQ(t.GetPosition().y, 5.0f);
    EXPECT_FLOAT_EQ(t.GetPosition().z, 6.0f);
    EXPECT_FLOAT_EQ(t.GetScale().y, 3.0f);
}

TEST(JsonSerializerTest, RoundTrip_TransformRotation_SurvivesEulerQuatConversion)
{
    Scene src;
    auto obj = std::make_shared<GameObject>("spinner");
    obj->GetTransform().SetRotation(10.0f, 20.0f, 30.0f);
    src.AddObject(std::move(obj));

    Scene dst;
    ASSERT_TRUE(JsonSerializer::Deserialize(dst, JsonSerializer::Serialize(src)));
    ASSERT_EQ(dst.GetObjects().size(), 1u);
    const Vec3 r = dst.GetObjects()[0]->GetTransform().GetRotation();
    // rotation 经欧拉角 <-> 四元数转换，不做逐位相等
    EXPECT_NEAR(r.x, 10.0f, 1e-3f);
    EXPECT_NEAR(r.y, 20.0f, 1e-3f);
    EXPECT_NEAR(r.z, 30.0f, 1e-3f);
}

TEST(JsonSerializerTest, RoundTrip_HierarchyIsPreservedRecursively)
{
    Scene src;
    auto root = std::make_shared<GameObject>("root");
    auto child = std::make_shared<GameObject>("child");
    auto grand = std::make_shared<GameObject>("grand");
    child->AddChild(std::move(grand));
    root->AddChild(std::move(child));
    src.AddObject(std::move(root));

    Scene dst;
    ASSERT_TRUE(JsonSerializer::Deserialize(dst, JsonSerializer::Serialize(src)));
    ASSERT_EQ(dst.GetObjects().size(), 1u);
    const auto& r = dst.GetObjects()[0];
    ASSERT_EQ(r->GetChildren().size(), 1u);
    EXPECT_EQ(r->GetChildren()[0]->GetName(), "child");
    ASSERT_EQ(r->GetChildren()[0]->GetChildren().size(), 1u);
    EXPECT_EQ(r->GetChildren()[0]->GetChildren()[0]->GetName(), "grand");
}

TEST(JsonSerializerTest, RoundTrip_SpriteComponent_ColorAndSortingRestored)
{
    Scene src;
    src.AddObject(MakeSpriteObject("spr"));

    Scene dst;
    ASSERT_TRUE(JsonSerializer::Deserialize(dst, JsonSerializer::Serialize(src)));
    ASSERT_EQ(dst.GetObjects().size(), 1u);
    const auto* spr = dst.GetObjects()[0]->GetComponent<SpriteComponent>();
    ASSERT_NE(spr, nullptr);
    EXPECT_FLOAT_EQ(spr->GetColor().x, 0.25f);
    EXPECT_FLOAT_EQ(spr->GetColor().y, 0.5f);
    EXPECT_FLOAT_EQ(spr->GetColor().z, 0.75f);
    EXPECT_EQ(spr->GetSortingLayer(), 3);
    EXPECT_EQ(spr->GetOrderInLayer(), 7);
}

TEST(JsonSerializerTest, RoundTrip_EmptyScene_ProducesNoObjects)
{
    Scene src;
    Scene dst;
    ASSERT_TRUE(JsonSerializer::Deserialize(dst, JsonSerializer::Serialize(src)));
    EXPECT_EQ(dst.GetObjects().size(), 0u);
}

// ── Deserialize 边界与失败契约 ─────────────────────────────────────────
TEST(JsonSerializerTest, Deserialize_MissingSceneRoot_ReturnsFalse)
{
    Scene dst;
    const nlohmann::json j = {{"notScene", nlohmann::json::object()}};
    EXPECT_FALSE(JsonSerializer::Deserialize(dst, j));
}

TEST(JsonSerializerTest, Deserialize_EmptySceneNode_ReturnsTrue_AndAddsNothing)
{
    Scene dst;
    const nlohmann::json j = {{"scene", nlohmann::json::object()}};
    EXPECT_TRUE(JsonSerializer::Deserialize(dst, j));
    EXPECT_EQ(dst.GetObjects().size(), 0u);
}

TEST(JsonSerializerTest, Deserialize_IsAdditive_DoesNotClearExistingObjects)
{
    Scene dst;
    dst.AddObject(std::make_shared<GameObject>("preexisting"));

    Scene src;
    src.SetName("incoming");
    src.AddObject(std::make_shared<GameObject>("newcomer"));
    ASSERT_TRUE(JsonSerializer::Deserialize(dst, JsonSerializer::Serialize(src)));

    // Deserialize 不调用 Clear()：既有对象保留，新对象追加
    EXPECT_EQ(dst.GetObjects().size(), 2u);
    EXPECT_EQ(dst.GetObjects()[0]->GetName(), "preexisting");
    EXPECT_EQ(dst.GetObjects()[1]->GetName(), "newcomer");
    EXPECT_EQ(dst.GetName(), "incoming");
}

TEST(JsonSerializerTest, Deserialize_LoadingTwice_DuplicatesObjects)
{
    Scene src;
    src.AddObject(std::make_shared<GameObject>("dup"));

    Scene dst;
    const auto j = JsonSerializer::Serialize(src);
    ASSERT_TRUE(JsonSerializer::Deserialize(dst, j));
    ASSERT_EQ(dst.GetObjects().size(), 1u);
    ASSERT_TRUE(JsonSerializer::Deserialize(dst, j));
    EXPECT_EQ(dst.GetObjects().size(), 2u);
}

TEST(JsonSerializerTest, Deserialize_UnknownComponentType_IsSkippedButObjectStillAdded)
{
    const nlohmann::json j = {
        {"scene", {{"name", "x"},
                   {"objects", nlohmann::json::array(
                       {{{"name", "victim"},
                         {"components", nlohmann::json::array(
                             {{{"type", "NoSuchComponentAnywhere"},
                               {"data", nlohmann::json::object()}}})}}})}}}};
    Scene dst;
    EXPECT_TRUE(JsonSerializer::Deserialize(dst, j));
    ASSERT_EQ(dst.GetObjects().size(), 1u);
    EXPECT_EQ(dst.GetObjects()[0]->GetName(), "victim");
}

TEST(JsonSerializerTest, Deserialize_ComponentWithoutDataField_StillAttachesComponent)
{
    const nlohmann::json j = {
        {"scene", {{"objects", nlohmann::json::array(
                       {{{"name", "bare_sprite"},
                         {"components", nlohmann::json::array(
                             {{{"type", "SpriteComponent"}}})}}})}}}};
    Scene dst;
    ASSERT_TRUE(JsonSerializer::Deserialize(dst, j));
    ASSERT_EQ(dst.GetObjects().size(), 1u);
    // SpriteComponent::Deserialize 在缺 "data" 时直接返回 true
    EXPECT_NE(dst.GetObjects()[0]->GetComponent<SpriteComponent>(), nullptr);
}

TEST(JsonSerializerTest, Deserialize_ShortArrayFields_AreIgnored)
{
    Scene dst;
    dst.GetProperties().ambientColor = Vec4(0.9f, 0.9f, 0.9f, 0.9f);
    dst.GetProperties().gravity = Vec2(1.0f, 2.0f);

    // ambientColor 只有 2 个元素、gravity 只有 1 个 → 均不满足 size 门槛
    const nlohmann::json j = {
        {"scene", {{"properties", {{"ambientColor", {1.0, 2.0}},
                                   {"gravity", {5.0}}}}}}};
    ASSERT_TRUE(JsonSerializer::Deserialize(dst, j));
    EXPECT_FLOAT_EQ(dst.GetProperties().ambientColor.x, 0.9f);
    EXPECT_FLOAT_EQ(dst.GetProperties().gravity.x, 1.0f);
}

TEST(JsonSerializerTest, Deserialize_WrongTypedVectorFields_AreIgnored)
{
    Scene dst;
    dst.GetProperties().fogDensity = 0.75f;
    dst.GetProperties().ambientColor = Vec4(0.4f, 0.4f, 0.4f, 0.4f);
    dst.GetProperties().gravity = Vec2(3.0f, 4.0f);

    // 向量字段有 is_array() + size() 双重防护 → 类型不符时静默忽略
    nlohmann::json j;
    j["scene"]["properties"]["ambientColor"] = "not_an_array";
    j["scene"]["properties"]["gravity"] = 7;  // 数字而非数组
    j["scene"]["properties"]["fogColor"] = "nope";
    ASSERT_TRUE(JsonSerializer::Deserialize(dst, j));
    EXPECT_FLOAT_EQ(dst.GetProperties().ambientColor.x, 0.4f);
    EXPECT_FLOAT_EQ(dst.GetProperties().gravity.x, 3.0f);
    EXPECT_FLOAT_EQ(dst.GetProperties().fogDensity, 0.75f);
}

// 标量字段类型校验契约
//
// 背景：标量字段（fogDensity / enableFog / renderingOrder / name / active）
// 此前**没有**类型校验，值类型不符会抛出 nlohmann::json::type_error；而
// LoadFromFile 只捕获 parse_error，导致**语法合法**的坏文件把未捕获异常
// 抛给调用方。修复后的契约是：
//     JSON 语法错误 → false
//     标量类型错误 → false
//     合法 JSON     → 正常加载
// 刻意不把类型错误静默转成默认值并返回 true —— 那会把坏输入伪装成成功。
// 向量字段（ambientColor / gravity / fogColor）维持既有行为：类型或长度
// 不符时静默忽略；组件解析失败也仍是 best-effort（见上面的未知组件用例）。
TEST(JsonSerializerTest, Deserialize_WrongTypedFogDensity_ReturnsFalse)
{
    Scene dst;
    nlohmann::json j;
    j["scene"]["properties"]["fogDensity"] = "not_a_number";
    EXPECT_FALSE(JsonSerializer::Deserialize(dst, j));
}

TEST(JsonSerializerTest, Deserialize_WrongTypedRenderingOrderAndEnableFog_ReturnFalse)
{
    Scene dst;
    nlohmann::json j1;
    j1["scene"]["properties"]["renderingOrder"] = "not_a_uint";
    EXPECT_FALSE(JsonSerializer::Deserialize(dst, j1));

    Scene dst2;
    nlohmann::json j2;
    j2["scene"]["properties"]["enableFog"] = "not_a_bool";
    EXPECT_FALSE(JsonSerializer::Deserialize(dst2, j2));
}

TEST(JsonSerializerTest, Deserialize_WrongTypedNameAndActive_ReturnFalse)
{
    Scene dst;
    nlohmann::json j1;
    j1["scene"]["name"] = 12345;  // 期望 string
    EXPECT_FALSE(JsonSerializer::Deserialize(dst, j1));

    Scene dst2;
    nlohmann::json j2;
    nlohmann::json obj;
    obj["name"] = "victim";
    obj["active"] = "yes_please";  // 期望 bool
    j2["scene"]["objects"] = nlohmann::json::array({obj});
    EXPECT_FALSE(JsonSerializer::Deserialize(dst2, j2));
}

TEST(JsonSerializerTest, Deserialize_WrongTypedChildName_ReturnFalse)
{
    // 子对象同样受校验：畸形 child 会让整体失败，而不是被静默丢弃
    nlohmann::json j;
    nlohmann::json child;
    child["name"] = 99;
    nlohmann::json root;
    root["name"] = "root";
    root["children"] = nlohmann::json::array({child});
    j["scene"]["objects"] = nlohmann::json::array({root});

    Scene dst;
    EXPECT_FALSE(JsonSerializer::Deserialize(dst, j));
}

TEST(JsonSerializerTest, LoadFromFile_ValidJsonWithWrongTypedField_ReturnsFalse)
{
    // 端到端：语法合法但标量类型不符 → 返回 false，而非未捕获异常
    TempSceneFile f("wrong_type");
    f.writeText(R"({"scene":{"properties":{"fogDensity":"not_a_number"}}})");
    Scene dst;
    EXPECT_FALSE(JsonSerializer::LoadFromFile(dst, f.path()));
}

TEST(JsonSerializerTest, LoadFromFile_WrongTypedObjectField_ReturnsFalse)
{
    TempSceneFile f("wrong_type_obj");
    f.writeText(R"({"scene":{"objects":[{"name":"a","active":"nope"}]}})");
    Scene dst;
    EXPECT_FALSE(JsonSerializer::LoadFromFile(dst, f.path()));
}

TEST(JsonSerializerTest, LoadFromFile_ValidScalarTypes_StillLoadSuccessfully)
{
    // 反向对照：类型正确时必须照常成功，避免修复把合法输入也拒掉
    TempSceneFile f("right_type");
    f.writeText(R"({"scene":{"name":"ok","properties":{"fogDensity":0.5,"enableFog":true,"renderingOrder":3}}})");
    Scene dst;
    ASSERT_TRUE(JsonSerializer::LoadFromFile(dst, f.path()));
    EXPECT_EQ(dst.GetName(), "ok");
    EXPECT_FLOAT_EQ(dst.GetProperties().fogDensity, 0.5f);
    EXPECT_TRUE(dst.GetProperties().enableFog);
    EXPECT_EQ(dst.GetProperties().renderingOrder, 3u);
}

TEST(JsonSerializerTest, LoadFromFile_MalformedJson_StillReturnsFalse)
{
    // 反向对照：语法错误行为不变（由 parse_error 分支处理）
    TempSceneFile f("still_bad");
    f.writeText("{ this is not valid json ]]");
    Scene dst;
    EXPECT_FALSE(JsonSerializer::LoadFromFile(dst, f.path()));
}

TEST(JsonSerializerTest, Deserialize_WrongTypedVectorField_KeepsExistingIgnoreBehaviour)
{
    // 向量字段维持既有行为：静默忽略并视为成功（与标量字段不同）
    Scene dst;
    dst.GetProperties().ambientColor = Vec4(0.4f, 0.4f, 0.4f, 0.4f);
    nlohmann::json j;
    j["scene"]["properties"]["ambientColor"] = "not_an_array";
    EXPECT_TRUE(JsonSerializer::Deserialize(dst, j));
    EXPECT_FLOAT_EQ(dst.GetProperties().ambientColor.x, 0.4f);
}

TEST(JsonSerializerTest, Deserialize_ObjectsNotAnArray_IsIgnored)
{
    const nlohmann::json j = {
        {"scene", {{"name", "weird"}, {"objects", "not_an_array"}}}};
    Scene dst;
    EXPECT_TRUE(JsonSerializer::Deserialize(dst, j));
    EXPECT_EQ(dst.GetName(), "weird");
    EXPECT_EQ(dst.GetObjects().size(), 0u);
}

TEST(JsonSerializerTest, Deserialize_NameAbsent_LeavesSceneNameUntouched)
{
    Scene dst;
    dst.SetName("original");
    const nlohmann::json j = {{"scene", {{"objects", nlohmann::json::array()}}}};
    ASSERT_TRUE(JsonSerializer::Deserialize(dst, j));
    EXPECT_EQ(dst.GetName(), "original");
}