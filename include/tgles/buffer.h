#ifndef TGLES_BUFFER_H
#define TGLES_BUFFER_H

// Buffer objects (spec chapter 6): creation, binding, data store,
// mapping, copying, indexed bindings and queries.

#include <cstddef>
#include <cstdint>
#include <map>
#include <vector>

#include "tgles/error.h"
#include "tgles/gl_types.h"

namespace tgles {

// Buffer binding targets (spec Table 6.1, 13 total).
inline constexpr GLenum kGlArrayBuffer = 0x8892;
inline constexpr GLenum kGlAtomicCounterBuffer = 0x92C0;
inline constexpr GLenum kGlCopyReadBuffer = 0x8F36;
inline constexpr GLenum kGlCopyWriteBuffer = 0x8F37;
inline constexpr GLenum kGlDispatchIndirectBuffer = 0x90EE;
inline constexpr GLenum kGlDrawIndirectBuffer = 0x8F3F;
inline constexpr GLenum kGlElementArrayBuffer = 0x8893;
inline constexpr GLenum kGlPixelPackBuffer = 0x88EB;
inline constexpr GLenum kGlPixelUnpackBuffer = 0x88EC;
inline constexpr GLenum kGlShaderStorageBuffer = 0x90D2;
inline constexpr GLenum kGlTextureBuffer = 0x8C2A;
inline constexpr GLenum kGlTransformFeedbackBuffer = 0x8C8E;
inline constexpr GLenum kGlUniformBuffer = 0x8A11;

// Usage hints (9 total).
inline constexpr GLenum kGlStreamDraw = 0x88E0;
inline constexpr GLenum kGlStaticDraw = 0x88E4;
inline constexpr GLenum kGlDynamicDraw = 0x88E8;
inline constexpr GLenum kGlStreamRead = 0x88E1;
inline constexpr GLenum kGlStaticRead = 0x88E5;
inline constexpr GLenum kGlDynamicRead = 0x88E9;
inline constexpr GLenum kGlStreamCopy = 0x88E2;
inline constexpr GLenum kGlStaticCopy = 0x88E6;
inline constexpr GLenum kGlDynamicCopy = 0x88EA;

// Map access bits (spec 6.3).
inline constexpr GLbitfield kGlMapReadBit = 0x0001;
inline constexpr GLbitfield kGlMapWriteBit = 0x0002;
inline constexpr GLbitfield kGlMapInvalidateRangeBit = 0x0004;
inline constexpr GLbitfield kGlMapInvalidateBufferBit = 0x0008;
inline constexpr GLbitfield kGlMapFlushExplicitBit = 0x0010;
inline constexpr GLbitfield kGlMapUnsynchronizedBit = 0x0020;
inline constexpr GLbitfield kGlMapPersistentBit = 0x0040;
inline constexpr GLbitfield kGlMapCoherentBit = 0x0080;

// Buffer parameter queries (spec Table 6.2).
inline constexpr GLenum kGlBufferSize = 0x8764;
inline constexpr GLenum kGlBufferUsage = 0x8765;
inline constexpr GLenum kGlBufferAccessFlags = 0x911F;
inline constexpr GLenum kGlBufferMapped = 0x88A6;
inline constexpr GLenum kGlBufferMapOffset = 0x9121;
inline constexpr GLenum kGlBufferMapLength = 0x9120;

// Indexed binding limits reported by TGL (all >= spec minimums).
inline constexpr GLint kMaxUniformBufferBindings = 72;  // Spec minimum: 72.
inline constexpr GLint kMaxShaderStorageBufferBindings = 8;
inline constexpr GLint kMaxAtomicCounterBufferBindings = 8;
inline constexpr GLint kMaxTransformFeedbackSeparateAttribs = 4;  // Spec min.

// EXT_buffer_storage flags (docs/reference/glext.h, NOT core gl32.h).
// Only valid on buffers created via BufferStorageEXT.
inline constexpr GLbitfield kGlDynamicStorageBitExt = 0x0100;
inline constexpr GLbitfield kGlClientStorageBitExt = 0x0200;
inline constexpr GLenum kGlBufferStorageFlagsExt = 0x8220;
inline constexpr GLenum kGlBufferImmutable = 0x821F;

// Binding offset alignments.
inline constexpr GLintptr kUniformBufferOffsetAlignment = 16;
inline constexpr GLintptr kShaderStorageBufferOffsetAlignment = 16;

struct IndexedBinding {
  GLuint buffer = 0;
  GLintptr offset = 0;
  GLsizeiptr size = 0;
};

class BufferManager {
 public:
  BufferManager();

