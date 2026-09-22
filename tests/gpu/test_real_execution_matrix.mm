// Ma trận thực thi thật trên GPU (Apple only, cần MTLDevice thật).
//
// Trung thực, không đoán: mỗi TEST dưới đây chạy trên Apple bridge thật và
// khẳng định pixel thật (ReadbackPixel) khi đường đó thực thi, hoặc mã lỗi
// GL thật khi đường đó fail-closed trong code (src/facade/gles.cpp,
// src/host/host_runtime.cpp). Không có gì ở đây chạy trên Mock.
//
// Bản đồ trung thực sau khi fix (Phase 4):
//   THỰC THI (pixel thật): POINTS/LINES/LINE_STRIP/TRIANGLES/TRIANGLE_STRIP
//     trực tiếp; LINE_LOOP/TRIANGLE_FAN + adjacency (LINES/TRIANGLES/
//     STRIP_ADJACENCY bỏ verts kề) CPU-expand trong facade;
//     indexed/instanced/indirect/range/base-vertex; attrib
//     fixed-point/packed/half/normalized; divisor per-instance; stencil
//     func/mask/op hai mặt; blend/depth (kể cả depth+blend, per-buffer blend
//     qua descriptor loop, MRT+MSAA resolve từng attachment) + CONSTANT_*;
//     textured sampling (2D/cube/3D/array + mips + LOD clamps); uniform arrays/
//     structs/InstanceID; UBO int/bool; compute ADD_ONE; clear/readback.
//   FAIL-CLOSED (INVALID_OPERATION thật): PATCHES (cần mesh Apple7+ Metal3),
//     geometry/tess programs (logic stage bị bỏ sẽ sai), BGRA vertex format,
//     thiếu COLOR_ATTACHMENT0. Stencil split-refs VẼ HAI PASS; integer attribs
//     convert sang float.
//   VALIDATE-NHƯNG-KHÔNG-THỰC-THI: không còn (MRT n>1 thực thi).

#import <Foundation/Foundation.h>
#if __has_include(<Metal/Metal.h>)
#import <Metal/Metal.h>
#define TGLES_REAL_HAS_METAL 1
#else
#define TGLES_REAL_HAS_METAL 0
#endif
#if __has_include(<QuartzCore/QuartzCore.h>)
#import <QuartzCore/QuartzCore.h>
#define TGLES_REAL_HAS_QUARTZ 1
#else
#define TGLES_REAL_HAS_QUARTZ 0
#endif

#include "test_framework.h"

#include "tgles/facade/gles.h"
#include "tgles/gpu/metal_bridge_apple.h"
#include "tgles/gpu/metal_mapping.h"
#include "tgles/host/abi.h"
#include "tgles/host/abi_gl.h"
#include "tgles/host/host_runtime.h"

#if TGLES_REAL_HAS_METAL && TGLES_REAL_HAS_QUARTZ

namespace {

// Tam giác fullscreen đỏ, đúng layout MSL VertexIn (float4 pos @0, float4 col
// @1, stride 32). Mọi test thực thi dùng chung scene này.
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

void PresentAndWait(tgles::metal_bridge::MetalBridge& bridge) {
  CAMetalLayer* layer = [CAMetalLayer layer];
  bridge.SetLayer((__bridge void*)layer, 16, 16);
  bridge.Present();
  bridge.WaitForCompletion(bridge.FrameSerial());
}

bool CenterIsRed(tgles::metal_bridge::MetalBridge& bridge) {
  std::uint8_t px[4] = {0};
  if (!bridge.ReadbackPixel(8, 8, px)) return false;
  return px[0] == 255u && px[1] == 0u && px[2] == 0u && px[3] == 255u;
}

// NDC y=0 raster hoá vào hàng pixel 7 trên target 16x16 (hàng 7 phủ [7,8),
// đã đo bằng BlitToCpu), nên họ line/point assert (8,7) thay vì (8,8).
bool CenterRow7IsRed(tgles::metal_bridge::MetalBridge& bridge) {
  std::uint8_t px[4] = {0};
  if (!bridge.ReadbackPixel(8, 7, px)) return false;
  return px[0] == 255u && px[1] == 0u && px[2] == 0u && px[3] == 255u;
}

}  // namespace

TEST(RealExecution, ArrayDrawsRedPixels) {
  auto bridge = tgles::metal_bridge::CreateAppleBridge();
  EXPECT_TRUE(bridge != nullptr);
  if (bridge == nullptr) return;
  if (!bridge->Initialize("Apple3")) return;
  tgles::GlesContext ctx = tgles::GlesContext::Create(false);
  BuildRedScene(ctx);
  EXPECT_TRUE(ctx.RenderFrame(*bridge, tgles::kGlTriangles, 0, 3));
  EXPECT_EQ(ctx.GetError(), tgles::kGlNoError);
  PresentAndWait(*bridge);
  EXPECT_TRUE(CenterIsRed(*bridge));
  EXPECT_EQ(bridge->GetError(), tgles::kGlNoError);
}

