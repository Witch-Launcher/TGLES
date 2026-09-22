#ifndef TGLES_VERTEX_ARRAY_H
#define TGLES_VERTEX_ARRAY_H

// Vertex arrays and VAOs (spec sections 10.3-10.4): generic attribute state,
// separate attribute format/binding state (ES 3.1+), divisors, VAO container
// semantics (ELEMENT_ARRAY_BUFFER binding is VAO state).

#include <array>
#include <cstdint>
#include <map>

#include "tgles/base/error.h"
#include "tgles/base/gl_types.h"

namespace tgles {

// Generic attribute types accepted by VertexAttribPointer.
inline constexpr GLenum kGlByte = 0x1400;
inline constexpr GLenum kGlUnsignedByte = 0x1401;
inline constexpr GLenum kGlShort = 0x1402;
inline constexpr GLenum kGlUnsignedShort = 0x1403;
inline constexpr GLenum kGlInt = 0x1404;
inline constexpr GLenum kGlUnsignedInt = 0x1405;
inline constexpr GLenum kGlFloat = 0x1406;
inline constexpr GLenum kGlHalfFloat = 0x140B;
inline constexpr GLenum kGlFixed = 0x140C;
inline constexpr GLenum kGlInt2101010Rev = 0x8D9F;
inline constexpr GLenum kGlUnsignedInt2101010Rev = 0x8368;
// BGRA size token for VertexAttribPointer.
inline constexpr GLenum kGlBgra = 0x80E1;

// Spec minimum number of generic attribute slots.
inline constexpr GLint kMaxVertexAttribs = 16;
// Separate-attribute (ES 3.1+) binding points and relative-offset limit.
inline constexpr GLuint kMaxVertexAttribBindings = 16;
inline constexpr GLuint kMaxVertexAttribRelativeOffset = 2047u;

// GetVertexAttrib* query names (gl32.h, spec 10.3.1).
inline constexpr GLenum kGlVertexAttribArrayEnabled = 0x8622;
inline constexpr GLenum kGlVertexAttribArraySize = 0x8623;
inline constexpr GLenum kGlVertexAttribArrayStride = 0x8624;
inline constexpr GLenum kGlVertexAttribArrayType = 0x8625;
inline constexpr GLenum kGlCurrentVertexAttrib = 0x8626;
inline constexpr GLenum kGlVertexAttribArrayNormalized = 0x886A;
inline constexpr GLenum kGlVertexAttribArrayBufferBinding = 0x889F;
inline constexpr GLenum kGlVertexAttribArrayDivisor = 0x88FE;
inline constexpr GLenum kGlVertexAttribArrayPointer = 0x8645;

struct GenericAttrib {
  bool enabled = false;
  GLint size = 4;
  GLenum type = kGlFloat;
  bool normalized = false;
  GLsizei stride = 0;
  GLuint buffer = 0;  // ARRAY_BUFFER captured at Pointer time.
  std::uintptr_t offset = 0;
  GLuint divisor = 0;
  GLuint binding_index = 0;  // For the *Format/*Binding API.
  GLuint relative_offset = 0;  // Offset within the bound vertex buffer.
  bool pure_integer = false;  // Set by VertexAttribIPointer.
};

struct VertexBinding {
  GLuint buffer = 0;
  std::uintptr_t offset = 0;
  GLsizei stride = 0;
  GLuint divisor = 0;
};

struct GenericAttribValue {
  GLfloat f[4] = {0.0f, 0.0f, 0.0f, 1.0f};
  GLint i[4] = {0, 0, 0, 1};
  GLuint u[4] = {0u, 0u, 0u, 1u};
};

class VertexArrayManager {
 public:
  VertexArrayManager();

  GLenum GetError();
  bool HasPending() const;

  void GenVertexArrays(GLsizei n, GLuint* arrays);
  void DeleteVertexArrays(GLsizei n, const GLuint* arrays);
  GLboolean IsVertexArray(GLuint array);
  void BindVertexArray(GLuint array);
  GLuint BoundArray() const;

