#include "tgles/pipeline/draw.h"

#include <cstdint>

namespace tgles {

DrawValidator::DrawValidator() = default;

GLenum DrawValidator::GetError() { return errors_.Get(); }
bool DrawValidator::HasPending() const { return errors_.HasPending(); }

void DrawValidator::SetIndirectBufferBound(bool bound) {
  indirect_buffer_bound_ = bound;
}

void DrawValidator::SetElementArrayBufferBound(bool bound) {
  element_array_buffer_bound_ = bound;
}

void DrawValidator::FlagBridgeError() {
  errors_.Record(kGlInvalidOperation);
}

bool DrawValidator::ValidMode(GLenum mode) const {
  switch (mode) {
    case kGlPoints:
    case kGlLines:
    case 0x0002:  // LINE_LOOP
    case 0x0003:  // LINE_STRIP
    case 0x0004:  // TRIANGLES
    case 0x0005:  // TRIANGLE_STRIP
    case 0x0006:  // TRIANGLE_FAN
    case 0x000A:  // LINES_ADJACENCY
    case 0x000B:  // LINE_STRIP_ADJACENCY
    case 0x000C:  // TRIANGLES_ADJACENCY
    case 0x000D:  // TRIANGLE_STRIP_ADJACENCY
    case 0x000E:  // PATCHES
      return true;
    default:
      return false;
  }
}

bool DrawValidator::ValidIndexType(GLenum type) const {
  return type == kGlUnsignedByteIndex || type == kGlUnsignedShortIndex ||
         type == kGlUnsignedIntIndex;
}

void DrawValidator::DrawArrays(GLenum mode, GLint first, GLsizei count) {
  if (!ValidMode(mode)) {
    errors_.Record(kGlInvalidEnum);
    return;
  }
  if (first < 0 || count < 0) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  last_.mode = mode;
  last_.count = count;
}

void DrawValidator::DrawElements(GLenum mode, GLsizei count, GLenum type,
                                 std::uintptr_t indices) {
  if (!ValidMode(mode)) {
    errors_.Record(kGlInvalidEnum);
    return;
  }
  if (!ValidIndexType(type)) {
    errors_.Record(kGlInvalidEnum);
    return;
  }
  if (count < 0) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  if (!element_array_buffer_bound_) {
    // ES 3.2 core has no client-side index arrays (spec 10.5).
    errors_.Record(kGlInvalidOperation);
    return;
  }
  (void)indices;
  last_.mode = mode;
  last_.count = count;
}

void DrawValidator::DrawRangeElements(GLenum mode, GLuint start, GLuint end,
                                      GLsizei count, GLenum type,
                                      std::uintptr_t indices) {
  if (!ValidMode(mode)) {
    errors_.Record(kGlInvalidEnum);
    return;
  }
  if (!ValidIndexType(type)) {
    errors_.Record(kGlInvalidEnum);
    return;
  }
  if (start > end) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  DrawElements(mode, count, type, indices);
}

void DrawValidator::DrawArraysInstanced(GLenum mode, GLint first, GLsizei count,
                                        GLsizei instancecount) {
  if (instancecount < 0) {
    if (!ValidMode(mode)) {
      errors_.Record(kGlInvalidEnum);
      return;
    }
    errors_.Record(kGlInvalidValue);
    return;
  }
  DrawArrays(mode, first, count);
}

void DrawValidator::DrawElementsInstanced(GLenum mode, GLsizei count,
                                          GLenum type, std::uintptr_t indices,
                                          GLsizei instancecount) {
  if (instancecount < 0) {
    if (!ValidMode(mode) || !ValidIndexType(type)) {
      errors_.Record(kGlInvalidEnum);
      if (!ValidMode(mode)) return;
    }
    errors_.Record(kGlInvalidValue);
    return;
  }
  DrawElements(mode, count, type, indices);
}

void DrawValidator::DrawArraysIndirect(GLenum mode, std::uintptr_t indirect) {
  if (!ValidMode(mode)) {
    errors_.Record(kGlInvalidEnum);
    return;
  }
  if (!indirect_buffer_bound_) {
    errors_.Record(kGlInvalidOperation);
    return;
  }
  if ((indirect % 4) != 0) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  last_.mode = mode;
}
void DrawValidator::DrawElementsIndirect(GLenum mode, GLenum type,
                                         std::uintptr_t indirect) {
  if (!ValidMode(mode) || !ValidIndexType(type)) {
    errors_.Record(kGlInvalidEnum);
    return;
  }
  if (!indirect_buffer_bound_) {
    errors_.Record(kGlInvalidOperation);
    return;
  }
  if ((indirect % 4) != 0) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  last_.mode = mode;
}

void DrawValidator::DrawElementsBaseVertex(GLenum mode, GLsizei count,
                                           GLenum type, std::uintptr_t indices,
                                           GLint basevertex) {
  (void)basevertex;  // Any offset is legal; restart compares pre-add.
  DrawElements(mode, count, type, indices);
}

void DrawValidator::DrawRangeElementsBaseVertex(GLenum mode, GLuint start,
                                                GLuint end, GLsizei count,
                                                GLenum type,
                                                std::uintptr_t indices,
                                                GLint basevertex) {
  (void)basevertex;
  DrawRangeElements(mode, start, end, count, type, indices);
}

void DrawValidator::DrawElementsInstancedBaseVertex(GLenum mode, GLsizei count,
                                                    GLenum type,
                                                    std::uintptr_t indices,
                                                    GLsizei instancecount,
                                                    GLint basevertex) {
  (void)basevertex;
  DrawElementsInstanced(mode, count, type, indices, instancecount);
}

DrawValidator::Call DrawValidator::LastCall() const { return last_; }

}  // namespace tgles