TEST(RealExecution, IndexedDrawsRedPixels) {
  auto bridge = tgles::metal_bridge::CreateAppleBridge();
  EXPECT_TRUE(bridge != nullptr);
  if (bridge == nullptr) return;
  if (!bridge->Initialize("Apple3")) return;
  tgles::GlesContext ctx = tgles::GlesContext::Create(false);
  BuildRedScene(ctx);
  // Index buffer 0,1,2 trên VAO hiện tại.
  tgles::GLuint eab = 0;
  ctx.buffers().GenBuffers(1, &eab);
  ctx.buffers().BindBuffer(tgles::kGlElementArrayBuffer, eab);
  static const std::uint8_t kIdx[3] = {0, 1, 2};
  ctx.buffers().BufferData(tgles::kGlElementArrayBuffer, sizeof(kIdx), kIdx,
                           tgles::kGlStaticDraw);
  ctx.vertex_arrays().SetElementArrayBuffer(eab);
  EXPECT_TRUE(ctx.RenderElements(*bridge, tgles::kGlTriangles, 3,
                                 tgles::kGlUnsignedByte, 0));
  EXPECT_EQ(ctx.GetError(), tgles::kGlNoError);
  PresentAndWait(*bridge);
  EXPECT_TRUE(CenterIsRed(*bridge));
}

TEST(RealExecution, InstancedDrawDrawsRedPixels) {
  auto bridge = tgles::metal_bridge::CreateAppleBridge();
  EXPECT_TRUE(bridge != nullptr);
  if (bridge == nullptr) return;
  if (!bridge->Initialize("Apple3")) return;
  tgles::GlesContext ctx = tgles::GlesContext::Create(false);
  BuildRedScene(ctx);
  // CPU expansion: 2 instance của cùng tam giác, divisor 0.
  EXPECT_TRUE(ctx.RenderInstanced(bridge.get(), tgles::kGlTriangles, 0, 3, 2));
  EXPECT_EQ(ctx.GetError(), tgles::kGlNoError);
  PresentAndWait(*bridge);
  EXPECT_TRUE(CenterIsRed(*bridge));
}

TEST(RealExecution, LinesDrawRedCenterPixel) {
  auto bridge = tgles::metal_bridge::CreateAppleBridge();
  EXPECT_TRUE(bridge != nullptr);
  if (bridge == nullptr) return;
  if (!bridge->Initialize("Apple3")) return;
  tgles::GlesContext ctx = tgles::GlesContext::Create(false);
  // Đường ngang giữa màn hình: NDC y=0 -> hàng pixel giữa.
  static const float kPos[6] = {-1, 0, 0, 1, 0, 0};
  static const float kCol[8] = {1, 0, 0, 1, 1, 0, 0, 1};
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
  ctx.textures().TexImage2D(tgles::kGlTexture2d, 0, tgles::kGlRgba8, 16, 16,
                            0, tgles::kGlRgba, tgles::kGlUnsignedByte,
                            nullptr);
  tgles::GLuint f = 0;
  ctx.framebuffers().GenFramebuffers(1, &f);
  ctx.framebuffers().BindFramebuffer(tgles::kGlDrawFramebuffer, f);
  ctx.framebuffers().FramebufferTexture2D(
      tgles::kGlDrawFramebuffer, tgles::kGlColorAttachment0,
      tgles::kGlTexture2d, t, 0);
  EXPECT_TRUE(ctx.RenderFrame(*bridge, tgles::kGlLines, 0, 2));
  EXPECT_EQ(ctx.GetError(), tgles::kGlNoError);
  PresentAndWait(*bridge);
  EXPECT_TRUE(CenterRow7IsRed(*bridge));
}

TEST(RealExecution, PointsDrawRedCenterPixel) {
  auto bridge = tgles::metal_bridge::CreateAppleBridge();
  EXPECT_TRUE(bridge != nullptr);
  if (bridge == nullptr) return;
  if (!bridge->Initialize("Apple3")) return;
  tgles::GlesContext ctx = tgles::GlesContext::Create(false);
  // Hai point quanh tâm (point size 1px): NDC +-0.0625 -> pixel 7 và 8.
  static const float kPos[6] = {-0.0625f, 0, 0, 0.0625f, 0, 0};
  static const float kCol[8] = {1, 0, 0, 1, 1, 0, 0, 1};
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
  ctx.textures().TexImage2D(tgles::kGlTexture2d, 0, tgles::kGlRgba8, 16, 16,
                            0, tgles::kGlRgba, tgles::kGlUnsignedByte,
                            nullptr);
  tgles::GLuint f = 0;
  ctx.framebuffers().GenFramebuffers(1, &f);
  ctx.framebuffers().BindFramebuffer(tgles::kGlDrawFramebuffer, f);
  ctx.framebuffers().FramebufferTexture2D(
      tgles::kGlDrawFramebuffer, tgles::kGlColorAttachment0,
      tgles::kGlTexture2d, t, 0);
  EXPECT_TRUE(ctx.RenderFrame(*bridge, tgles::kGlPoints, 0, 2));
  EXPECT_EQ(ctx.GetError(), tgles::kGlNoError);
  PresentAndWait(*bridge);
  EXPECT_TRUE(CenterRow7IsRed(*bridge));
}

