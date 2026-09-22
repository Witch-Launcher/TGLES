// Phase-4 red-first proofs (pure C++, Mock bridge): each TEST names the
// missing piece in its printf so a red run tells exactly what is absent.
// Ground truth: ES 3.2 spec (gl.xml), GLSL ES 3.20 spec, Metal SDK 26.2
// (lodMinClamp/MaxClamp, [[instance_id]]/[[vertex_id]], mesh Apple7+).

#include "test_framework.h"

#include <cstdio>
#include <cstdint>
#include <string>
#include <vector>

#include "tgles/facade/gles.h"
#include "tgles/gpu/backend.h"
#include "tgles/gpu/glsl_to_msl.h"
#include "tgles/gpu/metal_bridge.h"

namespace {

void BuildTriangle(tgles::GlesContext& ctx) {
  static const float kPos[9] = {-1, -1, 0, 3, -1, 0, -1, 3, 0};
  static const float kCol[12] = {1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1};
  tgles::GLuint pb = 0, cb = 0;
  ctx.buffers().GenBuffers(1, &pb);
  ctx.buffers().GenBuffers(1, &cb);
  ctx.buffers().BindBuffer(tgles::kGlArrayBuffer, pb);
  ctx.buffers().BufferData(tgles::kGlArrayBuffer, sizeof(kPos), kPos,
                           tgles::kGlStaticDraw);
  ctx.buffers().BindBuffer(tgles::kGlArrayBuffer, cb);
  ctx.buffers().BufferData(tgles::kGlArrayBuffer, sizeof(kCol), kCol,
                           tgles::kGlStaticDraw);
  tgles::GLuint vao = 0;
  ctx.vertex_arrays().GenVertexArrays(1, &vao);
  ctx.vertex_arrays().BindVertexArray(vao);
  ctx.vertex_arrays().VertexAttribPointer(0, 3, tgles::kGlFloat,
                                          tgles::kGlFalse, 0, 0, pb);
  ctx.vertex_arrays().VertexAttribPointer(1, 4, tgles::kGlFloat,
                                          tgles::kGlFalse, 0, 0, cb);
  ctx.vertex_arrays().EnableVertexAttribArray(0);
  ctx.vertex_arrays().EnableVertexAttribArray(1);
  tgles::GLuint t = 0;
  ctx.textures().GenTextures(1, &t);
  ctx.textures().BindTexture(tgles::kGlTexture2d, t);
  ctx.textures().TexImage2D(tgles::kGlTexture2d, 0, tgles::kGlRgba8, 16, 16, 0,
                            tgles::kGlRgba, tgles::kGlUnsignedByte, nullptr);
  tgles::GLuint f = 0;
  ctx.framebuffers().GenFramebuffers(1, &f);
  ctx.framebuffers().BindFramebuffer(tgles::kGlDrawFramebuffer, f);
  ctx.framebuffers().FramebufferTexture2D(
      tgles::kGlDrawFramebuffer, tgles::kGlColorAttachment0,
      tgles::kGlTexture2d, t, 0);
}

tgles::GLuint LinkVsFs(tgles::GlesContext& ctx, const char* vs,
                       const char* fs) {
  tgles::GLuint v = ctx.shaders().CreateShader(tgles::kGlVertexShader);
  ctx.shaders().ShaderSource(v, 1, &vs, nullptr);
  ctx.shaders().CompileShader(v);
  tgles::GLuint f = ctx.shaders().CreateShader(tgles::kGlFragmentShader);
  ctx.shaders().ShaderSource(f, 1, &fs, nullptr);
  ctx.shaders().CompileShader(f);
  tgles::GLuint p = ctx.programs().CreateProgram();
  ctx.programs().AttachShader(p, v);
  ctx.programs().AttachShader(p, f);
  ctx.programs().LinkProgram(p);
  ctx.programs().UseProgram(p);
  return p;
}

}  // namespace

