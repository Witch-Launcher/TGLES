#include "tgles/state/buffer.h"

#include <cstring>

namespace tgles {

BufferManager::BufferManager() {
  static const GLenum kTargets[] = {
      kGlArrayBuffer,      kGlAtomicCounterBuffer, kGlCopyReadBuffer,
      kGlCopyWriteBuffer,  kGlDispatchIndirectBuffer, kGlDrawIndirectBuffer,
      kGlElementArrayBuffer, kGlPixelPackBuffer, kGlPixelUnpackBuffer,
      kGlShaderStorageBuffer, kGlTextureBuffer, kGlTransformFeedbackBuffer,
      kGlUniformBuffer,
  };
  for (GLenum t : kTargets) bindings_[t] = 0;
  indexed_[kGlUniformBuffer].resize(kMaxUniformBufferBindings);
  indexed_[kGlShaderStorageBuffer].resize(kMaxShaderStorageBufferBindings);
  indexed_[kGlAtomicCounterBuffer].resize(kMaxAtomicCounterBufferBindings);
  indexed_[kGlTransformFeedbackBuffer].resize(
      kMaxTransformFeedbackSeparateAttribs);
}

GLenum BufferManager::GetError() { return errors_.Get(); }
bool BufferManager::HasPending() const { return errors_.HasPending(); }

bool BufferManager::IsBufferTarget(GLenum target) const {
  return bindings_.find(target) != bindings_.end();
}

bool BufferManager::IsIndexedTarget(GLenum target) const {
  return indexed_.find(target) != indexed_.end();
}

GLint BufferManager::IndexedLimit(GLenum target) const {
  auto it = indexed_.find(target);
  return it == indexed_.end() ? 0
                              : static_cast<GLint>(it->second.size());
}

GLintptr BufferManager::IndexedAlignment(GLenum target) const {
  switch (target) {
    case kGlUniformBuffer: return kUniformBufferOffsetAlignment;
    case kGlShaderStorageBuffer: return kShaderStorageBufferOffsetAlignment;
    default: return 4;
  }
}

bool BufferManager::IsUsage(GLenum usage) const {
  switch (usage) {
    case kGlStreamDraw:
    case kGlStaticDraw:
    case kGlDynamicDraw:
    case kGlStreamRead:
    case kGlStaticRead:
    case kGlDynamicRead:
    case kGlStreamCopy:
    case kGlStaticCopy:
    case kGlDynamicCopy:
      return true;
    default:
      return false;
  }
}

GLuint BufferManager::EnsureName(GLuint name) {
  auto it = buffers_.find(name);
  if (it == buffers_.end() || !it->second.alive) {
    buffers_[name] = Buffer();
    buffers_[name].alive = true;
  }
  return name;
}

void BufferManager::GenBuffers(GLsizei n, GLuint* buffers) {
  if (n < 0 || (n > 0 && buffers == nullptr)) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  for (GLsizei i = 0; i < n; ++i) {
    buffers[i] = next_name_++;
    buffers_[buffers[i]] = Buffer();  // Reserved but not yet a buffer object.
  }
}

void BufferManager::DeleteBuffers(GLsizei n, const GLuint* buffers) {
  if (n < 0 || (n > 0 && buffers == nullptr)) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  for (GLsizei i = 0; i < n; ++i) {
    const GLuint name = buffers[i];
    if (name == 0) continue;
    auto it = buffers_.find(name);
    if (it == buffers_.end() || !it->second.alive) continue;
    it->second = Buffer();  // Frees the store, marks dead.
    for (auto& kv : bindings_) {
      if (kv.second == name) kv.second = 0;
    }
    for (auto& kv : indexed_) {
      for (auto& b : kv.second) {
        if (b.buffer == name) b = IndexedBinding();
      }
    }
  }
}

GLboolean BufferManager::IsBuffer(GLuint buffer) {
  if (buffer == 0) return kGlFalse;
  auto it = buffers_.find(buffer);
  return (it != buffers_.end() && it->second.alive) ? kGlTrue : kGlFalse;
}

void BufferManager::BindBuffer(GLenum target, GLuint buffer) {
  if (!IsBufferTarget(target)) {
    errors_.Record(kGlInvalidEnum);
    return;
  }
  if (buffer != 0) EnsureName(buffer);
  bindings_[target] = buffer;
}

GLuint BufferManager::BoundBuffer(GLenum target) const {
  auto it = bindings_.find(target);
  return it == bindings_.end() ? 0 : it->second;
}

BufferManager::Buffer* BufferManager::BoundForWrite(GLenum target) {
  auto it = bindings_.find(target);
  if (it == bindings_.end()) {
    errors_.Record(kGlInvalidEnum);
    return nullptr;
  }
  if (it->second == 0) {
    errors_.Record(kGlInvalidOperation);
    return nullptr;
  }
  auto bit = buffers_.find(it->second);
  if (bit == buffers_.end() || !bit->second.alive) {
    errors_.Record(kGlInvalidOperation);
    return nullptr;
  }
  return &bit->second;
}

const BufferManager::Buffer* BufferManager::BoundForRead(
    GLenum target) const {
  return const_cast<BufferManager*>(this)->BoundForWrite(target);
}

void BufferManager::BufferData(GLenum target, GLsizeiptr size,
                               const void* data, GLenum usage) {
  if (!IsBufferTarget(target)) {
    errors_.Record(kGlInvalidEnum);
    return;
  }
  if (!IsUsage(usage)) {
    errors_.Record(kGlInvalidEnum);
    return;
  }
  if (size < 0) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  Buffer* buf = BoundForWrite(target);
  if (buf == nullptr) return;
  if (buf->immutable) {
    errors_.Record(kGlInvalidOperation);
    return;
  }
  buf->data.assign(static_cast<std::size_t>(size), 0);
  if (data != nullptr && size > 0) {
    std::memcpy(buf->data.data(), data, static_cast<std::size_t>(size));
  }
  buf->usage = usage;
  buf->mapped = false;
  buf->access_flags = 0;
  buf->storage_flags = 0;
}

void BufferManager::BufferStorageEXT(GLenum target, GLsizeiptr size,
                                     const void* data, GLbitfield flags) {
  if (!IsBufferTarget(target)) {
    errors_.Record(kGlInvalidEnum);
    return;
  }
  constexpr GLbitfield kValid = kGlMapReadBit | kGlMapWriteBit |
                                kGlMapPersistentBit | kGlMapCoherentBit |
                                kGlDynamicStorageBitExt | kGlClientStorageBitExt;
  if ((flags & ~kValid) != 0) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  if (size < 0) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  Buffer* buf = BoundForWrite(target);
  if (buf == nullptr) return;
  if (buf->immutable || !buf->data.empty()) {
    errors_.Record(kGlInvalidOperation);
    return;
  }
  buf->data.assign(static_cast<std::size_t>(size), 0);
  if (data != nullptr && size > 0) {
    std::memcpy(buf->data.data(), data, static_cast<std::size_t>(size));
  }
  buf->immutable = true;
  buf->storage_flags = flags;
  buf->usage = kGlStaticDraw;
  buf->mapped = false;
  buf->access_flags = 0;
}

void BufferManager::FlushMappedBufferRange(GLenum target, GLintptr offset,
                                           GLsizeiptr length) {
  Buffer* buf = BoundForWrite(target);
  if (buf == nullptr) return;
  if (!buf->mapped) {
    errors_.Record(kGlInvalidOperation);
    return;
  }
  if (offset < 0 || length < 0 ||
      offset + length > static_cast<GLintptr>(buf->data.size())) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  if (offset < buf->map_offset ||
      offset + length > buf->map_offset + buf->map_length) {
    errors_.Record(kGlInvalidValue);
    return;
  }
}

void BufferManager::BufferSubData(GLenum target, GLintptr offset,
                                  GLsizeiptr size, const void* data) {
  Buffer* buf = BoundForWrite(target);
  if (buf == nullptr) return;
  if (buf->immutable &&
      (buf->storage_flags & kGlDynamicStorageBitExt) == 0) {
    errors_.Record(kGlInvalidOperation);
    return;
  }
  if (offset < 0 || size < 0 ||
      offset + size > static_cast<GLintptr>(buf->data.size())) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  if (buf->mapped) {
    errors_.Record(kGlInvalidOperation);
    return;
  }
  if (data != nullptr && size > 0) {
    std::memcpy(buf->data.data() + offset, data,
                static_cast<std::size_t>(size));
  }
}

void* BufferManager::MapBufferRange(GLenum target, GLintptr offset,
                                    GLsizeiptr length, GLbitfield access) {
  if (!IsBufferTarget(target)) {
    errors_.Record(kGlInvalidEnum);
    return nullptr;
  }
  constexpr GLbitfield kAllBits = 0xFFu;
  if ((access & ~kAllBits) != 0) {
    errors_.Record(kGlInvalidValue);
    return nullptr;
  }
  Buffer* buf = BoundForWrite(target);
  if (buf == nullptr) return nullptr;
  if (offset < 0 || length < 0 ||
      offset + length > static_cast<GLintptr>(buf->data.size())) {
    errors_.Record(kGlInvalidValue);
    return nullptr;
  }
  if (buf->mapped) {
    errors_.Record(kGlInvalidOperation);
    return nullptr;
  }
  if ((access & (kGlMapReadBit | kGlMapWriteBit)) == 0) {
    errors_.Record(kGlInvalidOperation);
    return nullptr;
  }
  buf->mapped = true;
  buf->map_offset = offset;
  buf->map_length = length;
  buf->access_flags = access;
  return buf->data.data() + offset;
}

GLboolean BufferManager::UnmapBuffer(GLenum target) {
  Buffer* buf = BoundForWrite(target);
  if (buf == nullptr) return kGlFalse;
  if (!buf->mapped) {
    errors_.Record(kGlInvalidOperation);
    return kGlFalse;
  }
  buf->mapped = false;
  return kGlTrue;
}

void BufferManager::CopyBufferSubData(GLenum read_target, GLenum write_target,
                                      GLintptr read_offset,
                                      GLintptr write_offset, GLsizeiptr size) {
  const Buffer* src = BoundForRead(read_target);
  if (src == nullptr) return;
  Buffer* dst = (read_target == write_target)
                    ? const_cast<Buffer*>(src)
                    : BoundForWrite(write_target);
  if (dst == nullptr) return;
  if (read_offset < 0 || write_offset < 0 || size < 0 ||
      read_offset + size > static_cast<GLintptr>(src->data.size()) ||
      write_offset + size > static_cast<GLintptr>(dst->data.size())) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  if (src->mapped || dst->mapped) {
    errors_.Record(kGlInvalidOperation);
    return;
  }
  std::memmove(dst->data.data() + write_offset,
               src->data.data() + read_offset,
               static_cast<std::size_t>(size));
}

void BufferManager::GetBufferParameteriv(GLenum target, GLenum pname,
                                         GLint* params) {
  if (params == nullptr) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  const Buffer* buf = BoundForRead(target);
  if (buf == nullptr) return;
  switch (pname) {
    case kGlBufferSize:
      *params = static_cast<GLint>(buf->data.size());
      return;
    case kGlBufferUsage:
      *params = static_cast<GLint>(buf->usage);
      return;
    case kGlBufferAccessFlags:
      *params = static_cast<GLint>(buf->access_flags);
      return;
    case kGlBufferMapped:
      *params = buf->mapped ? 1 : 0;
      return;
    case kGlBufferMapOffset:
      *params = static_cast<GLint>(buf->map_offset);
      return;
    case kGlBufferMapLength:
      *params = static_cast<GLint>(buf->map_length);
      return;
    case kGlBufferImmutable:
      *params = buf->immutable ? 1 : 0;
      return;
    case kGlBufferStorageFlagsExt:
      *params = static_cast<GLint>(buf->storage_flags);
      return;
    default:
      break;
  }
  errors_.Record(kGlInvalidEnum);
}

void BufferManager::GetBufferPointerv(GLenum target, GLenum pname,
                                      void** params) {
  // docs.gl/es3: target must be a buffer target, pname must be
  // BUFFER_MAP_POINTER (0x88BD, gl32.h:692). Verified against Khronos headers.
  static constexpr GLenum kBufferMapPointer = 0x88BDu;
  if (params == nullptr) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  if (!IsBufferTarget(target) || pname != kBufferMapPointer) {
    errors_.Record(kGlInvalidEnum);
    return;
  }
  auto it = bindings_.find(target);
  if (it == bindings_.end() || it->second == 0) {
    errors_.Record(kGlInvalidOperation);
    return;
  }
  auto bit = buffers_.find(it->second);
  if (bit == buffers_.end() || !bit->second.alive) {
    errors_.Record(kGlInvalidOperation);
    return;
  }
  Buffer& buf = bit->second;
  if (!buf.mapped) {
    *params = nullptr;
    return;
  }
  *params = static_cast<void*>(buf.data.data() + buf.map_offset);
}

void BufferManager::BindBufferBase(GLenum target, GLuint index,
                                   GLuint buffer) {
  if (!IsIndexedTarget(target)) {
    errors_.Record(kGlInvalidEnum);
    return;
  }
  if (index >= static_cast<GLuint>(IndexedLimit(target))) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  if (buffer != 0) {
    if (IsBuffer(buffer) == kGlFalse && buffers_.count(buffer) != 0u &&
        !buffers_[buffer].alive) {
      // Re-binding a deleted name recreates the object.
    }
    EnsureName(buffer);
  }
  IndexedBinding& slot = indexed_[target][index];
  slot.buffer = buffer;
  slot.offset = 0;
  slot.size = (buffer == 0) ? 0 : -1;  // -1 marks whole-buffer binding.
}

void BufferManager::BindBufferRange(GLenum target, GLuint index, GLuint buffer,
                                    GLintptr offset, GLsizeiptr size) {
  if (!IsIndexedTarget(target)) {
    errors_.Record(kGlInvalidEnum);
    return;
  }
  if (index >= static_cast<GLuint>(IndexedLimit(target))) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  if (buffer == 0) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  const GLintptr alignment = IndexedAlignment(target);
  if (offset < 0 || size <= 0 || (offset % alignment) != 0) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  EnsureName(buffer);
  const Buffer& buf = buffers_[buffer];
  if (offset + size > static_cast<GLintptr>(buf.data.size())) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  IndexedBinding& slot = indexed_[target][index];
  slot.buffer = buffer;
  slot.offset = offset;
  slot.size = size;
}

IndexedBinding BufferManager::IndexedBindingState(GLenum target,
                                                  GLuint index) const {
  auto it = indexed_.find(target);
  if (it == indexed_.end() ||
      index >= static_cast<GLuint>(it->second.size())) {
    return IndexedBinding();
  }
  return it->second[index];
}

GLsizeiptr BufferManager::BufferSize(GLuint buffer) const {
  auto it = buffers_.find(buffer);
  if (it == buffers_.end() || !it->second.alive) return -1;
  return static_cast<GLsizeiptr>(it->second.data.size());
}

bool BufferManager::IsMapped(GLuint buffer) const {
  auto it = buffers_.find(buffer);
  return it != buffers_.end() && it->second.alive && it->second.mapped;
}

bool BufferManager::IsImmutable(GLuint buffer) const {
  auto it = buffers_.find(buffer);
  return it != buffers_.end() && it->second.alive && it->second.immutable;
}

bool BufferManager::GetBufferBytes(GLuint buffer, const std::uint8_t** data,
                                   GLsizeiptr* size) const {
  auto it = buffers_.find(buffer);
  if (it == buffers_.end() || !it->second.alive) return false;
  if (data != nullptr) *data = it->second.data.data();
  if (size != nullptr) {
    *size = static_cast<GLsizeiptr>(it->second.data.size());
  }
  return true;
}

}  // namespace tgles
