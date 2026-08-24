/**
 * @file Dogfood09Test.cpp
 * @brief DF09 Multi-Scene / Reference Integrity — 资产/场景规模压力测试
 *
 * 八道门禁：
 *   R1 双场景共享资产 → GUID 唯一且稳定
 *   R2 rename/move 后引用不失效（Unregister+RegisterExplicit 原语）
 *   R3 场景持久层不泄漏路径（只存 GUID）
 *   R4 双场景独立 Save 互不污染
 *   R5 跨进程重启后双场景恢复
 *   R6 缺失/损坏 → 失败局部化且可诊断
 *   R7 Browser/Inspector/Registry 三方身份一致
 *   R8 重复 Import / 重命名 / 再 Import 无 GUID 漂移
 * 附：DL-02 多场景规模探针（mid-session 实体的 binding 归属）
 */

#include <gtest/gtest.h>
#include "Engine/Core/Content/ContentAsset.h"
#include "Engine/Core/Content/SceneSerializerV1.h"
#include "Engine/Core/Scene/Scene.h"
#include "Engine/Core/Resources/ResourceGUID.h"
#include "Engine/Core/RenderResources/TextureManager.h"

#include "Engine/Scripting/LuaEngine.h"
#include "Engine/Scripting/GameplayAPI.h"
#include "Engine/Scripting/ScriptAPI.h"
#include "Engine/Scripting/ScriptInstance.h"
#include "Engine/OpenGL/OpenGLGraphicsFactory.h"

#include <nlohmann/json.hpp>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

using namespace Engine;
using namespace Engine::Content;
using namespace Engine::Scripting;

namespace {
    const std::string kDir = "df09_scratch";

    void W9(const std::string& name, const std::string& body) {
        std::filesystem::create_directories(kDir);
        std::ofstream f(kDir + "/" + name, std::ios::binary | std::ios::trunc);
        f << body;
    }
    std::string P9(const std::string& n) { return kDir + "/" + n; }

    std::string ReadAll(const std::string& path) {
        std::ifstream f(path, std::ios::binary);
        std::stringstream ss; ss << f.rdbuf();
        return ss.str();
    }
}

