// RenderFrame TDD (red first): wires app state (VAO attribs + buffer
// stores + program MVP + draw-FBO size) into a MetalBridge frame.
// Bridge layout contract: interleaved float4 position + float4 color,
// stride 32, matching the bridge MSL VertexIn (attribute(0/1)).

#include "test_framework.h"

#include "tgles/gles.h"
#include "tgles/metal_bridge.h"

namespace {

// Red fullscreen triangle as two separate float3/float4 streams, like an
// app would upload them (positions xyz + colors rgba).
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
  // 16x16 color texture on draw-FBO COLOR_ATTACHMENT0 gives the target size.
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
}

}  // namespace

TEST(RenderFrame, MockDrawsAppTriangle) {
  tgles::GlesContext ctx = tgles::GlesContext::Create(false);
  BuildTriangle(ctx);
  tgles::metal_bridge::MockMetalBridge bridge;
  EXPECT_TRUE(bridge.Initialize("Apple3"));
  EXPECT_TRUE(ctx.RenderFrame(bridge, tgles::kGlTriangles, 0, 3));
  EXPECT_EQ(bridge.DrawCount(), 1u);
  EXPECT_EQ(ctx.GetError(), tgles::kGlNoError);
  EXPECT_EQ(bridge.GetError(), tgles::kGlNoError);
}

TEST(RenderFrame, NonTriangleModeFailsClosed) {
  tgles::GlesContext ctx = tgles::GlesContext::Create(false);
  BuildTriangle(ctx);
  tgles::metal_bridge::MockMetalBridge bridge;
  EXPECT_TRUE(bridge.Initialize("Apple3"));
  EXPECT_FALSE(ctx.RenderFrame(bridge, tgles::kGlPoints, 0, 3));
  EXPECT_EQ(ctx.GetError(), tgles::kGlInvalidOperation);
  EXPECT_EQ(ctx.GetError(), tgles::kGlNoError);
}

TEST(RenderFrame, MissingAttribFailsClosed) {
  tgles::GlesContext ctx = tgles::GlesContext::Create(false);
  BuildTriangle(ctx);
  ctx.vertex_arrays().DisableVertexAttribArray(1);
  tgles::metal_bridge::MockMetalBridge bridge;
  EXPECT_TRUE(bridge.Initialize("Apple3"));
  EXPECT_FALSE(ctx.RenderFrame(bridge, tgles::kGlTriangles, 0, 3));
  EXPECT_EQ(ctx.GetError(), tgles::kGlInvalidOperation);
  EXPECT_EQ(ctx.GetError(), tgles::kGlNoError);
}

TEST(RenderFrame, DefaultFramebufferHasNoSize) {
  tgles::GlesContext ctx = tgles::GlesContext::Create(false);
  BuildTriangle(ctx);
  // Unbind the FBO: window size is unknown in the CPU model.
  ctx.framebuffers().BindFramebuffer(tgles::kGlDrawFramebuffer, 0);
  tgles::metal_bridge::MockMetalBridge bridge;
  EXPECT_TRUE(bridge.Initialize("Apple3"));
  EXPECT_FALSE(ctx.RenderFrame(bridge, tgles::kGlTriangles, 0, 3));
  EXPECT_EQ(ctx.GetError(), tgles::kGlInvalidOperation);
  EXPECT_EQ(ctx.GetError(), tgles::kGlNoError);
}

TEST(RenderFrame, ZeroCountIsNoOp) {
  tgles::GlesContext ctx = tgles::GlesContext::Create(false);
  BuildTriangle(ctx);
  tgles::metal_bridge::MockMetalBridge bridge;
  EXPECT_TRUE(bridge.Initialize("Apple3"));
  EXPECT_TRUE(ctx.RenderFrame(bridge, tgles::kGlTriangles, 0, 0));
  EXPECT_EQ(bridge.DrawCount(), 0u);
  EXPECT_EQ(ctx.GetError(), tgles::kGlNoError);
}
