# Conformance

## Gate trong repo (chạy trước, vài giây)

```sh
cmake -S . -B build && cmake --build build
ctest --test-dir build --output-on-failure   # 274 tests, gồm CTS subset + host contract
python3 tools/cts/preflight.py --lib build/libtgles.dylib
```

- `tests/cts/test_cts_subset.cpp`: subset nhóm ES32 của Khronos CTS (luật state mà suite bắt tuân theo).
- `tests/host/test_abi_contract.cpp`: mọi tên contract qua `dlsym` + `eglGetProcAddress`, chuỗi F5 surfaceless, honesty `EXTENSIONS`, budget gap.
- `tools/cts/preflight.py`: chạy đúng F5 qua `ctypes`, không cần rebuild — gate cho farmer trước khi build cả CTS.

## CTS Khronos trên macOS (surfaceless)

Platform OSX của CTS chỉ có desktop CGL profiles, nên chạy ES phải dùng
surfaceless platform (`tcuSurfacelessPlatform.cpp`): `GetDisplay(NULL)` →
`Initialize` → `ChooseConfig(R3+PBUFFER)` → `GetConfigAttrib` → `CreateContext`
→ `CreatePbufferSurface(WIDTH,HEIGHT)` → `MakeCurrent`, với `DYLD_LIBRARY_PATH`
trỏ vào symlink `libEGL.so` tới dylib của mình.

```sh
sh tools/cts/build_es32.sh --cts-dir ./cts --lib $PWD/build/libtgles.dylib
DYLD_LIBRARY_PATH=./cts/build ./cts/build/cts-runner --type=es32
```

Danh sách group lấy từ `docs/reference/cts/ES32_GROUPS.md`.

## Đọc một failure

| Loại | Nghĩa | Xem ở đâu |
|---|---|---|
| Preflight FAIL | Hình dạng library sai (thiếu symbol, sentinel, string xấu) | `host-abi.md`, ledger |
| CTS `NonConformance` ở tên gap | Call đã ghi nhận, chưa chạy | `tglesAbiGapCalls(name)` |
| Lệch pixels ở đường RGBA8 | CPU unpacker vs GPU | `tests/state/test_texture_upload.cpp` |
| `eglGetProcAddress` NULL ở tên required | Drift bảng dispatch | `gen_gl_abi.py --check` |

## Trạng thái (trung thực)

Gate trong repo xanh (274/274). Chưa claim tỉ lệ pass `--type=es32`:
ledger báo gap thật theo từng call (341 khai báo), tỉ lệ pass đi sau việc
chạy GPU, không đi sau giấy tờ.
