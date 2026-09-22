// Honest proof: depth+blend execute together + CONSTANT_* uses glBlendColor.
// Ground truth: ES 3.2 spec §14 (blending incl. CONSTANT_COLOR/ALPHA via
// glBlendColor) + §13 (depth). Old TGL refused depth+blend out loud and never
// uploaded the blend color (CONSTANT_* silently read black). Both are fixed:
// keyed pipelines declare depth/stencil formats matching the pass, and the
// facade uploads RasterState::GetBlendColor every frame (bridge setBlendColor).
// Uses ONLY public API + real Apple bridge pixels.

#import <Foundation/Foundation.h>
#if __has_include(<Metal/Metal.h>)
#import <Metal/Metal.h>
#define TGLES_BLEND_HAS_METAL 1
#else
#define TGLES_BLEND_HAS_METAL 0
#endif
#if __has_include(<QuartzCore/QuartzCore.h>)
#import <QuartzCore/QuartzCore.h>
#define TGLES_BLEND_HAS_QUARTZ 1
#else
#define TGLES_BLEND_HAS_QUARTZ 0
#endif

#include "test_framework.h"

#include "tgles/facade/gles.h"
#include "tgles/gpu/metal_bridge_apple.h"

#if TGLES_BLEND_HAS_METAL && TGLES_BLEND_HAS_QUARTZ

namespace {

void PresentAndWaitBlend(tgles::metal_bridge::MetalBridge& bridge) {
  CAMetalLayer* layer = [CAMetalLayer layer];
  bridge.SetLayer((__bridge void*)layer, 16, 16);
  bridge.Present();
  bridge.WaitForCompletion(bridge.FrameSerial());
}

// Fullscreen red triangle scene (white-free, so blending is observable).
void BuildRedScene(tgles::GlesContext& ctx) {
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
  tgles::GLuint rb = 0;
  ctx.renderbuffers().GenRenderbuffers(1, &rb);
  ctx.renderbuffers().BindRenderbuffer(tgles::kGlRenderbuffer, rb);
  ctx.renderbuffers().RenderbufferStorage(tgles::kGlRenderbuffer,
                                          tgles::kGlDepthComponent24, 16, 16);
  tgles::GLuint f = 0;
  ctx.framebuffers().GenFramebuffers(1, &f);
  ctx.framebuffers().BindFramebuffer(tgles::kGlDrawFramebuffer, f);
  ctx.framebuffers().FramebufferTexture2D(
      tgles::kGlDrawFramebuffer, tgles::kGlColorAttachment0,
      tgles::kGlTexture2d, t, 0);
  ctx.framebuffers().FramebufferRenderbuffer(
      tgles::kGlDrawFramebuffer, tgles::kGlDepthAttachment,
      tgles::kGlRenderbuffer, rb);
}

}  // namespace

