# P8 G4-4 - Minimal Editor Prefab Workflow Closure

**Status**: PASS - CLOSED - MINIMAL EDITOR PREFAB WORKFLOW VERIFIED - SCOPE-BOUND
**Contract**: `docs/P8-G4-Prefab-Format-Contract.md`
**Uses**: ABI v1.15 / `CREATE_PREFAB_FROM_ENTITY = 60`

## 1. Delivered

- Hierarchy `PrefabNameBox`, `CreatePrefabBtn`, fixed target display, and
  `PrefabDiagnostic`.
- Calls the existing G4-3 Create Prefab ABI for the selected entity.
- Defaults blank name to the selected entity name.
- On success reloads assets and selects the new Prefab asset; source Scene and
  hierarchy are unchanged.
- On capture/publish failure displays a diagnostic and leaves the Scene
  unchanged.
- Asset Browser Prefab type display, filter entry, selection, path/GUID/status
  display.
- Prefab dependency view: direct Mesh, missing Mesh `[UNRESOLVED]`.
- Fixed a latent G2 hierarchy refresh bug: `HierarchyRows` is now cleared before
  rebuilding, so refresh does not append duplicate ListBox rows.

## 2. UIA evidence

Debug and Release, 13/13 each:

- successful Create Prefab + Asset Browser + GUID/path/status + dependency;
- legacy-binding capture failure with unchanged hierarchy;
- RevisionStore Publish failure with unchanged hierarchy.

The success/failure tests use real `AvaloniaEditor.exe` and UIA, not only
Managed/ABI unit tests.

## 3. Boundary

- Storage path is the frozen `assets/prefabs`; no arbitrary path field was added
  because ABI v1.15 accepts only entity ID + asset name.
- No Prefab Instance, instance metadata, Local ID mapping, Override, Apply,
  Revert, recovery, or source update.
- Source entities are never replaced by Prefab instances.
- No ABI/operation change in G4-4 beyond using the G4-3 v1.15 surface.

## 4. Verification

- Native Debug/Release: 1256 executed, 1254 PASS, 1 PICK-01, 1 SDK skip.
- `test_content`: 165, 164 PASS + 1 skip.
- `test_bridge`: 127/127.
- UIA: 13/13 Debug and Release.
- Managed static: 384.

No new stable failures. The known GPU transient is retained in test history and
is not reclassified.

## 5. Git

G4-4 changes are uncommitted. No commit, push, reset, clean, or stash was
performed.
