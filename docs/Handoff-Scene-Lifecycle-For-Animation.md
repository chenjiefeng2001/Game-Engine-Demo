# Handoff: scene update and draw target different objects, so animation cannot play

**Owner:** HRC owner of `engine/src/Editor/EngineEditor.cpp`
**Raised by:** Animation skinned-mesh product integration on branch `avalonia`
**Commits:** `a1e962a` (component clone contract), `748cc68` (skinned draw branch), `982d9ff` (shared draw implementation)
**Status:** awaiting a decision on the correct update and preview semantics

---

## 1. What is already working

The rendering side of skinned meshes is connected and verified. None of this
requires any change from you:

- `SkinningComponent` now survives the production scene clone. It has a stable
  contract type name, serialises its skeleton, skinned mesh and named animation
  tracks, and is registered with `ComponentRegistryGo` (`a1e962a`). Before this,
  `CaptureScene` skipped it (no type name) and `InstantiateScene` did not
  rebuild it, so the Play clone had no skeleton, no mesh and no timeline.
- `EditorDemoApp::RenderGameObject()` draws skinned meshes through the product
  viewport path, taking matrices from `GetSkinningMatrices()` (`748cc68`).
- The product path and a headless pixel test share one implementation,
  `Engine::DrawSkinnedMeshIndexed` (`982d9ff`). Verified against a real OpenGL
  context and an offscreen FBO: bind pose and translated pose both cover the
  centre band and produce different pixels. `test_renderer` is 14/14 and the
  committed suite is 13/13.

**This does not mean animation plays in the editor.** The pixel evidence supplies
bone matrices directly; it does not prove that scene updates drive them.

## 2. The open problem

The scene that is updated is not the scene that is drawn.

| Mode | Scene receiving `Update(dt)` | Scene drawn by `RenderActiveScene()` |
|---|---|---|
Edit | **none** | `m_EditScene` |
Play | `m_EditScene` | `m_Runtime` |

Relevant code, `engine/src/Editor/EngineEditor.cpp:466-472`:

```cpp
void EngineEditor::OnUpdate(float32 dt) {
    Scene* scene = m_SceneManager.GetScene();
    if (!scene) return;
    if (m_SceneManager.IsPlaying()) scene->Update(dt);
    else if (m_SceneManager.IsPaused() && m_SceneManager.IsStepRequested()) {
        scene->Update(dt);
        m_SceneManager.ConsumeStepRequest();
    }
    m_Viewport.OnUpdate(dt, true);
}
```

`m_SceneManager.GetScene()` is the edit scene. `RenderActiveScene()`
(`sandbox/src/EditorDemo/EditorDemoApp.h`) draws
`m_GP01.RenderScene()`, which is `m_Playing ? m_Runtime : m_EditScene`.

Consequences:

- **Edit mode**: the skinned mesh is drawn, but nothing advances the pose, so it
  stays at the bind pose.
- **Play mode**: the pose advances on the edit scene while the runtime clone is
  drawn, so the two are unrelated.

`m_EditScene`, `m_Scene` and `RenderScene()` in edit mode are the same object
(`EditorDemoApp.h` assigns `m_Scene` from the GP01 scene-replaced callback), so
the edit-mode case is purely a missing update, not an identity mismatch.

## 3. What we need you to decide

1. **Which scene Play mode should update.** The rendered object during Play is
   the runtime clone, so advancing the edit scene cannot drive what is on screen.
   The runtime clone does now carry a valid `SkinningComponent` (`a1e962a`), so
   updating it is viable.

2. **Whether Edit mode needs a preview update, and if so how.** If animation
   should be visible without entering Play, a separate preview mechanism is
   needed. Note that `Scene::Update` drives components generally, not just
   skinning.

## 4. What we explicitly did not do

`Scene::Update(dt)` was **not** made unconditional in Edit mode. It drives
components generally — scripts, physics and anything else attached — so enabling
it unconditionally would silently change existing editor behaviour beyond
animation. Picking between "Play updates the runtime scene", "Edit gets a
preview-only update path" and "Edit stays static" is an editor semantics
decision, not a rendering one.

No HRC-owned file was modified. The remaining gap is the lifecycle wiring
described above; the rendering code needs no further change once the correct
update target is in place.

## 5. How to verify afterwards

- Build `EditorDemo` and confirm the skinned actor animates in the viewport.
- The headless product draw test (`GPUSkinningPixelTest.ProductDrawBranchChangesPixelsWithPose`)
  must continue to pass; it is independent of the lifecycle decision.
- Committed suite must stay at 13/13.