TEST(RealExecution, TriangleStripDrawsRed) {
  auto bridge = tgles::metal_bridge::CreateAppleBridge();
  EXPECT_TRUE(bridge != nullptr);
  if (bridge == nullptr) return;
  if (!bridge->Initialize("Apple3")) return;
  tgles::GlesContext ctx = tgles::GlesContext::Create(false);
  // Strip 4 đỉnh phủ fullscreen (tam giác đầu đã phủ tâm).
  static const float kPos[12] = {-1, -1, 0, 3, -1, 0, -1, 3, 0, 3, 3, 0};
  static const float kCol[16] = {1, 0, 0, 1, 1, 0, 0, 1,
                                 1, 0, 0, 1, 1, 0, 0, 1};
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
  ctx.textures().TexImage2D(tgles::kGlTexture2d, 0, tgles::kGlRgba8, 16, 16,
                            0, tgles::kGlRgba, tgles::kGlUnsignedByte,
                            nullptr);
  tgles::GLuint f = 0;
  ctx.framebuffers().GenFramebuffers(1, &f);
  ctx.framebuffers().BindFramebuffer(tgles::kGlDrawFramebuffer, f);
  ctx.framebuffers().FramebufferTexture2D(
      tgles::kGlDrawFramebuffer, tgles::kGlColorAttachment0,
      tgles::kGlTexture2d, t, 0);
  EXPECT_TRUE(ctx.RenderFrame(*bridge, tgles::kGlTriangleStrip, 0, 4));
  EXPECT_EQ(ctx.GetError(), tgles::kGlNoError);
  PresentAndWait(*bridge);
  EXPECT_TRUE(CenterIsRed(*bridge));
}

TEST(RealExecution, TriangleFanCpuExpandedDrawsRed) {
  auto bridge = tgles::metal_bridge::CreateAppleBridge();
  EXPECT_TRUE(bridge != nullptr);
  if (bridge == nullptr) return;
  if (!bridge->Initialize("Apple3")) return;
  tgles::GlesContext ctx = tgles::GlesContext::Create(false);
  // Fan v0,v1,v2,v3 -> (v0,v1,v2),(v0,v2,v3), phủ fullscreen.
  static const float kPos[12] = {-1, -1, 0, 3, -1, 0, 3, 3, 0, -1, 3, 0};
  static const float kCol[16] = {1, 0, 0, 1, 1, 0, 0, 1,
                                 1, 0, 0, 1, 1, 0, 0, 1};
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
  ctx.textures().TexImage2D(tgles::kGlTexture2d, 0, tgles::kGlRgba8, 16, 16,
                            0, tgles::kGlRgba, tgles::kGlUnsignedByte,
                            nullptr);
  tgles::GLuint f = 0;
  ctx.framebuffers().GenFramebuffers(1, &f);
  ctx.framebuffers().BindFramebuffer(tgles::kGlDrawFramebuffer, f);
  ctx.framebuffers().FramebufferTexture2D(
      tgles::kGlDrawFramebuffer, tgles::kGlColorAttachment0,
      tgles::kGlTexture2d, t, 0);
  EXPECT_TRUE(ctx.RenderFrame(*bridge, tgles::kGlTriangleFan, 0, 4));
  EXPECT_EQ(ctx.GetError(), tgles::kGlNoError);
  PresentAndWait(*bridge);
  EXPECT_TRUE(CenterIsRed(*bridge));
}

TEST(RealExecution, LineStripAndLoopDrawRedCenterPixel) {
  for (tgles::GLenum mode :
       {tgles::kGlLineStrip, tgles::kGlLineLoop}) {
    auto bridge = tgles::metal_bridge::CreateAppleBridge();
    EXPECT_TRUE(bridge != nullptr);
    if (bridge == nullptr) return;
    if (!bridge->Initialize("Apple3")) return;
    tgles::GlesContext ctx = tgles::GlesContext::Create(false);
    static const float kPos[9] = {-1, 0, 0, 1, 0, 0, 1, 1, 0};
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
    EXPECT_TRUE(ctx.RenderFrame(*bridge, mode, 0, 3));
    EXPECT_EQ(ctx.GetError(), tgles::kGlNoError);
    PresentAndWait(*bridge);
    EXPECT_TRUE(CenterRow7IsRed(*bridge));
  }
}

