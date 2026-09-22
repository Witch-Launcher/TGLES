// Geometry stage rules (spec 11.3): valid input/output primitives.
// Mirrors include/tgles/state/geometry.h.

#include "tgles/state/geometry.h"

namespace tgles {

bool GeometryState::IsValidInputPrimitive(GLenum mode) {
  switch (mode) {
    case 0x0000:  // POINTS
    case 0x0001:  // LINES
    case 0x0003:  // LINE_STRIP
    case 0x0004:  // TRIANGLES
    case 0x0005:  // TRIANGLE_STRIP
    case kGlLinesAdjacency:
    case kGlLineStripAdjacency:
    case kGlTrianglesAdjacency:
    case kGlTriangleStripAdjacency:
      return true;
    default:
      return false;
  }
}

bool GeometryState::IsValidOutputPrimitive(GLenum mode) {
  switch (mode) {
    case 0x0000:  // POINTS
    case 0x0003:  // LINE_STRIP
    case 0x0005:  // TRIANGLE_STRIP
      return true;
    default:
      return false;
  }
}

}  // namespace tgles
