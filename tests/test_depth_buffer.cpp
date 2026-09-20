// Depth-buffer path TDD: core depth state (enable/func/mask/clear), draw-FBO
// depth wiring through GlesContext::RenderFrame into the MetalBridge
// DepthConfig contract, and bridge-side validation. Real-pixel proof (two
// overlapping triangles, draw order vs. depth order) lives in
// tests/test_metal_device.mm (RealDepthResolvesOverlap) because only a real
// GPU rasterizer can decide it.

#include "test_framework.h"

#include "tgles/gles.h"
#include "tgles/metal_bridge.h"

namespace {

// 16x16 RGBA8 color target on the draw FBO + red fullscreen triangle as two
// float streams (positions xyz, colors rgba) on VAO attribs 0/1.
void BuildColorTarget(tgles::GlesContext& ctx, tgles::GLsizei w,
                      tgles::GLsizei h) {
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
  ctx.textures().TexImage2D(tgles::kGlTexture2d, 0, tgles::kGlRgba8, w, h, 0,
                            tgles::kGlRgba, tgles::kGlUnsignedByte, nullptr);
  tgles::GLuint f = 0;
  ctx.framebuffers().GenFramebuffers(1, &f);
  ctx.framebuffers().BindFramebuffer(tgles::kGlDrawFramebuffer, f);
  ctx.framebuffers().FramebufferTexture2D(
      tgles::kGlDrawFramebuffer, tgles::kGlColorAttachment0,
      tgles::kGlTexture2d, t, 0);
}

// DEPTH_COMPONENT24 renderbuffer attached to DEPTH_ATTACHMENT of the bound
// draw FBO.
void AttachDepthRb(tgles::GlesContext& ctx, tgles::GLsizei w, tgles::GLsizei h) {
  tgles::GLuint rb = 0;
  ctx.renderbuffers().GenRenderbuffers(1, &rb);
  ctx.renderbuffers().BindRenderbuffer(tgles::kGlRenderbuffer, rb);
  ctx.renderbuffers().RenderbufferStorage(tgles::kGlRenderbuffer,
                                          tgles::kGlDepthComponent24, w, h);
  ctx.framebuffers().FramebufferRenderbuffer(
      tgles::kGlDrawFramebuffer, tgles::kGlDepthAttachment,
      tgles::kGlRenderbuffer, rb);
}

}  // namespace

TEST(DepthBuffer, StateDefaults) {
  tgles::GlesContext ctx = tgles::GlesContext::Create(false);
  EXPECT_EQ(ctx.foundation().IsEnabled(tgles::kGlDepthTest), tgles::kGlFalse);
  EXPECT_EQ(ctx.raster().GetDepthFunc(), tgles::kGlLess);
  EXPECT_EQ(ctx.raster().GetDepthMask(), true);
  EXPECT_EQ(ctx.raster().GetClearDepth(), 1.0f);
  EXPECT_EQ(ctx.GetError(), tgles::kGlNoError);
}

TEST(DepthBuffer, FuncMaskClearRoundTrip) {
  tgles::GlesContext ctx = tgles::GlesContext::Create(false);
  ctx.raster().DepthFunc(tgles::kGlGreater);
  ctx.raster().DepthMask(tgles::kGlFalse);
  ctx.raster().ClearDepthf(0.25f);
  EXPECT_EQ(ctx.raster().GetDepthFunc(), tgles::kGlGreater);
  EXPECT_EQ(ctx.raster().GetDepthMask(), false);
  EXPECT_EQ(ctx.raster().GetClearDepth(), 0.25f);
  ctx.raster().DepthFunc(0x1234u);
  EXPECT_EQ(ctx.GetError(), tgles::kGlInvalidEnum);
  EXPECT_EQ(ctx.raster().GetDepthFunc(), tgles::kGlGreater);  // Unchanged.
  EXPECT_EQ(ctx.GetError(), tgles::kGlNoError);
}

