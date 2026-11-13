# P8 G6-4-5 - Startup Recovery Closure

**Status**: PASS - CLOSED - SCOPE-BOUND
**Contract**: `docs/P8-G6-A-Override-Apply-Revert-Contract.md`
**Report**: `docs/P8-G6-4-5-Startup-Recovery-Report.md`

## 1. Delivered

- Startup recovery hook after RevisionStore.Open and before Manifest/Scene
  consumption/edit availability.
- Actual-byte-based Phase 1/Phase 2 reconstruction and record cleanup.
- Prepared cancellation, Phase 1 completion, Phase 2 completion, and
  RecoveryRequired paths.
- Idempotent repeated startup.
- Updated committed path consumption after recovery Publish.
- Tests for complete recovery, retry after Phase 2 failure, corrupt record.

## 2. Evidence

```text
Native Debug/Release : 1282 executed, 1280 PASS, 1 PICK-01, 1 SDK skip
test_content         : 176 tests, 175 PASS + 1 SDK skip
test_bridge          : 142/142 Debug and Release
UIA                  : not rerun; no UI behavior changed
```

## 3. Limits

Apply ABI/UI remains G6-4-6. No source reconciliation (G7). Unknown states fail
closed with RecoveryRequired.

## 4. Git

Uncommitted; no commit/push/reset/clean/stash.
