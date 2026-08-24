/**
 * @file GP01EditorWorkflowTest.cpp
 * @brief GP1-D 生产门：Editor Workflow 四契约（GoldenPath/IterationLoops/
 *        RenameSafety/WrongEditRecovery）
 *
 * 纪律（docs/GP01-D-Editor-Workflow-Plan.md §6）：
 *   - 全部操作走冻结契约的编辑器等价 API（Import/SetName/SetPosition/
 *     SetTexture/CaptureScene/SaveManifest/Reload），零手写 JSON；
 *   - 不为通过而改 Gameplay C++ / 引擎；发现的摩擦只登记；
 *   - 本文件的断言同时充当 DL-01/DL-02 的行为证据。
 *   - 读 _PERSIST 字段一律先镜像到 _g_* 全局再断言。
 */

#include "GP01Harness.h"

#include "Engine/Core/GameObject/GameObject.h"
#include "Engine/Core/GameObject/SpriteComponent.h"
#include "Engine/Core/GameObject/TransformComponent.h"

#include <fstream>

using namespace gp01;

namespace {

    const std::string kEd = "gp01_editor_scratch";

    const char* kTexPlayer = "assets/gp01/tex/player.png";
    const char* kTexWall   = "assets/gp01/tex/wall.png";
    const char* kTexCrate  = "assets/gp01/tex/prop_crate.png";
    const char* kTexScoutE = "assets/gp01/tex/scout_elite.png";

    inline std::shared_ptr<GameObject> EdFind(Scene& s, const std::string& n) {
        for (auto& o : s.GetObjects())
            if (o->GetName() == n) return o;
        return nullptr;
    }

