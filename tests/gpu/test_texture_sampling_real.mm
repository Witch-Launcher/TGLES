// Test-first HONEST: fragment texture sampling must affect pixels.
// Ground truth: ES 3.2 spec §8 (sampler2D uniforms sample bound TEXTURE_2D)
// + GLSL ES 3.20 §4.7 (texture() in fragment). TGL's fixed MSL
// `frag_main { return in.color; }` ignores bound textures (see
// src/gpu/apple/metal_bridge_apple.mm kBridgeMsl + src/facade/gles.cpp
// SubmitVertices which never queries textures/samplers). This test FAILS
// until the textured pipeline lands. It uses ONLY public API + real Apple
// bridge pixels — no Mock, no guessing.

#import <Foundation/Foundation.h>
#if __has_include(<Metal/Metal.h>)
#import <Metal/Metal.h>
#define TGLES_TEX_HAS_METAL 1
#else
#define TGLES_TEX_HAS_METAL 0
#endif
#if __has_include(<QuartzCore/QuartzCore.h>)
#import <QuartzCore/QuartzCore.h>
#define TGLES_TEX_HAS_QUARTZ 1
#else
#define TGLES_TEX_HAS_QUARTZ 0
#endif

#include "test_framework.h"

#include "tgles/facade/gles.h"
#include "tgles/gpu/metal_bridge_apple.h"

#if TGLES_TEX_HAS_METAL && TGLES_TEX_HAS_QUARTZ

namespace {

void PresentAndWaitTex(tgles::metal_bridge::MetalBridge& bridge) {
  CAMetalLayer* layer = [CAMetalLayer layer];
  bridge.SetLayer((__bridge void*)layer, 16, 16);
  bridge.Present();
  bridge.WaitForCompletion(bridge.FrameSerial());
}

}  // namespace

