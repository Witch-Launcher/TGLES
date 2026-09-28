#ifndef TGLES_DRAW_H
#define TGLES_DRAW_H

// Draw-command validation (spec 10.5): mode/type enums, count rules,
// indirect-buffer requirements and offset alignment.

#include <cstdint>

#include "tgles/base/error.h"
#include "tgles/base/gl_types.h"

namespace tgles {

// Primitive modes.
inline constexpr GLenum kGlPoints = 0x0000;
inline constexpr GLenum kGlLines = 0x0001;
inline constexpr GLenum kGlLineLoop = 0x0002;
inline constexpr GLenum kGlLineStrip = 0x0003;
inline constexpr GLenum kGlTriangles = 0x0004;
inline constexpr GLenum kGlTriangleStrip = 0x0005;
inline constexpr GLenum kGlTriangleFan = 0x0006;

// Index types.
inline constexpr GLenum kGlUnsignedByteIndex = 0x1401;
inline constexpr GLenum kGlUnsignedShortIndex = 0x1403;
inline constexpr GLenum kGlUnsignedIntIndex = 0x1405;

class DrawValidator {
 public:
  DrawValidator();

  GLenum GetError();
  bool HasPending() const;

  // The facade tells the validator whether DRAW_INDIRECT_BUFFER is bound.
  void SetIndirectBufferBound(bool bound);
  // The facade tells the validator whether the VAO has an ELEMENT_ARRAY_BUFFER.
  // Defaults to bound: a bare validator checks enums only (see Step08); the
  // GlesContext/HostRuntime always sync the real state first, so production
  // draws never rely on the default.
  void SetElementArrayBufferBound(bool bound);

  // Records INVALID_OPERATION for a bridge-side draw failure (valid GL
  // call the Metal bridge cannot execute: non-triangle mode, undecodable
  // attributes, missing target). Keeps facade GetError() coherent.
  void FlagBridgeError();

  // Each Draw* returns true when THIS call's validation passed. Callers must
  // gate execution on the return value, not HasPending(): a stale
  // FlagBridgeError (or any prior queue entry) stays pending until GetError()
  // and must not reject a later valid draw (ES 3.2 spec 2.3.1).
  bool DrawArrays(GLenum mode, GLint first, GLsizei count);
  bool DrawElements(GLenum mode, GLsizei count, GLenum type,
                    std::uintptr_t indices);
  bool DrawRangeElements(GLenum mode, GLuint start, GLuint end, GLsizei count,
                         GLenum type, std::uintptr_t indices);
  bool DrawArraysInstanced(GLenum mode, GLint first, GLsizei count,
                           GLsizei instancecount);
  bool DrawElementsInstanced(GLenum mode, GLsizei count, GLenum type,
                             std::uintptr_t indices, GLsizei instancecount);
  bool DrawArraysIndirect(GLenum mode, std::uintptr_t indirect);
  bool DrawElementsIndirect(GLenum mode, GLenum type, std::uintptr_t indirect);
  bool DrawElementsBaseVertex(GLenum mode, GLsizei count, GLenum type,
                              std::uintptr_t indices, GLint basevertex);
  bool DrawRangeElementsBaseVertex(GLenum mode, GLuint start, GLuint end,
                                   GLsizei count, GLenum type,
                                   std::uintptr_t indices, GLint basevertex);
  bool DrawElementsInstancedBaseVertex(GLenum mode, GLsizei count, GLenum type,
                                       std::uintptr_t indices,
                                       GLsizei instancecount, GLint basevertex);

  struct Call {
    GLenum mode = 0;
    GLsizei count = 0;
  };
  Call LastCall() const;

 private:
  bool ValidMode(GLenum mode) const;
  bool ValidIndexType(GLenum type) const;

  ErrorQueue errors_;
  bool indirect_buffer_bound_ = false;
  bool element_array_buffer_bound_ = true;
  Call last_;
};

}  // namespace tgles

#endif  // TGLES_DRAW_H