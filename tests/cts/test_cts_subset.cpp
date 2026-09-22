// CTS subset: state-level mapping of the Khronos ES 3.2 conformance groups
// (provider inventory vendored in docs/reference/cts/ES32_GROUPS.md,
// es32cTestPackage.cpp) onto TGL managers. These run without a GPU; groups
// needing shader execution / sampling / rasterization are covered by the
// on-device Metal render test (tests/test_metal_device.mm) instead.
// Border clamp (0x812D) is a documented gap: rejected, needs the sampler
// border-color path. 3D/array CopyImage slices are a documented gap.

#include "test_framework.h"

#include <string>

#include "tgles/state/buffer.h"
#include "tgles/state/compute.h"
#include "tgles/state/context.h"
#include "tgles/state/framebuffer.h"
#include "tgles/state/image_units.h"
#include "tgles/state/program.h"
#include "tgles/state/query.h"
#include "tgles/pipeline/raster.h"
#include "tgles/state/sampler.h"
#include "tgles/state/shader.h"
#include "tgles/state/texture.h"
#include "tgles/state/transform_feedback.h"
#include "tgles/state/vertex_array.h"

namespace {

tgles::GLuint Compile(tgles::ShaderManager& sm, tgles::GLenum type,
                      const char* src) {
  tgles::GLuint s = sm.CreateShader(type);
  sm.ShaderSource(s, 1, &src, nullptr);
  sm.CompileShader(s);
  return s;
}

const char* kV320 = "#version 320 es\nvoid main() {}";

}  // namespace

// InfoTests: version identity.
TEST(CtsSubset, InfoVersionStrings) {
  tgles::Context ctx = tgles::Context::Create(false);
  EXPECT_TRUE(
      std::string(
          reinterpret_cast<const char*>(ctx.GetString(tgles::kGlVersion)))
          .find("OpenGL ES 3.2") == 0);
  tgles::GLint major = 0, minor = 0;
  ctx.GetIntegerv(tgles::kGlMajorVersion, &major);
  ctx.GetIntegerv(tgles::kGlMinorVersion, &minor);
  EXPECT_EQ(major, 3);
  EXPECT_EQ(minor, 2);
  EXPECT_EQ(ctx.GetError(), tgles::kGlNoError);
}

// ShaderNegativeTests (320 es): bad version, missing entry, mixed link.
TEST(CtsSubset, ShaderNegativeRules) {
  tgles::ShaderManager sm;
  const char* bad = "#version 999 es\nvoid main() {}";
  tgles::GLuint b = Compile(sm, tgles::kGlVertexShader, bad);
  EXPECT_FALSE(sm.CompileSucceeded(b));
  const char* no_main = "#version 320 es\nuniform float x;";
  tgles::GLuint n = Compile(sm, tgles::kGlFragmentShader, no_main);
  EXPECT_FALSE(sm.CompileSucceeded(n));
  tgles::ProgramManager pm(&sm);
  tgles::GLuint prog = pm.CreateProgram();
  pm.AttachShader(prog, Compile(sm, tgles::kGlVertexShader, kV320));
  const char* old = "#version 100 es\nvoid main() {}";
  pm.AttachShader(prog, Compile(sm, tgles::kGlFragmentShader, old));
  pm.LinkProgram(prog);
  EXPECT_FALSE(pm.LinkSucceeded(prog));
  // Compute must link alone.
  tgles::GLuint cprog = pm.CreateProgram();
  pm.AttachShader(cprog, Compile(sm, tgles::kGlComputeShader, kV320));
  pm.AttachShader(cprog, Compile(sm, tgles::kGlVertexShader, kV320));
  pm.LinkProgram(cprog);
  EXPECT_FALSE(pm.LinkSucceeded(cprog));
  EXPECT_EQ(pm.GetError(), tgles::kGlNoError);
}

// ShaderConstExpr/ShaderMacro/ShaderStruct level: array uniforms parse.
TEST(CtsSubset, ArrayUniformReflection) {
  tgles::ShaderManager sm;
  tgles::ProgramManager pm(&sm);
  const char* vs =
      "#version 320 es\nuniform vec4 u_arr[3];\nvoid main() {}";
  tgles::GLuint prog = pm.CreateProgram();
  pm.AttachShader(prog, Compile(sm, tgles::kGlVertexShader, vs));
  pm.AttachShader(prog, Compile(sm, tgles::kGlFragmentShader, kV320));
  pm.LinkProgram(prog);
  EXPECT_TRUE(pm.LinkSucceeded(prog));
  EXPECT_TRUE(pm.GetUniformLocation(prog, "u_arr") >= 0);
  EXPECT_TRUE(pm.GetUniformLocation(prog, "u_arr[2]") >= 0);
  EXPECT_EQ(pm.GetError(), tgles::kGlNoError);
}