// White vertex color x 2x2 checker texture (red/green/blue/white).
// Fullscreen triangle with UVs (0,0),(2,0),(0,2) so center (8,8) samples
// near UV (0.5,0.5) = boundary of 4 texels with NEAREST -> must NOT be white.
// If fragment ignores textures, center stays white (255,255,255) -> FAIL.
TEST(TextureSamplingReal, BoundTextureModulatesFragment) {
  auto bridge = tgles::metal_bridge::CreateAppleBridge();
  EXPECT_TRUE(bridge != nullptr);
  if (bridge == nullptr) return;
  if (!bridge->Initialize("Apple3")) return;
  tgles::GlesContext ctx = tgles::GlesContext::Create(false);

  static const float kPos[9] = {-1, -1, 0, 3, -1, 0, -1, 3, 0};
  static const float kCol[12] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
  // UV per vertex matching positions above. Center NDC (0,0) has barycentric
  // (0.5,0.25,0.25) so UV = 0.5*(0,0)+0.25*(1,0)+0.25*(0,1) = (0.25,0.25),
  // deterministically texel (0,0)=red with NEAREST+CLAMP.
  static const float kUv[6] = {0, 0, 1, 0, 0, 1};
  tgles::GLuint pb = 0, cb = 0, ub = 0;
  ctx.buffers().GenBuffers(1, &pb);
  ctx.buffers().GenBuffers(1, &cb);
  ctx.buffers().GenBuffers(1, &ub);
  ctx.buffers().BindBuffer(tgles::kGlArrayBuffer, pb);
  ctx.buffers().BufferData(tgles::kGlArrayBuffer, sizeof(kPos), kPos,
                           tgles::kGlStaticDraw);
  ctx.buffers().BindBuffer(tgles::kGlArrayBuffer, cb);
  ctx.buffers().BufferData(tgles::kGlArrayBuffer, sizeof(kCol), kCol,
                           tgles::kGlStaticDraw);
  ctx.buffers().BindBuffer(tgles::kGlArrayBuffer, ub);
  ctx.buffers().BufferData(tgles::kGlArrayBuffer, sizeof(kUv), kUv,
                           tgles::kGlStaticDraw);
  tgles::GLuint vao = 0;
  ctx.vertex_arrays().GenVertexArrays(1, &vao);
  ctx.vertex_arrays().BindVertexArray(vao);
  ctx.vertex_arrays().VertexAttribPointer(0, 3, tgles::kGlFloat, tgles::kGlFalse,
                                          0, 0, pb);
  ctx.vertex_arrays().VertexAttribPointer(1, 4, tgles::kGlFloat, tgles::kGlFalse,
                                          0, 0, cb);
  ctx.vertex_arrays().VertexAttribPointer(2, 2, tgles::kGlFloat, tgles::kGlFalse,
                                          0, 0, ub);
  ctx.vertex_arrays().EnableVertexAttribArray(0);
  ctx.vertex_arrays().EnableVertexAttribArray(1);
  ctx.vertex_arrays().EnableVertexAttribArray(2);

  // 2x2 RGBA checker: (0,0)=red, (1,0)=green, (0,1)=blue, (1,1)=white.
  // Bound to unit 1 so the draw-target creation on unit 0 below cannot
  // clobber the sampling binding (honest unit separation, ES 3.2 §8).
  static const std::uint8_t kChecker[16] = {
      255, 0,   0,   255,  // red
      0,   255, 0,   255,  // green
      0,   0,   255, 255,  // blue
      255, 255, 255, 255,  // white
  };
  tgles::GLuint tex = 0;
  ctx.textures().GenTextures(1, &tex);
  ctx.textures().ActiveTexture(0x84C0u + 1);
  ctx.textures().BindTexture(tgles::kGlTexture2d, tex);
  ctx.textures().TexImage2D(tgles::kGlTexture2d, 0, tgles::kGlRgba8, 2, 2, 0,
                            tgles::kGlRgba, tgles::kGlUnsignedByte, kChecker);
  ctx.textures().TexParameteri(tgles::kGlTexture2d, tgles::kGlTextureMinFilter,
                               tgles::kGlNearest);
  ctx.textures().TexParameteri(tgles::kGlTexture2d, tgles::kGlTextureMagFilter,
                               tgles::kGlNearest);
  ctx.textures().TexParameteri(tgles::kGlTexture2d, tgles::kGlTextureWrapS,
                               tgles::kGlClampToEdge);
  ctx.textures().TexParameteri(tgles::kGlTexture2d, tgles::kGlTextureWrapT,
                               tgles::kGlClampToEdge);
  EXPECT_EQ(ctx.textures().GetError(), tgles::kGlNoError);
  ctx.textures().ActiveTexture(0x84C0u + 0);

  // Program with sampler2D bound to unit 0.
  const char* vs =
      "#version 320 es\n"
      "layout(location=0) in vec4 a_pos;\n"
      "layout(location=1) in vec4 a_col;\n"
      "layout(location=2) in vec2 a_uv;\n"
      "out vec4 v_col; out vec2 v_uv;\n"
      "uniform mat4 u_modelViewProj;\n"
      "void main(){ v_col=a_col; v_uv=a_uv;"
      " gl_Position=u_modelViewProj*a_pos; }";
  const char* fs =
      "#version 320 es\nprecision mediump float;\n"
      "in vec4 v_col; in vec2 v_uv; uniform sampler2D u_tex;\n"
      "out vec4 o_col;\n"
      "void main(){ o_col = v_col * texture(u_tex, v_uv); }";
  tgles::GLuint v = ctx.shaders().CreateShader(tgles::kGlVertexShader);
  ctx.shaders().ShaderSource(v, 1, &vs, nullptr);
  ctx.shaders().CompileShader(v);
  tgles::GLuint f = ctx.shaders().CreateShader(tgles::kGlFragmentShader);
  ctx.shaders().ShaderSource(f, 1, &fs, nullptr);
  ctx.shaders().CompileShader(f);
  tgles::GLuint prog = ctx.programs().CreateProgram();
  ctx.programs().AttachShader(prog, v);
  ctx.programs().AttachShader(prog, f);
  ctx.programs().LinkProgram(prog);
  EXPECT_TRUE(ctx.programs().LinkSucceeded(prog));
  ctx.programs().UseProgram(prog);
  tgles::GLint tex_loc = ctx.programs().GetUniformLocation(prog, "u_tex");
  EXPECT_TRUE(tex_loc >= 0);
  ctx.programs().Uniform1i(tex_loc, 1);
  // u_modelViewProj identity (RenderFrame default).
  EXPECT_EQ(ctx.GetError(), tgles::kGlNoError);

  // Draw target FBO.
  tgles::GLuint t = 0;
  ctx.textures().GenTextures(1, &t);
  ctx.textures().BindTexture(tgles::kGlTexture2d, t);
  ctx.textures().TexImage2D(tgles::kGlTexture2d, 0, tgles::kGlRgba8, 16, 16, 0,
                            tgles::kGlRgba, tgles::kGlUnsignedByte, nullptr);
  tgles::GLuint fbo = 0;
  ctx.framebuffers().GenFramebuffers(1, &fbo);
  ctx.framebuffers().BindFramebuffer(tgles::kGlDrawFramebuffer, fbo);
  ctx.framebuffers().FramebufferTexture2D(
      tgles::kGlDrawFramebuffer, tgles::kGlColorAttachment0,
      tgles::kGlTexture2d, t, 0);
  EXPECT_EQ(ctx.GetError(), tgles::kGlNoError);

  EXPECT_TRUE(ctx.RenderFrame(*bridge, tgles::kGlTriangles, 0, 3));
  EXPECT_EQ(ctx.GetError(), tgles::kGlNoError);
  EXPECT_TRUE(bridge->HasFragmentTexture(1));
  PresentAndWaitTex(*bridge);
  std::uint8_t px[4] = {0};
  EXPECT_TRUE(bridge->ReadbackPixel(8, 8, px));
  // HONEST: white vertex color x red texel = red. Current fixed MSL returns
  // white (ignores texture) so this FAILS until the textured pipeline lands.
  EXPECT_EQ(px[0], 255u);
  EXPECT_EQ(px[1], 0u);
  EXPECT_EQ(px[2], 0u);
  EXPECT_EQ(px[3], 255u);
}