TEST(DepthBuffer, CompleteColorDepthFbo) {
  tgles::GlesContext ctx = tgles::GlesContext::Create(false);
  BuildColorTarget(ctx, 64, 64);
  AttachDepthRb(ctx, 64, 64);
  EXPECT_EQ(ctx.framebuffers().CheckFramebufferStatus(
                tgles::kGlDrawFramebuffer),
            tgles::kGlFramebufferComplete);
  // Same attachments, smaller depth: spec 9.4 dimensions rule.
  AttachDepthRb(ctx, 32, 32);
  EXPECT_EQ(ctx.framebuffers().CheckFramebufferStatus(
                tgles::kGlDrawFramebuffer),
            tgles::kGlFramebufferIncompleteDimensions);
  EXPECT_EQ(ctx.GetError(), tgles::kGlNoError);
}

TEST(DepthBuffer, RenderFrameDepthOffByDefault) {
  tgles::GlesContext ctx = tgles::GlesContext::Create(false);
  BuildColorTarget(ctx, 16, 16);
  AttachDepthRb(ctx, 16, 16);  // Present, but the test is disabled.
  tgles::metal_bridge::MockMetalBridge bridge;
  EXPECT_TRUE(bridge.Initialize("Apple3"));
  EXPECT_TRUE(ctx.RenderFrame(bridge, tgles::kGlTriangles, 0, 3));
  EXPECT_FALSE(bridge.DepthEnabled());
  EXPECT_EQ(bridge.DrawCount(), 1u);
  EXPECT_EQ(ctx.GetError(), tgles::kGlNoError);
  EXPECT_EQ(bridge.GetError(), tgles::kGlNoError);
}

TEST(DepthBuffer, RenderFrameForwardsDepthState) {
  tgles::GlesContext ctx = tgles::GlesContext::Create(false);
  BuildColorTarget(ctx, 16, 16);
  AttachDepthRb(ctx, 16, 16);
  ctx.foundation().Enable(tgles::kGlDepthTest);
  ctx.raster().DepthFunc(tgles::kGlLequal);
  ctx.raster().DepthMask(tgles::kGlFalse);
  ctx.raster().ClearDepthf(0.25f);
  tgles::metal_bridge::MockMetalBridge bridge;
  EXPECT_TRUE(bridge.Initialize("Apple3"));
  EXPECT_TRUE(ctx.RenderFrame(bridge, tgles::kGlTriangles, 0, 3));
  EXPECT_TRUE(bridge.DepthEnabled());
  const tgles::metal_bridge::DepthConfig cfg = bridge.LastDepthConfig();
  EXPECT_EQ(cfg.func, tgles::kGlLequal);
  EXPECT_EQ(cfg.write_mask, false);
  EXPECT_EQ(cfg.clear_depth, 0.25f);
  EXPECT_EQ(ctx.GetError(), tgles::kGlNoError);
  EXPECT_EQ(bridge.GetError(), tgles::kGlNoError);
}

TEST(DepthBuffer, RenderFrameDepthWithoutAttachment) {
  tgles::GlesContext ctx = tgles::GlesContext::Create(false);
  BuildColorTarget(ctx, 16, 16);  // No depth attachment at all.
  ctx.foundation().Enable(tgles::kGlDepthTest);
  tgles::metal_bridge::MockMetalBridge bridge;
  EXPECT_TRUE(bridge.Initialize("Apple3"));
  // Documented simplification: depth test without a depth buffer renders
  // color-only (as if the test always passes).
  EXPECT_TRUE(ctx.RenderFrame(bridge, tgles::kGlTriangles, 0, 3));
  EXPECT_FALSE(bridge.DepthEnabled());
  EXPECT_EQ(bridge.DrawCount(), 1u);
  EXPECT_EQ(ctx.GetError(), tgles::kGlNoError);
  EXPECT_EQ(bridge.GetError(), tgles::kGlNoError);
}

