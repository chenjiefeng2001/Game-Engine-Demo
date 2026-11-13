# P7-B G7 - Integrated Regression Closure

**Status**: PASS - BASELINE EXCEPTIONS RETAINED

## Frozen inventory

Both Debug and Release:

| Metric | Value |
|---|---:|
| Native targets | 21 |
| Native discovered | 1208 |
| Native executed | 1208 |
| Native PASS | 1206 |
| Native FAIL | 1 (`PICK-01`) |
| Native SKIP | 1 (SDK shaderc) |
| Managed static | 379 |
| CTest entries | 22 |

`PICK-01` is the retained `EditorHostAbiTest.D3PickRequestPumpFetchHappyPath`
environment/readback exception; it is unrelated to P7-B Scene editing.
The SKIP is the existing SDK-specific shaderc case.

## Regressions retained green

- P6-1B B5/B6 Mesh runtime and Editor model tests.
- P7-C span/material multi-slot production pixel tests.
- P7-A Material edit/dependency/reimport/runtime refresh tests.
- Scene/Mesh GUID persistence, cold restart, and production Viewport UIA.

## Builds

- Native Debug and Release `ALL_BUILD` succeeded.
- Managed editor Debug and Release succeeded.
- ABI version/count assertions updated to **v1.12 / 47**.
