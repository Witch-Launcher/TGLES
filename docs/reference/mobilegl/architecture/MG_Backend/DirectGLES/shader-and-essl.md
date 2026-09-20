## Program synchronization

The frontend program is linked from the semantic shader objects, but GLES receives a backend-specific program. `BackendProgramObjectImpl::SyncToBackend` compares the semantic link version. On a new link it obtains the generated SPIR-V, applies GLES compatibility transforms, converts it to ESSL with SPIRV-Cross, compiles each stage with `glShaderSource`/`glCompileShader`, links the native program, and reflects locations and special bindings.

Link-derived caches are rebuilt together: sampler locations, uniform block indices, global UBO layout, indirect-parameter binding and draw-parameter uniforms. Retaining one of these from an older link would associate a valid location with the wrong native program.

## Why a transform layer is required

The semantic API is broader than the GLES implementation available on a device. The shader path repairs differences that cannot be fixed by a state call:

- `gl_BaseInstance` and draw ID are lowered to uniforms or generated values;
- `noperspective` and other interpolation details are emulated where ESSL lacks an equivalent;
- UBO precision and layout are adjusted for ESSL compiler behavior;
- clip-distance and vendor-specific Qualcomm paths are rewritten;
- 1D texture coordinate and sampler representations are mapped to GLES targets;
- robust access and format-fallback clamps are inserted where required.

The backend program stores locations for these generated uniforms and updates them immediately before the corresponding draw. For indirect or base-instance emulation, `SetBaseInstance`, `SetBaseInstanceWordIndex` and `SetDrawID` provide the values that native GLES cannot supply through the original call.

## Uniform and sampler binding

Sampler-style frontend uniforms store texture-unit numbers. After linking, the wrapper caches each native sampler uniform location and its last assigned unit, avoiding repeated string lookups. Texture and sampler objects are then bound to the units expected by the translated shader.

Global frontend state is packed into a backend global UBO. The backend reflects the actual native block size, because ESSL layout/padding can be larger than the frontend reflection result, allocates a properly aligned ring range, uploads only when the global state version changes, and binds that range to the generated global block.

## Compile failure behavior

The native program is not installed as the active backend representation until compilation, linking and required reflection succeed. This keeps a previous valid program usable for error reporting paths and avoids caching an incomplete link as if it represented the semantic program version.

## GLES-specific format workarounds

Some normalized and packed formats are stored through a canonical representation. The program records output clamp masks for SNORM/UNORM fallback paths, and draw code applies the corresponding shader/output fixups. This couples shader compilation to texture capability probing: a format decision is not complete until storage, sampling and fragment output behavior agree.
