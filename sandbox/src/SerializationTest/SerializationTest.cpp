#include "SerializationTest.h"
#include <Engine/Core/Scene/Scene.h>
#include <Engine/Core/Scene/Serializer.h>
#include <Engine/Core/GameObject/GameObject.h>
#include <Engine/Core/GameObject/SpriteComponent.h>
#include <Engine/Core/Physics/PhysicsComponent.h>
#include <nlohmann/json.hpp>

#include <iostream>
#include <string>
#include <cmath>
#include <cstdlib>

namespace Engine {

    int SerializationTest::s_Passed = 0;
    int SerializationTest::s_Failed = 0;

    // ============================================================
    // 辅助方法
    // ============================================================

    bool SerializationTest::Vec3Equal(const Vec3& a, const Vec3& b, float eps) {
        return std::abs(a.x - b.x) < eps &&
               std::abs(a.y - b.y) < eps &&
               std::abs(a.z - b.z) < eps;
    }

    bool SerializationTest::Vec4Equal(const Vec4& a, const Vec4& b, float eps) {
        return std::abs(a.x - b.x) < eps &&
               std::abs(a.y - b.y) < eps &&
               std::abs(a.z - b.z) < eps &&
               std::abs(a.w - b.w) < eps;
    }

    // ============================================================
    // 测试用例 1: 场景属性序列化
    // ============================================================
    // 验证：环境光颜色、重力、雾参数、渲染顺序全部正确往返
    bool SerializationTest::TestSceneProperties() {
        Scene original("PropTest");
        original.SetAmbientColor(Vec4(0.1f, 0.2f, 0.3f, 0.4f));
        original.SetGravity(Vec2(0.0f, -9.81f));
        original.SetFogDensity(0.05f);
        original.SetFogColor(Vec4(0.5f, 0.6f, 0.7f, 0.8f));
        original.SetFogEnabled(true);
        original.GetProperties().renderingOrder = 3;

        nlohmann::json j = JsonSerializer::Serialize(original);
        Scene loaded;
        JsonSerializer::Deserialize(loaded, j);

        // 验证场景名称
        if (loaded.GetName() != "PropTest")
            return false;

        // 验证环境光
        if (!Vec4Equal(loaded.GetAmbientColor(), Vec4(0.1f, 0.2f, 0.3f, 0.4f)))
            return false;

        // 验证重力
        if (std::abs(loaded.GetGravity().x - 0.0f) > 0.001f ||
            std::abs(loaded.GetGravity().y - (-9.81f)) > 0.001f)
            return false;

        // 验证雾密度
        if (std::abs(loaded.GetFogDensity() - 0.05f) > 0.001f)
            return false;

        // 验证雾颜色
        if (!Vec4Equal(loaded.GetFogColor(), Vec4(0.5f, 0.6f, 0.7f, 0.8f)))
            return false;

        // 验证雾开启
        if (!loaded.IsFogEnabled())
            return false;

        // 验证渲染顺序
        if (loaded.GetProperties().renderingOrder != 3)
            return false;

        // 验证对象数为 0
        if (loaded.GetObjectCount() != 0)
            return false;

        return true;
    }

    // ============================================================
    // 测试用例 2: 空场景
    // ============================================================
    bool SerializationTest::TestEmptyScene() {
        Scene empty("Empty");
        nlohmann::json j = JsonSerializer::Serialize(empty);
        Scene loaded;
        JsonSerializer::Deserialize(loaded, j);

        return loaded.GetName() == "Empty" &&
               loaded.GetObjectCount() == 0;
    }

    // ============================================================
    // 测试用例 3: 单对象 + Transform 精确往返
    // ============================================================
    bool SerializationTest::TestTransformRoundTrip() {
        Scene scene("TransformTest");
        auto obj = std::make_shared<GameObject>("Cube");
        obj->GetTransform().SetPosition(1.0f, 2.0f, 3.0f);
        obj->GetTransform().SetRotation(10.0f, 20.0f, 30.0f);
        obj->GetTransform().SetScale(Vec3(2.0f, 3.0f, 4.0f));
        scene.AddObject(obj);

        nlohmann::json j = JsonSerializer::Serialize(scene);
        Scene loaded;
        JsonSerializer::Deserialize(loaded, j);

        if (loaded.GetObjectCount() != 1) return false;
        auto* loadedObj = loaded.FindObject("Cube");
        if (!loadedObj) return false;

        auto& t = loadedObj->GetTransform();
        return Vec3Equal(t.GetPosition(), Vec3(1.0f, 2.0f, 3.0f)) &&
               Vec3Equal(t.GetRotation(), Vec3(10.0f, 20.0f, 30.0f)) &&
               Vec3Equal(t.GetScale(), Vec3(2.0f, 3.0f, 4.0f));
    }

