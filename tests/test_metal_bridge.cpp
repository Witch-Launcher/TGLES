// Step 11 TDD (red first): Metal execution + CAMetalLayer present bridge.
//
// Why a separate ObjC++ target: tgles_core stays pure C++17 (no <Metal/*>,
// no <QuartzCore/*>) so macOS-Intel host tests, iphoneos/arm64 device and
// iphonesimulator builds share it. The real MTL calls live in
// src/metal_bridge_apple.mm (OBJCXX, -framework Metal/QuartzCore/Foundation,
// iOS SDK only). This file tests the pure-C++ contract that the .mm must
// honor, via NullMetalBridge (no-op) and MockMetalBridge (records calls,
// simulates drawable/present + frame-fence serials).
//
// Ground truth mirrored here:
// - MobileGL MG_Backend/DirectGLES/DirectGLES.cpp:10601 Present(): one
//   glFenceSync per frame BEFORE eglSwapBuffers, poll prior fences AFTER to
//   advance the completed-frame watermark gating buffer-pool recycling.
// - MobileGL MG_Impl/EGLImpl/EGLImpl.cpp:59 DetectWindowBackend():
//   __APPLE__ -> MetalLayer; BackendObject.cpp:191 CreateEGLWindowSurface
//   with WindowHandle{MetalLayer, CAMetalLayer*}.
// - TGL backend.h:7 + egl.h:7 + egl.cpp:651: planning-only PsoCache/
//   CommandPlan + SwapBuffers counting, real present in the iOS bridge.

#include "test_framework.h"

#include "tgles/backend.h"
#include "tgles/metal_bridge.h"
#include "tgles/texture.h"

TEST(MetalBridge, NullBridgeIsSafeNoOp) {
  tgles::metal_bridge::NullMetalBridge bridge;
  EXPECT_FALSE(bridge.IsInitialized());
  EXPECT_EQ(bridge.CreateRenderPipeline(tgles::backend::PsoKey()), 0u);
  EXPECT_EQ(
      bridge.CreateBuffer(64, tgles::backend::StorageMode::kShared), 0u);
  EXPECT_FALSE(bridge.BeginFrame(16, 16));
  EXPECT_EQ(bridge.GetError(), tgles::kGlInvalidOperation);
  EXPECT_EQ(bridge.FrameSerial(), 0u);
  EXPECT_EQ(bridge.SwapCount(), 0u);
}

TEST(MetalBridge, InitializeGatesOnFamily) {
  tgles::metal_bridge::MockMetalBridge bridge;
  EXPECT_TRUE(bridge.Initialize("Apple3"));
  EXPECT_TRUE(bridge.IsInitialized());
  EXPECT_EQ(bridge.DeviceFamily(), std::string("Apple3"));
  EXPECT_FALSE(bridge.Initialize("Nope"));
  EXPECT_EQ(bridge.GetError(), tgles::kGlInvalidEnum);
  EXPECT_EQ(bridge.GetError(), tgles::kGlNoError);
}

TEST(MetalBridge, PipelineCreateReusesSameKey) {
  tgles::metal_bridge::MockMetalBridge bridge;
  EXPECT_TRUE(bridge.Initialize("Apple9"));
  tgles::backend::PsoKey a;
  a.vertex_program = 1;
  tgles::backend::PsoKey b = a;
  b.fragment_program = 2;
  uint32_t ha = bridge.CreateRenderPipeline(a);
  EXPECT_NE(ha, 0u);
  EXPECT_EQ(bridge.CreateRenderPipeline(a), ha);
  EXPECT_NE(bridge.CreateRenderPipeline(b), ha);
  EXPECT_EQ(bridge.PipelineCount(), 2u);
  EXPECT_EQ(bridge.GetError(), tgles::kGlNoError);
}