TEST(RealExecution, AdjacencyExpandsPatchesFailClosed) {
  auto bridge = tgles::metal_bridge::CreateAppleBridge();
  EXPECT_TRUE(bridge != nullptr);
  if (bridge == nullptr) return;
  if (!bridge->Initialize("Apple3")) return;
  tgles::GlesContext ctx = tgles::GlesContext::Create(false);
  BuildRedScene(ctx);
  // Adjacency CPU-expand (Phase 4 item 1): 6 verts TRIANGLES_ADJACENCY với
  // 0,2,4 là tam giác fullscreen -> đỏ (pixel proof, không còn fail-closed).
  // BuildRedScene chỉ có 3 verts nên dùng scene 6 verts riêng:
  {
    tgles::GlesContext ctx2 = tgles::GlesContext::Create(false);
    static const float kPos[18] = {-1, -1, 0, 0, 0, 0, 3, -1, 0,
                                   0, 0, 0, -1, 3, 0, 0, 0, 0};
    static const float kCol[24] = {1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1,
                                   1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1};
    tgles::GLuint pb = 0, cb = 0;
    ctx2.buffers().GenBuffers(1, &pb);
    ctx2.buffers().GenBuffers(1, &cb);
    ctx2.buffers().BindBuffer(tgles::kGlArrayBuffer, pb);
    ctx2.buffers().BufferData(tgles::kGlArrayBuffer, sizeof(kPos), kPos,
                              tgles::kGlStaticDraw);
    ctx2.buffers().BindBuffer(tgles::kGlArrayBuffer, cb);
    ctx2.buffers().BufferData(tgles::kGlArrayBuffer, sizeof(kCol), kCol,
                              tgles::kGlStaticDraw);
    tgles::GLuint vao = 0;
    ctx2.vertex_arrays().GenVertexArrays(1, &vao);
    ctx2.vertex_arrays().BindVertexArray(vao);
    ctx2.vertex_arrays().VertexAttribPointer(0, 3, tgles::kGlFloat,
                                             tgles::kGlFalse, 0, 0, pb);
    ctx2.vertex_arrays().VertexAttribPointer(1, 4, tgles::kGlFloat,
                                             tgles::kGlFalse, 0, 0, cb);
    ctx2.vertex_arrays().EnableVertexAttribArray(0);
    ctx2.vertex_arrays().EnableVertexAttribArray(1);
    tgles::GLuint t = 0;
    ctx2.textures().GenTextures(1, &t);
    ctx2.textures().BindTexture(tgles::kGlTexture2d, t);
    ctx2.textures().TexImage2D(tgles::kGlTexture2d, 0, tgles::kGlRgba8, 16, 16,
                               0, tgles::kGlRgba, tgles::kGlUnsignedByte,
                               nullptr);
    tgles::GLuint f = 0;
    ctx2.framebuffers().GenFramebuffers(1, &f);
    ctx2.framebuffers().BindFramebuffer(tgles::kGlDrawFramebuffer, f);
    ctx2.framebuffers().FramebufferTexture2D(
        tgles::kGlDrawFramebuffer, tgles::kGlColorAttachment0,
        tgles::kGlTexture2d, t, 0);
    EXPECT_TRUE(ctx2.RenderFrame(*bridge, 0x000Cu /*TRIANGLES_ADJACENCY*/, 0,
                                 6));
    EXPECT_EQ(ctx2.GetError(), tgles::kGlNoError);
    PresentAndWait(*bridge);
    EXPECT_TRUE(CenterIsRed(*bridge));
  }
  // PATCHES vẫn fail-closed trên Intel (cần mesh Apple7+ Metal3, TGL-DEBUG).
  EXPECT_FALSE(ctx.RenderFrame(*bridge, 0x000Eu /*PATCHES*/, 0, 3));
  EXPECT_EQ(ctx.GetError(), tgles::kGlInvalidOperation);
  EXPECT_EQ(ctx.GetError(), tgles::kGlNoError);
}

TEST(RealExecution, FixedPointAttribsDrawRed) {
  auto bridge = tgles::metal_bridge::CreateAppleBridge();
  EXPECT_TRUE(bridge != nullptr);
  if (bridge == nullptr) return;
  if (!bridge->Initialize("Apple3")) return;
  tgles::GlesContext ctx = tgles::GlesContext::Create(false);
  // Vị trí BYTE normalized (-127->-1.0, 127->+1.0), màu UBYTE normalized.
  // BYTE không biểu diễn được overshoot 3.0 nên dùng TRIANGLE_STRIP 4 đỉnh
  // phủ kín màn hình (hai tam giác phủ toàn bộ target 16x16).
  static const std::int8_t kPos[12] = {-127, -127, 0, 127, -127, 0, -127,
                                       127, 0,   127, 127, 0};
  static const std::uint8_t kCol[16] = {255, 0, 0, 255, 255, 0, 0, 255,
                                        255, 0, 0, 255, 255, 0, 0, 255};
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
  ctx.vertex_arrays().VertexAttribPointer(0, 3, tgles::kGlByte,
                                          tgles::kGlTrue, 0, 0, pb);
  ctx.vertex_arrays().VertexAttribPointer(1, 4, tgles::kGlUnsignedByte,
                                          tgles::kGlTrue, 0, 0, cb);
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
  EXPECT_TRUE(ctx.RenderFrame(*bridge, tgles::kGlTriangleStrip, 0, 4));
  EXPECT_EQ(ctx.GetError(), tgles::kGlNoError);
  PresentAndWait(*bridge);
  EXPECT_TRUE(CenterIsRed(*bridge));
}

TEST(RealExecution, IntegerAttribsConvertExactly) {
  auto bridge = tgles::metal_bridge::CreateAppleBridge();
  EXPECT_TRUE(bridge != nullptr);
  if (bridge == nullptr) return;
  if (!bridge->Initialize("Apple3")) return;
  tgles::GlesContext ctx = tgles::GlesContext::Create(false);
  BuildRedScene(ctx);
  // Attrib integer thuần (IPointer) convert sang float trên CPU (chính xác
  // dưới 2^24; bitwise ops trong shader vẫn fail-closed ở translator).
  // Vị trí INT (-1,-1,0),(3,-1,0),(-1,3,0) -> tam giác fullscreen đỏ.
  tgles::GLuint ib = 0;
  ctx.buffers().GenBuffers(1, &ib);
  static const std::int32_t kPos[9] = {-1, -1, 0, 3, -1, 0, -1, 3, 0};
  ctx.buffers().BindBuffer(tgles::kGlArrayBuffer, ib);
  ctx.buffers().BufferData(tgles::kGlArrayBuffer, sizeof(kPos), kPos,
                           tgles::kGlStaticDraw);
  ctx.vertex_arrays().VertexAttribIPointer(0, 3, tgles::kGlInt, 0, 0, ib);
  EXPECT_TRUE(ctx.RenderFrame(*bridge, tgles::kGlTriangles, 0, 3));
  EXPECT_EQ(ctx.GetError(), tgles::kGlNoError);
  PresentAndWait(*bridge);
  EXPECT_TRUE(CenterIsRed(*bridge));
}

