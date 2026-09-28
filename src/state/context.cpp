#include "tgles/state/context.h"

#include <string>

#include "tgles/state/buffer.h"
#include "tgles/state/framebuffer.h"
#include "tgles/state/image_units.h"
#include "tgles/state/texture.h"

namespace tgles {

namespace {

// Server strings. VERSION follows "OpenGL ES N.M <vendor info>" and
// SHADING_LANGUAGE_VERSION follows "OpenGL ES GLSL ES N.M <info>"
// (spec 20.2; SL minor is always two digits).
constexpr char kVendor[] = "TGL";
constexpr char kRenderer[] = "TGL Metal (Apple GPU)";
constexpr char kVersion[] = "OpenGL ES 3.2 TGL";
constexpr char kShadingLanguageVersion[] = "OpenGL ES GLSL ES 3.20 TGL";
// Advertised extensions (MobileGL FillInGLESCapabilities gates on these
// STRINGS, so each entry is a promise of full execution, not validation):
// - GL_EXT_buffer_storage: BufferStorageEXT incl. PERSISTENT/COHERENT bits +
//   Map/Unmap/Flush over the CPU store (coherent by construction; draws
//   re-upload per frame, so GPU always sees latest).
// - GL_EXT_multi_draw_indirect: multidraw indirect loops executing each
//   command like its single-draw sibling (draws before a mid-list failure
//   stand).
// - GL_EXT_draw_elements_base_vertex + GL_EXT_multi_draw_arrays: client-side
//   multidraw lists with CPU index rebasing (byte offsets into the EAB).
// - GL_EXT_base_instance: instanced draws with shifted instance ids for
//   divisor stepping.
// - GL_EXT_texture_view: storage-aliasing views with reinterpreted format
//   (same target, full layer coverage, same texel size + format class) +
//   SAMPLING through views (shared bytes, device-tested red pixels).
// - Texture sampling v1 (no EXT string, core §8): single sampler2D unit,
//   TEXTURE_2D RGBA8 level 0, NEAREST/LINEAR + CLAMP/REPEAT, UV attrib 2,
//   v_col * texture() in frag_tex_main (device-tested). Multi-sampler,
//   3D/cube/array, mipmap filtering and sRGB decode are tracked work.
// - Compute v1 (no EXT string, core ch.19): TGL_COMPUTE_ADD_ONE_SSBO0
//   executes (CPU store + real MTLCompute kernel, bit-exact device test),
//   direct and indirect (groups read from the DISPATCH_INDIRECT store);
//   other compute programs validate without mutation (documented).
// - GLSL->MSL v1 (no EXT string): linked vertex+fragment sources compile to
//   app-derived MSL (textured sampling, diffuse lighting via dot/normalize,
//   trivial passthrough fallback); out-of-subset programs fail closed.
// Deliberately NOT advertised: timer/parallel-compile/dual-source (no GPU
// counters / dual-source pipeline), MRT/multisample resolve, integer
// attribs, split stencil refs (all fail closed). Depth+blend and CONSTANT_*
// EXECUTE (keyed depth/stencil pipelines + per-frame blend color).
constexpr const char* kExtensionList[] = {
    "GL_EXT_buffer_storage",
    "GL_EXT_multi_draw_indirect",
    "GL_EXT_draw_elements_base_vertex",
    "GL_EXT_multi_draw_arrays",
    "GL_EXT_base_instance",
    "GL_EXT_texture_view",
};
constexpr std::size_t kExtensionCount =
    sizeof(kExtensionList) / sizeof(kExtensionList[0]);
constexpr char kExtensions[] =
    "GL_EXT_buffer_storage GL_EXT_multi_draw_indirect "
    "GL_EXT_draw_elements_base_vertex GL_EXT_multi_draw_arrays "
    "GL_EXT_base_instance GL_EXT_texture_view";

// Limits come from texture.h / image_units.h / buffer.h / framebuffer.h
// (all >= spec minimums); context.cpp only maps pnames to them.

}  // namespace

Context Context::Create(bool debug) {
  Context ctx;
  ctx.is_debug_ = debug;
  // DEBUG_OUTPUT starts TRUE on debug contexts, FALSE otherwise
  // (spec Table 21.53); every other cap keeps its member initializer.
  ctx.debug_output_ = debug;
  return ctx;
}

bool Context::IsCoreEnableCap(GLenum cap) const {
  switch (cap) {
    case kGlBlend:
    case kGlCullFace:
    case kGlDebugOutput:
    case kGlDebugOutputSynchronous:
    case kGlDepthTest:
    case kGlDither:
    case kGlPolygonOffsetFill:
    case kGlPrimitiveRestartFixedIndex:
    case kGlRasterizerDiscard:
    case kGlSampleAlphaToCoverage:
    case kGlSampleCoverage:
    case kGlScissorTest:
    case kGlStencilTest:
      return true;
    default:
      return false;
  }
}

bool* Context::MutableFlag(GLenum cap) {
  switch (cap) {
    case kGlBlend: return &blend_;
    case kGlCullFace: return &cull_face_;
    case kGlDebugOutput: return &debug_output_;
    case kGlDebugOutputSynchronous: return &debug_output_synchronous_;
    case kGlDepthTest: return &depth_test_;
    case kGlDither: return &dither_;
    case kGlPolygonOffsetFill: return &polygon_offset_fill_;
    case kGlPrimitiveRestartFixedIndex: return &primitive_restart_fixed_index_;
    case kGlRasterizerDiscard: return &rasterizer_discard_;
    case kGlSampleAlphaToCoverage: return &sample_alpha_to_coverage_;
    case kGlSampleCoverage: return &sample_coverage_;
    case kGlScissorTest: return &scissor_test_;
    case kGlStencilTest: return &stencil_test_;
    default: return nullptr;
  }
}

void Context::Enable(GLenum cap) {
  bool* flag = MutableFlag(cap);
  if (flag == nullptr) {
    errors_.Record(kGlInvalidEnum);
    return;
  }
  *flag = true;
}

void Context::Disable(GLenum cap) {
  bool* flag = MutableFlag(cap);
  if (flag == nullptr) {
    errors_.Record(kGlInvalidEnum);
    return;
  }
  *flag = false;
}

GLboolean Context::IsEnabled(GLenum cap) {
  const bool* flag = MutableFlag(cap);
  if (flag == nullptr) {
    // Value-returning commands return zero on error (spec 2.3.1).
    errors_.Record(kGlInvalidEnum);
    return kGlFalse;
  }
  return *flag ? kGlTrue : kGlFalse;
}

GLenum Context::GetError() { return errors_.Get(); }
bool Context::HasPending() const { return errors_.HasPending(); }
void Context::RecordError(GLenum code) { errors_.Record(code); }

const GLubyte* Context::GetString(GLenum name) const {
  switch (name) {
    case kGlVendor: return reinterpret_cast<const GLubyte*>(kVendor);
    case kGlRenderer: return reinterpret_cast<const GLubyte*>(kRenderer);
    case kGlVersion: return reinterpret_cast<const GLubyte*>(kVersion);
    case kGlShadingLanguageVersion:
      return reinterpret_cast<const GLubyte*>(kShadingLanguageVersion);
    case kGlExtensions: return reinterpret_cast<const GLubyte*>(kExtensions);
    default: break;
  }
  // Mutable error queue inside a const method.
  const_cast<Context*>(this)->errors_.Record(kGlInvalidEnum);
  return nullptr;
}

const GLubyte* Context::GetStringi(GLenum name, GLuint index) {
  if (name != kGlExtensions) {
    errors_.Record(kGlInvalidEnum);
    return nullptr;
  }
  if (index >= kExtensionCount) {
    errors_.Record(kGlInvalidValue);
    return nullptr;
  }
  return reinterpret_cast<const GLubyte*>(kExtensionList[index]);
}

void Context::GetBooleanv(GLenum pname, GLboolean* params) {
  if (params == nullptr) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  const bool was_pending = HasPending();
  GLint v = 0;
  GetIntegerv(pname, &v);
  // A pre-existing error is not ours (GetIntegerv writes anyway); only skip
  // the write when THIS call recorded (spec: no writes on error).
  if (!was_pending && HasPending()) return;
  *params = (v != 0) ? kGlTrue : kGlFalse;
}

void Context::GetFloatv(GLenum pname, GLfloat* params) {
  if (params == nullptr) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  const bool was_pending = HasPending();
  GLint v = 0;
  GetIntegerv(pname, &v);
  if (!was_pending && HasPending()) return;
  *params = static_cast<GLfloat>(v);
}

void Context::GetIntegerv(GLenum pname, GLint* params) {
  if (params == nullptr) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  // Enable caps read back as 0/1 (spec 20.2 boolean queries).
  if (bool* flag = MutableFlag(pname)) {
    *params = *flag ? 1 : 0;
    return;
  }
  switch (pname) {
    case kGlMajorVersion:
      *params = kEsMajorVersion;
      return;
    case kGlMinorVersion:
      *params = kEsMinorVersion;
      return;
    case kGlNumExtensions:
      *params = static_cast<GLint>(kExtensionCount);
      return;
    case kGlContextFlags:
      *params = is_debug_ ? static_cast<GLint>(kGlContextFlagDebugBit) : 0;
      return;
    case kGlMaxTextureSize:
      *params = kMaxTextureSizeValue;
      return;
    // Core limit table (CTS Internalformat/Info groups + the limits
    // MobileGL's loader queries from its host). Every value comes from
    // TGL's own limit constants, all >= spec minimums.
    case kGlMaxTextureImageUnitsQ:
      *params = kMaxTextureImageUnits;
      return;
    case kGlMaxVertexTextureImageUnitsQ:
      *params = kMaxVertexTextureImageUnits;
      return;
    case kGlMaxCombinedTextureImageUnitsQ:
      *params = kMaxCombinedTextureImageUnits;
      return;
    case kGlMaxImageUnitsQ:
      *params = kMaxImageUnitsValue;
      return;
    case kGlTextureBufferOffsetAlignmentQ:
      *params = kTextureBufferOffsetAlignmentValue;
      return;
    case kGlMaxTextureBufferSizeQ:
      *params = kMaxTextureBufferSizeValue;
      return;
    case kGlMaxUniformBufferBindingsQ:
      *params = kMaxUniformBufferBindings;
      return;
    case kGlMaxShaderStorageBufferBindingsQ:
      *params = kMaxShaderStorageBufferBindings;
      return;
    case kGlMaxDrawBuffersQ:
      *params = kMaxDrawBuffers;
      return;
    case kGlMaxColorAttachmentsQ:
      *params = kMaxColorAttachments;
      return;
    case kGlMaxSamplesQ:
      *params = kMaxSamplesValue;
      return;
    case kGlMaxRenderbufferSizeQ:
      *params = kMaxRenderbufferSizeValue;
      return;
    case kGlMaxCubeMapTextureSizeQ:
      *params = kMaxCubeMapTextureSizeValue;
      return;
    case kGlMax3dTextureSizeQ:
      *params = kMax3dTextureSizeValue;
      return;
    case kGlMaxArrayTextureLayersQ:
      *params = kMaxArrayTextureLayersValue;
      return;
    case kGlMaxComputeTextureImageUnitsQ:
      *params = kMaxTextureImageUnits;
      return;
    default:
      break;
  }
  // Unknown pname: record INVALID_ENUM, leave *params untouched.
  errors_.Record(kGlInvalidEnum);
}

bool Context::IsDebugContext() const { return is_debug_; }

std::string Context::GetStringForReport() const {
  std::string out;
  out += reinterpret_cast<const char*>(kVendor);
  out += " | ";
  out += reinterpret_cast<const char*>(kRenderer);
  out += " | ";
  out += reinterpret_cast<const char*>(kVersion);
  out += " | ";
  out += reinterpret_cast<const char*>(kShadingLanguageVersion);
  return out;
}

}  // namespace tgles
