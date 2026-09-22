// GLES -> Metal enum translation (pure C++; the .mm casts these ints to the
// MTL enums 1:1). Values verified against the macOS 26.2 SDK:
// MTLRenderPipeline.h (MTLBlendFactor/Operation/ColorWriteMask) and
// MTLDepthStencil.h (MTLCompareFunction).

#include "tgles/gpu/metal_translate.h"

#include "tgles/pipeline/raster.h"  // kGl* factor/equation/compare constants.

namespace tgles {
namespace metal_translate {

int BlendFactor(GLenum factor) {
  switch (factor) {
    case kGlZero:
      return 0;  // MTLBlendFactorZero
    case kGlOne:
      return 1;  // MTLBlendFactorOne
    case kGlSrcColor:
      return 2;  // MTLBlendFactorSourceColor
    case kGlOneMinusSrcColor:
      return 3;  // MTLBlendFactorOneMinusSourceColor
    case kGlSrcAlpha:
      return 4;  // MTLBlendFactorSourceAlpha
    case kGlOneMinusSrcAlpha:
      return 5;  // MTLBlendFactorOneMinusSourceAlpha
    case kGlDstColor:
      return 6;  // MTLBlendFactorDestinationColor
    case kGlOneMinusDstColor:
      return 7;  // MTLBlendFactorOneMinusDestinationColor
    case kGlDstAlpha:
      return 8;  // MTLBlendFactorDestinationAlpha
    case kGlOneMinusDstAlpha:
      return 9;  // MTLBlendFactorOneMinusDestinationAlpha
    case kGlSrcAlphaSaturate:
      return 10;  // MTLBlendFactorSourceAlphaSaturated
    case kGlConstantColor:
      return 11;  // MTLBlendFactorBlendColor
    case kGlOneMinusConstantColor:
      return 12;  // MTLBlendFactorOneMinusBlendColor
    case kGlConstantAlpha:
      return 13;  // MTLBlendFactorBlendAlpha
    case kGlOneMinusConstantAlpha:
      return 14;  // MTLBlendFactorOneMinusBlendAlpha
    case kGlSrc1Color:
      return 15;  // MTLBlendFactorSource1Color (SDK 26.2 verified)
    case kGlOneMinusSrc1Color:
      return 16;  // MTLBlendFactorOneMinusSource1Color
    case kGlSrc1Alpha:
      return 17;  // MTLBlendFactorSource1Alpha
    case kGlOneMinusSrc1Alpha:
      return 18;  // MTLBlendFactorOneMinusSource1Alpha
    default:
      return -1;  // Unknown: no PSO may use it.
  }
}

int BlendOperation(GLenum equation) {
  switch (equation) {
    case kGlFuncAdd:
      return 0;  // MTLBlendOperationAdd
    case kGlFuncSubtract:
      return 1;  // MTLBlendOperationSubtract
    case kGlFuncReverseSubtract:
      return 2;  // MTLBlendOperationReverseSubtract
    case kGlMinBlend:
      return 3;  // MTLBlendOperationMin
    case kGlMaxBlend:
      return 4;  // MTLBlendOperationMax
    default:
      return -1;
  }
}

int ColorWriteMask(bool r, bool g, bool b, bool a) {
  // NOTE the reversed bit order (SDK MTLColorWriteMask): Red=0x8, not 0x1.
  int mask = 0;
  if (r) mask |= 0x8;
  if (g) mask |= 0x4;
  if (b) mask |= 0x2;
  if (a) mask |= 0x1;
  return mask;
}

int CompareFunction(GLenum func) {
  // GL NEVER..ALWAYS (0x0200..0x0207) onto MTLCompareFunctionNever..Always
  // (0..7): the same 1:1 the Apple bridge already uses in ConfigureDepth.
  if (func < kGlNever || func > kGlAlways) return -1;
  return static_cast<int>(func - kGlNever);
}

}  // namespace metal_translate
}  // namespace tgles
