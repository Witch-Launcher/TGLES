// Step 8 tests: draw validation (spec 10.5) and raster/per-fragment state
// defaults (spec ch.13-15).

#include "test_framework.h"

#include "tgles/pipeline/draw.h"
#include "tgles/pipeline/raster.h"

TEST(Step08, DrawArraysValidation) {
  tgles::DrawValidator d;
  d.DrawArrays(tgles::kGlTriangles, 0, 3);
  EXPECT_EQ(d.GetError(), tgles::kGlNoError);
  EXPECT_EQ(d.LastCall().count, 3);
  d.DrawArrays(0x9999u, 0, 3);
  EXPECT_EQ(d.GetError(), tgles::kGlInvalidEnum);
  d.DrawArrays(tgles::kGlTriangles, -1, 3);
  EXPECT_EQ(d.GetError(), tgles::kGlInvalidValue);
  d.DrawArrays(tgles::kGlTriangles, 0, -1);
  EXPECT_EQ(d.GetError(), tgles::kGlInvalidValue);
  d.DrawArrays(0x000Eu, 0, 9);  // PATCHES is a core mode.
  EXPECT_EQ(d.GetError(), tgles::kGlNoError);
}

TEST(Step08, DrawElementsValidation) {
  tgles::DrawValidator d;
  d.DrawElements(tgles::kGlTriangles, 6, tgles::kGlUnsignedShortIndex, 0);
  EXPECT_EQ(d.GetError(), tgles::kGlNoError);
  d.DrawElements(tgles::kGlTriangles, 6, 0x1406u, 0);  // FLOAT index?
  EXPECT_EQ(d.GetError(), tgles::kGlInvalidEnum);
  d.DrawRangeElements(tgles::kGlTriangles, 5, 2, 6,
                      tgles::kGlUnsignedShortIndex, 0);  // start > end.
  EXPECT_EQ(d.GetError(), tgles::kGlInvalidValue);
  d.DrawElementsInstanced(tgles::kGlTriangles, 6,
                          tgles::kGlUnsignedShortIndex, 0, -2);
  EXPECT_EQ(d.GetError(), tgles::kGlInvalidValue);
  d.DrawArraysInstanced(tgles::kGlTriangles, 0, 3, 4);
  EXPECT_EQ(d.GetError(), tgles::kGlNoError);
}

TEST(Step08, IndirectNeedsBufferAndAlignment) {
  tgles::DrawValidator d;
  d.DrawArraysIndirect(tgles::kGlTriangles, 0);
  EXPECT_EQ(d.GetError(), tgles::kGlInvalidOperation);  // No buffer bound.
  d.SetIndirectBufferBound(true);
  d.DrawArraysIndirect(tgles::kGlTriangles, 3);  // Misaligned.
  EXPECT_EQ(d.GetError(), tgles::kGlInvalidValue);
  d.DrawArraysIndirect(tgles::kGlTriangles, 16);
  EXPECT_EQ(d.GetError(), tgles::kGlNoError);
  d.DrawElementsIndirect(tgles::kGlTriangles, tgles::kGlUnsignedIntIndex, 0);
  EXPECT_EQ(d.GetError(), tgles::kGlNoError);
}

TEST(Step08, RasterDefaults) {
  tgles::RasterState r;
  EXPECT_EQ(r.GetDepthFunc(), tgles::kGlLess);
  EXPECT_EQ(r.GetLineWidth(), 1.0f);
  EXPECT_EQ(r.GetClearDepth(), 1.0f);
  tgles::BlendState b = r.GetBlend(0);
  EXPECT_EQ(b.equation_rgb, tgles::kGlFuncAdd);
  EXPECT_EQ(b.src_rgb, tgles::kGlOne);
  EXPECT_EQ(b.dst_rgb, tgles::kGlZero);
  EXPECT_EQ(r.GetError(), tgles::kGlNoError);
}

