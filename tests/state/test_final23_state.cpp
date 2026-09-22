// Type-1 final23: manager-level unit tests for the last 23 gaps.
// Mirrors test_final23_abi.cpp at state level.

#include "test_framework.h"

#include "tgles/facade/gles.h"
#include "tgles/pipeline/raster.h"
#include "tgles/state/context.h"
#include "tgles/state/framebuffer.h"
#include "tgles/state/texture.h"

TEST(Final23State, Compressed) {
  tgles::TextureManager tm;
  tgles::GLuint id[1] = {0};
  tm.GenTextures(1, id);
  tm.BindTexture(tgles::kGlTexture2d, id[0]);
  unsigned char blk[8] = {};
  tm.CompressedTexImage2D(tgles::kGlTexture2d, 0, tgles::kGlEtc2Rgb8, 4, 4, 0,
                          8, blk);
  EXPECT_EQ(tm.GetError(), tgles::kGlNoError);
  tm.CompressedTexImage2D(tgles::kGlTexture2d, 0, 0x9999u, 4, 4, 0, 8, blk);
  EXPECT_EQ(tm.GetError(), tgles::kGlInvalidEnum);
}

TEST(Final23State, IndexedCaps) {
  tgles::RasterState rs;
  rs.EnableIndexed(0x0BE2u, 0);
  EXPECT_EQ(rs.GetError(), tgles::kGlNoError);
  EXPECT_TRUE(rs.IsEnabledIndexed(0x0BE2u, 0) == tgles::kGlTrue);
  rs.DisableIndexed(0x0BE2u, 0);
  EXPECT_TRUE(rs.IsEnabledIndexed(0x0BE2u, 0) == tgles::kGlFalse);
  rs.EnableIndexed(0x9999u, 0);
  EXPECT_EQ(rs.GetError(), tgles::kGlInvalidEnum);
  rs.EnableIndexed(0x0BE2u, 99u);
  EXPECT_EQ(rs.GetError(), tgles::kGlInvalidValue);
}

TEST(Final23State, RasterMisc) {
  tgles::RasterState rs;
  rs.LogicOp(0x1503u);
  EXPECT_EQ(rs.GetError(), tgles::kGlNoError);
  rs.LogicOp(0x9999u);
  EXPECT_EQ(rs.GetError(), tgles::kGlInvalidEnum);
  rs.PointSize(2.0f);
  EXPECT_EQ(rs.GetError(), tgles::kGlNoError);
  rs.PointSize(0.0f);
  EXPECT_EQ(rs.GetError(), tgles::kGlInvalidValue);
  rs.BlendBarrier();
  EXPECT_EQ(rs.GetError(), tgles::kGlNoError);
  rs.MinSampleShading(0.5f);
  EXPECT_EQ(rs.GetError(), tgles::kGlNoError);
  rs.MinSampleShading(2.0f);
  EXPECT_EQ(rs.GetError(), tgles::kGlInvalidValue);
}

TEST(Final23State, FramebufferTexture) {
  tgles::TextureManager tm;
  tgles::RenderbufferManager rb;
  tgles::FramebufferManager fb(&tm, &rb);
  tgles::GLuint t[1] = {0}, f[1] = {0};
  tm.GenTextures(1, t);
  tm.BindTexture(tgles::kGlTexture2d, t[0]);
  tm.TexStorage2D(tgles::kGlTexture2d, 1, tgles::kGlRgba8, 8, 8);
  fb.GenFramebuffers(1, f);
  fb.BindFramebuffer(tgles::kGlFramebuffer, f[0]);
  fb.FramebufferTexture(tgles::kGlFramebuffer, tgles::kGlColorAttachment0,
                        t[0], 0);
  EXPECT_EQ(fb.GetError(), tgles::kGlNoError);
  fb.InvalidateSubFramebuffer(tgles::kGlFramebuffer, 0, nullptr, 0, 0, 4, 4);
  // numAttachments 0 with null is NO_ERROR (nothing to invalidate).
  EXPECT_EQ(fb.GetError(), tgles::kGlNoError);
}
