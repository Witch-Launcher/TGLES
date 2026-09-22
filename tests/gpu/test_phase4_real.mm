// Phase-4 device pixel proofs (Apple only, real pixels). Ground truth: ES 3.2
// spec §9.4 (MRT), §14.x (MSAA resolve, per-buffer blend), §8.14 (mips),
// GLSL ES 3.20 (uniform arrays, UBO int, structs, InstanceID). Each TEST prints
// the missing piece on failure (printf pixel) and the bridge logs TGL-DEBUG.

#import <Foundation/Foundation.h>
#if __has_include(<Metal/Metal.h>)
#import <Metal/Metal.h>
#define TGLES_P4_HAS_METAL 1
#else
#define TGLES_P4_HAS_METAL 0
#endif
#if __has_include(<QuartzCore/QuartzCore.h>)
#import <QuartzCore/QuartzCore.h>
#define TGLES_P4_HAS_QUARTZ 1
#else
#define TGLES_P4_HAS_QUARTZ 0
#endif

#include <cstdio>

#include "test_framework.h"

#include "tgles/facade/gles.h"
#include "tgles/gpu/metal_bridge_apple.h"

#if TGLES_P4_HAS_METAL && TGLES_P4_HAS_QUARTZ

namespace {

void PresentAndWait(tgles::metal_bridge::MetalBridge& bridge) {
  CAMetalLayer* layer = [CAMetalLayer layer];
  bridge.SetLayer((__bridge void*)layer, 16, 16);
  bridge.Present();
  bridge.WaitForCompletion(bridge.FrameSerial());
}

void BuildPosCol(tgles::GlesContext& ctx) {
  static const float kPos[9] = {-1, -1, 0, 3, -1, 0, -1, 3, 0};
  static const float kCol[12] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
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

tgles::GLuint Link(tgles::GlesContext& ctx, const char* vs, const char* fs) {
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

// MRT+MSAA combo: two 4x MSAA renderbuffers, MRT red+green. Per-attachment
// resolve must deliver red on 0 and green on 1 (single-resolve would leave 1
// black).
TEST(Phase4Real, MrtMsaaComboResolvesBoth) {
  auto bridge = tgles::metal_bridge::CreateAppleBridge();
  EXPECT_TRUE(bridge != nullptr);
  if (bridge == nullptr) return;
  if (!bridge->Initialize("Apple3")) return;
  tgles::GlesContext ctx = tgles::GlesContext::Create(false);
  BuildPosCol(ctx);
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
  tgles::GLuint prog = Link(ctx, vs, fs);
  EXPECT_TRUE(ctx.programs().LinkSucceeded(prog));
  tgles::GLuint rb[2] = {0, 0};
  ctx.renderbuffers().GenRenderbuffers(2, rb);
  for (int i = 0; i < 2; ++i) {
    ctx.renderbuffers().BindRenderbuffer(tgles::kGlRenderbuffer, rb[i]);
    ctx.renderbuffers().RenderbufferStorageMultisample(
        tgles::kGlRenderbuffer, 4, tgles::kGlRgba8, 16, 16);
  }
  tgles::GLuint fbo = 0;
  ctx.framebuffers().GenFramebuffers(1, &fbo);
  ctx.framebuffers().BindFramebuffer(tgles::kGlDrawFramebuffer, fbo);
  ctx.framebuffers().FramebufferRenderbuffer(
      tgles::kGlDrawFramebuffer, tgles::kGlColorAttachment0,
      tgles::kGlRenderbuffer, rb[0]);
  ctx.framebuffers().FramebufferRenderbuffer(
      tgles::kGlDrawFramebuffer, tgles::kGlColorAttachment0 + 1u,
      tgles::kGlRenderbuffer, rb[1]);
  const tgles::GLenum bufs[2] = {tgles::kGlColorAttachment0,
                                 tgles::kGlColorAttachment0 + 1u};
  ctx.framebuffers().DrawBuffers(2, bufs);
  EXPECT_TRUE(ctx.RenderFrame(*bridge, tgles::kGlTriangles, 0, 3));
  EXPECT_EQ(ctx.GetError(), tgles::kGlNoError);
  PresentAndWait(*bridge);
  std::uint8_t p0[4] = {0}, p1[4] = {0};
  EXPECT_TRUE(bridge->ReadbackAttachment(0, 8, 8, p0));
  EXPECT_TRUE(bridge->ReadbackAttachment(1, 8, 8, p1));
  if (!(p0[0] == 255u && p1[1] == 255u)) {
    std::printf("[MISSING] Phase4 MRT+MSAA: got (%u,%u,%u)/(%u,%u,%u)\n",
                p0[0], p0[1], p0[2], p1[0], p1[1], p1[2]);
  }
  EXPECT_EQ(p0[0], 255u);
  EXPECT_EQ(p1[1], 255u);
}

// Per-buffer blend: buffer 0 opaque red, buffer 1 blended (SRC_ALPHA).
// Different equations per attachment prove the descriptor loop.
TEST(Phase4Real, PerBufferBlendDiffers) {
  auto bridge = tgles::metal_bridge::CreateAppleBridge();
  EXPECT_TRUE(bridge != nullptr);
  if (bridge == nullptr) return;
  if (!bridge->Initialize("Apple3")) return;
  tgles::GlesContext ctx = tgles::GlesContext::Create(false);
  BuildPosCol(ctx);
  const char* vs =
      "#version 320 es\nlayout(location=0) in vec4 a_pos;\n"
      "layout(location=1) in vec4 a_col;\n"
      "out vec4 v_col; uniform mat4 u_mvp;\n"
      "void main(){ v_col = a_col; gl_Position = u_mvp * a_pos; }";
  const char* fs =
      "#version 320 es\nprecision mediump float;\n"
      "in vec4 v_col;\nlayout(location=0) out vec4 o0;\n"
      "layout(location=1) out vec4 o1;\n"
      "void main(){ o0 = vec4(1.0,0.0,0.0,0.5);"
      " o1 = vec4(1.0,0.0,0.0,0.5); }";
  tgles::GLuint prog = Link(ctx, vs, fs);
  EXPECT_TRUE(ctx.programs().LinkSucceeded(prog));
  tgles::GLuint t[2] = {0, 0};
  for (int i = 0; i < 2; ++i) {
    ctx.textures().GenTextures(1, &t[i]);
    ctx.textures().BindTexture(tgles::kGlTexture2d, t[i]);
    ctx.textures().TexImage2D(tgles::kGlTexture2d, 0, tgles::kGlRgba8, 16, 16,
                              0, tgles::kGlRgba, tgles::kGlUnsignedByte,
                              nullptr);
  }
  tgles::GLuint fbo = 0;
  ctx.framebuffers().GenFramebuffers(1, &fbo);
  ctx.framebuffers().BindFramebuffer(tgles::kGlDrawFramebuffer, fbo);
  ctx.framebuffers().FramebufferTexture2D(
      tgles::kGlDrawFramebuffer, tgles::kGlColorAttachment0,
      tgles::kGlTexture2d, t[0], 0);
  ctx.framebuffers().FramebufferTexture2D(
      tgles::kGlDrawFramebuffer, tgles::kGlColorAttachment0 + 1u,
      tgles::kGlTexture2d, t[1], 0);
  const tgles::GLenum bufs[2] = {tgles::kGlColorAttachment0,
                                 tgles::kGlColorAttachment0 + 1u};
  ctx.framebuffers().DrawBuffers(2, bufs);
  // Buffer 1 blends, buffer 0 does not (opaque). The bridge clears to
  // opaque black at pass start (no standalone glClear path), so red 0.5 over
  // black is (127,0,0) on the blended attachment and opaque red on the other.
  ctx.foundation().Enable(tgles::kGlBlend);
  ctx.raster().BlendFunci(0, tgles::kGlOne, tgles::kGlZero);
  ctx.raster().BlendFunci(1, tgles::kGlSrcAlpha, tgles::kGlOneMinusSrcAlpha);
  EXPECT_TRUE(ctx.RenderFrame(*bridge, tgles::kGlTriangles, 0, 3));
  EXPECT_EQ(ctx.GetError(), tgles::kGlNoError);
  PresentAndWait(*bridge);
  std::uint8_t p0[4] = {0}, p1[4] = {0};
  EXPECT_TRUE(bridge->ReadbackAttachment(0, 8, 8, p0));
  EXPECT_TRUE(bridge->ReadbackAttachment(1, 8, 8, p1));
  // p0 opaque red (blend off), p1 half-red (0.5 red over black).
  if (!(p0[0] == 255u && p0[2] == 0u)) {
    std::printf("[MISSING] Phase4 per-buffer blend buf0 got %u,%u,%u\n",
                p0[0], p0[1], p0[2]);
  }
  EXPECT_EQ(p0[0], 255u);
  EXPECT_EQ(p0[1], 0u);
  EXPECT_EQ(p0[2], 0u);
  if (!(p1[0] >= 120u && p1[0] <= 135u && p1[2] == 0u)) {
    std::printf("[MISSING] Phase4 per-buffer blend buf1 got %u,%u,%u\n",
                p1[0], p1[1], p1[2]);
  }
  EXPECT_TRUE(p1[0] >= 120u && p1[0] <= 135u);
  EXPECT_EQ(p1[1], 0u);
  EXPECT_EQ(p1[2], 0u);
}

// Uniform array full-frame (repro for suite-order investigation).
TEST(Phase4Real, UniformArraySecondElementTints) {
  auto bridge = tgles::metal_bridge::CreateAppleBridge();
  EXPECT_TRUE(bridge != nullptr);
  if (bridge == nullptr) return;
  if (!bridge->Initialize("Apple3")) return;
  tgles::GlesContext ctx = tgles::GlesContext::Create(false);
  BuildPosCol(ctx);
  const char* vs =
      "#version 320 es\nlayout(location=0) in vec4 a_pos;\n"
      "layout(location=1) in vec4 a_col;\n"
      "out vec4 v_col; uniform vec4 u_c[2];\n"
      "void main(){ v_col = a_col * u_c[1];"
      " gl_Position = a_pos + u_c[0] * 0.0; }";
  const char* fs =
      "#version 320 es\nprecision mediump float;\n"
      "in vec4 v_col; out vec4 o;\n"
      "void main(){ o = v_col; }";
  tgles::GLuint prog = Link(ctx, vs, fs);
  EXPECT_TRUE(ctx.programs().LinkSucceeded(prog));
  tgles::GLint base = ctx.programs().GetUniformLocation(prog, "u_c[0]");
  EXPECT_TRUE(base >= 0);
  const float vals[8] = {0, 0, 0, 0, 1, 0, 0, 1};
  ctx.programs().Uniform4fv(base, 2, vals);
  EXPECT_EQ(ctx.GetError(), tgles::kGlNoError);
  EXPECT_TRUE(ctx.RenderFrame(*bridge, tgles::kGlTriangles, 0, 3));
  EXPECT_EQ(ctx.GetError(), tgles::kGlNoError);
  PresentAndWait(*bridge);
  std::uint8_t px[4] = {0};
  EXPECT_TRUE(bridge->ReadbackPixel(8, 8, px));
  if (!(px[0] == 255u && px[1] == 0u && px[2] == 0u)) {
    std::printf("[MISSING] Phase4 uniform array: got %u,%u,%u\n", px[0],
                px[1], px[2]);
  }
  EXPECT_EQ(px[0], 255u);
  EXPECT_EQ(px[1], 0u);
  EXPECT_EQ(px[2], 0u);
}

#else

TEST(Phase4Real, NoMetalHostSkips) { EXPECT_TRUE(true); }

#endif
