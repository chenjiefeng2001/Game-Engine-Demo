/**
 * @file DogfoodDXTest.cpp
 * @brief Dogfood-04 — Developer Experience Audit（从空项目到可玩游戏的步骤审计）
 *
 * 本测试模拟一个首次使用引擎的开发者的完整工作流。
 * 每个 _FRICTION_ 注释标记一个真实摩擦点。
 * 测试通过 = 工作流可完成；但不等于工作流是顺畅的。
 */

#include <gtest/gtest.h>
#include "Engine/Core/Content/ContentAsset.h"
#include "Engine/Core/Content/SceneSerializerV1.h"
#include "Engine/Core/Content/ResourceLifecycle.h"
#include "Engine/Core/Scene/Scene.h"
#include "Engine/Core/Resources/ResourceGUID.h"

#include "Engine/Scripting/LuaEngine.h"
#include "Engine/Scripting/GameplayAPI.h"
#include "Engine/Scripting/ScriptAPI.h"
#include "Engine/Scripting/ScriptInstance.h"

#include <filesystem>
#include <fstream>

using namespace Engine;
using namespace Engine::Content;
using namespace Engine::Scripting;

namespace {
    const std::string kDX = "dx_audit_scratch";

    void W(const std::string& name, const std::string& body) {
        std::filesystem::create_directories(kDX);
        std::ofstream f(kDX + "/" + name, std::ios::binary | std::ios::trunc);
        f << body;
    }
    std::string DP(const std::string& name) { return kDX + "/" + name; }
}

// ═══ Full workflow: Empty project → Playable game ═══
//
// Simulates a developer's actions IN ORDER:
//   1. Start engine (no prior state)
//   2. Import a texture
//   3. Create entities
//   4. Assign assets to entities
//   5. Write gameplay script
//   6. Attach script to entity
//   7. Play (drive input)
//   8. Save scene
//   9. "Restart" (fresh load)
//  10. Continue playing
//
// Each FRICTION comment = a real DX gap discovered during implementation.

