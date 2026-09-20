# Conformance

## Mục tiêu

Pass bộ **Khronos CTS / dEQP ES 3.2** (`cts-runner --type=es32`) trên iOS.

## Chiến lược (theo spec, không theo plan cũ)

1. **Unit + integration trong repo** (`tests/`): mỗi bước 1 file test,
   chạy bằng `ctest` trên macOS Intel.
2. **Cross-compile iOS**: `iphoneos` (device) + `iphonesimulator`, deployment
   target iOS 14.0 (A9/Apple3 là baseline thấp nhất).
3. **CTS**: build dEQP framework, `fetch_sources.py`, chạy `--type=es32` trên
   farm thiết bị, waiver theo quy trình Khronos Adopter khi cần.
4. **Benchmark**: FPS / CPU / GPU cho draw-call nặng, post-processing,
   compute shader; so với Metal thuần (không so với "OpenGL ES native trên
   Android" như plan cũ viết).

## Tình trạng

- Toàn bộ 10 bước: unit tests xanh trên macOS (**137 tests, 740 checks**).
- Mỗi bước đều cross-compile sạch cho `iphoneos` (arm64) +
  `iphonesimulator` (x86_64), deployment target iOS 14.0.
- `GlesContext::ConformanceChecklist()` (step 10) encode toàn bộ minimums
  và plan-corrections thành gate tự động — mảng rỗng = đạt.
- CTS full (`--type=es32`): chạy trên farm thiết bị với dEQP khi có device;
  checklist trong repo là gate trước CTS.
