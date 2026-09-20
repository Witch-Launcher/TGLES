# Metal matrix (Apple GPU families)

Source: `Metal-Feature-Set-Tables.pdf` (May 21 2026) in `docs/reference/`.

| Family | Typical SoCs | Tessellation | BC/DXT | Arg-buf tier2 | Mesh |
|--------|--------------|--------------|--------|---------------|------|
| Apple3 | A9, A10 | [PASS] | [FAIL] | [FAIL] | [FAIL] |
| Apple4 | A11 | [PASS] | [FAIL] | [FAIL] | [FAIL] |
| Apple5 | A12 | [PASS] | [FAIL] | [FAIL] | [FAIL] |
| Apple6 | A13 | [PASS] | [FAIL] | [FAIL] | [FAIL] |
| Apple7 | A14, M1 | [PASS] | [FAIL] | [PASS] | [PASS] |
| Apple8 | A15, A16, M2 | [PASS] | [FAIL] | [PASS] | [PASS] |
| Apple9 | A17, A18, M3, M4 | [PASS] | [PASS] | [PASS] | [PASS] |
| Apple10 | A19, M5 | [PASS] | [PASS] | [PASS] | [PASS] |

Corrections vs. the plan: **BC = Apple9+** (not A14+), **mesh = Apple7+**
(not A13), and **no A20** exists in the published tables.

The `tgles::metal` module (`metal_mapping.h`) encodes this table plus the
GL→Metal mapping for draw/compute/sync/query (geometry = emulated,
timer query = unsupported).