  GLenum GetError();
  bool HasPending() const;

  void GenBuffers(GLsizei n, GLuint* buffers);
  void DeleteBuffers(GLsizei n, const GLuint* buffers);
  GLboolean IsBuffer(GLuint buffer);

  void BindBuffer(GLenum target, GLuint buffer);
  GLuint BoundBuffer(GLenum target) const;

  void BufferData(GLenum target, GLsizeiptr size, const void* data,
                  GLenum usage);
  void BufferSubData(GLenum target, GLintptr offset, GLsizeiptr size,
                     const void* data);
  void* MapBufferRange(GLenum target, GLintptr offset, GLsizeiptr length,
                       GLbitfield access);
  GLboolean UnmapBuffer(GLenum target);
  void CopyBufferSubData(GLenum read_target, GLenum write_target,
                         GLintptr read_offset, GLintptr write_offset,
                         GLsizeiptr size);
  void GetBufferParameteriv(GLenum target, GLenum pname, GLint* params);

  void BindBufferBase(GLenum target, GLuint index, GLuint buffer);
  void BindBufferRange(GLenum target, GLuint index, GLuint buffer,
                       GLintptr offset, GLsizeiptr size);
  IndexedBinding IndexedBindingState(GLenum target, GLuint index) const;

  // EXT_buffer_storage (REQUIRED by MobileGL loader for persistent rings).
  void BufferStorageEXT(GLenum target, GLsizeiptr size, const void* data,
                        GLbitfield flags);
  void FlushMappedBufferRange(GLenum target, GLintptr offset,
                              GLsizeiptr length);

  // Test helpers.
  GLsizeiptr BufferSize(GLuint buffer) const;
  bool IsMapped(GLuint buffer) const;
  bool IsImmutable(GLuint buffer) const;
  // Read access to a buffer store for facade uploads (e.g. RenderFrame
  // vertex decoding). Returns false for unknown/dead buffers; the pointer
  // stays valid until the next mutation of that buffer.
  bool GetBufferBytes(GLuint buffer, const std::uint8_t** data,
                      GLsizeiptr* size) const;

 private:
  struct Buffer {
    bool alive = false;
    std::vector<std::uint8_t> data;
    GLenum usage = kGlStaticDraw;
    GLbitfield access_flags = 0;
    bool mapped = false;
    GLintptr map_offset = 0;
    GLsizeiptr map_length = 0;
    bool immutable = false;
    GLbitfield storage_flags = 0;
  };

  bool IsBufferTarget(GLenum target) const;
  bool IsIndexedTarget(GLenum target) const;
  GLint IndexedLimit(GLenum target) const;
  GLintptr IndexedAlignment(GLenum target) const;
  bool IsUsage(GLenum usage) const;
  Buffer* BoundForWrite(GLenum target);
  const Buffer* BoundForRead(GLenum target) const;
  GLuint EnsureName(GLuint name);

  ErrorQueue errors_;
  GLuint next_name_ = 1;
  std::map<GLuint, Buffer> buffers_;
  std::map<GLenum, GLuint> bindings_;
  std::map<GLenum, std::vector<IndexedBinding>> indexed_;
};

}  // namespace tgles

#endif  // TGLES_BUFFER_H