
This section is organized after the `MobileGL-cpp/MobileGL` source tree. Each directory documents one ownership layer and names the concrete classes, tables and managers found there. The final [Execution Workflows](Workflows/README.md) section connects the layers through real operations.

## Source-shaped map

```text
MobileGL/
├── Init.cpp, Config*.cpp             process lifecycle and configuration
├── MG_Impl/                          exported APIs and host frontends
│   ├── EGLImpl/                      common EGL display/context path
│   ├── GLImpl/                       desktop GL entry points and validation
│   ├── GLXImpl/, WGLImpl/            X11 and Win32 host adapters
│   └── CGLImpl/, NSOpenGLImpl/       macOS host adapters
├── MG_State/                         backend-independent GL/EGL state
│   ├── EGLState/                     displays, surfaces, contexts, current threads
│   └── GLState/                      buffers, textures, programs, VAOs, FBOs, errors
├── MG_Backend/                       native API translation boundary
│   ├── BackendObject.*                common contract and function table
│   ├── DirectGLES/                    GLES objects, shadows and managers
│   └── DirectVulkan/                  renderer, resources, pipelines and WSI
├── MG_Util/                          loaders, shader passes, conversion and diagnostics
├── MG_Test/ and MG_Benchmark/         native test and benchmark targets
└── tools/                             CTS, Piglit, trace replay and smoke harnesses
```

## Ownership in one sentence

`MG_Impl` decides what an API call means at the exported ABI, `MG_State` stores that meaning and its object relationships, `MG_Backend` realizes it on the selected native API, and `MG_Util` supplies the compiler/conversion/loader machinery used by both. No layer is allowed to use a convenient native handle as a substitute for the layer above it.

## Initialization graph

`MobileGL::EnsureInitialized()` is the root. `Init.cpp` loads configuration, creates `MG_State`, creates the selected `BackendObject`, initializes `MG_Impl`, and starts glslang. `MG_Backend::Init()` selects `BackendObject_DirectGLES` or `BackendObject_DirectVulkan`, calls its `Initialize()`, and copies its `GlobalBackendFunctionsTable` into `gBackendFunctionsTable`.

```text
first EGL/WGL/CGL entry
  -> EnsureInitialized
  -> ConfigLoader::Init
  -> MG_State::Init
  -> MG_Backend::Init
       -> pActiveBackendObject = DirectGLES or DirectVulkan
       -> GetBackendFunctions()
       -> gBackendFunctionsTable = returned table
  -> MG_Impl::Init
  -> glslang::InitializeProcess
```

The table is the normal dispatch seam. `GLImpl` validates and records state, then invokes `gBackendFunctionsTable.GL.*`; the selected backend callback performs synchronization and native work.

## Navigation

- [MG_Impl: API and host frontends](MG_Impl/README.md)
- [MG_State: semantic state and object graph](MG_State/README.md)
- [MG_Backend: common contract](MG_Backend/README.md)
- [DirectGLES implementation](MG_Backend/DirectGLES/README.md)
- [DirectVulkan implementation](MG_Backend/DirectVulkan/README.md)
- [MG_Util: shared mechanisms](MG_Util/README.md)
- [Tools and test boundaries](Tools/README.md)
- [Execution workflows](Workflows/README.md)
