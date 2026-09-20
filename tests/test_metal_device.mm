// On-device Metal test (Apple only, needs a real MTLDevice).
// Proves what the Mock cannot: MSL compiles on a real driver, a real PSO
// draws a real triangle, blit/readback return exact pixels, the fence ring
// is genuinely async (completed lags until waited), and presentDrawable
// presents through a real CAMetalLayer. When no device exists the tests
// return early (skip) instead of failing, so GPU-less CI stays green.
// Evidence: signatures verified in the macOS 26.2 SDK (MTLDevice.h,
// CAMetalLayer.h) and probed live on host Metal (Intel KBL).

#import <Foundation/Foundation.h>
#if __has_include(<Metal/Metal.h>)
#import <Metal/Metal.h>
#define TGLES_DEVICE_HAS_METAL 1
#else
#define TGLES_DEVICE_HAS_METAL 0
#endif
#if __has_include(<QuartzCore/QuartzCore.h>)
#import <QuartzCore/QuartzCore.h>
#define TGLES_DEVICE_HAS_QUARTZ 1
#else
#define TGLES_DEVICE_HAS_QUARTZ 0
#endif

#include "test_framework.h"

#include "tgles/backend.h"
#include "tgles/gles.h"
#include "tgles/metal_bridge_apple.h"

namespace {

// Fullscreen triangle (positions) with constant red, matching the bridge
// MSL VertexIn layout: float4 position @attribute(0), float4 color
// @attribute(1), stride 32. Covers every pixel of the target.
struct Vert {
  float pos[4];
  float col[4];
};

void FillFullscreenRedTriangle(std::uint8_t* out, float* mvp_out) {
  static const Vert kTri[3] = {
      {{-1, -1, 0, 1}, {1, 0, 0, 1}},
      {{3, -1, 0, 1}, {1, 0, 0, 1}},
      {{-1, 3, 0, 1}, {1, 0, 0, 1}},
  };
  __builtin_memcpy(out, kTri, sizeof(kTri));
  static const float kIdentity[16] = {1, 0, 0, 0, 0, 1, 0, 0,
                                      0, 0, 1, 0, 0, 0, 0, 1};
  __builtin_memcpy(mvp_out, kIdentity, sizeof(kIdentity));
}

}  // namespace

TEST(MetalDevice, FactoryExistsOnApple) {
#if TGLES_DEVICE_HAS_METAL
  auto bridge = tgles::metal_bridge::CreateAppleBridge();
  EXPECT_TRUE(bridge != nullptr);
  if (bridge == nullptr) return;
  // Unknown family fails without touching the device.
  EXPECT_FALSE(bridge->Initialize("Nope"));
  EXPECT_EQ(bridge->GetError(), tgles::kGlInvalidEnum);
  EXPECT_EQ(bridge->GetError(), tgles::kGlNoError);
#else
  EXPECT_TRUE(true);  // Non-Apple host: no Metal, nothing to prove.
#endif
}

TEST(MetalDevice, RealMslCompilesAndDrawsRed) {
#if TGLES_DEVICE_HAS_METAL && TGLES_DEVICE_HAS_QUARTZ
  auto bridge = tgles::metal_bridge::CreateAppleBridge();
  EXPECT_TRUE(bridge != nullptr);
  if (bridge == nullptr) return;
  if (!bridge->Initialize("Apple3")) return;  // No GPU here: skip.
  EXPECT_TRUE(bridge->IsInitialized());

  // Real PSO from real MSL (fails closed on driver compile errors).
  tgles::backend::PsoKey key;
  std::uint32_t pso = bridge->CreateRenderPipeline(key);
  EXPECT_NE(pso, 0u);
  EXPECT_EQ(bridge->CreateRenderPipeline(key), pso);
  if (pso == 0u) return;

  std::uint8_t verts[sizeof(Vert) * 3];
  float mvp[16];
  FillFullscreenRedTriangle(verts, mvp);
  bridge->SetVertexBytes(verts, sizeof(verts), 32);
  EXPECT_EQ(bridge->GetError(), tgles::kGlNoError);
  bridge->SetMVP(mvp);
  EXPECT_EQ(bridge->GetError(), tgles::kGlNoError);

  CAMetalLayer* layer = [CAMetalLayer layer];
  bridge->SetLayer((__bridge void*)layer, 16, 16);
  EXPECT_TRUE(bridge->BeginFrame(16, 16));
  bridge->BeginRenderPass();
  EXPECT_EQ(bridge->GetError(), tgles::kGlNoError);
  bridge->Draw();
  EXPECT_EQ(bridge->DrawCount(), 1u);
  bridge->EndRenderPass();
  EXPECT_TRUE(bridge->CommitFrame());
  // Async ring proof: completed must lag until explicitly waited.
  EXPECT_TRUE(bridge->Present());
  EXPECT_EQ(bridge->SwapCount(), 1u);
  EXPECT_EQ(bridge->FrameSerial(), 1u);
  EXPECT_EQ(bridge->CompletedSerial(), 0u);
  EXPECT_TRUE(bridge->WaitForCompletion(1u));
  EXPECT_EQ(bridge->CompletedSerial(), 1u);

  // Exact pixels through the ReadPixels model (RGBA order).
  std::uint8_t px[4] = {0};
  EXPECT_TRUE(bridge->ReadbackPixel(8, 8, px));
  EXPECT_EQ(px[0], 255u);
  EXPECT_EQ(px[1], 0u);
  EXPECT_EQ(px[2], 0u);
  EXPECT_EQ(px[3], 255u);
  // Exact pixels through the blit model (full-target copy).
  std::uint8_t buf[16 * 16 * 4];
  __builtin_memset(buf, 0, sizeof(buf));
  EXPECT_TRUE(bridge->BlitToCpu(buf, sizeof(buf)));
  EXPECT_EQ(buf[(8 * 16 + 8) * 4 + 0], 255u);
  EXPECT_EQ(buf[(8 * 16 + 8) * 4 + 1], 0u);
  EXPECT_EQ(buf[(8 * 16 + 8) * 4 + 2], 0u);
  EXPECT_EQ(buf[(8 * 16 + 8) * 4 + 3], 255u);
  EXPECT_EQ(bridge->GetError(), tgles::kGlNoError);
#else
  EXPECT_TRUE(true);
#endif
}

