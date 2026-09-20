#include "tgles/context.h"

#include <string>

#include "tgles/buffer.h"
#include "tgles/framebuffer.h"
#include "tgles/image_units.h"
#include "tgles/texture.h"

namespace tgles {

namespace {

// Server strings. VERSION follows "OpenGL ES N.M <vendor info>" and
// SHADING_LANGUAGE_VERSION follows "OpenGL ES GLSL ES N.M <info>"
// (spec 20.2; SL minor is always two digits).
constexpr char kVendor[] = "TGL";
constexpr char kRenderer[] = "TGL Metal (Apple GPU)";
constexpr char kVersion[] = "OpenGL ES 3.2 TGL";
constexpr char kShadingLanguageVersion[] = "OpenGL ES GLSL ES 3.20 TGL";
constexpr char kExtensions[] = "";

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
  // Step 1 ships zero extensions; any index is out of range.
  static_cast<void>(index);
  errors_.Record(kGlInvalidValue);
  return nullptr;
}

void Context::GetIntegerv(GLenum pname, GLint* params) {
  if (params == nullptr) {
    errors_.Record(kGlInvalidValue);
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
      *params = 0;
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