  void EnableVertexAttribArray(GLuint index);
  void DisableVertexAttribArray(GLuint index);
  void VertexAttribPointer(GLuint index, GLint size, GLenum type,
                           GLboolean normalized, GLsizei stride,
                           std::uintptr_t offset, GLuint array_buffer);
  void VertexAttribIPointer(GLuint index, GLint size, GLenum type,
                            GLsizei stride, std::uintptr_t offset,
                            GLuint array_buffer);
  void VertexAttribDivisor(GLuint index, GLuint divisor);
  void VertexAttrib4f(GLuint index, GLfloat x, GLfloat y, GLfloat z, GLfloat w);
  // Generic-attribute constant setters (spec 10.3.1): write the CURRENT
  // value slot; arrays copy count*comps. -like errors mirror VertexAttrib4f.
  void VertexAttrib1f(GLuint index, GLfloat x);
  void VertexAttrib2f(GLuint index, GLfloat x, GLfloat y);
  void VertexAttrib3f(GLuint index, GLfloat x, GLfloat y, GLfloat z);
  void VertexAttrib1fv(GLuint index, const GLfloat* values);
  void VertexAttrib2fv(GLuint index, const GLfloat* values);
  void VertexAttrib3fv(GLuint index, const GLfloat* values);
  void VertexAttrib4fv(GLuint index, const GLfloat* values);
  void VertexAttribI4i(GLuint index, GLint x, GLint y, GLint z, GLint w);
  void VertexAttribI4ui(GLuint index, GLuint x, GLuint y, GLuint z, GLuint w);
  void VertexAttribI4iv(GLuint index, const GLint* values);
  void VertexAttribI4uiv(GLuint index, const GLuint* values);
  // Attribute queries (spec 10.3.1 GetVertexAttrib*): CURRENT reads the
  // value slots above; array state reads AttribState. Unknown pname is
  // INVALID_ENUM, bad index INVALID_VALUE, null params INVALID_VALUE.
  // Separate-attribute format (ES 3.1+, docs.gl/es3): BindVertexBuffer binds
  // a buffer to a binding point; VertexAttrib*Format describes the attrib
  // layout within that binding; VertexAttribBinding links attrib->binding;
  // VertexBindingDivisor sets per-instance stepping per binding.
  void BindVertexBuffer(GLuint bindingIndex, GLuint buffer,
                        std::uintptr_t offset, GLsizei stride);
  void VertexAttribFormat(GLuint attribIndex, GLint size, GLenum type,
                          GLboolean normalized, GLuint relativeOffset);
  void VertexAttribIFormat(GLuint attribIndex, GLint size, GLenum type,
                           GLuint relativeOffset);
  void VertexAttribBinding(GLuint attribIndex, GLuint bindingIndex);
  void VertexBindingDivisor(GLuint bindingIndex, GLuint divisor);
  void GetVertexAttribfv(GLuint index, GLenum pname, GLfloat* params);
  void GetVertexAttribiv(GLuint index, GLenum pname, GLint* params);
  void GetVertexAttribIiv(GLuint index, GLenum pname, GLint* params);
  void GetVertexAttribIuiv(GLuint index, GLenum pname, GLuint* params);
  void GetVertexAttribPointerv(GLuint index, GLenum pname, void** pointer);

  // ELEMENT_ARRAY_BUFFER binding is VAO state (spec 10.4).
  void SetElementArrayBuffer(GLuint buffer);
  GLuint ElementArrayBuffer() const;
  // Separate-attribute binding state (ES 3.1+): per-VAO binding points.
  VertexBinding BindingState(GLuint bindingIndex) const;

  GenericAttrib AttribState(GLuint index) const;
  GenericAttribValue AttribValue(GLuint index) const;

  // Fixed-index restart value for an index type (spec 10.3.4).
  static GLuint RestartIndexForType(GLenum index_type);

 private:
  struct Array {
    bool alive = false;
    std::array<GenericAttrib, kMaxVertexAttribs> attribs;
    std::array<VertexBinding, kMaxVertexAttribBindings> bindings;
    GLuint element_array_buffer = 0;
  };

  bool ValidIndex(GLuint index) const;
  Array* BoundForWrite();
  bool IsPointerType(GLenum type) const;
  bool IsIntegerPointerType(GLenum type) const;

  ErrorQueue errors_;
  GLuint next_name_ = 1;
  std::map<GLuint, Array> arrays_;
  GLuint bound_ = 0;  // Default VAO, always alive.
  std::array<GenericAttribValue, kMaxVertexAttribs> values_;
};

}  // namespace tgles

#endif  // TGLES_VERTEX_ARRAY_H
