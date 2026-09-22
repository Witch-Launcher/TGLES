// EXT multidraw + base-instance execution (facade level, Mock bridge).
// Ground truth: EXT_multi_draw_indirect / EXT_base_instance semantics +
// MobileGL's capability gating (strings + entry points, Loader.cpp:1006+).
// Base-instance shifts instance ids (divisor fetch uses (id+base)/divisor);
// multidraw executes each command like its single-draw sibling.

#include "test_framework.h"

#include <cstdint>
#include <cstring>
#include <vector>

#include "tgles/facade/gles.h"
#include "tgles/gpu/metal_bridge.h"

namespace {

// Red fullscreen triangle streams; color stream holds per-instance colors
// when asked (instance i reads element i of the color buffer).
void BuildScene(tgles::GlesContext& ctx, bool two_colors) {
  static const float kPos[9] = {-1, -1, 0, 3, -1, 0, -1, 3, 0};
  static const float kRed[12] = {1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1};
  static const float kRedGreen[24] = {1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1,
                                      0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1};
  tgles::GLuint pb = 0, cb = 0;
  ctx.buffers().GenBuffers(1, &pb);
  ctx.buffers().GenBuffers(1, &cb);
  ctx.buffers().BindBuffer(tgles::kGlArrayBuffer, pb);
  ctx.buffers().BufferData(tgles::kGlArrayBuffer, sizeof(kPos), kPos,
                           tgles::kGlStaticDraw);
  ctx.buffers().BindBuffer(tgles::kGlArrayBuffer, cb);
  if (two_colors) {
    ctx.buffers().BufferData(tgles::kGlArrayBuffer, sizeof(kRedGreen),
                             kRedGreen, tgles::kGlStaticDraw);
  } else {
    ctx.buffers().BufferData(tgles::kGlArrayBuffer, sizeof(kRed), kRed,
                             tgles::kGlStaticDraw);
  }
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
  ctx.textures().TexImage2D(tgles::kGlTexture2d, 0, tgles::kGlRgba8, 16, 16,
                            0, tgles::kGlRgba, tgles::kGlUnsignedByte,
                            nullptr);
  tgles::GLuint f = 0;
  ctx.framebuffers().GenFramebuffers(1, &f);
  ctx.framebuffers().BindFramebuffer(tgles::kGlDrawFramebuffer, f);
  ctx.framebuffers().FramebufferTexture2D(
      tgles::kGlDrawFramebuffer, tgles::kGlColorAttachment0,
      tgles::kGlTexture2d, t, 0);
}

}  // namespace

