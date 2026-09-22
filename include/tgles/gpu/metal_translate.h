#ifndef TGLES_METAL_TRANSLATE_H
#define TGLES_METAL_TRANSLATE_H

#include "tgles/base/gl_types.h"

// GLES state -> Metal enum values, pure C++ (no <Metal/*>).
//
// Why numbers, not names: tgles_core must compile without the Metal SDK
// (iphoneos cross-builds, Linux CI), so this module speaks Metal in raw
// integers and the .mm translates them 1:1 into MTL enums. Every value below
// was read off the macOS 26.2 SDK headers (MTLRenderPipeline.h,
// MTLDepthStencil.h) — a mismatch fails the device tests, not just these
// table tests.
//
// Unmapped inputs return -1, keeping them out of a PSO instead of
// miscompiling. Dual-source factors (SRC1_*, 15..18) execute via the
// translator's DualOut fragment ([[color(0), index(1)]]); CONSTANT_* factors
// (11..14) read the per-frame blend color.

namespace tgles {
namespace metal_translate {

// MTLBlendFactor (MTLRenderPipeline.h): Zero=0 .. OneMinusBlendAlpha=14.
int BlendFactor(GLenum factor);

// MTLBlendOperation: Add=0, Subtract=1, ReverseSubtract=2, Min=3, Max=4.
int BlendOperation(GLenum equation);

// MTLColorWriteMask: None=0, Red=0x8, Green=0x4, Blue=0x2, Alpha=0x1.
int ColorWriteMask(bool r, bool g, bool b, bool a);

// MTLCompareFunction: Never=0 .. Always=7 (GL NEVER..ALWAYS minus 0x0200).
int CompareFunction(GLenum func);

}  // namespace metal_translate
}  // namespace tgles

#endif  // TGLES_METAL_TRANSLATE_H