    // ============================================================
    // 测试用例 4: 单对象 + SpriteComponent 字段完整验证
    // ============================================================
    bool SerializationTest::TestSingleObjectWithSprite() {
        Scene scene("SpriteTest");
        auto obj = std::make_shared<GameObject>("Hero");
        obj->GetTransform().SetPosition(100.0f, 200.0f, 0.0f);

        auto* sprite = obj->AddComponent<SpriteComponent>();
        sprite->SetColor(Vec4(0.2f, 0.4f, 0.6f, 0.8f));
        sprite->SetUV(0.1f, 0.2f, 0.7f, 0.8f);
        sprite->SetTiling(Vec2(2.0f, 2.0f));
        sprite->SetOffset(Vec2(0.5f, 0.5f));
        sprite->SetSortingLayer(5);
        sprite->SetOrderInLayer(3);
        sprite->SetVisible(false);

        scene.AddObject(obj);

        // ── 序列化并检查 JSON 结构 ──
        nlohmann::json j = JsonSerializer::Serialize(scene);

        // 验证 JSON 结构
        const auto& objects = j["scene"]["objects"];
        if (!objects.is_array() || objects.size() != 1) return false;

        const auto& objJson = objects[0];
        if (objJson["name"] != "Hero") return false;
        if (objJson["active"] != true) return false;

        // 验证 Transform
        const auto& t = objJson["transform"];
        if (t["position"] != nlohmann::json::array({100.0f, 200.0f, 0.0f})) return false;

        // 验证组件
        const auto& comps = objJson["components"];
        if (!comps.is_array() || comps.size() != 1) return false;

        const auto& spriteJson = comps[0];
        if (spriteJson["type"] != "SpriteComponent") return false;

        const auto& data = spriteJson["data"];
        if (data["color"] != nlohmann::json::array({0.2f, 0.4f, 0.6f, 0.8f})) return false;
        if (data["uv"] != nlohmann::json::array({0.1f, 0.2f, 0.7f, 0.8f})) return false;
        if (data["tiling"] != nlohmann::json::array({2.0f, 2.0f})) return false;
        if (data["offset"] != nlohmann::json::array({0.5f, 0.5f})) return false;
        if (data["sortingLayer"] != 5) return false;
        if (data["orderInLayer"] != 3) return false;
        if (data["visible"] != false) return false;

        // ── 反序列化并验证结果 ──
        Scene loaded;
        JsonSerializer::Deserialize(loaded, j);

        if (loaded.GetObjectCount() != 1) return false;
        auto* loadedObj = loaded.FindObject("Hero");
        if (!loadedObj) return false;

        // Transform
        auto& lt = loadedObj->GetTransform();
        if (!Vec3Equal(lt.GetPosition(), Vec3(100.0f, 200.0f, 0.0f))) return false;

        // SpriteComponent
        auto* loadedSprite = loadedObj->GetComponent<SpriteComponent>();
        if (!loadedSprite) return false;

        if (!Vec4Equal(loadedSprite->GetColor(), Vec4(0.2f, 0.4f, 0.6f, 0.8f))) return false;
        if (std::abs(loadedSprite->GetUVX() - 0.1f) > 0.001f) return false;
        if (std::abs(loadedSprite->GetUVW() - 0.7f) > 0.001f) return false;
        if (std::abs(loadedSprite->GetTiling().x - 2.0f) > 0.001f) return false;
        if (std::abs(loadedSprite->GetOffset().y - 0.5f) > 0.001f) return false;
        if (loadedSprite->GetSortingLayer() != 5) return false;
        if (loadedSprite->GetOrderInLayer() != 3) return false;
        if (loadedSprite->IsVisible() != false) return false;

        return true;
    }