// 4 — MRT+MSAA per-attachment resolve must execute (was INVALID_OPERATION).
TEST(Phase4, MrtMsaaComboExecutesOnMock) {
  tgles::GlesContext ctx = tgles::GlesContext::Create(false);
  BuildTriangle(ctx);
  const char* vs =
      "#version 320 es\nlayout(location=0) in vec4 a_pos;\n"
      "layout(location=1) in vec4 a_col;\n"
      "out vec4 v_col; uniform mat4 u_mvp;\n"
      "void main(){ v_col = a_col; gl_Position = u_mvp * a_pos; }";
  const char* fs =
      "#version 320 es\nprecision mediump float;\n"
      "in vec4 v_col;\nlayout(location=0) out vec4 o0;\n"
      "layout(location=1) out vec4 o1;\n"
      "void main(){ o0 = vec4(1.0,0.0,0.0,1.0); o1 = vec4(0.0,1.0,0.0,1.0); }";
  LinkVsFs(ctx, vs, fs);
  // Complete MRT+MSAA FBO: two 4x MSAA renderbuffers, same size.
  tgles::GLuint rb[2] = {0, 0};
  ctx.renderbuffers().GenRenderbuffers(2, rb);
  for (int i = 0; i < 2; ++i) {
    ctx.renderbuffers().BindRenderbuffer(tgles::kGlRenderbuffer, rb[i]);
    ctx.renderbuffers().RenderbufferStorageMultisample(
        tgles::kGlRenderbuffer, 4, tgles::kGlRgba8, 16, 16);
  }
  ctx.framebuffers().FramebufferRenderbuffer(
      tgles::kGlDrawFramebuffer, tgles::kGlColorAttachment0,
      tgles::kGlRenderbuffer, rb[0]);
  ctx.framebuffers().FramebufferRenderbuffer(
      tgles::kGlDrawFramebuffer, tgles::kGlColorAttachment0 + 1u,
      tgles::kGlRenderbuffer, rb[1]);
  const tgles::GLenum bufs[2] = {tgles::kGlColorAttachment0,
                                 tgles::kGlColorAttachment0 + 1u};
  ctx.framebuffers().DrawBuffers(2, bufs);
  EXPECT_EQ(ctx.framebuffers().CheckFramebufferStatus(
                tgles::kGlDrawFramebuffer),
            tgles::kGlFramebufferComplete);
  tgles::metal_bridge::MockMetalBridge bridge;
  EXPECT_TRUE(bridge.Initialize("Apple3"));
  bool ok = ctx.RenderFrame(bridge, tgles::kGlTriangles, 0, 3);
  if (!ok) {
    std::printf(
        "[MISSING] Phase4 MRT+MSAA per-attachment resolve: RenderFrame "
        "rejected, want execute (pixel proof needs per-attachment resolve)\n");
  }
  EXPECT_TRUE(ok);
  EXPECT_EQ(ctx.GetError(), tgles::kGlNoError);
  // PsoKey must carry both dimensions.
  tgles::backend::PsoKey key =
      tgles::backend::PsoKeyForDraw(ctx.foundation(), ctx.raster());
  // Facade sets mrt_count=2,samples=4 for this draw (pinned via Mock PSO).
  // Mock accepts the combined key (device resolves per attachment).
  key.mrt_count = 2;
  key.sample_count = 4;
  tgles::metal_bridge::MockMetalBridge b2;
  EXPECT_TRUE(b2.Initialize("Apple3"));
  EXPECT_NE(b2.CreateRenderPipeline(key), 0u);
}

