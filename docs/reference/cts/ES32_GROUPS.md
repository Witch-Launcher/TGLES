# Khronos CTS inventory for OpenGL ES 3.2 (vendored evidence, not the suite)

Source (Apache-2.0): https://github.com/KhronosGroup/VK-GL-CTS
File: external/openglcts/modules/gles32/es32cTestPackage.cpp (branch main,
fetched Sep 2026). Copyright 2016 Google Inc. / The Khronos Group Inc.
Only this inventory is vendored; the full suite (deqp-gles32 binary, GPU +
EGL harness, GBs of sources) cannot run on a CPU-only host. TGL maps each
group below to a state-level subset in tests/test_cts_subset.cpp; groups
needing real shader execution / sampling / rasterization are covered by the
on-device Metal render test (tests/test_metal_device.mm) instead.

## How the provider runs it

Binary `deqp-gles32`, case filter `--deqp-case=KHR-GLES32.*`
(see external/openglcts/README.md). Each case init/resetState, iterate,
then postIterate (native events + eglSwapBuffers).

## ES32 package groups (exact child list from es32cTestPackage.cpp::init)

shaders/
  ShaderFunctionTests(GLSL_VERSION_320_ES)
  ShaderIntegerMixTests(GLSL_VERSION_320_ES)
  ShaderNegativeTests(GLSL_VERSION_320_ES)
  ShaderStructTests(GLSL_VERSION_320_ES)
  AggressiveShaderOptimizationsTests
core/
  GeometryShaderTests (AEP-promoted, EXTENSIONTYPE_NONE)
  GPUShader5Tests
  TessellationShaderTests
  TextureCubeMapArrayTests
  TextureBorderClampTests
  TextureBufferTests
  DrawBuffersIndexedTests
  ShaderConstExprTests
  ShaderMacroTests
  SeparableProgramsTransformFeedbackTests
  CopyImageTests (es32cCopyImageTests.cpp, the only gles32/-local module)
  InternalformatTests
  TextureShadowLodTest
  NearestEdgeCases
  FramebufferCompletenessTests
  TextureCompatibilityTests
  CompressedFormatTests
InfoTests (package-level)

## Inherited coverage (same repo, other modules, not vendored)

ES 3.2 implementations must also pass the 3.0/3.1 packages:
modules/gles3 (ES 3.0 groups), modules/gles31 (ES 3.1: compute shaders,
SSBO, image load/store, indirect draws, separate program objects),
modules/glesext (extension groups). TGL state coverage for those areas
lives in tests/test_step*.cpp + tests/test_cts_subset.cpp.

## TGL mapping status (honest, per group)

Covered at state level: ShaderNegative (version/main/mixed-version/compute
rules), ShaderStruct/ConstExpr/Macro (parse-level incl. array uniforms),
Geometry/Tessellation stage presence + link rules, TextureBuffer (tier +
range + 0x919F offset alignment + size limits), TextureCubeMapArray target,
  DrawBuffersIndexed (draw buffers + per-buffer blend/mask), SeparablePrograms
  TransformFeedback (pipelines + XFB objects), CopyImage (2D/cube-face/3D/
  array row copies on the unpacked store via TexImage3D/SubImage3D),
  Internalformat (full GetIntegerv limit table), Framebuffer Completeness,
  TextureCompatibility (sized-format classification), CompressedFormat (ASTC
  block rules), Info (version strings), UniformBlocks (block index/count/
  binding via GetUniformBlockIndex), TextureShadowLod (compare-mode state),
  TextureBorderClamp (CLAMP_TO_BORDER + float border color on textures and
  samplers), image units + buffer storage + Table 4.2 queries (gles31
  heritage), app draw wiring (RenderFrame: VAO float attribs + program MVP
  + draw-FBO size into bridge frames, TRIANGLES).
Device level, verified on host Metal (tests/test_metal_device.mm): MSL
compile, PSO creation, triangle draw, presentDrawable, async fence ring,
exact pixels via readback AND blit.
Out of scope for the CPU model: full GLSL semantic analysis (provider uses
a real compiler frontend; TGL validates version/entry/uniform-shape only;
vendoring glslang like MobileGL does is the tracked next epic).
Full-suite note: `deqp-gles32 --deqp-case=KHR-GLES32.*` still requires the
CTS binary + GPU + EGL harness; the subset above is TGL's runnable,
in-repo proof per group.

## Metal iOS API baseline (verified, not guessed)

Every MTL/QuartzCore call in src/metal_bridge_apple.mm was checked against
the macOS 26.2 SDK headers for iOS availability; all are at or below the
iOS 14 baseline (Apple3/A9 target): MTLCreateSystemDefaultDevice,
newCommandQueue, newBufferWithLength:options:, newTextureWithDescriptor:,
newLibraryWithSource:options:error:, newRenderPipelineStateWithDescriptor:
:error:, MTLVertexDescriptor attribute/layout slots,
renderCommandEncoderWithDescriptor:, drawPrimitives:vertexStart:vertexCount:,
setVertexBuffer:offset:atIndex:, setVertexBytes:length:atIndex:,
blitCommandEncoder, copyFromTexture:...:toBuffer:... (option-less form),
getBytes:bytesPerRow:fromRegion:mipmapLevel: (unannotated, baseline),
waitUntilCompleted, presentDrawable:, CAMetalLayer device/pixelFormat/
drawableSize/nextDrawable. No Metal 3-only API is used. Storage modes are
Shared/Private only (Managed is macOS-only and never referenced).
