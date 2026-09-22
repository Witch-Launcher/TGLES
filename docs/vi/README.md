# TGL — OpenGL ES 3.2 cho iOS

> Triển khai nghiêm túc, từng bước, mỗi bước đều có test chuẩn spec.

## Mục tiêu

Đưa **toàn bộ OpenGL ES 3.2** (spec 05/05/2022 + GLSL ES 3.20) chạy trên iOS
qua Metal backend, đạt chuẩn conformance, thay thế MetalANGLE (chỉ ES 3.0
complete, 3.1/3.2 in-progress).

## Nguyên tắc làm việc

1. **Không tin 100% vào plan gốc** — mọi claim đối chiếu spec trong
   `docs/reference/` (xem [Host ABI](host-abi.md)).
2. **Test-first**: viết test C/C++ trước, implementation sau.
3. **Từng bước một**: xong bước nào, test kỹ bước đó đạt chuẩn spec mới qua
   bước khác.
4. Code C/C++ portable: build được trên macOS (Intel), cross-compile được
   sang `iphoneos` / `iphonesimulator`.

## Build & test

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

Cross-compile check sang iOS (chỉ biên dịch, không chạy):

```sh
cmake -S . -B build-ios -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_DEPLOYMENT_TARGET=14.0
cmake --build build-ios
```

## Docs

```sh
docsify serve docs   # preview ở http://localhost:3000
# hoặc: python3 -m http.server 3000 --directory docs
```

## Tiến độ các bước

| Bước | Nội dung | Spec | Trạng thái |
|------|----------|------|------------|
| 1 | Core foundation: types, error, context, enable/disable | §2.3.1, §18, §20.2–20.3 | [PASS] xong, test xanh |
| 2 | Buffer objects + VAO + transform feedback | §6, §10.3–10.4 | [PASS] xong, test xanh |
| 3 | Shader + program + uniforms | §7 | [PASS] xong, test xanh |
| 4 | Textures + samplers + ASTC | §8 | [PASS] xong, test xanh |
| 5 | Framebuffers + renderbuffers + MRT | §9 | [PASS] xong, test xanh |
| 6 | Tessellation + geometry + compute | §11–12 | [PASS] xong, test xanh |
| 7 | Sync + query + debug output | §4, §18 | [PASS] xong, test xanh |
| 8 | Draw commands + rasterization state | §10.5, §13–15 | [PASS] xong, test xanh |
| 9 | Metal backend (PSO cache, resources, encoder) | Metal tables 05/2026 | [PASS] xong, test xanh |
| 10 | Facade + conformance gates + phát hành | CTS | [PASS] xong, test xanh |

Chi tiết từng bước xem trong sidebar.