TEST(RealExecution, DivisorStepsPerInstance) {
  auto bridge = tgles::metal_bridge::CreateAppleBridge();
  EXPECT_TRUE(bridge != nullptr);
  if (bridge == nullptr) return;
  if (!bridge->Initialize("Apple3")) return;
  tgles::GlesContext ctx = tgles::GlesContext::Create(false);
  // Vị trí per-vertex (divisor 0), màu per-instance (divisor 1):
  // instance 0 đỏ, instance 1 xanh lá. Instance sau đè lên trên (không depth)
  // nên pixel tâm phải xanh lá — chứng minh stepping thật, không phải bỏ qua.
  static const float kPos[9] = {-1, -1, 0, 3, -1, 0, -1, 3, 0};
  static const float kCol[8] = {1, 0, 0, 1, 0, 1, 0, 1};
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
  ctx.vertex_arrays().VertexAttribDivisor(1, 1);
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
  EXPECT_TRUE(ctx.RenderInstanced(bridge.get(), tgles::kGlTriangles, 0, 3,
                                  2));
  EXPECT_EQ(ctx.GetError(), tgles::kGlNoError);
  PresentAndWait(*bridge);
  std::uint8_t px[4] = {0};
  EXPECT_TRUE(bridge->ReadbackPixel(8, 8, px));
  EXPECT_EQ(px[0], 0u);
  EXPECT_EQ(px[1], 255u);
  EXPECT_EQ(px[2], 0u);
  EXPECT_EQ(px[3], 255u);
}

TEST(RealExecution, BaseVertexDrawsRedPixelsViaHost) {
  auto bridge = tgles::metal_bridge::CreateAppleBridge();
  EXPECT_TRUE(bridge != nullptr);
  if (bridge == nullptr) return;
  if (!bridge->Initialize("Apple3")) return;
  // Đi qua C ABI thật + HostRuntime thật: CPU-side index rebasing trong
  // facade, rồi shared submit path (không còn fail-closed).
  EGLDisplay dpy = eglGetDisplay(nullptr);
  if (dpy == nullptr) return;
  if (eglInitialize(dpy, nullptr, nullptr) == 0) return;
  EGLint cfg_a[] = {0x3038};
  EGLConfig cfg = nullptr;
  EGLint n = 0;
  if (eglChooseConfig(dpy, cfg_a, &cfg, 1, &n) == 0) return;
  EGLint ctx_a[] = {0x3098, 3, 0x30FB, 2, 0x3038};
  EGLContext c = eglCreateContext(dpy, cfg, nullptr, ctx_a);
  if (c == nullptr) return;
  EGLint pb_a[] = {0x3057, 16, 0x3056, 16, 0x3038};
  EGLSurface s = eglCreatePbufferSurface(dpy, cfg, pb_a);
  if (s == nullptr) return;
  if (eglMakeCurrent(dpy, s, s, c) == 0) return;
  // Hai tam giác đỏ fullscreen liên tiếp (verts 0-2 và 3-5).
  static const float kPos[18] = {-1, -1, 0, 3, -1, 0, -1, 3, 0,
                                 -1, -1, 0, 3, -1, 0, -1, 3, 0};
  static const float kCol[24] = {1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1,
                                 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1};
  GLuint pb_id = 0, cb_id = 0;
  glGenBuffers(1, &pb_id);
  glGenBuffers(1, &cb_id);
  glBindBuffer(0x8892 /*ARRAY_BUFFER*/, pb_id);
  glBufferData(0x8892, sizeof(kPos), kPos, 0x88E4 /*STATIC_DRAW*/);
  glBindBuffer(0x8892, cb_id);
  glBufferData(0x8892, sizeof(kCol), kCol, 0x88E4);
  GLuint vao = 0;
  glGenVertexArrays(1, &vao);
  glBindVertexArray(vao);
  glBindBuffer(0x8892, pb_id);
  glVertexAttribPointer(0, 3, 0x1406 /*FLOAT*/, 0, 0, nullptr);
  glBindBuffer(0x8892, cb_id);
  glVertexAttribPointer(1, 4, 0x1406, 0, 0, nullptr);
  glEnableVertexAttribArray(0);
  glEnableVertexAttribArray(1);
  GLuint eab = 0;
  glGenBuffers(1, &eab);
  glBindBuffer(0x8893 /*ELEMENT_ARRAY_BUFFER*/, eab);
  static const std::uint8_t kIdx[3] = {0, 1, 2};
  glBufferData(0x8893, sizeof(kIdx), kIdx, 0x88E4);
  GLuint tex = 0, fbo = 0;
  glGenTextures(1, &tex);
  glBindTexture(0x0DE1 /*TEXTURE_2D*/, tex);
  glTexImage2D(0x0DE1, 0, 0x8058 /*RGBA8*/, 16, 16, 0, 0x1908 /*RGBA*/,
               0x1401 /*UNSIGNED_BYTE*/, nullptr);
  glGenFramebuffers(1, &fbo);
  glBindFramebuffer(0x8CA9 /*FRAMEBUFFER*/, fbo);
  glFramebufferTexture2D(0x8CA9, 0x8CE0 /*COLOR_ATTACHMENT0*/, 0x0DE1, tex,
                         0);
  EXPECT_EQ(glGetError(), tgles::kGlNoError);
  tgles::HostRuntime::Instance().SetMetalBridge(bridge.get());
  // indices {0,1,2} + base 3 -> verts {3,4,5} = tam giác đỏ thứ hai.
  glDrawElementsBaseVertex(0x0004 /*TRIANGLES*/, 3, 0x1401 /*UBYTE*/,
                           nullptr, 3);
  EXPECT_EQ(glGetError(), tgles::kGlNoError);
  tgles::HostRuntime::Instance().SetMetalBridge(nullptr);
  PresentAndWait(*bridge);
  EXPECT_TRUE(CenterIsRed(*bridge));
  eglDestroySurface(dpy, s);
  eglTerminate(dpy);
}

