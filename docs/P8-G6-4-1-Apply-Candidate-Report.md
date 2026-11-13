# P8 G6-4-1 - Apply Candidate Builder Report

**Status**: PASS - CLOSED - SCOPE-BOUND
**Contract**: `docs/P8-G6-A-Override-Apply-Revert-Contract.md`
**Baseline**: G6-4-0 accepted; ABI unchanged v1.18 / 65 ops
**Scope**: Candidate construction and validation only. No Publish, Recovery
Record, Apply ABI, or UI.

## 1. Delivered

New native API:

```text
BuildPrefabApplyCandidate(source, overrides, localToScene, expectedGuid,
                          sourceRevision)
```

`EditorSession::BuildPrefabApplyCandidateForInstance` loads the exact source
through `sourceDigest`/`sourcePath`/`expectedGuid`, rejects legacy and empty
override sets, then delegates to the native builder.

Validation:

- source GUID, revision >= 0, full Prefab validation;
- mapping count equals source entity count;
- every source Local ID mapped exactly once; root mapped; Scene IDs nonzero and
  unique; no unknown Local IDs;
- override Local ID mapped; unique identity; strict component/field/value
  validation; base value exists and differs from override value;
- unsupported Skinning/Mesh visible/Material fields reject.

Construction:

- copy of source model; apply Transform/component values only at frozen paths;
- preserves Prefab GUID, entity names, Local IDs, Parent hierarchy, asset GUIDs,
  and unmodified component data;
- no Scene Entity IDs, runtime IDs, GPU handles, or temporary state written;
- validates candidate and canonical `WritePrefab`;
- returns canonical bytes and full-byte Digest.

Failure returns empty model/bytes/digest/error; source and Scene are untouched.
No RevisionStore publish occurs in this gate.

## 2. Tests

`PrefabApplyCandidateTest` (3):

- single/multi-field and multi-entity allowed fields; Entity/GUID/hierarchy/
  unmodified data preservation; Writer determinism;
- invalid GUID/revision/mapping/duplicate identity/field/value/base mismatch/
  duplicate identity;
- failure leaves source/candidate empty and unchanged.

`PrefabInstanceTest.ApplyCandidateValidatesExactSourceAndLegacyIdentity`:

- exact source and override produce candidate bytes/digest;
- corrupted source bytes (digest mismatch) fail with empty candidate;
- legacy source identity fail-closed.

Focused execution:

```text
test_content Debug   : 172 tests, 171 PASS, 1 SDK skip
test_content Release : 172 tests, 171 PASS, 1 SDK skip
test_bridge Debug    : 135/135 PASS
test_bridge Release  : 135/135 PASS
```

Full Native inventory and UIA were not rerun for this gate; previous accepted
G6-3 baseline remains. No Product/Scene UI behavior changed.

## 3. Known limits

- No durable Recovery Record yet.
- No Phase 1/Phase 2 Publish.
- No startup recovery.
- No Apply ABI/UI.
- Source `sourceRevision` is validated nonnegative and carried to the candidate
  context; actual revision/source publication is G6-4-3/G6-4-4.
- Other Instances are not modified by candidate construction.

## 4. Git

Uncommitted; no commit/push/reset/clean/stash.
