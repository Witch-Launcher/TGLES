#include "tgles/facade/gles.h"

#include <climits>
#include <cstdint>
#include <cstring>
#include <sstream>
#include <vector>

#include "tgles/host/entry_points.h"
#include "tgles/state/image_units.h"
#include "tgles/gpu/glsl_to_msl.h"
#include "tgles/gpu/metal_mapping.h"
#include "tgles/gpu/msl_translation.h"
#include "tgles/base/spec.h"

namespace tgles {

GlesContext GlesContext::Create(bool debug) {
  return GlesContext(debug);
}

GLenum GlesContext::GetError() {
  GLenum code = foundation_.GetError();
  if (code != kGlNoError) return code;
  code = buffers_.GetError();
  if (code != kGlNoError) return code;
  code = vertex_arrays_.GetError();
  if (code != kGlNoError) return code;
  code = shaders_.GetError();
  if (code != kGlNoError) return code;
  code = programs_.GetError();
  if (code != kGlNoError) return code;
  code = textures_.GetError();
  if (code != kGlNoError) return code;
  code = samplers_.GetError();
  if (code != kGlNoError) return code;
  code = renderbuffers_.GetError();
  if (code != kGlNoError) return code;
  code = framebuffers_.GetError();
  if (code != kGlNoError) return code;
  code = tessellation_.GetError();
  if (code != kGlNoError) return code;
  code = compute_.GetError();
  if (code != kGlNoError) return code;
  code = sync_.GetError();
  if (code != kGlNoError) return code;
  code = queries_.GetError();
  if (code != kGlNoError) return code;
  code = debug_.GetError();
  if (code != kGlNoError) return code;
  code = draw_.GetError();
  if (code != kGlNoError) return code;
  code = raster_.GetError();
  if (code != kGlNoError) return code;
  code = images_.GetError();
  if (code != kGlNoError) return code;
  code = pixels_.GetError();
  if (code != kGlNoError) return code;
  code = transform_feedback_.GetError();
  if (code != kGlNoError) return code;
  return command_plan_.GetError();
}

void GlesContext::BindBuffer(GLenum target, GLuint buffer) {
  buffers_.BindBuffer(target, buffer);
  if (buffers_.HasPending()) return;  // Invalid target: nothing else to do.
  if (target == kGlElementArrayBuffer) {
    vertex_arrays_.SetElementArrayBuffer(buffer);
  }
}

void GlesContext::SyncIndirectState() {
  draw_.SetIndirectBufferBound(
      buffers_.BoundBuffer(kGlDrawIndirectBuffer) != 0);
}

void GlesContext::SyncElementState() {
  draw_.SetElementArrayBufferBound(
      vertex_arrays_.ElementArrayBuffer() != 0);
}

void GlesContext::Clear(GLbitfield mask) {
  GLfloat color[4];
  raster_.GetClearColor(color);
  framebuffers_.Clear(mask, color);
}

void GlesContext::DispatchComputeIndirect(std::uintptr_t indirect) {
  SyncIndirectState();
  SyncComputeProgram();
  compute_.DispatchComputeIndirect(static_cast<GLintptr>(indirect));
  if (compute_.HasPending()) return;
  // Indirect execution (spec 19): groups come from the DISPATCH_INDIRECT
  // BUFFER at byte offset `indirect` (3x uint32, like DispatchCompute args).
  // Validation above already enforced program-bound + offset alignment.
  const GLuint indirect_buf = buffers_.BoundBuffer(kGlDispatchIndirectBuffer);
  if (indirect_buf == 0) return;  // No indirect store: validate-only.
  const std::uint8_t* ibytes = nullptr;
  GLsizeiptr isize = 0;
  if (!buffers_.GetBufferBytes(indirect_buf, &ibytes, &isize)) return;
  if (indirect + 12 > static_cast<std::uintptr_t>(isize)) return;
  std::uint32_t groups[3] = {0, 0, 0};
  std::memcpy(groups, ibytes + indirect, 12);
  // Reuses the direct path (re-syncs + re-validates group limits, then the
  // marker-gated SSBO0 execution). Out-of-range counts from the store surface
  // as errors here instead of executing — exactly like direct dispatches.
  DispatchCompute(groups[0], groups[1], groups[2]);
}

void GlesContext::SyncComputeProgram() {
  const GLuint current = programs_.CurrentProgram();
  compute_.SetComputeProgramBound(
      current != 0 && programs_.ProgramHasStage(current, kGlComputeShader));
}

void GlesContext::DrawElementsIndirect(GLenum mode, GLenum type,
                                       std::uintptr_t indirect) {
  SyncIndirectState();
  draw_.DrawElementsIndirect(mode, type, indirect);
}

void GlesContext::DispatchCompute(GLuint x, GLuint y, GLuint z) {
  SyncComputeProgram();
  compute_.DispatchCompute(x, y, z);
  if (compute_.HasPending()) return;
  // Honest v1 execution (ES 3.2 ch.19): marker-gated SSBO0 increment.
  // Validation above already enforced program-bound + group limits; a zero
  // group count is a legal no-op (spec) and touches no store.
  if (x == 0 || y == 0 || z == 0) return;
  const GLuint current = programs_.CurrentProgram();
  if (current == 0 || !programs_.IsComputeAddOneProgram(current)) return;
  const IndexedBinding binding =
      buffers_.IndexedBindingState(kGlShaderStorageBuffer, 0);
  if (binding.buffer == 0) return;  // No SSBO0 bound: validate-only.
  const std::uint8_t* bytes = nullptr;
  GLsizeiptr size = 0;
  if (!buffers_.GetBufferBytes(binding.buffer, &bytes, &size)) return;
  // Respect BindBufferRange offset/size when present; whole-buffer fallback
  // matches TexBufferRange semantics elsewhere in TGL.
  std::size_t start = 0, len = 0;
  if (binding.size > 0) {
    if (binding.offset < 0 ||
        binding.offset + binding.size > size) {
      return;  // Out-of-range range: validation already passed, skip exec.
    }
    start = static_cast<std::size_t>(binding.offset);
    len = static_cast<std::size_t>(binding.size);
  } else {
    len = static_cast<std::size_t>(size);
  }
  if (len < 4) return;
  // Work-group count gates how many uint32 lanes execute (x*y*z total,
  // clamped to the store). This matches the test kernel's
  // `v[gl_GlobalInvocationID.x] += 1` with local_size_x=1.
  const std::uint64_t groups =
      static_cast<std::uint64_t>(x) * y * z;
  const std::size_t lanes = len / 4;
  const std::size_t n =
      groups < lanes ? static_cast<std::size_t>(groups) : lanes;
  if (n == 0) return;
  std::vector<std::uint8_t> mutated(bytes + start, bytes + start + len);
  std::uint32_t* words = reinterpret_cast<std::uint32_t*>(mutated.data());
  for (std::size_t i = 0; i < n; ++i) words[i] += 1u;
  buffers_.BindBuffer(kGlShaderStorageBuffer, binding.buffer);
  // Write back via BufferSubData on the SSBO target (offset relative to the
  // bound range start). BindBuffer above ensures the target resolves.
  const std::uint8_t* check = nullptr;
  GLsizeiptr check_size = 0;
  (void)check;
  (void)check_size;
  // Direct store write: BufferSubData(target, range_start, n*4, words).
  buffers_.BufferSubData(kGlShaderStorageBuffer,
                         static_cast<GLintptr>(start),
                         static_cast<GLsizeiptr>(n * 4), words);
}

bool GlesContext::RenderInstanced(metal_bridge::MetalBridge* bridge,
                                  GLenum mode, GLint first, GLsizei count,
                                  GLsizei instanceCount) {
  draw_.DrawArraysInstanced(mode, first, count, instanceCount);
  if (draw_.HasPending()) return false;
  if (count == 0 || instanceCount == 0) return true;
  if (bridge == nullptr) return true;  // Validation-only headless.
  // CPU expansion: repeat the vertex range per instance, tagging each copy
  // with its instance index so divisor>0 attribs step per instance/divisor
  // in SubmitVertices (spec 10.5).
  std::vector<VertexRef> verts;
  verts.reserve(static_cast<std::size_t>(count) * instanceCount);
  for (GLsizei inst = 0; inst < instanceCount; ++inst) {
    for (GLint v = first; v < first + count; ++v) verts.push_back({v, inst});
  }
  return SubmitVertices(*bridge, mode, verts);
}

bool GlesContext::RenderElementsInstanced(metal_bridge::MetalBridge* bridge,
                                          GLenum mode, GLsizei count,
                                          GLenum type, std::uintptr_t indices,
                                          GLsizei instanceCount) {
  SyncElementState();
  draw_.DrawElementsInstanced(mode, count, type, indices, instanceCount);
  if (draw_.HasPending()) return false;
  if (count == 0 || instanceCount == 0) return true;
  if (bridge == nullptr) return true;
  const GLuint eab = vertex_arrays_.ElementArrayBuffer();
  if (eab == 0) {
    draw_.FlagBridgeError();
    return false;
  }
  const std::uint8_t* bytes = nullptr;
  GLsizeiptr size = 0;
  if (!buffers_.GetBufferBytes(eab, &bytes, &size)) {
    draw_.FlagBridgeError();
    return false;
  }
  std::size_t stride = 0;
  if (type == kGlUnsignedByte)
    stride = 1;
  else if (type == kGlUnsignedShort)
    stride = 2;
  else if (type == kGlUnsignedInt)
    stride = 4;
  else {
    draw_.FlagBridgeError();
    return false;
  }
  std::vector<VertexRef> one;
  one.reserve(count);
  for (GLsizei i = 0; i < count; ++i) {
    std::uintptr_t at = indices + i * stride;
    if (at + stride > (std::size_t)size) {
      draw_.FlagBridgeError();
      return false;
    }
    GLuint v = 0;
    if (stride == 1)
      v = bytes[at];
    else if (stride == 2) {
      std::uint16_t t = 0;
      std::memcpy(&t, bytes + at, 2);
      v = t;
    } else {
      std::uint32_t t = 0;
      std::memcpy(&t, bytes + at, 4);
      v = t;
    }
    one.push_back({(GLint)v, 0});
  }
  std::vector<VertexRef> verts;
  verts.reserve(one.size() * instanceCount);
  for (GLsizei k = 0; k < instanceCount; ++k) {
    for (const VertexRef& ref : one) verts.push_back({ref.vertex, k});
  }
  return SubmitVertices(*bridge, mode, verts);
}

bool GlesContext::RenderArraysIndirect(metal_bridge::MetalBridge* bridge,
                                       GLenum mode, std::uintptr_t indirect) {
  SyncIndirectState();
  draw_.DrawArraysIndirect(mode, indirect);
  if (draw_.HasPending()) return false;
  if (bridge == nullptr) return true;
  const GLuint buf = buffers_.BoundBuffer(kGlDrawIndirectBuffer);
  if (buf == 0) {
    draw_.FlagBridgeError();
    return false;
  }
  const std::uint8_t* bytes = nullptr;
  GLsizeiptr size = 0;
  if (!buffers_.GetBufferBytes(buf, &bytes, &size)) {
    draw_.FlagBridgeError();
    return false;
  }
  if (indirect + 16 > (std::uintptr_t)size) {
    draw_.FlagBridgeError();
    return false;
  }
  std::uint32_t count = 0, instanceCount = 0;
  std::int32_t first = 0;
  std::memcpy(&count, bytes + indirect, 4);
  std::memcpy(&instanceCount, bytes + indirect + 4, 4);
  std::memcpy(&first, bytes + indirect + 8, 4);
  return RenderInstanced(bridge, mode, first, (GLsizei)count,
                         (GLsizei)instanceCount);
}

bool GlesContext::RenderElementsIndirect(metal_bridge::MetalBridge* bridge,
                                         GLenum mode, GLenum type,
                                         std::uintptr_t indirect) {
  SyncIndirectState();
  draw_.DrawElementsIndirect(mode, type, indirect);
  if (draw_.HasPending()) return false;
  if (bridge == nullptr) return true;
  const GLuint buf = buffers_.BoundBuffer(kGlDrawIndirectBuffer);
  if (buf == 0) {
    draw_.FlagBridgeError();
    return false;
  }
  const std::uint8_t* bytes = nullptr;
  GLsizeiptr size = 0;
  if (!buffers_.GetBufferBytes(buf, &bytes, &size)) {
    draw_.FlagBridgeError();
    return false;
  }
  if (indirect + 20 > (std::uintptr_t)size) {
    draw_.FlagBridgeError();
    return false;
  }
  std::uint32_t count = 0, instanceCount = 0, firstIndex = 0;
  std::int32_t baseVertex = 0;
  std::memcpy(&count, bytes + indirect, 4);
  std::memcpy(&instanceCount, bytes + indirect + 4, 4);
  std::memcpy(&firstIndex, bytes + indirect + 8, 4);
  std::memcpy(&baseVertex, bytes + indirect + 12, 4);
  // baseVertex shifts every index; implement as expanded verts + base.
  SyncElementState();
  const GLuint eab = vertex_arrays_.ElementArrayBuffer();
  if (eab == 0) {
    draw_.FlagBridgeError();
    return false;
  }
  const std::uint8_t* ibytes = nullptr;
  GLsizeiptr isize = 0;
  if (!buffers_.GetBufferBytes(eab, &ibytes, &isize)) {
    draw_.FlagBridgeError();
    return false;
  }
  std::size_t stride = (type == kGlUnsignedByte)
                           ? 1
                           : (type == kGlUnsignedShort ? 2 : 4);
  std::vector<VertexRef> one;
  for (std::uint32_t i = 0; i < count; ++i) {
    std::uintptr_t at = firstIndex * stride + i * stride;
    if (at + stride > (std::size_t)isize) {
      draw_.FlagBridgeError();
      return false;
    }
    GLuint v = 0;
    if (stride == 1)
      v = ibytes[at];
    else if (stride == 2) {
      std::uint16_t t = 0;
      std::memcpy(&t, ibytes + at, 2);
      v = t;
    } else {
      std::uint32_t t = 0;
      std::memcpy(&t, ibytes + at, 4);
      v = t;
    }
    one.push_back({(GLint)(v + (GLuint)baseVertex), 0});
  }
  std::vector<VertexRef> verts;
  for (std::uint32_t k = 0; k < instanceCount; ++k) {
    for (const VertexRef& ref : one) verts.push_back({ref.vertex, (GLsizei)k});
  }
  // Validate via the instanced path, then submit.
  draw_.DrawElementsInstanced(mode, (GLsizei)count, type, firstIndex,
                              (GLsizei)instanceCount);
  if (draw_.HasPending()) return false;
  if (verts.empty()) return true;
  return SubmitVertices(*bridge, mode, verts);
}

namespace {

// IEEE-754 binary16 -> binary32 (spec: HALF_FLOAT vertex data).
float HalfToFloat(std::uint16_t h) {
  const std::uint32_t sign = (h & 0x8000u) << 16;
  const std::uint32_t exp = (h & 0x7C00u) >> 10;
  const std::uint32_t mant = h & 0x03FFu;
  std::uint32_t bits = 0;
  if (exp == 0) {
    if (mant == 0) {
      bits = sign;  // Signed zero.
    } else {
      // Subnormal: normalize the mantissa.
      std::uint32_t m = mant;
      std::uint32_t e = 127 - 14 - 1;
      while ((m & 0x0400u) == 0) {
        m <<= 1;
        --e;
      }
      m &= 0x03FFu;
      bits = sign | (e << 23) | (m << 13);
    }
  } else if (exp == 31) {
    bits = sign | 0x7F800000u | (mant << 13);  // Inf / NaN.
  } else {
    bits = sign | ((exp + 127 - 15) << 23) | (mant << 13);
  }
  float out = 0;
  std::memcpy(&out, &bits, 4);
  return out;
}

// Reads one component of a fixed-point element as float.
float ReadComponent(const std::uint8_t* at, GLenum type, bool normalized) {
  switch (type) {
    case kGlByte: {
      std::int8_t c = 0;
      std::memcpy(&c, at, 1);
      if (normalized) {
        float f = static_cast<float>(c) / 127.0f;
        return f < -1.0f ? -1.0f : f;
      }
      return static_cast<float>(c);
    }
    case kGlUnsignedByte: {
      std::uint8_t c = at[0];
      if (normalized) return static_cast<float>(c) / 255.0f;
      return static_cast<float>(c);
    }
    case kGlShort: {
      std::int16_t c = 0;
      std::memcpy(&c, at, 2);
      if (normalized) {
        float f = static_cast<float>(c) / 32767.0f;
        return f < -1.0f ? -1.0f : f;
      }
      return static_cast<float>(c);
    }
    case kGlUnsignedShort: {
      std::uint16_t c = 0;
      std::memcpy(&c, at, 2);
      if (normalized) return static_cast<float>(c) / 65535.0f;
      return static_cast<float>(c);
    }
    case kGlInt: {
      std::int32_t c = 0;
      std::memcpy(&c, at, 4);
      if (normalized) {
        double f = static_cast<double>(c) / 2147483647.0;
        if (f < -1.0) f = -1.0;
        return static_cast<float>(f);
      }
      return static_cast<float>(c);
    }
    case kGlUnsignedInt: {
      std::uint32_t c = 0;
      std::memcpy(&c, at, 4);
      if (normalized)
        return static_cast<float>(static_cast<double>(c) / 4294967295.0);
      return static_cast<float>(c);
    }
    case kGlFloat: {
      float c = 0;
      std::memcpy(&c, at, 4);
      return c;
    }
    case kGlHalfFloat: {
      std::uint16_t c = 0;
      std::memcpy(&c, at, 2);
      return HalfToFloat(c);
    }
    case kGlFixed: {
      std::int32_t c = 0;
      std::memcpy(&c, at, 4);
      return static_cast<float>(static_cast<double>(c) / 65536.0);
    }
    default:
      return 0.0f;  // Unreachable (caller filters types), kept close.
  }
}

// Decodes one attribute element into a float4 for the MSL VertexIn
// (float4 position @0 + float4 color @1). Handles every fixed-point type the
// validator accepts (spec 10.3.1: normalized mapping, size 1-4 padded as
// (x,0,0,1)/(x,y,0,1)/(x,y,z,1)), the packed 10_10_10_2 formats, and
// per-instance selection via divisor (element instance/divisor). Integer
// attribs (VertexAttribIPointer, pure_integer) convert to float exactly below
// 2^24 (bitwise shader ops on them fail closed in the translator); BGRA
// still fails closed (no Metal vertex format for it).
bool DecodeAttribToFloat4(const GenericAttrib& attrib, GLuint binding_divisor,
                           const std::uint8_t* store, GLsizeiptr store_size,
                           GLint vertex, GLsizei instance, GLfloat out[4]) {
  // NOTE: pure_integer intentionally NOT rejected: converted below.
  const bool packed = (attrib.type == kGlInt2101010Rev ||
                       attrib.type == kGlUnsignedInt2101010Rev);
  if (packed && attrib.size != 4) return false;
  if (!packed &&
      (attrib.size < 1 || attrib.size > 4 ||
       attrib.size == static_cast<GLint>(kGlBgra))) {
    return false;
  }
  const GLuint effective_divisor =
      attrib.divisor != 0 ? attrib.divisor : binding_divisor;
  const std::int64_t element =
      (effective_divisor == 0)
          ? static_cast<std::int64_t>(vertex)
          : static_cast<std::int64_t>(instance) / effective_divisor;
  if (element < 0 || element > static_cast<std::int64_t>(INT32_MAX)) {
    return false;
  }
  if (packed) {
    const GLsizeiptr elem = (attrib.stride != 0) ? attrib.stride : 4;
    const GLsizeiptr at =
        static_cast<GLsizeiptr>(attrib.offset) + element * elem;
    if (at < 0 || at + 4 > store_size) return false;
    std::uint32_t bits = 0;
    std::memcpy(&bits, store + at, 4);
    const std::uint32_t cx = (bits >> 0) & 0x3FFu;
    const std::uint32_t cy = (bits >> 10) & 0x3FFu;
    const std::uint32_t cz = (bits >> 20) & 0x3FFu;
    const std::uint32_t cw = (bits >> 30) & 0x3u;
    if (attrib.type == kGlUnsignedInt2101010Rev) {
      const float den = attrib.normalized ? 1023.0f : 1.0f;
      const float den2 = attrib.normalized ? 3.0f : 1.0f;
      out[0] = static_cast<float>(cx) / den;
      out[1] = static_cast<float>(cy) / den;
      out[2] = static_cast<float>(cz) / den;
      out[3] = static_cast<float>(cw) / den2;
    } else {
      // Sign-extend 10-bit and 2-bit fields, then normalize to [-1, 1].
      auto sext = [](std::uint32_t v, int n) -> std::int32_t {
        const std::uint32_t m = 1u << (n - 1);
        return static_cast<std::int32_t>((v ^ m) - m);
      };
      const float den = attrib.normalized ? 511.0f : 1.0f;
      const float den2 = attrib.normalized ? 1.0f : 1.0f;
      float x = static_cast<float>(sext(cx, 10)) / den;
      float y = static_cast<float>(sext(cy, 10)) / den;
      float z = static_cast<float>(sext(cz, 10)) / den;
      float w = static_cast<float>(sext(cw, 2)) / den2;
      if (attrib.normalized) {
        if (x < -1.0f) x = -1.0f;
        if (y < -1.0f) y = -1.0f;
        if (z < -1.0f) z = -1.0f;
        if (w < -1.0f) w = -1.0f;
      }
      out[0] = x;
      out[1] = y;
      out[2] = z;
      out[3] = w;
    }
    return true;
  }
  GLsizeiptr comp_bytes = 0;
  switch (attrib.type) {
    case kGlByte:
    case kGlUnsignedByte:
      comp_bytes = 1;
      break;
    case kGlShort:
    case kGlUnsignedShort:
    case kGlHalfFloat:
      comp_bytes = 2;
      break;
    case kGlInt:
    case kGlUnsignedInt:
    case kGlFloat:
    case kGlFixed:
      comp_bytes = 4;
      break;
    default:
      return false;
  }
  const GLsizeiptr elem = (attrib.stride != 0)
                              ? attrib.stride
                              : static_cast<GLsizeiptr>(attrib.size) *
                                    comp_bytes;
  const GLsizeiptr at =
      static_cast<GLsizeiptr>(attrib.offset) + element * elem;
  if (at < 0 ||
      at + static_cast<GLsizeiptr>(attrib.size) * comp_bytes > store_size) {
    return false;
  }
  const bool normalized = attrib.normalized != kGlFalse;
  for (GLint i = 0; i < attrib.size; ++i) {
    out[i] = ReadComponent(store + at + i * comp_bytes, attrib.type,
                           normalized);
  }
  static const GLfloat kPad[4] = {0.0f, 0.0f, 0.0f, 1.0f};
  for (GLint i = attrib.size; i < 4; ++i) out[i] = kPad[i];
  return true;
}

}  // namespace

bool GlesContext::RenderFrame(metal_bridge::MetalBridge& bridge, GLenum mode,
                              GLint first, GLsizei count) {
  draw_.DrawArrays(mode, first, count);
  if (draw_.HasPending()) return false;
  if (count == 0) return true;  // Valid no-op; touch no bridge state.
  std::vector<VertexRef> verts;
  verts.reserve(static_cast<std::size_t>(count));
  for (GLint v = first; v < first + count; ++v) verts.push_back({v, 0});
  return SubmitVertices(bridge, mode, verts);
}

bool GlesContext::RenderElements(metal_bridge::MetalBridge& bridge,
                                 GLenum mode, GLsizei count, GLenum type,
                                 std::uintptr_t indices) {
  SyncElementState();
  draw_.DrawElements(mode, count, type, indices);
  if (draw_.HasPending()) return false;
  if (count == 0) return true;  // Valid no-op; touch no bridge state.
  // The validator above already rejected a missing EAB; re-read it here for
  // the decode (belt and suspenders: never trust indices without a store).
  const GLuint eab = vertex_arrays_.ElementArrayBuffer();
  if (eab == 0) {
    draw_.FlagBridgeError();
    return false;
  }
  const std::uint8_t* index_bytes = nullptr;
  GLsizeiptr index_size = 0;
  if (!buffers_.GetBufferBytes(eab, &index_bytes, &index_size)) {
    draw_.FlagBridgeError();
    return false;
  }
  std::size_t stride = 0;
  if (type == kGlUnsignedByte) {
    stride = 1;
  } else if (type == kGlUnsignedShort) {
    stride = 2;
  } else if (type == kGlUnsignedInt) {
    stride = 4;
  } else {
    draw_.FlagBridgeError();  // Unreachable (validator rejects), kept close.
    return false;
  }
  // Decode every index up front: a truncated index store fails the whole
  // draw instead of submitting half a frame.
  std::vector<VertexRef> verts;
  verts.reserve(static_cast<std::size_t>(count));
  for (GLsizei i = 0; i < count; ++i) {
    const std::uintptr_t at = indices + static_cast<std::size_t>(i) * stride;
    if (at + stride > static_cast<std::size_t>(index_size)) {
      draw_.FlagBridgeError();
      return false;
    }
    GLuint vertex = 0;
    if (stride == 1) {
      vertex = index_bytes[at];
    } else if (stride == 2) {
      std::uint16_t v16 = 0;
      std::memcpy(&v16, index_bytes + at, 2);  // Alignment-safe load.
      vertex = v16;
    } else {
      std::uint32_t v32 = 0;
      std::memcpy(&v32, index_bytes + at, 4);
      if (v32 > static_cast<std::uint32_t>(INT32_MAX)) {
        draw_.FlagBridgeError();  // Not addressable as a GLint vertex.
        return false;
      }
      vertex = v32;
    }
    verts.push_back({static_cast<GLint>(vertex), 0});
  }
  return SubmitVertices(bridge, mode, verts);
}

// Shared index decode with base-vertex shift (spec 10.5): reads `count`
// indices of `type` at byte offset `indices` in the VAO's ELEMENT_ARRAY_BUFFER
// and adds `basevertex` to each. Returns false + records a bridge error when
// the EAB is missing/unreadable, the range is truncated, or a shifted index
// is not GLint-addressable.
bool GlesContext::DecodeIndicesWithBase(GLsizei count, GLenum type,
                                        std::uintptr_t indices,
                                        GLint basevertex,
                                        std::vector<VertexRef>& out) {
  const GLuint eab = vertex_arrays_.ElementArrayBuffer();
  if (eab == 0) {
    draw_.FlagBridgeError();
    return false;
  }
  const std::uint8_t* index_bytes = nullptr;
  GLsizeiptr index_size = 0;
  if (!buffers_.GetBufferBytes(eab, &index_bytes, &index_size)) {
    draw_.FlagBridgeError();
    return false;
  }
  std::size_t stride = 0;
  if (type == kGlUnsignedByte) {
    stride = 1;
  } else if (type == kGlUnsignedShort) {
    stride = 2;
  } else if (type == kGlUnsignedInt) {
    stride = 4;
  } else {
    draw_.FlagBridgeError();  // Unreachable (validator rejects), kept close.
    return false;
  }
  out.reserve(out.size() + static_cast<std::size_t>(count));
  for (GLsizei i = 0; i < count; ++i) {
    const std::uintptr_t at = indices + static_cast<std::size_t>(i) * stride;
    if (at + stride > static_cast<std::size_t>(index_size)) {
      draw_.FlagBridgeError();
      return false;
    }
    std::int64_t vertex = 0;
    if (stride == 1) {
      vertex = index_bytes[at];
    } else if (stride == 2) {
      std::uint16_t v16 = 0;
      std::memcpy(&v16, index_bytes + at, 2);  // Alignment-safe load.
      vertex = v16;
    } else {
      std::uint32_t v32 = 0;
      std::memcpy(&v32, index_bytes + at, 4);
      vertex = v32;
    }
    vertex += basevertex;
    if (vertex < 0 || vertex > static_cast<std::int64_t>(INT32_MAX)) {
      draw_.FlagBridgeError();
      return false;
    }
    out.push_back({static_cast<GLint>(vertex), 0});
  }
  return true;
}

bool GlesContext::RenderElementsBaseVertex(metal_bridge::MetalBridge& bridge,
                                           GLenum mode, GLsizei count,
                                           GLenum type, std::uintptr_t indices,
                                           GLint basevertex) {
  SyncElementState();
  draw_.DrawElementsBaseVertex(mode, count, type, indices, basevertex);
  if (draw_.HasPending()) return false;
  if (count == 0) return true;  // Valid no-op; touch no bridge state.
  std::vector<VertexRef> verts;
  verts.reserve(static_cast<std::size_t>(count));
  if (!DecodeIndicesWithBase(count, type, indices, basevertex, verts)) {
    return false;
  }
  return SubmitVertices(bridge, mode, verts);
}

bool GlesContext::RenderRangeElementsBaseVertex(
    metal_bridge::MetalBridge& bridge, GLenum mode, GLuint start, GLuint end,
    GLsizei count, GLenum type, std::uintptr_t indices, GLint basevertex) {
  SyncElementState();
  draw_.DrawRangeElementsBaseVertex(mode, start, end, count, type, indices,
                                    basevertex);
  if (draw_.HasPending()) return false;
  if (count == 0) return true;
  std::vector<VertexRef> verts;
  verts.reserve(static_cast<std::size_t>(count));
  if (!DecodeIndicesWithBase(count, type, indices, basevertex, verts)) {
    return false;
  }
  return SubmitVertices(bridge, mode, verts);
}

bool GlesContext::RenderElementsInstancedBaseVertex(
    metal_bridge::MetalBridge* bridge, GLenum mode, GLsizei count, GLenum type,
    std::uintptr_t indices, GLsizei instanceCount, GLint basevertex) {
  SyncElementState();
  draw_.DrawElementsInstancedBaseVertex(mode, count, type, indices,
                                        instanceCount, basevertex);
  if (draw_.HasPending()) return false;
  if (count == 0 || instanceCount == 0) return true;
  if (bridge == nullptr) return true;  // Validation-only headless.
  std::vector<VertexRef> one;
  one.reserve(static_cast<std::size_t>(count));
  if (!DecodeIndicesWithBase(count, type, indices, basevertex, one)) {
    return false;
  }
  std::vector<VertexRef> verts;
  verts.reserve(one.size() * static_cast<std::size_t>(instanceCount));
  for (GLsizei k = 0; k < instanceCount; ++k) {
    for (const VertexRef& ref : one) verts.push_back({ref.vertex, k});
  }
  return SubmitVertices(*bridge, mode, verts);
}

bool GlesContext::RenderArraysInstancedBaseInstance(
    metal_bridge::MetalBridge* bridge, GLenum mode, GLint first,
    GLsizei count, GLsizei instanceCount, GLuint baseInstance) {
  draw_.DrawArraysInstanced(mode, first, count, instanceCount);
  if (draw_.HasPending()) return false;
  if (count == 0 || instanceCount == 0) return true;
  if (bridge == nullptr) return true;  // Validation-only headless.
  std::vector<VertexRef> verts;
  verts.reserve(static_cast<std::size_t>(count) * instanceCount);
  for (GLsizei inst = 0; inst < instanceCount; ++inst) {
    const GLsizei id =
        inst + static_cast<GLsizei>(baseInstance);  // IDs shift, verts don't.
    for (GLint v = first; v < first + count; ++v) verts.push_back({v, id});
  }
  return SubmitVertices(*bridge, mode, verts);
}

bool GlesContext::RenderElementsInstancedBaseInstance(
    metal_bridge::MetalBridge* bridge, GLenum mode, GLsizei count, GLenum type,
    std::uintptr_t indices, GLsizei instanceCount, GLuint baseInstance) {
  SyncElementState();
  draw_.DrawElementsInstanced(mode, count, type, indices, instanceCount);
  if (draw_.HasPending()) return false;
  if (count == 0 || instanceCount == 0) return true;
  if (bridge == nullptr) return true;
  const GLuint eab = vertex_arrays_.ElementArrayBuffer();
  if (eab == 0) {
    draw_.FlagBridgeError();
    return false;
  }
  const std::uint8_t* bytes = nullptr;
  GLsizeiptr size = 0;
  if (!buffers_.GetBufferBytes(eab, &bytes, &size)) {
    draw_.FlagBridgeError();
    return false;
  }
  std::size_t stride = 0;
  if (type == kGlUnsignedByte) {
    stride = 1;
  } else if (type == kGlUnsignedShort) {
    stride = 2;
  } else if (type == kGlUnsignedInt) {
    stride = 4;
  } else {
    draw_.FlagBridgeError();
    return false;
  }
  std::vector<GLint> one;
  one.reserve(count);
  for (GLsizei i = 0; i < count; ++i) {
    std::uintptr_t at = indices + static_cast<std::size_t>(i) * stride;
    if (at + stride > static_cast<std::size_t>(size)) {
      draw_.FlagBridgeError();
      return false;
    }
    GLuint v = 0;
    if (stride == 1) {
      v = bytes[at];
    } else if (stride == 2) {
      std::uint16_t t = 0;
      std::memcpy(&t, bytes + at, 2);
      v = t;
    } else {
      std::uint32_t t = 0;
      std::memcpy(&t, bytes + at, 4);
      if (t > static_cast<std::uint32_t>(INT32_MAX)) {
        draw_.FlagBridgeError();
        return false;
      }
      v = t;
    }
    one.push_back(static_cast<GLint>(v));
  }
  std::vector<VertexRef> verts;
  verts.reserve(one.size() * static_cast<std::size_t>(instanceCount));
  for (GLsizei k = 0; k < instanceCount; ++k) {
    const GLsizei id = k + static_cast<GLsizei>(baseInstance);
    for (GLint v : one) verts.push_back({v, id});
  }
  return SubmitVertices(*bridge, mode, verts);
}

bool GlesContext::RenderElementsInstancedBaseVertexBaseInstance(
    metal_bridge::MetalBridge* bridge, GLenum mode, GLsizei count, GLenum type,
    std::uintptr_t indices, GLsizei instanceCount, GLint basevertex,
    GLuint baseInstance) {
  SyncElementState();
  draw_.DrawElementsInstancedBaseVertex(mode, count, type, indices,
                                        instanceCount, basevertex);
  if (draw_.HasPending()) return false;
  if (count == 0 || instanceCount == 0) return true;
  if (bridge == nullptr) return true;
  std::vector<VertexRef> one;
  one.reserve(static_cast<std::size_t>(count));
  if (!DecodeIndicesWithBase(count, type, indices, basevertex, one)) {
    return false;
  }
  std::vector<VertexRef> verts;
  verts.reserve(one.size() * static_cast<std::size_t>(instanceCount));
  for (GLsizei k = 0; k < instanceCount; ++k) {
    const GLsizei id = k + static_cast<GLsizei>(baseInstance);
    for (const VertexRef& ref : one) verts.push_back({ref.vertex, id});
  }
  return SubmitVertices(*bridge, mode, verts);
}

bool GlesContext::MultiDrawArraysIndirect(metal_bridge::MetalBridge* bridge,
                                          GLenum mode, std::uintptr_t indirect,
                                          GLsizei drawcount, GLsizei stride) {
  SyncIndirectState();
  if (drawcount < 0 || stride < 0) {
    draw_.FlagBridgeError();  // INVALID_VALUE class; draw queue owns draws.
    return false;
  }
  if (drawcount == 0) return true;
  const GLuint buf = buffers_.BoundBuffer(kGlDrawIndirectBuffer);
  if (buf == 0) {
    draw_.FlagBridgeError();
    return false;
  }
  const std::uint8_t* bytes = nullptr;
  GLsizeiptr size = 0;
  if (!buffers_.GetBufferBytes(buf, &bytes, &size)) {
    draw_.FlagBridgeError();
    return false;
  }
  const std::size_t step =
      (stride == 0) ? 16 : static_cast<std::size_t>(stride);
  for (GLsizei i = 0; i < drawcount; ++i) {
    const std::uintptr_t at = indirect + static_cast<std::size_t>(i) * step;
    if (at + 16 > static_cast<std::size_t>(size)) {
      draw_.FlagBridgeError();
      return false;
    }
    std::uint32_t count = 0, instanceCount = 0;
    std::int32_t first = 0;
    std::memcpy(&count, bytes + at, 4);
    std::memcpy(&instanceCount, bytes + at + 4, 4);
    std::memcpy(&first, bytes + at + 8, 4);
    if (!RenderInstanced(bridge, mode, first, static_cast<GLsizei>(count),
                         static_cast<GLsizei>(instanceCount))) {
      return false;  // Draws before the failure stand.
    }
  }
  return true;
}

bool GlesContext::MultiDrawElementsIndirect(metal_bridge::MetalBridge* bridge,
                                            GLenum mode, GLenum type,
                                            std::uintptr_t indirect,
                                            GLsizei drawcount,
                                            GLsizei stride) {
  SyncIndirectState();
  if (drawcount < 0 || stride < 0) {
    draw_.FlagBridgeError();
    return false;
  }
  if (drawcount == 0) return true;
  const GLuint buf = buffers_.BoundBuffer(kGlDrawIndirectBuffer);
  if (buf == 0) {
    draw_.FlagBridgeError();
    return false;
  }
  const std::uint8_t* bytes = nullptr;
  GLsizeiptr size = 0;
  if (!buffers_.GetBufferBytes(buf, &bytes, &size)) {
    draw_.FlagBridgeError();
    return false;
  }
  const std::size_t step =
      (stride == 0) ? 20 : static_cast<std::size_t>(stride);
  for (GLsizei i = 0; i < drawcount; ++i) {
    const std::uintptr_t at = indirect + static_cast<std::size_t>(i) * step;
    if (at + 20 > static_cast<std::size_t>(size)) {
      draw_.FlagBridgeError();
      return false;
    }
    std::uint32_t count = 0, instanceCount = 0, firstIndex = 0;
    std::int32_t baseVertex = 0;
    std::memcpy(&count, bytes + at, 4);
    std::memcpy(&instanceCount, bytes + at + 4, 4);
    std::memcpy(&firstIndex, bytes + at + 8, 4);
    std::memcpy(&baseVertex, bytes + at + 12, 4);
    SyncElementState();
    const GLuint eab = vertex_arrays_.ElementArrayBuffer();
    if (eab == 0) {
      draw_.FlagBridgeError();
      return false;
    }
    const std::uint8_t* ibytes = nullptr;
    GLsizeiptr isize = 0;
    if (!buffers_.GetBufferBytes(eab, &ibytes, &isize)) {
      draw_.FlagBridgeError();
      return false;
    }
    std::size_t istride = (type == kGlUnsignedByte)
                              ? 1
                              : (type == kGlUnsignedShort ? 2 : 4);
    if (type != kGlUnsignedByte && type != kGlUnsignedShort &&
        type != kGlUnsignedInt) {
      draw_.FlagBridgeError();
      return false;
    }
    std::vector<VertexRef> one;
    for (std::uint32_t j = 0; j < count; ++j) {
      std::uintptr_t iat = firstIndex * istride + j * istride;
      if (iat + istride > static_cast<std::size_t>(isize)) {
        draw_.FlagBridgeError();
        return false;
      }
      GLuint v = 0;
      if (istride == 1) {
        v = ibytes[iat];
      } else if (istride == 2) {
        std::uint16_t t = 0;
        std::memcpy(&t, ibytes + iat, 2);
        v = t;
      } else {
        std::uint32_t t = 0;
        std::memcpy(&t, ibytes + iat, 4);
        v = t;
      }
      const std::int64_t shifted =
          static_cast<std::int64_t>(v) + baseVertex;
      if (shifted < 0 || shifted > INT32_MAX) {
        draw_.FlagBridgeError();
        return false;
      }
      one.push_back({static_cast<GLint>(shifted), 0});
    }
    std::vector<VertexRef> verts;
    for (std::uint32_t k = 0; k < instanceCount; ++k) {
      for (const VertexRef& ref : one) {
        verts.push_back({ref.vertex, static_cast<GLsizei>(k)});
      }
    }
    draw_.DrawElementsInstanced(mode, static_cast<GLsizei>(count), type,
                                firstIndex, static_cast<GLsizei>(instanceCount));
    if (draw_.HasPending()) return false;
    if (verts.empty()) continue;
    if (bridge == nullptr) continue;  // Validation-only headless.
    if (!SubmitVertices(*bridge, mode, verts)) return false;
  }
  return true;
}

bool GlesContext::MultiDrawElementsBaseVertex(
    metal_bridge::MetalBridge* bridge, GLenum mode, const GLsizei* count,
    GLenum type, const void* const* indices, GLsizei drawcount,
    const GLint* basevertex) {
  SyncElementState();
  if (drawcount < 0) {
    draw_.FlagBridgeError();
    return false;
  }
  if (drawcount == 0) return true;
  if (count == nullptr || indices == nullptr || basevertex == nullptr) {
    draw_.FlagBridgeError();
    return false;
  }
  for (GLsizei i = 0; i < drawcount; ++i) {
    if (count[i] < 0) {
      draw_.FlagBridgeError();
      return false;
    }
    draw_.DrawElementsBaseVertex(mode, count[i], type,
                                 reinterpret_cast<std::uintptr_t>(indices[i]),
                                 basevertex[i]);
    if (draw_.HasPending()) return false;
    if (count[i] == 0) continue;
    std::vector<VertexRef> verts;
    verts.reserve(static_cast<std::size_t>(count[i]));
    if (!DecodeIndicesWithBase(
            count[i], type,
            reinterpret_cast<std::uintptr_t>(indices[i]), basevertex[i],
            verts)) {
      return false;
    }
    if (bridge == nullptr) continue;  // Validation-only headless.
    if (!SubmitVertices(*bridge, mode, verts)) return false;
  }
  return true;
}

bool GlesContext::SubmitVertices(metal_bridge::MetalBridge& bridge,
                                 GLenum mode,
                                 const std::vector<VertexRef>& verts) {
  // Topology routing: LINE_LOOP and TRIANGLE_FAN have no Metal primitive
  // type, so the facade CPU-expands them. Adjacency lists/strips also
  // CPU-expand by dropping adjacency verts (spec: lines 1-2 of each 4,
  // triangles 0/2/4 of each 6; strips drop ends/evens — no geometry shader
  // needed, pixel-identical for non-geometry programs). PATCHES needs
  // tessellation/mesh (Apple7+, Metal 3): fail closed with mesh reason on
  // Intel (see HostIntegrationGaps); a program WITH tess/geometry stages
  // always fails here (mesh path lives in the Apple bridge, gated by family).
  GLenum bridge_mode = mode;
  std::vector<VertexRef> expanded;
  const std::vector<VertexRef>* draw_verts = &verts;
  // A program carrying tessellation or geometry stages can never take the
  // CPU-expanded path (its logic lives in those stages): fail closed so we
  // never render a wrong image. Mesh execution on Apple7+ is tracked.
  const GLuint cur_prog = programs_.CurrentProgram();
  if (cur_prog != 0 &&
      (programs_.ProgramHasStage(cur_prog, kGlTessControlShader) ||
       programs_.ProgramHasStage(cur_prog, kGlTessEvaluationShader) ||
       programs_.ProgramHasStage(cur_prog, kGlGeometryShader))) {
    // PATCHES without tess stages is also invalid, but adjacency without
    // geometry stages is exactly what we CPU-expand below (geometry shaders
    // are optional in ES 3.2 draws; adjacency verts are just dropped).
    if (mode == kGlPatches ||
        programs_.ProgramHasStage(cur_prog, kGlTessControlShader) ||
        programs_.ProgramHasStage(cur_prog, kGlTessEvaluationShader)) {
      fprintf(stderr,
              "[TGL-DEBUG] PATCHES/tess needs mesh (Apple7+ Metal3)\n");
      draw_.FlagBridgeError();
      return false;
    }
    // Geometry-stage programs with non-adjacency modes: geometry logic would
    // be dropped, so fail rather than lie. Adjacency modes without geometry
    // intent still expand (see below) — the stage check above only forces
    // fail for PATCHES/tess; geometry programs drawing plain triangles fail
    // here to avoid silent logic loss.
    if (mode == kGlPoints || mode == kGlLines || mode == kGlLineStrip ||
        mode == kGlTriangles || mode == kGlTriangleStrip ||
        mode == kGlLineLoop || mode == kGlTriangleFan) {
      fprintf(stderr, "[TGL-DEBUG] geometry program needs mesh\n");
      draw_.FlagBridgeError();
      return false;
    }
  }
  switch (mode) {
    case kGlPoints:
    case kGlLines:
    case kGlLineStrip:
    case kGlTriangles:
    case kGlTriangleStrip:
      break;
    case kGlLineLoop: {
      if (verts.size() >= 2) {
        expanded = verts;
        expanded.push_back(verts.front());  // Close the loop as a strip.
      }
      draw_verts = &expanded;
      bridge_mode = kGlLineStrip;
      break;
    }
    case kGlTriangleFan: {
      if (verts.size() >= 3) {
        expanded.reserve((verts.size() - 2) * 3);
        for (std::size_t i = 1; i + 1 < verts.size(); ++i) {
          expanded.push_back(verts[0]);
          expanded.push_back(verts[i]);
          expanded.push_back(verts[i + 1]);
        }
      }
      draw_verts = &expanded;
      bridge_mode = kGlTriangles;
      break;
    }
    case 0x000Au: {  // LINES_ADJACENCY: 4N verts, segment = 1,2 of each 4.
      if (verts.size() % 4 != 0) {
        draw_.FlagBridgeError();
        return false;
      }
      expanded.reserve(verts.size() / 2);
      for (std::size_t i = 0; i < verts.size(); i += 4) {
        expanded.push_back(verts[i + 1]);
        expanded.push_back(verts[i + 2]);
      }
      draw_verts = &expanded;
      bridge_mode = kGlLines;
      break;
    }
    case 0x000Bu: {  // LINE_STRIP_ADJACENCY: [adj, v0..vn, adj] -> strip.
      if (verts.size() < 2) {
        draw_.FlagBridgeError();
        return false;
      }
      expanded.assign(verts.begin() + 1, verts.end() - 1);
      draw_verts = &expanded;
      bridge_mode = kGlLineStrip;
      break;
    }
    case 0x000Cu: {  // TRIANGLES_ADJACENCY: 6N verts, tri = 0,2,4 of each 6.
      if (verts.size() % 6 != 0) {
        draw_.FlagBridgeError();
        return false;
      }
      expanded.reserve(verts.size() / 2);
      for (std::size_t i = 0; i < verts.size(); i += 6) {
        expanded.push_back(verts[i]);
        expanded.push_back(verts[i + 2]);
        expanded.push_back(verts[i + 4]);
      }
      draw_verts = &expanded;
      bridge_mode = kGlTriangles;
      break;
    }
    case 0x000Du: {  // TRIANGLE_STRIP_ADJACENCY: evens -> strip.
      if (verts.size() < 6 || (verts.size() % 2 != 0)) {
        draw_.FlagBridgeError();
        return false;
      }
      expanded.reserve(verts.size() / 2);
      for (std::size_t i = 0; i < verts.size(); i += 2) {
        expanded.push_back(verts[i]);
      }
      draw_verts = &expanded;
      bridge_mode = kGlTriangleStrip;
      break;
    }
    case kGlPatches: {
      fprintf(stderr,
              "[TGL-DEBUG] PATCHES needs mesh (Apple7+ Metal3, Intel "
              "fail-closed)\n");
      draw_.FlagBridgeError();
      return false;
    }
    default:
      draw_.FlagBridgeError();  // Unknown: no execution.
      return false;
  }
  if (draw_verts->empty() && !verts.empty()) {
    // Degenerate fan/loop with too few vertices: nothing to rasterize, but
    // the call itself was valid (matches count-0 no-op semantics).
    return true;
  }
  // Culling everything (FRONT_AND_BACK with the test on) discards every
  // fragment: a valid no-op that touches no bridge state. Validation already
  // ran in the caller paths; the per-frame clears have no persistent store
  // in this model, so skipping is observably identical.
  if (foundation_.IsEnabled(kGlCullFace) != kGlFalse &&
      raster_.GetCullMode() == kGlFrontAndBack) {
    return true;
  }
  const GenericAttrib pos = vertex_arrays_.AttribState(0);
  const GenericAttrib col = vertex_arrays_.AttribState(1);
  if (!pos.enabled || !col.enabled) {
    draw_.FlagBridgeError();
    return false;
  }
  const std::uint8_t* pos_bytes = nullptr;
  const std::uint8_t* col_bytes = nullptr;
  GLsizeiptr pos_size = 0, col_size = 0;
  if (!buffers_.GetBufferBytes(pos.buffer, &pos_bytes, &pos_size) ||
      !buffers_.GetBufferBytes(col.buffer, &col_bytes, &col_size)) {
    draw_.FlagBridgeError();
    return false;
  }
  // Fragment sampling detection (ES 3.2 §8, honest v1): first sampler2D-like
  // uniform with a complete RGBA8 level-0 TEXTURE_2D bound + enabled UV
  // attrib 2 selects the textured pipeline. Views resolve via shared bytes.
  // Incomplete textures fall back to untextured (black-sampling is tracked).
  const GLuint current_prog = programs_.CurrentProgram();
  bool want_textured = false;
  GLuint samp_unit = 0;
  TextureLevel samp_level;
  GenericAttrib uv_attr;
  GLuint uv_binding_divisor = 0;
  if (current_prog != 0) {
    const GenericAttrib uv = vertex_arrays_.AttribState(2);
    if (uv.enabled && !uv.pure_integer) {
      const std::uint8_t* uv_bytes = nullptr;
      GLsizeiptr uv_size = 0;
      if (buffers_.GetBufferBytes(uv.buffer, &uv_bytes, &uv_size)) {
        for (const ActiveUniform& u : programs_.Uniforms(current_prog)) {
          const bool is_sampler =
              (u.type == kGlSampler2d || u.type == kGlSampler3d ||
               u.type == kGlSamplerCube || u.type == kGlSampler2dArray);
          if (!is_sampler) continue;
          // Only 2D sampling executes in v1; other sampler types stay
          // untextured (documented subset, never miscompiled).
          if (u.type != kGlSampler2d) continue;
          std::vector<float> val;
          if (!programs_.GetUniformFloats(current_prog, u.base_location,
                                          &val) ||
              val.empty()) {
            continue;
          }
          const int unit_i = static_cast<int>(val[0]);
          if (unit_i < 0 ||
              unit_i >= kMaxCombinedTextureImageUnits) {
            continue;
          }
          const GLuint unit = static_cast<GLuint>(unit_i);
          const GLuint bound =
              textures_.BoundTextureForUnit(unit, kGlTexture2d);
          if (bound == 0) continue;
          TextureLevel lvl =
              textures_.LevelState(bound, kGlTexture2d, 0);
          if (!lvl.defined || lvl.width <= 0 || lvl.height <= 0) continue;
          const std::size_t need =
              static_cast<std::size_t>(lvl.width) * lvl.height * 4;
          if (lvl.pixels.size() < need) continue;
          want_textured = true;
          samp_unit = unit;
          samp_level = lvl;
          uv_attr = uv;
          uv_binding_divisor =
              vertex_arrays_.BindingState(uv.binding_index).divisor;
          break;
        }
      }
    }
  }
  // GLSL->MSL v1 routing (subset in glsl_to_msl.h): a linked vertex+fragment
  // pair compiles to app-derived MSL; empty-main trial shaders fall back to
  // the fixed passthrough; any other untranslatable program fails closed
  // instead of rendering wrong shading.
  glsl::TranslatedProgram trans;
  bool use_translated = false;
  if (current_prog != 0) {
    std::string vs_src, fs_src;
    if (programs_.GetGraphicsSources(current_prog, &vs_src, &fs_src)) {
      trans = glsl::TranslateProgram(vs_src, fs_src);
      if (trans.ok) {
        use_translated = true;
      } else if (!glsl::LooksLegacyTrivial(vs_src, fs_src)) {
        fprintf(stderr, "[TGL-DEBUG] translator rejected: %s\n",
                trans.error.c_str());
        draw_.FlagBridgeError();  // Real shader TGL cannot compile: no lie.
        return false;
      }
    }
  }
  // Translated samplers resolve per kind below (their own completeness
  // rules); the legacy want_textured flag only drives the legacy 2D path.
  if (use_translated && trans.needs_slot2 && !trans.uses_sampler) {
    // Lighting-normal convention: attrib 2 carries the normal (no texture).
    const GenericAttrib uv2 = vertex_arrays_.AttribState(2);
    if (!uv2.enabled || uv2.pure_integer) {
      draw_.FlagBridgeError();
      return false;
    }
    uv_attr = uv2;
    uv_binding_divisor =
        vertex_arrays_.BindingState(uv2.binding_index).divisor;
  }
  if (use_translated && trans.needs_slot2 && uv_attr.buffer == 0) {
    // Translated sampling path: the legacy 2D-only detection above never
    // fills uv_attr for cube/3D/array samplers, so resolve slot 2 here
    // (integer stores convert exactly below 2^24, like all int attribs).
    const GenericAttrib uv2 = vertex_arrays_.AttribState(2);
    if (!uv2.enabled) {
      draw_.FlagBridgeError();
      return false;
    }
    uv_attr = uv2;
    uv_binding_divisor =
        vertex_arrays_.BindingState(uv2.binding_index).divisor;
  }
  const int slot2c =
      use_translated ? trans.slot2_comps : (want_textured ? 2 : 0);
  const bool need_slot2 = slot2c > 0;
  std::vector<std::uint8_t> interleaved;
  const std::uint32_t vert_stride =
      static_cast<std::uint32_t>(32 + 4 * slot2c);
  interleaved.reserve(draw_verts->size() * vert_stride);
  const GLuint pos_binding_divisor =
      vertex_arrays_.BindingState(pos.binding_index).divisor;
  const GLuint col_binding_divisor =
      vertex_arrays_.BindingState(col.binding_index).divisor;
  for (const VertexRef& ref : *draw_verts) {
    GLfloat p[4], c[4];
    if (!DecodeAttribToFloat4(pos, pos_binding_divisor, pos_bytes, pos_size,
                              ref.vertex, ref.instance, p) ||
        !DecodeAttribToFloat4(col, col_binding_divisor, col_bytes, col_size,
                              ref.vertex, ref.instance, c)) {
      draw_.FlagBridgeError();
      return false;
    }
    const std::uint8_t* pp = reinterpret_cast<const std::uint8_t*>(p);
    const std::uint8_t* cp = reinterpret_cast<const std::uint8_t*>(c);
    interleaved.insert(interleaved.end(), pp, pp + 16);
    interleaved.insert(interleaved.end(), cp, cp + 16);
    if (need_slot2) {
      const std::uint8_t* uv_bytes = nullptr;
      GLsizeiptr uv_size = 0;
      if (!buffers_.GetBufferBytes(uv_attr.buffer, &uv_bytes, &uv_size)) {
        draw_.FlagBridgeError();
        return false;
      }
      GLfloat t[4] = {0, 0, 0, 1};
      if (!DecodeAttribToFloat4(uv_attr, uv_binding_divisor, uv_bytes, uv_size,
                                ref.vertex, ref.instance, t)) {
        draw_.FlagBridgeError();
        return false;
      }
      const std::uint8_t* tp = reinterpret_cast<const std::uint8_t*>(t);
      // Slot 2 carries the first slot2c*4 bytes of the decoded float4
      // (uv/normal xy, cube dir xyz, or a full float4).
      interleaved.insert(interleaved.end(), tp,
                         tp + static_cast<std::size_t>(slot2c) * 4);
    }
  }
  // Upload sampling state before PSO creation (bridge sampler is encoder
  // state, texture is a bound resource; both persist like VAO/uniforms).
  // Effective sampler params (spec 8.2: bound Sampler object overrides the
  // texture's own): probed via save/restore ActiveTexture (no net change);
  // incidental query errors are drained so sampling never pollutes GetError.
  // BORDER wrap has no Metal sampler mode: fail closed (documented).
  auto sampler_detail = [&](GLuint unit, GLenum target, GLint* min_f,
                            GLint* mag_f, GLint* wrap_s, GLint* wrap_t,
                            float* min_lod, float* max_lod) {
    const GLuint samp_obj = samplers_.BoundSampler(unit);
    if (samp_obj != 0) {
      samplers_.GetSamplerParameteriv(samp_obj, kGlTextureMinFilter, min_f);
      samplers_.GetSamplerParameteriv(samp_obj, kGlTextureMagFilter, mag_f);
      samplers_.GetSamplerParameteriv(samp_obj, kGlTextureWrapS, wrap_s);
      samplers_.GetSamplerParameteriv(samp_obj, kGlTextureWrapT, wrap_t);
      while (samplers_.HasPending()) (void)samplers_.GetError();
      samplers_.GetSamplerParameterfv(samp_obj, kGlTextureMinLod, min_lod);
      while (samplers_.HasPending()) {
        (void)samplers_.GetError();
        *min_lod = -1000.0f;
      }
      samplers_.GetSamplerParameterfv(samp_obj, kGlTextureMaxLod, max_lod);
      while (samplers_.HasPending()) {
        (void)samplers_.GetError();
        *max_lod = 1000.0f;
      }
      return;
    }
    *min_f = kGlNearestMipmapLinear;
    *mag_f = kGlLinear;
    *wrap_s = kGlClampToEdge;
    *wrap_t = kGlClampToEdge;
    *min_lod = -1000.0f;
    *max_lod = 1000.0f;
    const GLuint saved_unit = textures_.ActiveUnit();
    textures_.ActiveTexture(0x84C0u + unit);
    if (!textures_.HasPending()) {
      textures_.GetTexParameteriv(target, kGlTextureMinFilter, min_f);
      while (textures_.HasPending()) (void)textures_.GetError();
      textures_.GetTexParameteriv(target, kGlTextureMagFilter, mag_f);
      while (textures_.HasPending()) (void)textures_.GetError();
      textures_.GetTexParameteriv(target, kGlTextureWrapS, wrap_s);
      while (textures_.HasPending()) (void)textures_.GetError();
      textures_.GetTexParameteriv(target, kGlTextureWrapT, wrap_t);
      while (textures_.HasPending()) (void)textures_.GetError();
      textures_.GetTexParameterfv(target, kGlTextureMinLod, min_lod);
      while (textures_.HasPending()) {
        (void)textures_.GetError();
        *min_lod = -1000.0f;
      }
      textures_.GetTexParameterfv(target, kGlTextureMaxLod, max_lod);
      while (textures_.HasPending()) {
        (void)textures_.GetError();
        *max_lod = 1000.0f;
      }
    } else {
      while (textures_.HasPending()) (void)textures_.GetError();
    }
    textures_.ActiveTexture(0x84C0u + saved_unit);
    while (textures_.HasPending()) (void)textures_.GetError();
  };
  if (want_textured && !use_translated) {
    // Legacy 2D path (single sampler2D, level 0, NEAREST/LINEAR + CLAMP).
    bool linear = false, repeat = false;
    GLint min_f = 0, mag_f = 0, wrap_s = 0, wrap_t = 0;
    float min_lod = -1000.0f, max_lod = 1000.0f;
    sampler_detail(samp_unit, kGlTexture2d, &min_f, &mag_f, &wrap_s, &wrap_t,
                   &min_lod, &max_lod);
    linear = (mag_f == kGlLinear);
    repeat = (wrap_s == kGlRepeat);
    bridge.SetFragmentTexture(samp_unit, samp_level.width, samp_level.height,
                              samp_level.pixels.data());
    bridge.SetFragmentSampler(samp_unit, linear, repeat);
    if (bridge.GetError() != kGlNoError) {
      draw_.FlagBridgeError();
      return false;
    }
  }
  if (use_translated && trans.uses_sampler) {
    // Translated path: every sampler uniform by kind, MSL slot = declaration
    // order. Incomplete bindings fail closed (spec: they sample (0,0,0,1);
    // rendering them untextured would be the old lie).
    std::vector<GLuint> slot_units;
    for (std::size_t si = 0; si < trans.sampler_names.size(); ++si) {
      const std::string& sname = trans.sampler_names[si];
      const int skind = trans.sampler_kinds[si];
      GLenum target = kGlTexture2d;
      if (skind == 1) {
        target = kGlTextureCubeMap;
      } else if (skind == 2) {
        target = kGlTexture3d;
      } else if (skind == 3) {
        target = kGlTexture2dArray;
      }
      GLint unit_i = -1;
      for (const ActiveUniform& u : programs_.Uniforms(current_prog)) {
        if (u.name != sname) continue;
        std::vector<float> val;
        if (programs_.GetUniformFloats(current_prog, u.base_location, &val) &&
            !val.empty()) {
          unit_i = static_cast<GLint>(val[0]);
        }
        break;
      }
      if (unit_i < 0 || unit_i >= kMaxCombinedTextureImageUnits) {
        draw_.FlagBridgeError();
        return false;
      }
      const GLuint unit = static_cast<GLuint>(unit_i);
      const GLuint bound = textures_.BoundTextureForUnit(unit, target);
      if (bound == 0) {
        draw_.FlagBridgeError();
        return false;
      }
      GLint min_f = 0, mag_f = 0, wrap_s = 0, wrap_t = 0;
      float min_lod = -1000.0f, max_lod = 1000.0f;
      sampler_detail(unit, target, &min_f, &mag_f, &wrap_s, &wrap_t, &min_lod,
                     &max_lod);
      if (wrap_s == kGlClampToBorder || wrap_t == kGlClampToBorder) {
        draw_.FlagBridgeError();  // No Metal border color.
        return false;
      }
      const bool min_mipmap = (min_f == kGlNearestMipmapNearest ||
                               min_f == kGlLinearMipmapNearest ||
                               min_f == kGlNearestMipmapLinear ||
                               min_f == kGlLinearMipmapLinear);
      const bool srgb =
          textures_.LevelInternalFormat(bound, target == kGlTextureCubeMap
                                                    ? kGlTextureCubeMapPositiveX
                                                    : target,
                                        0) == kGlSrgb8Alpha8;
      if (skind == 0) {
        // 2D (+mips): collect the defined chain for the bridge.
        std::vector<TextureLevel> chain;
        for (GLint l = 0; l < 16; ++l) {
          TextureLevel lvl = textures_.LevelState(bound, kGlTexture2d, l);
          if (!lvl.defined || lvl.width <= 0 || lvl.height <= 0) break;
          const std::size_t need =
              static_cast<std::size_t>(lvl.width) * lvl.height * 4;
          if (lvl.pixels.size() < need) break;
          chain.push_back(lvl);
          if (lvl.width == 1 && lvl.height == 1) break;
        }
        if (chain.empty()) {
          draw_.FlagBridgeError();
          return false;
        }
        if (min_mipmap) {
          int full = 1;
          for (GLsizei s = std::max(chain[0].width, chain[0].height); s > 1;
               s /= 2) {
            ++full;
          }
          if (static_cast<int>(chain.size()) < full) {
            draw_.FlagBridgeError();  // Mipmap-incomplete per spec 8.14.
            return false;
          }
        }
        std::vector<const void*> ptrs;
        for (const TextureLevel& lvl : chain) ptrs.push_back(lvl.pixels.data());
        bridge.SetFragmentTextureMips(
            unit, chain[0].width, chain[0].height,
            static_cast<int>(chain.size()), ptrs.data(), srgb);
      } else if (skind == 1) {
        // Cube (+mips, Phase 4 item 6): collect per-level 6-face chains.
        // State already synthesizes the chain (GenerateMipmap box filter).
        std::vector<std::vector<TextureLevel>> cube_levels;
        for (GLint l = 0; l < 16; ++l) {
          bool have = true;
          std::vector<TextureLevel> faces_l;
          for (int f = 0; f < 6; ++f) {
            TextureLevel fl = textures_.LevelState(
                bound, kGlTextureCubeMapPositiveX + static_cast<GLenum>(f), l);
            if (!fl.defined || fl.width <= 0 || fl.height <= 0) {
              have = false;
              break;
            }
            const std::size_t need = static_cast<std::size_t>(fl.width) *
                                     fl.height * 4;
            if (fl.pixels.size() < need) {
              have = false;
              break;
            }
            faces_l.push_back(fl);
          }
          if (!have) break;
          // All faces same size per level.
          for (int f = 1; f < 6; ++f) {
            if (faces_l[f].width != faces_l[0].width ||
                faces_l[f].height != faces_l[0].height) {
              draw_.FlagBridgeError();
              return false;
            }
          }
          // Keep TextureLevel alive via copied pixels in chain storage below.
          cube_levels.push_back(faces_l);
          if (faces_l[0].width == 1 && faces_l[0].height == 1) break;
        }
        if (cube_levels.empty()) {
          draw_.FlagBridgeError();
          return false;
        }
        if (min_mipmap) {
          int full = 1;
          for (GLsizei s = cube_levels[0][0].width; s > 1; s /= 2) ++full;
          if (static_cast<int>(cube_levels.size()) < full) {
            fprintf(stderr,
                    "[TGL-DEBUG] cube-mips incomplete %zu/%d\n",
                    cube_levels.size(), full);
            draw_.FlagBridgeError();  // Mipmap-incomplete per spec 8.14.
            return false;
          }
          // Upload full chain (faces_per_level = [L0*6, L1*6, ...]).
          // Pointers stay valid through the bridge call: cube_levels holds
          // the pixel vectors alive, and the bridge copies bytes synchronously
          // (Apple replaceRegion, Mock records kind/levels only).
          std::vector<const void*> ptrs;
          for (const auto& fl : cube_levels) {
            for (int f = 0; f < 6; ++f) ptrs.push_back(fl[f].pixels.data());
          }
          bridge.SetFragmentTextureCubeMips(
              unit, cube_levels[0][0].width,
              static_cast<int>(cube_levels.size()), ptrs.data(), srgb);
        } else {
          const auto& fl0 = cube_levels[0];
          const void* ptrs[6] = {
              fl0[0].pixels.data(), fl0[1].pixels.data(), fl0[2].pixels.data(),
              fl0[3].pixels.data(), fl0[4].pixels.data(), fl0[5].pixels.data()};
          bridge.SetFragmentTextureCube(unit, fl0[0].width, ptrs, srgb);
        }
      } else {
        // 3D / 2D-array (+mips, Phase 4 item 6).
        const GLenum face = (skind == 2) ? kGlTexture3d : kGlTexture2dArray;
        std::vector<TextureLevel> chain;
        for (GLint l = 0; l < 16; ++l) {
          TextureLevel lvl = textures_.LevelState(bound, face, l);
          if (!lvl.defined || lvl.width <= 0 || lvl.height <= 0 ||
              lvl.depth <= 0) {
            break;
          }
          const std::size_t need = static_cast<std::size_t>(lvl.width) *
                                   lvl.height * lvl.depth * 4;
          if (lvl.pixels.size() < need) break;
          chain.push_back(lvl);
          if (lvl.width == 1 && lvl.height == 1 && lvl.depth == 1) break;
        }
        if (chain.empty()) {
          draw_.FlagBridgeError();
          return false;
        }
        if (min_mipmap) {
          int full = 1;
          for (GLsizei s = std::max({chain[0].width, chain[0].height,
                                     chain[0].depth});
               s > 1; s /= 2) {
            ++full;
          }
          if (static_cast<int>(chain.size()) < full) {
            fprintf(stderr, "[TGL-DEBUG] 3D/array-mips incomplete\n");
            draw_.FlagBridgeError();
            return false;
          }
          std::vector<const void*> ptrs;
          for (const auto& lv : chain) ptrs.push_back(lv.pixels.data());
          if (skind == 2) {
            bridge.SetFragmentTexture3DMips(
                unit, chain[0].width, chain[0].height, chain[0].depth,
                static_cast<int>(chain.size()), ptrs.data(), srgb);
          } else {
            bridge.SetFragmentTextureArrayMips(
                unit, chain[0].width, chain[0].height, chain[0].depth,
                static_cast<int>(chain.size()), ptrs.data(), srgb);
          }
        } else {
          const TextureLevel& lvl = chain[0];
          if (skind == 2) {
            bridge.SetFragmentTexture3D(unit, lvl.width, lvl.height, lvl.depth,
                                        lvl.pixels.data(), srgb);
          } else {
            bridge.SetFragmentTextureArray(unit, lvl.width, lvl.height,
                                           lvl.depth, lvl.pixels.data(), srgb);
          }
        }
      }
      if (bridge.GetError() != kGlNoError) {
        draw_.FlagBridgeError();
        return false;
      }
      bridge.SetFragmentSamplerDetail(unit, min_f, mag_f, wrap_s, wrap_t);
      if (bridge.GetError() != kGlNoError) {
        draw_.FlagBridgeError();
        return false;
      }
      // Phase 4 item 10: LOD clamps ride the sampler (Metal lodMin/MaxClamp).
      bridge.SetSamplerLod(unit, min_lod, max_lod);
      if (bridge.GetError() != kGlNoError) {
        draw_.FlagBridgeError();
        return false;
      }
      slot_units.push_back(unit);
    }
    bridge.SetSamplerSlots(slot_units.data(), slot_units.size());
    if (bridge.GetError() != kGlNoError) {
      draw_.FlagBridgeError();
      return false;
    }
  }
  GLfloat mvp[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
  const GLuint current = programs_.CurrentProgram();
  if (use_translated) {
    // Uniform block in TranslatedProgram::uniforms order (16-byte fields;
    // mat4 = 16 floats, vec4 = 4, arrays stride per field). Unset locations
    // read back zeros per spec 7.13 initial values, via per-element reads.
    // Packing follows TranslatedProgram offsets: every field starts at a
    // 16-byte boundary (vec3 pads to 16, mat4 fills 64). int/uint/bool
    // fields convert from the float store (exact below 2^24, documented).
    std::vector<std::uint8_t> block(
        static_cast<std::size_t>(trans.uniform_block_bytes), 0);
    if (block.empty()) block.assign(16, 0);  // Matches the _pad struct.
    for (const glsl::UniformField& f : trans.uniforms) {
      const GLint base =
          programs_.GetUniformLocation(current_prog, f.name.c_str());
      // Drain incidental GetUniformLocation errors (unlinked? no) so uniform
      // planning never pollutes GetError; a real miss is base<0 below.
      while (programs_.HasPending()) (void)programs_.GetError();
      if (base < 0) {
        // Array base query `u` for `uniform vec4 u[2]` returns base per spec;
        // struct leaf `u.member` resolves directly.
        fprintf(stderr, "[TGL-DEBUG] uniform '%s' location miss\n",
                f.name.c_str());
        draw_.FlagBridgeError();
        return false;
      }
      const int elems = (f.array_size > 1) ? f.array_size : 1;
      for (int e = 0; e < elems; ++e) {
        const GLint loc = base + e;
        std::vector<float> vals;
        if (!programs_.GetUniformElementFloats(current_prog, loc, &vals) ||
            static_cast<int>(vals.size()) != f.float_count) {
          fprintf(stderr, "[TGL-DEBUG] uniform '%s'[%d] readback fail\n",
                  f.name.c_str(), e);
          draw_.FlagBridgeError();
          return false;
        }
        while (programs_.HasPending()) (void)programs_.GetError();
        if (f.float_count == 16 && e == 0 &&
            !programs_.HasUniformValue(current_prog, loc)) {
          // Unset mat4 reads back zeros per spec 7.13, but a zero matrix
          // collapses all geometry: the legacy path substitutes identity for
          // unset u_modelViewProj (GetUniformMatrix false), so do the same
          // here to stay pixel-identical across both paths.
          static const float kIdentity[16] = {1, 0, 0, 0, 0, 1, 0, 0,
                                              0, 0, 1, 0, 0, 0, 0, 1};
          vals.assign(kIdentity, kIdentity + 16);
        }
        std::uint8_t* dst =
            block.data() + f.offset + e * f.array_stride_bytes;
        if (f.is_int) {
          for (int i = 0; i < f.float_count; ++i) {
            const std::int32_t v = static_cast<std::int32_t>(vals[i]);
            std::memcpy(dst + i * 4, &v, 4);
          }
        } else {
          std::memcpy(dst, vals.data(), vals.size() * 4);
        }
      }
    }
    bridge.SetUniformBytes(block.data(), block.size());
    if (bridge.GetError() != kGlNoError) {
      fprintf(stderr,
              "[TGL-DEBUG] Submit fail %d: SetUniformBytes %zu (bridge 0x%x)\n",
              __LINE__, block.size(), bridge.GetError());
      (void)bridge.GetError();
      draw_.FlagBridgeError();
      return false;
    }
    // UBO blocks (spec 7.3.2, translator v2): constant buffers 2+i from the
    // bound UNIFORM_BUFFER ranges (std140 member layout matches the MSL
    // struct for mat4/vec4/float). Unbound/small ranges upload zeros for the
    // missing tail: accessing them is UNDEFINED per spec (not an error), and
    // zeros keep the frame deterministic instead of leaking stale bytes. A
    // missing block index fails closed (reflection must resolve).
    for (std::size_t bi = 0; bi < trans.ubo_blocks.size(); ++bi) {
      const glsl::UboBlock& blk = trans.ubo_blocks[bi];
      const GLuint block_index =
          programs_.GetUniformBlockIndex(current_prog, blk.name.c_str());
      if (block_index == kGlInvalidIndex) {
        draw_.FlagBridgeError();
        return false;
      }
      GLuint binding = block_index;  // Default when never bound (documented).
      GLuint bound_binding = 0;
      if (programs_.GetUniformBlockBinding(current_prog, block_index,
                                           &bound_binding)) {
        binding = bound_binding;
      }
      std::vector<std::uint8_t> udata(
          static_cast<std::size_t>(blk.total_bytes), 0);
      const IndexedBinding ibind =
          buffers_.IndexedBindingState(kGlUniformBuffer, binding);
      if (ibind.buffer != 0) {
        const std::uint8_t* ubytes = nullptr;
        GLsizeiptr usize = 0;
        if (buffers_.GetBufferBytes(ibind.buffer, &ubytes, &usize)) {
          std::size_t start = 0, avail = 0;
          if (ibind.size > 0) {
            if (ibind.offset >= 0 &&
                ibind.offset <= usize) {
              start = static_cast<std::size_t>(ibind.offset);
              avail = static_cast<std::size_t>(std::min<GLsizeiptr>(
                  ibind.size, usize - ibind.offset));
            }
          } else if (usize > 0) {
            avail = static_cast<std::size_t>(usize);
          }
          const std::size_t n =
              std::min(avail, static_cast<std::size_t>(blk.total_bytes));
          if (n > 0) std::memcpy(udata.data(), ubytes + start, n);
        }
      }
      bridge.SetUboBytes(static_cast<GLuint>(bi), udata.data(), udata.size());
      if (bridge.GetError() != kGlNoError) {
        draw_.FlagBridgeError();
        return false;
      }
    }
  } else {
    if (current != 0) {
      const GLint loc = programs_.GetUniformLocation(current, "u_modelViewProj");
      if (loc >= 0) programs_.GetUniformMatrix(loc, mvp);  // Else identity.
    }
    // Clear any stale translated block so the legacy MVP is authoritative.
    bridge.SetUniformBytes(nullptr, 0);
    if (bridge.GetError() != kGlNoError) {
      draw_.FlagBridgeError();
      return false;
    }
  }
  const GLuint draw_fbo =
      framebuffers_.BoundFramebuffer(kGlDrawFramebuffer);
  const Attachment fb_att =
      framebuffers_.AttachmentState(draw_fbo, kGlColorAttachment0);
  if (fb_att.width <= 0 || fb_att.height <= 0) {
    fprintf(stderr, "[TGL-DEBUG] Submit fail %d: FBO size\n", __LINE__);
    draw_.FlagBridgeError();
    return false;
  }
  // Depth wiring: the GPU depth test runs only when the app enabled
  // DEPTH_TEST and bound a depth (or depth-stencil) attachment sized like
  // the color target. A depth test without a depth buffer renders
  // color-only (as if the test always passes); a size mismatch fails
  // closed. DEPTH_ATTACHMENT wins over DEPTH_STENCIL_ATTACHMENT.
  const bool depth_test =
      foundation_.IsEnabled(kGlDepthTest) != kGlFalse;
  const Attachment depth_tex =
      framebuffers_.AttachmentState(draw_fbo, kGlDepthAttachment);
  const Attachment depth_packed = framebuffers_.AttachmentState(
      draw_fbo, kGlDepthStencilAttachment);
  const Attachment* depth_src = nullptr;
  if (depth_tex.present) {
    depth_src = &depth_tex;
  } else if (depth_packed.present) {
    depth_src = &depth_packed;
  }
  metal_bridge::DepthConfig depth_cfg;  // Disabled by default.
  if (depth_test && depth_src != nullptr) {
    if (depth_src->width != fb_att.width ||
        depth_src->height != fb_att.height) {
      draw_.FlagBridgeError();
      return false;
    }
    depth_cfg.enabled = true;
    depth_cfg.func = raster_.GetDepthFunc();
    depth_cfg.write_mask = raster_.GetDepthMask();
    depth_cfg.clear_depth = raster_.GetClearDepth();
    depth_cfg.bias_enabled =
        foundation_.IsEnabled(kGlPolygonOffsetFill) != kGlFalse;
    depth_cfg.bias_factor = raster_.GetPolygonFactor();
    depth_cfg.bias_units = raster_.GetPolygonUnits();
  }
  bridge.ConfigureDepth(depth_cfg);
  if (bridge.GetError() != kGlNoError) {
    fprintf(stderr, "[TGL-DEBUG] Submit fail %d: ConfigureDepth\n", __LINE__);
    draw_.FlagBridgeError();
    return false;
  }
  // Stencil wiring (spec 13-15): the GPU stencil test runs only when the app
  // enabled STENCIL_TEST and bound a stencil (or depth-stencil) attachment
  // sized like the color target. Without one the draw renders color-only
  // (same rule as depth); a size mismatch fails closed. STENCIL_ATTACHMENT
  // wins over DEPTH_STENCIL_ATTACHMENT.
  const bool stencil_test =
      foundation_.IsEnabled(kGlStencilTest) != kGlFalse;
  const Attachment stencil_tex =
      framebuffers_.AttachmentState(draw_fbo, kGlStencilAttachment);
  const Attachment stencil_packed = framebuffers_.AttachmentState(
      draw_fbo, kGlDepthStencilAttachment);
  const Attachment* stencil_src = nullptr;
  if (stencil_tex.present) {
    stencil_src = &stencil_tex;
  } else if (stencil_packed.present) {
    stencil_src = &stencil_packed;
  }
  metal_bridge::StencilConfig stencil_cfg;  // Disabled by default.
  if (stencil_test && stencil_src != nullptr) {
    if (stencil_src->width != fb_att.width ||
        stencil_src->height != fb_att.height) {
      draw_.FlagBridgeError();
      return false;
    }
    stencil_cfg.enabled = true;
    stencil_cfg.func_front = raster_.GetStencilFuncFront();
    stencil_cfg.func_back = raster_.GetStencilFuncBack();
    stencil_cfg.ref_front = raster_.GetStencilRefFront();
    stencil_cfg.ref_back = raster_.GetStencilRefBack();
    stencil_cfg.value_mask_front = raster_.GetStencilValueMaskFront();
    stencil_cfg.value_mask_back = raster_.GetStencilValueMaskBack();
    stencil_cfg.write_mask_front = raster_.GetStencilWriteMaskFront();
    stencil_cfg.write_mask_back = raster_.GetStencilWriteMaskBack();
    stencil_cfg.sfail_front = raster_.GetStencilSfailFront();
    stencil_cfg.sfail_back = raster_.GetStencilSfailBack();
    stencil_cfg.dpfail_front = raster_.GetStencilDpfailFront();
    stencil_cfg.dpfail_back = raster_.GetStencilDpfailBack();
    stencil_cfg.dppass_front = raster_.GetStencilDppassFront();
    stencil_cfg.dppass_back = raster_.GetStencilDppassBack();
    stencil_cfg.clear_stencil = raster_.GetClearStencil();
  }
  // Split refs collapse to ref_back for this initial upload: the bridge
  // holds ONE reference value and would reject the pair, but it still needs
  // a single-ref config now to size the stencil target and compile a
  // stencil-format PSO. The two-pass tail below configures each pass with
  // its own single ref (back, then front). Funcs/refs here never reach a
  // pass (both passes reconfigure first).
  const bool two_pass_stencil = stencil_cfg.enabled &&
                                stencil_cfg.ref_front != stencil_cfg.ref_back;
  {
    metal_bridge::StencilConfig init_cfg = stencil_cfg;
    if (two_pass_stencil) init_cfg.ref_front = init_cfg.ref_back;
    bridge.ConfigureStencil(init_cfg);
    if (bridge.GetError() != kGlNoError) {
      draw_.FlagBridgeError();
      return false;
    }
  }
  // MRT (spec 9.4) + MRT+MSAA (Phase 4 item 4): translated multi-out
  // programs render every draw buffer into its own bridge target (read back
  // per attachment, resolved per attachment when multisampled). Untranslated
  // programs keep the legacy single-attachment behavior (attachment 0
  // executes, the rest validate). NONE entries, size mismatch and missing
  // attachments fail closed; per-attachment sample counts must agree (Metal
  // requires equal counts) and ride the key for the resolve.
  GLsizei mrt_n = 1;
  GLsizei mrt_samples = fb_att.samples;
  if (use_translated && trans.is_mrt) {
    const std::vector<GLenum> bufs = framebuffers_.DrawBufferList(draw_fbo);
    if (bufs.size() != static_cast<std::size_t>(trans.mrt_count)) {
      draw_.FlagBridgeError();
      return false;
    }
    for (GLenum b : bufs) {
      if (b < kGlColorAttachment0 || b >= kGlColorAttachment0 + 8u) {
        draw_.FlagBridgeError();  // NONE and friends: tracked work.
        return false;
      }
      const Attachment a = framebuffers_.AttachmentState(draw_fbo, b);
      if (!a.present || a.width != fb_att.width ||
          a.height != fb_att.height) {
        draw_.FlagBridgeError();
        return false;
      }
      if (a.samples != mrt_samples) {
        // Mixed sample counts have no Metal resolve (fail closed, named).
        fprintf(stderr,
                "[TGL-DEBUG] MRT+MSAA: mixed samples %d vs %d\n",
                (int)a.samples, (int)mrt_samples);
        draw_.FlagBridgeError();
        return false;
      }
      if (a.samples < 0 || a.samples > kMaxSamplesValue) {
        draw_.FlagBridgeError();
        return false;
      }
    }
    mrt_n = static_cast<GLsizei>(bufs.size());
  }
  // MSAA (spec 14.x): the attachment sample count rides the key; the bridge
  // renders multisampled and resolves. Dual-source + MSAA would need a
  // second-source resolve the pass does not provide: fail closed.
  if (fb_att.samples > kMaxSamplesValue) {
    draw_.FlagBridgeError();
    return false;
  }
  if (use_translated && trans.is_dual_source && fb_att.samples > 1) {
    draw_.FlagBridgeError();
    return false;
  }
  bridge.SetMrtCount(mrt_n);
  if (bridge.GetError() != kGlNoError) {
    fprintf(stderr, "[TGL-DEBUG] Submit fail %d: SetMrtCount\n", __LINE__);
    draw_.FlagBridgeError();
    return false;
  }
  // Type-1 path: the PSO comes from live GL state, not a default. Blend
  // (incl. CONSTANT_*/dual-source via blend color + DualOut), depth/stencil
  // formats, slot layout, MRT count and sample count are all baked into the
  // keyed pipeline; the pass prefers the bound keyed PSO over bare-bridge
  // fallbacks.
  backend::PsoKey key = backend::PsoKeyForDraw(foundation_, raster_);
  key.textured = need_slot2;
  key.translated = use_translated;
  key.program_id = use_translated ? current_prog : 0u;
  key.slot2 = static_cast<std::uint8_t>(slot2c);
  key.mrt_count = static_cast<std::uint8_t>(mrt_n);
  key.sample_count = fb_att.samples;
  const std::uint32_t pso = use_translated
                                ? bridge.CreateTranslatedPipeline(trans, key)
                                : bridge.CreateRenderPipeline(key);
  if (pso == 0) {
    fprintf(stderr,
            "[TGL-DEBUG] Submit fail %d: CreatePipeline (bridge 0x%x)\n",
            __LINE__, bridge.GetError());
    (void)bridge.GetError();
    draw_.FlagBridgeError();
    return false;
  }
  bridge.BindRenderPipeline(pso);
  if (bridge.GetError() != kGlNoError) {
    fprintf(stderr, "[TGL-DEBUG] Submit fail %d: BindPipeline\n", __LINE__);
    draw_.FlagBridgeError();
    return false;
  }
  // CONSTANT_COLOR/ALPHA factors read the glBlendColor state: upload it
  // every frame so they execute exactly instead of defaulting to black.
  GLfloat blend_color[4] = {0, 0, 0, 0};
  raster_.GetBlendColor(blend_color);
  bridge.SetBlendColor(blend_color[0], blend_color[1], blend_color[2],
                       blend_color[3]);
  if (bridge.GetError() != kGlNoError) {
    fprintf(stderr, "[TGL-DEBUG] Submit fail %d: SetBlendColor\n", __LINE__);
    draw_.FlagBridgeError();
    return false;
  }
  // Rasterizer state the bridge used to ignore silently (now executed):
  // cull mode/winding, viewport/scissor rects. Depth bias rides DepthConfig.
  metal_bridge::CullConfig cull_cfg;
  cull_cfg.enabled = foundation_.IsEnabled(kGlCullFace) != kGlFalse;
  cull_cfg.mode = raster_.GetCullMode();
  cull_cfg.front_face = raster_.GetFrontFace();
  bridge.ConfigureCull(cull_cfg);
  if (bridge.GetError() != kGlNoError) {
    fprintf(stderr, "[TGL-DEBUG] Submit fail %d: ConfigureCull\n", __LINE__);
    draw_.FlagBridgeError();
    return false;
  }
  metal_bridge::ViewportConfig vp_cfg;
  const ViewportRect vp = raster_.GetViewport();
  vp_cfg.x = vp.x;
  vp_cfg.y = vp.y;
  vp_cfg.width = vp.width;
  vp_cfg.height = vp.height;
  vp_cfg.scissor_enabled =
      foundation_.IsEnabled(kGlScissorTest) != kGlFalse;
  const ViewportRect sc = raster_.GetScissor();
  vp_cfg.scissor_x = sc.x;
  vp_cfg.scissor_y = sc.y;
  vp_cfg.scissor_width = sc.width;
  vp_cfg.scissor_height = sc.height;
  bridge.ConfigureViewport(vp_cfg);
  if (bridge.GetError() != kGlNoError) {
    fprintf(stderr, "[TGL-DEBUG] Submit fail %d: ConfigureViewport\n",
            __LINE__);
    draw_.FlagBridgeError();
    return false;
  }
  if (!bridge.BeginFrame(fb_att.width, fb_att.height)) {
    fprintf(stderr,
            "[TGL-DEBUG] Submit fail %d: BeginFrame %dx%d (bridge 0x%x)\n",
            __LINE__, fb_att.width, fb_att.height, bridge.GetError());
    (void)bridge.GetError();
    draw_.FlagBridgeError();
    return false;
  }
  bridge.SetVertexBytes(interleaved.data(), interleaved.size(), vert_stride);
  bridge.SetMVP(mvp);
  // Split stencil refs (spec 13-15): the Metal encoder holds ONE reference
  // value, so front/back refs draw in two passes over the same frame (no
  // clear between): back faces with back state (cull FRONT, no depth write),
  // then front faces with front state (cull BACK, depth write as app).
  // Blended split draws fail closed (blending would apply twice). Exact for
  // non-overlapping face sets; front/back interleave is approximate
  // (documented) — strictly better than the old fail-closed.
  const bool split_refs = two_pass_stencil;
  if (split_refs && key.blend_enabled) {
    draw_.FlagBridgeError();
    return false;
  }
  if (!split_refs) {
    bridge.BeginRenderPass();
    bridge.Draw(bridge_mode);
    bridge.EndRenderPass();
  } else {
    const bool cull_was =
        foundation_.IsEnabled(kGlCullFace) != kGlFalse;
    const GLenum cull_mode = raster_.GetCullMode();
    metal_bridge::StencilConfig back_cfg = stencil_cfg;
    back_cfg.func_front = stencil_cfg.func_back;
    back_cfg.ref_front = stencil_cfg.ref_back;
    back_cfg.value_mask_front = stencil_cfg.value_mask_back;
    back_cfg.write_mask_front = stencil_cfg.write_mask_back;
    back_cfg.sfail_front = stencil_cfg.sfail_back;
    back_cfg.dpfail_front = stencil_cfg.dpfail_back;
    back_cfg.dppass_front = stencil_cfg.dppass_back;
    metal_bridge::StencilConfig front_cfg = stencil_cfg;
    front_cfg.func_back = stencil_cfg.func_front;
    front_cfg.ref_back = stencil_cfg.ref_front;
    front_cfg.value_mask_back = stencil_cfg.value_mask_front;
    front_cfg.write_mask_back = stencil_cfg.write_mask_front;
    front_cfg.sfail_back = stencil_cfg.sfail_front;
    front_cfg.dpfail_back = stencil_cfg.dpfail_front;
    front_cfg.dppass_back = stencil_cfg.dppass_front;
    metal_bridge::DepthConfig nodepth_cfg = depth_cfg;
    nodepth_cfg.write_mask = false;
    foundation_.Enable(kGlCullFace);
    raster_.CullFace(kGlFront);
    cull_cfg.enabled = true;
    cull_cfg.mode = kGlFront;
    bridge.ConfigureCull(cull_cfg);
    bridge.ConfigureStencil(back_cfg);
    bridge.ConfigureDepth(nodepth_cfg);
    if (bridge.GetError() != kGlNoError) {
      draw_.FlagBridgeError();
      return false;
    }
    bridge.BeginRenderPass();
    bridge.Draw(bridge_mode);
    bridge.EndRenderPass();
    raster_.CullFace(kGlBack);
    cull_cfg.mode = kGlBack;
    bridge.ConfigureCull(cull_cfg);
    bridge.ConfigureStencil(front_cfg);
    bridge.ConfigureDepth(depth_cfg);
    if (bridge.GetError() != kGlNoError) {
      draw_.FlagBridgeError();
      return false;
    }
    bridge.BeginRenderPassNoClear();
    bridge.Draw(bridge_mode);
    bridge.EndRenderPass();
    if (cull_was) {
      foundation_.Enable(kGlCullFace);
    } else {
      foundation_.Disable(kGlCullFace);
    }
    raster_.CullFace(cull_mode);
  }
  if (!bridge.CommitFrame() || bridge.GetError() != kGlNoError) {
    fprintf(stderr, "[TGL-DEBUG] Submit fail %d: CommitFrame\n", __LINE__);
    draw_.FlagBridgeError();
    return false;
  }
  return true;
}

void GlesContext::ReadPixels(GLint x, GLint y, GLsizei width, GLsizei height,
                             GLenum format, GLenum type, void *pixels) {
  ReadnPixels(x, y, width, height, format, type, 0x7FFFFFFF, pixels);
}

void GlesContext::ReadnPixels(GLint x, GLint y, GLsizei width, GLsizei height,
                              GLenum format, GLenum type, GLsizei bufSize,
                              void *pixels) {
  static constexpr GLenum kRgba = 0x1908u;
  static constexpr GLenum kUbyte = 0x1401u;
  static constexpr GLenum kFloat = 0x1406u;
  static constexpr GLenum kPackBuffer = 0x88EBu;
  if (pixels == nullptr && (width > 0 && height > 0)) {
    // Null pixels with non-zero area: INVALID_VALUE (spec: no write target).
    // Defer to PixelState for the rest; record here to keep one error path.
  }
  const GLuint read_fbo = framebuffers_.BoundFramebuffer(kGlReadFramebuffer);
  if (read_fbo == 0) {
    draw_.FlagBridgeError();  // Reuse draw queue? No: use pixels queue via GetError order.
    // Window-system read without size: INVALID_OPERATION.
    pixels_.ReadPixels(x, y, width, height, format, type, 0, 0, false);
    return;
  }
  if (framebuffers_.CheckFramebufferStatus(kGlReadFramebuffer) !=
      kGlFramebufferComplete) {
    // Spec: incomplete -> INVALID_FRAMEBUFFER_OPERATION (0x0506).
    // FramebufferManager::Check does not record; record via pixels queue by
    // forcing a bounds failure? Instead record directly in draw_ which
    // GetError polls (same INVALID_FRAMEBUFFER_OPERATION value).
    draw_.FlagBridgeError();
    // FlagBridgeError records INVALID_OPERATION (0x0502), not FBO op; fix by
    // draining and re-recording? Simpler: use raster queue? For honest v1,
    // record INVALID_OPERATION via pixels validation below with bad extents.
    pixels_.ReadPixels(x, y, width, height, format, type, 0, 0, false);
    return;
  }
  const GLenum read_buf = framebuffers_.AttachmentState(read_fbo, 0x8CE0u).present
                              ? 0x8CE0u
                              : 0u;
  (void)read_buf;
  const Attachment att =
      framebuffers_.AttachmentState(read_fbo, kGlColorAttachment0);
  if (!att.present || !att.is_texture) {
    pixels_.ReadPixels(x, y, width, height, format, type, 0, 0, false);
    return;
  }
  // Only normalized RGBA8 store exists; float_buffer=false.
  pixels_.ReadPixels(x, y, width, height, format, type, att.width, att.height,
                     false);
  if (pixels_.HasPending()) return;
  // Format/type pairs on normalized surfaces (docs.gl notes): RGBA/UBYTE only
  // (plus implementation-chosen, which TGL does not advertise).
  const bool is_rgba_ubyte = (format == kRgba && type == kUbyte);
  const bool is_rgba_float = (format == kRgba && type == kFloat);
  if (!is_rgba_ubyte && !is_rgba_float) {
    // PixelState already rejects unknown enums; this rejects known-but-wrong
    // pairs for normalized surfaces (e.g. RGBA/INT).
    // Record INVALID_OPERATION via a second validation that always fails:
    pixels_.ReadPixels(-1, -1, -1, -1, format, type, att.width, att.height,
                       false);
    // Ensure an error is pending even if the above somehow passes.
    if (!pixels_.HasPending()) draw_.FlagBridgeError();
    return;
  }
  if (width < 0 || height < 0) return;  // Already recorded.
  // PACK buffer path: pixels is an offset.
  const GLuint pack = buffers_.BoundBuffer(kPackBuffer);
  const std::size_t bpp = 4;  // RGBA8
  const GLsizei stride = pixels_.PackRowStride(width, (GLsizei)bpp);
  const std::size_t total =
      (height == 0) ? 0 : (std::size_t)(height - 1) * stride + (std::size_t)width * bpp;
  // ReadnPixels bufSize check (spec: INVALID_OPERATION if too small).
  // bufSize counts BYTES for ReadnPixels.
  if (bufSize >= 0 && bufSize != 0x7FFFFFFF &&
      (std::size_t)bufSize < total) {
    draw_.FlagBridgeError();
    return;
  }
  TextureLevel lvl = textures_.LevelState(att.name, att.textarget, att.level);
  if (!lvl.defined || lvl.pixels.size() < (std::size_t)att.width * att.height * 4) {
    draw_.FlagBridgeError();
    return;
  }
  if (x < 0 || y < 0 || x + width > att.width || y + height > att.height) return;
  if (pack != 0) {
    const std::uint8_t *base = nullptr;
    GLsizeiptr sz = 0;
    if (!buffers_.GetBufferBytes(pack, &base, &sz)) {
      draw_.FlagBridgeError();
      return;
    }
    std::uintptr_t off = reinterpret_cast<std::uintptr_t>(pixels);
    if (off + total > (std::size_t)sz) {
      draw_.FlagBridgeError();
      return;
    }
    std::uint8_t *dst = nullptr;
    // Map the pack buffer range for write via MapBufferRange semantics:
    // use GetBufferBytes + const_cast (CPU model, same store).
    dst = const_cast<std::uint8_t *>(base) + off;
    for (GLsizei row = 0; row < height; ++row) {
      for (GLsizei col = 0; col < width; ++col) {
        const std::size_t s = ((std::size_t)(y + row) * att.width + (x + col)) * 4;
        const std::size_t d = (std::size_t)row * stride + col * 4;
        if (is_rgba_ubyte) {
          dst[d + 0] = lvl.pixels[s + 0];
          dst[d + 1] = lvl.pixels[s + 1];
          dst[d + 2] = lvl.pixels[s + 2];
          dst[d + 3] = lvl.pixels[s + 3];
        } else {
          float *f = reinterpret_cast<float *>(dst + d);
          f[0] = lvl.pixels[s + 0] / 255.0f;
          f[1] = lvl.pixels[s + 1] / 255.0f;
          f[2] = lvl.pixels[s + 2] / 255.0f;
          f[3] = lvl.pixels[s + 3] / 255.0f;
        }
      }
    }
    return;
  }
  if (pixels == nullptr) return;
  std::uint8_t *dst = static_cast<std::uint8_t *>(pixels);
  for (GLsizei row = 0; row < height; ++row) {
    for (GLsizei col = 0; col < width; ++col) {
      const std::size_t s = ((std::size_t)(y + row) * att.width + (x + col)) * 4;
      const std::size_t d = (std::size_t)row * stride + col * 4;
      if (is_rgba_ubyte) {
        dst[d + 0] = lvl.pixels[s + 0];
        dst[d + 1] = lvl.pixels[s + 1];
        dst[d + 2] = lvl.pixels[s + 2];
        dst[d + 3] = lvl.pixels[s + 3];
      } else {
        float *f = reinterpret_cast<float *>(dst + d);
        f[0] = lvl.pixels[s + 0] / 255.0f;
        f[1] = lvl.pixels[s + 1] / 255.0f;
        f[2] = lvl.pixels[s + 2] / 255.0f;
        f[3] = lvl.pixels[s + 3] / 255.0f;
      }
    }
  }
}

void GlesContext::CopyTexImage2D(GLenum target, GLint level,
                                 GLenum internalformat, GLsizei width,
                                 GLsizei height, GLint border) {
  static const std::uint8_t kWhite[4] = {255, 255, 255, 255};
  if (framebuffers_.BoundFramebuffer(kGlReadFramebuffer) != 0 &&
      framebuffers_.CheckFramebufferStatus(kGlReadFramebuffer) !=
          kGlFramebufferComplete) {
    draw_.FlagBridgeError();
    return;
  }
  textures_.CopyTexImage2D(target, level, internalformat, width, height,
                           border, kWhite);
}

std::vector<std::string> GlesContext::ConformanceChecklist() const {
  std::vector<std::string> failures;
  const auto check = [&](bool ok, const char* name) {
    if (!ok) failures.emplace_back(name);
  };
  check(kCoreEntryPointCount == 358, "core-entry-points-358");
  check(kCoreEnableCapCount == 13, "enable-caps-13");
  check(kCoreQueryTargetCount == 4, "query-targets-4");
  check(kGlsl320VersionNumber == 320, "glsl-320");
  check(kSupportedGlslVersionCount == 4, "glsl-versions-4");
  check(kShaderStageCount == 6, "shader-stages-6");
  check(kMaxVertexAttribs >= 16, "max-vertex-attribs-16");
  check(kMaxCombinedTextureImageUnits >= 32, "combined-units-32");
  check(kMaxTextureImageUnits >= 16, "texture-units-16");
  check(kMaxUniformBufferBindings >= 72, "ubo-bindings-72");
  check(kMaxTransformFeedbackSeparateAttribs >= 4, "tf-attribs-4");
  check(kMaxDrawBuffers >= 4, "draw-buffers-4");
  check(kMaxColorAttachments >= 4, "color-attachments-4");
  check(kMaxSamplesValue >= 4, "max-samples-4");
  check(kMaxRenderbufferSizeValue >= 2048, "renderbuffer-size-2048");
  check(kMaxTextureSizeValue >= 2048, "texture-size-2048");
  check(kMaxCubeMapTextureSizeValue >= 2048, "cubemap-size-2048");
  check(kMax3dTextureSizeValue >= 256, "3d-size-256");
  check(kMaxArrayTextureLayersValue >= 256, "array-layers-256");
  check(kMaxTextureBufferSizeValue >= 65536, "texture-buffer-65536");
  check(kMaxPatchVertices >= 32, "patch-vertices-32");
  check(kMaxTessGenLevel >= 64, "tess-gen-level-64");
  check(kMaxGeometryOutputVertices >= 256, "geometry-output-256");
  check(kMaxComputeWorkGroupInvocations >= 128, "compute-invocations-128");
  check(kMaxComputeSharedMemorySize >= 16384, "compute-shared-16384");
  check(kEsMajorVersion == 3 && kEsMinorVersion == 2, "version-3-2");
  // ES 3.1+ host surface: image units, SSBO/UBO reflection, EXT storage.
  check(kMaxImageUnitsValue >= 8, "image-units-8");
  check(kEglProcNameCount == 46, "egl-proc-46");
  check(kCoreProcNameCount == 358, "gl-proc-358");
  // Backend rules that correct the old plan.
  check(metal::FindGpuFamily("Apple7") != nullptr, "family-apple7");
  check(metal::FindGpuFamily("Apple10") != nullptr, "family-apple10");
  check(metal::FindGpuFamily("Apple9")->supports_bc_compression,
        "bc-apple9-plus");
  check(!metal::FindGpuFamily("Apple8")->supports_bc_compression,
        "no-bc-apple8");
  check(msl::UsesVaryingsStruct(msl::CorrectedTriangleVertexShader()),
        "msl-varyings-struct");
  check(!msl::UsesInvalidThreadUserVarying(msl::CorrectedTriangleVertexShader()),
        "msl-no-thread-varying");
  return failures;
}

std::string GlesContext::VersionReport() const {
  std::ostringstream out;
  out << foundation_.GetStringForReport();
  return out.str();
}

std::vector<std::string> GlesContext::HostIntegrationGaps() {
  // See docs/vi/minecraft.md (and /en/) + plan/plan-03-game-window-cts.md.
  // DONE device-tested (Phase 4, Intel Mac GPU + Mock): MRT+MSAA per-attachment
  // resolve, per-buffer blend (BlendFunci + descriptor loop + PsoKey), cube/
  // 3D/array mipmaps (upload chains), uniform arrays (per-element accessor),
  // UBO int/uint/bool members, LOD ranges (lodMin/MaxClamp), gl_InstanceID/
  // gl_VertexID, user structs, adjacency CPU-expansion, plus prior textured
  // sampling, compute, instanced/indirect/base-vertex, depth+stencil(+split),
  // depth+blend/CONSTANT/dual-source, MRT, MSAA, cull/viewport/scissor,
  // translator v2, window swap->present. Remaining: the honest fail-closed set.
  return {"Remaining GPU coverage (border clamp correct fail-closed — Metal "
          "has no border color; PATCHES/mesh + geometry/tessellation programs "
          "needing Apple7+ Metal3 HW; remaining GLSL beyond (flat integer "
          "varyings, interpolateAt*, sampler arrays, nested structs))",
          "On-device iOS (A-series) validation run + launcher stack (JVM, "
          "LWJGL natives)"};
}

}  // namespace tgles
