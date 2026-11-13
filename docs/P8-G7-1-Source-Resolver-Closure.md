# P8 G7-1 - Prefab Source Resolver & Snapshot Loader Closure

**Status**: PASS - SCOPE-BOUND - PENDING ACCEPTANCE
**Contract**: `docs/P8-G7-A-Source-Reconciliation-Contract.md`
**Scope**: Base/Latest/Local source snapshots and strict identity/mapping validation.
No three-way diff, conflict model, executor, ABI, or UI.

## 1. Delivered

New native data model (`PrefabReconciliationSources`):

- `PrefabSourceSnapshot` (GUID, projectRevision, contentDigest, contentPath,
  `PrefabModel`);
- `PrefabLocalInstanceSnapshot` (anchor, Local ID -> Scene ID map, local entities,
  overrides, syncState);
- local entity snapshots with Name, Parent Local ID, Local Transform and raw
  component payloads; unsupported components remain represented for later
  structural conflict detection, not silently dropped.

`EditorSession::LoadPrefabReconciliationSources`:

1. rejects missing/legacy instance metadata;
2. reads Base through persisted `sourcePath`, verifies `sourceDigest` and GUID,
   and strict Prefab Reader;
3. reads Latest through current registry path, computes full-byte Digest,
   verifies GUID and strict Prefab Reader;
4. builds Local by verifying anchor/subtree/mapping equality, unique Local IDs,
   unique Scene IDs, parent mapping, and actual entity/component data;
5. returns `outOfDate` when Base digest/path differs from Latest.

No latest fallback, no revision-number lookup, no Scene or Prefab mutation.
Base/Latest source identity failure returns false with explicit diagnostic.

## 2. Tests

`PrefabInstanceTest.ReconciliationSourcesReadBaseLatestAndLocal`:

- two independent Instances; Apply changes the latest Prefab path for one;
- second Instance resolves old Base and new Latest;
- `outOfDate=true`, source GUIDs match, local anchor/entities/parent map are
  preserved;
- missing Base bytes fail closed rather than using Latest.

`PrefabInstanceTest.ReconciliationSourcesRejectExtraSubtreeEntity`:

- extra Scene entity under the Instance anchor makes subtree/mapping mismatch
  fail closed.

Focused execution:

```text
test_content Debug/Release : unchanged 176 tests, 175 PASS + 1 SDK skip
test_bridge Debug/Release  : 147/147 PASS
Native full inventory      : 1287 executed, 1285 PASS, 1 PICK-01, 1 SDK skip
UIA                        : not rerun; no UI/ABI change
Managed static             : 385
```

## 3. Limits

- No three-way diff or conflict classification yet.
- Local snapshot retains unsupported component payloads for later conflict
  handling; it does not give them Override support.
- Latest `projectRevision` is the current committed project revision; Digest
  and Path remain authoritative.
- No ABI/EngineHost/Managed exposure yet.

## 4. Git

Uncommitted; no commit/push/reset/clean/stash.