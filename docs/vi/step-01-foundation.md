# Bước 1 — Core foundation

## Spec áp dụng

- §2.3.1 Errors: first-error latch, `GetError` trả + xóa, lệnh lỗi bị bỏ qua
  (ngoại trừ `OUT_OF_MEMORY` thì kết quả undefined), return 0, không ghi pointer.
- §15.1.7 + §18: `DITHER` mặc định enabled; `DEBUG_OUTPUT` mặc định TRUE nếu
  debug context, FALSE nếu không.
- §20.2–20.3: `GetString` nhận `VENDOR/RENDERER/VERSION/EXTENSIONS/
  SHADING_LANGUAGE_VERSION`; `GetStringi` chỉ nhận `EXTENSIONS`.

## API đã implement (module `tgles_core`)

`include/tgles/gl_types.h`, `error.h`, `context.h`:

- Types: `GLenum`, `GLboolean`, `GLint`, `GLuint`, `GLfloat`, ... + hằng
  `GL_TRUE/GL_FALSE`, `GL_NO_ERROR`, `GL_INVALID_ENUM/VALUE/OPERATION/
  OUT_OF_MEMORY/CONTEXT_LOST`.
- `ErrorQueue`: `record()`, `get()` (trả + xóa theo FIFO nhiều flag-code pairs
  cho distributed implementations), `hasPending()`.
- `Context`: `create(debug)`, `enable/disable/isEnabled`, `getError`,
  `getString`, `getStringi`, `getIntegerv(MAJOR/MINOR/NUM_EXTENSIONS/...)`.
- 13 caps core — xem bảng trong `context.h`.

## Test

`tests/test_step01_foundation.cpp` — 30+ cases: latch semantics, defaults,
`INVALID_ENUM` cho cap lạ, format chuỗi VERSION/SL, bounds `GetStringi`,
`MAJOR=3/MINOR=2`, multi-context độc lập.

## Gate

```sh
cmake --build build && ctest --test-dir build --output-on-failure
cmake -S . -B build-ios -DCMAKE_SYSTEM_NAME=iOS && cmake --build build-ios
```

Cả hai phải xanh mới được qua Bước 2.
