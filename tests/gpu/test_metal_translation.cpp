// Type-1a translation tests (pure C++, no GPU): GLES state -> Metal numbers.
//
// What this pins: the exact integers the Apple bridge casts into MTL enums
// when compiling a PSO. Values were read off the macOS 26.2 SDK
// (MTLRenderPipeline.h, MTLDepthStencil.h); a table typo here becomes a
// wrong-blend bug on device, caught first here and then by the pixels in
// tests/gpu/test_metal_device.mm (MetalTranslation.*).

#include "test_framework.h"

#include "tgles/gpu/metal_translate.h"
#include "tgles/pipeline/raster.h"

TEST(MetalTranslate, BlendFactorsMatchSdk) {
  using tgles::metal_translate::BlendFactor;
  EXPECT_EQ(BlendFactor(tgles::kGlZero), 0);
  EXPECT_EQ(BlendFactor(tgles::kGlOne), 1);
  EXPECT_EQ(BlendFactor(tgles::kGlSrcColor), 2);
  EXPECT_EQ(BlendFactor(tgles::kGlOneMinusSrcColor), 3);
  EXPECT_EQ(BlendFactor(tgles::kGlSrcAlpha), 4);
  EXPECT_EQ(BlendFactor(tgles::kGlOneMinusSrcAlpha), 5);
  EXPECT_EQ(BlendFactor(tgles::kGlDstColor), 6);
  EXPECT_EQ(BlendFactor(tgles::kGlOneMinusDstColor), 7);
  EXPECT_EQ(BlendFactor(tgles::kGlDstAlpha), 8);
  EXPECT_EQ(BlendFactor(tgles::kGlOneMinusDstAlpha), 9);
  EXPECT_EQ(BlendFactor(tgles::kGlSrcAlphaSaturate), 10);
  EXPECT_EQ(BlendFactor(tgles::kGlConstantColor), 11);
  EXPECT_EQ(BlendFactor(tgles::kGlOneMinusConstantColor), 12);
  EXPECT_EQ(BlendFactor(tgles::kGlConstantAlpha), 13);
  EXPECT_EQ(BlendFactor(tgles::kGlOneMinusConstantAlpha), 14);
  // Dual-source factors (GL_EXT_blend_func_extended values, MTL 15..18):
  // executed via DualOut MSL ([[color(0), index(1)]]), device-tested.
  EXPECT_EQ(BlendFactor(tgles::kGlSrc1Color), 15);
  EXPECT_EQ(BlendFactor(tgles::kGlOneMinusSrc1Color), 16);
  EXPECT_EQ(BlendFactor(tgles::kGlSrc1Alpha), 17);
  EXPECT_EQ(BlendFactor(tgles::kGlOneMinusSrc1Alpha), 18);
  EXPECT_EQ(BlendFactor(0x9999u), -1);
}

TEST(MetalTranslate, BlendOperationsMatchSdk) {
  using tgles::metal_translate::BlendOperation;
  EXPECT_EQ(BlendOperation(tgles::kGlFuncAdd), 0);
  EXPECT_EQ(BlendOperation(tgles::kGlFuncSubtract), 1);
  EXPECT_EQ(BlendOperation(tgles::kGlFuncReverseSubtract), 2);
  EXPECT_EQ(BlendOperation(tgles::kGlMinBlend), 3);
  EXPECT_EQ(BlendOperation(tgles::kGlMaxBlend), 4);
  EXPECT_EQ(BlendOperation(0x9999u), -1);
}

TEST(MetalTranslate, ColorWriteMaskBitOrder) {
  using tgles::metal_translate::ColorWriteMask;
  // SDK order is reversed (Red=0x8): the test that catches a naive 1<<i.
  EXPECT_EQ(ColorWriteMask(true, true, true, true), 0xF);
  EXPECT_EQ(ColorWriteMask(true, false, false, false), 0x8);
  EXPECT_EQ(ColorWriteMask(false, true, false, false), 0x4);
  EXPECT_EQ(ColorWriteMask(false, false, true, false), 0x2);
  EXPECT_EQ(ColorWriteMask(false, false, false, true), 0x1);
  EXPECT_EQ(ColorWriteMask(false, false, false, false), 0x0);
}

TEST(MetalTranslate, CompareFunctionsAreOneToOne) {
  using tgles::metal_translate::CompareFunction;
  for (unsigned i = 0; i < 8; ++i) {
    EXPECT_EQ(CompareFunction(tgles::kGlNever + i), (int)i);
  }
  EXPECT_EQ(CompareFunction(0x9999u), -1);
}
