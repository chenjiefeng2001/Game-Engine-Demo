# P6-0 — Repository & Baseline Stabilization Record

**Status**: PASS — REMOTE SYNCHRONIZED; INVENTORY FROZEN; RETENTION POLICY RECORDED  
**Branch**: avalonia  
**Precondition**: P5-F3-U1 Gate A/B closed (a9daee4)  
**Date**: 2026-11-05  
**Deliverable scope**: P6 roadmap §三 (Gate 0A/0B/0C); P6-1A 合同另见 `docs/P6-P1A-Asset-Import-Contract.md`

---

## 1. Gate 0A — Remote Sync

| Step | Result |
|------|--------|
| Pre-push ahead count | 5 (expected deliverables) |
| Commits reviewed | `cd33c77` (draft isolation), `581b621` (source-alignment record), `766ed9e` (Gate A1 scoped adapter diagnostic), `a592909` (Gate B end-to-end Jolt solid-fill GL readback), `a9daee4` (Gate A/B closure docs) |
| Diff span | `docs/*` + `tests/test_debugdraw/test_debugdraw.cpp` + `tests/CMakeLists.txt`, +383/−36 |
| Push | `711381e..a9daee4 avalonia -> avalonia` |
| Post-push ahead/behind | **0/0** |

Dirty third_party submodules 与 `crashes/` 未触碰（见 §3）。

## 2. Gate 0B — Authoritative Test Inventory (机器可生成)

**Tool**: `tools/test_inventory.ps1` (committed with this record) — runs `ctest -C <cfg>`
(handles content fixture, timeouts, CWD rules), parses `Testing/Temporary/LastTest.log`
for per-target gtest counters, plus `--gtest_list_tests` discovery and static
`[TestMethod]` counts for managed projects. Output: `artifacts/test_inventory/…`
(JSON + MD; `artifacts/` is git-ignored, the frozen numbers below are the record).

**Statistics calibers are deliberately separated (do not mix):**

| Caliber | Meaning |validated value at a9daee4|
|---------|---------|--------------------------|
| Native gtest cases | sum over 18 native targets, Debug | **1008 discovered = 1008 executed; 1006 PASS / 1 FAIL / 1 SKIP** |
| Native gtest cases | same, Release | **1008 discovered = 1008 executed; 1006 PASS / 1 FAIL / 1 SKIP** |
| CTest entries | registered tests incl. fixture | **19** (18 native + `content_fixture_setup`) |
| P5 regression subset | 5 targets (renderer/core/bridge/pick/debugdraw) | **244/244** Debug+Release |
| Managed MSTest | static `[TestMethod]` count (not a run) | **378** across 12 C# projects |

**Native targets (Debug = Release counts)**:

| Target | Suites | Cases | Pass | Fail | Skip |
|---|---|---|---|---|---|
| test_animation | 5 | 118 | 118 | | |
| test_audio | 6 | 145 | 145 | | |
| test_bridge | 9 | 113 | 113 | | |
| test_content | 28 | 117 | 116 | | 1 |
| test_core | 6 | 71 | 71 | | |
| test_debugdraw | 3 | 27 | 27 | | |
| test_e2e | 4 | 6 | 6 | | |
| test_ecs | 1 | 21 | 21 | | |
| test_gp01 | 6 | 22 | 22 | | |
| test_hrc | 3 | 15 | 15 | | |
| test_io | 8 | 202 | 202 | | |
| test_job | 1 | 4 | 4 | | |
| test_physics | 6 | 71 | 71 | | |
| test_pick | 3 | 10 | 10 | | |
| test_pick_transport | 2 | 9 | 8 | 1 | |
| test_renderer | 5 | 23 | 23 | | |
| test_scripting | 3 | 29 | 29 | | |
| test_viewport_resize | 3 | 5 | 5 | | |
| **Total** | | **1008** | **1006** | **1** | **1** |

Facts pinned by this baseline:

1. **The only native FAIL** is `EditorHostAbiTest.D3PickRequestPumpFetchHappyPath`
   (test_pick_transport), stable across both configurations and repeated runs
   (two reruns). Assertions at `tests/test_bridge/test_pick_transport.cpp:231/233`
   expect `packet.status == ENGINE_HOST_PICK_HIT` and `entity_id >= 1` but the
   transport returned non-HIT / id 0. The file header documents that these five
   tests are "ones that once produced RED results in a prior session, restored
   verbatim" — this failure is therefore **restored known-RED evidence, not a new
   regression introduced by P6**. Registered as **PICK-01** in
   `docs/Project-Gap-Register.md` (P6-2 scope). Not repaired in P6-0.
2. **The only SKIP** is `ShaderCompileRule.SkippedWithoutVulkanOrShaderc`
   (test_content) — environment-conditional explicit GTEST_SKIP (no Vulkan SDK in
   this machine's compile discoverability); identical in both configurations.
   Tracked in QA coverage, not a defect.
3. **P6-0 build fix (incidental, in-scope)**: `test_pick_transport` compilation
   failed with C1083 (`EngineHost.h` → `Engine/Application.h` not on include
   path). Root cause: the target linked only `EditorBridge` (no usage-requirement
   include propagation) and lacked `engine/include`; previous builds were lucky
   leftovers of an older vcxproj. Fix: link `EngineCore` PRIVATE to receive the
   same usage-requirements as `test_bridge` (`tests/CMakeLists.txt`, registered
   defect **BUILD-01 → FIXED**). The target now builds and runs 8/9 (its one
   failure is PICK-01, unchanged in behavior).

## 3. Gate 0C — Retention Policy (submodules + crashes)

### 3.1 Dirty third_party submodules (investigated individually)

| Submodule | Local dirty content | Nature | Decision (this slice) |
|---|---|---|---|
| d3d12ma | `D bin/D3D12Sample.exe` (deleted tracked sample binary) | sample-binary residue, zero impact on EngineCore | **Status 2 — retain + record**; restore optionally later |
| openal-soft | `D build/.empty` (deleted 0-byte placeholder) | placeholder residue; the MSVC consteval fix (`9fb7bb38`) lives **inside the fork commits** (`chenjiefeng2001/openal-soft-forked.git` per `.gitmodules`), not in worktree edits | **Status 2 — retain + record**; fork URL recorded |
| vma | `D bin/Shader.frag.spv`, `D bin/Shader.vert.spv`, `D bin/VmaSample_Release_vs2026.exe` | sample-build residue | **Status 2 — retain + record** |

- All three dirty states are **deletions of sample/placeholder artifacts inside
  the submodule working trees**; none touches library source or build contracts.
- gitlinks still point at the pinned SHAs (`1d86c11`, `9fb7bb38`, `3aa9212`);
  no submodule pointer drift.
- README correction note: vma pin is **v3.4.0** (`git describe`), not "3.x"
  ambiguity (P6-7 README task).

### 3.2 crashes/ artifacts

- **115 files = 23 `.dmp` + 23 `.json` + 69 `.txt`, ~1.04 GB**, all timestamped
  within **2026-09-30 17:49–18:04** (single-day debugging burst; not an
  ongoing pipeline).
- Policy this slice: **inventory only, zero deletion, zero movement.** A
  retention/archive rule (e.g. keep latest N dumps per event class, compress
  binaries after N weeks) is a P6-7 deliverable and requires explicit approval
  before touching these artifacts.

## 4. P6-0 Completion Standard

| Item | Status |
|---|---|
| Remote sync (0/0) | PASS |
| Unified machine-generatable test inventory | PASS (`tools/test_inventory.ps1` + frozen numbers in §2) |
| Workspace retention policy explicit | PASS (§3: Status 2 for three submodules, crashes/ untouched) |
| Build/registration defects surfaced by the sweep | BUILD-01 FIXED; PICK-01 registered (not repaired here) |

## 5. Follow-ups opened

- **PICK-01** → P6-2 (editor pick transport verification path).
- **P6-1A contract** → `docs/P6-P1A-Asset-Import-Contract.md` (frozen in this
  slice; implementation P6-1B starts only after this contract is approved).
EOF
