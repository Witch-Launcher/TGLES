// PBO / stale-validator regression (Minecraft 26.1.2 black screen + SIGSEGV).
//
// Why: MobileGL stages mipmap regen into PIXEL_UNPACK_BUFFER and passes a
// byte offset as `pixels`. Treating that offset as a host pointer crashed
// UnpackToRgba8 (SIGSEGV at low si_addr). Separately, one early
// FlagBridgeError latched draw_ INVALID_OPERATION until GetError(), and
// RenderFrame/RenderElements rejected EVERY subsequent valid draw via
// HasPending() — leaving only clear (black present, EGL_BAD_ALLOC).
// GL: a pending error is sticky for glGetError but must not block later
// valid commands (ES 3.2 spec 2.3.1).

#include "test_framework.h"

#include <cstdint>
#include <vector>

#include "tgles/facade/gles.h"
#include "tgles/gpu/metal_bridge.h"

namespace {

void BuildRedTriangle(tgles::GlesContext& ctx) {
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
  ctx.framebuffers().FramebufferTexture2D(tgles::kGlDrawFramebuffer,
                                          tgles::kGlColorAttachment0,
                                          tgles::kGlTexture2d, t, 0);
}

}  // namespace

// --- PBO resolve: offset must become a host pointer, never be dereferenced
// as-is. In-range offset unpacks; OOB records INVALID_OPERATION and skips.

TEST(PboTexelSource, NoPboPassesClientPointerThrough) {
  tgles::GlesContext ctx = tgles::GlesContext::Create(false);
  const std::uint8_t host[4] = {9, 8, 7, 6};
  const void* out = nullptr;
  EXPECT_TRUE(ctx.ResolveTexelSource(host, 1, 1, 1, tgles::kGlRgba,
                                     tgles::kGlUnsignedByte, &out));
  EXPECT_TRUE(out == host);
  EXPECT_EQ(ctx.GetError(), tgles::kGlNoError);
}

TEST(PboTexelSource, InRangeOffsetResolvesToStoreBase) {
  tgles::GlesContext ctx = tgles::GlesContext::Create(false);
  std::vector<std::uint8_t> store(128, 0xAB);
  tgles::GLuint pbo = 0;
  ctx.buffers().GenBuffers(1, &pbo);
  ctx.buffers().BindBuffer(tgles::kGlPixelUnpackBuffer, pbo);
  ctx.buffers().BufferData(tgles::kGlPixelUnpackBuffer,
                           static_cast<tgles::GLsizeiptr>(store.size()),
                           store.data(), tgles::kGlStaticDraw);
  EXPECT_EQ(ctx.buffers().BoundBuffer(tgles::kGlPixelUnpackBuffer), pbo);
  // MobileGL's UnpackRingPixelOffset(0xea80) style: non-null "pointer".
  // 4x4 RGBA = 64 bytes; offset 16 + 64 = 80 <= 128.
  const void* out = nullptr;
  const void* offset16 = reinterpret_cast<const void*>(static_cast<
      std::uintptr_t>(16));
  EXPECT_TRUE(ctx.ResolveTexelSource(offset16, 4, 4, 1, tgles::kGlRgba,
                                     tgles::kGlUnsignedByte, &out));
  const std::uint8_t* base = nullptr;
  tgles::GLsizeiptr size = 0;
  EXPECT_TRUE(ctx.buffers().GetBufferBytes(pbo, &base, &size));
  EXPECT_TRUE(out == base + 16);
  EXPECT_EQ(size, 128);
  EXPECT_EQ(ctx.GetError(), tgles::kGlNoError);
}

TEST(PboTexelSource, NullPixelsWithPboIsOffsetZero) {
  tgles::GlesContext ctx = tgles::GlesContext::Create(false);
  std::vector<std::uint8_t> store(32, 1);
  tgles::GLuint pbo = 0;
  ctx.buffers().GenBuffers(1, &pbo);
  ctx.buffers().BindBuffer(tgles::kGlPixelUnpackBuffer, pbo);
  ctx.buffers().BufferData(tgles::kGlPixelUnpackBuffer,
                           static_cast<tgles::GLsizeiptr>(store.size()),
                           store.data(), tgles::kGlStaticDraw);
  const void* out = nullptr;
  EXPECT_TRUE(ctx.ResolveTexelSource(nullptr, 2, 2, 1, tgles::kGlRgba,
                                     tgles::kGlUnsignedByte, &out));
  const std::uint8_t* base = nullptr;
  tgles::GLsizeiptr size = 0;
  EXPECT_TRUE(ctx.buffers().GetBufferBytes(pbo, &base, &size));
  EXPECT_TRUE(out == base);
  EXPECT_EQ(ctx.GetError(), tgles::kGlNoError);
}

