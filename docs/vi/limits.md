# Giới hạn

TGL vẽ được gì và chưa vẽ được gì. Tra cứu ở đây trước khi port app —
mọi dòng dưới đều do test pin, không phải lời hứa.

## Đường render

- Chạy topologies lõi (`POINTS`/`LINES`/`LINE_STRIP`/`TRIANGLES`/`TRIANGLE_STRIP`
  + CPU-expand `LINE_LOOP`/`TRIANGLE_FAN`/adjacency), indexed/instanced/indirect/
  range/base-vertex, attribs fixed-point/packed/normalized/divisor/integer.
- Blend chạy thật (mọi factors/equations kể cả `CONSTANT_*`, dual-source qua
  `DualOut`, per-buffer qua `BlendFunci` + descriptor loop + `PsoKey`).
  Depth+blend chung một frame; split-stencil hai pass.
- MRT (tới 8) + MRT+MSAA resolve từng attachment chạy thật.
- Stencil testing chạy thật (front/back func/mask/op, split hai pass).
- Sampling: đa sampler 2D/cube/3D/array, mips kể cả cube/3D/array + LOD clamps,
  sRGB decode.
- GLSL→MSL: MRT/dual outs, UBO kể cả int/uint/bool, uniform arrays, structs
  người dùng, `gl_InstanceID`/`gl_VertexID`.
- Adjacency CPU-expand; `PATCHES`/mesh + chương trình geometry/tess fail-closed
  trên Intel (`TGL-DEBUG`, cần Apple7+ Metal3); border clamp fail-closed
  (Metal không có border color — đúng).
- Chưa có đường `glClear` độc lập (clear color/depth ở đầu pass).

## Đường pixels

- Store unpack là **RGBA8**. Transfer hỗ trợ: `RGBA`/`RGB`/`RED`/`RG` x `UNSIGNED_BYTE` (copy đủ, sub-image stamp theo row). Combo hợp lệ nhưng chưa có unpacker -> `INVALID_OPERATION` (fail closed); enum lạ -> `INVALID_ENUM`.
- Compressed (ETC2/ASTC) và multisample storage có validate; chưa decompress/resolve trên CPU.

## Mặt host

- Export 371 GLES + 50 EGL symbols; **303 thật**, 68 gaps khai báo (đo bằng ledger, budget trong `test_abi_contract.cpp`).
- `eglQueryString(EXTENSIONS)` chỉ liệt kê extension đang phục vụ.
- Depth: đủ 8 funcs + write mask + clear value qua target `Depth32Float` (`LESS`); overlap chứng minh bằng pixels với draw order chống lại winner.

## Platforms

| Platform | Trạng thái |
|---|---|
| macOS Intel x86_64 | Đã kiểm chứng: 453 tests + trials + viewer + game window (chạy qua dylib) |
| Apple Silicon | Cùng đường portable, chưa có máy để chứng minh (mesh cần Apple7+) |
| iOS device/simulator | Cross-compile sạch; chờ chạy trên máy thật |
| GPU families Apple3–Apple10 | Đã encode + gate (BC = Apple9+, mesh = Apple7+) |
