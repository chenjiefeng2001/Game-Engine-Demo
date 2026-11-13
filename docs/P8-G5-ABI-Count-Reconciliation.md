# P8 G5 ABI Operation Count Reconciliation

**Status**: RESOLVED
**ABI version**: v1.16 (unchanged)
**Corrected op count**: **63**
**Source of authority**: `bridge/include/editor_bridge/capi.h`

## 1. Actual table

The enum includes `ENGINE_HOST_OP_NONE = 0` and contiguous operations 1–62.
`ENGINE_HOST_OP_COUNT` is the exclusivity bound, not the highest operation ID.

| Range | Meaning |
|---|---|
| 0 | `ENGINE_HOST_OP_NONE` |
| 1–36 | pre-P8 operations |
| 37–46 | P7-B transform/history/editor camera |
| 47–57 | P8-G2 hierarchy ID ops |
| 58–59 | P8-G3 duplicate ops |
| 60 | P8-G4 Create Prefab |
| 61 | P8-G5 Instantiate Prefab |
| 62 | P8-G5 last instantiated root ID |
| 63 | `ENGINE_HOST_OP_COUNT` |

Invariant:

```text
valid operations = 0..62 inclusive = 63 values
ENGINE_HOST_OP_COUNT = GET_LAST_INSTANTIATED_ENTITY_ID + 1 = 63
```

Parsing the current enum found no missing IDs from 0 through 62 and no duplicate
IDs. The earlier G5 report's `62 ops` was an off-by-one count; it has been
corrected in the G5 closure/report/test documents and managed constants.

## 2. Files reconciled

- `bridge/include/editor_bridge/capi.h`: count 63, contiguous ordering.
- `editor_avalonia/Native/EditorBridgeApi.cs`: count 63.
- `tests/test_bridge/test_editor_capi.cpp`: explicit last-ID + 1 invariant and
  count 63.

Version remains v1.16 because no API shape/ID changed; only the count constant
and its test/documentation were corrected.

## 3. Verification performed

Targeted, not full workspace:

- `test_bridge` filter: ABI version/count and PrefabInstance tests passed.
- Managed `T6BIncrementIsAdditiveOnly` passed.
- No production behavior/API IDs changed; no full native/UIA inventory rerun.

Historical `PICK-01` and SDK shaderc skip remain unchanged.
