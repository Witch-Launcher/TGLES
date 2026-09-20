
`MG_Backend` is the native translation boundary. It is selected once per initialized MobileGL instance from `MG_Config::ActiveBackendType` and exposes a common function table to `MG_Impl`.

## Common files

| File | Role |
| --- | --- |
| `BackendObject.h` | `BackendObject`, `BackendType`, capability structures, `GLFunctionsTable`, `GlobalBackendFunctionsTable`. |
| `BackendObjects.h` | global `pActiveBackendObject` and `gBackendFunctionsTable` declarations. |
| `BackendObject.cpp` | common EGL surface/current-thread/resource bookkeeping. |
| `Init.cpp` | backend selection, initialization, table installation and renderer logging. |

## Selection and table installation

`MG_Backend::Init` switches on `MG_Config::ActiveBackendType`, constructs `BackendObject_DirectGLES` or `BackendObject_DirectVulkan`, calls `Initialize`, then assigns:

```cpp
gBackendFunctionsTable = pActiveBackendObject->GetBackendFunctions();
```

The table contains draw, clear, blit/copy, readback, compute, query and sync callbacks. `Present` is separate because it belongs to the EGL surface/frame boundary. Test targets can replace individual table entries with recording functions, which is why the function-table boundary is also useful for isolated GL tests.

## Capability boundary

`DynamicBackendParameters` supplies runtime limits. `FormatCapabilityCache` is indexed by internal format and texture/renderbuffer target and stores full capabilities, caveat capabilities and sample counts. `GLImpl` uses these facts to validate requests; the backend remains responsible for fallback storage and conversion.

## Backend pages

- [DirectGLES implementation](DirectGLES/README.md)
- [DirectVulkan implementation](DirectVulkan/README.md)
- [Frame, cache and destruction rules](frame-and-lifetime.md)
