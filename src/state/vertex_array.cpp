#include "tgles/state/vertex_array.h"

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

// Separate-attribute format (ES 3.1+, docs.gl/es3 glVertexAttribFormat +
// glBindVertexBuffer): errors verified against Khronos refpages, not TGL docs.
void VertexArrayManager::BindVertexBuffer(GLuint bindingIndex, GLuint buffer,
                                          std::uintptr_t offset,
                                          GLsizei stride) {
  if (bindingIndex >= kMaxVertexAttribBindings) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  if (stride < 0) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  // Offset is uintptr_t; negative cannot occur via C ABI (GLintptr >=0 check
  // happens in the gl_real wrapper which sees the signed value).
  VertexBinding& b = BoundForWrite()->bindings[bindingIndex];
  b.buffer = buffer;
  b.offset = offset;
  b.stride = stride;
}

void VertexArrayManager::VertexAttribFormat(GLuint attribIndex, GLint size,
                                            GLenum type, GLboolean normalized,
                                            GLuint relativeOffset) {
  if (!ValidIndex(attribIndex)) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  if (size < 1 || size > 4) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  if (relativeOffset > kMaxVertexAttribRelativeOffset) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  if (!IsPointerType(type)) {
    errors_.Record(kGlInvalidEnum);
    return;
  }
  GenericAttrib& a = BoundForWrite()->attribs[attribIndex];
  a.size = size;
  a.type = type;
  a.normalized = (normalized != kGlFalse);
  a.relative_offset = relativeOffset;
  a.pure_integer = false;
}

void VertexArrayManager::VertexAttribIFormat(GLuint attribIndex, GLint size,
                                             GLenum type,
                                             GLuint relativeOffset) {
  if (!ValidIndex(attribIndex)) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  if (size < 1 || size > 4) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  if (relativeOffset > kMaxVertexAttribRelativeOffset) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  if (!IsIntegerPointerType(type)) {
    errors_.Record(kGlInvalidEnum);
    return;
  }
  GenericAttrib& a = BoundForWrite()->attribs[attribIndex];
  a.size = size;
  a.type = type;
  a.relative_offset = relativeOffset;
  a.pure_integer = true;
}

void VertexArrayManager::VertexAttribBinding(GLuint attribIndex,
                                             GLuint bindingIndex) {
  if (!ValidIndex(attribIndex)) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  if (bindingIndex >= kMaxVertexAttribBindings) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  BoundForWrite()->attribs[attribIndex].binding_index = bindingIndex;
}