TEST(Step08, ViewportScissorLineWidth) {
  tgles::RasterState r;
  r.Viewport(0, 0, 640, 480);
  EXPECT_EQ(r.GetViewport().width, 640);
  EXPECT_EQ(r.GetError(), tgles::kGlNoError);
  r.Viewport(0, 0, -640, 480);
  EXPECT_EQ(r.GetError(), tgles::kGlInvalidValue);
  r.Scissor(0, 0, 640, -1);
  EXPECT_EQ(r.GetError(), tgles::kGlInvalidValue);
  r.LineWidth(0.0f);
  EXPECT_EQ(r.GetError(), tgles::kGlInvalidValue);
  r.LineWidth(2.5f);
  EXPECT_EQ(r.GetLineWidth(), 2.5f);
  r.CullFace(0x1234u);
  EXPECT_EQ(r.GetError(), tgles::kGlInvalidEnum);
  r.FrontFace(tgles::kGlCw);
  EXPECT_EQ(r.GetError(), tgles::kGlNoError);
  r.DepthRangef(-1.0f, 2.0f);  // Clamped, no error.
  EXPECT_EQ(r.GetError(), tgles::kGlNoError);
}

TEST(Step08, BlendStatePerBuffer) {
  tgles::RasterState r;
  r.BlendFuncSeparate(tgles::kGlSrcAlpha, tgles::kGlOneMinusSrcAlpha,
                      tgles::kGlOne, tgles::kGlOne);
  tgles::BlendState b = r.GetBlend(0);
  EXPECT_EQ(b.src_rgb, tgles::kGlSrcAlpha);
  EXPECT_EQ(b.dst_alpha, tgles::kGlOne);
  r.BlendEquationi(1, tgles::kGlMaxBlend);
  EXPECT_EQ(r.GetBlend(1).equation_rgb, tgles::kGlMaxBlend);
  EXPECT_EQ(r.GetBlend(0).equation_rgb, tgles::kGlFuncAdd);  // Untouched.
  r.BlendEquationi(99, tgles::kGlFuncAdd);
  EXPECT_EQ(r.GetError(), tgles::kGlInvalidValue);
  r.BlendFunc(0x1234u, tgles::kGlOne);
  EXPECT_EQ(r.GetError(), tgles::kGlInvalidEnum);
  r.ColorMaski(99, 1, 1, 1, 1);
  EXPECT_EQ(r.GetError(), tgles::kGlInvalidValue);
}

TEST(Step08, DepthStencilState) {
  tgles::RasterState r;
  r.DepthFunc(tgles::kGlLequal);
  EXPECT_EQ(r.GetDepthFunc(), tgles::kGlLequal);
  r.DepthFunc(0x1234u);
  EXPECT_EQ(r.GetError(), tgles::kGlInvalidEnum);
  r.StencilFuncSeparate(tgles::kGlFront, tgles::kGlEqual, 1, 0xFFu);
  EXPECT_EQ(r.GetError(), tgles::kGlNoError);
  r.StencilOpSeparate(tgles::kGlBackFace, tgles::kGlKeep, tgles::kGlIncr,
                      tgles::kGlReplace);
  EXPECT_EQ(r.GetError(), tgles::kGlNoError);
  r.StencilFuncSeparate(0x1234u, tgles::kGlEqual, 1, 0xFFu);
  EXPECT_EQ(r.GetError(), tgles::kGlInvalidEnum);
  r.StencilOp(tgles::kGlKeep, tgles::kGlKeep, 0x1234u);
  EXPECT_EQ(r.GetError(), tgles::kGlInvalidEnum);
  r.SampleMaski(1, 0xFFFFFFFFu);  // Only word 0 exists.
  EXPECT_EQ(r.GetError(), tgles::kGlInvalidValue);
  r.SampleMaski(0, 0xFFu);
  EXPECT_EQ(r.GetError(), tgles::kGlNoError);
}