TEST(RealExecution, StencilNeverBlocksDraw) {
  auto bridge = tgles::metal_bridge::CreateAppleBridge();
  EXPECT_TRUE(bridge != nullptr);
  if (bridge == nullptr) return;
  if (!bridge->Initialize("Apple3")) return;
  tgles::GlesContext ctx = tgles::GlesContext::Create(false);
  BuildRedScene(ctx);
  tgles::GLuint rb = 0;
  ctx.renderbuffers().GenRenderbuffers(1, &rb);
  ctx.renderbuffers().BindRenderbuffer(tgles::kGlRenderbuffer, rb);
  ctx.renderbuffers().RenderbufferStorage(tgles::kGlRenderbuffer,
                                          tgles::kGlDepth24Stencil8, 16, 16);
  ctx.framebuffers().FramebufferRenderbuffer(
      tgles::kGlDrawFramebuffer, tgles::kGlDepthStencilAttachment,
      tgles::kGlRenderbuffer, rb);
  // Stencil NEVER: gọi hợp lệ nhưng mọi fragment bị loại — màn hình đen,
  // không lỗi (đây mới là thực thi, không còn là validate-only).
  ctx.foundation().Enable(tgles::kGlStencilTest);
  ctx.raster().StencilFunc(tgles::kGlNever, 0, 0xFFu);
  EXPECT_EQ(ctx.raster().GetError(), tgles::kGlNoError);
  EXPECT_TRUE(ctx.RenderFrame(*bridge, tgles::kGlTriangles, 0, 3));
  EXPECT_EQ(ctx.GetError(), tgles::kGlNoError);
  EXPECT_TRUE(bridge->StencilEnabled());
  PresentAndWait(*bridge);
  std::uint8_t px[4] = {0};
  EXPECT_TRUE(bridge->ReadbackPixel(8, 8, px));
  EXPECT_EQ(px[0], 0u);
  EXPECT_EQ(px[1], 0u);
  EXPECT_EQ(px[2], 0u);
  EXPECT_EQ(px[3], 255u);
}

TEST(RealExecution, StencilAlwaysWithReplaceDrawsRed) {
  auto bridge = tgles::metal_bridge::CreateAppleBridge();
  EXPECT_TRUE(bridge != nullptr);
  if (bridge == nullptr) return;
  if (!bridge->Initialize("Apple3")) return;
  tgles::GlesContext ctx = tgles::GlesContext::Create(false);
  BuildRedScene(ctx);
  tgles::GLuint rb = 0;
  ctx.renderbuffers().GenRenderbuffers(1, &rb);
  ctx.renderbuffers().BindRenderbuffer(tgles::kGlRenderbuffer, rb);
  ctx.renderbuffers().RenderbufferStorage(tgles::kGlRenderbuffer,
                                          tgles::kGlDepth24Stencil8, 16, 16);
  ctx.framebuffers().FramebufferRenderbuffer(
      tgles::kGlDrawFramebuffer, tgles::kGlDepthStencilAttachment,
      tgles::kGlRenderbuffer, rb);
  // ALWAYS + REPLACE ref=1: vẽ đỏ, đồng thời ghi stencil (op biên dịch thật
  // sang MTLStencilOperation).
  ctx.foundation().Enable(tgles::kGlStencilTest);
  ctx.raster().StencilFunc(tgles::kGlAlways, 1, 0xFFu);
  ctx.raster().StencilOp(tgles::kGlKeep, tgles::kGlKeep, tgles::kGlReplace);
  EXPECT_EQ(ctx.raster().GetError(), tgles::kGlNoError);
  EXPECT_TRUE(ctx.RenderFrame(*bridge, tgles::kGlTriangles, 0, 3));
  EXPECT_EQ(ctx.GetError(), tgles::kGlNoError);
  PresentAndWait(*bridge);
  EXPECT_TRUE(CenterIsRed(*bridge));
}

TEST(RealExecution, StencilSplitRefsExecuteTwoPass) {
  auto bridge = tgles::metal_bridge::CreateAppleBridge();
  EXPECT_TRUE(bridge != nullptr);
  if (bridge == nullptr) return;
  if (!bridge->Initialize("Apple3")) return;
  tgles::GlesContext ctx = tgles::GlesContext::Create(false);
  BuildRedScene(ctx);
  tgles::GLuint rb = 0;
  ctx.renderbuffers().GenRenderbuffers(1, &rb);
  ctx.renderbuffers().BindRenderbuffer(tgles::kGlRenderbuffer, rb);
  ctx.renderbuffers().RenderbufferStorage(tgles::kGlRenderbuffer,
                                          tgles::kGlDepth24Stencil8, 16, 16);
  ctx.framebuffers().FramebufferRenderbuffer(
      tgles::kGlDrawFramebuffer, tgles::kGlDepthStencilAttachment,
      tgles::kGlRenderbuffer, rb);
  // Encoder Metal chỉ có MỘT ref value (SDK 26.2) nên facade vẽ hai pass
  // trong cùng frame (back faces + back state, rồi front faces + front
  // state, không clear giữa chừng). Tam giác fullscreen CCW là front nên
  // pass 2 vẽ đỏ.
  ctx.foundation().Enable(tgles::kGlStencilTest);
  ctx.raster().StencilFuncSeparate(tgles::kGlFront, tgles::kGlAlways, 1,
                                   0xFFu);
  ctx.raster().StencilFuncSeparate(tgles::kGlBackFace, tgles::kGlAlways, 2,
                                   0xFFu);
  EXPECT_EQ(ctx.raster().GetError(), tgles::kGlNoError);
  EXPECT_TRUE(ctx.RenderFrame(*bridge, tgles::kGlTriangles, 0, 3));
  EXPECT_EQ(ctx.GetError(), tgles::kGlNoError);
  PresentAndWait(*bridge);
  EXPECT_TRUE(CenterIsRed(*bridge));
}

