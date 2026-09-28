#include "tgles/pipeline/draw.h"

#include <cstdint>

#include "tgles/base/debug_log.h"

namespace tgles {

namespace {

// A rejected draw leaves nothing but INVALID_ENUM in the error queue: the
// pixels simply never appear, so a dropped GUI/background quad reads on
// screen as "black UI". Log the mode so a device log names what vanished.
// Bounded so a per-frame reject cannot flood the 64KB ring.
void LogBadMode(const char* fn, GLenum mode) {
  static int total = 0;
  ++total;
  if (total <= 32 || (total % 100) == 0) {
    TglDebugf("draw reject: %s mode=0x%x total=%d", fn, mode, total);
  }
}

}  // namespace

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
    case 0x0007:  // QUADS: desktop-GL primitive (removed from core profile but
                  // still emitted by GL 4.6 compat apps such as Minecraft's
                  // GUI fills); the facade CPU-expands it to triangles.
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

bool DrawValidator::DrawArrays(GLenum mode, GLint first, GLsizei count) {
  if (!ValidMode(mode)) {
    LogBadMode("DrawArrays", mode);
    errors_.Record(kGlInvalidEnum);
    return false;
  }
  if (first < 0 || count < 0) {
    errors_.Record(kGlInvalidValue);
    return false;
  }
  last_.mode = mode;
  last_.count = count;
  return true;
}

bool DrawValidator::DrawElements(GLenum mode, GLsizei count, GLenum type,
                                 std::uintptr_t indices) {
  if (!ValidMode(mode)) {
    TglDebugf("DrawElements reject: mode 0x%x invalid", mode);
    errors_.Record(kGlInvalidEnum);
    return false;
  }
  if (!ValidIndexType(type)) {
    TglDebugf("DrawElements reject: type 0x%x invalid (mode 0x%x count %d)",
              type, mode, count);
    errors_.Record(kGlInvalidEnum);
    return false;
  }
  if (count < 0) {
    TglDebugf("DrawElements reject: count %d < 0", count);
    errors_.Record(kGlInvalidValue);
    return false;
  }
  if (!element_array_buffer_bound_) {
    // ES 3.2 core has no client-side index arrays (spec 10.5).
    TglDebugf(
        "DrawElements reject: no ELEMENT_ARRAY_BUFFER (mode 0x%x count %d "
        "type 0x%x)",
        mode, count, type);
    errors_.Record(kGlInvalidOperation);
    return false;
  }
  (void)indices;
  last_.mode = mode;
  last_.count = count;
  return true;
}

bool DrawValidator::DrawRangeElements(GLenum mode, GLuint start, GLuint end,
                                      GLsizei count, GLenum type,
                                      std::uintptr_t indices) {
  if (!ValidMode(mode)) {
    LogBadMode("DrawRangeElements", mode);
    errors_.Record(kGlInvalidEnum);
    return false;
  }
  if (!ValidIndexType(type)) {
    errors_.Record(kGlInvalidEnum);
    return false;
  }
  if (start > end) {
    errors_.Record(kGlInvalidValue);
    return false;
  }
  return DrawElements(mode, count, type, indices);
}

bool DrawValidator::DrawArraysInstanced(GLenum mode, GLint first, GLsizei count,
                                        GLsizei instancecount) {
  if (instancecount < 0) {
    if (!ValidMode(mode)) {
      LogBadMode("DrawArraysInstanced", mode);
      errors_.Record(kGlInvalidEnum);
      return false;
    }
    errors_.Record(kGlInvalidValue);
    return false;
  }
  return DrawArrays(mode, first, count);
}

bool DrawValidator::DrawElementsInstanced(GLenum mode, GLsizei count,
                                          GLenum type, std::uintptr_t indices,
                                          GLsizei instancecount) {
  if (instancecount < 0) {
    if (!ValidMode(mode) || !ValidIndexType(type)) {
      errors_.Record(kGlInvalidEnum);
      if (!ValidMode(mode)) {
        LogBadMode("DrawElementsInstanced", mode);
        return false;
      }
    }
    errors_.Record(kGlInvalidValue);
    return false;
  }
  return DrawElements(mode, count, type, indices);
}

bool DrawValidator::DrawArraysIndirect(GLenum mode, std::uintptr_t indirect) {
  if (!ValidMode(mode)) {
    LogBadMode("DrawArraysIndirect", mode);
    errors_.Record(kGlInvalidEnum);
    return false;
  }
  if (!indirect_buffer_bound_) {
    errors_.Record(kGlInvalidOperation);
    return false;
  }
  if ((indirect % 4) != 0) {
    errors_.Record(kGlInvalidValue);
    return false;
  }
  last_.mode = mode;
  return true;
}
bool DrawValidator::DrawElementsIndirect(GLenum mode, GLenum type,
                                         std::uintptr_t indirect) {
  if (!ValidMode(mode) || !ValidIndexType(type)) {
    if (!ValidMode(mode)) LogBadMode("DrawElementsIndirect", mode);
    errors_.Record(kGlInvalidEnum);
    return false;
  }
  if (!indirect_buffer_bound_) {
    errors_.Record(kGlInvalidOperation);
    return false;
  }
  if ((indirect % 4) != 0) {
    errors_.Record(kGlInvalidValue);
    return false;
  }
  last_.mode = mode;
  return true;
}

bool DrawValidator::DrawElementsBaseVertex(GLenum mode, GLsizei count,
                                           GLenum type, std::uintptr_t indices,
                                           GLint basevertex) {
  (void)basevertex;  // Any offset is legal; restart compares pre-add.
  return DrawElements(mode, count, type, indices);
}

bool DrawValidator::DrawRangeElementsBaseVertex(GLenum mode, GLuint start,
                                                GLuint end, GLsizei count,
                                                GLenum type,
                                                std::uintptr_t indices,
                                                GLint basevertex) {
  (void)basevertex;
  return DrawRangeElements(mode, start, end, count, type, indices);
}

bool DrawValidator::DrawElementsInstancedBaseVertex(GLenum mode, GLsizei count,
                                                    GLenum type,
                                                    std::uintptr_t indices,
                                                    GLsizei instancecount,
                                                    GLint basevertex) {
  (void)basevertex;
  return DrawElementsInstanced(mode, count, type, indices, instancecount);
}

DrawValidator::Call DrawValidator::LastCall() const { return last_; }

}  // namespace tgles
