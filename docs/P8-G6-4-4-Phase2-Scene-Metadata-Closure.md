# P8 G6-4-4 - Phase 2 Scene Metadata Publish Closure

**Status**: PASS - CLOSED - SCOPE-BOUND
**Contract**: `docs/P8-G6-A-Override-Apply-Revert-Contract.md`
**Report**: `docs/P8-G6-4-4-Phase2-Scene-Metadata-Report.md`

## 1. Delivered

- Phase 1 revision/candidate digest/path validation before Phase 2.
- Committed Scene metadata update only for the target Instance anchor.
- Candidate Manifest + updated Scene Publish.
- Source revision/digest/path update, override clearing, clean sync state.
- Other Instance metadata and Scene identity unchanged.
- Record success cleanup and failure retention/RecoveryRequired semantics.
- Save/cold reopen and Phase 2 safe-failure tests.

## 2. Evidence

```text
Native Debug/Release : 1279 executed, 1277 PASS, 1 PICK-01, 1 SDK skip
test_content         : 176 tests, 175 PASS + 1 SDK skip
test_bridge          : 139/139 Debug and Release
UIA                  : not rerun; no UI/runtime path change
```

## 3. Limits

Startup Recovery and Apply ABI/UI remain G6-4-5/G6-4-6. No auto-recovery after
Phase 2 Publish/record/live-update inconsistency yet.

## 4. Git

Uncommitted; no commit/push/reset/clean/stash.
