
## Source-stage processing

`ShaderSourceProcessor` inspects the application's GLSL before glslang sees it. It recognizes valid `#version` directives, preserves legal `#line` directives, strips only GLSL-illegal filename forms and lexically blanks comments without deleting code after an unterminated-looking banner. Invalid version/profile tokens remain invalid so glslang can reject them instead of MobileGL legalizing malformed input.

## Compilation and SPIR-V

`ShaderCompiler` initializes glslang, compiles desktop GLSL to SPIR-V and uses SPIRV-Cross sessions for target emission. Backend callers choose compile option bits. Shared semantic passes include robust access and source compatibility; backend paths add target-specific transformations.

```text
GL ShaderObject
  -> source inspection/normalization
  -> glslang parse/link representation
  -> SPIR-V
  -> option-selected SPIR-V passes
  -> DirectGLES: SPIRV-Cross -> ESSL -> GLES compiler
  -> DirectVulkan: validation/reflection -> VkShaderModule
```

## DirectGLES transformations

ESSL cannot express every desktop builtin or qualifier used by applications. DirectGLES can lower draw parameters, promote values into uniforms, emulate `noperspective`, clamp robust access-chain indexes, lower Qualcomm-sensitive builtins and preserve the emitted shader's actual GLES version. The result is compiled by the driver and checked through the native info log.

## DirectVulkan transformations

DirectVulkan keeps SPIR-V as the native shader representation and reflects it in `ProgramFactory`. It can apply position Y/Z/surface rotation transforms, explicit LOD 0 sampling when the sampler state proves it safe, and other option-specific passes. The transformed module identity is included in the program/pipeline cache path.

## Why passes are explicit

Source regexes cannot reliably distinguish comments, identifiers, access chains or helper-function scope. SPIR-V passes have typed operations and decorations, so transformations such as `noperspective` emulation and robust access can preserve semantics without rewriting unrelated text.
