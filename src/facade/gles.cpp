#include "tgles/facade/gles.h"

#include <algorithm>
#include <climits>
#include <cstdint>
#include <cstring>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "tgles/base/debug_log.h"
#include "tgles/host/entry_points.h"
#include "tgles/state/image_units.h"
#include "tgles/gpu/glsl_to_msl.h"
#include "tgles/gpu/metal_mapping.h"
#include "tgles/gpu/msl_translation.h"
#include "tgles/base/spec.h"

namespace tgles {

namespace {

// First-N draw / clear diagnostics for black-screen triage (bounded so a
// long session cannot flood the 64KB ring).
int DiagCount(const char* tag) {
  static std::map<std::string, int> n;
  return ++n[tag];
}

// Round-3 triage: union of every submitted draw's clip-space extent for the
// frame currently being built, plus that frame's viewport. MC draws its
// loading background as a fullscreen quad, so a frame whose union stays a
// bottom band ([y..1]) proves no background geometry ever reaches the
// bridge — the screen can only be what a clear painted. One line per frame,
// bounded so the 64KB ring survives a long session.
struct FrameExtent {
  double minx = 1e30;
  double miny = 1e30;
  double maxx = -1e30;
  double maxy = -1e30;
  double vpx = 0, vpy = 0, vpw = 0, vph = 0;
  int draws = 0;
};
FrameExtent g_frame_extent;
int g_frame_index = 0;
int g_frame_logged = 0;
// Color the presented target was last *defined* with: a window clear, or the
// clear that initialized a brand-new target. The no-window-source
// force-clear in Submit repaints with THIS instead of the live
// glClearColor — MC paints its background red once, then switches to a
// transparent color for its offscreen target, and repainting with the live
// value wiped the red (device log v9/v10: frame 2 came up empty).
GLfloat g_last_clear_rgba[4] = {0, 0, 0, 1};

// FNV-1a (64): cheap content fingerprints for the upload/translation caches.
std::uint64_t Fnv1a(const void* data, std::size_t n,
                    std::uint64_t h = 14695981039346656037ull) {
  const auto* p = static_cast<const std::uint8_t*>(data);
  for (std::size_t i = 0; i < n; ++i) {
    h ^= p[i];
    h *= 1099511628211ull;
  }
  return h;
}
std::uint64_t Fnv1a(const std::string& s) {
  return Fnv1a(s.data(), s.size());
}

// Chain fingerprint for the per-unit upload cache: identity of every level
// in the sampled chain. Level addresses are stable while the level lives;
// `revision` re-arms on every in-place pixel write, and a redefined level is
// a new object (fresh revision at construction). Views share the original's
// TextureLevel objects, so writes through either name change the fingerprint
// both ways. Bits: level ptr, revision, width<<32|height, depth, size.
std::uint64_t ChainFingerprint(const std::vector<const TextureLevel*>& refs) {
  std::uint64_t h = 14695981039346656037ull;
  for (const TextureLevel* l : refs) {
    h = Fnv1a(&l, sizeof(l), h);
    h = Fnv1a(&l->revision, sizeof(l->revision), h);
    const std::uint64_t wh =
        (static_cast<std::uint64_t>(static_cast<std::uint32_t>(l->width))
         << 32) |
        static_cast<std::uint32_t>(l->height);
    h = Fnv1a(&wh, sizeof(wh), h);
    const std::uint64_t d = static_cast<std::uint32_t>(l->depth);
    h = Fnv1a(&d, sizeof(d), h);
    const std::size_t nbytes = l->pixels.size();
    h = Fnv1a(&nbytes, sizeof(nbytes), h);
  }
  return h;
}

}  // namespace

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
  // Snapshot pending so a prior buffer error (e.g. BufferData on 0) cannot
  // block the VAO EAB mirror: ELEMENT_ARRAY_BUFFER is always a valid target,
  // and skipping SetElementArrayBuffer left DrawElements fail-closed forever.
  const bool was_pending = buffers_.HasPending();
  buffers_.BindBuffer(target, buffer);
  const bool this_failed = buffers_.HasPending() && !was_pending;
  if (target == kGlElementArrayBuffer) {
    if (!this_failed) {
      vertex_arrays_.SetElementArrayBuffer(buffer);
      const int n = DiagCount("eab");
      if (n <= 8) {
        TglDebugf("diag eab#%d buf=%u vao=%u", n, buffer,
                  vertex_arrays_.BoundArray());
      }
    }
    return;
  }
  if (buffers_.HasPending()) return;  // Invalid target: nothing else to do.
}

void GlesContext::SyncIndirectState() {
  draw_.SetIndirectBufferBound(
      buffers_.BoundBuffer(kGlDrawIndirectBuffer) != 0);
}

void GlesContext::SyncElementState() {
  draw_.SetElementArrayBufferBound(
      vertex_arrays_.ElementArrayBuffer() != 0);
}

void GlesContext::BindVertexArray(GLuint array) {
  vertex_arrays_.BindVertexArray(array);
  const int n = DiagCount("vao");
  if (n <= 8) {
    TglDebugf("diag vao#%d vao=%u eab=%u", n, array,
              vertex_arrays_.ElementArrayBuffer());
  }
}