TEST(MetalDevice, RealFenceAndTextureGating) {
#if TGLES_DEVICE_HAS_METAL && TGLES_DEVICE_HAS_QUARTZ
  auto bridge = tgles::metal_bridge::CreateAppleBridge();
  if (bridge == nullptr) return;
  if (!bridge->Initialize("Apple9")) return;  // No GPU here: skip.
  // BC1 is Apple9+: real gate evaluated against the family table.
  EXPECT_NE(bridge->CreateTexture(16, 16, 0x83F1u, "Apple9"), 0u);
  EXPECT_EQ(bridge->CreateTexture(16, 16, 0x83F1u, "Apple7"), 0u);
  EXPECT_EQ(bridge->GetError(), tgles::kGlInvalidOperation);
  // Waiting for a future serial fails closed.
  EXPECT_FALSE(bridge->WaitForCompletion(999u));
  EXPECT_EQ(bridge->GetError(), tgles::kGlInvalidValue);
  EXPECT_EQ(bridge->GetError(), tgles::kGlNoError);
#else
  EXPECT_TRUE(true);
#endif
}

// Full app-data path: real GLES managers (buffers, VAO, FBO) drive the real
// bridge. Proves the RenderFrame wiring with manager-built state instead of
// hand-packed bytes.
TEST(MetalDevice, RealAppDataPathDrawsRed) {
#if TGLES_DEVICE_HAS_METAL && TGLES_DEVICE_HAS_QUARTZ
  auto bridge = tgles::metal_bridge::CreateAppleBridge();
  if (bridge == nullptr) return;
  if (!bridge->Initialize("Apple3")) return;  // No GPU here: skip.

  tgles::GlesContext ctx = tgles::GlesContext::Create(false);
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
  ctx.textures().TexImage2D(tgles::kGlTexture2d, 0, tgles::kGlRgba8, 16,
                            16, 0, tgles::kGlRgba, tgles::kGlUnsignedByte,
                            nullptr);
  tgles::GLuint f = 0;
  ctx.framebuffers().GenFramebuffers(1, &f);
  ctx.framebuffers().BindFramebuffer(tgles::kGlDrawFramebuffer, f);
  ctx.framebuffers().FramebufferTexture2D(
      tgles::kGlDrawFramebuffer, tgles::kGlColorAttachment0,
      tgles::kGlTexture2d, t, 0);
  EXPECT_TRUE(ctx.RenderFrame(*bridge, tgles::kGlTriangles, 0, 3));
  EXPECT_EQ(ctx.GetError(), tgles::kGlNoError);

  CAMetalLayer* layer = [CAMetalLayer layer];
  bridge->SetLayer((__bridge void*)layer, 16, 16);
  // RenderFrame already committed the frame; present it for real.
  EXPECT_TRUE(bridge->Present());
  EXPECT_TRUE(bridge->WaitForCompletion(bridge->FrameSerial()));
  std::uint8_t px[4] = {0};
  EXPECT_TRUE(bridge->ReadbackPixel(8, 8, px));
  EXPECT_EQ(px[0], 255u);
  EXPECT_EQ(px[1], 0u);
  EXPECT_EQ(px[2], 0u);
  EXPECT_EQ(px[3], 255u);
  EXPECT_EQ(bridge->GetError(), tgles::kGlNoError);
  EXPECT_EQ(ctx.GetError(), tgles::kGlNoError);
#else
  EXPECT_TRUE(true);
#endif
}

