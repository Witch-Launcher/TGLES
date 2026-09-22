// RenderFrame TDD (red first): wires app state (VAO attribs + buffer
// stores + program MVP + draw-FBO size) into a MetalBridge frame.
// Bridge layout contract: interleaved float4 position + float4 color,
// stride 32, matching the bridge MSL VertexIn (attribute(0/1)).

#include "test_framework.h"

#include <cstdint>

#include "tgles/facade/gles.h"
#include "tgles/gpu/metal_bridge.h"

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

TEST(RenderFrame, BindsKeyCompiledFromLiveState) {
  // Type-1a (Mock side): the facade must bind exactly the PSO compiled from
  // current GL state — not a default. Enables BLEND with a distinct equation
  // so the key differs from a default PsoKey.
  tgles::GlesContext ctx = tgles::GlesContext::Create(false);
  BuildTriangle(ctx);
  ctx.foundation().Enable(tgles::kGlBlend);
  ctx.raster().BlendFuncSeparate(tgles::kGlSrcAlpha,
                                 tgles::kGlOneMinusSrcAlpha, tgles::kGlOne,
                                 tgles::kGlZero);
  tgles::metal_bridge::MockMetalBridge bridge;
  EXPECT_TRUE(bridge.Initialize("Apple3"));
  EXPECT_TRUE(ctx.RenderFrame(bridge, tgles::kGlTriangles, 0, 3));
  const tgles::backend::PsoKey want =
      tgles::backend::PsoKeyForDraw(ctx.foundation(), ctx.raster());
  EXPECT_TRUE(want.blend_enabled);
  EXPECT_EQ(bridge.BoundPipeline(), bridge.CreateRenderPipeline(want));
  EXPECT_NE(bridge.BoundPipeline(), 0u);
  EXPECT_EQ(ctx.GetError(), tgles::kGlNoError);
}

