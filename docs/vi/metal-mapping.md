# Ma trận Metal (Apple GPU families)

Nguồn: `Metal-Feature-Set-Tables.pdf` (May 21 2026) trong `docs/reference/`.

| Family | SoC tiêu biểu | Tessellation | BC/DXT | Arg-buf tier2 | Mesh |
|--------|---------------|--------------|--------|---------------|------|
| Apple3 | A9, A10 | [PASS] | [FAIL] | [FAIL] | [FAIL] |
| Apple4 | A11 | [PASS] | [FAIL] | [FAIL] | [FAIL] |
| Apple5 | A12 | [PASS] | [FAIL] | [FAIL] | [FAIL] |
| Apple6 | A13 | [PASS] | [FAIL] | [FAIL] | [FAIL] |
| Apple7 | A14, M1 | [PASS] | [FAIL] | [PASS] | [PASS] |
| Apple8 | A15, A16, M2 | [PASS] | [FAIL] | [PASS] | [PASS] |
| Apple9 | A17, A18, M3, M4 | [PASS] | [PASS] | [PASS] | [PASS] |
| Apple10 | A19, M5 | [PASS] | [PASS] | [PASS] | [PASS] |

Lưu ý đã sửa từ plan: **BC = Apple9+** (không phải A14+), **mesh = Apple7+**
(không phải A13), **không có A20** trong tables đã công bố.

Module `tgles::metal` (`metal_mapping.h`) mã hóa bảng này + mapping
GL→Metal cho draw/compute/sync/query (geometry = emulated, timer = unsupported).
