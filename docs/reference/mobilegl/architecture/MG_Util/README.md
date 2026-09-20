
`MG_Util` contains mechanisms shared by the API/state/backend layers. It is not an undifferentiated helper folder: each utility exists at a specific translation boundary.

## Major groups

```text
MG_Util/
├── ShaderTranspiler/       GLSL inspection, glslang, SPIR-V passes, SPIRV-Cross
├── Texture/                pixel-store and format processing
├── Converters/             enum, format, extension and string conversion
├── BackendLoaders/         GLES/Vulkan dynamic loaders
├── Debug/                  logging and error visibility
├── SelfTest/               driver POST probes
├── Loader/                 generated/native API loading support
└── Math/Metrics/Types/     shared data types and instrumentation
```

## Consumers

- `MG_Impl` uses converters, validators, logging and format helpers.
- `MG_State` uses types, math, shader reflection metadata and buffer metrics.
- `DirectGLES` uses loaders, ESSL shader emission, pixel conversion and driver probes.
- `DirectVulkan` uses SPIR-V transforms, format/readback conversion, loaders and diagnostics.
- Android/desktop tools use logging, POST reports, trace result parsing and image conversion.

## Design rule

Utilities may know about a translation problem, such as converting a GL packed pixel type or rewriting a SPIR-V instruction, but they should not silently mutate `GLContext` ownership or decide which backend is active. Backend-specific policy is passed through explicit options or backend calls.

See [Shader pipeline](shader-pipeline.md), [Format and readback](format-readback.md), and [Diagnostics](diagnostics.md).
