# Productization Milestone 1 — Baseline Acceptance Record

> **TAG**: `milestone-1`
> **DATE**: 2026-08-23
> **STATUS**: COMPLETE / FROZEN
> **TESTS**: 61/61 PASS

---

## Frozen Subsystems

| Subsystem | Contract Version | Key Files |
|-----------|-----------------|-----------|
| GPU Physics | v1.x | GPUPhysicsEngine.cpp, dogfood_game.lua (BruteForce default) |
| Scripting | API v2.1 | ScriptInstance.cpp, ScriptAPI.cpp, GameplayAPI.cpp |
| Content Pipeline | SerializerV1 + Registry v1 | SceneSerializerV1.cpp, ContentAsset.cpp |
| Editor Workflow | v1 | ScriptSandboxApp.cpp (Hierarchy/Inspector/Save/Load/Console) |

## Isolated / Rejected

| Component | Verdict | Restart Gates |
|-----------|---------|---------------|
| RenderGraph (716 lines) | FAIL as-is | F1-F6 (see SG6 report) |
| Spatial Hash | Not default | Real workload proving random-access bottleneck |

## Deferred Backlog

| ID | Capability | Priority | Trigger Condition |
|----|-----------|----------|-------------------|
| M006 | CWD anchoring | P1 | Infrastructure fix, independent |
| M004 | Input.pressed() | P1 | Real gameplay needs edge detection |
| M002 | Component data serialization | P1 | Entity needs radius/color beyond Transform |
| M003 | Prefab / Entity Template | P3 | Same-type entities > 10 |

## Test Evidence

```
test_scripting   23/23 PASS
test_physics     16/16 PASS
test_renderer     9/9 PASS
test_content     13/13 PASS
─────────────────────────
TOTAL           61/61 PASS
```

## Golden Gate Evidence

```
Process A: Save scene (Player+Cube) + manifest → exit
Process B: Fresh start → Load manifest → Instantiate scene
           → Resolve script GUID → Start ScriptInstance
           → Simulate WASD input → Player moved
           → Console Execute → api_version == 2.0 confirmed
Result: PASS
```

## Negative Feedback Validated

| Discovery | Outcome |
|-----------|---------|
| Shader dual-track (.inl vs .glsl) | Eliminated via single-source pipeline |
| Spatial Hash slower than BruteForce at 16K+ | Kept as experimental path, BruteForce stays default |
| RenderGraph 47.5× overhead + broken topo-order | Isolated with F1-F6 restart gates |
| Scripting contract ≠ implementation (Gauss-Seidel vs Jacobi) | Fixed to true Jacobi, bit-exact verified |
| Reload transient leak (globals survived) | Fixed to clean-rebuild semantics |
| GL46Queue::WaitIdle empty implementation | Documented as engine defect |

## Architecture Principles Established

1. Any architecture must be proven by real path + data gates; no evidence = no priority.
2. Handle/ID in serialized data, never runtime pointers.
3. Explicit failure contracts per load scenario (fatal vs non-fatal).
4. Dogfood is the acceptance gate — not test count alone.
5. Backlog existence IS correct engineering state; no need to empty it before freezing.

---

*This tag serves as the unambiguous anchor for future AI sessions.
Do NOT re-explain the project from scratch — reference this document.*