// Texture views sample the same bytes (EXT_texture_view honest v1):
// immutable original + sRGB view, sampler bound to the VIEW name. Bytes are
// shared live (shared_ptr levels), so sampling the view must equal sampling
// the original. This closes the "aliasing/storage only" gap noted in review.
TEST(TextureSamplingReal, ViewSamplesSameBytesAsOriginal) {
  auto bridge = tgles::metal_bridge::CreateAppleBridge();
  EXPECT_TRUE(bridge != nullptr);
  if (bridge == nullptr) return;
  if (!bridge->Initialize("Apple3")) return;
  tgles::GlesContext ctx = tgles::GlesContext::Create(false);

  static const float kPos[9] = {-1, -1, 0, 3, -1, 0, -1, 3, 0};
  static const float kCol[12] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
  static const float kUv[6] = {0, 0, 1, 0, 0, 1};
  tgles::GLuint pb = 0, cb = 0, ub = 0;
  ctx.buffers().GenBuffers(1, &pb);
  ctx.buffers().GenBuffers(1, &cb);
  ctx.buffers().GenBuffers(1, &ub);
  ctx.buffers().BindBuffer(tgles::kGlArrayBuffer, pb);
  ctx.buffers().BufferData(tgles::kGlArrayBuffer, sizeof(kPos), kPos,
                           tgles::kGlStaticDraw);
  ctx.buffers().BindBuffer(tgles::kGlArrayBuffer, cb);
  ctx.buffers().BufferData(tgles::kGlArrayBuffer, sizeof(kCol), kCol,
                           tgles::kGlStaticDraw);
  ctx.buffers().BindBuffer(tgles::kGlArrayBuffer, ub);
  ctx.buffers().BufferData(tgles::kGlArrayBuffer, sizeof(kUv), kUv,
                           tgles::kGlStaticDraw);
  tgles::GLuint vao = 0;
  ctx.vertex_arrays().GenVertexArrays(1, &vao);
  ctx.vertex_arrays().BindVertexArray(vao);
  ctx.vertex_arrays().VertexAttribPointer(0, 3, tgles::kGlFloat, tgles::kGlFalse,
                                          0, 0, pb);
  ctx.vertex_arrays().VertexAttribPointer(1, 4, tgles::kGlFloat, tgles::kGlFalse,
                                          0, 0, cb);
  ctx.vertex_arrays().VertexAttribPointer(2, 2, tgles::kGlFloat, tgles::kGlFalse,
                                          0, 0, ub);
  ctx.vertex_arrays().EnableVertexAttribArray(0);
  ctx.vertex_arrays().EnableVertexAttribArray(1);
  ctx.vertex_arrays().EnableVertexAttribArray(2);

  // Immutable 2x2 original filled red via sub-image, sRGB view of level 0.
  tgles::GLuint orig = 0, view = 0;
  ctx.textures().GenTextures(1, &orig);
  ctx.textures().GenTextures(1, &view);
  ctx.textures().ActiveTexture(0x84C0u + 1);
  ctx.textures().BindTexture(tgles::kGlTexture2d, orig);
  ctx.textures().TexStorage2D(tgles::kGlTexture2d, 1, tgles::kGlRgba8, 2, 2);
  EXPECT_EQ(ctx.textures().GetError(), tgles::kGlNoError);
  static const std::uint8_t kRed[16] = {
      255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255,
  };
  ctx.textures().TexSubImage2D(tgles::kGlTexture2d, 0, 0, 0, 2, 2,
                               tgles::kGlRgba, tgles::kGlUnsignedByte, kRed);
  EXPECT_EQ(ctx.textures().GetError(), tgles::kGlNoError);
  ctx.textures().TextureView(view, tgles::kGlTexture2d, orig,
                             tgles::kGlSrgb8Alpha8, 0, 1, 0, 1);
  EXPECT_EQ(ctx.textures().GetError(), tgles::kGlNoError);
  // Sampler binds the VIEW, not the original.
  ctx.textures().BindTexture(tgles::kGlTexture2d, view);
  ctx.textures().TexParameteri(tgles::kGlTexture2d, tgles::kGlTextureMinFilter,
                               tgles::kGlNearest);
  ctx.textures().TexParameteri(tgles::kGlTexture2d, tgles::kGlTextureMagFilter,
                               tgles::kGlNearest);
  ctx.textures().ActiveTexture(0x84C0u + 0);

  const char* vs =
      "#version 320 es\nlayout(location=0) in vec4 a_pos;\n"
      "layout(location=1) in vec4 a_col;\nlayout(location=2) in vec2 a_uv;\n"
      "out vec4 v_col; out vec2 v_uv; uniform mat4 u_modelViewProj;\n"
      "void main(){ v_col=a_col; v_uv=a_uv;"
      " gl_Position=u_modelViewProj*a_pos; }";
  const char* fs =
      "#version 320 es\nprecision mediump float;\n"
      "in vec4 v_col; in vec2 v_uv; uniform sampler2D u_tex; out vec4 o_col;\n"
      "void main(){ o_col = v_col * texture(u_tex, v_uv); }";
  tgles::GLuint v = ctx.shaders().CreateShader(tgles::kGlVertexShader);
  ctx.shaders().ShaderSource(v, 1, &vs, nullptr);
  ctx.shaders().CompileShader(v);
  tgles::GLuint f = ctx.shaders().CreateShader(tgles::kGlFragmentShader);
  ctx.shaders().ShaderSource(f, 1, &fs, nullptr);
  ctx.shaders().CompileShader(f);
  tgles::GLuint prog = ctx.programs().CreateProgram();
  ctx.programs().AttachShader(prog, v);
  ctx.programs().AttachShader(prog, f);
  ctx.programs().LinkProgram(prog);
  EXPECT_TRUE(ctx.programs().LinkSucceeded(prog));
  ctx.programs().UseProgram(prog);
  tgles::GLint tex_loc = ctx.programs().GetUniformLocation(prog, "u_tex");
  EXPECT_TRUE(tex_loc >= 0);
  ctx.programs().Uniform1i(tex_loc, 1);

  tgles::GLuint t = 0;
  ctx.textures().GenTextures(1, &t);
  ctx.textures().ActiveTexture(0x84C0u + 0);
  ctx.textures().BindTexture(tgles::kGlTexture2d, t);
  ctx.textures().TexImage2D(tgles::kGlTexture2d, 0, tgles::kGlRgba8, 16, 16, 0,
                            tgles::kGlRgba, tgles::kGlUnsignedByte, nullptr);
  tgles::GLuint fbo = 0;
  ctx.framebuffers().GenFramebuffers(1, &fbo);
  ctx.framebuffers().BindFramebuffer(tgles::kGlDrawFramebuffer, fbo);
  ctx.framebuffers().FramebufferTexture2D(
      tgles::kGlDrawFramebuffer, tgles::kGlColorAttachment0,
      tgles::kGlTexture2d, t, 0);
  EXPECT_EQ(ctx.GetError(), tgles::kGlNoError);

  EXPECT_TRUE(ctx.RenderFrame(*bridge, tgles::kGlTriangles, 0, 3));
  EXPECT_EQ(ctx.GetError(), tgles::kGlNoError);
  PresentAndWaitTex(*bridge);
  std::uint8_t px[4] = {0};
  EXPECT_TRUE(bridge->ReadbackPixel(8, 8, px));
  EXPECT_EQ(px[0], 255u);
  EXPECT_EQ(px[1], 0u);
  EXPECT_EQ(px[2], 0u);
  EXPECT_EQ(px[3], 255u);
}

#else

TEST(TextureSamplingReal, NoMetalHostSkips) { EXPECT_TRUE(true); }

#endif