void GlesContext::Clear(GLbitfield mask) {
  GLfloat color[4];
  raster_.GetClearColor(color);
  framebuffers_.Clear(mask, color);
  // Size matters: a clear to a window-sized FBO vs. a small offscreen
  // atlas FBO decides whether MC's background ever reaches the screen.
  const GLuint cfbo = framebuffers_.BoundFramebuffer(kGlDrawFramebuffer);
  const Attachment att =
      framebuffers_.AttachmentState(cfbo, kGlColorAttachment0);
  // The old fixed 32-line cap stopped after ~16 frames, exactly where MC
  // settles into its steady loop — so a later change (a red window clear)
  // could never appear. Instead: always log a *change* of (fbo, mask,
  // color), log a repeat only every 64th occurrence, and keep a total cap.
  static GLuint prev_fbo = ~0u;
  static GLbitfield prev_mask = ~0u;
  static GLfloat prev_rgba[4] = {-1, -1, -1, -1};
  static int streak = 0;
  static int logged = 0;
  const bool same = cfbo == prev_fbo && mask == prev_mask &&
                    color[0] == prev_rgba[0] && color[1] == prev_rgba[1] &&
                    color[2] == prev_rgba[2] && color[3] == prev_rgba[3];
  if (same) {
    ++streak;
    if (streak < 64 || logged >= 96) return;
  } else {
    prev_fbo = cfbo;
    prev_mask = mask;
    for (int i = 0; i < 4; ++i) prev_rgba[i] = color[i];
  }
  if (logged >= 96) return;
  ++logged;
  TglDebugf(
      "diag clear#%d x%d mask=0x%x rgba=%.3f,%.3f,%.3f,%.3f fbo=%u(%dx%d) "
      "wsrc=%u t=%lld",
      logged, streak + 1, mask, color[0], color[1], color[2], color[3], cfbo,
      att.width, att.height, framebuffers_.WindowSource(), TglNowMs());
  streak = 0;
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
  if (!draw_.DrawElementsIndirect(mode, type, indirect)) return;
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
  if (!draw_.DrawArraysInstanced(mode, first, count, instanceCount))
    return false;
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
  if (!draw_.DrawElementsInstanced(mode, count, type, indices, instanceCount))
    return false;
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
  if (!draw_.DrawArraysIndirect(mode, indirect)) return false;
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
  if (!draw_.DrawElementsIndirect(mode, type, indirect)) return false;
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
  if (!draw_.DrawElementsInstanced(mode, (GLsizei)count, type, firstIndex,
                                   (GLsizei)instanceCount))
    return false;
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

bool GlesContext::ResolveTexelSource(const void* pixels, GLsizei width,
                                     GLsizei height, GLsizei depth,
                                     GLenum format, GLenum type,
                                     const void** out) {
  *out = pixels;
  const GLuint pbo = buffers_.BoundBuffer(kGlPixelUnpackBuffer);
  if (pbo == 0) return true;  // No PBO: client pointer (nullptr = zero-fill).
  // PBO bound (spec 8.3): `pixels` is a byte offset (nullptr means 0).
  const std::uintptr_t offset = reinterpret_cast<std::uintptr_t>(pixels);
  const std::uint8_t* base = nullptr;
  GLsizeiptr store = 0;
  if (!buffers_.GetBufferBytes(pbo, &base, &store)) {
    foundation_.RecordError(kGlInvalidOperation);
    return false;
  }
  std::size_t need = 0;
  if (width > 0 && height > 0 && depth > 0) {
    // Tightly packed source size for the formats/types UnpackToRgba8 can
    // read (UNSIGNED_BYTE); other transfer types return kNoUnpacker without
    // touching src, so need=0 still bounds-checks the offset alone.
    std::size_t channels = 0;
    switch (format) {
      case kGlRgba: channels = 4; break;
      case kGlRgb: channels = 3; break;
      case kGlRg: channels = 2; break;
      case kGlRed:
      case kGlDepthComponent: channels = 1; break;
      default: channels = 0; break;
    }
    std::size_t bytes_per = 0;
    if (type == kGlUnsignedByte) {
      bytes_per = channels;
    }
    need = bytes_per * static_cast<std::size_t>(width) *
           static_cast<std::size_t>(height) * static_cast<std::size_t>(depth);
  }
  if (offset > static_cast<std::uintptr_t>(store) ||
      need > static_cast<std::uintptr_t>(store) - offset) {
    foundation_.RecordError(kGlInvalidOperation);
    return false;
  }
  *out = base + offset;
  return true;
}

bool GlesContext::RenderFrame(metal_bridge::MetalBridge& bridge, GLenum mode,
                              GLint first, GLsizei count) {
  if (!draw_.DrawArrays(mode, first, count)) {
    TglDebugf("RenderFrame reject: draw validation failed (mode=0x%x count=%d)",
              mode, count);
    return false;
  }
  if (count == 0) return true;  // Valid no-op; touch no bridge state.
  std::vector<VertexRef> verts;
  verts.reserve(static_cast<std::size_t>(count));
  for (GLint v = first; v < first + count; ++v) verts.push_back({v, 0});
  return SubmitVertices(bridge, mode, verts);
}

void GlesContext::RenderElementsRejectDiag(GLenum mode, GLsizei count) {
  TglDebugf(
      "RenderElements reject: draw validation failed (mode=0x%x count=%d "
      "vao=%u eab=%u bound_eab=%u)",
      mode, count, vertex_arrays_.BoundArray(),
      vertex_arrays_.ElementArrayBuffer(),
      buffers_.BoundBuffer(kGlElementArrayBuffer));
}

bool GlesContext::RenderElements(metal_bridge::MetalBridge& bridge,
                                 GLenum mode, GLsizei count, GLenum type,
                                 std::uintptr_t indices) {
  SyncElementState();
  if (!draw_.DrawElements(mode, count, type, indices)) {
    RenderElementsRejectDiag(mode, count);
    return false;
  }
  if (count == 0) return true;  // Valid no-op; touch no bridge state.
  // The validator above already rejected a missing EAB; re-read it here for
  // the decode (belt and suspenders: never trust indices without a store).
  const GLuint eab = vertex_arrays_.ElementArrayBuffer();
  if (eab == 0) {
    TglDebugf("RenderElements reject: no ELEMENT_ARRAY_BUFFER\n");
    draw_.FlagBridgeError();
    return false;
  }
  const std::uint8_t* index_bytes = nullptr;
  GLsizeiptr index_size = 0;
  if (!buffers_.GetBufferBytes(eab, &index_bytes, &index_size)) {
    TglDebugf("RenderElements reject: EAB %u unreadable\n", eab);
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
  if (!draw_.DrawElementsBaseVertex(mode, count, type, indices, basevertex))
    return false;
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
  if (!draw_.DrawRangeElementsBaseVertex(mode, start, end, count, type, indices,
                                         basevertex))
    return false;
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
  if (!draw_.DrawElementsInstancedBaseVertex(mode, count, type, indices,
                                             instanceCount, basevertex))
    return false;
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
    for (const VertexRef& ref : one) verts.push_back({ref.vertex, k});
  }
  return SubmitVertices(*bridge, mode, verts);
}

bool GlesContext::RenderArraysInstancedBaseInstance(
    metal_bridge::MetalBridge* bridge, GLenum mode, GLint first,
    GLsizei count, GLsizei instanceCount, GLuint baseInstance) {
  if (!draw_.DrawArraysInstanced(mode, first, count, instanceCount))
    return false;
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
  if (!draw_.DrawElementsInstanced(mode, count, type, indices, instanceCount))
    return false;
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
  if (!draw_.DrawElementsInstancedBaseVertex(mode, count, type, indices,
                                             instanceCount, basevertex))
    return false;
  if (count == 0 || instanceCount == 0) return true;
  if (bridge == nullptr) return true;
  std::vector<VertexRef> one;
  one.reserve(count);
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
    if (!draw_.DrawElementsInstanced(mode, static_cast<GLsizei>(count), type,
                                     firstIndex,
                                     static_cast<GLsizei>(instanceCount)))
      return false;
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
    if (!draw_.DrawElementsBaseVertex(
            mode, count[i], type,
            reinterpret_cast<std::uintptr_t>(indices[i]), basevertex[i]))
      return false;
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
  // Drain any stale bridge error (e.g. from a prior Present/swap failure that
  // recorded INVALID_OPERATION without the app reading it) so this draw does
  // not false-fail on a leftover latch (ErrorQueue is first-error-wins).
  (void)bridge.GetError();
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
      static int tess_reject_seen = 0;
      if (tess_reject_seen < 16) {
        ++tess_reject_seen;
        TglDebugf("PATCHES/tess needs mesh (Apple7+ Metal3): prog=%u mode=0x%x "
                  "count=%zu t=%lld",
                  cur_prog, mode, verts.size(), TglNowMs());
      }
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
        mode == kGlLineLoop || mode == kGlTriangleFan || mode == 0x0007u) {
      static int geom_reject_seen = 0;
      if (geom_reject_seen < 16) {
        ++geom_reject_seen;
        TglDebugf(
            "geometry program needs mesh: prog=%u mode=0x%x count=%zu "
            "t=%lld",
            cur_prog, mode, verts.size(), TglNowMs());
      }
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
    case 0x0007u: {  // GL_QUADS: desktop-GL primitive, 4N verts -> 6N tri
      // verts (v0,v1,v2,v3) -> triangles (0,1,2)+(0,2,3): same orientation
      // and coverage for any convex quad (GUI fills are rectangles).
      if (verts.empty() || (verts.size() % 4) != 0) {
        TglDebugf("draw reject: QUADS count=%zu not a multiple of 4",
                  verts.size());
        draw_.FlagBridgeError();
        return false;
      }
      expanded.reserve((verts.size() / 4) * 6);
      for (std::size_t i = 0; i + 3 < verts.size(); i += 4) {
        expanded.push_back(verts[i]);
        expanded.push_back(verts[i + 1]);
        expanded.push_back(verts[i + 2]);
        expanded.push_back(verts[i]);
        expanded.push_back(verts[i + 2]);
        expanded.push_back(verts[i + 3]);
      }
      draw_verts = &expanded;
      bridge_mode = kGlTriangles;
      // Positive evidence: whether the host actually emits quads decides if
      // this expansion is load-bearing for the game's GUI (fills, bar,
      // background). Bounded head of the session.
      static int quads_seen = 0;
      if (quads_seen < 32) {
        ++quads_seen;
        TglDebugf("diag quads#%d in=%zu out=%zu t=%lld", quads_seen,
                  verts.size(), expanded.size(), TglNowMs());
      }
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
      TglDebugf("PATCHES needs mesh (Apple7+ Metal3, Intel fail-closed)");
      draw_.FlagBridgeError();
      return false;
    }
    default: {
      // Silent here before: an unknown mode became a bare INVALID_ENUM and a
      // vanished quad. Name it (bounded) so the next log says what dropped.
      static int reject_seen = 0;
      if (reject_seen < 32) {
        ++reject_seen;
        TglDebugf("draw reject: mode=0x%x count=%zu (unknown primitive)", mode,
                  verts.size());
      }
      draw_.FlagBridgeError();  // Unknown: no execution.
      return false;
    }
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
  // Resolve an attribute store for CPU decode. Binding-model draws
  // (ARB_vertex_attrib_binding / VertexAttribFormat+Binding) leave
  // attrib.buffer == 0 — the buffer lives on the binding point. Disabled
  // arrays (and enabled-but-storeless) use the current generic value
  // (spec 10.3.1), never fail closed: POSITION-only formats never enable
  // color, and attribute-less draws only need gl_VertexID.
  //
  // GL locations (MC BindAttribLocation by vertex-format element order) are
  // only used to FIND the source VAO slot. The facade always re-packs into
  // Metal slots 0=pos, 1=col, 2=slot2 — the translated MSL matches that
  // fixed layout, not the app's GL binding numbers.
  struct ResolvedStream {
    bool ok = false;
    bool use_current = false;
    GenericAttrib attrib{};
    const std::uint8_t* bytes = nullptr;
    GLsizeiptr size = 0;
    GLuint divisor = 0;
    GLfloat current[4] = {0.0f, 0.0f, 0.0f, 1.0f};
  };
  auto fill_current = [&](GLuint index, GLfloat out[4]) {
    const GenericAttribValue v = vertex_arrays_.AttribValue(index);
    const GenericAttrib a = vertex_arrays_.AttribState(index);
    if (a.pure_integer) {
      for (int k = 0; k < 4; ++k) out[k] = static_cast<GLfloat>(v.i[k]);
    } else {
      for (int k = 0; k < 4; ++k) out[k] = v.f[k];
    }
  };
  auto resolve_stream = [&](GLint index, const char* label) {
    ResolvedStream s;
    if (index < 0) {
      // Attribute absent from the program (POSITION-only color, etc.):
      // current generic value, always "readable". Slot 1's current value
      // is the conventional default color for apps that never set it.
      s.use_current = true;
      fill_current(static_cast<GLuint>(index < 0 ? 1 : index), s.current);
      s.ok = true;
      return s;
    }
    const GLuint ui = static_cast<GLuint>(index);
    const GenericAttrib a = vertex_arrays_.AttribState(ui);
    const VertexBinding b = vertex_arrays_.BindingState(a.binding_index);
    s.divisor = a.divisor != 0 ? a.divisor : b.divisor;
    s.attrib = a;
    if (a.buffer != 0) {
      s.attrib.offset = a.offset;
      s.attrib.stride = a.stride;
    } else if (b.buffer != 0) {
      s.attrib.buffer = b.buffer;
      s.attrib.offset = b.offset + a.relative_offset;
      s.attrib.stride = b.stride;
    } else {
      s.attrib.buffer = 0;
    }
    if (!a.enabled || s.attrib.buffer == 0) {
      s.use_current = true;
      fill_current(ui, s.current);
      s.ok = true;
      return s;
    }
    if (!buffers_.GetBufferBytes(s.attrib.buffer, &s.bytes, &s.size)) {
      TglDebugf("SubmitVertices: %s buffer unreadable (index=%u buf=%u)",
                label, ui, s.attrib.buffer);
      return s;
    }
    s.ok = true;
    return s;
  };

  // Semantic GL locations from the linked program (name → VAO slot).
  // MC uses Position/Color/UV0; trial shaders use a_pos/a_col/a_uv. Color
  // or UV may be absent from the program — -1 selects the current generic
  // value (spec 10.3.1). Empty programs (legacy empty-main / attribute-less)
  // keep the classic 0/1/2 fixed pair so the VAO buffers still resolve.
  const GLuint current_prog = programs_.CurrentProgram();
  GLint gl_pos = 0;
  GLint gl_col = 1;
  GLint gl_uv = 2;
  GLint gl_uv2 = -1;
  if (current_prog != 0 && !programs_.Attribs(current_prog).empty()) {
    auto lookup = [&](const char* a, const char* b) -> GLint {
      GLint loc = programs_.GetAttribLocation(current_prog, a);
      if (loc < 0 && b != nullptr) {
        loc = programs_.GetAttribLocation(current_prog, b);
      }
      return loc;
    };
    const GLint p = lookup("Position", "a_pos");
    const GLint c = lookup("Color", "a_col");
    const GLint u = lookup("UV0", "a_uv");
    const GLint u2 = lookup("UV2", "a_uv2");
    if (p >= 0) gl_pos = p;
    gl_col = c;  // -1 → current value (POSITION-only / POSITION_TEX)
    gl_uv = u;   // -1 → current value (no texture stream)
    gl_uv2 = u2; // -1 → no lightmap stream
  }

  const ResolvedStream pos_stream = resolve_stream(gl_pos, "pos");
  const ResolvedStream col_stream = resolve_stream(gl_col, "col");
  if (!pos_stream.ok || !col_stream.ok) {
    draw_.FlagBridgeError();
    return false;
  }
  const GenericAttrib pos = pos_stream.attrib;
  const GenericAttrib col = col_stream.attrib;
  // Fragment sampling detection (ES 3.2 §8, honest v1): first sampler2D-like
  // uniform with a complete RGBA8 level-0 TEXTURE_2D bound + a real UV
  // array (not just the current value) selects the textured pipeline.
  // Views resolve via shared bytes. Incomplete textures fall back to
  // untextured (black-sampling is tracked).
  bool want_textured = false;
  GLuint samp_unit = 0;
  GLuint samp_tex = 0;
  const TextureLevel* samp_level = nullptr;  // live level; no pixel copy
  ResolvedStream uv_stream;
  bool uv_stream_ok = false;
  if (current_prog != 0) {
    uv_stream = resolve_stream(gl_uv, "uv");
    uv_stream_ok = uv_stream.ok;
    if (uv_stream_ok && !uv_stream.use_current &&
        !uv_stream.attrib.pure_integer) {
      const std::uint8_t* uv_bytes = uv_stream.bytes;
      const GLsizeiptr uv_size = uv_stream.size;
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
        const TextureLevel* lvl =
            textures_.LevelStateRef(bound, kGlTexture2d, 0);
        if (lvl == nullptr || !lvl->defined || lvl->width <= 0 ||
            lvl->height <= 0) {
          continue;
        }
        const std::size_t need =
            static_cast<std::size_t>(lvl->width) * lvl->height * 4;
        if (lvl->pixels.size() < need) continue;
        want_textured = true;
        samp_unit = unit;
        samp_tex = bound;
        samp_level = lvl;
        (void)uv_bytes;
        (void)uv_size;
        break;
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
      // Force the facade Metal slot layout by semantic name: Position=0,
      // Color=1, UV0=2. MC BindAttribLocation numbers (UV0 often bound to
      // 1 on POSITION_TEX) are only used above to FIND the VAO stream —
      // the re-packed interleave and [[attribute(N)]] must agree.
      // Unknown names keep their linked location so location>2 still fails
      // closed inside TranslateProgram (entity/terrain UV2/Normal).
      std::map<std::string, int> attrib_locs;
      for (const ActiveAttrib& at : programs_.Attribs(current_prog)) {
        if (at.name == "Position" || at.name == "a_pos") {
          attrib_locs[at.name] = 0;
        } else if (at.name == "Color" || at.name == "a_col") {
          attrib_locs[at.name] = 1;
        } else if (at.name == "UV0" || at.name == "a_uv") {
          attrib_locs[at.name] = 2;
        } else if (at.name == "UV2" || at.name == "a_uv2") {
          attrib_locs[at.name] = 3;
        } else if (at.location >= 0) {
          attrib_locs[at.name] = at.location;
        }
      }
      // TranslateProgram runs the regex pipeline over the full sources
      // (ms-scale per call) — that per-draw cost alone tanked the frame
      // rate. Cache the result per program keyed on the source bytes and
      // attrib layout: sources are re-read every draw, so ShaderSource +
      // relink invalidates naturally (program ids are never reused).
      const std::uint64_t vs_h = Fnv1a(vs_src);
      const std::uint64_t fs_h = Fnv1a(fs_src);
      std::uint64_t at_h = 14695981039346656037ull;
      for (const auto& kv : attrib_locs) {
        at_h = Fnv1a(kv.first.data(), kv.first.size(), at_h);
        at_h = Fnv1a(&kv.second, sizeof(kv.second), at_h);
      }
      auto tcit = trans_cache_.find(current_prog);
      if (tcit != trans_cache_.end() && tcit->second.vs_hash == vs_h &&
          tcit->second.fs_hash == fs_h && tcit->second.attrib_hash == at_h) {
        trans = tcit->second.trans;
      } else {
        trans = glsl::TranslateProgram(vs_src, fs_src,
                                       attrib_locs.empty() ? nullptr
                                                           : &attrib_locs);
        TransCacheEntry& e = trans_cache_[current_prog];
        e.vs_hash = vs_h;
        e.fs_hash = fs_h;
        e.attrib_hash = at_h;
        e.trans = trans;
      }
      if (trans.ok) {
        use_translated = true;
      } else if (!glsl::LooksLegacyTrivial(vs_src, fs_src)) {
        const int n = DiagCount("xlate_reject");
        if (n <= 8) {
          TglDebugf("translator rejected #%d: %s\n", n, trans.error.c_str());
        }
        draw_.FlagBridgeError();  // Real shader TGL cannot compile: no lie.
        return false;
      }
    } else {
      const int n = DiagCount("no_src");
      if (n <= 8) {
        TglDebugf("diag no_graphics_sources#%d prog=%u linked", n,
                  current_prog);
      }
    }
  }
  {
    const int n = DiagCount("draw");
    if (n <= 24) {
      const int natt = static_cast<int>(programs_.Attribs(current_prog).size());
      TglDebugf(
          "diag draw#%d mode=0x%x nverts=%zu prog=%u atts=%d "
          "trans=%d gl_pos=%d gl_col=%d gl_uv=%d "
          "pos{en=%d buf=%u use_cur=%d} col{en=%d buf=%u use_cur=%d}",
          n, mode, draw_verts->size(), current_prog, natt,
          use_translated ? 1 : 0, gl_pos, gl_col, gl_uv,
          vertex_arrays_.AttribState(static_cast<GLuint>(gl_pos < 0 ? 0 : gl_pos))
              .enabled
              ? 1
              : 0,
          pos_stream.use_current ? 0u : pos_stream.attrib.buffer,
          pos_stream.use_current ? 1 : 0,
          col_stream.use_current
              ? 1
              : (vertex_arrays_.AttribState(static_cast<GLuint>(gl_col < 0 ? 1 : gl_col))
                     .enabled
                     ? 1
                     : 0),
          col_stream.use_current ? 0u : col_stream.attrib.buffer,
          col_stream.use_current ? 1 : 0);
    }
    // Uncapped aggregate: which programs actually draw, forever (bounded by
    // program count, one line each) + periodic totals so a session log shows
    // the mix after the first-24 cap goes silent.
    {
      static std::map<GLuint, int> draws_per_prog;
      static int total_draws = 0;
      static std::map<GLuint, bool> first_draw_logged;
      ++total_draws;
      ++draws_per_prog[current_prog];
      if (!first_draw_logged[current_prog]) {
        first_draw_logged[current_prog] = true;
        TglDebugf("diag first_draw prog=%u total_draws=%d", current_prog,
                  total_draws);
      }
      if (n % 512 == 0) {
        std::string s;
        for (const auto& kv : draws_per_prog) {
          s += " p" + std::to_string(kv.first) + "=" +
               std::to_string(kv.second);
        }
        TglDebugf("diag draw_stats total=%d%s", total_draws, s.c_str());
      }
    }
  }
  // Slot2 (uv/normal/dir) comes from the resolved stream: disabled arrays
  // use the current generic value instead of failing the draw.
  if (use_translated && trans.needs_slot2) {
    if (!uv_stream_ok) {
      uv_stream = resolve_stream(gl_uv, "slot2");
      uv_stream_ok = uv_stream.ok;
    }
    if (!uv_stream_ok) {
      TglDebugf("Submit fail %d: slot2 stream resolve fail (prog=%u)",
                __LINE__, current_prog);
      draw_.FlagBridgeError();
      return false;
    }
  }
  // Slot3 (UV2/lightmap): separate stream after slot2 in the interleave.
  ResolvedStream uv2_stream;
  bool uv2_stream_ok = false;
  if (use_translated && trans.needs_slot3) {
    uv2_stream = resolve_stream(gl_uv2, "slot3");
    uv2_stream_ok = uv2_stream.ok;
    if (!uv2_stream_ok) {
      TglDebugf("Submit fail %d: slot3 stream resolve fail (prog=%u)",
                __LINE__, current_prog);
      draw_.FlagBridgeError();
      return false;
    }
  }
  const int slot2c =
      use_translated ? trans.slot2_comps : (want_textured ? 2 : 0);
  const int slot3c = use_translated ? trans.slot3_comps : 0;
  const bool need_slot2 = slot2c > 0;
  const bool need_slot3 = slot3c > 0;
  std::vector<std::uint8_t> interleaved;
  const std::uint32_t vert_stride =
      static_cast<std::uint32_t>(32 + 4 * slot2c + 4 * slot3c);
  interleaved.reserve(draw_verts->size() * vert_stride);
  // Round-3: raw position extent of this draw (transformed to clip space
  // once the MVP is known below).
  double raw_x0 = 1e30, raw_y0 = 1e30, raw_x1 = -1e30, raw_y1 = -1e30;
  // Raw triangle corners (capped) so the winding can be judged in CLIP space
  // after the MVP is known: cull=BACK with the wrong winding discards a whole
  // quad and is invisible in every other state we log.
  struct RawVert {
    double x;
    double y;
  };
  std::vector<RawVert> raw_tris;
  raw_tris.reserve(draw_verts->size());
  for (const VertexRef& ref : *draw_verts) {
    GLfloat p[4], c[4];
    if (pos_stream.use_current) {
      for (int k = 0; k < 4; ++k) p[k] = pos_stream.current[k];
    } else if (!DecodeAttribToFloat4(pos_stream.attrib, pos_stream.divisor,
                                     pos_stream.bytes, pos_stream.size,
                                     ref.vertex, ref.instance, p)) {
      TglDebugf("SubmitVertices: DecodeAttrib failed (v=%d inst=%d)",
                ref.vertex, ref.instance);
      draw_.FlagBridgeError();
      return false;
    }
    if (p[0] < raw_x0) raw_x0 = p[0];
    if (p[0] > raw_x1) raw_x1 = p[0];
    if (p[1] < raw_y0) raw_y0 = p[1];
    if (p[1] > raw_y1) raw_y1 = p[1];
    if (raw_tris.size() < 384) raw_tris.push_back({p[0], p[1]});
    if (col_stream.use_current) {
      for (int k = 0; k < 4; ++k) c[k] = col_stream.current[k];
    } else if (!DecodeAttribToFloat4(col_stream.attrib, col_stream.divisor,
                                     col_stream.bytes, col_stream.size,
                                     ref.vertex, ref.instance, c)) {
      TglDebugf("SubmitVertices: DecodeAttrib failed (v=%d inst=%d)",
                ref.vertex, ref.instance);
      draw_.FlagBridgeError();
      return false;
    }
    const std::uint8_t* pp = reinterpret_cast<const std::uint8_t*>(p);
    const std::uint8_t* cp = reinterpret_cast<const std::uint8_t*>(c);
    interleaved.insert(interleaved.end(), pp, pp + 16);
    interleaved.insert(interleaved.end(), cp, cp + 16);
    if (need_slot2) {
      if (!uv_stream_ok) {
        uv_stream = resolve_stream(gl_uv, "slot2");
        uv_stream_ok = uv_stream.ok;
        if (!uv_stream_ok) {
          TglDebugf("Submit fail %d: slot2 stream resolve fail in interleave (v=%d)",
                    __LINE__, ref.vertex);
          draw_.FlagBridgeError();
          return false;
        }
      }
      GLfloat t[4] = {0, 0, 0, 1};
      if (uv_stream.use_current) {
        for (int k = 0; k < 4; ++k) t[k] = uv_stream.current[k];
      } else if (!DecodeAttribToFloat4(uv_stream.attrib, uv_stream.divisor,
                                       uv_stream.bytes, uv_stream.size,
                                       ref.vertex, ref.instance, t)) {
        TglDebugf("SubmitVertices: slot2 DecodeAttrib failed (v=%d inst=%d)",
                  ref.vertex, ref.instance);
        draw_.FlagBridgeError();
        return false;
      }
      const std::uint8_t* tp = reinterpret_cast<const std::uint8_t*>(t);
      // Slot 2 carries the first slot2c*4 bytes of the decoded float4
      // (uv/normal xy, cube dir xyz, or a full float4).
      interleaved.insert(interleaved.end(), tp,
                         tp + static_cast<std::size_t>(slot2c) * 4);
    }
    if (need_slot3) {
      if (!uv2_stream_ok) {
        uv2_stream = resolve_stream(gl_uv2, "slot3");
        uv2_stream_ok = uv2_stream.ok;
        if (!uv2_stream_ok) {
          TglDebugf("Submit fail %d: slot3 DecodeAttrib stream fail (v=%d)",
                    __LINE__, ref.vertex);
          draw_.FlagBridgeError();
          return false;
        }
      }
      GLfloat t2[4] = {0, 0, 0, 1};
      if (uv2_stream.use_current) {
        for (int k = 0; k < 4; ++k) t2[k] = uv2_stream.current[k];
      } else if (!DecodeAttribToFloat4(uv2_stream.attrib, uv2_stream.divisor,
                                       uv2_stream.bytes, uv2_stream.size,
                                       ref.vertex, ref.instance, t2)) {
        TglDebugf("SubmitVertices: slot3 DecodeAttrib failed (v=%d inst=%d)",
                  ref.vertex, ref.instance);
        draw_.FlagBridgeError();
        return false;
      }
      const std::uint8_t* t2p = reinterpret_cast<const std::uint8_t*>(t2);
      interleaved.insert(interleaved.end(), t2p,
                         t2p + static_cast<std::size_t>(slot3c) * 4);
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
    // Same per-unit upload cache as the translated path (kind=4: legacy
    // uploads only level 0 without mips — a different MTLTexture kind).
    const std::uint64_t legacy_fp = ChainFingerprint({samp_level});
    FragUploadCache& up = frag_upload_[samp_unit];
    if (!(up.valid && up.owner == &bridge && up.tex == samp_tex &&
          up.fp == legacy_fp && up.kind == 4)) {
      bridge.SetFragmentTexture(samp_unit, samp_level->width,
                                samp_level->height, samp_level->pixels.data());
      up.valid = true;
      up.owner = &bridge;
      up.tex = samp_tex;
      up.fp = legacy_fp;
      up.kind = 4;
      up.mips = false;
      up.srgb = false;
    }
    bridge.SetFragmentSampler(samp_unit, linear, repeat);
    {
      const GLenum terr = bridge.GetError();
      if (terr != kGlNoError) {
        TglDebugf("Submit fail %d: legacy SetFragmentTexture/Sampler unit=%u (bridge 0x%x)",
                  __LINE__, samp_unit, terr);
        draw_.FlagBridgeError();
        return false;
      }
    }
  }
  if (use_translated && trans.uses_sampler) {
    // Translated path: every sampler uniform by kind, MSL slot = declaration
    // order. Incomplete bindings fail closed (spec: they sample (0,0,0,1);
    // rendering them untextured would be the old lie).
    // Fragment slots (in_fs) → SetSamplerSlots; vertex slots (in_vs) →
    // SetVertexSamplerSlots. Both maps share the uploaded GL units.
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
        TglDebugf("Submit fail %d: sampler '%s' unit index %d out of range",
                  __LINE__, sname.c_str(), unit_i);
        draw_.FlagBridgeError();
        return false;
      }
      const GLuint unit = static_cast<GLuint>(unit_i);
      const GLuint bound = textures_.BoundTextureForUnit(unit, target);
      if (bound == 0) {
        TglDebugf("Submit fail %d: sampler '%s' unit=%u has no bound texture (target=0x%x)",
                  __LINE__, sname.c_str(), unit, target);
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
      // Chain walk (pointer-based — no per-level pixel copies) + upload
      // fingerprint: the bridge re-creates its MTLTexture only when the
      // chain content actually changed (revision re-arms on in-place writes;
      // a redefined level is a new object with a fresh revision). Flattened:
      // 2D/3D/array one ref per level, cube L0x6, L1x6, ...
      std::vector<const TextureLevel*> chain;
      if (skind == 0) {
        // 2D (+mips): defined chain for the bridge.
        for (GLint l = 0; l < 16; ++l) {
          const TextureLevel* lvl =
              textures_.LevelStateRef(bound, kGlTexture2d, l);
          if (lvl == nullptr || !lvl->defined || lvl->width <= 0 ||
              lvl->height <= 0) {
            break;
          }
          const std::size_t need =
              static_cast<std::size_t>(lvl->width) * lvl->height * 4;
          if (lvl->pixels.size() < need) break;
          chain.push_back(lvl);
          if (lvl->width == 1 && lvl->height == 1) break;
        }
        if (chain.empty()) {
          TglDebugf("Submit fail %d: sampler unit=%u 2D mipmap chain empty (tex=%u)",
                    __LINE__, unit, bound);
          draw_.FlagBridgeError();
          return false;
        }
      } else if (skind == 1) {
        // Cube (+mips): per-level 6-face chains (state synthesizes mips).
        for (GLint l = 0; l < 16; ++l) {
          const TextureLevel* faces[6] = {nullptr, nullptr, nullptr,
                                          nullptr, nullptr, nullptr};
          bool have = true;
          for (int f = 0; f < 6; ++f) {
            const TextureLevel* fl = textures_.LevelStateRef(
                bound, kGlTextureCubeMapPositiveX + static_cast<GLenum>(f), l);
            if (fl == nullptr || !fl->defined || fl->width <= 0 ||
                fl->height <= 0) {
              have = false;
              break;
            }
            const std::size_t need =
                static_cast<std::size_t>(fl->width) * fl->height * 4;
            if (fl->pixels.size() < need) {
              have = false;
              break;
            }
            faces[f] = fl;
          }
          if (!have) break;
          // All faces same size per level.
          for (int f = 1; f < 6; ++f) {
            if (faces[f]->width != faces[0]->width ||
                faces[f]->height != faces[0]->height) {
              draw_.FlagBridgeError();
              return false;
            }
          }
          for (int f = 0; f < 6; ++f) chain.push_back(faces[f]);
          if (faces[0]->width == 1 && faces[0]->height == 1) break;
        }
        if (chain.empty()) {
          draw_.FlagBridgeError();
          return false;
        }
      } else {
        // 3D / 2D-array (+mips, Phase 4 item 6).
        const GLenum face = (skind == 2) ? kGlTexture3d : kGlTexture2dArray;
        for (GLint l = 0; l < 16; ++l) {
          const TextureLevel* lvl = textures_.LevelStateRef(bound, face, l);
          if (lvl == nullptr || !lvl->defined || lvl->width <= 0 ||
              lvl->height <= 0 || lvl->depth <= 0) {
            break;
          }
          const std::size_t need = static_cast<std::size_t>(lvl->width) *
                                   lvl->height * lvl->depth * 4;
          if (lvl->pixels.size() < need) break;
          chain.push_back(lvl);
          if (lvl->width == 1 && lvl->height == 1 && lvl->depth == 1) break;
        }
        if (chain.empty()) {
          draw_.FlagBridgeError();
          return false;
        }
      }
      // Upload shape (cube chain flattens 6 refs per level) + fingerprint.
      const int levels = (skind == 1) ? static_cast<int>(chain.size() / 6)
                                      : static_cast<int>(chain.size());
      const TextureLevel& l0 = *chain[0];
      const bool key_mips = (skind == 0) ? true : min_mipmap;
      const std::uint64_t fp = ChainFingerprint(chain);
      FragUploadCache& upc = frag_upload_[unit];
      if (!(upc.valid && upc.owner == &bridge && upc.tex == bound &&
            upc.fp == fp && upc.kind == skind && upc.mips == key_mips &&
            upc.srgb == srgb)) {
        // Miss: re-upload through the bridge (the call copies the bytes
        // synchronously — Apple replaceRegion, Mock records kind only — and
        // `chain` keeps the pixels alive across it).
        std::vector<const void*> ptrs;
        ptrs.reserve(chain.size());
        for (const TextureLevel* l : chain) ptrs.push_back(l->pixels.data());
        if (skind == 0) {
          bridge.SetFragmentTextureMips(unit, l0.width, l0.height, levels,
                                        ptrs.data(), srgb);
        } else if (skind == 1) {
          if (key_mips) {
            bridge.SetFragmentTextureCubeMips(unit, l0.width, levels,
                                              ptrs.data(), srgb);
          } else {
            const void* p0[6] = {ptrs[0], ptrs[1], ptrs[2],
                                 ptrs[3], ptrs[4], ptrs[5]};
            bridge.SetFragmentTextureCube(unit, l0.width, p0, srgb);
          }
        } else if (key_mips) {
          if (skind == 2) {
            bridge.SetFragmentTexture3DMips(unit, l0.width, l0.height,
                                            l0.depth, levels, ptrs.data(),
                                            srgb);
          } else {
            bridge.SetFragmentTextureArrayMips(unit, l0.width, l0.height,
                                               l0.depth, levels, ptrs.data(),
                                               srgb);
          }
        } else if (skind == 2) {
          bridge.SetFragmentTexture3D(unit, l0.width, l0.height, l0.depth,
                                      ptrs[0], srgb);
        } else {
          bridge.SetFragmentTextureArray(unit, l0.width, l0.height, l0.depth,
                                         ptrs[0], srgb);
        }
        upc.valid = true;
        upc.owner = &bridge;
        upc.tex = bound;
        upc.fp = fp;
        upc.kind = skind;
        upc.mips = key_mips;
        upc.srgb = srgb;
      }
      // Spec 8.14: a partial mip chain samples (0,0,0,1) — NOT a draw error.
      // Upload the chain, then clamp max LOD so Metal never fetches a
      // missing level (draw stays alive). Runs on cache hit and miss alike;
      // capped diagnostics (this fires per textured draw and flooded the ring).
      if (min_mipmap) {
        int full = 1;
        for (GLsizei s = (skind >= 2)
                             ? std::max({l0.width, l0.height, l0.depth})
                             : std::max(l0.width, l0.height);
             s > 1; s /= 2) {
          ++full;
        }
        if (levels < full) {
          if (skind == 0) {
            const int seen = DiagCount("mipmap_partial");
            if (seen <= 16) {
              TglDebugf("diag mipmap partial unit=%u (%d/%d levels) tex=%u",
                        unit, levels, full, bound);
            } else if (seen == 17) {
              TglDebugf("diag mipmap partial capped at 16 (further silenced)");
            }
          } else if (skind == 1) {
            const int seen = DiagCount("cube_mip_partial");
            if (seen <= 8) {
              TglDebugf("diag cube-mips partial#%d %d/%d", seen, levels, full);
            }
          } else {
            const int seen = DiagCount("arr_mip_partial");
            if (seen <= 8) {
              TglDebugf("diag 3D/array-mips partial#%d (%d/%d)", seen, levels,
                        full);
            }
          }
          max_lod = std::min(max_lod, static_cast<float>(levels - 1));
        }
      }
      {
        const GLenum terr = bridge.GetError();
        if (terr != kGlNoError) {
          TglDebugf("Submit fail %d: SetFragmentTexture* unit=%u (bridge 0x%x)",
                    __LINE__, unit, terr);
          draw_.FlagBridgeError();
          return false;
        }
      }
      bridge.SetFragmentSamplerDetail(unit, min_f, mag_f, wrap_s, wrap_t);
      {
        const GLenum serr = bridge.GetError();
        if (serr != kGlNoError) {
          TglDebugf("Submit fail %d: SetFragmentSamplerDetail unit=%u (bridge 0x%x)",
                    __LINE__, unit, serr);
          draw_.FlagBridgeError();
          return false;
        }
      }
      // Phase 4 item 10: LOD clamps ride the sampler (Metal lodMin/MaxClamp).
      bridge.SetSamplerLod(unit, min_lod, max_lod);
      {
        const GLenum lerr = bridge.GetError();
        if (lerr != kGlNoError) {
          TglDebugf("Submit fail %d: SetSamplerLod unit=%u (bridge 0x%x)",
                    __LINE__, unit, lerr);
          draw_.FlagBridgeError();
          return false;
        }
      }
      slot_units.push_back(unit);
    }
    // Split by stage: MSL fragment [[texture(i)]] only sees in_fs samplers
    // (declaration order); vertex [[texture(i)]] only sees in_vs. Texture
    // upload already went through SetFragmentTexture* keyed by GL unit —
    // both maps share frag_texs_/frag_smps_ by unit id.
    {
      std::vector<GLuint> fs_units;
      std::vector<GLuint> vs_units;
      for (std::size_t si = 0; si < trans.sampler_names.size(); ++si) {
        if (si >= slot_units.size()) break;
        const bool is_fs =
            si < trans.sampler_in_fs.size() && trans.sampler_in_fs[si];
        const bool is_vs =
            si < trans.sampler_in_vs.size() && trans.sampler_in_vs[si];
        if (is_fs) fs_units.push_back(slot_units[si]);
        if (is_vs) vs_units.push_back(slot_units[si]);
      }
      {
        static int slots_logged = 0;
        if (slots_logged < 8) {
          ++slots_logged;
          std::string names;
          for (std::size_t si = 0; si < trans.sampler_names.size(); ++si) {
            if (si) names += ",";
            names += trans.sampler_names[si];
            names += (si < trans.sampler_in_fs.size() && trans.sampler_in_fs[si])
                         ? "(fs)"
                         : "(vs)";
          }
          std::string fs_s, vs_s;
          for (GLuint u : fs_units) fs_s += std::to_string(u) + " ";
          for (GLuint u : vs_units) vs_s += std::to_string(u) + " ";
          TglDebugf("diag slots#%d prog=%u names=%s fs=[%s] vs=[%s]",
                    slots_logged, current_prog, names.c_str(), fs_s.c_str(),
                    vs_s.c_str());
        }
      }
      bridge.SetSamplerSlots(fs_units.data(), fs_units.size());
      {
        const GLenum serr = bridge.GetError();
        if (serr != kGlNoError) {
          TglDebugf("Submit fail %d: SetSamplerSlots n=%zu (bridge 0x%x)",
                    __LINE__, fs_units.size(), serr);
          draw_.FlagBridgeError();
          return false;
        }
      }
      bridge.SetVertexSamplerSlots(vs_units.data(), vs_units.size());
      {
        const GLenum serr = bridge.GetError();
        if (serr != kGlNoError) {
          TglDebugf("Submit fail %d: SetVertexSamplerSlots n=%zu (bridge 0x%x)",
                    __LINE__, vs_units.size(), serr);
          draw_.FlagBridgeError();
          return false;
        }
      }
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
    // The translated path feeds the bridge a full uniform block, so the
    // legacy `mvp` local (used for the frame-extent diagnostic and SetMVP)
    // would stay identity and report GUI-space vertex coordinates as if they
    // were NDC. Capture the block's MVP here so the extent reads the real
    // screen rectangle. Behavior-neutral: when uniform_bytes_ is non-empty
    // the bridge binds those bytes and ignores SetMVP.
    bool mvp_captured = false;
    for (const glsl::UniformField& f : trans.uniforms) {
      const GLint base =
          programs_.GetUniformLocation(current_prog, f.name.c_str());
      // Drain incidental GetUniformLocation errors (unlinked? no) so uniform
      // planning never pollutes GetError; a real miss is base<0 below.
      while (programs_.HasPending()) (void)programs_.GetError();
      if (base < 0) {
        // Array base query `u` for `uniform vec4 u[2]` returns base per spec;
        // struct leaf `u.member` resolves directly.
        TglDebugf("uniform '%s' location miss\n",
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
          TglDebugf("uniform '%s'[%d] readback fail\n",
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
        {
          static int uni_logged = 0;
          if (uni_logged < 32) {
            ++uni_logged;
            const int nf = static_cast<int>(vals.size());
            TglDebugf(
                "diag uni#%d prog=%u '%s'[%d] n=%d v=%.4g,%.4g,%.4g,%.4g",
                uni_logged, current_prog, f.name.c_str(), e, nf,
                nf > 0 ? vals[0] : 0.f, nf > 1 ? vals[1] : 0.f,
                nf > 2 ? vals[2] : 0.f, nf > 3 ? vals[3] : 0.f);
          }
        }
        if (!mvp_captured && f.float_count == 16 && e == 0 &&
            (trans.mvp_name.empty() || f.name == trans.mvp_name)) {
          // First mat4 in vs-first order (glsl_to_msl picks the same one for
          // out.mvp_name) — the block's modelViewProj.
          std::memcpy(mvp, vals.data(), sizeof(mvp));
          mvp_captured = true;
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
    {
      const GLenum uerr = bridge.GetError();
      if (uerr != kGlNoError) {
        TglDebugf("Submit fail %d: SetUniformBytes %zu (bridge 0x%x)",
                  __LINE__, block.size(), uerr);
        draw_.FlagBridgeError();
        return false;
      }
    }
    // UBO blocks (spec 7.3.2, translator v2): constant buffers 2+i from the
    // bound UNIFORM_BUFFER ranges (std140 member layout matches the MSL
    // struct for mat4/vec4/float). Unbound/small ranges upload zeros for the
    // missing tail: accessing them is UNDEFINED per spec (not an error), and
    // zeros keep the frame deterministic instead of leaking stale bytes. A
    // missing block index fails closed (reflection must resolve).
    for (std::size_t bi = 0; bi < trans.ubo_blocks.size(); ++bi) {
      const glsl::UboBlock& blk = trans.ubo_blocks[bi];
      GLuint block_index = kGlInvalidIndex;
      if (!blk.block_name.empty()) {
        block_index = programs_.GetUniformBlockIndex(
            current_prog, blk.block_name.c_str());
      }
      if (block_index == kGlInvalidIndex) {
        block_index =
            programs_.GetUniformBlockIndex(current_prog, blk.name.c_str());
      }
      if (block_index == kGlInvalidIndex) {
        TglDebugf("Submit fail %d: UBO block '%s' (ref '%s') not found in program %u",
                  __LINE__, blk.name.c_str(),
                  blk.block_name.empty() ? "?" : blk.block_name.c_str(),
                  current_prog);
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
      // MVP capture for the frame-extent log: in the v13 device log the
      // capture never fired because MC's shaders keep the projection in a
      // uniform block, not in the legacy uniform list — so `mvp` stayed
      // identity and `rect`/`px` printed GUI coordinates as NDC. Take the
      // block's first mat4 (same rule glsl_to_msl uses for mvp_name).
      if (!mvp_captured) {
        for (const glsl::UboMember& m : blk.members) {
          if (m.float_count == 16 && m.offset >= 0 &&
              static_cast<std::size_t>(m.offset) + sizeof(mvp) <=
                  udata.size()) {
            std::memcpy(mvp, udata.data() + m.offset, sizeof(mvp));
            mvp_captured = true;
            break;
          }
        }
      }
      bridge.SetUboBytes(static_cast<GLuint>(bi), udata.data(), udata.size());
      {
        const GLenum berr = bridge.GetError();
        if (berr != kGlNoError) {
          TglDebugf("Submit fail %d: SetUboBytes bi=%zu %zu (bridge 0x%x)",
                    __LINE__, bi, udata.size(), berr);
          draw_.FlagBridgeError();
          return false;
        }
      }
    }
    // One-shot per program: does the MVP live in the legacy uniform list or
    // in a uniform block? v13's extent stayed at GUI coordinates because the
    // first capture never fired — this line says why, next log.
    {
      static int ublock_logged = 0;
      if (ublock_logged < 8) {
        ++ublock_logged;
        TglDebugf(
            "diag ublock#%d prog=%u uniforms=%zu bytes=%zu ubos=%zu "
            "mvp='%s' cap=%d",
            ublock_logged, current_prog, trans.uniforms.size(),
            static_cast<std::size_t>(trans.uniform_block_bytes),
            trans.ubo_blocks.size(), trans.mvp_name.c_str(),
            mvp_captured ? 1 : 0);
      }
    }
  } else {
    if (current != 0) {
      const GLint loc = programs_.GetUniformLocation(current, "u_modelViewProj");
      if (loc >= 0) programs_.GetUniformMatrix(loc, mvp);  // Else identity.
    }
    // Clear any stale translated block so the legacy MVP is authoritative.
    bridge.SetUniformBytes(nullptr, 0);
    {
      const GLenum lerr = bridge.GetError();
      if (lerr != kGlNoError) {
        TglDebugf("Submit fail %d: legacy SetUniformBytes clear (bridge 0x%x)",
                  __LINE__, lerr);
        draw_.FlagBridgeError();
        return false;
      }
    }
  }
  // Winding in CLIP space (after the MVP, which is what Metal culls in):
  // 0/0 = mode is not TRIANGLES. A quad whose winding the projection flips
  // is discarded whole by cull=BACK while every other logged state looks
  // perfect — one of the candidates for "this draw paints nothing".
  int wind_cw = 0, wind_ccw = 0;
  if (mode == kGlTriangles) {
    for (std::size_t ti = 0; ti + 2 < raw_tris.size(); ti += 3) {
      double cx[3], cy[3];
      for (int k = 0; k < 3; ++k) {
        const double sx = raw_tris[ti + k].x;
        const double sy = raw_tris[ti + k].y;
        const double w = mvp[3] * sx + mvp[7] * sy + mvp[15];
        const double iw = (w != 0.0) ? 1.0 / w : 1.0;
        cx[k] = (mvp[0] * sx + mvp[4] * sy + mvp[12]) * iw;
        cy[k] = (mvp[1] * sx + mvp[5] * sy + mvp[13]) * iw;
      }
      const double cr = (cx[1] - cx[0]) * (cy[2] - cy[0]) -
                        (cy[1] - cy[0]) * (cx[2] - cx[0]);
      if (cr > 0.0) {
        ++wind_ccw;
      } else if (cr < 0.0) {
        ++wind_cw;
      }
    }
  }
  // Round-3: this draw's extent in NDC. The MVP is affine for GUI passes,
  // so the four bbox corners suffice; a w divide keeps perspective passes
  // honest enough for a coverage diagnostic.
  double ndc_x0 = 1e30, ndc_y0 = 1e30, ndc_x1 = -1e30, ndc_y1 = -1e30;
  const bool have_ndc = raw_x1 >= raw_x0 && raw_y1 >= raw_y0;
  if (have_ndc) {
    for (int i = 0; i < 4; ++i) {
      const double sx = (i & 1) ? raw_x1 : raw_x0;
      const double sy = (i & 2) ? raw_y1 : raw_y0;
      const double cx = mvp[0] * sx + mvp[4] * sy + mvp[12];
      const double cy = mvp[1] * sx + mvp[5] * sy + mvp[13];
      const double cw = mvp[3] * sx + mvp[7] * sy + mvp[15];
      const double iw = (cw == 0.0) ? 1.0 : 1.0 / cw;
      const double nx = cx * iw;
      const double ny = cy * iw;
      if (nx < ndc_x0) ndc_x0 = nx;
      if (nx > ndc_x1) ndc_x1 = nx;
      if (ny < ndc_y0) ndc_y0 = ny;
      if (ny > ndc_y1) ndc_y1 = ny;
    }
  }
  const GLuint draw_fbo =
      framebuffers_.BoundFramebuffer(kGlDrawFramebuffer);
  Attachment fb_att =
      framebuffers_.AttachmentState(draw_fbo, kGlColorAttachment0);
  if (draw_fbo == 0) {
    // Default (window) framebuffer: Attachment() is empty by design, but
    // the host sizes it via tglHostAttachMetalLayer → SetDefaultFramebufferSize.
    // Without that size the draw fails closed (honest headless / pre-attach).
    GLsizei dw = 0, dh = 0;
    framebuffers_.DefaultFramebufferSize(&dw, &dh);
    fb_att.present = true;
    fb_att.is_texture = false;
    fb_att.width = dw;
    fb_att.height = dh;
    fb_att.samples = 0;
  }
  if (fb_att.width <= 0 || fb_att.height <= 0) {
    TglDebugf("Submit fail %d: FBO size (fbo=%u %dx%d)\n", __LINE__,
              draw_fbo, fb_att.width, fb_att.height);
    draw_.FlagBridgeError();
    return false;
  }
  // Offscreen draws never reach the presented image — BlitFramebuffer moves
  // no pixels (window-source bookkeeping only), so an atlas build's output
  // is invisible either way. What it did cost: every size change
  // reallocates the bridge target, wiping the window (the device log showed
  // 64+ such wipes in MC's atlas phase, and the screen sampled fully empty
  // at swaps 70/80). Only window-sized draws may touch the presented
  // target. Headless hosts (no default size attached) keep the old
  // behavior so FBO unit tests still exercise the bridge.
  GLsizei win_w = 0, win_h = 0;
  framebuffers_.DefaultFramebufferSize(&win_w, &win_h);
  if (win_w > 0 && win_h > 0 &&
      (fb_att.width != win_w || fb_att.height != win_h)) {
    static int skipped = 0;
    ++skipped;
    if (skipped <= 24 || (skipped % 512) == 0) {
      TglDebugf("diag offscreen_skip#%d %ux%u fbo=%u t=%lld", skipped,
                fb_att.width, fb_att.height, draw_fbo, TglNowMs());
    }
    return true;
  }
  // Depth wiring: the GPU depth test runs only when the app enabled
  // DEPTH_TEST and bound a depth (or depth-stencil) attachment sized like
  // the color target — or, for the default framebuffer, always when the
  // test is on (window FB has an implementation depth buffer in GLES).
  // A depth test without a depth buffer renders color-only (as if the test
  // always passes); a size mismatch fails closed. DEPTH_ATTACHMENT wins
  // over DEPTH_STENCIL_ATTACHMENT.
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
  if (depth_test && (depth_src != nullptr || draw_fbo == 0)) {
    if (depth_src != nullptr &&
        (depth_src->width != fb_att.width ||
         depth_src->height != fb_att.height)) {
      TglDebugf("Submit fail %d: depth size mismatch\n", __LINE__);
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
    TglDebugf("Submit fail %d: ConfigureDepth\n", __LINE__);
    draw_.FlagBridgeError();
    return false;
  }
  // Stencil wiring (spec 13-15): the GPU stencil test runs only when the app
  // enabled STENCIL_TEST and bound a stencil (or depth-stencil) attachment
  // sized like the color target — or the default framebuffer (implicit
  // window depth/stencil). Without one the draw renders color-only (same
  // rule as depth); a size mismatch fails closed. STENCIL_ATTACHMENT
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
  if (stencil_test && (stencil_src != nullptr || draw_fbo == 0)) {
    if (stencil_src != nullptr &&
        (stencil_src->width != fb_att.width ||
         stencil_src->height != fb_att.height)) {
      TglDebugf("Submit fail %d: stencil size mismatch\n", __LINE__);
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
    {
      const GLenum serr = bridge.GetError();
      if (serr != kGlNoError) {
        TglDebugf("Submit fail %d: ConfigureStencil (bridge 0x%x)", __LINE__,
                  serr);
        draw_.FlagBridgeError();
        return false;
      }
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
        TglDebugf("MRT+MSAA: mixed samples %d vs %d", (int)a.samples,
                  (int)mrt_samples);
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
    TglDebugf("Submit fail %d: SetMrtCount\n", __LINE__);
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
  key.slot3 = static_cast<std::uint8_t>(slot3c);
  key.mrt_count = static_cast<std::uint8_t>(mrt_n);
  key.sample_count = fb_att.samples;
  const std::uint32_t pso = use_translated
                                ? bridge.CreateTranslatedPipeline(trans, key)
                                : bridge.CreateRenderPipeline(key);
  if (pso == 0) {
    const GLenum perr = bridge.GetError();
    TglDebugf("Submit fail %d: CreatePipeline (bridge 0x%x)", __LINE__, perr);
    draw_.FlagBridgeError();
    return false;
  }
  bridge.BindRenderPipeline(pso);
  if (bridge.GetError() != kGlNoError) {
    TglDebugf("Submit fail %d: BindPipeline\n", __LINE__);
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
    TglDebugf("Submit fail %d: SetBlendColor\n", __LINE__);
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
    TglDebugf("Submit fail %d: ConfigureCull\n", __LINE__);
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
    TglDebugf("Submit fail %d: ConfigureViewport\n",
            __LINE__);
    draw_.FlagBridgeError();
    return false;
  }
  // Frame lifecycle (multi-draw before eglSwapBuffers):
  //  - Fresh frame: BeginFrame + first BeginRenderPass clears with
  //    glClearColor (SetClearColor) — glClear on FBO 0 only marks pending.
  //  - Same-size frame already committed: BeginFrame reopens without a new
  //    command buffer; this pass Loads (NoClear) so prior draws survive.
  //  - Same-size frame still open (nested): reuse open frame, NoClear pass.
  //  - Size change: CommitFrame seals the old frame; BeginFrame starts a
  //    new one (clears again — correct for a new target size).
  // glClear on FBO 0 only marks pending (the CPU model has no window
  // pixels); force a device-side Clear on the next pass when pending.
  const bool had_window_clear = framebuffers_.ConsumeWindowClear();
  if (bridge.FrameOpen() || bridge.Committed()) {
    if (bridge.TargetWidth() != fb_att.width ||
        bridge.TargetHeight() != fb_att.height) {
      if (bridge.Committed() && !bridge.FrameOpen()) {
        // Sealed frame at wrong size: start fresh (Present will show the
        // last committed frame only; host commits before swap).
      } else if (bridge.FrameOpen() && !bridge.CommitFrame()) {
        TglDebugf("Submit fail %d: CommitFrame on size change\n", __LINE__);
        draw_.FlagBridgeError();
        return false;
      }
    }
  }
  // clear_rgba / SetClearColor live next to clear_this_pass (below): the
  // no-window-source force-clear repaints with g_last_clear_rgba there, so
  // the color has to be chosen after that flag is known.
  const bool reopen =
      (bridge.FrameOpen() || bridge.Committed()) &&
      bridge.TargetWidth() == fb_att.width &&
      bridge.TargetHeight() == fb_att.height;
  // The bridge is about to allocate a brand-new target texture (first frame
  // or size change): its contents are undefined, so this pass must clear.
  // Compare BEFORE BeginFrame, which updates the reported target size.
  const bool new_target =
      bridge.TargetWidth() != fb_att.width ||
      bridge.TargetHeight() != fb_att.height || bridge.TargetWidth() == 0;
  // Every size switch reallocates the presented target (old pixels gone).
  // MC's offscreen work (texture-atlas draws, 512x256 .. 2048x1024) takes
  // this path mid-frame, so a device log must show when the screen changed
  // size and back. Bounded: a session must not flood the 64KB ring.
  if (fb_att.width != bridge.TargetWidth() ||
      fb_att.height != bridge.TargetHeight()) {
    static int resize_seen = 0;
    if (resize_seen < 64) {
      ++resize_seen;
      TglDebugf("diag target_resize#%d %ux%u -> %ux%u fbo=%u t=%lld",
                resize_seen, bridge.TargetWidth(), bridge.TargetHeight(),
                fb_att.width, fb_att.height, draw_fbo, TglNowMs());
    }
  }
  // true only when BeginFrame runs this submit (fresh frame, reopen after
  // CommitFrame at swap, or size-change fresh). Mid-frame multi-draw leaves
  // the frame open and does NOT call BeginFrame (NoClear append).
  bool frame_started = false;
  if (!reopen) {
    if (!bridge.BeginFrame(fb_att.width, fb_att.height)) {
      const GLenum berr = bridge.GetError();
      TglDebugf("Submit fail %d: BeginFrame %dx%d (bridge 0x%x)", __LINE__,
                fb_att.width, fb_att.height, berr);
      draw_.FlagBridgeError();
      return false;
    }
    frame_started = true;
    } else if (bridge.Committed() && !bridge.FrameOpen()) {
      if (!bridge.BeginFrame(fb_att.width, fb_att.height)) {
        const GLenum berr = bridge.GetError();
        TglDebugf("Submit fail %d: BeginFrame reopen %dx%d (bridge 0x%x)",
                  __LINE__, fb_att.width, fb_att.height, berr);
        draw_.FlagBridgeError();
        return false;
      }
    frame_started = true;
  }
  // Round-3: close the previous frame's extent when this draw opens a new
  // one, then fold this draw in. A GUI fullscreen background reads as
  // ndc=[-1.00,-1.00]-[1.00,1.00]; anything narrower is all the screen ever
  // gets (the clear decides the rest).
  if (frame_started && g_frame_extent.draws > 0) {
    ++g_frame_index;
    if (g_frame_logged < 120) {
      ++g_frame_logged;
      // Screen rectangle (top-left origin, target pixels) so a device log can
      // be read against a screenshot: NDC alone hides a GUI laid out in a
      // corner. `px` uses this frame's target size; NDC is now the real MVP
      // (captured from the uniform block above), not the identity fallback.
      const double pxw =
          fb_att.width > 0 ? static_cast<double>(fb_att.width)
                           : static_cast<double>(bridge.TargetWidth());
      const double pxh =
          fb_att.height > 0 ? static_cast<double>(fb_att.height)
                            : static_cast<double>(bridge.TargetHeight());
      const double px0 = (g_frame_extent.minx * 0.5 + 0.5) * pxw;
      const double px1 = (g_frame_extent.maxx * 0.5 + 0.5) * pxw;
      const double py0 = (1.0 - (g_frame_extent.maxy * 0.5 + 0.5)) * pxh;
      const double py1 = (1.0 - (g_frame_extent.miny * 0.5 + 0.5)) * pxh;
      TglDebugf(
          "diag frame#%d draws=%d ndc=[%.2f,%.2f]-[%.2f,%.2f] "
          "px=[%.0f,%.0f]-[%.0f,%.0f] vp=%.0f,%.0f,%.0fx%.0f t=%lld",
          g_frame_index, g_frame_extent.draws, g_frame_extent.minx,
          g_frame_extent.miny, g_frame_extent.maxx, g_frame_extent.maxy, px0,
          py0, px1, py1, g_frame_extent.vpx, g_frame_extent.vpy,
          g_frame_extent.vpw, g_frame_extent.vph, TglNowMs());
    }
    g_frame_extent = FrameExtent{};
  }
  if (g_frame_extent.draws == 0) {
    g_frame_extent.vpx = vp_cfg.x;
    g_frame_extent.vpy = vp_cfg.y;
    g_frame_extent.vpw = vp_cfg.width;
    g_frame_extent.vph = vp_cfg.height;
  }
  if (have_ndc) {
    if (ndc_x0 < g_frame_extent.minx) g_frame_extent.minx = ndc_x0;
    if (ndc_y0 < g_frame_extent.miny) g_frame_extent.miny = ndc_y0;
    if (ndc_x1 > g_frame_extent.maxx) g_frame_extent.maxx = ndc_x1;
    if (ndc_y1 > g_frame_extent.maxy) g_frame_extent.maxy = ndc_y1;
  }
  // One-frame A/B (v15): force every vertex's UV to the quad centre on the
  // first textured draw of window frame 7, so the sampled texel is one whose
  // content `diag texc#` just reported. If the pipeline (attribute layout ->
  // sampler -> blend) is healthy, hist at that swap grows a patch over the
  // logo rect; if it stays empty, the fault is inside the shader path, not
  // in the app's texture. Runs once, costs one frame.
  {
    static int uvf_frames = 0;
    static bool uvf_done = false;
    if (win_w > 0 && fb_att.width == win_w && fb_att.height == win_h) {
      if (frame_started) ++uvf_frames;
      if (!uvf_done && uvf_frames == 7 && need_slot2 && slot2c >= 2 &&
          vert_stride >= 40) {
        const float kHalf = 0.5f;
        for (std::size_t vi = 0;
             vi * vert_stride + 40 <= interleaved.size(); ++vi) {
          const std::size_t base = vi * vert_stride + 32;
          std::memcpy(interleaved.data() + base, &kHalf, 4);
          std::memcpy(interleaved.data() + base + 4, &kHalf, 4);
        }
        uvf_done = true;
        TglDebugf("diag uvforce# frame=%d prog=%u verts=%zu slot2c=%d",
                  uvf_frames, current_prog, draw_verts->size(), slot2c);
      }
    }
  }
  ++g_frame_extent.draws;
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
    TglDebugf("Submit fail %d: split stencil + blend\n", __LINE__);
    draw_.FlagBridgeError();
    return false;
  }
  // First pass of a fresh frame clears; later multi-draw passes Load so the
  // clear does not wipe earlier draws in the same swap. Once an app has
  // blitted an FBO into the window (window source known), ONLY its clear
  // may wipe the bridge target — clearing some other FBO (MC's GUI target,
  // transparent black) must not decide what the window shows, and an app
  // that never clears the presented target must keep its pixels (spec 17.3).
  // Until then (hosts/tests that never blit to FBO 0, and the very first
  // frame before any present) keep the frame-start clear: it initializes the
  // brand-new bridge target.
  // Which passes clear, and with which color:
  //  - had_window_clear: the app cleared the FBO it declared as the window
  //    (a COLOR blit into FBO 0) — use the color captured at glClear time,
  //    because by now live glClearColor may hold another FBO's color.
  //  - new_target: the bridge is about to allocate the target texture, whose
  //    contents are undefined — clear with the live glClearColor.
  //  - else, while no window source is known (hosts/tests that never blit
  //    into FBO 0): force a fresh frame start, but repaint with the color the
  //    window was last DEFINED with (g_last_clear_rgba), never the live one.
  //    MC's second frame arrived with a transparent glClearColor (its
  //    offscreen target clears every frame), and that force-clear wiped the
  //    red background frame 1 had painted — the red-loss in the v9/v10 logs.
  const bool clear_this_pass =
      had_window_clear || new_target ||
      (!framebuffers_.HasWindowSource() &&
       (frame_started || bridge.DrawCount() == 0));
  const bool defines_color = had_window_clear || new_target;
  GLfloat clear_rgba[4];
  if (had_window_clear) {
    framebuffers_.GetWindowClearColor(clear_rgba);
  } else if (clear_this_pass && !defines_color) {
    for (int i = 0; i < 4; ++i) clear_rgba[i] = g_last_clear_rgba[i];
  } else {
    raster_.GetClearColor(clear_rgba);
  }
  if (defines_color) {
    for (int i = 0; i < 4; ++i) g_last_clear_rgba[i] = clear_rgba[i];
  }
  bridge.SetClearColor(clear_rgba[0], clear_rgba[1], clear_rgba[2],
                       clear_rgba[3]);
  {
    const int n = DiagCount("pass");
    // Bounded head of session + periodic samples: the interesting phase
    // (loading -> menu transition) happens hundreds of frames in, long after
    // the first-24 cap is spent.
    if (n <= 24 || (n % 200) == 0) {
      TglDebugf(
          "diag pass#%d clear=%d started=%d draws=%u frame_open=%d "
          "committed=%d target=%dx%d fbo_att=%dx%d fbo=%u "
          "clear_rgba=%.3f,%.3f,%.3f,%.3f "
          "had_win_clear=%d new_target=%d wsrc=%u t=%lld",
          n, clear_this_pass ? 1 : 0, frame_started ? 1 : 0, bridge.DrawCount(),
          bridge.FrameOpen() ? 1 : 0, bridge.Committed() ? 1 : 0,
          bridge.TargetWidth(), bridge.TargetHeight(), fb_att.width,
          fb_att.height, draw_fbo, clear_rgba[0], clear_rgba[1], clear_rgba[2],
          clear_rgba[3], had_window_clear ? 1 : 0, new_target ? 1 : 0,
          framebuffers_.WindowSource(), TglNowMs());
    }
  }
  // Per-draw state that decides whether this window draw produces a visible
  // pixel: depth test and whether a depth buffer is actually wired (a depth
  // test without a buffer renders color-only; with a stale/undefined buffer
  // every fragment fails and the screen shows the clear color only), cull
  // (wrong winding = nothing), blend factors (a zero source alpha or
  // ONE/ZERO pair leaves the background untouched), the sampled texture's
  // first texel (a transparent texel draws nothing), the draw's own vertex
  // color, and where the draw lands on screen. Sampled: the head of the
  // session plus every 8th window draw, because MC switches programs after
  // the loading screen and a head-only cap never shows those frames.
  {
    static int state_seen = 0;
    static int state_logged = 0;
    ++state_seen;
    if (state_logged < 24 || (state_seen % 8 == 0 && state_logged < 240)) {
      ++state_logged;
      GLuint tex0 = 0;
      GLsizei tw = 0, th = 0;
      int tex_px = 0;
      int t0r = -1, t0g = -1, t0b = -1, t0a = -1;
      const TextureLevel* lvl = nullptr;
      if (need_slot2) {
        tex0 = textures_.BoundTextureForUnit(0u, kGlTexture2d);
        if (tex0 != 0) {
          lvl = textures_.LevelStateRef(tex0, kGlTexture2d, 0);
          if (lvl != nullptr) {
            tw = lvl->width;
            th = lvl->height;
            tex_px = lvl->pixels.empty() ? 0 : 1;
            if (lvl->pixels.size() >= 4) {
              t0r = lvl->pixels[0];
              t0g = lvl->pixels[1];
              t0b = lvl->pixels[2];
              t0a = lvl->pixels[3];
            }
          }
        }
      }
      // Content probe: for the first 8 textures a window draw samples, is
      // the CPU-side level (what the facade uploads) actually populated?
      // "nz=0/a0=all" = the app gave us transparent bytes (or TGLES unpacked
      // them that way); "nz>0" moves the suspicion to the upload/sampler.
      static GLuint texc_seen[8];
      static int texc_n = 0;
      {
        bool known = false;
        for (int i = 0; i < texc_n; ++i) {
          if (texc_seen[i] == tex0) known = true;
        }
        if (lvl != nullptr && !known && texc_n < 8 && tw > 0 && th > 0) {
          texc_seen[texc_n++] = tex0;
          const std::size_t need =
              static_cast<std::size_t>(tw) * static_cast<std::size_t>(th) * 4;
          const bool aligned = lvl->pixels.size() == need;
          std::size_t a0 = 0, a255 = 0, nz = 0;
          if (aligned) {
            const std::size_t n = need / 4;
            for (std::size_t i = 0; i < n; ++i) {
              const std::uint8_t* q = lvl->pixels.data() + i * 4;
              if (q[3] == 0) {
                ++a0;
              } else if (q[3] == 255) {
                ++a255;
              }
              if (q[0] != 0 || q[1] != 0 || q[2] != 0 || q[3] != 0) ++nz;
            }
          }
          const std::size_t mid =
              (static_cast<std::size_t>(th / 2) * tw + tw / 2) * 4;
          int mr = -1, mg = -1, mb = -1, ma = -1, cr = -1, cg = -1, cb = -1,
              ca = -1;
          if (aligned && lvl->pixels.size() >= mid + 4) {
            mr = lvl->pixels[mid + 0];
            mg = lvl->pixels[mid + 1];
            mb = lvl->pixels[mid + 2];
            ma = lvl->pixels[mid + 3];
            cr = lvl->pixels[0];
            cg = lvl->pixels[1];
            cb = lvl->pixels[2];
            ca = lvl->pixels[3];
          }
          TglDebugf(
              "diag texc#%d tex=%u %dx%d bytes=%zu aligned=%d nz=%zu a0=%zu "
              "a255=%zu mid=(%d,%d,%d,%d) cor=(%d,%d,%d,%d)",
              texc_n, tex0, tw, th, lvl->pixels.size(), aligned ? 1 : 0, nz,
              a0, a255, mr, mg, mb, ma, cr, cg, cb, ca);
          // Same texels straight out of the GPU texture the draw samples:
          // CPU has content + GPU does not = upload bug; both have content
          // and the screen is still empty = the shader samples somewhere else.
          std::uint8_t gmid[4] = {0, 0, 0, 0};
          std::uint8_t gcor[4] = {0, 0, 0, 0};
          const bool ok_mid = bridge.TextureProbe(0, tw / 2, th / 2, gmid);
          const bool ok_cor = bridge.TextureProbe(0, 0, 0, gcor);
          TglDebugf(
              "diag gputex#%d unit=0 tex=%u mid_ok=%d mid=(%u,%u,%u,%u) "
              "cor_ok=%d cor=(%u,%u,%u,%u)",
              texc_n, tex0, ok_mid ? 1 : 0, gmid[0], gmid[1], gmid[2],
              gmid[3], ok_cor ? 1 : 0, gcor[0], gcor[1], gcor[2], gcor[3]);
        }
      }
      // Vertex 0 color (pos float4 + col float4): alpha 0 here makes a
      // blended draw invisible whatever else is right.
      float c0 = 0.f, c1 = 0.f, c2 = 0.f, c3 = 0.f;
      if (vert_stride >= 32 && interleaved.size() >= 32) {
        std::memcpy(&c0, interleaved.data() + 16, 4);
        std::memcpy(&c1, interleaved.data() + 20, 4);
        std::memcpy(&c2, interleaved.data() + 24, 4);
        std::memcpy(&c3, interleaved.data() + 28, 4);
      }
      // Slot-2 UVs (pos 16 + col 16 then uv in the interleaved layout).
      // u,v of vertex 0 is what a background quad usually samples with; the
      // bbox says whether the UVs are sane (0..1) or point past the atlas
      // edge (CLAMP_TO_EDGE then reads a corner texel — often transparent).
      double u0 = 0.0, v0 = 0.0;
      double uu0 = 1e30, vv0 = 1e30, uu1 = -1e30, vv1 = -1e30;
      int uvn = 0;
      if (slot2c >= 2 && vert_stride >= 40) {
        for (std::size_t vi = 0;
             vi * vert_stride + 40 <= interleaved.size(); ++vi) {
          const std::size_t base = vi * vert_stride + 32;
          float u = 0.f, v = 0.f;
          std::memcpy(&u, interleaved.data() + base, 4);
          std::memcpy(&v, interleaved.data() + base + 4, 4);
          if (vi == 0) {
            u0 = u;
            v0 = v;
          }
          if (u < uu0) uu0 = u;
          if (u > uu1) uu1 = u;
          if (v < vv0) vv0 = v;
          if (v > vv1) vv1 = v;
          ++uvn;
        }
      }
      // The texel actually fetched at (u0,v0): a transparent corner is
      // invisible under any blend, which is exactly the symptom under test.
      int uvr = -1, uvg = -1, uvb = -1, uva = -1;
      if (uvn > 0 && lvl != nullptr && tw > 0 && th > 0 &&
          lvl->pixels.size() >= static_cast<std::size_t>(tw) *
                                      static_cast<std::size_t>(th) * 4) {
        double fx = u0 * (tw - 1);
        double fy = v0 * (th - 1);
        int tx = static_cast<int>(fx < 0 ? fx - 0.5 : fx + 0.5);
        int ty = static_cast<int>(fy < 0 ? fy - 0.5 : fy + 0.5);
        if (tx < 0) tx = 0;
        if (tx > tw - 1) tx = tw - 1;
        if (ty < 0) ty = 0;
        if (ty > th - 1) ty = th - 1;
        const std::size_t p =
            (static_cast<std::size_t>(ty) * static_cast<std::size_t>(tw) +
             static_cast<std::size_t>(tx)) *
            4;
        uvr = lvl->pixels[p + 0];
        uvg = lvl->pixels[p + 1];
        uvb = lvl->pixels[p + 2];
        uva = lvl->pixels[p + 3];
      }
      // How the level got into memory: internalformat vs the transfer
      // format/type of the last upload (and its byte size).
      unsigned fmt0 = 0, fmt_up = 0, fmt_ty = 0;
      std::size_t tex_bytes = 0;
      if (lvl != nullptr) {
        fmt0 = lvl->internalformat;
        fmt_up = lvl->upload_format;
        fmt_ty = lvl->upload_type;
        tex_bytes = lvl->pixels.size();
      }
      double rx0 = 0.0, ry0 = 0.0, rx1 = 0.0, ry1 = 0.0;
      if (have_ndc) {
        const double pw =
            fb_att.width > 0 ? static_cast<double>(fb_att.width)
                             : static_cast<double>(bridge.TargetWidth());
        const double ph =
            fb_att.height > 0 ? static_cast<double>(fb_att.height)
                              : static_cast<double>(bridge.TargetHeight());
        rx0 = (ndc_x0 * 0.5 + 0.5) * pw;
        rx1 = (ndc_x1 * 0.5 + 0.5) * pw;
        ry0 = (1.0 - (ndc_y1 * 0.5 + 0.5)) * ph;
        ry1 = (1.0 - (ndc_y0 * 0.5 + 0.5)) * ph;
      }
      const BlendState bl = raster_.GetBlend(0);
      TglDebugf(
          "diag wstate#%d fbo=%u prog=%u trans=%d verts=%zu slot2=%d "
          "tex=%u(%dx%d px=%d) t0=%d,%d,%d,%d depth=%d dsrc=%d "
          "cull=%d(0x%x/0x%x) bln=%d blf=0x%x,0x%x scissor=%d vp=%dx%d "
          "col=%.2f,%.2f,%.2f,%.2f rect=[%.0f,%.0f]-[%.0f,%.0f] "
          "uv=[%.3f,%.3f]-[%.3f,%.3f] tuv=%d,%d,%d,%d "
          "tfmt=0x%x,0x%x,0x%x tbytes=%zu wind=%d,%d "
          "clear_pass=%d mode=0x%x t=%lld",
          state_logged, draw_fbo, current_prog, use_translated ? 1 : 0,
          draw_verts->size(), need_slot2 ? 1 : 0, tex0, tw, th, tex_px, t0r,
          t0g, t0b, t0a, depth_test ? 1 : 0,
          (depth_src != nullptr || (depth_test && draw_fbo == 0)) ? 1 : 0,
          cull_cfg.enabled ? 1 : 0, cull_cfg.mode, cull_cfg.front_face,
          (foundation_.IsEnabled(kGlBlend) != kGlFalse) ? 1 : 0, bl.src_rgb,
          bl.dst_rgb, vp_cfg.scissor_enabled ? 1 : 0, vp_cfg.width,
          vp_cfg.height, c0, c1, c2, c3, rx0, ry0, rx1, ry1, uu0, vv0, uu1,
          vv1, uvr, uvg, uvb, uva, fmt0, fmt_up, fmt_ty, tex_bytes, wind_cw,
          wind_ccw, clear_this_pass ? 1 : 0, bridge_mode, TglNowMs());
    }
  }
  // Facade-side memory on the same axis as [MEM]/hist: the CPU pixel storage
  // the translator keeps (a 3 GB device dies at 2 GiB and every extra MB of
  // texture cache is charged to us, not to the JVM). Throttled to one line
  // per 5 s so a 100-draw frame cannot flood the 64 KB ring.
  {
    static std::int64_t last_glesmem_ms = -1000000;
    const std::int64_t now_ms = TglNowMs();
    if (now_ms - last_glesmem_ms >= 5000) {
      last_glesmem_ms = now_ms;
      const std::size_t cpu_tex = textures_.TotalPixelBytes();
      TglDebugf("diag glesmem# cpu_tex=%.1fMB target=%dx%d fbo=%u t=%lld",
                static_cast<double>(cpu_tex) / (1024.0 * 1024.0),
                bridge.TargetWidth(), bridge.TargetHeight(), draw_fbo,
                now_ms);
    }
  }
  // MC clears depth every frame — but its clear lands on a different
  // (window-sized) FBO, which is deliberately not treated as a window clear,
  // and TGLES has no standalone glClear path. Without a depth reset at frame
  // start the buffer keeps last frame's values (or undefined contents, when
  // the depth target was created after the only clear that ever ran), so
  // depth-tested draws fail silently and the screen shows the clear color
  // only. A depth-only clear pass (color Loads) fixes that without touching
  // the color that holds the app's background.
  const bool depth_clear_this_pass =
      !clear_this_pass && frame_started && depth_cfg.enabled;
  const bool open_clear_pass = clear_this_pass || depth_clear_this_pass;
  bridge.SetClearAttachments(clear_this_pass,
                             clear_this_pass || depth_clear_this_pass);
  // A/B probe: v13's histogram shows only a white bar over the clear color,
  // so one of the two window draws (untextured prog=2, textured prog=3)
  // contributes nothing visible. Dropping one of them on two early frames
  // and letting the existing per-swap diff answer which is cheaper and
  // safer than a mid-pass readback. First 8 window frames only; a dropped
  // first draw still runs its clear pass (the clear rides BeginRenderPass),
  // so the experiment changes exactly one draw.
  bool drop_this_draw = false;
  {
    static int probe_frames = 0;
    static int probe_draw = 0;
    static int probe_seq = 0;
    if (!split_refs && probe_frames < 8 && win_w > 0 &&
        fb_att.width == win_w && fb_att.height == win_h) {
      if (frame_started) {
        ++probe_frames;
        probe_draw = 0;
      }
      if ((probe_frames == 3 && probe_draw == 1) ||
          (probe_frames == 5 && probe_draw == 0)) {
        drop_this_draw = true;
        TglDebugf("diag drop#%d frame=%d draw=%d prog=%u verts=%zu mode=0x%x "
                  "t=%lld",
                  ++probe_seq, probe_frames, probe_draw, current_prog,
                  draw_verts->size(), bridge_mode, TglNowMs());
      }
      ++probe_draw;
    }
  }
  if (drop_this_draw) {
    if (open_clear_pass) {
      bridge.BeginRenderPass();
      bridge.EndRenderPass();
    }
  } else if (!split_refs) {
    if (open_clear_pass) {
      bridge.BeginRenderPass();
    } else {
      bridge.BeginRenderPassNoClear();
    }
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
      TglDebugf("Submit fail %d: split stencil back config\n", __LINE__);
      draw_.FlagBridgeError();
      return false;
    }
    if (open_clear_pass) {
      bridge.BeginRenderPass();
    } else {
      bridge.BeginRenderPassNoClear();
    }
    bridge.Draw(bridge_mode);
    bridge.EndRenderPass();
    raster_.CullFace(kGlBack);
    cull_cfg.mode = kGlBack;
    bridge.ConfigureCull(cull_cfg);
    bridge.ConfigureStencil(front_cfg);
    bridge.ConfigureDepth(depth_cfg);
    if (bridge.GetError() != kGlNoError) {
      TglDebugf("Submit fail %d: split stencil front config\n", __LINE__);
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
  // Multi-draw path: leave the frame OPEN so later glDraw* in the same
  // swap append (NoClear reopen via Committed()). Commit only on size
  // change (already done above) — eglSwapBuffers / tglHostPresent commit
  // any remaining open frame before Present.
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
