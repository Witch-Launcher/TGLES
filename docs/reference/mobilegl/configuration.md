
MobileGL reads `MOBILEGL_*` variables during initialization. Boolean values are enabled when set to a non-empty value other than `0` or case-insensitive `false`. Quirk variables are tri-state: unset means automatic device detection; a truthy value forces on; another set value forces off.

## Backend and compatibility

| Variable | Values | Default | Use |
| --- | --- | --- | --- |
| `MOBILEGL_BACKEND_TYPE` | `DirectGLES`, `DirectVulkan` | `DirectGLES` | Select the translation backend. |
| `MOBILEGL_USE_ANGLE` | boolean | off | Load ANGLE EGL/GLES libraries for DirectGLES. |
| `MOBILEGL_RELAXED_SEMANTICS` | boolean | off | Relax selected strict core-profile rules. |
| `MOBILEGL_COHERENT_AS_FLUSH` | boolean | off | Treat persistent explicit-flush maps as coherent for applications that never flush them. |
| `MOBILEGL_DISABLE_UBO_RING` | boolean | off | Use per-draw uploads instead of the DirectGLES persistent UBO ring. |

## Vulkan and driver workarounds

| Variable | Values | Default | Use |
| --- | --- | --- | --- |
| `MOBILEGL_MAGMA_FRAMESINFLIGHT` | integer `1`-`64` | `3` | Set DirectVulkan frames in flight. |
| `MOBILEGL_DISABLE_SUBGROUP` | boolean | off | Disable Vulkan subgroup support. |
| `MOBILEGL_MAGMA_R11G11B10F_FALLBACK` | boolean | off | Use the Vulkan fallback for `R11G11B10F`. |
| `MOBILEGL_DISABLE_ROBUST_BUFFER_ACCESS` | boolean | off | Disable Vulkan robust buffer access. |
| `MOBILEGL_AVOID_SAMPLER_MIPMAP_MIN_FILTER` | boolean | off | Avoid mipmap minification filters on problematic ANGLE/llvmpipe paths. |
| `MOBILEGL_QUIRK_SUBGROUP_PREFIX_SCAN` | auto/on/off | auto | Override Qualcomm subgroup prefix-scan detection. |
| `MOBILEGL_MAGMA_DISABLE_BLENDED_DEPTH_WRITE` | auto/on/off | auto | Override the DirectVulkan blended-depth-write quirk. |

## Diagnostics and tools

`MOBILEGL_DISABLE_TIMERQUERY` disables timer-query exposure and use. `MOBILEGL_TRACE_SKIP_AUTODESTROY` is intended for trace environments that need to keep teardown disabled. `VK_ICD_FILENAMES` selects the Vulkan loader ICD JSON, while `MOBILEGL_LOG_FILE_PATH` selects a log file before normal feature loading.

Example:

```sh
MOBILEGL_BACKEND_TYPE=DirectVulkan \
MOBILEGL_MAGMA_FRAMESINFLIGHT=2 \
MOBILEGL_DISABLE_SUBGROUP=1 \
VK_ICD_FILENAMES=/path/to/icd.json \
your-application
```
