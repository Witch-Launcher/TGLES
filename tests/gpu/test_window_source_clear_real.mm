// End-to-end proof that the bridge target behaves like the presented FBO:
// the app clears its main target once, blits it into the window (that is how
// it names the window source), and then clears a DIFFERENT target every
// frame (MC's GUI overlay, transparent black). Only the window source's
// clear may wipe what the screen shows; the GUI clear must not.
// Uses ONLY public API + real Apple bridge pixels.

#import <Foundation/Foundation.h>
#if __has_include(<Metal/Metal.h>)
#import <Metal/Metal.h>
#define TGLES_WSC_HAS_METAL 1
#else
#define TGLES_WSC_HAS_METAL 0
#endif
#if __has_include(<QuartzCore/QuartzCore.h>)
#import <QuartzCore/QuartzCore.h>
#define TGLES_WSC_HAS_QUARTZ 1
#else
#define TGLES_WSC_HAS_QUARTZ 0
#endif

#include "test_framework.h"

#include "tgles/facade/gles.h"
#include "tgles/gpu/metal_bridge_apple.h"

#if TGLES_WSC_HAS_METAL && TGLES_WSC_HAS_QUARTZ

namespace {

constexpr tgles::GLsizei kSize = 64;

void PresentAndWaitWs(tgles::metal_bridge::MetalBridge& bridge) {
  CAMetalLayer* layer = [CAMetalLayer layer];
  bridge.SetLayer((__bridge void*)layer, kSize, kSize);
  bridge.Present();
  bridge.WaitForCompletion(bridge.FrameSerial());
}

// Two window-sized FBOs (main = presented, gui = cleared every frame) plus a
// small centered green triangle: corners carry the clear color, the center
// proves draws still paint.
void BuildScene(tgles::GlesContext& ctx, tgles::GLuint* main_fbo,
                tgles::GLuint* gui_fbo) {
  static const float kPos[9] = {-0.2f, -0.2f, 0.0f, 0.2f, -0.2f, 0.0f,
                                0.0f,  0.2f, 0.0f};
  static const float kCol[12] = {0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1};
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

  tgles::GLuint ta = 0, tb = 0;
  ctx.textures().GenTextures(1, &ta);
  ctx.textures().BindTexture(tgles::kGlTexture2d, ta);
  ctx.textures().TexImage2D(tgles::kGlTexture2d, 0, tgles::kGlRgba8, kSize,
                            kSize, 0, tgles::kGlRgba, tgles::kGlUnsignedByte,
                            nullptr);
  ctx.textures().GenTextures(1, &tb);
  ctx.textures().BindTexture(tgles::kGlTexture2d, tb);
  ctx.textures().TexImage2D(tgles::kGlTexture2d, 0, tgles::kGlRgba8, kSize,
                            kSize, 0, tgles::kGlRgba, tgles::kGlUnsignedByte,
                            nullptr);

  tgles::GLuint fa = 0, fb = 0;
  ctx.framebuffers().GenFramebuffers(1, &fa);
  ctx.framebuffers().BindFramebuffer(tgles::kGlDrawFramebuffer, fa);
  ctx.framebuffers().FramebufferTexture2D(tgles::kGlDrawFramebuffer,
                                          tgles::kGlColorAttachment0,
                                          tgles::kGlTexture2d, ta, 0);
  ctx.framebuffers().GenFramebuffers(1, &fb);
  ctx.framebuffers().BindFramebuffer(tgles::kGlDrawFramebuffer, fb);
  ctx.framebuffers().FramebufferTexture2D(tgles::kGlDrawFramebuffer,
                                          tgles::kGlColorAttachment0,
                                          tgles::kGlTexture2d, tb, 0);
  ctx.framebuffers().BindFramebuffer(tgles::kGlDrawFramebuffer, fa);
  *main_fbo = fa;
  *gui_fbo = fb;
}

void ExpectPixel(tgles::metal_bridge::MetalBridge& bridge, int x, int y,
                 std::uint8_t r, std::uint8_t g, std::uint8_t b,
                 std::uint8_t a) {
  std::uint8_t px[4] = {0};
  EXPECT_TRUE(bridge.ReadbackPixel(x, y, px));
  EXPECT_EQ(px[0], r);
  EXPECT_EQ(px[1], g);
  EXPECT_EQ(px[2], b);
  EXPECT_EQ(px[3], a);
}

}  // namespace