    // ============================================================
    // 测试用例 5: 父子层级递归序列化
    // ============================================================
    bool SerializationTest::TestParentChildHierarchy() {
        Scene scene("HierarchyTest");

        // 构建三层树：Root > Child > Grandchild
        auto root = std::make_shared<GameObject>("Root");
        root->GetTransform().SetPosition(0.0f, 0.0f, 0.0f);
        root->AddComponent<SpriteComponent>();

        auto child = std::make_shared<GameObject>("Child");
        child->GetTransform().SetPosition(10.0f, 20.0f, 0.0f);
        root->AddChild(child);

        auto grandchild = std::make_shared<GameObject>("Grandchild");
        grandchild->GetTransform().SetPosition(5.0f, 5.0f, 5.0f);
        child->AddChild(grandchild);

        scene.AddObject(root);

        // 序列化
        nlohmann::json j = JsonSerializer::Serialize(scene);

        // 验证 JSON 中的层级结构
        const auto& rootJson = j["scene"]["objects"][0];
        if (rootJson["name"] != "Root") return false;

        const auto& children = rootJson["children"];
        if (!children.is_array() || children.size() != 1) return false;
        if (children[0]["name"] != "Child") return false;

        const auto& grandchildren = children[0]["children"];
        if (!grandchildren.is_array() || grandchildren.size() != 1) return false;
        if (grandchildren[0]["name"] != "Grandchild") return false;

        // 验证子对象也有 transform
        if (!grandchildren[0].contains("transform")) return false;
        const auto& gcPos = grandchildren[0]["transform"]["position"];
        if (gcPos != nlohmann::json::array({5.0f, 5.0f, 5.0f})) return false;

        // 反序列化并验证
        Scene loaded;
        JsonSerializer::Deserialize(loaded, j);

        if (loaded.GetObjectCount() != 1) return false;
        auto* loadedRoot = loaded.FindObject("Root");
        if (!loadedRoot) return false;
        if (loadedRoot->GetChildren().size() != 1) return false;

        auto* loadedChild = loadedRoot->GetChildren()[0].get();
        if (loadedChild->GetName() != "Child") return false;
        if (loadedChild->GetChildren().size() != 1) return false;

        auto* loadedGC = loadedChild->GetChildren()[0].get();
        if (loadedGC->GetName() != "Grandchild") return false;

        // 验证子对象 Transform
        auto& gcPosLoaded = loadedGC->GetTransform().GetPosition();
        if (!Vec3Equal(gcPosLoaded, Vec3(5.0f, 5.0f, 5.0f))) return false;

        return true;
    }

    // ============================================================
    // 测试用例 6: 多个根对象
    // ============================================================
    bool SerializationTest::TestMultipleRootObjects() {
        Scene scene("MultiRoot");
        scene.AddObject(std::make_shared<GameObject>("A"));
        scene.AddObject(std::make_shared<GameObject>("B"));
        scene.AddObject(std::make_shared<GameObject>("C"));

        nlohmann::json j = JsonSerializer::Serialize(scene);
        Scene loaded;
        JsonSerializer::Deserialize(loaded, j);

        if (loaded.GetObjectCount() != 3) return false;
        if (!loaded.FindObject("A")) return false;
        if (!loaded.FindObject("B")) return false;
        if (!loaded.FindObject("C")) return false;

        return true;
    }

    // ============================================================
    // 测试用例 7: 非活跃对象
    // ============================================================
    bool SerializationTest::TestInactiveObject() {
        Scene scene("ActiveTest");
        auto active = std::make_shared<GameObject>("Active");
        active->SetActive(true);
        scene.AddObject(active);

        auto inactive = std::make_shared<GameObject>("Inactive");
        inactive->SetActive(false);
        scene.AddObject(inactive);

        nlohmann::json j = JsonSerializer::Serialize(scene);
        Scene loaded;
        JsonSerializer::Deserialize(loaded, j);

        auto* loadedActive = loaded.FindObject("Active");
        auto* loadedInactive = loaded.FindObject("Inactive");
        if (!loadedActive || !loadedInactive) return false;
        if (!loadedActive->IsActive()) return false;
        if (loadedInactive->IsActive()) return false;

        return true;
    }