TEST(Dogfood09, MultiScene_ReferenceIntegrity) {
    std::filesystem::remove_all(kDir);

    // ═══ Setup: 共享资产 + 双场景 ═══
    ContentRegistry reg;
    W9("shared.lua", "_PERSIST = _PERSIST or {}\nfunction OnCreate() end\nfunction OnUpdate(dt) end\n");
    auto sharedScr = reg.Import(P9("shared.lua"), AssetType::Script);
    auto sharedTex = reg.Import(P9("shared.png"), AssetType::Texture);

    SceneSnapshot snapA;                       // Scene A: Player(脚本) + DecoA(sprite)
    snapA.entities.push_back({"Player", 1.f, 0.f, 0.f,
                              ResourceGUID::Null, sharedScr});
    snapA.entities.push_back({"DecoA", -2.f, 0.f, 0.f, sharedTex, ResourceGUID::Null});

    SceneSnapshot snapB;                       // Scene B: Enemy(sprite) + DecoB(sprite)
    snapB.entities.push_back({"Enemy", 3.f, 0.f, -1.f, sharedTex, ResourceGUID::Null});
    snapB.entities.push_back({"DecoB", -3.f, 0.f, -1.f, sharedTex, ResourceGUID::Null});

    const std::string pathA = P9("scene_a.scene"), pathB = P9("scene_b.scene");
    ASSERT_TRUE(SaveSnapshotToFile(snapA, pathA));
    ASSERT_TRUE(SaveSnapshotToFile(snapB, pathB));
    ASSERT_TRUE(reg.SaveManifest(P9("manifest.json")));

    // ── R1: 共享资产的 GUID 唯一且稳定 ──
    EXPECT_EQ(reg.Count(), 2u);                          // 恰好两个资产，无重复条目
    EXPECT_EQ(reg.ResolvePath(sharedTex), P9("shared.png"));
    EXPECT_EQ(reg.ResolvePath(sharedScr), P9("shared.lua"));
    EXPECT_NE(sharedTex, sharedScr);

    // ── R3: 持久层不写路径（场景文件只含 GUID）──
    const std::string textA = ReadAll(pathA), textB = ReadAll(pathB);
    EXPECT_EQ(textA.find(".png"), std::string::npos) << "path leaked into scene A";
    EXPECT_EQ(textA.find(".lua"), std::string::npos) << "path leaked into scene A";
    EXPECT_EQ(textB.find(".png"), std::string::npos);
    EXPECT_EQ(textB.find(".lua"), std::string::npos);
    // 且确实以 GUID hex 形式引用
    EXPECT_NE(textA.find(sharedScr.ToHex()), std::string::npos)
        << "scene A does not reference script by GUID";

    // ── R4: 独立 Save 不互相污染 ──
    snapA.entities[0].px = 42.f;                         // 只改 A 的内存快照
    const std::string bytesB_before = textB;
    ASSERT_TRUE(SaveSnapshotToFile(snapA, pathA));
    EXPECT_EQ(ReadAll(pathB), bytesB_before)             // B 文件字节级不变
        << "saving A mutated B on disk";
    SceneSnapshot bBack; std::string err;
    ASSERT_TRUE(LoadSnapshotFromFile(pathB, bBack, err));
    EXPECT_FLOAT_EQ(bBack.entities[0].px, 3.f);          // B 内容未受影响

    // ── R2+R8: rename/move —— 引用不失效、无漂移 ──
    const std::string bytesA_atRename = ReadAll(pathA);
    reg.Unregister(sharedTex);
    EXPECT_FALSE(reg.ContainsGuid(sharedTex));
    ASSERT_TRUE(reg.RegisterExplicit(sharedTex, P9("renamed.png"), AssetType::Texture))
        << "rename primitive (Unregister+RegisterExplicit) failed";
    // 引用跟随注册表：同 GUID 新路径
    EXPECT_EQ(reg.ResolvePath(sharedTex), P9("renamed.png"));
    // 场景文件纹丝不动（契约核心：移动只需更新注册表）
    EXPECT_EQ(ReadAll(pathA), bytesA_atRename)
        << "rename must not touch scene files";
    // R8: 旧路径再 Import → 新身份；新路径再 Import → 幂等原身份
    auto staleOld = reg.Import(P9("shared.png"), AssetType::Texture);
    EXPECT_FALSE(staleOld == sharedTex) << "old path resurrected old GUID";
    auto reimport = reg.Import(P9("renamed.png"), AssetType::Texture);
    EXPECT_TRUE(reimport == sharedTex) << "GUID drift after rename+reimport";

    // ── R7: Browser / Inspector / Registry 三方一致 ──
    // Browser 等价 = GetAllEntries；Registry = ResolvePath/TypeOf；
    // Inspector 等价 = Instantiate 后的 bindings 解析结果。
    for (const auto& e : reg.GetAllEntries()) {
        EXPECT_EQ(reg.ResolvePath(e.guid), e.path) << "browser entry != registry";
        EXPECT_TRUE(e.type == AssetType::Texture || e.type == AssetType::Script);
        EXPECT_EQ(reg.TypeOf(e.guid), e.type);
    }
    EXPECT_EQ(reg.GetAllEntries().size(), 3u);           // renamed.png + shared.lua + shared.png(stale)

    // ═══ R5: 跨进程重启 → 双场景恢复 ═══
    OpenGLGraphicsFactory gfxF;
    TextureManager tm(gfxF);
    ContentRegistry reg2;                                // 全新进程等价
    ASSERT_TRUE(reg2.LoadManifest(P9("manifest.json")));
    // 注意：manifest 是重命名前保存的吗？——不是。SaveManifest 发生在 rename 前，
    // 因此此处重新保存以携带 rename 结果（真实编辑器会在 rename 后自动存清单）。
    ASSERT_TRUE(reg.SaveManifest(P9("manifest.json")));
    ASSERT_TRUE(reg2.LoadManifest(P9("manifest.json")));

    Engine::Scene sceneA2, sceneB2;
    GameplayAPI::Reset();
    GameplayAPI::SetScene(&sceneA2);
    auto rA = InstantiateScene(snapA, sceneA2, tm, reg2);
    ASSERT_TRUE(rA.ok);
    GameplayAPI::SetScene(&sceneB2);
    auto rB = InstantiateScene(snapB, sceneB2, tm, reg2);
    ASSERT_TRUE(rB.ok);

    EXPECT_EQ(sceneA2.GetObjectCount(), 2u);
    EXPECT_EQ(sceneB2.GetObjectCount(), 2u);
    EXPECT_NEAR(sceneA2.GetObjects()[0]->GetTransform().GetPosition().x, 42.f, 0.001f);

    // Inspector 视角：两场景的 sprite binding 都解析到 rename 后路径
    for (auto* r : {&rA, &rB}) {
        for (const auto& b : r->bindings) {
            if (!b.spriteGuid.IsNull())
                EXPECT_EQ(reg2.ResolvePath(b.spriteGuid), P9("renamed.png"))
                    << "sprite reference broken across restart+rename";
            if (!b.scriptGuid.IsNull())
                EXPECT_EQ(reg2.ResolvePath(b.scriptGuid), P9("shared.lua"))
                    << "script reference broken across restart";
        }
    }
    // R7 收尾：binding 层的每个 GUID 都能在 Browser 条目中找到
    for (auto* r : {&rA, &rB})
        for (const auto& b : r->bindings) {
            if (b.spriteGuid.IsNull()) continue;
            bool found = false;
            for (const auto& e : reg2.GetAllEntries())
                if (e.guid == b.spriteGuid) { found = true; break; }
            EXPECT_TRUE(found) << "inspector GUID not in browser entries";
        }

    // ═══ DL-02 多场景规模探针：mid-session 实体归属 ═══
    GameplayAPI::Reset();
    GameplayAPI::SetScene(&sceneA2);
    ASSERT_NE(GameplayAPI::HandleSpawn("Late_A"), 0u);   // 只加到 A
    auto bindExtA = rA.bindings;
    bindExtA.push_back({});
    SceneSnapshot aLive = CaptureScene(sceneA2, bindExtA);
    ASSERT_EQ(aLive.entities.size(), 3u);
    EXPECT_EQ(aLive.entities[2].name, "Late_A");
    ASSERT_TRUE(SaveSnapshotToFile(aLive, pathA));

    // B 保持 2 实体——A 的 mid-session 增员不扩散
    SceneSnapshot aIn, bIn;
    ASSERT_TRUE(LoadSnapshotFromFile(pathA, aIn, err));
    ASSERT_TRUE(LoadSnapshotFromFile(pathB, bIn, err));
    EXPECT_EQ(aIn.entities.size(), 3u);
    EXPECT_EQ(bIn.entities.size(), 2u);
    // DL-02 结论采样：Late_A 的 spriteGuid 为 Null（调用方未提供 binding 行）
    EXPECT_TRUE(aIn.entities[2].spriteGuid.IsNull());
}