void VertexArrayManager::VertexBindingDivisor(GLuint bindingIndex,
                                              GLuint divisor) {
  if (bindingIndex >= kMaxVertexAttribBindings) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  BoundForWrite()->bindings[bindingIndex].divisor = divisor;
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

void VertexArrayManager::VertexAttrib1f(GLuint index, GLfloat x) {
  if (!ValidIndex(index)) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  values_[index].f[0] = x;
}

void VertexArrayManager::VertexAttrib2f(GLuint index, GLfloat x, GLfloat y) {
  if (!ValidIndex(index)) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  values_[index].f[0] = x;
  values_[index].f[1] = y;
}

void VertexArrayManager::VertexAttrib3f(GLuint index, GLfloat x, GLfloat y,
                                        GLfloat z) {
  if (!ValidIndex(index)) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  values_[index].f[0] = x;
  values_[index].f[1] = y;
  values_[index].f[2] = z;
}

void VertexArrayManager::VertexAttrib1fv(GLuint index, const GLfloat* values) {
  if (!ValidIndex(index)) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  if (values == nullptr) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  values_[index].f[0] = values[0];
}

void VertexArrayManager::VertexAttrib2fv(GLuint index, const GLfloat* values) {
  if (!ValidIndex(index)) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  if (values == nullptr) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  values_[index].f[0] = values[0];
  values_[index].f[1] = values[1];
}

void VertexArrayManager::VertexAttrib3fv(GLuint index, const GLfloat* values) {
  if (!ValidIndex(index)) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  if (values == nullptr) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  values_[index].f[0] = values[0];
  values_[index].f[1] = values[1];
  values_[index].f[2] = values[2];
}

void VertexArrayManager::VertexAttrib4fv(GLuint index, const GLfloat* values) {
  if (!ValidIndex(index)) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  if (values == nullptr) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  for (int i = 0; i < 4; ++i) values_[index].f[i] = values[i];
}

void VertexArrayManager::VertexAttribI4i(GLuint index, GLint x, GLint y,
                                         GLint z, GLint w) {
  if (!ValidIndex(index)) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  values_[index].i[0] = x;
  values_[index].i[1] = y;
  values_[index].i[2] = z;
  values_[index].i[3] = w;
}

void VertexArrayManager::VertexAttribI4ui(GLuint index, GLuint x, GLuint y,
                                          GLuint z, GLuint w) {
  if (!ValidIndex(index)) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  values_[index].u[0] = x;
  values_[index].u[1] = y;
  values_[index].u[2] = z;
  values_[index].u[3] = w;
}

void VertexArrayManager::VertexAttribI4iv(GLuint index, const GLint* values) {
  if (!ValidIndex(index)) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  if (values == nullptr) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  for (int i = 0; i < 4; ++i) values_[index].i[i] = values[i];
}

void VertexArrayManager::VertexAttribI4uiv(GLuint index, const GLuint* values) {
  if (!ValidIndex(index)) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  if (values == nullptr) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  for (int i = 0; i < 4; ++i) values_[index].u[i] = values[i];
}

void VertexArrayManager::GetVertexAttribfv(GLuint index, GLenum pname,
                                           GLfloat* params) {
  if (!ValidIndex(index)) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  if (params == nullptr) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  const GenericAttrib state = AttribState(index);
  const GenericAttribValue value = AttribValue(index);
  switch (pname) {
    case kGlCurrentVertexAttrib:
      for (int i = 0; i < 4; ++i) params[i] = value.f[i];
      return;
    case kGlVertexAttribArrayEnabled:
      params[0] = state.enabled ? 1.0f : 0.0f;
      return;
    case kGlVertexAttribArraySize:
      params[0] = static_cast<GLfloat>(state.size);
      return;
    case kGlVertexAttribArrayStride:
      params[0] = static_cast<GLfloat>(state.stride);
      return;
    case kGlVertexAttribArrayType:
      params[0] = static_cast<GLfloat>(state.type);
      return;
    case kGlVertexAttribArrayNormalized:
      params[0] = state.normalized ? 1.0f : 0.0f;
      return;
    case kGlVertexAttribArrayBufferBinding:
      params[0] = static_cast<GLfloat>(state.buffer);
      return;
    case kGlVertexAttribArrayDivisor:
      params[0] = static_cast<GLfloat>(state.divisor);
      return;
    default:
      break;
  }
  errors_.Record(kGlInvalidEnum);
}

void VertexArrayManager::GetVertexAttribiv(GLuint index, GLenum pname,
                                           GLint* params) {
  if (!ValidIndex(index)) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  if (params == nullptr) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  const GenericAttrib state = AttribState(index);
  const GenericAttribValue value = AttribValue(index);
  switch (pname) {
    case kGlCurrentVertexAttrib:
      for (int i = 0; i < 4; ++i) params[i] = static_cast<GLint>(value.f[i]);
      return;
    case kGlVertexAttribArrayEnabled:
      params[0] = state.enabled ? 1 : 0;
      return;
    case kGlVertexAttribArraySize:
      params[0] = state.size;
      return;
    case kGlVertexAttribArrayStride:
      params[0] = state.stride;
      return;
    case kGlVertexAttribArrayType:
      params[0] = static_cast<GLint>(state.type);
      return;
    case kGlVertexAttribArrayNormalized:
      params[0] = state.normalized ? 1 : 0;
      return;
    case kGlVertexAttribArrayBufferBinding:
      params[0] = static_cast<GLint>(state.buffer);
      return;
    case kGlVertexAttribArrayDivisor:
      params[0] = static_cast<GLint>(state.divisor);
      return;
    default:
      break;
  }
  errors_.Record(kGlInvalidEnum);
}

void VertexArrayManager::GetVertexAttribIiv(GLuint index, GLenum pname,
                                            GLint* params) {
  if (!ValidIndex(index)) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  if (params == nullptr) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  if (pname != kGlCurrentVertexAttrib) {
    errors_.Record(kGlInvalidEnum);
    return;
  }
  const GenericAttribValue value = AttribValue(index);
  for (int i = 0; i < 4; ++i) params[i] = value.i[i];
}

void VertexArrayManager::GetVertexAttribIuiv(GLuint index, GLenum pname,
                                             GLuint* params) {
  if (!ValidIndex(index)) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  if (params == nullptr) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  if (pname != kGlCurrentVertexAttrib) {
    errors_.Record(kGlInvalidEnum);
    return;
  }
  const GenericAttribValue value = AttribValue(index);
  for (int i = 0; i < 4; ++i) params[i] = value.u[i];
}

void VertexArrayManager::GetVertexAttribPointerv(GLuint index, GLenum pname,
                                                 void** pointer) {
  if (!ValidIndex(index)) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  if (pointer == nullptr) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  if (pname != kGlVertexAttribArrayPointer) {
    errors_.Record(kGlInvalidEnum);
    return;
  }
  *pointer = reinterpret_cast<void*>(AttribState(index).offset);
}

void VertexArrayManager::SetElementArrayBuffer(GLuint buffer) {
  BoundForWrite()->element_array_buffer = buffer;
}

GLuint VertexArrayManager::ElementArrayBuffer() const {
  auto it = arrays_.find(bound_);
  return it == arrays_.end() ? 0 : it->second.element_array_buffer;
}

VertexBinding VertexArrayManager::BindingState(GLuint bindingIndex) const {
  auto it = arrays_.find(bound_);
  if (it == arrays_.end() || bindingIndex >= kMaxVertexAttribBindings) {
    return VertexBinding();
  }
  return it->second.bindings[bindingIndex];
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
