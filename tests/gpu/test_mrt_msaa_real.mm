// MRT + MSAA device proofs (Apple only, real pixels). Ground truth: ES 3.2
// spec §9.4 (DrawBuffers + per-attachment rendering) and §14.x (multisample
// render + resolve). MRT writes each fragment out to its own bridge target
// (read back per attachment); MSAA renders multisampled and resolves.
// Uses ONLY public API + real Apple bridge pixels.

#import <Foundation/Foundation.h>
#if __has_include(<Metal/Metal.h>)
#import <Metal/Metal.h>
#define TGLES_MRT_HAS_METAL 1
#else
#define TGLES_MRT_HAS_METAL 0
#endif
#if __has_include(<QuartzCore/QuartzCore.h>)
#import <QuartzCore/QuartzCore.h>
#define TGLES_MRT_HAS_QUARTZ 1
#else
#define TGLES_MRT_HAS_QUARTZ 0
#endif

#include "test_framework.h"

#include "tgles/facade/gles.h"
#include "tgles/gpu/metal_bridge_apple.h"

#if TGLES_MRT_HAS_METAL && TGLES_MRT_HAS_QUARTZ

namespace {

void PresentAndWaitMrt(tgles::metal_bridge::MetalBridge& bridge) {
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
}

tgles::GLuint BuildProgram(tgles::GlesContext& ctx, const char* vs,
                           const char* fs) {
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
  ctx.programs().UseProgram(prog);
  return prog;
}

}  // namespace

// Two fragment outs (locations 0+1) + DrawBuffers(2): attachment 0 red,
// attachment 1 green. A single-target bridge would show red twice (or fail),
// so the green readback on attachment 1 is the MRT proof.
TEST(MrtMsaaReal, TwoAttachmentsGetDifferentColors) {
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
      "in vec4 v_col;\n"
      "layout(location=0) out vec4 o0;\n"
      "layout(location=1) out vec4 o1;\n"
      "void main(){ o0 = vec4(1.0, 0.0, 0.0, 1.0);"
      " o1 = vec4(0.0, 1.0, 0.0, 1.0); }";
  tgles::GLuint prog = BuildProgram(ctx, vs, fs);
  EXPECT_TRUE(ctx.programs().LinkSucceeded(prog));

  tgles::GLuint t0 = 0, t1 = 0;
  ctx.textures().GenTextures(1, &t0);
  ctx.textures().GenTextures(1, &t1);
  ctx.textures().BindTexture(tgles::kGlTexture2d, t0);
  ctx.textures().TexImage2D(tgles::kGlTexture2d, 0, tgles::kGlRgba8, 16, 16, 0,
                            tgles::kGlRgba, tgles::kGlUnsignedByte, nullptr);
  ctx.textures().BindTexture(tgles::kGlTexture2d, t1);
  ctx.textures().TexImage2D(tgles::kGlTexture2d, 0, tgles::kGlRgba8, 16, 16, 0,
                            tgles::kGlRgba, tgles::kGlUnsignedByte, nullptr);
  tgles::GLuint fbo = 0;
  ctx.framebuffers().GenFramebuffers(1, &fbo);
  ctx.framebuffers().BindFramebuffer(tgles::kGlDrawFramebuffer, fbo);
  ctx.framebuffers().FramebufferTexture2D(
      tgles::kGlDrawFramebuffer, tgles::kGlColorAttachment0,
      tgles::kGlTexture2d, t0, 0);
  ctx.framebuffers().FramebufferTexture2D(
      tgles::kGlDrawFramebuffer, tgles::kGlColorAttachment0 + 1u,
      tgles::kGlTexture2d, t1, 0);
  const tgles::GLenum bufs[2] = {tgles::kGlColorAttachment0,
                                 tgles::kGlColorAttachment0 + 1u};
  ctx.framebuffers().DrawBuffers(2, bufs);
  EXPECT_EQ(ctx.GetError(), tgles::kGlNoError);

  EXPECT_TRUE(ctx.RenderFrame(*bridge, tgles::kGlTriangles, 0, 3));
  EXPECT_EQ(ctx.GetError(), tgles::kGlNoError);
  PresentAndWaitMrt(*bridge);
  std::uint8_t p0[4] = {0}, p1[4] = {0};
  EXPECT_TRUE(bridge->ReadbackAttachment(0, 8, 8, p0));
  EXPECT_TRUE(bridge->ReadbackAttachment(1, 8, 8, p1));
  EXPECT_EQ(p0[0], 255u);
  EXPECT_EQ(p0[1], 0u);
  EXPECT_EQ(p0[2], 0u);
  EXPECT_EQ(p0[3], 255u);
  EXPECT_EQ(p1[0], 0u);
  EXPECT_EQ(p1[1], 255u);
  EXPECT_EQ(p1[2], 0u);
  EXPECT_EQ(p1[3], 255u);
}

// 4x MSAA renderbuffer target: red triangle resolves to a red center pixel.
// Without the resolve step the readback target would stay cleared (black),
// so red proves render-then-resolve executed.
TEST(MrtMsaaReal, MsaaResolvesToRedCenter) {
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
      "in vec4 v_col; out vec4 o_col;\n"
      "void main(){ o_col = vec4(1.0, 0.0, 0.0, 1.0); }";
  tgles::GLuint prog = BuildProgram(ctx, vs, fs);
  EXPECT_TRUE(ctx.programs().LinkSucceeded(prog));

  tgles::GLuint rb = 0;
  ctx.renderbuffers().GenRenderbuffers(1, &rb);
  ctx.renderbuffers().BindRenderbuffer(tgles::kGlRenderbuffer, rb);
  ctx.renderbuffers().RenderbufferStorageMultisample(
      tgles::kGlRenderbuffer, 4, tgles::kGlRgba8, 16, 16);
  EXPECT_EQ(ctx.renderbuffers().GetError(), tgles::kGlNoError);
  tgles::GLuint fbo = 0;
  ctx.framebuffers().GenFramebuffers(1, &fbo);
  ctx.framebuffers().BindFramebuffer(tgles::kGlDrawFramebuffer, fbo);
  ctx.framebuffers().FramebufferRenderbuffer(
      tgles::kGlDrawFramebuffer, tgles::kGlColorAttachment0,
      tgles::kGlRenderbuffer, rb);
  EXPECT_EQ(ctx.framebuffers().CheckFramebufferStatus(
                tgles::kGlDrawFramebuffer),
            tgles::kGlFramebufferComplete);

  EXPECT_TRUE(ctx.RenderFrame(*bridge, tgles::kGlTriangles, 0, 3));
  EXPECT_EQ(ctx.GetError(), tgles::kGlNoError);
  PresentAndWaitMrt(*bridge);
  std::uint8_t px[4] = {0};
  EXPECT_TRUE(bridge->ReadbackPixel(8, 8, px));
  EXPECT_EQ(px[0], 255u);
  EXPECT_EQ(px[1], 0u);
  EXPECT_EQ(px[2], 0u);
  EXPECT_EQ(px[3], 255u);
}

#else

TEST(MrtMsaaReal, NoMetalHostSkips) { EXPECT_TRUE(true); }

#endif