TEST(ExtDraws, ArraysIndirectMultidrawExecutes) {
  tgles::GlesContext ctx = tgles::GlesContext::Create(false);
  BuildScene(ctx, false);
  tgles::metal_bridge::MockMetalBridge bridge;
  EXPECT_TRUE(bridge.Initialize("Apple9"));
  // Two indirect commands: (count=3, instances=1, first=0) twice.
  tgles::GLuint ibuf = 0;
  ctx.buffers().GenBuffers(1, &ibuf);
  ctx.buffers().BindBuffer(tgles::kGlDrawIndirectBuffer, ibuf);
  static const std::uint32_t kCmd[8] = {3, 1, 0, 0, 3, 1, 0, 0};
  ctx.buffers().BufferData(tgles::kGlDrawIndirectBuffer, sizeof(kCmd), kCmd,
                           tgles::kGlStaticDraw);
  ctx.SyncIndirectState();
  EXPECT_TRUE(
      ctx.MultiDrawArraysIndirect(&bridge, tgles::kGlTriangles, 0, 2, 0));
  EXPECT_EQ(ctx.GetError(), tgles::kGlNoError);
  // Strided variant (stride 32 > command 16).
  static const std::uint8_t kPad[64] = {3, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0,
                                        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                        0, 0, 0, 0, 0, 0, 0, 0, 3, 0, 0, 0,
                                        1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
  ctx.buffers().BufferData(tgles::kGlDrawIndirectBuffer, sizeof(kPad), kPad,
                           tgles::kGlStaticDraw);
  EXPECT_TRUE(
      ctx.MultiDrawArraysIndirect(&bridge, tgles::kGlTriangles, 0, 2, 32));
  EXPECT_EQ(ctx.GetError(), tgles::kGlNoError);
  // Negative drawcount fails closed.
  EXPECT_FALSE(
      ctx.MultiDrawArraysIndirect(&bridge, tgles::kGlTriangles, 0, -1, 0));
  EXPECT_EQ(ctx.GetError(), tgles::kGlInvalidOperation);
  EXPECT_EQ(ctx.GetError(), tgles::kGlNoError);
}

TEST(ExtDraws, ElementsIndirectMultidrawExecutes) {
  tgles::GlesContext ctx = tgles::GlesContext::Create(false);
  BuildScene(ctx, false);
  tgles::GLuint eab = 0;
  ctx.buffers().GenBuffers(1, &eab);
  ctx.buffers().BindBuffer(tgles::kGlElementArrayBuffer, eab);
  static const std::uint8_t kIdx[3] = {0, 1, 2};
  ctx.buffers().BufferData(tgles::kGlElementArrayBuffer, sizeof(kIdx), kIdx,
                           tgles::kGlStaticDraw);
  ctx.vertex_arrays().SetElementArrayBuffer(eab);
  tgles::metal_bridge::MockMetalBridge bridge;
  EXPECT_TRUE(bridge.Initialize("Apple9"));
  tgles::GLuint ibuf = 0;
  ctx.buffers().GenBuffers(1, &ibuf);
  ctx.buffers().BindBuffer(tgles::kGlDrawIndirectBuffer, ibuf);
  static const std::uint32_t kCmd[10] = {3, 1, 0, 0, 0, 3, 1, 0, 0, 0};
  ctx.buffers().BufferData(tgles::kGlDrawIndirectBuffer, sizeof(kCmd), kCmd,
                           tgles::kGlStaticDraw);
  ctx.SyncIndirectState();
  EXPECT_TRUE(ctx.MultiDrawElementsIndirect(
      &bridge, tgles::kGlTriangles, tgles::kGlUnsignedByte, 0, 2, 0));
  EXPECT_EQ(ctx.GetError(), tgles::kGlNoError);
}

TEST(ExtDraws, ClientListBaseVertexMultidrawExecutes) {
  tgles::GlesContext ctx = tgles::GlesContext::Create(false);
  BuildScene(ctx, false);
  tgles::GLuint eab = 0;
  ctx.buffers().GenBuffers(1, &eab);
  ctx.buffers().BindBuffer(tgles::kGlElementArrayBuffer, eab);
  static const std::uint8_t kIdx[6] = {0, 1, 2, 0, 1, 2};
  ctx.buffers().BufferData(tgles::kGlElementArrayBuffer, sizeof(kIdx), kIdx,
                           tgles::kGlStaticDraw);
  ctx.vertex_arrays().SetElementArrayBuffer(eab);
  tgles::metal_bridge::MockMetalBridge bridge;
  EXPECT_TRUE(bridge.Initialize("Apple9"));
  static const tgles::GLsizei kCounts[2] = {3, 3};
  static const void* kPtrs[2] = {reinterpret_cast<const void*>(0),
                                 reinterpret_cast<const void*>(3)};
  static const tgles::GLint kBase[2] = {0, 0};
  EXPECT_TRUE(ctx.MultiDrawElementsBaseVertex(
      &bridge, tgles::kGlTriangles, kCounts, tgles::kGlUnsignedByte, kPtrs,
      2, kBase));
  EXPECT_EQ(ctx.GetError(), tgles::kGlNoError);
  // Null tables with draws pending fail closed.
  EXPECT_FALSE(ctx.MultiDrawElementsBaseVertex(
      &bridge, tgles::kGlTriangles, nullptr, tgles::kGlUnsignedByte, kPtrs,
      1, kBase));
  EXPECT_EQ(ctx.GetError(), tgles::kGlInvalidOperation);
  EXPECT_EQ(ctx.GetError(), tgles::kGlNoError);
}

TEST(ExtDraws, BaseInstanceShiftsIds) {
  tgles::GlesContext ctx = tgles::GlesContext::Create(false);
  // Color buffer holds [red, green]; divisor 1 on attrib 1.
  BuildScene(ctx, true);
  ctx.vertex_arrays().VertexAttribDivisor(1, 1);
  tgles::metal_bridge::MockMetalBridge bridge;
  EXPECT_TRUE(bridge.Initialize("Apple9"));
  // base 0, one instance -> instance id 0 (red); validation + execution ok.
  EXPECT_TRUE(ctx.RenderArraysInstancedBaseInstance(
      &bridge, tgles::kGlTriangles, 0, 3, 1, 0));
  EXPECT_EQ(ctx.GetError(), tgles::kGlNoError);
  // base 1, one instance -> instance id 1 (green entry, divisor stepping).
  EXPECT_TRUE(ctx.RenderArraysInstancedBaseInstance(
      &bridge, tgles::kGlTriangles, 0, 3, 1, 1));
  EXPECT_EQ(ctx.GetError(), tgles::kGlNoError);
  // Indexed + base-vertex + base-instance combined.
  tgles::GLuint eab = 0;
  ctx.buffers().GenBuffers(1, &eab);
  ctx.buffers().BindBuffer(tgles::kGlElementArrayBuffer, eab);
  static const std::uint8_t kIdx[3] = {0, 1, 2};
  ctx.buffers().BufferData(tgles::kGlElementArrayBuffer, sizeof(kIdx), kIdx,
                           tgles::kGlStaticDraw);
  ctx.vertex_arrays().SetElementArrayBuffer(eab);
  EXPECT_TRUE(ctx.RenderElementsInstancedBaseInstance(
      &bridge, tgles::kGlTriangles, 3, tgles::kGlUnsignedByte, 0, 1, 1));
  EXPECT_EQ(ctx.GetError(), tgles::kGlNoError);
  EXPECT_TRUE(ctx.RenderElementsInstancedBaseVertexBaseInstance(
      &bridge, tgles::kGlTriangles, 3, tgles::kGlUnsignedByte, 0, 1, 0, 1));
  EXPECT_EQ(ctx.GetError(), tgles::kGlNoError);
}