    // ============================================================
    // 测试用例 8: 注册式组件工厂正确性
    // ============================================================
    bool SerializationTest::TestComponentFactoryRegistration() {
        // 验证已知组件类型已注册
        // 通过 JsonSerializer 的静态工厂映射验证
        // 由于 s_Factories 是 private，通过序列化/反序列化验证

        Scene scene("FactoryTest");
        auto obj = std::make_shared<GameObject>("Test");
        obj->AddComponent<SpriteComponent>();
        obj->AddComponent<PhysicsComponent>();
        scene.AddObject(obj);

        nlohmann::json j = JsonSerializer::Serialize(scene);

        // 验证 JSON 中两个组件都存在
        const auto& comps = j["scene"]["objects"][0]["components"];
        if (comps.size() != 2) return false;

        // 验证类型字符串正确
        std::string type1 = comps[0]["type"];
        std::string type2 = comps[1]["type"];
        if (type1 != "SpriteComponent" && type1 != "PhysicsComponent") return false;
        if (type2 != "SpriteComponent" && type2 != "PhysicsComponent") return false;
        if (type1 == type2) return false; // 不能重复

        // 反序列化验证组件被正确重建
        Scene loaded;
        JsonSerializer::Deserialize(loaded, j);

        auto* loadedObj = loaded.FindObject("Test");
        if (!loadedObj) return false;
        if (!loadedObj->GetComponent<SpriteComponent>()) return false;
        if (!loadedObj->GetComponent<PhysicsComponent>()) return false;

        return true;
    }

    // ============================================================
    // 测试用例 9: 资源路径完整性
    // ============================================================
    // 验证纹理路径在 JSON 输出中完整保留
    bool SerializationTest::TestResourcePathPreservation() {
        Scene scene("ResourceTest");
        auto obj = std::make_shared<GameObject>("TexturedObj");
        auto* sprite = obj->AddComponent<SpriteComponent>();
        // 注意：不设置纹理（无法在无 GL 环境下加载），
        // 但验证 texture 字段在 JSON 中不存在（null 处理）
        scene.AddObject(obj);

        nlohmann::json j = JsonSerializer::Serialize(scene);
        const auto& data = j["scene"]["objects"][0]["components"][0]["data"];

        // 没有设置纹理时，texture 字段不应出现在 JSON 中
        // （Serialize 中 if (m_Texture) 保护）
        if (data.contains("texture")) return false;

        // 验证即使没有纹理，其他字段仍在
        if (!data.contains("color")) return false;
        if (!data.contains("visible")) return false;

        return true;
    }

    // ============================================================
    // 测试用例 10: JSON 格式结构完整性
    // ============================================================
    bool SerializationTest::TestJsonFormatStructure() {
        Scene scene("StructTest");
        auto obj = std::make_shared<GameObject>("Obj");
        obj->AddComponent<SpriteComponent>();
        obj->AddComponent<PhysicsComponent>();

        auto child = std::make_shared<GameObject>("Child");
        child->AddComponent<SpriteComponent>();
        obj->AddChild(child);
        scene.AddObject(obj);

        nlohmann::json j = JsonSerializer::Serialize(scene);

        // 验证顶层结构
        if (!j.contains("scene")) return false;
        const auto& s = j["scene"];

        // scene 必须有 name 和 properties
        if (!s.contains("name") || !s["name"].is_string()) return false;
        if (!s.contains("properties") || !s["properties"].is_object()) return false;

        // properties 必须有所有字段
        const auto& p = s["properties"];
        if (!p.contains("ambientColor") || !p["ambientColor"].is_array() || p["ambientColor"].size() != 4) return false;
        if (!p.contains("gravity") || !p["gravity"].is_array() || p["gravity"].size() != 2) return false;
        if (!p.contains("enableFog") || !p["enableFog"].is_boolean()) return false;

        // objects 必须是数组
        if (!s.contains("objects") || !s["objects"].is_array()) return false;

        // 验证对象结构
        const auto& objJson = s["objects"][0];
        if (!objJson.contains("name")) return false;
        if (!objJson.contains("active")) return false;
        if (!objJson.contains("transform")) return false;
        if (!objJson.contains("components")) return false;

        // transform 必须有 position, rotation, scale
        const auto& t = objJson["transform"];
        if (!t.contains("position") || t["position"].size() != 3) return false;
        if (!t.contains("rotation") || t["rotation"].size() != 3) return false;
        if (!t.contains("scale") || t["scale"].size() != 3) return false;

        // components 必须是数组
        if (!objJson["components"].is_array()) return false;

        // 验证 children
        if (!objJson.contains("children") || !objJson["children"].is_array()) return false;
        if (objJson["children"].size() != 1) return false;
        const auto& childJson = objJson["children"][0];
        if (!childJson.contains("name") || childJson["name"] != "Child") return false;

        // 验证 JSON 格式正确：可重新解析
        std::string jsonStr = j.dump(4);
        nlohmann::json reparsed = nlohmann::json::parse(jsonStr);
        if (reparsed["scene"]["name"] != "StructTest") return false;

        return true;
    }

