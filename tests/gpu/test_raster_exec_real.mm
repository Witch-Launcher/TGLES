// Raster-state execution proofs (Apple only, real pixels). Ground truth:
// ES 3.2 spec §13 (culling), §14 (viewport/scissor/blending incl. dual-source
// EXT values, polygon offset), §15 (stencil two-sided refs). Uses ONLY public
// API + real Apple bridge pixels. Each TEST documents the Metal mapping it
// proves (setCullMode, setViewport/scissor, DualOut index(1), two-pass refs).

#import <Foundation/Foundation.h>
#if __has_include(<Metal/Metal.h>)
#import <Metal/Metal.h>
#define TGLES_RAST_HAS_METAL 1
#else
#define TGLES_RAST_HAS_METAL 0
#endif
#if __has_include(<QuartzCore/QuartzCore.h>)
#import <QuartzCore/QuartzCore.h>
#define TGLES_RAST_HAS_QUARTZ 1
#else
#define TGLES_RAST_HAS_QUARTZ 0
#endif

#include "test_framework.h"

#include "tgles/facade/gles.h"
#include "tgles/gpu/metal_bridge_apple.h"

#if TGLES_RAST_HAS_METAL && TGLES_RAST_HAS_QUARTZ

namespace {

void PresentAndWaitRast(tgles::metal_bridge::MetalBridge& bridge) {
  CAMetalLayer* layer = [CAMetalLayer layer];
  bridge.SetLayer((__bridge void*)layer, 16, 16);
  bridge.Present();
  bridge.WaitForCompletion(bridge.FrameSerial());
}

// Two overlapping fullscreen-ish triangles in ONE draw: front (CCW, red)
// then back (CW, green). Painter order without culling: green wins center.
void BuildTwoSidedScene(tgles::GlesContext& ctx) {
  static const float kPos[18] = {-1, -1, 0, 3,  -1, 0, -1, 3, 0,
                                 -1, -1, 0, -1, 3,  0, 3,  -1, 0};
  static const float kCol[24] = {1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1,
                                 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1};
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

void BuildRedSceneRast(tgles::GlesContext& ctx) {
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

void AttachStencilRast(tgles::GlesContext& ctx) {
  tgles::GLuint rb = 0;
  ctx.renderbuffers().GenRenderbuffers(1, &rb);
  ctx.renderbuffers().BindRenderbuffer(tgles::kGlRenderbuffer, rb);
  ctx.renderbuffers().RenderbufferStorage(tgles::kGlRenderbuffer,
                                          tgles::kGlDepth24Stencil8, 16, 16);
  ctx.framebuffers().FramebufferRenderbuffer(
      tgles::kGlDrawFramebuffer, tgles::kGlDepthStencilAttachment,
      tgles::kGlRenderbuffer, rb);
}

}  // namespace

// Cull BACK removes the CW green triangle: center stays red. Control case
// (no cull) paints green last, proving the green tri really covers center.
TEST(RasterExecReal, CullBackKeepsRedControlIsGreen) {
  for (int cull = 0; cull < 2; ++cull) {
    auto bridge = tgles::metal_bridge::CreateAppleBridge();
    EXPECT_TRUE(bridge != nullptr);
    if (bridge == nullptr) return;
    if (!bridge->Initialize("Apple3")) return;
    tgles::GlesContext ctx = tgles::GlesContext::Create(false);
    BuildTwoSidedScene(ctx);
    if (cull) {
      ctx.foundation().Enable(tgles::kGlCullFace);
      ctx.raster().CullFace(tgles::kGlBackFace);
    }
    EXPECT_TRUE(ctx.RenderFrame(*bridge, tgles::kGlTriangles, 0, 6));
    EXPECT_EQ(ctx.GetError(), tgles::kGlNoError);
    PresentAndWaitRast(*bridge);
    std::uint8_t px[4] = {0};
    EXPECT_TRUE(bridge->ReadbackPixel(8, 8, px));
    if (cull) {
      EXPECT_EQ(px[0], 255u);
      EXPECT_EQ(px[1], 0u);
      EXPECT_EQ(px[2], 0u);
    } else {
      EXPECT_EQ(px[0], 0u);
      EXPECT_EQ(px[1], 255u);
      EXPECT_EQ(px[2], 0u);
    }
    EXPECT_EQ(px[3], 255u);
  }
}

// Viewport (0,0,8,16): left half red, right half clear. The second half
// pins the vertical convention: (0,8,16,8) is the TOP half in GL (y up from
// bottom) — top red / bottom clear proves the bridge maps origins like GL,
// not Metal-top-left.
TEST(RasterExecReal, ViewportClipsToLeftHalf) {
  auto bridge = tgles::metal_bridge::CreateAppleBridge();
  EXPECT_TRUE(bridge != nullptr);
  if (bridge == nullptr) return;
  if (!bridge->Initialize("Apple3")) return;
  tgles::GlesContext ctx = tgles::GlesContext::Create(false);
  BuildRedSceneRast(ctx);
  // Opaque black clear so clipped pixels keep alpha=255 (GL default is 0).
  ctx.raster().ClearColor(0.f, 0.f, 0.f, 1.f);
  ctx.raster().Viewport(0, 0, 8, 16);
  EXPECT_TRUE(ctx.RenderFrame(*bridge, tgles::kGlTriangles, 0, 3));
  EXPECT_EQ(ctx.GetError(), tgles::kGlNoError);
  PresentAndWaitRast(*bridge);
  std::uint8_t left[4] = {0}, right[4] = {0};
  EXPECT_TRUE(bridge->ReadbackPixel(4, 8, left));
  EXPECT_TRUE(bridge->ReadbackPixel(12, 8, right));
  EXPECT_EQ(left[0], 255u);
  EXPECT_EQ(left[3], 255u);
  EXPECT_EQ(right[0], 0u);
  EXPECT_EQ(right[1], 0u);
  EXPECT_EQ(right[2], 0u);
  EXPECT_EQ(right[3], 255u);
}

TEST(RasterExecReal, ViewportTopHalfIsUp) {
  auto bridge = tgles::metal_bridge::CreateAppleBridge();
  EXPECT_TRUE(bridge != nullptr);
  if (bridge == nullptr) return;
  if (!bridge->Initialize("Apple3")) return;
  tgles::GlesContext ctx = tgles::GlesContext::Create(false);
  BuildRedSceneRast(ctx);
  ctx.raster().ClearColor(0.f, 0.f, 0.f, 1.f);
  ctx.raster().Viewport(0, 8, 16, 8);
  EXPECT_TRUE(ctx.RenderFrame(*bridge, tgles::kGlTriangles, 0, 3));
  EXPECT_EQ(ctx.GetError(), tgles::kGlNoError);
  PresentAndWaitRast(*bridge);
  std::uint8_t top[4] = {0}, bottom[4] = {0};
  EXPECT_TRUE(bridge->ReadbackPixel(8, 12, top));
  EXPECT_TRUE(bridge->ReadbackPixel(8, 4, bottom));
  EXPECT_EQ(top[0], 255u);
  EXPECT_EQ(top[3], 255u);
  EXPECT_EQ(bottom[0], 0u);
  EXPECT_EQ(bottom[3], 255u);
}

// Scissor (0,0,8,16) enabled: same expectation as the viewport (GL origin).
TEST(RasterExecReal, ScissorClipsToLeftHalf) {
  auto bridge = tgles::metal_bridge::CreateAppleBridge();
  EXPECT_TRUE(bridge != nullptr);
  if (bridge == nullptr) return;
  if (!bridge->Initialize("Apple3")) return;
  tgles::GlesContext ctx = tgles::GlesContext::Create(false);
  BuildRedSceneRast(ctx);
  ctx.raster().ClearColor(0.f, 0.f, 0.f, 1.f);
  ctx.foundation().Enable(tgles::kGlScissorTest);
  ctx.raster().Scissor(0, 0, 8, 16);
  EXPECT_TRUE(ctx.RenderFrame(*bridge, tgles::kGlTriangles, 0, 3));
  EXPECT_EQ(ctx.GetError(), tgles::kGlNoError);
  PresentAndWaitRast(*bridge);
  std::uint8_t left[4] = {0}, right[4] = {0};
  EXPECT_TRUE(bridge->ReadbackPixel(4, 8, left));
  EXPECT_TRUE(bridge->ReadbackPixel(12, 8, right));
  EXPECT_EQ(left[0], 255u);
  EXPECT_EQ(right[0], 0u);
  EXPECT_EQ(right[3], 255u);
}

TEST(RasterExecReal, ScissorTopHalfIsUp) {
  auto bridge = tgles::metal_bridge::CreateAppleBridge();
  EXPECT_TRUE(bridge != nullptr);
  if (bridge == nullptr) return;
  if (!bridge->Initialize("Apple3")) return;
  tgles::GlesContext ctx = tgles::GlesContext::Create(false);
  BuildRedSceneRast(ctx);
  ctx.raster().ClearColor(0.f, 0.f, 0.f, 1.f);
  ctx.foundation().Enable(tgles::kGlScissorTest);
  ctx.raster().Scissor(0, 8, 16, 8);
  EXPECT_TRUE(ctx.RenderFrame(*bridge, tgles::kGlTriangles, 0, 3));
  EXPECT_EQ(ctx.GetError(), tgles::kGlNoError);
  PresentAndWaitRast(*bridge);
  std::uint8_t top[4] = {0}, bottom[4] = {0};
  EXPECT_TRUE(bridge->ReadbackPixel(8, 12, top));
  EXPECT_TRUE(bridge->ReadbackPixel(8, 4, bottom));
  EXPECT_EQ(top[0], 255u);
  EXPECT_EQ(bottom[0], 0u);
  EXPECT_EQ(bottom[3], 255u);
}

// Dual-source blending: o0 white, o1 green, (SRC1_COLOR, ZERO) -> green.
// A single-source pipeline would show white; a refused draw would fail.
TEST(RasterExecReal, DualSourceSamplesSecondOutput) {
  auto bridge = tgles::metal_bridge::CreateAppleBridge();
  EXPECT_TRUE(bridge != nullptr);
  if (bridge == nullptr) return;
  if (!bridge->Initialize("Apple3")) return;
  tgles::GlesContext ctx = tgles::GlesContext::Create(false);
  BuildRedSceneRast(ctx);

  const char* vs =
      "#version 320 es\nlayout(location=0) in vec4 a_pos;\n"
      "layout(location=1) in vec4 a_col;\n"
      "out vec4 v_col; uniform mat4 u_mvp;\n"
      "void main(){ v_col = a_col; gl_Position = u_mvp * a_pos; }";
  const char* fs =
      "#version 320 es\nprecision mediump float;\n"
      "in vec4 v_col;\n"
      "layout(location=0) out vec4 o0;\n"
      "layout(location=0, index=1) out vec4 o1;\n"
      "void main(){ o0 = vec4(1.0); o1 = vec4(0.0, 1.0, 0.0, 1.0); }";
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

  ctx.foundation().Enable(tgles::kGlBlend);
  // SRC1_COLOR / ZERO are the EXT values (glext.h verified); the raster
  // layer accepts them since dual-source factors started mapping.
  ctx.raster().BlendFunc(0x88F9u /*SRC1_COLOR*/, tgles::kGlZero);
  EXPECT_EQ(ctx.raster().GetError(), tgles::kGlNoError);
  EXPECT_TRUE(ctx.RenderFrame(*bridge, tgles::kGlTriangles, 0, 3));
  EXPECT_EQ(ctx.GetError(), tgles::kGlNoError);
  PresentAndWaitRast(*bridge);
  std::uint8_t px[4] = {0};
  EXPECT_TRUE(bridge->ReadbackPixel(8, 8, px));
  EXPECT_EQ(px[0], 0u);
  EXPECT_EQ(px[1], 255u);
  EXPECT_EQ(px[2], 0u);
  EXPECT_EQ(px[3], 255u);
}

// Split stencil refs via two passes: front ALWAYS/1, back NEVER/2. The CW
// green triangle is back-facing -> discarded; the CCW red one draws. The old
// single-ref bridge failed this closed; emulation must show red.
TEST(RasterExecReal, SplitRefsDiscardBackFaces) {
  auto bridge = tgles::metal_bridge::CreateAppleBridge();
  EXPECT_TRUE(bridge != nullptr);
  if (bridge == nullptr) return;
  if (!bridge->Initialize("Apple3")) return;
  tgles::GlesContext ctx = tgles::GlesContext::Create(false);
  BuildTwoSidedScene(ctx);
  AttachStencilRast(ctx);
  ctx.foundation().Enable(tgles::kGlStencilTest);
  ctx.raster().StencilFuncSeparate(tgles::kGlFront, tgles::kGlAlways, 1,
                                   0xFFu);
  ctx.raster().StencilFuncSeparate(tgles::kGlBackFace, tgles::kGlNever, 2,
                                   0xFFu);
  EXPECT_EQ(ctx.raster().GetError(), tgles::kGlNoError);
  EXPECT_TRUE(ctx.RenderFrame(*bridge, tgles::kGlTriangles, 0, 6));
  EXPECT_EQ(ctx.GetError(), tgles::kGlNoError);
  PresentAndWaitRast(*bridge);
  std::uint8_t px[4] = {0};
  EXPECT_TRUE(bridge->ReadbackPixel(8, 8, px));
  EXPECT_EQ(px[0], 255u);
  EXPECT_EQ(px[1], 0u);
  EXPECT_EQ(px[2], 0u);
  EXPECT_EQ(px[3], 255u);
}

// Polygon offset reaches the encoder (ordering proof needs two draws in one
// frame, which the one-draw-per-frame bridge cannot express; the bias values
// are additionally pinned on the Mock in the state suite).
TEST(RasterExecReal, PolygonOffsetDrawsRed) {
  auto bridge = tgles::metal_bridge::CreateAppleBridge();
  EXPECT_TRUE(bridge != nullptr);
  if (bridge == nullptr) return;
  if (!bridge->Initialize("Apple3")) return;
  tgles::GlesContext ctx = tgles::GlesContext::Create(false);
  BuildRedSceneRast(ctx);
  ctx.foundation().Enable(tgles::kGlPolygonOffsetFill);
  ctx.raster().PolygonOffset(1.0f, 1.0f);
  EXPECT_TRUE(ctx.RenderFrame(*bridge, tgles::kGlTriangles, 0, 3));
  EXPECT_EQ(ctx.GetError(), tgles::kGlNoError);
  PresentAndWaitRast(*bridge);
  std::uint8_t px[4] = {0};
  EXPECT_TRUE(bridge->ReadbackPixel(8, 8, px));
  EXPECT_EQ(px[0], 255u);
  EXPECT_EQ(px[3], 255u);
}

#else

TEST(RasterExecReal, NoMetalHostSkips) { EXPECT_TRUE(true); }

#endif