    inline void WriteLua(const std::string& path, const std::string& code) {
        std::ofstream f(path, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(f.good()) << "cannot write " << path;
        f << code;
    }

    // 编辑器"创建实体 + 分配纹理"一步：HandleSpawn → Sprite → 绑定表同步
    // （bindings 与 scene 对象序一致 —— DL-02 纪律的正确形态）。
    // 定位用插入索引而非按名查找：重名时按名选择会静默命中旧实体，
    // 这正是 DX05 在编辑流程中的活证据（GP-DX 登记，不在此修）。
    inline void EditorCreateTextured(Ctx& c, ResourceGUID texGuid,
                                     const char* texPath,
                                     const std::string& name,
                                     float x, float z) {
        const size_t at = c.scene.GetObjectCount();
        Scripting::GameplayAPI::HandleSpawn(name);
        ASSERT_EQ(c.scene.GetObjectCount(), at + 1)
            << "spawn " << name << " failed";
        auto obj = c.scene.GetObjects()[at];
        obj->AddComponent<SpriteComponent>(c.tm, texPath);
        obj->GetTransform().SetPosition(x, 0.f, z);
        c.bindings.push_back({ texGuid, ResourceGUID{} });   // 同步绑定表
    }

    // GP1-D 冷启：从编辑器 scratch 工程恢复（区别于 harness 的 kScratch 版）。
    // Main.scene 布局下 bindings[0] 恒为 Director。
    inline void EdColdStart(Ctx& x, const std::string& tag) {
        x.BeginFresh((kEd + "/" + tag + "_manifest.json").c_str());
        x.LoadAndBind(kEd + "/" + tag + ".scene");
        ScriptInstance::Config cfg;
        ASSERT_TRUE(x.inst.Initialize(x.DirectorPath(), cfg));
        x.inst.OnCreate();
    }

} // namespace

// ════════════════════════════════════════════════════════════
// D6 黄金路径：Clean Start → Import → Create → Assign → Transform
//   → Save → Play → Reload → Modify → Save → Cold Restart
//   → Load → Verify → Play → Continue
// 验收点：全链无隐藏人工步骤（每步均为冻结 API 调用）。
// ════════════════════════════════════════════════════════════
TEST(GP01, EditorWorkflow_GoldenPath) {
    std::filesystem::remove_all(kEd);
    std::filesystem::create_directories(kEd);

    // ── Clean Start：空注册表 + 空场景 ──
    Ctx c;
    c.BeginFresh(nullptr);                       // 无 manifest = 全新项目
    ASSERT_EQ(c.reg.Count(), 0u);
    ASSERT_EQ(c.scene.GetObjectCount(), 0u);

    // ── Import（幂等）──
    auto gPlayer = c.reg.Import(kTexPlayer, AssetType::Texture);
    auto gWall   = c.reg.Import(kTexWall,   AssetType::Texture);
    ASSERT_FALSE(gPlayer.IsNull());
    ASSERT_FALSE(gWall.IsNull());
    EXPECT_TRUE(c.reg.Import(kTexPlayer, AssetType::Texture) == gPlayer)
        << "re-import must be idempotent";

    // ── Edit Lua v1（Script Editor 写盘）→ Import ──
    const std::string luaPath = kEd + "/director.lua";
    WriteLua(luaPath,
        "_PERSIST = _PERSIST or {}\n"
        "_PERSIST.version = 1\n"
        "function OnCreate() _PERSIST.created = true end\n"
        "function OnUpdate(dt)\n"
        "    _PERSIST.updates = (_PERSIST.updates or 0) + 1\n"
        "end\n");
    auto gScript = c.reg.Import(luaPath, AssetType::Script);
    ASSERT_FALSE(gScript.IsNull());

    // ── Create Entities + Assign Assets + 绑定表纪律 ──
    // 创建序即场景对象序，bindings 必须同步 push（DL-02 正确形态）。
    Scripting::GameplayAPI::HandleSpawn("Director");
    c.bindings.push_back({ ResourceGUID{}, gScript });

    EditorCreateTextured(c, gPlayer, kTexPlayer, "Player", 0.f, 3.f);
    EditorCreateTextured(c, gWall, kTexWall, "Wall_N", 0.f, -8.f);
    EditorCreateTextured(c, gWall, kTexWall, "Wall_S", 0.f,  8.f);
    EditorCreateTextured(c, gWall, kTexWall, "Wall_W", -8.f, 0.f);
    EditorCreateTextured(c, gWall, kTexWall, "Wall_E", 8.f,  0.f);
    ASSERT_EQ(c.scene.GetObjectCount(), 6u);
    ASSERT_EQ(c.bindings.size(), 6u);

    // ── Edit Transform（Inspector 数值输入等价）──
    auto player = EdFind(c.scene, "Player");
    ASSERT_NE(player, nullptr);
    player->GetTransform().SetPosition(1.5f, 0.f, -2.5f);
    EXPECT_FLOAT_EQ(player->GetTransform().GetPosition().x, 1.5f);

    // ── Save（场景 + 清单）──
    auto saveAll = [&](const std::string& tag) {
        SceneSnapshot live = CaptureScene(c.scene, c.bindings);
        EXPECT_TRUE(SaveSnapshotToFile(live, kEd + "/" + tag + ".scene"));
        EXPECT_TRUE(c.reg.SaveManifest(kEd + "/" + tag + "_manifest.json"));
        return live;
    };
    SceneSnapshot snap1 = saveAll("golden");
    {
        const SerializedEntity* pe = nullptr;
        for (auto& e : snap1.entities)
            if (e.name == "Player") { pe = &e; break; }
        ASSERT_NE(pe, nullptr);
        EXPECT_FLOAT_EQ(pe->px, 1.5f);           // authored transform 落盘
        EXPECT_TRUE(pe->spriteGuid == gPlayer);  // 绑定随保存存活
    }
    const size_t regCount = c.reg.Count();       // player/wall/lua = 3

    // ── Play（v1 行为）──
    const std::string resolved = c.reg.ResolvePath(gScript);
    ScriptInstance::Config cfg;
    ASSERT_TRUE(c.inst.Initialize(resolved, cfg));
    c.inst.OnCreate();
    c.Run(30);
    ASSERT_TRUE(c.inst.Execute("_g_v = _PERSIST.version "
                               "_g_u = _PERSIST.updates"));
    EXPECT_EQ((int)c.Num("_g_v"), 1) << "v1 behavior expected before edit";
    EXPECT_GT((int)c.Num("_g_u"), 0);

    // ── Edit Lua v2 → Reload（F5 等价；_PERSIST 保持）──
    WriteLua(luaPath,
        "_PERSIST = _PERSIST or {}\n"
        "_PERSIST.version = 2\n"
        "function OnCreate() _PERSIST.created = true end\n"
        "function OnUpdate(dt)\n"
        "    _PERSIST.updates = (_PERSIST.updates or 0) + 1\n"
        "    _PERSIST.v2_flag = true\n"
        "end\n");
    ASSERT_TRUE(c.inst.Reload()) << "hot reload failed";
    int before = (int)c.Num("_g_u");
    c.Run(10);
    ASSERT_TRUE(c.inst.Execute(
        "_g_f = _PERSIST.v2_flag and 1 or 0 "
        "_g_u = _PERSIST.updates"));
    EXPECT_EQ((int)c.Num("_g_f"), 1) << "new code inactive after Reload";
    EXPECT_GE((int)c.Num("_g_u"), before + 10)
        << "_PERSIST counter must survive Reload";

    // ── Modify Again（编辑器改 transform）→ Save ──
    EdFind(c.scene, "Wall_E")->GetTransform().SetPosition(9.5f, 0.f, 0.f);
    SceneSnapshot snap2 = saveAll("golden2");
    for (auto& e : snap2.entities)
        if (e.name == "Wall_E") EXPECT_FLOAT_EQ(e.px, 9.5f);

    // ── Cold Restart → Load → Verify → Play → Continue ──
    Ctx b;
    b.BeginFresh((kEd + "/golden2_manifest.json").c_str());
    b.LoadAndBind(kEd + "/golden2.scene");
    ASSERT_EQ(b.reg.Count(), regCount) << "registry round-trip drifted";
    ASSERT_EQ(b.scene.GetObjectCount(), 6u);

    // 导演定位：扫描绑定表中 scriptGuid 非空者（不依赖对象序约定）
    size_t dirIdx = SIZE_MAX;
    for (size_t i = 0; i < b.bindings.size(); ++i)
        if (!b.bindings[i].scriptGuid.IsNull()) { dirIdx = i; break; }
    ASSERT_NE(dirIdx, SIZE_MAX);
    const std::string dir2 = b.reg.ResolvePath(b.bindings[dirIdx].scriptGuid);
    ASSERT_FALSE(dir2.empty());

    ASSERT_TRUE(b.inst.Initialize(dir2, cfg));
    b.inst.OnCreate();
    b.Run(30);
    ASSERT_TRUE(b.inst.Execute(
        "_g_v = _PERSIST.version _g_f = _PERSIST.v2_flag and 1 or 0"));
    EXPECT_EQ((int)b.Num("_g_v"), 2)
        << "edited code asset must persist through cold restart";
    EXPECT_EQ((int)b.Num("_g_f"), 1);
    // 布局编辑同样穿越重启：
    EXPECT_FLOAT_EQ(b.Pos("Wall_E").x, 9.5f);
    EXPECT_FLOAT_EQ(b.Pos("Player").x, 1.5f);
    b.Run(60);
    EXPECT_TRUE(b.inst.IsValid());
}

// ════════════════════════════════════════════════════════════
// D3 三次迭代（真实 gp01 游戏上）：Create→Save→Play /
//   运行中新增+脚本热更 / 再修改→Save→Cold Restart→Load。
//   同时正面产出 DL-01/DL-02 行为证据。
// ════════════════════════════════════════════════════════════
TEST(GP01, EditorWorkflow_IterationLoops) {
    std::filesystem::remove_all(kEd);
    std::filesystem::create_directories(kEd);

    // ══ Iteration 1: Create → Save → Play ══
    Ctx c;
    StartRun(c);
    FreezeWaves(c);
    auto gCrate = c.reg.Import(kTexCrate, AssetType::Texture);  // 幂等复用清单
    EditorCreateTextured(c, gCrate, kTexCrate, "Crate_Prop", 5.f, -5.f);

    SceneSnapshot it1 = CaptureScene(c.scene, c.bindings);
    ASSERT_TRUE(SaveSnapshotToFile(it1, kEd + "/it1.scene"));
    ASSERT_TRUE(c.reg.SaveManifest(kEd + "/it1_manifest.json"));
    {
        bool found = false;
        for (auto& e : it1.entities)
            if (e.name == "Crate_Prop") {
                found = true;
                EXPECT_TRUE(e.spriteGuid == gCrate)
                    << "binding-table sync failed on create";
            }
        EXPECT_TRUE(found);
    }
    c.Run(60);                                   // Play
    EXPECT_TRUE(c.inst.IsValid());

    // ══ Iteration 2: 运行中新增 Entity → 修改 Script → Save ══
    // 运行时漂移（DL-01 观察窗）：把 Player 挪离 authored 位
    const Vec3 authored = c.Pos("Player");
    TeleportEntity(c, "Player", 2.f, 2.f);
    // 运行中新增（GP1-C 冻结 API；波次已冻结故为纯编辑态实验）
    const std::string en = SpawnOne(c, "grunt");
    ASSERT_FALSE(en.empty());
    c.Run(10);                                   // 敌人开始追击 → 更多漂移

    SceneSnapshot live2 = CaptureScene(c.scene, c.bindings);
    const SerializedEntity* pE = nullptr;
    const SerializedEntity* pP = nullptr;
    const SerializedEntity* pC = nullptr;
    for (auto& e : live2.entities) {
        if (e.name == en)           pE = &e;
        if (e.name == "Player")     pP = &e;
        if (e.name == "Crate_Prop") pC = &e;
    }
    // DL-02 证据：mid-session 新增实体不在编辑器绑定表内 → GUID 静默置 Null
    ASSERT_NE(pE, nullptr) << "runtime entity missing from capture";
    EXPECT_TRUE(pE->spriteGuid.IsNull() && pE->scriptGuid.IsNull())
        << "DL-02: unsynced mid-session entity loses content binding "
           "(documented frozen-contract behavior)";
    // Crate_Prop 有同步 → 绑定保持
    ASSERT_NE(pC, nullptr);
    EXPECT_TRUE(pC->spriteGuid == gCrate);
    // DL-01 证据：Capture 存运行时位置而非 authored 值
    // （冻结波次 + 无输入 → Player 精确停留在 teleport 落点）
    ASSERT_NE(pP, nullptr);
    EXPECT_FLOAT_EQ(pP->px, 2.f)
        << "DL-01: capture should hold runtime position, not authored";
    EXPECT_FLOAT_EQ(pP->pz, 2.f);
    EXPECT_FALSE(std::hypot(pP->px - authored.x,
                            pP->pz - authored.z) < 1e-4f)
        << "DL-01: runtime drift must be visible in capture";

    // 脚本修改循环（watcher 实例；不动仓库资产 game.lua）
    const std::string wPath = kEd + "/watcher.lua";
    WriteLua(wPath,
        "_PERSIST = _PERSIST or {}\n"
        "function OnUpdate(dt)\n"
        "    _PERSIST.updates = (_PERSIST.updates or 0) + 1\n"
        "end\n");
    auto gW = c.reg.Import(wPath, AssetType::Script);
    ScriptInstance w;
    ASSERT_TRUE(w.Initialize(c.reg.ResolvePath(gW), ScriptInstance::Config{}));
    w.OnCreate();
    for (int i = 0; i < 10; ++i) {
        c.inst.OnUpdate(1.f / 60.f);
        w.OnUpdate(1.f / 60.f);
    }
    ASSERT_TRUE(w.GetEngine()->RunString("_g_u = _PERSIST.updates"));
    EXPECT_EQ((int)(w.GetEngine()->GetGlobalDouble("_g_u", -1)), 10);
    // 修改 → Reload → 新代码生效且 _PERSIST 连续
    WriteLua(wPath,
        "_PERSIST = _PERSIST or {}\n"
        "function OnUpdate(dt)\n"
        "    _PERSIST.updates = (_PERSIST.updates or 0) + 1\n"
        "    _PERSIST.hot = true\n"
        "end\n");
    ASSERT_TRUE(w.Reload());
    c.inst.OnUpdate(1.f / 60.f);
    w.OnUpdate(1.f / 60.f);
    ASSERT_TRUE(w.GetEngine()->RunString(
        "_g_h = _PERSIST.hot and 1 or 0 "
        "_g_u = _PERSIST.updates"));
    EXPECT_EQ((int)(w.GetEngine()->GetGlobalDouble("_g_h", 0)), 1)
        << "script iteration did not activate after Reload";
    EXPECT_GE((int)(w.GetEngine()->GetGlobalDouble("_g_u", -1)), 11)
        << "_PERSIST lost across Reload";

    // ══ Iteration 3: 再修改 → Save → Cold Restart → Load ══
    EdFind(c.scene, "Wall_W")->GetTransform().SetPosition(-7.f, 0.f, 0.f);
    SceneSnapshot it3 = CaptureScene(c.scene, c.bindings);
    ASSERT_TRUE(SaveSnapshotToFile(it3, kEd + "/it3.scene"));
    ASSERT_TRUE(c.reg.SaveManifest(kEd + "/it3_manifest.json"));

    Ctx b;
    EdColdStart(b, "it3");                       // 纯编辑器语义重启（无 blob）
    FreezeWaves(b);                              // 聚焦工作流而非战斗
    // 精确口径：10 布局 + Crate_Prop + E_001 = 12（宽限期无新生成）
    ASSERT_EQ(b.scene.GetObjectCount(), 12u);
    EXPECT_FLOAT_EQ(b.Pos("Wall_W").x, -7.f)     // It.3 编辑穿越重启
        << "DL-01 round-trip: edited layout lost on restart";
    EXPECT_NEAR(b.Pos("Player").x, 2.f, 1.0f)
        << "DL-01 round-trip: drifted position persisted as-is";
    EXPECT_TRUE(b.InScene("Crate_Prop")) << "synced prop lost on restart";
    EXPECT_TRUE(b.InScene(en))
        << "mid-session entity persisted through editor save/restart";
    b.Run(60);
    EXPECT_TRUE(b.inst.IsValid()) << "game must boot on edited layout";
}

// ════════════════════════════════════════════════════════════
// D4 Rename 安全性：name 是查询便利，GUID 才是持久身份。
//   Enemy_Grunt_A → Guard_Left 全链 + 同名存储探针（DX05 观察）。
// ════════════════════════════════════════════════════════════
TEST(GP01, EditorWorkflow_RenameSafety) {
    std::filesystem::remove_all(kEd);
    std::filesystem::create_directories(kEd);

    Ctx c;
    StartRun(c);
    FreezeWaves(c);
    auto gScout = c.reg.Import(kTexScoutE, AssetType::Texture);
    EditorCreateTextured(c, gScout, kTexScoutE,
                         "Enemy_Grunt_A", -4.f, -4.f);

    // ── Rename（HierarchyPanel F2 等价：SetName）──
    auto obj = EdFind(c.scene, "Enemy_Grunt_A");
    ASSERT_NE(obj, nullptr);
    obj->SetName("Guard_Left");
    EXPECT_EQ(c.scene.FindObject("Enemy_Grunt_A"), nullptr)
        << "old name must be gone immediately";
    EXPECT_NE(c.scene.FindObject("Guard_Left"), nullptr);

    // ── Save → Cold Restart → Load ──
    SceneSnapshot snap = CaptureScene(c.scene, c.bindings);
    ASSERT_TRUE(SaveSnapshotToFile(snap, kEd + "/rename.scene"));
    ASSERT_TRUE(c.reg.SaveManifest(kEd + "/rename_manifest.json"));
    for (auto& e : snap.entities)
        if (e.name == "Guard_Left")
            EXPECT_TRUE(e.spriteGuid == gScout)
                << "pre-save capture must carry original binding";

    Ctx b;
    EdColdStart(b, "rename");
    FreezeWaves(b);
    // Hierarchy 名称正确
    EXPECT_NE(b.scene.FindObject("Guard_Left"), nullptr);
    EXPECT_EQ(b.scene.FindObject("Enemy_Grunt_A"), nullptr)
        << "stale identity resurrected across restart";
    // GUID 身份不变：改名不脱离持久身份（按索引对齐验证绑定存活）
    bool guidKept = false;
    const auto& objs = b.scene.GetObjects();
    for (size_t i = 0; i < objs.size() && i < b.bindings.size(); ++i)
        if (objs[i]->GetName() == "Guard_Left")
            guidKept = (b.bindings[i].spriteGuid == gScout);
    EXPECT_TRUE(guidKept)
        << "rename must not detach persistent identity (GUID)";
    // Script binding 不受影响：导演照常驱动
    b.Run(60);
    EXPECT_TRUE(b.inst.IsValid());

    // ── 同名存储探针（DX05 观察，不做深查找断言）──
    // 返回原工程继续编辑：GameplayAPI 全局绑定被冷启 ctx 抢占，
    // 必须手动重绑（GP-DX 观察项：跨工程上下文切换无自动恢复，
    // DX02 家族 —— 真实编辑器单场景下不触发，headless 多工程才显性）。
    Scripting::GameplayAPI::Reset();
    Scripting::GameplayAPI::SetScene(&c.scene);
    Scripting::GameplayAPI::SetInputProvider(&c.input);
    // 存储层按索引对齐：两个同名实体各自快照条目独立、位置可区分。
    EditorCreateTextured(c, gScout, kTexScoutE, "Crate", 1.f, 1.f);
    EditorCreateTextured(c, gScout, kTexScoutE, "Crate", 2.f, 2.f);
    SceneSnapshot dup = CaptureScene(c.scene, c.bindings);
    int dupCount = 0; float xs[2] = { 0.f, 0.f }; int n = 0;
    for (auto& e : dup.entities)
        if (e.name == "Crate" && n < 2) { xs[n++] = e.px; ++dupCount; }
    EXPECT_EQ(dupCount, 2) << "same-name entities must both persist";
    EXPECT_FLOAT_EQ(xs[0], 1.f);
    if (dupCount > 1) EXPECT_FLOAT_EQ(xs[1], 2.f);
    // DX05 备注：加载后按名查找将产生歧义（首中/覆盖）——
    // 该查找层摩擦登记 GP-DX，不在本测试引入 UUID UX 修复。
}

// ════════════════════════════════════════════════════════════
// D5 错误编辑恢复成本：错绑纹理 + 误移动 → Save（错误落盘）
//   → 发现 → 修正 → Save → 冷启验证。
//   不做 Undo/Redo —— 只测量恢复所需步骤。
// ════════════════════════════════════════════════════════════
TEST(GP01, EditorWorkflow_WrongEditRecovery) {
    std::filesystem::remove_all(kEd);
    std::filesystem::create_directories(kEd);

    Ctx c;
    StartRun(c);
    FreezeWaves(c);
    auto gPlayer = c.reg.Import(kTexPlayer, AssetType::Texture);
    auto gWall   = c.reg.Import(kTexWall,   AssetType::Texture);
    const Vec3 safePos = c.Pos("Player");        // authored 安全域

    // ── 错误编辑（两次误操作）＋ 绑定表跟着错 ──
    {
        size_t pi = c.idx.at("Player");
        auto po = EdFind(c.scene, "Player");
        ASSERT_NE(po, nullptr);
        po->GetComponent<SpriteComponent>()->SetTexture(c.tm, kTexWall);
        c.bindings[pi].spriteGuid = gWall;       // 编辑器绑定表同步错误值
        po->GetTransform().SetPosition(0.f, 0.f, -7.8f);  // 误拖近北墙
    }
    SceneSnapshot wrong = CaptureScene(c.scene, c.bindings);
    ASSERT_TRUE(SaveSnapshotToFile(wrong, kEd + "/wrong.scene"));
    ASSERT_TRUE(c.reg.SaveManifest(kEd + "/wrong_manifest.json"));

    // ── 发现：错误已被完整持久化（冷启读回验证）──
    {
        Ctx probe;
        EdColdStart(probe, "wrong");
        FreezeWaves(probe);
        size_t pi = probe.idx.at("Player");
        EXPECT_TRUE(probe.bindings[pi].spriteGuid == gWall)
            << "mistake did not persist? evidence invalid";
        EXPECT_NEAR(probe.Pos("Player").z, -7.8f, 1e-3f);
    }

    // ── 修正（仅属性复位，无 Undo、无手改 JSON、无需重导入）──
    {
        size_t pi = c.idx.at("Player");
        auto po = EdFind(c.scene, "Player");
        ASSERT_NE(po, nullptr);
        po->GetComponent<SpriteComponent>()->SetTexture(c.tm, kTexPlayer);
        c.bindings[pi].spriteGuid = gPlayer;
        po->GetTransform().SetPosition(safePos.x, 0.f, safePos.z);
    }
    SceneSnapshot fixed = CaptureScene(c.scene, c.bindings);
    ASSERT_TRUE(SaveSnapshotToFile(fixed, kEd + "/fixed.scene"));
    ASSERT_TRUE(c.reg.SaveManifest(kEd + "/fixed_manifest.json"));

    // ── 冷启终验：正确状态 + 可玩 ──
    Ctx b;
    EdColdStart(b, "fixed");
    size_t pi = b.idx.at("Player");
    EXPECT_TRUE(b.bindings[pi].spriteGuid == gPlayer)
        << "corrected binding lost on restart";
    EXPECT_FLOAT_EQ(b.Pos("Player").x, safePos.x);
    EXPECT_FLOAT_EQ(b.Pos("Player").z, safePos.z);
    ASSERT_TRUE(WaitForWave(b, 1, 5));           // 修正后的布局上可玩
    KillUntilDestroyed(b, 1);
    EXPECT_GE(b.Snapshot().score, 20);

    // 恢复成本记录（D5 七元组口径，供 Ledger GP-DX 引用）：
    //   操作数=4（2 属性复位 + 1 重新 Save + 1 冷启验证）
    //   手改 JSON=0 · 外部 IDE=0 · 重启 Editor=0 · 重新 Import=0 · 数据丢失=0
    //   → Undo/Redo 维持 deferred（未构成生产阻塞）。
}