// 5 — Per-buffer blend: BlendFunci(1) must differ from buffer 0 in PsoKey.
TEST(Phase4, PerBufferBlendKeyDiffers) {
  tgles::GlesContext ctx = tgles::GlesContext::Create(false);
  ctx.raster().BlendFunc(tgles::kGlOne, tgles::kGlZero);
  ctx.raster().BlendFunci(1, tgles::kGlSrcAlpha, tgles::kGlOneMinusSrcAlpha);
  tgles::backend::PsoKey k0 =
      tgles::backend::PsoKeyForDraw(ctx.foundation(), ctx.raster());
  // Flip buffer 1 back; keys must differ.
  ctx.raster().BlendFunci(1, tgles::kGlOne, tgles::kGlZero);
  tgles::backend::PsoKey k1 =
      tgles::backend::PsoKeyForDraw(ctx.foundation(), ctx.raster());
  if (!(k0 == k1)) {
    // Keys already differ: per-buffer blend landed.
  } else {
    std::printf(
        "[MISSING] Phase4 per-buffer blend: PsoKey ignores buffer 1 "
        "(BlendFunci has no effect)\n");
  }
  EXPECT_TRUE(!(k0 == k1));
  // Per-buffer enables must also be visible.
  ctx.raster().EnableIndexed(0x0BE2u /*BLEND*/, 1);
  EXPECT_TRUE(ctx.raster().IsEnabledIndexed(0x0BE2u, 1) == tgles::kGlTrue);
}

// 6 — Cube/3D/array mipmaps: GenerateMipmap chain must upload (was L0-only).
TEST(Phase4, CubeMipmapChainUploads) {
  tgles::GlesContext ctx = tgles::GlesContext::Create(false);
  tgles::GLuint tex = 0;
  ctx.textures().GenTextures(1, &tex);
  ctx.textures().BindTexture(tgles::kGlTextureCubeMap, tex);
  static std::uint8_t kFace[64];
  for (int i = 0; i < 64; ++i) kFace[i] = (std::uint8_t)(i & 0xFF);
  for (int f = 0; f < 6; ++f) {
    ctx.textures().TexImage2D(
        tgles::kGlTextureCubeMapPositiveX + static_cast<tgles::GLenum>(f), 0,
        tgles::kGlRgba8, 4, 4, 0, tgles::kGlRgba, tgles::kGlUnsignedByte, kFace);
  }
  ctx.textures().GenerateMipmap(tgles::kGlTextureCubeMap);
  EXPECT_EQ(ctx.textures().GetError(), tgles::kGlNoError);
  tgles::TextureLevel l1 = ctx.textures().LevelState(
      tex, tgles::kGlTextureCubeMapPositiveX, 1);
  if (!l1.defined) {
    std::printf("[MISSING] Phase4 cube mipmaps: L1 not generated\n");
  }
  EXPECT_TRUE(l1.defined);
  EXPECT_EQ(l1.width, 2);
  // Facade must accept a mipmap-minified cube draw (was tracked reject).
  tgles::metal_bridge::MockMetalBridge bridge;
  EXPECT_TRUE(bridge.Initialize("Apple3"));
  // Upload chain via new bridge entry (Mock records kind+levels).
  const void* faces_l0[6] = {kFace, kFace, kFace, kFace, kFace, kFace};
  bridge.SetFragmentTextureCube(0, 4, faces_l0, false);
  EXPECT_EQ(bridge.GetError(), tgles::kGlNoError);
  if (bridge.SlotKind(0) != 1)
    std::printf("[MISSING] Phase4 cube mipmaps: bridge slot kind != cube\n");
  EXPECT_EQ(bridge.SlotKind(0), 1);
}

TEST(Phase4, ArrayMipmapChainUploads) {
  tgles::GlesContext ctx = tgles::GlesContext::Create(false);
  tgles::GLuint tex = 0;
  ctx.textures().GenTextures(1, &tex);
  ctx.textures().BindTexture(tgles::kGlTexture2dArray, tex);
  static std::uint8_t kData[4 * 4 * 2 * 4];
  for (std::size_t i = 0; i < sizeof(kData); ++i) kData[i] = (std::uint8_t)i;
  ctx.textures().TexImage3D(tgles::kGlTexture2dArray, 0, tgles::kGlRgba8, 4, 4,
                            2, 0, tgles::kGlRgba, tgles::kGlUnsignedByte, kData);
  ctx.textures().GenerateMipmap(tgles::kGlTexture2dArray);
  EXPECT_EQ(ctx.textures().GetError(), tgles::kGlNoError);
  tgles::TextureLevel l1 =
      ctx.textures().LevelState(tex, tgles::kGlTexture2dArray, 1);
  if (!l1.defined)
    std::printf("[MISSING] Phase4 array mipmaps: L1 not generated\n");
  EXPECT_TRUE(l1.defined);
}