// ════════════════════════════════════════════════════════════
// R6: 缺失/损坏 → 失败局部化且可诊断
// ════════════════════════════════════════════════════════════
TEST(Dogfood09, FailureContract_LocalAndDiagnosable) {
    std::filesystem::remove_all(kDir);
    std::filesystem::create_directories(kDir);

    ContentRegistry reg;
    auto tex = reg.Import(P9("t.png"), AssetType::Texture);
    auto scr = reg.Import(P9("s.lua"), AssetType::Script);

    SceneSnapshot good;
    good.entities.push_back({"Solo", 0.f, 0.f, 0.f, tex, scr});
    const std::string pGood = P9("good.scene");
    ASSERT_TRUE(SaveSnapshotToFile(good, pGood));

    // (a) 场景文件损坏 → Load 干净失败，注册表不受牵连
    {
        const std::string pBad = P9("corrupt.scene");
        W9("corrupt.scene", "{ this is not valid json !!!");
        SceneSnapshot out; std::string err;
        EXPECT_FALSE(LoadSnapshotFromFile(pBad, out, err));
        EXPECT_FALSE(err.empty()) << "corruption must be diagnosable";
        EXPECT_EQ(reg.Count(), 2u);                      // 注册表无恙
    }
    // 同会话内 good.scene 仍可加载（局部性）
    {
        SceneSnapshot g2; std::string err;
        ASSERT_TRUE(LoadSnapshotFromFile(pGood, g2, err)) << err;
        EXPECT_EQ(g2.entities.size(), 1u);
    }

    // (b) 注册表缺资产 → 实例化降级为 warning，实体照常存在，script 不受 sprite 缺失牵连
    {
        ContentRegistry partialReg;                      // 故意不含 tex/scr
        partialReg.Import(P9("unrelated.txt"), AssetType::Texture);

        OpenGLGraphicsFactory gfxF;
        TextureManager tm(gfxF);
        Engine::Scene scene;
        GameplayAPI::Reset();
        GameplayAPI::SetScene(&scene);
        auto r = InstantiateScene(good, scene, tm, partialReg);
        EXPECT_TRUE(r.ok) << "missing asset must degrade, not fail";
        EXPECT_EQ(scene.GetObjectCount(), 1u);           // 实体存活
        EXPECT_EQ(r.warnings.size(), 2u);                // sprite + script 双诊断
        bool mentionsEntity = false;
        for (const auto& w : r.warnings)
            if (w.find("Solo") != std::string::npos) { mentionsEntity = true; break; }
        EXPECT_TRUE(mentionsEntity) << "warning must name the affected entity";
        // binding 层保持空 GUID（Inspector 可见"未解析"状态）
        EXPECT_TRUE(r.bindings[0].spriteGuid.IsNull());
        EXPECT_TRUE(r.bindings[0].scriptGuid.IsNull());
    }
}