TEST(WindowSourceClearReal, GuiClearDoesNotWipePresentedTarget) {
  auto bridge = tgles::metal_bridge::CreateAppleBridge();
  EXPECT_TRUE(bridge != nullptr);
  if (bridge == nullptr) return;
  if (!bridge->Initialize("Apple3")) return;
  tgles::GlesContext ctx = tgles::GlesContext::Create(false);
  tgles::GLuint main_fbo = 0, gui_fbo = 0;
  BuildScene(ctx, &main_fbo, &gui_fbo);

  // Frame 1: clear the main target, then name it the window source by
  // blitting it into FBO 0 (what MC's presentTexture does).
  ctx.raster().ClearColor(1.0f, 0.0f, 0.0f, 1.0f);
  ctx.Clear(tgles::kGlColorBufferBit);
  ctx.framebuffers().BindFramebuffer(tgles::kGlReadFramebuffer, main_fbo);
  ctx.framebuffers().BindFramebuffer(tgles::kGlDrawFramebuffer, 0);
  ctx.framebuffers().BlitFramebuffer(0, 0, kSize, kSize, 0, 0, kSize, kSize,
                                     tgles::kGlColorBufferBit,
                                     tgles::kGlNearest);
  ctx.framebuffers().BindFramebuffer(tgles::kGlDrawFramebuffer, main_fbo);
  EXPECT_TRUE(ctx.RenderFrame(*bridge, tgles::kGlTriangles, 0, 3));
  EXPECT_EQ(ctx.GetError(), tgles::kGlNoError);
  PresentAndWaitWs(*bridge);
  ExpectPixel(*bridge, 4, 4, 255, 0, 0, 255);    // Clear color at a corner.
  ExpectPixel(*bridge, 32, 32, 0, 255, 0, 255);  // Draw still paints.

  // Frame 2: only the GUI target is cleared (transparent black, the live
  // glClearColor). The window must keep frame 1's red + green.
  ctx.framebuffers().BindFramebuffer(tgles::kGlDrawFramebuffer, gui_fbo);
  ctx.raster().ClearColor(0.0f, 0.0f, 0.0f, 0.0f);
  ctx.Clear(tgles::kGlColorBufferBit);
  ctx.framebuffers().BindFramebuffer(tgles::kGlDrawFramebuffer, main_fbo);
  EXPECT_TRUE(ctx.RenderFrame(*bridge, tgles::kGlTriangles, 0, 3));
  EXPECT_EQ(ctx.GetError(), tgles::kGlNoError);
  PresentAndWaitWs(*bridge);
  ExpectPixel(*bridge, 4, 4, 255, 0, 0, 255);
  ExpectPixel(*bridge, 32, 32, 0, 255, 0, 255);

  // Frame 3: the window source itself is cleared to blue -> the window
  // clears to blue with THAT color (captured at glClear time).
  ctx.raster().ClearColor(0.0f, 0.0f, 1.0f, 1.0f);
  ctx.Clear(tgles::kGlColorBufferBit);
  EXPECT_TRUE(ctx.RenderFrame(*bridge, tgles::kGlTriangles, 0, 3));
  EXPECT_EQ(ctx.GetError(), tgles::kGlNoError);
  PresentAndWaitWs(*bridge);
  ExpectPixel(*bridge, 4, 4, 0, 0, 255, 255);
  ExpectPixel(*bridge, 32, 32, 0, 255, 0, 255);
}

#else

TEST(WindowSourceClearReal, NoMetalHostSkips) { EXPECT_TRUE(true); }

#endif