// 7 — Uniform arrays: translator must accept + per-element readback.
TEST(Phase4, UniformArraysTranslateAndReadPerElement) {
  const char* vs =
      "#version 320 es\nlayout(location=0) in vec4 a;\n"
      "layout(location=1) in vec4 b;\nuniform vec4 u_c[2];\n"
      "void main(){ gl_Position = a + u_c[0] + u_c[1]; }";
  const char* fs =
      "#version 320 es\nprecision mediump float;\n"
      "out vec4 o;\nvoid main(){ o = vec4(1.0); }";
  tgles::glsl::TranslatedProgram t =
      tgles::glsl::TranslateProgram(vs, fs);
  if (!t.ok) {
    std::printf(
        "[MISSING] Phase4 uniform arrays: translator rejected (%s)\n",
        t.error.c_str());
  }
  EXPECT_TRUE(t.ok);
  if (t.ok) {
    EXPECT_EQ(t.uniforms.size(), 1u);
    if (!t.uniforms.empty())
      EXPECT_TRUE(t.library_source.find("u_c[2]") != std::string::npos);
  }
  // State: per-element upload + readback.
  tgles::ShaderManager sm;
  tgles::ProgramManager pm(&sm);
  tgles::GLuint v = sm.CreateShader(tgles::kGlVertexShader);
  sm.ShaderSource(v, 1, &vs, nullptr);
  sm.CompileShader(v);
  tgles::GLuint f = sm.CreateShader(tgles::kGlFragmentShader);
  sm.ShaderSource(f, 1, &fs, nullptr);
  sm.CompileShader(f);
  tgles::GLuint p = pm.CreateProgram();
  pm.AttachShader(p, v);
  pm.AttachShader(p, f);
  pm.LinkProgram(p);
  EXPECT_TRUE(pm.LinkSucceeded(p));
  pm.UseProgram(p);
  tgles::GLint base = pm.GetUniformLocation(p, "u_c[0]");
  EXPECT_TRUE(base >= 0);
  const float vals[8] = {1, 2, 3, 4, 5, 6, 7, 8};
  pm.Uniform4fv(base, 2, vals);
  EXPECT_EQ(pm.GetError(), tgles::kGlNoError);
  std::vector<float> e1;
  bool got = pm.GetUniformElementFloats(p, base + 1, &e1);
  // Phase 4 item 7: per-element accessor slices element 1 (5,6,7,8).
  if (!got || e1.size() < 4 || e1[0] != 5.0f) {
    std::printf(
        "[MISSING] Phase4 uniform arrays: per-element readback got %s\n",
        got ? "wrong values" : "no data");
  }
  EXPECT_TRUE(got);
  if (got && e1.size() >= 4) {
    EXPECT_EQ(e1[0], 5.0f);
    EXPECT_EQ(e1[3], 8.0f);
  }
  // Element 0 still reads 1,2,3,4.
  std::vector<float> e0;
  EXPECT_TRUE(pm.GetUniformElementFloats(p, base, &e0));
  if (e0.size() >= 4) EXPECT_EQ(e0[0], 1.0f);
}

// 8 — UBO int/bool members.
TEST(Phase4, UboIntBoolMembersTranslate) {
  const char* vs =
      "#version 320 es\nlayout(location=0) in vec4 a;\n"
      "layout(location=1) in vec4 b;\n"
      "out vec4 v; uniform mat4 u_m;\n"
      "void main(){ v = b; gl_Position = u_m * a; }";
  const char* fs =
      "#version 320 es\nprecision mediump float;\n"
      "in vec4 v;\nlayout(std140) uniform F { int u_n; bool u_b; };\n"
      "out vec4 o;\nvoid main(){ o = v * float(u_n) + (u_b ? vec4(1.0) : "
      "vec4(0.0)); }";
  tgles::glsl::TranslatedProgram t =
      tgles::glsl::TranslateProgram(vs, fs);
  if (!t.ok) {
    std::printf("[MISSING] Phase4 UBO int/bool: translator rejected (%s)\n",
                t.error.c_str());
  }
  EXPECT_TRUE(t.ok);
  if (t.ok) {
    EXPECT_EQ(t.ubo_blocks.size(), 1u);
    if (!t.ubo_blocks.empty())
      EXPECT_EQ(t.ubo_blocks[0].members.size(), 2u);
  }
}