TEST(MetalBridge, BufferPlacementFollowsResourcePlan) {
  tgles::metal_bridge::MockMetalBridge bridge;
  EXPECT_TRUE(bridge.Initialize("Apple7"));
  uint32_t shared = bridge.CreateBuffer(
      64, tgles::backend::ResourcePlan::AdviseBufferStorage(
              tgles::kGlDynamicDraw));
  uint32_t priv = bridge.CreateBuffer(
      64, tgles::backend::ResourcePlan::AdviseBufferStorage(
              tgles::kGlStaticDraw));
  EXPECT_NE(shared, 0u);
  EXPECT_NE(priv, 0u);
  EXPECT_TRUE(bridge.IsBufferShared(shared));
  EXPECT_FALSE(bridge.IsBufferShared(priv));
  EXPECT_EQ(bridge.CreateBuffer(0, tgles::backend::StorageMode::kShared),
            0u);
  EXPECT_EQ(bridge.GetError(), tgles::kGlInvalidValue);
  EXPECT_EQ(bridge.GetError(), tgles::kGlNoError);
}

TEST(MetalBridge, TextureFormatGatedByFamily) {
  tgles::metal_bridge::MockMetalBridge bridge;
  EXPECT_TRUE(bridge.Initialize("Apple7"));
  // BC1 needs Apple9+ (Metal tables May 2026); RGBA8 works on Apple3+.
  EXPECT_EQ(bridge.CreateTexture(64, 64, 0x83F1u /*BC1*/, "Apple7"), 0u);
  EXPECT_EQ(bridge.GetError(), tgles::kGlInvalidOperation);
  EXPECT_NE(
      bridge.CreateTexture(64, 64, 0x83F1u, "Apple9"), 0u);
  EXPECT_NE(bridge.CreateTexture(64, 64, tgles::kGlRgba8, "Apple3"),
            0u);
  EXPECT_EQ(bridge.CreateTexture(0, 64, tgles::kGlRgba8, "Apple9"), 0u);
  EXPECT_EQ(bridge.GetError(), tgles::kGlInvalidValue);
  EXPECT_EQ(bridge.GetError(), tgles::kGlNoError);
}

TEST(MetalBridge, EncoderOrderingEnforced) {
  tgles::metal_bridge::MockMetalBridge bridge;
  EXPECT_TRUE(bridge.Initialize("Apple9"));
  bridge.Draw();  // No open pass.
  EXPECT_EQ(bridge.GetError(), tgles::kGlInvalidOperation);
  EXPECT_TRUE(bridge.BeginFrame(16, 16));
  bridge.Blit();  // Blit needs the render pass closed... but no pass open:
  // Mock treats stray blit outside a pass as legal no-op queue (like a
  // blit encoder), so no error here; the illegal case is blit INSIDE pass.
  EXPECT_EQ(bridge.GetError(), tgles::kGlNoError);
  bridge.BeginRenderPass();
  bridge.Draw();
  bridge.Draw();
  EXPECT_EQ(bridge.DrawCount(), 2u);
  bridge.Blit();  // Inside an open render pass -> error.
  EXPECT_EQ(bridge.GetError(), tgles::kGlInvalidOperation);
  bridge.EndRenderPass();
  bridge.Blit();
  EXPECT_EQ(bridge.GetError(), tgles::kGlNoError);
  EXPECT_TRUE(bridge.CommitFrame());
  EXPECT_TRUE(bridge.Committed());
  bridge.Draw();  // After commit.
  EXPECT_EQ(bridge.GetError(), tgles::kGlInvalidOperation);
  EXPECT_EQ(bridge.GetError(), tgles::kGlNoError);
}

