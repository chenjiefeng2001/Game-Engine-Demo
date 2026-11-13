# P8 G7-0 - Source Reconciliation Readiness Map

**Status**: ACCEPTED - READ-ONLY COMPLETE
**Parent contract**: `docs/P8-G6-A-Override-Apply-Revert-Contract.md`
**Baseline**: ABI v1.19 / 66 ops; G6 CLOSED
**Source changes in G7-0**: NONE
**Tests in G7-0**: NONE

## 1. Source identity facts

Prefab Instance metadata currently stores:

```text
prefabGuid
sourceRevision
sourceDigest
sourcePath
legacySourceIdentity
localToScene
overrides
syncState
```

- `sourceDigest` is the full G4-2 canonical Prefab digest.
- `sourcePath` points directly at the historical content-addressed
  `.prefab.json`; old files are not deleted by later Apply.
- The current ContentRegistry/manifest is the authoritative latest GUID ->
  path lookup.
- `sourceRevision` is the project revision at instantiate/Phase 1 Apply; it is
  an advisory identity label, not a content lookup key. The exact old source
  must be read through `sourcePath` and verified with `sourceDigest`.
- A missing/corrupt old path or digest mismatch must fail closed; latest must
  never substitute for old.
- `sourcePath` content is outside RevisionStore current hashes, so source
  history must be read and independently validated.

## 2. Instance / mapping facts

- The anchor is the Root Scene Entity ID.
- `localToScene` maps Prefab Local ID -> Scene Entity ID.
- `SetPrefabInstanceMetadata` validates that every mapped Scene Entity exists
  under the anchor, but it does **not** require every actual subtree entity to be
  mapped. G7 must validate actual subtree equality before reconciliation.
- G6 Revert instance already checks `subtreeScenes == mappedScenes`; this check
  must be reused for G7.
- Extra local entities under the instance root, duplicate mappings, zero IDs,
  missing mapped entities, and unmapped subtree entities are structural
  conflicts, not auto-deletion input.

## 3. Local state / unsupported edits

G6 tracks only the frozen Override fields:

- Transform local scalar components;
- Mesh `meshGuid`;
- Camera fields;
- Collider fields;
- RigidBody fields.

Entity Name, parent pointers, component presence/removal, and unsupported
component data are not covered by G6 Overrides. G7 must detect these local
structural edits and fail/conflict rather than silently discard or overwrite
them. Skinning, Mesh `visible`, Material/Texture fields remain unsupported.

## 4. Current APIs and transaction boundary

Reusable:

- `Engine::Content::ReadPrefab` strict reader;
- `Engine::Content::BuildPrefabApplyCandidate`;
- `Engine::Content::ValidatePrefabModel`;
- `EditorSession::LoadPrefabSourceForInstance`;
- `Scene::FindPrefabInstanceForEntity`;
- `Scene::SetPrefabInstanceMetadata`;
- Scene v2 `CaptureSceneById`/`InstantiateScene`;
- P7-B Snapshot History;
- `RevisionStore` committed Scene reads.

Missing:

- latest-versus-instance status query;
- old/latest/local source resolver model;
- three-way diff model;
- conflict persistence and resolution;
- structural reconciliation (add/delete/parent/name/component-set);
- G7 ABI and UI.

## 5. Readiness findings

1. **Old Source is recoverable only by sourcePath/digest**, not by revision
   number. Latest cannot be used as Base.
2. **Mapping completeness is not guaranteed by existing metadata setter**.
   G7 must add a subtree/mapping equality check before diff/execution.
3. **Entity structure and Name are untracked local state**; G7-A must freeze
   conflict rules and persistence before implementation.
4. **Prefab files are outside current hashes**; G7 must validate Base and Latest
   files independently.
5. **Apply/Recovery must not be reused as reconciliation commit**. G7 changes
   only the Scene Instance; it does not publish a Prefab Asset revision.
6. **Latest is not automatically safe**: current registry path can differ from
   instance Source Identity; status is derived from both.
7. **G7 ABI is not yet defined**; prior ABI v1.19/66 remains untouched until
   G7-A freezes operation shapes.

## 6. G7-0 exit result

Inputs: accepted G4-G6 baseline and current code paths.

Outputs: this map plus the G7-A decision list.

Fail conditions if any of these remain unresolved:

- Base vs Latest source identity semantics;
- structural conflict policy;
- conflict persistence schema;
- reconciliation transaction boundary;
- ABI/UI operation set.

The next step is G7-A contract freeze, not G7-1 implementation.