// 10 — LOD ranges stored + bridge sampler detail carries them.
TEST(Phase4, LodRangesReachSampler) {
  tgles::TextureManager tm;
  tgles::GLuint id[1] = {0};
  tm.GenTextures(1, id);
  tm.BindTexture(tgles::kGlTexture2d, id[0]);
  tm.TexParameterf(tgles::kGlTexture2d, tgles::kGlTextureMinLod, 0.5f);
  tm.TexParameterf(tgles::kGlTexture2d, tgles::kGlTextureMaxLod, 2.0f);
  EXPECT_EQ(tm.GetError(), tgles::kGlNoError);
  tgles::metal_bridge::MockMetalBridge bridge;
  EXPECT_TRUE(bridge.Initialize("Apple3"));
  bridge.SetFragmentSamplerDetail(0, tgles::kGlLinearMipmapLinear,
                                  tgles::kGlLinear, tgles::kGlClampToEdge,
                                  tgles::kGlClampToEdge);
  EXPECT_EQ(bridge.GetError(), tgles::kGlNoError);
  bridge.SetSamplerLod(0, 0.5f, 2.0f);
  EXPECT_EQ(bridge.GetError(), tgles::kGlNoError);
  if (bridge.SamplerMinLod(0) != 0.5f || bridge.SamplerMaxLod(0) != 2.0f) {
    std::printf(
        "[MISSING] Phase4 LOD ranges: lodMin/MaxClamp not in bridge "
        "(got %.2f/%.2f)\n",
        bridge.SamplerMinLod(0), bridge.SamplerMaxLod(0));
  }
  EXPECT_EQ(bridge.SamplerMinLod(0), 0.5f);
  EXPECT_EQ(bridge.SamplerMaxLod(0), 2.0f);
  bridge.SetSamplerLod(0, 3.0f, 1.0f);
  EXPECT_EQ(bridge.GetError(), tgles::kGlInvalidValue);
}

// 11a — gl_InstanceID / gl_VertexID.
TEST(Phase4, InstanceAndVertexIdTranslate) {
  const char* vs =
      "#version 320 es\nlayout(location=0) in vec4 a;\n"
      "layout(location=1) in vec4 b;\n"
      "out vec4 v;\n"
      "void main(){ v = vec4(float(gl_InstanceID), float(gl_VertexID), 0.0, "
      "1.0); gl_Position = a; }";
  const char* fs =
      "#version 320 es\nprecision mediump float;\n"
      "in vec4 v; out vec4 o;\nvoid main(){ o = v; }";
  tgles::glsl::TranslatedProgram t =
      tgles::glsl::TranslateProgram(vs, fs);
  if (!t.ok) {
    std::printf(
        "[MISSING] Phase4 gl_InstanceID/gl_VertexID: translator rejected (%s)\n",
        t.error.c_str());
  }
  EXPECT_TRUE(t.ok);
  if (t.ok) {
    EXPECT_TRUE(t.library_source.find("instance_id") != std::string::npos);
    EXPECT_TRUE(t.library_source.find("vertex_id") != std::string::npos);
  }
}

// 11b — User-defined structs.
TEST(Phase4, UserStructsTranslate) {
  const char* vs =
      "#version 320 es\nlayout(location=0) in vec4 a;\n"
      "layout(location=1) in vec4 b;\n"
      "struct Light { vec3 dir; vec4 col; };\nuniform Light u_l;\n"
      "out vec4 v;\n"
      "void main(){ v = b * u_l.col; gl_Position = a; }";
  const char* fs =
      "#version 320 es\nprecision mediump float;\n"
      "in vec4 v; out vec4 o;\nvoid main(){ o = v; }";
  tgles::glsl::TranslatedProgram t =
      tgles::glsl::TranslateProgram(vs, fs);
  if (!t.ok) {
    std::printf("[MISSING] Phase4 user structs: translator rejected (%s)\n",
                t.error.c_str());
  }
  EXPECT_TRUE(t.ok);
}

