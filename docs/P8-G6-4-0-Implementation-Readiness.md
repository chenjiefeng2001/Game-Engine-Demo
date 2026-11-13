# P8 G6-4-0 - Apply / Recovery Implementation Readiness

**Status**: ACCEPTED - READINESS COMPLETE - SCOPE-BOUND
**Contract**: `docs/P8-G6-A-Override-Apply-Revert-Contract.md`
**Baseline**: G6-3 accepted; ABI v1.18 / 65 ops
**Source changes in G6-4-0**: NONE
**Tests in G6-4-0**: NONE

## 1. Verified current interfaces

| Requirement | Current implementation | Readiness |
|---|---|---|
| Override model | `PrefabOverrideRecord`, `AddOrUpdatePrefabOverride`, base-value reading | Reusable |
| Instance anchor/local ID | `Scene::FindPrefabInstanceForEntity`, `localToScene` | Reusable |
| Strict source read | `EditorSession::LoadPrefabSourceForInstance` (digest/path/GUID) | Reusable, currently private |
| Candidate serialization | `WritePrefab`, `ValidatePrefabModel` | Reusable |
| Material-style asset rebinding | content-addressed file + candidate registry + Publish | Reusable pattern |
| RevisionStore | `Publish(manifest, scene)` with 8 fault points | Reusable, two Publish calls required |
| Atomic file/record | `WriteFileAtomic`, `CopyFileNoOverwrite` | Reusable |
| Startup order | RevisionStore.Open -> manifest load -> scene load/Instantiate | Recovery hook must run before manifest/scene consumption |
| ABI | `GET_PREFAB_INSTANCE_JSON=63`, `REVERT_OVERRIDE=64`, count65 | v1.19/66 Apply extension is additive |

### 1.1 Startup hook constraint

`EditorSession::OpenProjectInternal` currently:

```text
ProjectRoot -> RevisionStore::Open -> LoadManifest -> LoadSnapshotFromFile
           -> InstantiateScene -> expose editing
```

The Apply recovery hook must run after `RevisionStore::Open` and before manifest/
scene consumption. If recovery publishes, the session must then use the store's
updated committed paths/revision, not the pre-recovery `RevisionOpenResult`.
No edit operation may be accepted before recovery resolves to `clean` or
`RecoveryRequired`.

### 1.2 Current boundary correction

After Phase 1, `current` legitimately advances to the new revision: the new
Prefab is committed in the Manifest, while the Scene still contains old Instance
metadata. This is an intended intermediate state, not an atomic failure. The old
revision directory remains immutable. Phase 2 advances to a second revision with
the updated Scene. The Recovery Record must therefore record both actual
publication results, not claim the current pointer never changes.

## 2. Unified two-phase state and recovery semantics

### 2.1 Record states (non-authoritative hints)

```text
Prepared
PrefabPublished
ScenePublished
RecoveryRequired
```

Record state is only a hint. Recovery decisions must be based on authoritative
bytes:

1. current revision number/hash and its committed manifest/scene;
2. Prefab GUID -> path in that manifest;
3. actual Prefab file digest/path;
4. Scene instance metadata (`sourceRevision`, `sourceDigest`, `sourcePath`,
   overrides, syncState);
5. Recovery Record transaction identity/integrity.

A record saying `Prepared` must not override evidence that Phase 1 already
committed. A record saying `PrefabPublished` must not override actual current
state.

### 2.2 Publication meaning

| Point | Current manifest | Current Scene | Required action |
|---|---|---|---|
| Before Phase 1 | old Prefab | old metadata | keep old; record may be removed |
| Phase 1 success | new Prefab | old metadata | continue Phase 2 |
| Phase 2 success | new Prefab | new metadata | verify + clear record |
| Unknown/neither | mismatch | mismatch | RecoveryRequired |

- `sourceRevision` after Apply is the Phase 1 project revision (the revision
  containing the new Prefab asset revision), not the later Phase 2 Scene revision.
- Other instances of the same Prefab GUID are not changed.
- Prefab GUID is stable; digest/path change.
- Overrides are cleared only when Phase 2 is durable.

### 2.3 Recovery matrix

| Evidence | Action |
|---|---|
| Current old Prefab + old Scene, record Prepared | safe cancel: remove record, keep old |
| Current new Prefab + old Scene, any record state | continue Phase 2 |
| Current new Prefab + new Scene, any record state | verify identity, remove record |
| Current neither expected state | RecoveryRequired |
| Record corrupt/unknown version/integrity fail | RecoveryRequired |
| Instance mapping/source identity mismatch | RecoveryRequired |
| Expected Prefab bytes missing or digest mismatch | RecoveryRequired |
| Current readback unknown | RecoveryRequired |

Recovery is roll-forward where possible and idempotent. Repeated startup with the
same record must not create duplicate revisions or change other instances.
RecoveryRequired blocks Apply/Revert and ordinary writes; it does not delete old
revisions or candidate files.

## 3. Per-stage exit gates