TEST(PboTexelSource, OutOfRangeOffsetRecordsInvalidOperation) {
  tgles::GlesContext ctx = tgles::GlesContext::Create(false);
  std::vector<std::uint8_t> store(32, 0);
  tgles::GLuint pbo = 0;
  ctx.buffers().GenBuffers(1, &pbo);
  ctx.buffers().BindBuffer(tgles::kGlPixelUnpackBuffer, pbo);
  ctx.buffers().BufferData(tgles::kGlPixelUnpackBuffer,
                           static_cast<tgles::GLsizeiptr>(store.size()),
                           store.data(), tgles::kGlStaticDraw);
  // 0xea80 was the crashing ring offset against a tiny/empty store in one
  // path; any OOB must fail closed instead of reading host address 0xea80.
  const void* oob = reinterpret_cast<const void*>(static_cast<
      std::uintptr_t>(0xea80));
  const void* out = nullptr;
  EXPECT_FALSE(ctx.ResolveTexelSource(oob, 8, 8, 1, tgles::kGlRgba,
                                      tgles::kGlUnsignedByte, &out));
  EXPECT_EQ(ctx.GetError(), tgles::kGlInvalidOperation);
}

TEST(PboTexelSource, OffsetPlusNeedPastEndFailsEvenIfOffsetInStore) {
  tgles::GlesContext ctx = tgles::GlesContext::Create(false);
  std::vector<std::uint8_t> store(32, 0);
  tgles::GLuint pbo = 0;
  ctx.buffers().GenBuffers(1, &pbo);
  ctx.buffers().BindBuffer(tgles::kGlPixelUnpackBuffer, pbo);
  ctx.buffers().BufferData(tgles::kGlPixelUnpackBuffer,
                           static_cast<tgles::GLsizeiptr>(store.size()),
                           store.data(), tgles::kGlStaticDraw);
  // offset 16 + need 4*4*4=64 > 32.
  const void* offset16 = reinterpret_cast<const void*>(static_cast<
      std::uintptr_t>(16));
  const void* out = nullptr;
  EXPECT_FALSE(ctx.ResolveTexelSource(offset16, 4, 4, 1, tgles::kGlRgba,
                                      tgles::kGlUnsignedByte, &out));
  EXPECT_EQ(ctx.GetError(), tgles::kGlInvalidOperation);
}

// --- Stale FlagBridgeError must not reject later valid draws.

TEST(StaleValidator, DrawValidationReturnIgnoresPendingFromPriorError) {
  tgles::DrawValidator d;
  d.FlagBridgeError();  // Latches INVALID_OPERATION (first-wins).
  EXPECT_TRUE(d.HasPending());
  // This call's validation is independent of the stale queue entry.
  EXPECT_TRUE(d.DrawArrays(tgles::kGlTriangles, 0, 3));
  EXPECT_TRUE(d.HasPending());  // Still pending until GetError.
  EXPECT_EQ(d.GetError(), tgles::kGlInvalidOperation);
  EXPECT_EQ(d.GetError(), tgles::kGlNoError);
  EXPECT_TRUE(d.DrawArrays(tgles::kGlTriangles, 0, 3));
}

TEST(StaleValidator, InvalidDrawStillFailsWhilePending) {
  tgles::DrawValidator d;
  d.FlagBridgeError();
  EXPECT_FALSE(d.DrawArrays(0x9999u, 0, 3));  // Bad mode: this call fails.
  EXPECT_TRUE(d.HasPending());
}

TEST(StaleValidator, RenderFrameRunsAfterStaleBridgeError) {
  tgles::GlesContext ctx = tgles::GlesContext::Create(false);
  BuildRedTriangle(ctx);
  tgles::metal_bridge::MockMetalBridge bridge;
  EXPECT_TRUE(bridge.Initialize("Apple3"));
  // First draw fails closed (no program / whatever) and latches.
  ctx.draw().FlagBridgeError();
  EXPECT_EQ(ctx.GetError(), tgles::kGlInvalidOperation);
  // Drain would clear; WITHOUT draining, a second valid triangle must still
  // execute: the gate is this call's validation, not HasPending().
  // Re-flag without GetError to simulate an undrained mid-frame error.
  // FlagBridgeError is first-wins so re-record is a no-op while pending —
  // the point is HasPending() is still true.
  ctx.draw().FlagBridgeError();
  EXPECT_TRUE(ctx.draw().HasPending());
  const auto before = bridge.DrawCount();
  EXPECT_TRUE(ctx.RenderFrame(bridge, tgles::kGlTriangles, 0, 3));
  EXPECT_TRUE(bridge.DrawCount() > before);
}

TEST(StaleValidator, InvalidModeStillRejectedWithLogPath) {
  tgles::GlesContext ctx = tgles::GlesContext::Create(false);
  BuildRedTriangle(ctx);
  tgles::metal_bridge::MockMetalBridge bridge;
  EXPECT_TRUE(bridge.Initialize("Apple3"));
  const auto before = bridge.DrawCount();
  EXPECT_FALSE(ctx.RenderFrame(bridge, 0x9999u, 0, 3));
  EXPECT_EQ(bridge.DrawCount(), before);
  EXPECT_EQ(ctx.GetError(), tgles::kGlInvalidEnum);
}
