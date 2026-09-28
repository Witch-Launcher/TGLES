# Host ABI

The C surface a GLES host (`dlopen`s): 371 GLES + 50 EGL symbols, zero system
GLES/EGL headers. Verified against `tools/mobilegl/host_contract.json`
(MobileGL `dev` @ `fff9d639`, 2026-09-16) and Khronos `docs/reference/*.h`.

## Layout

| Artifact | Role |
|---|---|
| `include/tgles/host/abi.h` | Scalar types (Khronos widths) + 50 EGL declarations |
| `include/tgles/host/abi_gl.h` | 371 GLES prototypes, plain C (no `APIENTRY` macros) — **generated** |
| `src/host/host_c_api.cpp` | 27 real trial-subset bodies (buffers/VAO/shader/program/texture/FBO/draw) |
| `src/host/abi/egl_api.cpp` | 38 remaining EGL bodies over `EglState` |
| `src/host/abi/gl_real.cpp` | 276 real GLES bodies and counting (uniforms full family + reads + reflection, queries, state, raster, draws, objects, buffers, sync, textures, samplers, vertex attribs, renderbuffers, FBO, programs, compute, debug). Full table: `host-entries.md` |
| `src/host/abi/gl_gap.cpp` | 68 declared gaps: record in ledger, return spec default — **generated** |
| `src/host/abi/dispatch.cpp` | The single name->address table behind `eglGetProcAddress` |
| `tools/host_abi/gen_gl_abi.py` | Generator (`--check` is drift-proof CI) |

## Rules (pinned by `tests/host/test_abi_contract.cpp`)

- Every contract name resolves via **both** `dlsym` **and** `eglGetProcAddress` to the **same** address.
- Unknown names (incl. desktop `glPolygonMode`) return **NULL** — never `(void*)0x1`; MobileGL treats non-null as callable and would crash.
- `eglQueryString(EXTENSIONS)` advertises only what exists; grepping it is how launchers diagnose support.
- Gap budget `TGlesExpectedGapBudget = 68`: lower it as `gl_real.cpp` grows, never raise without a note here.

## Why MobileGL can load this (verified from upstream source)

Read at `MobileGL/.../BackendLoaders/OpenGL/Loader.cpp` @ `fff9d639`:

- GLES comes from **`eglGetProcAddress`** (`AcquireGLESFunctions`); a null logs `Failed to load GLES function`.
- EGL comes from **`dlsym` on the dlopened library**; on iOS the name is **`libtinygl4angle.dylib`**, elsewhere `libEGL.so.1` / `libEGL.so`; a null is fatal (`MGLOG_F`).
- Optional entry points (`INIT_GLES_FUNC_OPTIONAL`: texture views, buffer-texture spellings, `PolygonModeNV/ANGLE`, multi-draw/base-instance EXT) may be null — everything else must resolve.
- Stock macOS (`__APPLE__` without `MOBILEGL_IOS`) has **no DirectGLES path** (`OpenLib`/`ProcAddress` return null) — macOS CTS runs go through the surfaceless EGL platform (see `conformance.md`), iOS runs use the `MOBILEGL_IOS` path and `libtinygl4angle.dylib` (the built file is `libtgles.dylib`; `TGLES_HOST_LIBRARY_NAME` sets that name).

## Debugging a host bring-up

```sh
python3 tools/cts/preflight.py --lib build/libtgles.dylib  # F5 + sentinels + strings
```

If a call does nothing with `NO_ERROR`, ask the ledger: `tglesAbiGapCount/Name/Calls` tells you exactly which gap was hit and how often.

## Pulling the TGL debug log into your own log file

Every fail-closed path names its reason on the `[TGL-DEBUG]` channel
(translator rejections, submit-stage failures, bridge allocation failures).
`stderr` never reaches a launcher-owned log file on iOS, so the same text is
kept in a bounded in-memory ring (64 KB, oldest drops first) that the
launcher polls — e.g. once per second, or right after a black frame:

```objc
// Resolve once (dlsym or eglGetProcAddress — both agree by contract).
void (*setDebugLog)(int) = dlsym(lib, "tglHostSetDebugLog");
int (*getDebugLog)(char *, int) = dlsym(lib, "tglHostGetDebugLog");
// mode bitmask: 1 = stderr sink, 2 = memory ring, 0 = fully silent.
setDebugLog(1 | 2);
// ... run frames ...
char buf[8192];
int n = getDebugLog(buf, sizeof(buf));  // drains; NUL-terminated
if (n > 0) appendToMyLogFile(buf);
// Query pending bytes without draining: getDebugLog(NULL, 0).
// Silence everything (benchmarks, release): setDebugLog(0).
```

Default mode is `3` (both sinks on), matching historical stderr behavior.
