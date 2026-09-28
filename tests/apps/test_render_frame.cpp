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
  // Multi-draw before Present: the frame stays open, so draws accumulate
  // (7 core topologies above) instead of resetting per BeginFrame.
  EXPECT_EQ(bridge.DrawCount(), 7u);
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

TEST(RenderFrame, MissingAttribUsesCurrentValue) {
  // Spec 10.3.1: a disabled array reads the current generic value — never
  // fail closed. POSITION-only formats never enable color; the facade must
  // still draw (default color) instead of rejecting the draw.
  tgles::GlesContext ctx = tgles::GlesContext::Create(false);
  BuildTriangle(ctx);
  ctx.vertex_arrays().DisableVertexAttribArray(1);
  tgles::metal_bridge::MockMetalBridge bridge;
  EXPECT_TRUE(bridge.Initialize("Apple3"));
  EXPECT_TRUE(ctx.RenderFrame(bridge, tgles::kGlTriangles, 0, 3));
  EXPECT_EQ(ctx.GetError(), tgles::kGlNoError);
  EXPECT_EQ(bridge.GetError(), tgles::kGlNoError);
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

TEST(RenderFrame, QuadDrawExpandsToTriangles) {
  // Desktop-GL QUADS (0x0007): no Metal primitive exists, so the facade
  // CPU-expands 4 verts into 6 (two triangles). Minecraft's GUI fills and
  // loading background are quads — silently rejecting them reads as a black
  // UI with a live render loop.
  tgles::GlesContext ctx = tgles::GlesContext::Create(false);
  BuildIndexedQuad(ctx);  // 4-vert position/color stores.
  tgles::metal_bridge::MockMetalBridge bridge;
  EXPECT_TRUE(bridge.Initialize("Apple3"));
  EXPECT_TRUE(ctx.RenderFrame(bridge, 0x0007u /*QUADS*/, 0, 4));
  EXPECT_EQ(ctx.GetError(), tgles::kGlNoError);
  EXPECT_EQ(bridge.DrawCount(), 1u);
  EXPECT_EQ(bridge.LastPrimitiveMode(), tgles::kGlTriangles);
  EXPECT_EQ(bridge.GetError(), tgles::kGlNoError);
  // 3 verts is not a whole quad: fail closed rather than emit garbage.
  EXPECT_FALSE(ctx.RenderFrame(bridge, 0x0007u, 0, 3));
  EXPECT_EQ(ctx.GetError(), tgles::kGlInvalidOperation);
  EXPECT_EQ(ctx.GetError(), tgles::kGlNoError);
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

// MC-style present cycle: the app clears the FBO it later blits into the
// window (the window source), then every frame clears a DIFFERENT FBO (its
// GUI target) before drawing. The GUI clear must never decide what the
// window shows — only the window-source clear may force a device-side clear
// pass. Missing this wiped MC's loading background every frame: the forced
// per-frame clear used whatever glClearColor was set last (the GUI target's
// transparent black), so the screen showed the clear color instead of the
// presented target's content.
TEST(RenderFrame, OnlyWindowSourceClearForcesClearPass) {
  tgles::GlesContext ctx = tgles::GlesContext::Create(false);
  BuildTriangle(ctx);  // draw-FBO `main` (16x16 COLOR_ATTACHMENT0).
  const tgles::GLuint main_fbo =
      ctx.framebuffers().BoundFramebuffer(tgles::kGlDrawFramebuffer);
  // A second FBO standing in for MC's GUI target (cleared every frame).
  tgles::GLuint t2 = 0;
  ctx.textures().GenTextures(1, &t2);
  ctx.textures().BindTexture(tgles::kGlTexture2d, t2);
  ctx.textures().TexImage2D(tgles::kGlTexture2d, 0, tgles::kGlRgba8, 16, 16, 0,
                            tgles::kGlRgba, tgles::kGlUnsignedByte, nullptr);
  tgles::GLuint gui_fbo = 0;
  ctx.framebuffers().GenFramebuffers(1, &gui_fbo);
  ctx.framebuffers().BindFramebuffer(tgles::kGlDrawFramebuffer, gui_fbo);
  ctx.framebuffers().FramebufferTexture2D(tgles::kGlDrawFramebuffer,
                                          tgles::kGlColorAttachment0,
                                          tgles::kGlTexture2d, t2, 0);
  ctx.framebuffers().BindFramebuffer(tgles::kGlDrawFramebuffer, main_fbo);

  tgles::metal_bridge::MockMetalBridge bridge;
  EXPECT_TRUE(bridge.Initialize("Apple3"));

  // Frame 1: clear the main target (source not learned yet) and present it.
  ctx.raster().ClearColor(1.0f, 0.0f, 0.0f, 1.0f);
  ctx.Clear(tgles::kGlColorBufferBit);
  ctx.framebuffers().BindFramebuffer(tgles::kGlReadFramebuffer, main_fbo);
  ctx.framebuffers().BindFramebuffer(tgles::kGlDrawFramebuffer, 0);
  ctx.framebuffers().BlitFramebuffer(0, 0, 16, 16, 0, 0, 16, 16,
                                     tgles::kGlColorBufferBit,
                                     tgles::kGlNearest);
  EXPECT_TRUE(ctx.framebuffers().HasWindowSource());
  EXPECT_EQ(ctx.framebuffers().WindowSource(), main_fbo);
  ctx.framebuffers().BindFramebuffer(tgles::kGlDrawFramebuffer, main_fbo);
  EXPECT_TRUE(ctx.RenderFrame(bridge, tgles::kGlTriangles, 0, 3));
  EXPECT_EQ(bridge.PassClearCount(), 1u);  // Brand-new bridge target.
  EXPECT_TRUE(bridge.CommitFrame());
  EXPECT_EQ(ctx.GetError(), tgles::kGlNoError);

  // Frame 2: only the GUI target is cleared (transparent black). The window
  // target must keep frame 1's content -> Load, not Clear.
  ctx.framebuffers().BindFramebuffer(tgles::kGlDrawFramebuffer, gui_fbo);
  ctx.raster().ClearColor(0.0f, 0.0f, 0.0f, 0.0f);
  ctx.Clear(tgles::kGlColorBufferBit);
  ctx.framebuffers().BindFramebuffer(tgles::kGlDrawFramebuffer, main_fbo);
  EXPECT_TRUE(ctx.RenderFrame(bridge, tgles::kGlTriangles, 0, 3));
  EXPECT_EQ(bridge.PassClearCount(), 1u);
  EXPECT_EQ(bridge.PassLoadCount(), 1u);
  EXPECT_TRUE(bridge.CommitFrame());
  EXPECT_EQ(ctx.GetError(), tgles::kGlNoError);

  // Frame 3: the window source itself is cleared -> device clear pass with
  // THAT clear's color (captured at glClear time, not the live state).
  ctx.raster().ClearColor(0.0f, 0.0f, 1.0f, 1.0f);
  ctx.Clear(tgles::kGlColorBufferBit);
  EXPECT_TRUE(ctx.RenderFrame(bridge, tgles::kGlTriangles, 0, 3));
  EXPECT_EQ(bridge.PassClearCount(), 2u);
  EXPECT_EQ(bridge.PassLoadCount(), 1u);
  EXPECT_EQ(ctx.GetError(), tgles::kGlNoError);
}

// MC's texture-atlas phase draws into offscreen FBOs (512x256 .. 2048x1024).
// Those draws must never allocate/resize the presented target: every size
// change wipes the pixels about to be shown (the v10 log recorded 64+ such
// wipes, with the screen sampling fully empty at swaps 70/80). BlitFramebuffer
// copies no pixels, so offscreen content cannot reach the window anyway — only
// window-sized draws may touch the presented target.
TEST(RenderFrame, OffscreenDrawDoesNotResizeWindowTarget) {
  tgles::GlesContext ctx = tgles::GlesContext::Create(false);
  BuildTriangle(ctx);  // draw-FBO 16x16 (host layer is 32x32 here).
  ctx.framebuffers().SetDefaultFramebufferSize(32, 32);
  tgles::metal_bridge::MockMetalBridge bridge;
  EXPECT_TRUE(bridge.Initialize("Apple3"));
  EXPECT_TRUE(ctx.RenderFrame(bridge, tgles::kGlTriangles, 0, 3));
  EXPECT_EQ(bridge.DrawCount(), 0u);      // recorded, not submitted.
  EXPECT_EQ(bridge.TargetWidth(), 0);     // window target never allocated.
  EXPECT_EQ(ctx.GetError(), tgles::kGlNoError);
  EXPECT_EQ(bridge.GetError(), tgles::kGlNoError);

  // A draw sized like the host layer reaches the bridge (target + draw).
  ctx.framebuffers().SetDefaultFramebufferSize(16, 16);
  EXPECT_TRUE(ctx.RenderFrame(bridge, tgles::kGlTriangles, 0, 3));
  EXPECT_EQ(bridge.DrawCount(), 1u);
  EXPECT_EQ(bridge.TargetWidth(), 16);
  EXPECT_EQ(bridge.TargetHeight(), 16);
  EXPECT_EQ(ctx.GetError(), tgles::kGlNoError);
}

// MC's present blit arrives one draw late, so the frames before it take the
// no-window-source force-clear path. That clear must repaint the window with
// the color it was LAST defined with (frame 1's background), never with the
// live glClearColor: MC switches its clear color to transparent black for the
// per-frame offscreen target clear, and repainting with that value wiped the
// red loading background on device (log v9/v10: frame 2 came up empty).
TEST(RenderFrame, ForceClearRepaintsWithLastDefinedColor) {
  tgles::GlesContext ctx = tgles::GlesContext::Create(false);
  BuildTriangle(ctx);  // draw-FBO `main` (16x16 COLOR_ATTACHMENT0).
  const tgles::GLuint main_fbo =
      ctx.framebuffers().BoundFramebuffer(tgles::kGlDrawFramebuffer);
  tgles::metal_bridge::MockMetalBridge bridge;
  EXPECT_TRUE(bridge.Initialize("Apple3"));
  int fake_layer = 0;
  bridge.SetLayer(&fake_layer, 16, 16);  // Present() needs a layer.

  // Frame 1: no window source yet -> the brand-new target is cleared with
  // the live color, and that clear is what defines the window.
  ctx.raster().ClearColor(0.937f, 0.196f, 0.239f, 1.0f);  // MC's red
  EXPECT_TRUE(ctx.RenderFrame(bridge, tgles::kGlTriangles, 0, 3));
  EXPECT_EQ(bridge.PassClearCount(), 1u);
  EXPECT_EQ(bridge.LastClearColor()[0], 0.937f);
  EXPECT_EQ(bridge.LastClearColor()[3], 1.0f);
  EXPECT_TRUE(bridge.Present());  // MC's swap: frame sealed, not committed.

  // Frame 2: the app now clears a DIFFERENT (offscreen) FBO with transparent
  // black — its live glClearColor, which must NOT decide the window color.
  tgles::GLuint t2 = 0;
  ctx.textures().GenTextures(1, &t2);
  ctx.textures().BindTexture(tgles::kGlTexture2d, t2);
  ctx.textures().TexImage2D(tgles::kGlTexture2d, 0, tgles::kGlRgba8, 16, 16, 0,
                            tgles::kGlRgba, tgles::kGlUnsignedByte, nullptr);
  tgles::GLuint gui_fbo = 0;
  ctx.framebuffers().GenFramebuffers(1, &gui_fbo);
  ctx.framebuffers().BindFramebuffer(tgles::kGlDrawFramebuffer, gui_fbo);
  ctx.framebuffers().FramebufferTexture2D(tgles::kGlDrawFramebuffer,
                                          tgles::kGlColorAttachment0,
                                          tgles::kGlTexture2d, t2, 0);
  ctx.raster().ClearColor(0.0f, 0.0f, 0.0f, 0.0f);
  ctx.Clear(tgles::kGlColorBufferBit);
  ctx.framebuffers().BindFramebuffer(tgles::kGlDrawFramebuffer, main_fbo);

  // Still no window source (no blit yet): force-clear, repainted with the
  // RED frame 1 defined the window with.
  EXPECT_TRUE(ctx.RenderFrame(bridge, tgles::kGlTriangles, 0, 3));
  EXPECT_EQ(bridge.PassClearCount(), 2u);
  EXPECT_EQ(bridge.LastClearColor()[0], 0.937f);
  EXPECT_EQ(bridge.LastClearColor()[1], 0.196f);
  EXPECT_EQ(bridge.LastClearColor()[3], 1.0f);
  EXPECT_EQ(ctx.GetError(), tgles::kGlNoError);
  EXPECT_EQ(bridge.GetError(), tgles::kGlNoError);
}

// Frame-start depth reset: MC clears DEPTH every frame, but that clear lands
// on a different (window-sized) FBO, which is deliberately not a window
// clear — so TGLES never resets the window's depth buffer. With no reset the
// buffer keeps stale/undefined contents and every depth-tested draw fails
// silently: the screen shows the clear color only (the "red background with
// nothing on it" seen on device). The pass must reset depth while LEAVING
// COLOR LOADING, or the app's background would be wiped every frame.
TEST(RenderFrame, FrameStartResetsDepthWithoutWipingColor) {
  tgles::GlesContext ctx = tgles::GlesContext::Create(false);
  BuildTriangle(ctx);  // draw-FBO `main` (16x16 COLOR_ATTACHMENT0).
  const tgles::GLuint main_fbo =
      ctx.framebuffers().BoundFramebuffer(tgles::kGlDrawFramebuffer);
  tgles::GLuint rb = 0;
  ctx.renderbuffers().GenRenderbuffers(1, &rb);
  ctx.renderbuffers().BindRenderbuffer(tgles::kGlRenderbuffer, rb);
  ctx.renderbuffers().RenderbufferStorage(tgles::kGlRenderbuffer,
                                          tgles::kGlDepthComponent24, 16, 16);
  ctx.framebuffers().FramebufferRenderbuffer(tgles::kGlDrawFramebuffer,
                                             tgles::kGlDepthAttachment,
                                             tgles::kGlRenderbuffer, rb);
  ctx.foundation().Enable(tgles::kGlDepthTest);
  // Declare `main` as the window source so the no-window-source force-clear
  // (which also clears color) does not mask the depth-only path.
  ctx.framebuffers().BindFramebuffer(tgles::kGlReadFramebuffer, main_fbo);
  ctx.framebuffers().BindFramebuffer(tgles::kGlDrawFramebuffer, 0);
  ctx.framebuffers().BlitFramebuffer(0, 0, 16, 16, 0, 0, 16, 16,
                                     tgles::kGlColorBufferBit,
                                     tgles::kGlNearest);
  EXPECT_TRUE(ctx.framebuffers().HasWindowSource());
  ctx.framebuffers().BindFramebuffer(tgles::kGlDrawFramebuffer, main_fbo);

  tgles::metal_bridge::MockMetalBridge bridge;
  EXPECT_TRUE(bridge.Initialize("Apple3"));
  int fake_layer = 0;
  bridge.SetLayer(&fake_layer, 16, 16);

  // Frame 1: brand-new target -> color clear (depth rides along).
  ctx.raster().ClearColor(0.937f, 0.196f, 0.239f, 1.0f);
  EXPECT_TRUE(ctx.RenderFrame(bridge, tgles::kGlTriangles, 0, 3));
  EXPECT_EQ(bridge.PassClearCount(), 1u);
  EXPECT_EQ(bridge.PassDepthClearCount(), 0u);
  EXPECT_TRUE(bridge.Present());

  // Frame 2: same target, no window clear -> depth-only pass, color Loads.
  EXPECT_TRUE(ctx.RenderFrame(bridge, tgles::kGlTriangles, 0, 3));
  EXPECT_EQ(bridge.PassDepthClearCount(), 1u);
  EXPECT_EQ(bridge.PassClearCount(), 1u);  // color untouched
  EXPECT_EQ(bridge.PassLoadCount(), 0u);
  EXPECT_EQ(ctx.GetError(), tgles::kGlNoError);
  EXPECT_EQ(bridge.GetError(), tgles::kGlNoError);
}