// Core GeometryShaderTests / TessellationShaderTests: stage presence + rules.
TEST(CtsSubset, GeometryAndTessellationLinkRules) {
  tgles::ShaderManager sm;
  tgles::ProgramManager pm(&sm);
  EXPECT_NE(sm.CreateShader(tgles::kGlGeometryShader), 0u);
  EXPECT_NE(sm.CreateShader(tgles::kGlTessControlShader), 0u);
  EXPECT_NE(sm.CreateShader(tgles::kGlTessEvaluationShader), 0u);
  // Geometry without vertex input must not link.
  const char* gs = "#version 320 es\nvoid main() {}";
  tgles::GLuint prog = pm.CreateProgram();
  pm.AttachShader(prog, Compile(sm, tgles::kGlGeometryShader, gs));
  pm.AttachShader(prog, Compile(sm, tgles::kGlFragmentShader, kV320));
  pm.LinkProgram(prog);
  EXPECT_FALSE(pm.LinkSucceeded(prog));
  // Tess control without eval must not link.
  tgles::GLuint tprog = pm.CreateProgram();
  pm.AttachShader(tprog, Compile(sm, tgles::kGlVertexShader, kV320));
  pm.AttachShader(tprog, Compile(sm, tgles::kGlTessControlShader, gs));
  pm.LinkProgram(tprog);
  EXPECT_FALSE(pm.LinkSucceeded(tprog));
  EXPECT_EQ(pm.GetError(), tgles::kGlNoError);
}

// Core TextureBufferTests: tier + range + offset alignment (0x919F).
TEST(CtsSubset, TextureBufferRangeAndAlignment) {
  tgles::Context ctx = tgles::Context::Create(false);
  tgles::GLint align = 0, size = 0;
  ctx.GetIntegerv(tgles::kGlTextureBufferOffsetAlignmentQ, &align);
  ctx.GetIntegerv(tgles::kGlMaxTextureBufferSizeQ, &size);
  EXPECT_EQ(align, tgles::kTextureBufferOffsetAlignmentValue);
  EXPECT_TRUE(size >= 65536);
  tgles::TextureManager tm;
  tgles::GLuint t = 0;
  tm.GenTextures(1, &t);
  tm.BindTexture(tgles::kGlTextureBuffer, t);
  EXPECT_EQ(tm.GetError(), tgles::kGlNoError);
  tm.TexBuffer(tgles::kGlTextureBuffer, tgles::kGlRgba8, 7);
  EXPECT_EQ(tm.GetError(), tgles::kGlNoError);
  tm.TexBufferRange(tgles::kGlTextureBuffer, tgles::kGlRgba8, 7, 0, 64);
  EXPECT_EQ(tm.GetError(), tgles::kGlNoError);
  tm.TexBufferRange(tgles::kGlTextureBuffer, tgles::kGlRgba8, 7, -4, 64);
  EXPECT_EQ(tm.GetError(), tgles::kGlInvalidValue);
  EXPECT_EQ(tm.GetError(), tgles::kGlNoError);
}

// Core TextureCubeMapArrayTests: target exists and binds.
TEST(CtsSubset, CubeMapArrayTargetBinds) {
  tgles::TextureManager tm;
  tgles::GLuint t = 0;
  tm.GenTextures(1, &t);
  tm.BindTexture(tgles::kGlTextureCubeMapArray, t);
  EXPECT_EQ(tm.GetError(), tgles::kGlNoError);
}

// TextureBorderClamp: wrap mode + float border color on both paths.
TEST(CtsSubset, BorderClampWrapAndColor) {
  tgles::TextureManager tm;
  tgles::GLuint t = 0;
  tm.GenTextures(1, &t);
  tm.BindTexture(tgles::kGlTexture2d, t);
  tm.TexParameteri(tgles::kGlTexture2d, tgles::kGlTextureWrapS,
                   tgles::kGlClampToBorder);
  EXPECT_EQ(tm.GetError(), tgles::kGlNoError);
  const tgles::GLfloat color[4] = {1, 0, 0, 1};
  tm.TexParameterfv(tgles::kGlTexture2d, tgles::kGlTextureBorderColor,
                    color);
  EXPECT_EQ(tm.GetError(), tgles::kGlNoError);
  EXPECT_EQ(tm.GetError(), tgles::kGlNoError);
}