TEST(DepthBuffer, RenderFrameDepthSizeMismatchFails) {
  tgles::GlesContext ctx = tgles::GlesContext::Create(false);
  BuildColorTarget(ctx, 16, 16);
  AttachDepthRb(ctx, 8, 8);  // Cannot back the 16x16 color target.
  ctx.foundation().Enable(tgles::kGlDepthTest);
  tgles::metal_bridge::MockMetalBridge bridge;
  EXPECT_TRUE(bridge.Initialize("Apple3"));
  EXPECT_FALSE(ctx.RenderFrame(bridge, tgles::kGlTriangles, 0, 3));
  EXPECT_EQ(ctx.GetError(), tgles::kGlInvalidOperation);
  EXPECT_EQ(ctx.GetError(), tgles::kGlNoError);
}

TEST(DepthBuffer, RenderFrameDepthStencilAttachment) {
  tgles::GlesContext ctx = tgles::GlesContext::Create(false);
  BuildColorTarget(ctx, 16, 16);
  tgles::GLuint rb = 0;
  ctx.renderbuffers().GenRenderbuffers(1, &rb);
  ctx.renderbuffers().BindRenderbuffer(tgles::kGlRenderbuffer, rb);
  ctx.renderbuffers().RenderbufferStorage(tgles::kGlRenderbuffer,
                                          tgles::kGlDepth24Stencil8, 16, 16);
  ctx.framebuffers().FramebufferRenderbuffer(
      tgles::kGlDrawFramebuffer, tgles::kGlDepthStencilAttachment,
      tgles::kGlRenderbuffer, rb);
  ctx.foundation().Enable(tgles::kGlDepthTest);
  tgles::metal_bridge::MockMetalBridge bridge;
  EXPECT_TRUE(bridge.Initialize("Apple3"));
  // The combined attachment lends its depth aspect + size; stencil testing
  // itself is not executed by the bridge.
  EXPECT_TRUE(ctx.RenderFrame(bridge, tgles::kGlTriangles, 0, 3));
  EXPECT_TRUE(bridge.DepthEnabled());
  EXPECT_EQ(ctx.GetError(), tgles::kGlNoError);
  EXPECT_EQ(bridge.GetError(), tgles::kGlNoError);
}

TEST(DepthBuffer, MockConfigureDepthValidation) {
  tgles::metal_bridge::MockMetalBridge bridge;
  EXPECT_TRUE(bridge.Initialize("Apple3"));
  EXPECT_FALSE(bridge.DepthEnabled());
  tgles::metal_bridge::DepthConfig bad;
  bad.enabled = true;
  bad.func = 0x1234u;
  bridge.ConfigureDepth(bad);
  EXPECT_EQ(bridge.GetError(), tgles::kGlInvalidEnum);
  EXPECT_FALSE(bridge.DepthEnabled());
  EXPECT_EQ(bridge.GetError(), tgles::kGlNoError);
  tgles::metal_bridge::DepthConfig cfg;
  cfg.enabled = true;
  cfg.func = tgles::kGlGreater;
  cfg.write_mask = false;
  cfg.clear_depth = 0.5f;
  bridge.ConfigureDepth(cfg);
  EXPECT_TRUE(bridge.DepthEnabled());
  EXPECT_EQ(bridge.LastDepthConfig().func, tgles::kGlGreater);
  EXPECT_EQ(bridge.LastDepthConfig().write_mask, false);
  EXPECT_EQ(bridge.LastDepthConfig().clear_depth, 0.5f);
  EXPECT_EQ(bridge.GetError(), tgles::kGlNoError);
  tgles::metal_bridge::DepthConfig off;
  bridge.ConfigureDepth(off);
  EXPECT_FALSE(bridge.DepthEnabled());
  EXPECT_EQ(bridge.GetError(), tgles::kGlNoError);
}

TEST(DepthBuffer, NullConfigureDepthFailsClosed) {
  tgles::metal_bridge::NullMetalBridge bridge;
  tgles::metal_bridge::DepthConfig cfg;
  cfg.enabled = true;
  bridge.ConfigureDepth(cfg);
  EXPECT_EQ(bridge.GetError(), tgles::kGlInvalidOperation);
  EXPECT_FALSE(bridge.DepthEnabled());
  EXPECT_EQ(bridge.GetError(), tgles::kGlNoError);
}