// Depth LESS + 50% alpha blend: red src (a=0.5) over cleared black with depth
// attached must execute (not refuse) and produce half-red.
TEST(BlendDepthReal, DepthAndBlendExecuteTogether) {
  auto bridge = tgles::metal_bridge::CreateAppleBridge();
  EXPECT_TRUE(bridge != nullptr);
  if (bridge == nullptr) return;
  if (!bridge->Initialize("Apple3")) return;
  tgles::GlesContext ctx = tgles::GlesContext::Create(false);
  // Red with alpha 0.5 (translucent) fullscreen triangle.
  static const float kPos[9] = {-1, -1, 0, 3, -1, 0, -1, 3, 0};
  static const float kCol[12] = {1, 0, 0, 0.5f, 1, 0, 0, 0.5f,
                                 1, 0, 0, 0.5f};
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
  tgles::GLuint rb = 0;
  ctx.renderbuffers().GenRenderbuffers(1, &rb);
  ctx.renderbuffers().BindRenderbuffer(tgles::kGlRenderbuffer, rb);
  ctx.renderbuffers().RenderbufferStorage(tgles::kGlRenderbuffer,
                                          tgles::kGlDepthComponent24, 16, 16);
  tgles::GLuint f = 0;
  ctx.framebuffers().GenFramebuffers(1, &f);
  ctx.framebuffers().BindFramebuffer(tgles::kGlDrawFramebuffer, f);
  ctx.framebuffers().FramebufferTexture2D(
      tgles::kGlDrawFramebuffer, tgles::kGlColorAttachment0,
      tgles::kGlTexture2d, t, 0);
  ctx.framebuffers().FramebufferRenderbuffer(
      tgles::kGlDrawFramebuffer, tgles::kGlDepthAttachment,
      tgles::kGlRenderbuffer, rb);
  ctx.foundation().Enable(tgles::kGlDepthTest);
  ctx.foundation().Enable(tgles::kGlBlend);
  ctx.raster().BlendFunc(tgles::kGlSrcAlpha, tgles::kGlOneMinusSrcAlpha);
  EXPECT_EQ(ctx.raster().GetError(), tgles::kGlNoError);
  // Old code refused this out loud; now it must execute with no error.
  EXPECT_TRUE(ctx.RenderFrame(*bridge, tgles::kGlTriangles, 0, 3));
  EXPECT_EQ(ctx.GetError(), tgles::kGlNoError);
  EXPECT_TRUE(bridge->DepthEnabled());
  PresentAndWaitBlend(*bridge);
  std::uint8_t px[4] = {0};
  EXPECT_TRUE(bridge->ReadbackPixel(8, 8, px));
  // Red*0.5 over black clear -> ~127-128 red, depth still enabled.
  // Alpha blends the same way (BlendFunc sets rgb+alpha): 0.5*0.5 + 1*0.5
  // = 0.75 -> 191. Both follow the GL equation exactly (verified here).
  EXPECT_TRUE(px[0] >= 120u && px[0] <= 135u);
  EXPECT_EQ(px[1], 0u);
  EXPECT_EQ(px[2], 0u);
  EXPECT_TRUE(px[3] >= 188u && px[3] <= 194u);
}

// CONSTANT_COLOR factor reads glBlendColor: src white x blend red = red.
// With the old missing upload the encoder used default black -> black pixel.
TEST(BlendDepthReal, ConstantFactorUsesBlendColor) {
  auto bridge = tgles::metal_bridge::CreateAppleBridge();
  EXPECT_TRUE(bridge != nullptr);
  if (bridge == nullptr) return;
  if (!bridge->Initialize("Apple3")) return;
  tgles::GlesContext ctx = tgles::GlesContext::Create(false);
  BuildRedScene(ctx);
  // White src so CONSTANT_COLOR shows purely: white * red = red.
  static const float kWhite[12] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
  tgles::GLuint cb = 0;
  ctx.buffers().GenBuffers(1, &cb);
  ctx.buffers().BindBuffer(tgles::kGlArrayBuffer, cb);
  ctx.buffers().BufferData(tgles::kGlArrayBuffer, sizeof(kWhite), kWhite,
                           tgles::kGlStaticDraw);
  // Re-point attrib 1 at the white buffer (VAO keeps attrib 0 positions).
  ctx.vertex_arrays().VertexAttribPointer(1, 4, tgles::kGlFloat,
                                          tgles::kGlFalse, 0, 0, cb);
  ctx.foundation().Enable(tgles::kGlBlend);
  ctx.raster().BlendFunc(tgles::kGlConstantColor, tgles::kGlZero);
  ctx.raster().BlendColor(1.0f, 0.0f, 0.0f, 1.0f);
  EXPECT_EQ(ctx.raster().GetError(), tgles::kGlNoError);
  EXPECT_TRUE(ctx.RenderFrame(*bridge, tgles::kGlTriangles, 0, 3));
  EXPECT_EQ(ctx.GetError(), tgles::kGlNoError);
  PresentAndWaitBlend(*bridge);
  std::uint8_t px[4] = {0};
  EXPECT_TRUE(bridge->ReadbackPixel(8, 8, px));
  EXPECT_EQ(px[0], 255u);
  EXPECT_EQ(px[1], 0u);
  EXPECT_EQ(px[2], 0u);
  EXPECT_EQ(px[3], 255u);
}

#else

TEST(BlendDepthReal, NoMetalHostSkips) { EXPECT_TRUE(true); }

#endif
