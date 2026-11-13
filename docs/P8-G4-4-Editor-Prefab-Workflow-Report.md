# P8 G4-4 - Minimal Editor Prefab Workflow

**Status**: IMPLEMENTED - PENDING ACCEPTANCE
**Contract**: `docs/P8-G4-Prefab-Format-Contract.md` / ABI v1.15
**Scope**: Hierarchy Create Prefab, AssetBrowser Prefab display/filter/select,
dependency view, real Debug/Release UIA. No Instantiate/Override/Apply.

## 1. UI

Hierarchy adds:

- `PrefabNameBox` (blank defaults to selected entity name);
- `CreatePrefabBtn` — calls ABI op 60 for the selected Scene Entity ID;
- fixed `target: assets/prefabs` display, matching frozen G4-A A3/A16;
- `PrefabDiagnostic` for capture/publish failures.

Success:

- no Scene entity is deleted or replaced;
- hierarchy/selection remain;
- Asset Browser reloads and the new Prefab asset is selected;
- type, path, GUID, and availability are visible in the existing asset row.

Failure:

- diagnostic is shown in the Hierarchy panel and Console;
- Scene/hierarchy remain unchanged;
- no asset is registered on Publish failure.

The storage directory is fixed by the frozen contract. G4-4 does not add an
arbitrary target-path field because ABI v1.15 accepts only source Entity ID and
asset name.

## 2. Asset Browser

- Prefab type display and `Prefab` filter entry.
- Existing Name/Type/Path/Availability/GUID row remains the single display.
- Selection and dependency query use the existing asset index flow.
- No Texture/Script/Mesh/Material behavior changes.

## 3. Dependency view

For a Prefab:

- direct Mesh component GUIDs appear as `Mesh` dependencies;
- missing Mesh GUIDs show `[UNRESOLVED]` with an empty path, not a guessed path;
- Mesh dependency queries include Prefab dependents.

## 4. UIA coverage

New Debug/Release tests:

1. `Prefab_create_publishes_asset_and_shows_dependency`
   - select a Scene entity with a Mesh component;
   - create `UiPrefab`;
   - verify source hierarchy unchanged;
   - verify Asset Browser row contains Prefab type, content-addressed path,
     32-hex GUID, and Available status;
   - verify direct Mesh dependency is shown as unresolved.
2. `Prefab_create_failure_leaves_scene_and_reports_error`
   - source entity has a legacy Script binding;
   - create fails with a visible legacy-binding diagnostic;
   - hierarchy remains unchanged.
3. `Prefab_publish_failure_leaves_scene_unchanged`
   - RevisionStore candidate creation is forced to fail;
   - visible failure diagnostic;
   - hierarchy remains unchanged.

All 13 UIA tests pass in Debug and Release. G4-4 does not test Instantiate or
Override because those are G5/G6.

## 5. UI refresh correction

The success UIA exposed an existing Hierarchy ListBox refresh defect: `HierarchyRows`
was appended without being cleared after the G2 TreeView-to-ListBox switch. This
produced duplicate rows after refresh. `RebuildHierarchy()` now clears
`HierarchyRows` before rebuilding. This is a G2 UI regression fix, not a new
feature, and is covered by all hierarchy UIA tests.

## 6. Explicit non-goals

- Prefab Instantiate, instance metadata, Local ID -> Scene Entity ID mapping.
- Overrides, Apply/Revert, recovery.
- Source update/reconciliation.
- Arbitrary target path outside `assets/prefabs`.