TEST(RenderFrame, CoreTopologiesExecuteAdjacencyExpandsPatchesFail) {
  tgles::GlesContext ctx = tgles::GlesContext::Create(false);
  BuildTriangle(ctx);
  tgles::metal_bridge::MockMetalBridge bridge;
  EXPECT_TRUE(bridge.Initialize("Apple3"));
  // POINTS/LINES/STRIP/FAN/LOOP thực thi (fan/loop CPU-expand trong facade).
  for (tgles::GLenum mode :
       {tgles::kGlPoints, tgles::kGlLines, tgles::kGlLineStrip,
        tgles::kGlTriangles, tgles::kGlTriangleStrip, tgles::kGlTriangleFan,
        tgles::kGlLineLoop}) {
    EXPECT_TRUE(ctx.RenderFrame(bridge, mode, 0, 3));
    EXPECT_EQ(ctx.GetError(), tgles::kGlNoError);
  }
  EXPECT_EQ(bridge.DrawCount(), 1u);  // Mock resets draws_ per BeginFrame.
  EXPECT_EQ(bridge.LastPrimitiveMode(), tgles::kGlLineStrip);  // Loop expands.
  // Adjacency CPU-expands (Phase 4 item 1, no geometry shader needed):
  // LINES_ADJACENCY 4 verts -> LINES (verts 1,2 of 0..3 fit in the 3-vert
  // scene, adjacency verts 0/3 are dropped before decode).
  EXPECT_TRUE(ctx.RenderFrame(bridge, 0x000Au /*LINES_ADJACENCY*/, 0, 4));
  EXPECT_EQ(ctx.GetError(), tgles::kGlNoError);
  EXPECT_EQ(bridge.LastPrimitiveMode(), tgles::kGlLines);
  // TRIANGLES_ADJACENCY needs 6 verts (0,2,4 real); the 3-vert scene cannot
  // supply vertex 4, so a 6-vert check lives in Phase4.LinesAdjacencyExpands
  // and the real pixel test (both build 6-vert scenes). Here we pin the
  // count rule: 3 is not a multiple of 6.
  // Bad adjacency counts still fail (spec: multiples of 4/6).
  EXPECT_FALSE(ctx.RenderFrame(bridge, 0x000Cu /*TRIANGLES_ADJACENCY*/, 0, 3));
  EXPECT_EQ(ctx.GetError(), tgles::kGlInvalidOperation);
  EXPECT_EQ(ctx.GetError(), tgles::kGlNoError);
  // PATCHES needs mesh (Apple7+ Metal3): Intel fail-closed with TGL-DEBUG.
  EXPECT_FALSE(ctx.RenderFrame(bridge, 0x000Eu /*PATCHES*/, 0, 3));
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

// Indexed path (what every game uses): a quad as 4 verts + 6 UShort indices.
// The ELEMENT_ARRAY_BUFFER bind goes through the facade so VAO EAB state
// follows it (spec 10.3.2); indices are offsets into that store.
namespace {

void BuildIndexedQuad(tgles::GlesContext& ctx) {
  static const float kPos[12] = {-1, -1, 0, 1, -1, 0,
                                 1,  1,  0, -1, 1, 0};
  static const float kCol[16] = {1, 0, 0, 1, 0, 1, 0, 1,
                                 0, 0, 1, 1, 1, 1, 1, 1};
  static const std::uint16_t kIdx[6] = {0, 1, 2, 0, 2, 3};
  tgles::GLuint pb = 0, cb = 0, eb = 0;
  ctx.buffers().GenBuffers(1, &pb);
  ctx.buffers().GenBuffers(1, &cb);
  ctx.buffers().GenBuffers(1, &eb);
  ctx.buffers().BindBuffer(tgles::kGlArrayBuffer, pb);
  ctx.buffers().BufferData(tgles::kGlArrayBuffer, sizeof(kPos), kPos,
                           tgles::kGlStaticDraw);
  ctx.buffers().BindBuffer(tgles::kGlArrayBuffer, cb);
  ctx.buffers().BufferData(tgles::kGlArrayBuffer, sizeof(kCol), kCol,
                           tgles::kGlStaticDraw);
  ctx.BindBuffer(tgles::kGlElementArrayBuffer, eb);  // Facade: syncs VAO EAB.
  ctx.buffers().BufferData(tgles::kGlElementArrayBuffer, sizeof(kIdx), kIdx,
                           tgles::kGlStaticDraw);
  tgles::GLuint vao = 0;
  ctx.vertex_arrays().GenVertexArrays(1, &vao);
  ctx.vertex_arrays().BindVertexArray(vao);
  // Re-bind EAB after the VAO bind: EAB is VAO state (spec 10.3.2).
  ctx.BindBuffer(tgles::kGlElementArrayBuffer, eb);
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
}

}  // namespace

TEST(RenderElements, IndexedQuadDraws) {
  tgles::GlesContext ctx = tgles::GlesContext::Create(false);
  BuildIndexedQuad(ctx);
  tgles::metal_bridge::MockMetalBridge bridge;
  EXPECT_TRUE(bridge.Initialize("Apple3"));
  EXPECT_TRUE(ctx.RenderElements(bridge, tgles::kGlTriangles, 6,
                                 tgles::kGlUnsignedShort, 0));
  EXPECT_EQ(bridge.DrawCount(), 1u);
  EXPECT_EQ(ctx.GetError(), tgles::kGlNoError);
  EXPECT_EQ(bridge.GetError(), tgles::kGlNoError);
}

TEST(RenderElements, NoElementBufferFailsClosed) {
  tgles::GlesContext ctx = tgles::GlesContext::Create(false);
  BuildTriangle(ctx);  // No EAB anywhere: ES 3.2 core has no client arrays.
  tgles::metal_bridge::MockMetalBridge bridge;
  EXPECT_TRUE(bridge.Initialize("Apple3"));
  EXPECT_FALSE(ctx.RenderElements(bridge, tgles::kGlTriangles, 3,
                                  tgles::kGlUnsignedShort, 0));
  EXPECT_EQ(ctx.GetError(), tgles::kGlInvalidOperation);
  EXPECT_EQ(ctx.GetError(), tgles::kGlNoError);
}

TEST(FacadeClear, ColorFillReachesTextureStore) {
  tgles::GlesContext ctx = tgles::GlesContext::Create(false);
  BuildTriangle(ctx);  // 16x16 RGBA8 texture on draw-FBO COLOR_ATTACHMENT0.
  ctx.raster().ClearColor(1.0f, 0.0f, 0.0f, 1.0f);
  ctx.Clear(0x00004000u /*COLOR_BUFFER_BIT*/);
  EXPECT_EQ(ctx.GetError(), tgles::kGlNoError);
  // Every texel of the attachment is now opaque red.
  tgles::GLuint fbo = ctx.framebuffers().BoundFramebuffer(
      tgles::kGlDrawFramebuffer);
  tgles::Attachment att =
      ctx.framebuffers().AttachmentState(fbo, tgles::kGlColorAttachment0);
  EXPECT_TRUE(att.present && att.is_texture);
  tgles::TextureLevel lvl =
      ctx.textures().LevelState(att.name, att.textarget, att.level);
  EXPECT_EQ(lvl.pixels.size(), std::size_t{16 * 16 * 4});
  for (std::size_t i = 0; i < lvl.pixels.size(); i += 4) {
    EXPECT_EQ(lvl.pixels[i + 0], (std::uint8_t)255);
    EXPECT_EQ(lvl.pixels[i + 1], (std::uint8_t)0);
    EXPECT_EQ(lvl.pixels[i + 2], (std::uint8_t)0);
    EXPECT_EQ(lvl.pixels[i + 3], (std::uint8_t)255);
  }
}

TEST(RenderElements, BadIndexTypeFailsClosed) {
  tgles::GlesContext ctx = tgles::GlesContext::Create(false);
  BuildIndexedQuad(ctx);
  tgles::metal_bridge::MockMetalBridge bridge;
  EXPECT_TRUE(bridge.Initialize("Apple3"));
  EXPECT_FALSE(ctx.RenderElements(bridge, tgles::kGlTriangles, 6,
                                  0x9999u /*bad type*/, 0));
  EXPECT_EQ(ctx.GetError(), tgles::kGlInvalidEnum);
  EXPECT_EQ(ctx.GetError(), tgles::kGlNoError);
}
