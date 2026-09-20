#include "tgles/vertex_array.h"

namespace tgles {

VertexArrayManager::VertexArrayManager() { arrays_[0] = Array(); }

GLenum VertexArrayManager::GetError() { return errors_.Get(); }
bool VertexArrayManager::HasPending() const { return errors_.HasPending(); }

bool VertexArrayManager::ValidIndex(GLuint index) const {
  return index < static_cast<GLuint>(kMaxVertexAttribs);
}

VertexArrayManager::Array* VertexArrayManager::BoundForWrite() {
  auto it = arrays_.find(bound_);
  if (it == arrays_.end()) {
    arrays_[bound_] = Array();
    it = arrays_.find(bound_);
  }
  return &it->second;
}

bool VertexArrayManager::IsPointerType(GLenum type) const {
  switch (type) {
    case kGlByte:
    case kGlUnsignedByte:
    case kGlShort:
    case kGlUnsignedShort:
    case kGlInt:
    case kGlUnsignedInt:
    case kGlHalfFloat:
    case kGlFloat:
    case kGlFixed:
    case kGlInt2101010Rev:
    case kGlUnsignedInt2101010Rev:
      return true;
    default:
      return false;
  }
}

bool VertexArrayManager::IsIntegerPointerType(GLenum type) const {
  switch (type) {
    case kGlByte:
    case kGlUnsignedByte:
    case kGlShort:
    case kGlUnsignedShort:
    case kGlInt:
    case kGlUnsignedInt:
      return true;
    default:
      return false;
  }
}

void VertexArrayManager::GenVertexArrays(GLsizei n, GLuint* arrays) {
  if (n < 0 || (n > 0 && arrays == nullptr)) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  for (GLsizei i = 0; i < n; ++i) {
    arrays[i] = next_name_++;
    arrays_[arrays[i]] = Array();
  }
}

void VertexArrayManager::DeleteVertexArrays(GLsizei n, const GLuint* arrays) {
  if (n < 0 || (n > 0 && arrays == nullptr)) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  for (GLsizei i = 0; i < n; ++i) {
    if (arrays[i] == 0) continue;
    arrays_.erase(arrays[i]);
    if (bound_ == arrays[i]) bound_ = 0;
  }
}

GLboolean VertexArrayManager::IsVertexArray(GLuint array) {
  if (array == 0) return kGlFalse;
  auto it = arrays_.find(array);
  return (it != arrays_.end()) ? kGlTrue : kGlFalse;
}

void VertexArrayManager::BindVertexArray(GLuint array) {
  auto it = arrays_.find(array);
  if (array != 0 && it == arrays_.end()) {
    // Binding a deleted/never-generated name recreates it (spec 2.6.1).
    arrays_[array] = Array();
  }
  bound_ = array;
}

GLuint VertexArrayManager::BoundArray() const { return bound_; }

void VertexArrayManager::EnableVertexAttribArray(GLuint index) {
  if (!ValidIndex(index)) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  BoundForWrite()->attribs[index].enabled = true;
}

void VertexArrayManager::DisableVertexAttribArray(GLuint index) {
  if (!ValidIndex(index)) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  BoundForWrite()->attribs[index].enabled = false;
}

void VertexArrayManager::VertexAttribPointer(GLuint index, GLint size,
                                             GLenum type, GLboolean normalized,
                                             GLsizei stride,
                                             std::uintptr_t offset,
                                             GLuint array_buffer) {
  if (!ValidIndex(index)) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  const bool bgra = (size == static_cast<GLint>(kGlBgra));
  if (!((size >= 1 && size <= 4) || bgra)) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  if (!IsPointerType(type)) {
    errors_.Record(kGlInvalidEnum);
    return;
  }
  if (stride < 0) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  if (bgra && (type != kGlUnsignedByte || normalized == kGlFalse)) {
    errors_.Record(kGlInvalidOperation);
    return;
  }
  GenericAttrib& a = BoundForWrite()->attribs[index];
  a.size = size;
  a.type = type;
  a.normalized = (normalized != kGlFalse);
  a.stride = stride;
  a.buffer = array_buffer;
  a.offset = offset;
  a.pure_integer = false;
}

void VertexArrayManager::VertexAttribIPointer(GLuint index, GLint size,
                                              GLenum type, GLsizei stride,
                                              std::uintptr_t offset,
                                              GLuint array_buffer) {
  if (!ValidIndex(index)) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  if (size < 1 || size > 4) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  if (!IsIntegerPointerType(type)) {
    errors_.Record(kGlInvalidEnum);
    return;
  }
  if (stride < 0) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  GenericAttrib& a = BoundForWrite()->attribs[index];
  a.size = size;
  a.type = type;
  a.normalized = false;
  a.stride = stride;
  a.buffer = array_buffer;
  a.offset = offset;
  a.pure_integer = true;
}

void VertexArrayManager::VertexAttribDivisor(GLuint index, GLuint divisor) {
  if (!ValidIndex(index)) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  BoundForWrite()->attribs[index].divisor = divisor;
}

void VertexArrayManager::VertexAttrib4f(GLuint index, GLfloat x, GLfloat y,
                                        GLfloat z, GLfloat w) {
  if (!ValidIndex(index)) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  values_[index].f[0] = x;
  values_[index].f[1] = y;
  values_[index].f[2] = z;
  values_[index].f[3] = w;
}

void VertexArrayManager::SetElementArrayBuffer(GLuint buffer) {
  BoundForWrite()->element_array_buffer = buffer;
}

GLuint VertexArrayManager::ElementArrayBuffer() const {
  auto it = arrays_.find(bound_);
  return it == arrays_.end() ? 0 : it->second.element_array_buffer;
}

GenericAttrib VertexArrayManager::AttribState(GLuint index) const {
  auto it = arrays_.find(bound_);
  if (it == arrays_.end() || !ValidIndex(index)) return GenericAttrib();
  return it->second.attribs[index];
}

GenericAttribValue VertexArrayManager::AttribValue(GLuint index) const {
  if (!ValidIndex(index)) return GenericAttribValue();
  return values_[index];
}

GLuint VertexArrayManager::RestartIndexForType(GLenum index_type) {
  switch (index_type) {
    case kGlUnsignedByte: return 0xFFu;
    case kGlUnsignedShort: return 0xFFFFu;
    case kGlUnsignedInt: return 0xFFFFFFFFu;
    default: return 0;
  }
}

}  // namespace tgles