TEST(MetalBridge, PresentNeedsLayerAndAdvancesSerials) {
  tgles::metal_bridge::MockMetalBridge bridge;
  EXPECT_TRUE(bridge.Initialize("Apple9"));
  int fake_layer = 1;
  // No layer -> present must fail (mirrors zero-area suspend / missing
  // CAMetalLayer in MobileGL); offscreen commit without present still works.
  EXPECT_TRUE(bridge.BeginFrame(16, 16));
  bridge.BeginRenderPass();
  bridge.Draw();
  bridge.EndRenderPass();
  EXPECT_TRUE(bridge.CommitFrame());
  EXPECT_FALSE(bridge.Present());
  EXPECT_EQ(bridge.GetError(), tgles::kGlInvalidOperation);
  // Attach layer -> present succeeds, swap + frame serials advance together,
  // completed watermark follows (fence ring simulation).
  bridge.SetLayer(&fake_layer, 16, 16);
  EXPECT_TRUE(bridge.BeginFrame(16, 16));
  bridge.BeginRenderPass();
  bridge.Draw();
  bridge.EndRenderPass();
  EXPECT_TRUE(bridge.CommitFrame());
  EXPECT_TRUE(bridge.Present());
  EXPECT_EQ(bridge.SwapCount(), 1u);
  EXPECT_EQ(bridge.FrameSerial(), 1u);
  EXPECT_EQ(bridge.CompletedSerial(), 1u);
  EXPECT_TRUE(bridge.BeginFrame(16, 16));
  bridge.BeginRenderPass();
  bridge.EndRenderPass();
  EXPECT_TRUE(bridge.CommitFrame());
  EXPECT_TRUE(bridge.Present());
  EXPECT_EQ(bridge.SwapCount(), 2u);
  EXPECT_EQ(bridge.FrameSerial(), 2u);
  EXPECT_EQ(bridge.CompletedSerial(), 2u);
  EXPECT_EQ(bridge.GetError(), tgles::kGlNoError);
}

TEST(MetalBridge, ResizeZeroAreaSuspendsPresent) {
  tgles::metal_bridge::MockMetalBridge bridge;
  EXPECT_TRUE(bridge.Initialize("Apple9"));
  int fake_layer = 2;
  bridge.SetLayer(&fake_layer, 16, 16);
  bridge.Resize(0, 16);  // Zero-area -> suspend like MobileGL WSI.
  EXPECT_TRUE(bridge.BeginFrame(16, 16));
  bridge.BeginRenderPass();
  bridge.EndRenderPass();
  EXPECT_TRUE(bridge.CommitFrame());
  EXPECT_FALSE(bridge.Present());
  EXPECT_EQ(bridge.GetError(), tgles::kGlInvalidOperation);
  bridge.Resize(16, 16);
  EXPECT_TRUE(bridge.BeginFrame(16, 16));
  bridge.BeginRenderPass();
  bridge.EndRenderPass();
  EXPECT_TRUE(bridge.CommitFrame());
  EXPECT_TRUE(bridge.Present());
  EXPECT_EQ(bridge.SwapCount(), 1u);
  EXPECT_EQ(bridge.GetError(), tgles::kGlNoError);
}

TEST(MetalBridge, PsoCacheHandlesStayStableAcrossFrames) {
  tgles::metal_bridge::MockMetalBridge bridge;
  EXPECT_TRUE(bridge.Initialize("Apple9"));
  int fake_layer = 3;
  bridge.SetLayer(&fake_layer, 8, 8);
  tgles::backend::PsoKey key;
  key.vertex_program = 7;
  uint32_t h = bridge.CreateRenderPipeline(key);
  for (int i = 0; i < 3; ++i) {
    EXPECT_TRUE(bridge.BeginFrame(8, 8));
    bridge.BeginRenderPass();
    bridge.Draw();
    bridge.EndRenderPass();
    EXPECT_TRUE(bridge.CommitFrame());
    EXPECT_TRUE(bridge.Present());
  }
  EXPECT_EQ(bridge.CreateRenderPipeline(key), h);
  EXPECT_EQ(bridge.SwapCount(), 3u);
  EXPECT_EQ(bridge.FrameSerial(), 3u);
}