// Core DrawBuffersIndexedTests: draw buffers + per-buffer blend/mask.
TEST(CtsSubset, DrawBuffersIndexedState) {
  tgles::RasterState raster;
  raster.BlendFunci(0, tgles::kGlOne, tgles::kGlZero);
  EXPECT_EQ(raster.GetError(), tgles::kGlNoError);
  raster.ColorMaski(1, tgles::kGlTrue, tgles::kGlTrue, tgles::kGlTrue,
                    tgles::kGlTrue);
  EXPECT_EQ(raster.GetError(), tgles::kGlNoError);
  raster.BlendFunci(99, tgles::kGlOne, tgles::kGlZero);
  EXPECT_EQ(raster.GetError(), tgles::kGlInvalidValue);
  EXPECT_EQ(raster.GetError(), tgles::kGlNoError);
}

// Core SeparableProgramsTransformFeedbackTests: pipelines + XFB objects.
TEST(CtsSubset, SeparablePipelineAndXfb) {
  tgles::ShaderManager sm;
  tgles::ProgramManager pm(&sm);
  tgles::GLuint pipe = 0;
  pm.GenProgramPipelines(1, &pipe);
  EXPECT_NE(pipe, 0u);
  tgles::GLuint prog = pm.CreateProgram();
  pm.ProgramParameteri(prog, tgles::kGlProgramSeparable, 1);
  pm.AttachShader(prog, Compile(sm, tgles::kGlVertexShader, kV320));
  pm.LinkProgram(prog);
  EXPECT_TRUE(pm.LinkSucceeded(prog));
  pm.UseProgramStages(pipe, tgles::kGlVertexShaderBit, prog);
  EXPECT_EQ(pm.GetError(), tgles::kGlNoError);
  tgles::TransformFeedbackManager tf;
  tgles::GLuint id = 0;
  tf.GenTransformFeedbacks(1, &id);
  tf.BindTransformFeedback(tgles::kGlTransformFeedback, id);
  EXPECT_EQ(tf.IsTransformFeedback(id), tgles::kGlTrue);
  EXPECT_EQ(tf.GetError(), tgles::kGlNoError);
}

// Core CopyImageTests (es32cCopyImageTests.cpp): 2D row copies + errors.
TEST(CtsSubset, CopyImageSubData2D) {
  tgles::TextureManager tm;
  tgles::GLuint s = 0, d = 0;
  tm.GenTextures(1, &s);
  tm.GenTextures(1, &d);
  tm.BindTexture(tgles::kGlTexture2d, s);
  tm.TexImage2D(tgles::kGlTexture2d, 0, tgles::kGlRgba8, 4, 4, 0,
                tgles::kGlRgba, tgles::kGlUnsignedByte, nullptr);
  tm.BindTexture(tgles::kGlTexture2d, d);
  tm.TexImage2D(tgles::kGlTexture2d, 0, tgles::kGlRgba8, 4, 4, 0,
                tgles::kGlRgba, tgles::kGlUnsignedByte, nullptr);
  tm.CopyImageSubData(s, tgles::kGlTexture2d, 0, 0, 0, 0, d,
                      tgles::kGlTexture2d, 0, 0, 0, 0, 4, 4, 1);
  EXPECT_EQ(tm.GetError(), tgles::kGlNoError);
  tm.CopyImageSubData(s, tgles::kGlTexture2d, 0, 2, 2, 0, d,
                      tgles::kGlTexture2d, 0, 0, 0, 0, 4, 4, 1);
  EXPECT_EQ(tm.GetError(), tgles::kGlInvalidValue);
  tm.CopyImageSubData(s, 0x1234u, 0, 0, 0, 0, d, tgles::kGlTexture2d, 0,
                      0, 0, 0, 4, 4, 1);
  EXPECT_EQ(tm.GetError(), tgles::kGlInvalidEnum);
  tm.CopyImageSubData(s, tgles::kGlTexture2d, 0, 0, 0, 0, d,
                      tgles::kGlTexture2d, 0, 0, 0, 1, 4, 4, 1);
  EXPECT_EQ(tm.GetError(), tgles::kGlInvalidValue);  // 2D has one slice.
  EXPECT_EQ(tm.GetError(), tgles::kGlNoError);
}