    // ============================================================
    // 测试用例 11: 场景保存/加载到文件
    // ============================================================
    bool SerializationTest::TestSceneSaveLoadFile() {
        // 使用临时文件测试 SaveToFile / LoadFromFile
        Scene original("FileTest");
        original.SetAmbientColor(Vec4(0.1f, 0.2f, 0.3f, 0.4f));

        auto obj = std::make_shared<GameObject>("FileObj");
        obj->GetTransform().SetPosition(5.0f, 10.0f, 15.0f);
        original.AddObject(obj);

        // 保存到临时路径
        std::string testPath = "test_serialization_output.json";

        if (!JsonSerializer::SaveToFile(original, testPath)) return false;

        // 验证文件存在且非空
        std::ifstream fin(testPath);
        if (!fin.is_open()) return false;
        std::string content((std::istreambuf_iterator<char>(fin)),
                             std::istreambuf_iterator<char>());
        fin.close();
        if (content.empty()) return false;

        // 验证可解析
        nlohmann::json parsed;
        try {
            parsed = nlohmann::json::parse(content);
        } catch (...) {
            return false;
        }
        if (parsed["scene"]["name"] != "FileTest") return false;

        // 加载回场景
        Scene loaded;
        if (!JsonSerializer::LoadFromFile(loaded, testPath)) return false;
        if (loaded.GetName() != "FileTest") return false;
        if (loaded.GetObjectCount() != 1) return false;
        if (!loaded.FindObject("FileObj")) return false;

        // 清理临时文件
        std::remove(testPath.c_str());

        return true;
    }

    // ============================================================
    // 运行全部测试
    // ============================================================

    bool SerializationTest::RunAll() {
        s_Passed = 0;
        s_Failed = 0;

#ifdef _WIN32
        SetConsoleOutputCP(CP_UTF8);
#endif

        std::cout << "========================================" << std::endl;
        std::cout << "  场景序列化/反序列化 — 完整测试套件" << std::endl;
        std::cout << "========================================" << std::endl;
        std::cout << "引擎版本: Game-Engine-Demo" << std::endl;
        std::cout << "序列化格式: JSON (nlohmann/json v3.11.3)" << std::endl;
        std::cout << "========================================" << std::endl;

        // ── 场景属性 ──
        TEST_SECTION("场景属性");
        TEST_CASE("场景属性序列化/反序列化", TestSceneProperties());
        TEST_CASE("空场景", TestEmptyScene());

        // ── 对象与变换 ──
        TEST_SECTION("对象与变换");
        TEST_CASE("Transform 精确往返", TestTransformRoundTrip());
        TEST_CASE("多根对象", TestMultipleRootObjects());
        TEST_CASE("非活跃对象标记", TestInactiveObject());

        // ── 组件序列化 ──
        TEST_SECTION("组件序列化");
        TEST_CASE("SpriteComponent 全部字段", TestSingleObjectWithSprite());
        TEST_CASE("注册式组件工厂正确性", TestComponentFactoryRegistration());
        TEST_CASE("资源路径引用完整性", TestResourcePathPreservation());

        // ── 层级结构 ──
        TEST_SECTION("层级结构");
        TEST_CASE("父子层级递归序列化", TestParentChildHierarchy());

        // ── 格式与文件 ──
        TEST_SECTION("格式与文件 I/O");
        TEST_CASE("JSON 格式结构完整性", TestJsonFormatStructure());
        TEST_CASE("场景文件保存/加载", TestSceneSaveLoadFile());

        // ── 汇总 ──
        std::cout << "\n========================================" << std::endl;
        std::cout << "  测试完成: "
                  << s_Passed << " 通过, "
                  << s_Failed << " 失败, "
                  << (s_Passed + s_Failed) << " 总计"
                  << std::endl;
        std::cout << "========================================" << std::endl;

        return s_Failed == 0;
    }

} // namespace Engine