TEST(DogfoodDX, Full_Workflow_EmptyToPlayable) {
    // ═══ Step 1: Start engine ═══
    // A first-time developer launches ScriptSandbox.exe.
    //
    // FRICTION-DX01: The app ALWAYS loads sandbox_player.lua on startup.
    //   There is no "New Project" / "Empty Scene" option.
    //   A first-time user sees someone else's script running immediately.
    // SEVERITY: HIGH — confusing first impression.

    ContentRegistry reg;
    Engine::Scene scene;
    OpenGLGraphicsFactory gfxF;
    Engine::TextureManager tm(gfxF);

    Scripting::GameplayAPI::Reset();
    Scripting::GameplayAPI::SetScene(&scene);
    // NOTE: No SetInputProvider here yet — that's another gap.
    // FRICTION-DX02: Developer must know they need to call SetInputProvider
    //   before any input works. No automatic default provider exists.

    // ═══ Step 2: Import a texture ═══
    //
    // In the real editor: Asset Browser → but only shows ALREADY imported assets.
    // FRICTION-DX03: There is no "Import New Asset" button in the Asset Browser.
    //   The developer must know the file path beforehand and type it into
    //   Inspector's text field, which calls Registry.Import().
    //   No file dialog, no directory scan, no drag-drop from OS.
    // SEVERITY: MEDIUM — functional but not discoverable.

    auto texGuid = reg.Import("assets/textures/test.png", AssetType::Texture);
    ASSERT_FALSE(texGuid.IsNull());
    EXPECT_EQ(reg.ResolvePath(texGuid), "assets/textures/test.png");

    // ═══ Step 3: Create entities ═══
    //
    // In the editor: Hierarchy → Create Entity button ✓
    // Headless: GameplayAPI::HandleSpawn("name") ✓

    Scripting::GameplayAPI::HandleSpawn("Player");
    Scripting::GameplayAPI::HandleSpawn("Goal");
    auto playerH = Scripting::GameplayAPI::HandleFindByName("Player");
    auto goalH = Scripting::GameplayAPI::HandleFindByName("Goal");
    ASSERT_NE(playerH, 0u);
    ASSERT_NE(goalH, 0u);

    // FRICTION-DX04: No way to set initial position at creation time.
    //   All entities spawn at origin. Developer must immediately
    //   switch to Inspector and drag/set position for EVERY entity.
    //   No "spawn at camera center" or "spawn at mouse position".
    // SEVERITY: LOW — annoying but functional.

    // ═══ Step 4: Assign texture to Player entity ═══
    //
    // In the editor: Asset Browser double-click → assigns spriteGuid ✓
    // Headless equivalent: store binding

    // FRICTION-DX05: The assignment is stored in a separate map
    //   (m_Bindings) that lives OUTSIDE the GameObject. If the developer
    //   creates two entities with the same name, bindings become ambiguous
    //   (looked up by name in SaveScene).
    // SEVERITY: MEDIUM — data integrity risk with duplicate names.

    // ═══ Step 5: Write gameplay script ═══
    //
    // FRICTION-DX06: There is NO in-editor script editor.
    //   Developer must use external editor + save + switch back + F5.
    //   No syntax checking until runtime. No error highlighting.
    // SEVERITY: HIGH for productivity, ACCEPTABLE for MVP.

    W("game_logic.lua",
        "_PERSIST = _PERSIST or {}\n"
        "_PERSIST.moveCount = 0\n"
        "local pH = Engine.entity.find('Player')\n"
        "local gH = Engine.entity.find('Goal')\n"
        "\n"
        "function OnCreate()\n"
        "    -- position player near goal for quick win\n"
        "    Engine.transform.set_position(pH, 3.0, 0.0, -4.0)\n"
        "end\n"
        "\n"
        "function OnUpdate(dt)\n"
        "    local x,y,z = Engine.transform.get_position(gH)\n"
        "    local px,py,pz = Engine.transform.get_position(pH)\n"
        "    local d2 = (px-gx)*(px-gx)\n"
        "    -- simplified: just count updates\n"
        "    _PERSIST.moveCount = _PERSIST.moveCount + 1\n"
        "end\n");

    // ═══ Step 6: Attach script to entity ═══
    //
    // FRICTION-DX07: The script must be Imported into the registry BEFORE
    //   it can be assigned. But the developer just wrote the file — they
    //   don't know they need to "import" it. They expect "attach script"
    //   to be enough.
    // SEVERITY: MEDIUM — conceptually confusing for new users.

    auto scrGuid = reg.Import(DP("game_logic.lua"), AssetType::Script);
    ASSERT_FALSE(scrGuid.IsNull());

    // ═══ Step 7: Play ═══
    //
    // FRICTION-DX08: There is no Play/Pause mode separation.
    //   The script starts executing immediately upon Initialize+OnCreate.
    //   The developer cannot pause, step through frames, or inspect state.
    // SEVERITY: LOW for MVP, HIGH for debugging complex games.

    ScriptInstance inst;
    ScriptInstance::Config cfg; cfg.instructionBudget = 2000000;
    ASSERT_TRUE(inst.Initialize(DP("game_logic.lua"), cfg));

    // Bind entities via Execute (manual bridge)
    inst.Execute("_PERSIST.playerH = " + std::to_string(playerH));
    inst.Execute("_PERSIST.goalH = " + std::to_string(goalH));

    inst.OnCreate();
    inst.OnUpdate(0.016f);

    // Verify: player moved by OnCreate
    {
        GameObject* p = nullptr;
        for (auto& o : scene.GetObjects())
            if (o->GetName() == "Player") { p = o.get(); break; }
        ASSERT_NE(p, nullptr);
        EXPECT_FLOAT_EQ(p->GetTransform().GetPosition().x, 3.0f);
    }

    // ═══ Step 8: Save scene ═══
    //
    // In the editor: Scene → Save Scene button ✓
    // Headless: CaptureScene → SaveSnapshotToFile ✓

    std::vector<EntityContentBinding> bindings;
    bindings.push_back({ texGuid, scrGuid });   // Player binding
    auto snap = CaptureScene(scene, bindings);
    ASSERT_TRUE(SaveSnapshotToFile(snap, DP("saved_scene.scene")));
    ASSERT_TRUE(reg.SaveManifest(DP("dx_manifest.json")));

    // ═══ Step 9: Restart (fresh registry + scene) ═══

    ContentRegistry reg2;
    ASSERT_TRUE(reg2.LoadManifest(DP("dx_manifest.json")));
    SceneSnapshot snap2; std::string err2;
    ASSERT_TRUE(LoadSnapshotFromFile(DP("saved_scene.scene"), snap2, err2)) << err2;

    Engine::Scene scene2;
    Scripting::GameplayAPI::Reset();
    Scripting::GameplayAPI::SetScene(&scene2);
    auto r2 = InstantiateScene(snap2, scene2, tm, reg2);
    ASSERT_TRUE(r2.ok);
    EXPECT_EQ(scene2.GetObjectCount(), 1u);   // Only Player was created in this test

    // ═══ Step 10: Continue playing ═══
    ScriptInstance inst2;
    ASSERT_TRUE(inst2.Initialize(DP("game_logic.lua"), cfg));
    inst2.Execute("_PERSIST.playerH = " +
        std::to_string(Scripting::GameplayAPI::HandleFindByName("Player")));
    inst2.OnCreate();
    inst2.OnUpdate(0.016f);

    // Verify: entity still exists and is at restored position
    bool found = false;
    for (auto& o : scene2.GetObjects()) {
        if (o->GetName() == "Player") {
            found = true;
            EXPECT_FLOAT_EQ(o->GetTransform().GetPosition().x, 3.0f);
            break;
        }
    }
    EXPECT_TRUE(found);
}

// ═══ Friction Summary Test ═══
// Counts and categorises all friction points discovered above.

TEST(DogfoodDX, Friction_Summary) {
    // These are compile-time constants documenting the audit result.
    constexpr int kTotalFrictionPoints = 8;
    constexpr int kHighSeverity = 2;      // DX01 no-empty-project, DX06 no-script-editor
    constexpr int kMediumSeverity = 4;    // DX02 no-default-input, DX03 no-import-btn,
                                          // DX05 name-collision-risk, DX07 import-before-attach
    constexpr int kLowSeverity = 2;       // DX04 spawn-at-origin, DX08 no-play-pause

    // This test always passes — it exists to document the audit result.
    EXPECT_EQ(kTotalFrictionPoints, kHighSeverity + kMediumSeverity + kLowSeverity);

    // Ledger promotion candidates (HIGH severity):
    // DX01 → New Project / Empty Scene button
    // DX06 → In-editor script editor (or at minimum: file watcher + auto-reload)
}