// Core InternalformatTests: the GetIntegerv limit table (new).
TEST(CtsSubset, InternalformatLimitTable) {
  tgles::Context ctx = tgles::Context::Create(false);
  struct Row {
    tgles::GLenum pname;
    tgles::GLint minimum;
  };
  static const Row kRows[] = {
      {tgles::kGlMaxTextureImageUnitsQ, 16},
      {tgles::kGlMaxVertexTextureImageUnitsQ, 16},
      {tgles::kGlMaxCombinedTextureImageUnitsQ, 32},
      {tgles::kGlMaxImageUnitsQ, 8},
      {tgles::kGlMaxTextureBufferSizeQ, 65536},
      {tgles::kGlMaxUniformBufferBindingsQ, 72},
      {tgles::kGlMaxShaderStorageBufferBindingsQ, 8},
      {tgles::kGlMaxDrawBuffersQ, 4},
      {tgles::kGlMaxColorAttachmentsQ, 4},
      {tgles::kGlMaxSamplesQ, 4},
      {tgles::kGlMaxRenderbufferSizeQ, 2048},
      {tgles::kGlMaxCubeMapTextureSizeQ, 2048},
      {tgles::kGlMax3dTextureSizeQ, 256},
      {tgles::kGlMaxArrayTextureLayersQ, 256},
      {tgles::kGlMaxComputeTextureImageUnitsQ, 16},
  };
  for (const Row& r : kRows) {
    tgles::GLint v = -1;
    ctx.GetIntegerv(r.pname, &v);
    EXPECT_TRUE(v >= r.minimum);
    EXPECT_EQ(ctx.GetError(), tgles::kGlNoError);
  }
  tgles::GLint v = 0;
  ctx.GetIntegerv(0x1234u, &v);
  EXPECT_EQ(ctx.GetError(), tgles::kGlInvalidEnum);
  ctx.GetIntegerv(tgles::kGlMaxTextureSize, nullptr);
  EXPECT_EQ(ctx.GetError(), tgles::kGlInvalidValue);
  EXPECT_EQ(ctx.GetError(), tgles::kGlNoError);
}

// Core FramebufferCompletenessTests: status matrix basics.
TEST(CtsSubset, FramebufferCompletenessBasics) {
  tgles::TextureManager tm;
  tgles::SamplerManager sm;
  tgles::FramebufferManager fbm(&tm, nullptr);
  (void)sm;
  tgles::GLuint f = 0;
  fbm.GenFramebuffers(1, &f);
  fbm.BindFramebuffer(tgles::kGlFramebuffer, f);
  EXPECT_EQ(
      fbm.CheckFramebufferStatus(tgles::kGlFramebuffer),
      tgles::kGlFramebufferIncompleteMissingAttachment);
  EXPECT_EQ(fbm.GetError(), tgles::kGlNoError);
}

// Core TextureCompatibilityTests: format classification.
TEST(CtsSubset, TextureFormatCompatibility) {
  EXPECT_TRUE(tgles::GlIsColorRenderable(tgles::kGlRgba8));
  EXPECT_TRUE(tgles::GlIsDepthFormat(tgles::kGlDepthComponent24));
  EXPECT_TRUE(tgles::GlIsDepthStencilFormat(tgles::kGlDepth24Stencil8));
  EXPECT_FALSE(tgles::GlIsColorRenderable(tgles::kGlDepthComponent24));
}

// Core CompressedFormatTests (ASTC): block-size rule.
TEST(CtsSubset, CompressedAstcBlockRule) {
  tgles::TextureManager tm;
  tgles::GLuint t = 0;
  tm.GenTextures(1, &t);
  tm.BindTexture(tgles::kGlTexture2d, t);
  const std::uint8_t blob[64] = {};
  tm.CompressedTexImage2D(tgles::kGlTexture2d, 0,
                          tgles::kGlCompressedRgbaAstc4x4, 5, 4, 0, 16,
                          blob);
  EXPECT_EQ(tm.GetError(), tgles::kGlInvalidValue);  // 5x4 vs 4x4 blocks.
  tm.CompressedTexImage2D(tgles::kGlTexture2d, 0,
                          tgles::kGlCompressedRgbaAstc4x4, 8, 8, 0,
                          sizeof(blob), blob);
  EXPECT_EQ(tm.GetError(), tgles::kGlNoError);
}