| Gate | Work | Exit gate (must all pass) |
|---|---|---|
| G6-4-0 | Readiness | authoritative state model frozen; startup hook located; no unresolved contract conflict; no source changes |
| G6-4-1 | Candidate builder | source/GUID/digest/path/mapping/base-value validated; candidate built with stable Local IDs/names/hierarchy; Writer bytes+digest; failures leave source and Scene untouched; native tests |
| G6-4-2 | Recovery Record | schema/version/integrity strict read/write; atomic create/update; corrupt/unknown record rejected; record path confined to `.engine-content/prefab-apply`; native tests |
| G6-4-3 | Phase 1 Publish | new content-addressed Prefab file; same GUID; candidate manifest + old Scene; `PrefabPublished`/actual revision recorded; all Publish fault points handled; old revision bytes intact; no duplicate Apply |
| G6-4-4 | Phase 2 Publish | updated Instance metadata + cleared overrides published with candidate manifest; Phase 1 data remains intact on failure; only success after consistency verification; no other instance modified |
| G6-4-5 | Startup recovery | hook runs before exposed editing; all crash points classified from actual bytes; roll-forward/cancel/RecoveryRequired behavior; startup idempotent; targeted restart tests |
| G6-4-6 | ABI/UI | additive op65, v1.19/count66; Apply disabled for legacy/no-overrides/recovery/conflict; success shows new source revision; no ordinary Undo claim |
| G6-4-7 | Fault/closure | focused fault matrix, candidate/record/phase1/phase2/restart/idempotency tests; regression; docs; no new stable failure |

## 4. Execution checklist

### G6-4-0
- [x] Map source/override/instance APIs.
- [x] Identify startup hook and post-recovery path reload requirement.
- [x] Define authoritative two-phase evidence and RecoveryRequired boundaries.
- [ ] Accept readiness record.

### G6-4-1
- [ ] Add reusable source loader/candidate builder.
- [ ] Validate every Override identity/field/value and base value.
- [ ] Apply values to a copy of the source model only.
- [ ] Validate + canonical Writer + digest.
- [ ] Test single/multi field, multi entity, five component types, duplicate,
  invalid path, base mismatch, legacy, digest mismatch, Writer failure.

### G6-4-2
- [ ] Define record version/schema/integrity hash.
- [ ] Atomic create/update under `.engine-content/prefab-apply/`.
- [ ] Strict loader rejects corrupt/unknown/incomplete records.
- [ ] Test write failure, corruption, unknown version, deletion behavior.

### G6-4-3
- [ ] Stage new Prefab content-addressed file; never overwrite old bytes.
- [ ] Rebind same GUID to candidate path in candidate Manifest.
- [ ] Publish candidate Manifest with old committed Scene.
- [ ] Record `phase1Revision` or unambiguous failure.
- [ ] Run all 8 RevisionFaultPoint timings; old revision bytes/current semantics
  checked.

### G6-4-4
- [ ] Build updated committed Scene metadata from old committed Scene bytes.
- [ ] Publish candidate Manifest + updated Scene.
- [ ] Verify new Prefab digest/path/GUID and Instance metadata.
- [ ] Clear overrides only after commit; keep all other instance metadata.
- [ ] Run Phase 2 fault points and failure retention checks.

### G6-4-5
- [ ] Install recovery before Manifest/Scene consumption.
- [ ] Classify by actual committed bytes, not record state alone.
- [ ] Handle all crash points in G6-4 plan section 4D.
- [ ] Make repeated recovery idempotent.
- [ ] Enter RecoveryRequired on ambiguity; block Apply/Revert/writes.
- [ ] Add restart tests, including missing/corrupt record.

### G6-4-6
- [ ] Add `APPLY_PREFAB_OVERRIDES=65`, v1.19/count66, C export, Dispatch,
  Managed service, Inspector Apply.
- [ ] Disable Apply when legacy/no overrides/recovery/conflict.
- [ ] Show override count and post-Apply source revision.
- [ ] Keep Apply outside Scene Snapshot Undo.

### G6-4-7
- [ ] Run focused Native/Content/Bridge tests.
- [ ] Run Debug/Release UIA Apply success and failure/recovery feedback.
- [ ] Record counts and historical PICK-01/SDK skip separately.
- [ ] Write implementation, recovery, fault, test, closure docs.
- [ ] Confirm no source changes outside G6-4 scope.

## 5. Blocking decisions before code

1. Recovery Record exact integrity hash and path canonicalization.
2. Recovery hook must reload manifest/Scene after any recovery Publish.
3. Candidate Manifest must be reused for Phase 2; Phase 1 revision is the
   Instance `sourceRevision`.
4. Apply is disabled for `legacySourceIdentity`, `RecoveryRequired`, `conflict`,
   empty overrides, and source revision/digest/path mismatch.
5. A source conflict is a G7 concern; G6 must not auto-bind latest or silently
   reconcile.

No G6-4 production code should start until these decisions are accepted.