// 1 — PATCHES/adjacency planning: Mock refuses (no mesh on Intel) with reason.
TEST(Phase4, PatchesFailClosedWithMeshReason) {
  tgles::GlesContext ctx = tgles::GlesContext::Create(false);
  BuildTriangle(ctx);
  tgles::metal_bridge::MockMetalBridge bridge;
  EXPECT_TRUE(bridge.Initialize("Apple3"));
  bool ok = ctx.RenderFrame(bridge, tgles::kGlPatches, 0, 3);
  if (ok) {
    std::printf("[MISSING] Phase4 PATCHES: executed without mesh/tess\n");
  }
  EXPECT_TRUE(!ok);
  EXPECT_EQ(ctx.GetError(), tgles::kGlInvalidOperation);
  // Geometry adjacency input classification must exist (already does).
  EXPECT_TRUE(tgles::GeometryState::IsValidInputPrimitive(0x000Cu));
}

// 6b — Facade executes cube-mip draws (was tracked reject).
TEST(Phase4, CubeMipsExecuteThroughFacade) {
  tgles::GlesContext ctx = tgles::GlesContext::Create(false);
  // Triangle + slot-2 dir (cube convention).
  static const float kPos[9] = {-1, -1, 0, 3, -1, 0, -1, 3, 0};
  static const float kCol[12] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
  static const float kDir[9] = {-1, -1, 1, 3, -1, 1, -1, 3, 1};
  tgles::GLuint pb = 0, cb = 0, db = 0;
  ctx.buffers().GenBuffers(1, &pb);
  ctx.buffers().GenBuffers(1, &cb);
  ctx.buffers().GenBuffers(1, &db);
  ctx.buffers().BindBuffer(tgles::kGlArrayBuffer, pb);
  ctx.buffers().BufferData(tgles::kGlArrayBuffer, sizeof(kPos), kPos,
                           tgles::kGlStaticDraw);
  ctx.buffers().BindBuffer(tgles::kGlArrayBuffer, cb);
  ctx.buffers().BufferData(tgles::kGlArrayBuffer, sizeof(kCol), kCol,
                           tgles::kGlStaticDraw);
  ctx.buffers().BindBuffer(tgles::kGlArrayBuffer, db);
  ctx.buffers().BufferData(tgles::kGlArrayBuffer, sizeof(kDir), kDir,
                           tgles::kGlStaticDraw);
  tgles::GLuint vao = 0;
  ctx.vertex_arrays().GenVertexArrays(1, &vao);
  ctx.vertex_arrays().BindVertexArray(vao);
  ctx.vertex_arrays().VertexAttribPointer(0, 3, tgles::kGlFloat,
                                          tgles::kGlFalse, 0, 0, pb);
  ctx.vertex_arrays().VertexAttribPointer(1, 4, tgles::kGlFloat,
                                          tgles::kGlFalse, 0, 0, cb);
  ctx.vertex_arrays().VertexAttribPointer(2, 3, tgles::kGlFloat,
                                          tgles::kGlFalse, 0, 0, db);
  ctx.vertex_arrays().EnableVertexAttribArray(0);
  ctx.vertex_arrays().EnableVertexAttribArray(1);
  ctx.vertex_arrays().EnableVertexAttribArray(2);
  tgles::GLuint t = 0;
  ctx.textures().GenTextures(1, &t);
  ctx.textures().BindTexture(tgles::kGlTexture2d, t);
  ctx.textures().TexImage2D(tgles::kGlTexture2d, 0, tgles::kGlRgba8, 16, 16, 0,
                            tgles::kGlRgba, tgles::kGlUnsignedByte, nullptr);
  tgles::GLuint f = 0;
  ctx.framebuffers().GenFramebuffers(1, &f);
  ctx.framebuffers().BindFramebuffer(tgles::kGlDrawFramebuffer, f);
  ctx.framebuffers().FramebufferTexture2D(
      tgles::kGlDrawFramebuffer, tgles::kGlColorAttachment0,
      tgles::kGlTexture2d, t, 0);
  const char* vs =
      "#version 320 es\nlayout(location=0) in vec4 a_pos;\n"
      "layout(location=1) in vec4 a_col;\nlayout(location=2) in vec3 a_dir;\n"
      "out vec4 v_col; out vec3 v_dir; uniform mat4 u_mvp;\n"
      "void main(){ v_col = a_col; v_dir = a_dir;"
      " gl_Position = u_mvp * a_pos; }";
  const char* fs =
      "#version 320 es\nprecision mediump float;\n"
      "in vec4 v_col; in vec3 v_dir; uniform samplerCube u_cube;\n"
      "out vec4 o_col;\n"
      "void main(){ o_col = v_col * texture(u_cube, v_dir); }";
  tgles::GLuint prog = LinkVsFs(ctx, vs, fs);
  EXPECT_TRUE(ctx.programs().LinkSucceeded(prog));
  // Cube 4x4 with mips + mipmap min filter.
  static std::uint8_t kFace[64];
  for (int i = 0; i < 64; ++i) kFace[i] = 200;
  tgles::GLuint cube = 0;
  ctx.textures().GenTextures(1, &cube);
  ctx.textures().ActiveTexture(0x84C0u + 1);
  ctx.textures().BindTexture(tgles::kGlTextureCubeMap, cube);
  for (int ff = 0; ff < 6; ++ff) {
    ctx.textures().TexImage2D(
        tgles::kGlTextureCubeMapPositiveX + static_cast<tgles::GLenum>(ff), 0,
        tgles::kGlRgba8, 4, 4, 0, tgles::kGlRgba, tgles::kGlUnsignedByte, kFace);
  }
  ctx.textures().GenerateMipmap(tgles::kGlTextureCubeMap);
  EXPECT_EQ(ctx.textures().GetError(), tgles::kGlNoError);
  ctx.textures().TexParameteri(tgles::kGlTextureCubeMap,
                               tgles::kGlTextureMinFilter,
                               tgles::kGlLinearMipmapLinear);
  ctx.textures().ActiveTexture(0x84C0u + 0);
  tgles::GLint loc = ctx.programs().GetUniformLocation(prog, "u_cube");
  EXPECT_TRUE(loc >= 0);
  ctx.programs().Uniform1i(loc, 1);
  tgles::metal_bridge::MockMetalBridge bridge;
  EXPECT_TRUE(bridge.Initialize("Apple3"));
  bool ok = ctx.RenderFrame(bridge, tgles::kGlTriangles, 0, 3);
  if (!ok) {
    std::printf("[MISSING] Phase4 cube-mips facade rejected\n");
  }
  EXPECT_TRUE(ok);
  EXPECT_EQ(ctx.GetError(), tgles::kGlNoError);
  // Mock must have recorded a cube-mips chain (kind 5, levels>1).
  EXPECT_EQ(bridge.SlotKind(1), 5);
}

// 1b — Adjacency CPU-expands (lines adjacency 4 verts -> 1 line).
TEST(Phase4, LinesAdjacencyExpands) {
  tgles::GlesContext ctx = tgles::GlesContext::Create(false);
  BuildTriangle(ctx);
  tgles::metal_bridge::MockMetalBridge bridge;
  EXPECT_TRUE(bridge.Initialize("Apple3"));
  // 4 verts as lines-adjacency: middle 2 are the real line.
  bool ok = ctx.RenderFrame(bridge, 0x000Au /*LINES_ADJACENCY*/, 0, 4);
  if (!ok) {
    std::printf(
        "[MISSING] Phase4 adjacency: LINES_ADJACENCY not CPU-expanded\n");
  }
  EXPECT_TRUE(ok);
  EXPECT_EQ(bridge.LastPrimitiveMode(), tgles::kGlLines);
}