// Uniform-block reflection (CTS UniformBlock area + MobileGL UBO path).
TEST(CtsSubset, UniformBlockReflectionAndBinding) {
  tgles::ShaderManager sm;
  tgles::ProgramManager pm(&sm);
  const char* vs =
      "#version 320 es\nlayout(std140) uniform Camera { mat4 vp; };\n"
      "void main() {}";
  tgles::GLuint prog = pm.CreateProgram();
  pm.AttachShader(prog, Compile(sm, tgles::kGlVertexShader, vs));
  pm.AttachShader(prog, Compile(sm, tgles::kGlFragmentShader, kV320));
  pm.LinkProgram(prog);
  EXPECT_TRUE(pm.LinkSucceeded(prog));
  tgles::GLint blocks = 0;
  pm.GetProgramiv(prog, tgles::kGlActiveUniformBlocks, &blocks);
  EXPECT_EQ(blocks, 1);
  EXPECT_EQ(pm.GetUniformBlockIndex(prog, "Camera"), 0u);
  EXPECT_EQ(pm.GetUniformBlockIndex(prog, "Missing"),
            tgles::kGlInvalidIndex);
  EXPECT_EQ(pm.GetProgramResourceIndex(prog, tgles::kGlUniformBlockInterface,
                                       "Camera"),
            0u);
  pm.UniformBlockBinding(prog, 0, 2);
  EXPECT_EQ(pm.GetError(), tgles::kGlNoError);
  pm.UniformBlockBinding(prog, 0, 9999u);
  EXPECT_EQ(pm.GetError(), tgles::kGlInvalidValue);
  EXPECT_EQ(pm.GetError(), tgles::kGlNoError);
}

// Core TextureShadowLod area: depth-compare sampler/texture state.
TEST(CtsSubset, ShadowCompareState) {
  tgles::TextureManager tm;
  tgles::GLuint t = 0;
  tm.GenTextures(1, &t);
  tm.BindTexture(tgles::kGlTexture2d, t);
  tm.TexParameteri(tgles::kGlTexture2d, tgles::kGlTextureCompareMode,
                   tgles::kGlCompareRefToTexture);
  EXPECT_EQ(tm.GetError(), tgles::kGlNoError);
  tgles::SamplerManager smgr;
  tgles::GLuint s = 0;
  smgr.GenSamplers(1, &s);
  smgr.BindSampler(0, s);  // Gen reserves the name; Bind creates the object.
  smgr.SamplerParameteri(s, tgles::kGlTextureCompareMode,
                         tgles::kGlCompareRefToTexture);
  EXPECT_EQ(smgr.GetError(), tgles::kGlNoError);
}

// gles31 heritage, host-critical: image units + buffer storage.
TEST(CtsSubset, ImageUnitsAndStorageLimits) {
  tgles::Context ctx = tgles::Context::Create(false);
  tgles::GLint units = 0;
  ctx.GetIntegerv(tgles::kGlMaxImageUnitsQ, &units);
  EXPECT_EQ(units, tgles::kMaxImageUnitsValue);
  tgles::ImageUnitManager imgs;
  imgs.BindImageTexture(0, 1, 0, tgles::kGlFalse, 0, tgles::kGlReadOnly,
                        tgles::kGlRgba8);
  EXPECT_EQ(imgs.GetError(), tgles::kGlNoError);
  imgs.BindImageTexture(99, 1, 0, tgles::kGlFalse, 0, tgles::kGlReadOnly,
                        tgles::kGlRgba8);
  EXPECT_EQ(imgs.GetError(), tgles::kGlInvalidValue);
  tgles::BufferManager bufs;
  tgles::GLuint b = 0;
  bufs.GenBuffers(1, &b);
  bufs.BindBuffer(tgles::kGlArrayBuffer, b);
  bufs.BufferStorageEXT(tgles::kGlArrayBuffer, 16, nullptr,
                        tgles::kGlMapWriteBit);
  EXPECT_EQ(bufs.GetError(), tgles::kGlNoError);
  EXPECT_TRUE(bufs.IsImmutable(b));
  EXPECT_EQ(bufs.GetError(), tgles::kGlNoError);
}

// Table 4.2 async queries (CTS query area heritage).
TEST(CtsSubset, CoreQueryTargets) {
  tgles::QueryManager q;
  tgles::GLuint id = 0;
  q.GenQueries(1, &id);
  q.BeginQuery(tgles::QueryTargetEnum(tgles::QueryTarget::kAnySamplesPassed),
               id);
  EXPECT_EQ(q.GetError(), tgles::kGlNoError);
  q.EndQuery(tgles::QueryTargetEnum(tgles::QueryTarget::kAnySamplesPassed));
  EXPECT_EQ(q.GetError(), tgles::kGlNoError);
  EXPECT_TRUE(tgles::IsNonCoreQueryEnum("GL_TIME_ELAPSED"));
}
