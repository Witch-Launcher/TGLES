# Conformance

## In-repo gates (run first, seconds)

```sh
cmake -S . -B build && cmake --build build
ctest --test-dir build --output-on-failure   # 274 tests, incl. CTS subset + host contract
python3 tools/cts/preflight.py --lib build/libtgles.dylib
```

- `tests/cts/test_cts_subset.cpp`: Khronos CTS ES32-group subset (state rules the suite must obey).
- `tests/host/test_abi_contract.cpp`: every contract name via `dlsym` + `eglGetProcAddress`, F5 surfaceless sequence, `EXTENSIONS` honesty, gap budget.
- `tools/cts/preflight.py`: the same F5 over `ctypes`, no rebuild needed — the farmer's gate before building all of CTS.

## Khronos CTS on macOS (surfaceless)

The CTS OSX platform offers desktop CGL profiles only, so ES runs use the
surfaceless platform (`tcuSurfacelessPlatform.cpp`): `GetDisplay(NULL)` →
`Initialize` → `ChooseConfig(R3+PBUFFER)` → `GetConfigAttrib` → `CreateContext`
→ `CreatePbufferSurface(WIDTH,HEIGHT)` → `MakeCurrent`, with `DYLD_LIBRARY_PATH`
pointing at a `libEGL.so` symlink to our dylib.

```sh
sh tools/cts/build_es32.sh --cts-dir ./cts --lib $PWD/build/libtgles.dylib
DYLD_LIBRARY_PATH=./cts/build ./cts/build/cts-runner --type=es32
```

Groups come from `docs/reference/cts/ES32_GROUPS.md`.

## Reading a failure

| Class | Meaning | Where to look |
|---|---|---|
| Preflight FAIL | Library shape wrong (missing symbol, sentinel, bad string) | `host-abi.md`, ledger |
| CTS listado `NonConformance` on a gap name | Call recorded, not executed | `tglesAbiGapCalls(name)` count |
| Pixel mismatch on RGBA8 paths | CPU unpacker vs GPU | `tests/state/test_texture_upload.cpp` |
| `eglGetProcAddress` NULL for a required name | Dispatch table drift | `gen_gl_abi.py --check` |

## Status (honest)

In-repo gates green (274/274). No full `--type=es32` pass rate is claimed:
the ledger reports the true gap per call (341 declared), and a pass rate
follows GPU execution work, not paperwork.
