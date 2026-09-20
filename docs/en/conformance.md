# Conformance

## Goal

Pass the **Khronos CTS / dEQP ES 3.2** suite (`cts-runner --type=es32`) on iOS.

## Strategy (spec-driven, not plan-driven)

1. **Unit + integration in the repo** (`tests/`): one test file per step,
   run with `ctest` on macOS Intel.
2. **iOS cross-compile**: `iphoneos` (device) + `iphonesimulator`, deployment
   target iOS 14.0 (A9/Apple3 is the lowest baseline).
3. **CTS**: build the dEQP framework, `fetch_sources.py`, run `--type=es32`
   on a device farm, waivers per the Khronos Adopter process when needed.
4. **Benchmarks**: FPS / CPU / GPU for heavy draw calls, post-processing,
   compute shaders; compared against native Metal (not against "native
   OpenGL ES on Android" as the old plan suggested).

## Status

- All 10 steps: unit tests green on macOS (**137 tests, 740 checks**).
- Every step cross-compiles cleanly for `iphoneos` (arm64) +
  `iphonesimulator` (x86_64), deployment target iOS 14.0.
- `GlesContext::ConformanceChecklist()` (step 10) encodes all minimums
  and plan corrections as automatic gates — empty array = pass.
- Full CTS (`--type=es32`): runs on a device farm with dEQP once devices
  are available; the in-repo checklist is the pre-CTS gate.
