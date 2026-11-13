# P8 G6-1 - Override Metadata & Field Path Model

**Status**: PASS - CLOSED - SCOPE-BOUND
**Contract**: `docs/P8-G6-A-Override-Apply-Revert-Contract.md`
**Scope**: Source Identity, Override records, stable field paths, value
validation, base comparison, Scene v2 persistence, Undo/Redo/cold restart.
**Excluded**: Inspector write tracking (G6-2), Revert UI/API (G6-3), Apply and
Recovery execution (G6-4).

## 1. Delivered

### Source identity

`PrefabInstanceMetadata` now carries:

- `sourceRevision`;
- `sourceDigest` (16-hex canonical Prefab Digest);
- `sourcePath` (content-addressed `.prefab.json`);
- `legacySourceIdentity`;
- `overrides`;
- `syncState` (`Clean`, `Overrides`, `ApplyPending`, `RecoveryRequired`,
  `Conflict`).

`InstantiatePrefab` fills complete source identity from the committed Prefab
asset bytes/path. Legacy G5 Scene metadata without digest/path loads as
`legacySourceIdentity`; it never silently binds to the latest Prefab.

### Override model

New native types:

- `PrefabOverrideRecord`;
- `PrefabOverrideValueType`;
- `PrefabOverrideFieldSpec`;
- `PrefabSyncState`.

New field/path functions:

- `ResolvePrefabOverrideField`;
- `ValidatePrefabOverrideValue`;
- `ValidatePrefabOverrideRecord`;
- `ReadPrefabOverrideBaseValue`;
- `PrefabOverrideMatchesBase`;
- `AddOrUpdatePrefabOverride`;
- `RemovePrefabOverride`;
- `NormalizePrefabOverrides`.

Allowed fields are exactly the G6-A allowlist. Skinning, Mesh `visible`, and
Material/Texture fields are rejected. Transform uses Local quaternion component
paths. Values are scalar bool/int/finite-number/string only.

`AddOrUpdatePrefabOverride` removes all matching identity records, removes the
record when it equals the Prefab base value, and keeps canonical ordering.

### Scene v2 persistence

`prefabInstance` now persists:

- sourceRevision/sourceDigest/sourcePath;
- overrides with exact `localId/component/field/value`;
- syncState.

Strict load validation rejects malformed source identity, duplicate/unsupported
overrides, unmapped Local IDs, legacy instances with overrides, and sync-state
inconsistency. Capture and Undo/Redo preserve the metadata.

## 2. Tests

`PrefabOverrideTest` (4 tests):

- field allowlist/value validation;
- add/update/remove/base comparison;
- Scene v2 source/override round-trip;
- legacy G5 source identity.

`PrefabInstanceTest.OverrideMetadataSurvivesUndoRedoAndColdRestart`:

- metadata override added in one Snapshot transaction;
- Undo removes it, Redo restores it;
- Save/cold reopen preserves it.

Full evidence:

```text
Native Debug   : 1264 executed, 1262 PASS, 1 PICK-01, 1 SDK skip
Native Release : 1264 executed, 1262 PASS, 1 PICK-01, 1 SDK skip
test_content   : 169 tests, 168 PASS + 1 SDK skip
test_bridge     : 131/131 Debug and Release
UIA Debug       : 13/13
UIA Release     : 13/13 (one transient UndoSceneBtn timeout passed on clean rerun)
Managed static  : 384
```

## 3. Known limits

- No Inspector automatic override tracking yet.
- No Revert/Apply ABI or recovery execution yet.
- No ABI v1.17 operations yet.
- No source update/reconciliation.
- Skinning/Mesh visible/Material overrides remain unsupported.

## 4. Git

G6-1 changes are uncommitted. No commit, push, reset, clean, or stash was performed.