TEST(RealExecution, DrawBuffersSingleExecutesMultiValidated) {
  auto bridge = tgles::metal_bridge::CreateAppleBridge();
  EXPECT_TRUE(bridge != nullptr);
  if (bridge == nullptr) return;
  if (!bridge->Initialize("Apple3")) return;
  tgles::GlesContext ctx = tgles::GlesContext::Create(false);
  BuildRedScene(ctx);
  // n=1 thực thi; n=4 validate ok ở tầng state nhưng bridge chỉ dùng
  // COLOR_ATTACHMENT0 (không MRT) — vẽ vẫn đỏ attachment 0.
  const tgles::GLenum one[1] = {tgles::kGlColorAttachment0};
  ctx.framebuffers().DrawBuffers(1, one);
  EXPECT_EQ(ctx.framebuffers().GetError(), tgles::kGlNoError);
  const tgles::GLenum four[4] = {
      tgles::kGlColorAttachment0,
      tgles::kGlColorAttachment0 + 1u,  // COLOR_ATTACHMENT1 (0x8CE1)
      tgles::kGlColorAttachment0 + 2u,  // COLOR_ATTACHMENT2 (0x8CE2)
      tgles::kGlColorAttachment0 + 3u};  // COLOR_ATTACHMENT3 (0x8CE3)
  ctx.framebuffers().DrawBuffers(4, four);
  EXPECT_EQ(ctx.framebuffers().GetError(), tgles::kGlNoError);
  EXPECT_TRUE(ctx.RenderFrame(*bridge, tgles::kGlTriangles, 0, 3));
  PresentAndWait(*bridge);
  EXPECT_TRUE(CenterIsRed(*bridge));
}

