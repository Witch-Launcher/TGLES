#ifndef TGLES_GEOMETRY_H
#define TGLES_GEOMETRY_H

// Geometry stage helpers: output limits and primitive-type classification.

#include "tgles/base/error.h"
#include "tgles/base/gl_types.h"

namespace tgles {

inline constexpr GLenum kGlTrianglesAdjacency = 0x000C;
inline constexpr GLenum kGlTriangleStripAdjacency = 0x000D;
inline constexpr GLenum kGlLinesAdjacency = 0x000A;
inline constexpr GLenum kGlLineStripAdjacency = 0x000B;
// NOTE: kGlTriangleStrip (0x0005) and kGlLineStrip (0x0003) live in draw.h.

inline constexpr GLint kMaxGeometryOutputVertices = 256;  // Spec minimum.
inline constexpr GLint kMaxGeometryTotalOutputComponents = 1024;

class GeometryState {
 public:
  GeometryState() = default;

  // True for primitive types a geometry shader may consume.
  static bool IsValidInputPrimitive(GLenum mode);
  // True for primitive types a geometry shader may emit.
  static bool IsValidOutputPrimitive(GLenum mode);
};

}  // namespace tgles

#endif  // TGLES_GEOMETRY_H