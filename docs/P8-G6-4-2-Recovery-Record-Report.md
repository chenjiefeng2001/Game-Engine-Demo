# P8 G6-4-2 - Durable Recovery Record Report

**Status**: PASS - CLOSED - SCOPE-BOUND
**Contract**: `docs/P8-G6-A-Override-Apply-Revert-Contract.md`
**Scope**: Durable Apply Recovery Record only. No Phase 1/2 Publish, startup
recovery, Apply ABI, or UI.

## 1. Delivered

New native `Engine::Content::PrefabApplyRecord`:

```text
transactionId
recordVersion = 1
state = prepared | prefab_published | scene_published | recovery_required
prefabGuid
oldSourceRevision / oldSourceDigest / oldSourcePath
newDigest / newPath / newSourceRevision
instanceRootEntityId
oldInstanceMetadata / newInstanceMetadata JSON objects
oldRevisionSnapshot
phase1Revision / phase2Revision
integrityHash
```

APIs:

- `ValidatePrefabApplyRecord`;
- `ComputePrefabApplyRecordIntegrity`;
- `SerializePrefabApplyRecord`;
- `ParsePrefabApplyRecord`;
- `WritePrefabApplyRecord`;
- `ReadPrefabApplyRecord`;
- `RemovePrefabApplyRecord`;
- `PrefabApplyRecordPath`.

Storage:

```text
<ProjectRoot>/.engine-content/prefab-apply/<prefabGuid>.<rootEntityId>.json
```

Rules implemented:

- exact top-level schema, unknown/missing fields reject;
- recordVersion 1 only;
- canonical state strings; invalid state rejects;
- canonical GUID/digest/source path validation;
- source paths must be project-relative `assets/...prefab.json` with no
  traversal;
- phase/state consistency and transaction ID validation;
- instance metadata must be JSON objects;
- integrity hash covers canonical record body (excluding hash), using full
  `RevisionStore::HashBytes`;
- strict parse recomputes and compares integrity;
- atomic create/update via `WriteFileAtomic`;
- implicit recovery directory creation;
- read/remove bound to generated path and identity;
- damaged/unknown records fail closed.

No `RevisionStore::Publish` is invoked in this gate.

## 2. Tests

`PrefabApplyRecordTest` (4):

- write/read round-trip and atomic update;
- unknown field, missing integrity, unsupported version, invalid state,
  altered integrity, malformed/trailing JSON;
- invalid phase/state, new source revision, unsafe paths, malformed digest,
  invalid transaction ID;
- recovery-directory failure, remove/read-after-remove.

Focused execution:

```text
test_content Debug   : 176 tests, 175 PASS, 1 SDK skip
test_content Release : 176 tests, 175 PASS, 1 SDK skip
```

`test_bridge`/UIA/Native full inventory were not rerun: this gate adds no ABI,
managed UI, Scene/EngineHost dispatch, or runtime behavior.

## 3. Known limits

- Record producer/consumer integration is not implemented yet.
- No Phase 1/Phase 2 Publish or startup recovery.
- No Apply ABI/Inspector button.
- Instance metadata content schema beyond "object" is owned by G6-4-3/G6-4-4.
- RecoveryRequired/write-blocking behavior is not wired to EditorSession yet.

## 4. Git

Uncommitted; no commit/push/reset/clean/stash.