// Depth proof on a real GPU: two overlapping fullscreen triangles at
// different NDC depths. Case B uploads the NEAR triangle FIRST, so only a
// working depth test (not upload order) can make it win.
//
// Metal constraint (probed on host Intel, 2026-09): NDC z must lie in
// [0, 1], unlike GL's [-1, 1] — a triangle at z=-0.5 vanishes entirely
// (clipped, depth test never sees it). Hence 0.7/0.3 here, and apps must
// project into [0, 1] (also noted on DepthConfig).
TEST(MetalDevice, RealDepthResolvesOverlap) {
#if TGLES_DEVICE_HAS_METAL && TGLES_DEVICE_HAS_QUARTZ
  auto bridge = tgles::metal_bridge::CreateAppleBridge();
  if (bridge == nullptr) return;
  if (!bridge->Initialize("Apple3")) return;  // No GPU here: skip.

  tgles::GlesContext ctx = tgles::GlesContext::Create(false);
  tgles::GLuint pb = 0, cb = 0;
  ctx.buffers().GenBuffers(1, &pb);
  ctx.buffers().GenBuffers(1, &cb);
  tgles::GLuint vao = 0;
  ctx.vertex_arrays().GenVertexArrays(1, &vao);
  ctx.vertex_arrays().BindVertexArray(vao);
  ctx.vertex_arrays().VertexAttribPointer(0, 3, tgles::kGlFloat,
                                          tgles::kGlFalse, 0, 0, pb);
  ctx.vertex_arrays().VertexAttribPointer(1, 4, tgles::kGlFloat,
                                          tgles::kGlFalse, 0, 0, cb);
  ctx.vertex_arrays().EnableVertexAttribArray(0);
  ctx.vertex_arrays().EnableVertexAttribArray(1);
  // No program: RenderFrame falls back to the identity MVP, so clip z is
  // the uploaded NDC z verbatim.
  tgles::GLuint t = 0;
  ctx.textures().GenTextures(1, &t);
  ctx.textures().BindTexture(tgles::kGlTexture2d, t);
  ctx.textures().TexImage2D(tgles::kGlTexture2d, 0, tgles::kGlRgba8, 16,
                            16, 0, tgles::kGlRgba, tgles::kGlUnsignedByte,
                            nullptr);
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
  EXPECT_EQ(ctx.framebuffers().CheckFramebufferStatus(
                tgles::kGlDrawFramebuffer),
            tgles::kGlFramebufferComplete);
  ctx.foundation().Enable(tgles::kGlDepthTest);  // LESS, mask on, clear 1.

  CAMetalLayer* layer = [CAMetalLayer layer];
  bridge->SetLayer((__bridge void*)layer, 16, 16);

  // One frame = two fullscreen tris (z + flat color each), drawn in upload
  // order through a single RenderFrame Draw.
  auto draw_pair = [&](float z0, const float c0[4], float z1,
                       const float c1[4]) {
    const float pos[18] = {-1, -1, z0, 3, -1, z0, -1, 3, z0,
                           -1, -1, z1, 3, -1, z1, -1, 3, z1};
    float col[24];
    for (int v = 0; v < 3; ++v)
      for (int c = 0; c < 4; ++c) col[v * 4 + c] = c0[c];
    for (int v = 3; v < 6; ++v)
      for (int c = 0; c < 4; ++c) col[v * 4 + c] = c1[c];
    ctx.buffers().BindBuffer(tgles::kGlArrayBuffer, pb);
    ctx.buffers().BufferData(tgles::kGlArrayBuffer, sizeof(pos), pos,
                             tgles::kGlStaticDraw);
    ctx.buffers().BindBuffer(tgles::kGlArrayBuffer, cb);
    ctx.buffers().BufferData(tgles::kGlArrayBuffer, sizeof(col), col,
                             tgles::kGlStaticDraw);
    EXPECT_TRUE(ctx.RenderFrame(*bridge, tgles::kGlTriangles, 0, 6));
    EXPECT_TRUE(bridge->DepthEnabled());
    EXPECT_TRUE(bridge->Present());
    EXPECT_TRUE(bridge->WaitForCompletion(bridge->FrameSerial()));
  };

  const float kRed[4] = {1, 0, 0, 1};
  const float kGreen[4] = {0, 1, 0, 1};
  // Case A (sanity): far red first, near green second -> green wins under
  // both upload order and depth.
  draw_pair(0.7f, kRed, 0.3f, kGreen);
  std::uint8_t px[4] = {0};
  EXPECT_TRUE(bridge->ReadbackPixel(8, 8, px));
  EXPECT_EQ(px[0], 0u);
  EXPECT_EQ(px[1], 255u);
  EXPECT_EQ(px[2], 0u);
  EXPECT_EQ(px[3], 255u);
  // Case B (proof): NEAR red first, FAR green second -> red can only win
  // through the depth test (upload order alone would leave green).
  draw_pair(0.3f, kRed, 0.7f, kGreen);
  EXPECT_TRUE(bridge->ReadbackPixel(8, 8, px));
  EXPECT_EQ(px[0], 255u);
  EXPECT_EQ(px[1], 0u);
  EXPECT_EQ(px[2], 0u);
  EXPECT_EQ(px[3], 255u);
  EXPECT_EQ(bridge->GetError(), tgles::kGlNoError);
  EXPECT_EQ(ctx.GetError(), tgles::kGlNoError);
#else
  EXPECT_TRUE(true);
#endif
}