TEST(RealExecution, GlDepthSemanticsNegativeNearWins) {
  auto bridge = tgles::metal_bridge::CreateAppleBridge();
  EXPECT_TRUE(bridge != nullptr);
  if (bridge == nullptr) return;
  if (!bridge->Initialize("Apple3")) return;
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
  tgles::GLuint t = 0;
  ctx.textures().GenTextures(1, &t);
  ctx.textures().BindTexture(tgles::kGlTexture2d, t);
  ctx.textures().TexImage2D(tgles::kGlTexture2d, 0, tgles::kGlRgba8, 16, 16,
                            0, tgles::kGlRgba, tgles::kGlUnsignedByte,
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
  ctx.foundation().Enable(tgles::kGlDepthTest);  // LESS mặc định.
  // GL semantics: z=-0.5 (gần) đỏ vẽ TRƯỚC, z=+0.5 (xa) xanh vẽ SAU.
  // Không remap, -0.5 đã bị clip và xanh thắng; có remap, đỏ thắng bằng LESS.
  const float pos[18] = {-1, -1, -0.5f, 3, -1, -0.5f, -1, 3, -0.5f,
                         -1, -1, 0.5f,  3, -1, 0.5f,  -1, 3, 0.5f};
  const float col[24] = {1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1,
                         0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1};
  ctx.buffers().BindBuffer(tgles::kGlArrayBuffer, pb);
  ctx.buffers().BufferData(tgles::kGlArrayBuffer, sizeof(pos), pos,
                           tgles::kGlStaticDraw);
  ctx.buffers().BindBuffer(tgles::kGlArrayBuffer, cb);
  ctx.buffers().BufferData(tgles::kGlArrayBuffer, sizeof(col), col,
                           tgles::kGlStaticDraw);
  EXPECT_TRUE(ctx.RenderFrame(*bridge, tgles::kGlTriangles, 0, 6));
  EXPECT_TRUE(bridge->DepthEnabled());
  PresentAndWait(*bridge);
  std::uint8_t px[4] = {0};
  EXPECT_TRUE(bridge->ReadbackPixel(8, 8, px));
  EXPECT_EQ(px[0], 255u);
  EXPECT_EQ(px[1], 0u);
  EXPECT_EQ(px[2], 0u);
  EXPECT_EQ(px[3], 255u);
  EXPECT_EQ(bridge->GetError(), tgles::kGlNoError);
  EXPECT_EQ(ctx.GetError(), tgles::kGlNoError);
}

TEST(RealExecution, MultiDrawIndirectDrawsRed) {
  auto bridge = tgles::metal_bridge::CreateAppleBridge();
  EXPECT_TRUE(bridge != nullptr);
  if (bridge == nullptr) return;
  if (!bridge->Initialize("Apple3")) return;
  tgles::GlesContext ctx = tgles::GlesContext::Create(false);
  BuildRedScene(ctx);
  tgles::GLuint ibuf = 0;
  ctx.buffers().GenBuffers(1, &ibuf);
  ctx.buffers().BindBuffer(tgles::kGlDrawIndirectBuffer, ibuf);
  static const std::uint32_t kCmd[8] = {3, 1, 0, 0, 3, 1, 0, 0};
  ctx.buffers().BufferData(tgles::kGlDrawIndirectBuffer, sizeof(kCmd), kCmd,
                           tgles::kGlStaticDraw);
  ctx.SyncIndirectState();
  EXPECT_TRUE(ctx.MultiDrawArraysIndirect(bridge.get(), tgles::kGlTriangles,
                                          0, 2, 0));
  EXPECT_EQ(ctx.GetError(), tgles::kGlNoError);
  PresentAndWait(*bridge);
  EXPECT_TRUE(CenterIsRed(*bridge));
}

TEST(RealExecution, BaseInstanceShiftsColorToGreen) {
  auto bridge = tgles::metal_bridge::CreateAppleBridge();
  EXPECT_TRUE(bridge != nullptr);
  if (bridge == nullptr) return;
  if (!bridge->Initialize("Apple3")) return;
  tgles::GlesContext ctx = tgles::GlesContext::Create(false);
  // Position per-vertex, color per-instance [red, green] with divisor 1:
  // baseinstance 1 selects id 1 (green). Without the shift id 0 (red) wins.
  static const float kPos[9] = {-1, -1, 0, 3, -1, 0, -1, 3, 0};
  static const float kCol[8] = {1, 0, 0, 1, 0, 1, 0, 1};
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
  ctx.vertex_arrays().VertexAttribDivisor(1, 1);
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
  EXPECT_TRUE(ctx.RenderArraysInstancedBaseInstance(
      bridge.get(), tgles::kGlTriangles, 0, 3, 1, 1));
  EXPECT_EQ(ctx.GetError(), tgles::kGlNoError);
  PresentAndWait(*bridge);
  std::uint8_t px[4] = {0};
  EXPECT_TRUE(bridge->ReadbackPixel(8, 8, px));
  EXPECT_EQ(px[0], 0u);
  EXPECT_EQ(px[1], 255u);
  EXPECT_EQ(px[2], 0u);
  EXPECT_EQ(px[3], 255u);
}

TEST(RealExecution, ViewAttachedFboDrawsRed) {
  auto bridge = tgles::metal_bridge::CreateAppleBridge();
  EXPECT_TRUE(bridge != nullptr);
  if (bridge == nullptr) return;
  if (!bridge->Initialize("Apple3")) return;
  tgles::GlesContext ctx = tgles::GlesContext::Create(false);
  BuildRedScene(ctx);
  // Immutable original + sRGB view; the DRAW FBO hangs off the view, so the
  // frame sizes from aliased storage and the GPU still draws red.
  tgles::GLuint orig = 0, view = 0;
  ctx.textures().GenTextures(1, &orig);
  ctx.textures().GenTextures(1, &view);
  ctx.textures().BindTexture(tgles::kGlTexture2d, orig);
  ctx.textures().TexStorage2D(tgles::kGlTexture2d, 1, tgles::kGlRgba8, 16,
                              16);
  ctx.textures().TextureView(view, tgles::kGlTexture2d, orig,
                             tgles::kGlSrgb8Alpha8, 0, 1, 0, 1);
  EXPECT_EQ(ctx.textures().GetError(), tgles::kGlNoError);
  tgles::GLuint f = 0;
  ctx.framebuffers().GenFramebuffers(1, &f);
  ctx.framebuffers().BindFramebuffer(tgles::kGlDrawFramebuffer, f);
  ctx.framebuffers().FramebufferTexture2D(
      tgles::kGlDrawFramebuffer, tgles::kGlColorAttachment0,
      tgles::kGlTexture2d, view, 0);
  EXPECT_TRUE(ctx.RenderFrame(*bridge, tgles::kGlTriangles, 0, 3));
  EXPECT_EQ(ctx.GetError(), tgles::kGlNoError);
  PresentAndWait(*bridge);
  EXPECT_TRUE(CenterIsRed(*bridge));
}

TEST(RealExecution, MappingHonestyPinsExecutedVsEmulated) {
  using tgles::metal::MappingKind;
  // Thực thi pixel thật ở trên (kể cả base-vertex: CPU index rebasing).
  for (const char* n :
       {"glDrawArrays", "glDrawElements", "glDrawArraysInstanced",
        "glDrawElementsInstanced", "glDrawArraysIndirect",
        "glDrawElementsIndirect", "glDrawRangeElements",
        "glDrawElementsBaseVertex", "glDrawRangeElementsBaseVertex",
        "glDrawElementsInstancedBaseVertex", "glStencilFunc",
        "glStencilFuncSeparate", "glStencilMask", "glStencilMaskSeparate",
        "glStencilOp", "glStencilOpSeparate"}) {
    const auto* m = tgles::metal::FindMapping(n);
    EXPECT_TRUE(m != nullptr);
    if (m != nullptr) EXPECT_TRUE(m->kind == MappingKind::kDirect);
  }
  // Validate-only thật ở trên -> phải là Emulated.
  for (const char* n :
       {"glDrawBuffers", "glReadBuffer", "glBlitFramebuffer"}) {
    const auto* m = tgles::metal::FindMapping(n);
    EXPECT_TRUE(m != nullptr);
    if (m != nullptr) EXPECT_TRUE(m->kind == MappingKind::kEmulated);
  }
}

#else

// Không có Metal/Quartz (CI Linux): file vẫn biên dịch, không test gì.
TEST(RealExecution, NoMetalHostSkips) { EXPECT_TRUE(true); }

#endif
