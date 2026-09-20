#ifndef TGLES_VERTEX_ARRAY_H
#define TGLES_VERTEX_ARRAY_H

// Vertex arrays and VAOs (spec sections 10.3-10.4): generic attribute state,
// separate attribute format/binding state (ES 3.1+), divisors, VAO container
// semantics (ELEMENT_ARRAY_BUFFER binding is VAO state).

#include <array>
#include <cstdint>
#include <map>

#include "tgles/error.h"
#include "tgles/gl_types.h"

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
  bool pure_integer = false;  // Set by VertexAttribIPointer.
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

  // ELEMENT_ARRAY_BUFFER binding is VAO state (spec 10.4).
  void SetElementArrayBuffer(GLuint buffer);
  GLuint ElementArrayBuffer() const;

  GenericAttrib AttribState(GLuint index) const;
  GenericAttribValue AttribValue(GLuint index) const;

  // Fixed-index restart value for an index type (spec 10.3.4).
  static GLuint RestartIndexForType(GLenum index_type);

 private:
  struct Array {
    bool alive = false;
    std::array<GenericAttrib, kMaxVertexAttribs> attribs;
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
