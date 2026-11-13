# P8 G6-4-1 - Apply Candidate Builder Closure

**Status**: PASS - CLOSED - SCOPE-BOUND
**Contract**: `docs/P8-G6-A-Override-Apply-Revert-Contract.md`
**Report**: `docs/P8-G6-4-1-Apply-Candidate-Report.md`

## 1. Delivered

- Native `BuildPrefabApplyCandidate`.
- EditorSession exact-source/legacy/empty-override validation wrapper.
- Strict mapping/base/field/value checks.
- Candidate model validation, canonical Writer bytes and full-byte Digest.
- Failure isolation: empty candidate, no source/Scene mutation.
- Focused tests for allowed fields, identity preservation, duplicate/invalid
  fields, mapping/GUID/revision failure, source digest mismatch, legacy
  fail-closed.

## 2. Evidence

```text
test_content Debug/Release   : 172 tests, 171 PASS + 1 SDK skip
test_bridge Debug/Release    : 135/135 PASS
Full Native inventory/UIA    : not rerun (no UI/ABI change; selective policy)
```

## 3. Limits

No Recovery Record, Phase 1/Phase 2, startup recovery, or Apply ABI/UI.

## 4. Git

Uncommitted; no commit/push/reset/clean/stash.
