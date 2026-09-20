#include "tgles/raster.h"

#include <algorithm>

namespace tgles {

RasterState::RasterState() {
  for (auto& m : color_mask_) m = {true, true, true, true};
}

GLenum RasterState::GetError() { return errors_.Get(); }
bool RasterState::HasPending() const { return errors_.HasPending(); }

bool RasterState::IsBlendEquation(GLenum mode) {
  return mode == kGlFuncAdd || mode == kGlFuncSubtract ||
         mode == kGlFuncReverseSubtract || mode == kGlMinBlend ||
         mode == kGlMaxBlend;
}

bool RasterState::IsBlendFactor(GLenum factor) {
  switch (factor) {
    case kGlZero:
    case kGlOne:
    case kGlSrcColor:
    case kGlOneMinusSrcColor:
    case kGlSrcAlpha:
    case kGlOneMinusSrcAlpha:
    case kGlDstAlpha:
    case kGlOneMinusDstAlpha:
    case kGlDstColor:
    case kGlOneMinusDstColor:
    case kGlSrcAlphaSaturate:
    case kGlConstantColor:
    case kGlOneMinusConstantColor:
    case kGlConstantAlpha:
    case kGlOneMinusConstantAlpha:
      return true;
    default:
      return false;
  }
}

bool RasterState::IsCompareFunc(GLenum func) {
  return func >= kGlNever && func <= kGlAlways;
}

bool RasterState::IsStencilOp(GLenum op) {
  switch (op) {
    case kGlKeep:
    case kGlZero:
    case kGlReplace:
    case kGlIncr:
    case kGlDecr:
    case kGlInvert:
    case kGlIncrWrap:
    case kGlDecrWrap:
      return true;
    default:
      return false;
  }
}

bool RasterState::IsFace(GLenum face) {
  return face == kGlFront || face == kGlBackFace || face == kGlFrontAndBack;
}

void RasterState::Viewport(GLint x, GLint y, GLsizei width, GLsizei height) {
  if (width < 0 || height < 0) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  viewport_ = {x, y, width, height};
}

void RasterState::DepthRangef(GLfloat n, GLfloat f) {
  // ES clamps the range into [0, 1]; no error is generated.
  depth_near_ = std::clamp(n, 0.0f, 1.0f);
  depth_far_ = std::clamp(f, 0.0f, 1.0f);
}

void RasterState::Scissor(GLint x, GLint y, GLsizei width, GLsizei height) {
  if (width < 0 || height < 0) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  scissor_ = {x, y, width, height};
}

void RasterState::CullFace(GLenum mode) {
  if (mode != kGlFront && mode != kGlBackFace && mode != kGlFrontAndBack) {
    errors_.Record(kGlInvalidEnum);
    return;
  }
  cull_mode_ = mode;
}

void RasterState::FrontFace(GLenum mode) {
  if (mode != kGlCw && mode != kGlCcw) {
    errors_.Record(kGlInvalidEnum);
    return;
  }
  front_face_ = mode;
}

void RasterState::PolygonOffset(GLfloat factor, GLfloat units) {
  polygon_factor_ = factor;
  polygon_units_ = units;
}

void RasterState::LineWidth(GLfloat width) {
  if (!(width > 0.0f)) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  line_width_ = width;
}

void RasterState::BlendEquation(GLenum mode) {
  if (!IsBlendEquation(mode)) {
    errors_.Record(kGlInvalidEnum);
    return;
  }
  for (auto& b : blend_) {
    b.equation_rgb = mode;
    b.equation_alpha = mode;
  }
}

void RasterState::BlendEquationSeparate(GLenum mode_rgb, GLenum mode_alpha) {
  if (!IsBlendEquation(mode_rgb) || !IsBlendEquation(mode_alpha)) {
    errors_.Record(kGlInvalidEnum);
    return;
  }
  for (auto& b : blend_) {
    b.equation_rgb = mode_rgb;
    b.equation_alpha = mode_alpha;
  }
}

void RasterState::BlendEquationi(GLuint buf, GLenum mode) {
  if (!IsBlendEquation(mode)) {
    errors_.Record(kGlInvalidEnum);
    return;
  }
  if (buf >= static_cast<GLuint>(kMaxDrawBufferCount)) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  blend_[buf].equation_rgb = mode;
  blend_[buf].equation_alpha = mode;
}

void RasterState::BlendFunc(GLenum sfactor, GLenum dfactor) {
  if (!IsBlendFactor(sfactor) || !IsBlendFactor(dfactor)) {
    errors_.Record(kGlInvalidEnum);
    return;
  }
  for (auto& b : blend_) {
    b.src_rgb = sfactor;
    b.dst_rgb = dfactor;
    b.src_alpha = sfactor;
    b.dst_alpha = dfactor;
  }
}

void RasterState::BlendFuncSeparate(GLenum src_rgb, GLenum dst_rgb,
                                    GLenum src_alpha, GLenum dst_alpha) {
  if (!IsBlendFactor(src_rgb) || !IsBlendFactor(dst_rgb) ||
      !IsBlendFactor(src_alpha) || !IsBlendFactor(dst_alpha)) {
    errors_.Record(kGlInvalidEnum);
    return;
  }
  for (auto& b : blend_) {
    b.src_rgb = src_rgb;
    b.dst_rgb = dst_rgb;
    b.src_alpha = src_alpha;
    b.dst_alpha = dst_alpha;
  }
}

void RasterState::BlendFunci(GLuint buf, GLenum sfactor, GLenum dfactor) {
  if (!IsBlendFactor(sfactor) || !IsBlendFactor(dfactor)) {
    errors_.Record(kGlInvalidEnum);
    return;
  }
  if (buf >= static_cast<GLuint>(kMaxDrawBufferCount)) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  blend_[buf].src_rgb = sfactor;
  blend_[buf].dst_rgb = dfactor;
  blend_[buf].src_alpha = sfactor;
  blend_[buf].dst_alpha = dfactor;
}

void RasterState::BlendColor(GLfloat r, GLfloat g, GLfloat b, GLfloat a) {
  blend_color_[0] = r;
  blend_color_[1] = g;
  blend_color_[2] = b;
  blend_color_[3] = a;
}

void RasterState::DepthFunc(GLenum func) {
  if (!IsCompareFunc(func)) {
    errors_.Record(kGlInvalidEnum);
    return;
  }
  depth_func_ = func;
}

void RasterState::DepthMask(GLboolean flag) { depth_mask_ = (flag != 0); }

void RasterState::StencilFunc(GLenum func, GLint ref, GLuint mask) {
  if (!IsCompareFunc(func)) {
    errors_.Record(kGlInvalidEnum);
    return;
  }
  stencil_func_front_ = stencil_func_back_ = func;
  stencil_ref_front_ = stencil_ref_back_ = ref;
  stencil_mask_front_ = stencil_mask_back_ = mask;
}

void RasterState::StencilOp(GLenum sfail, GLenum dpfail, GLenum dppass) {
  if (!IsStencilOp(sfail) || !IsStencilOp(dpfail) || !IsStencilOp(dppass)) {
    errors_.Record(kGlInvalidEnum);
    return;
  }
  stencil_sfail_front_ = stencil_sfail_back_ = sfail;
  stencil_dpfail_front_ = stencil_dpfail_back_ = dpfail;
  stencil_dppass_front_ = stencil_dppass_back_ = dppass;
}

void RasterState::StencilFuncSeparate(GLenum face, GLenum func, GLint ref,
                                      GLuint mask) {
  if (!IsFace(face)) {
    errors_.Record(kGlInvalidEnum);
    return;
  }
  if (!IsCompareFunc(func)) {
    errors_.Record(kGlInvalidEnum);
    return;
  }
  if (face == kGlFront || face == kGlFrontAndBack) {
    stencil_func_front_ = func;
    stencil_ref_front_ = ref;
    stencil_mask_front_ = mask;
  }
  if (face == kGlBackFace || face == kGlFrontAndBack) {
    stencil_func_back_ = func;
    stencil_ref_back_ = ref;
    stencil_mask_back_ = mask;
  }
}

void RasterState::StencilOpSeparate(GLenum face, GLenum sfail, GLenum dpfail,
                                    GLenum dppass) {
  if (!IsFace(face)) {
    errors_.Record(kGlInvalidEnum);
    return;
  }
  if (!IsStencilOp(sfail) || !IsStencilOp(dpfail) || !IsStencilOp(dppass)) {
    errors_.Record(kGlInvalidEnum);
    return;
  }
  if (face == kGlFront || face == kGlFrontAndBack) {
    stencil_sfail_front_ = sfail;
    stencil_dpfail_front_ = dpfail;
    stencil_dppass_front_ = dppass;
  }
  if (face == kGlBackFace || face == kGlFrontAndBack) {
    stencil_sfail_back_ = sfail;
    stencil_dpfail_back_ = dpfail;
    stencil_dppass_back_ = dppass;
  }
}

void RasterState::StencilMask(GLuint mask) {
  stencil_writemask_front_ = stencil_writemask_back_ = mask;
}

void RasterState::StencilMaskSeparate(GLenum face, GLuint mask) {
  if (!IsFace(face)) {
    errors_.Record(kGlInvalidEnum);
    return;
  }
  if (face == kGlFront || face == kGlFrontAndBack) {
    stencil_writemask_front_ = mask;
  }
  if (face == kGlBackFace || face == kGlFrontAndBack) {
    stencil_writemask_back_ = mask;
  }
}

void RasterState::SampleCoverage(GLfloat value, GLboolean invert) {
  sample_coverage_ = std::clamp(value, 0.0f, 1.0f);
  sample_invert_ = (invert != 0);
}

void RasterState::SampleMaski(GLuint mask_number, GLbitfield mask) {
  if (mask_number >= 1) {  // MAX_SAMPLE_MASK_WORDS == 1.
    errors_.Record(kGlInvalidValue);
    return;
  }
  sample_mask_[mask_number] = mask;
}

void RasterState::ColorMask(GLboolean r, GLboolean g, GLboolean b,
                            GLboolean a) {
  for (auto& m : color_mask_) m = {r != 0, g != 0, b != 0, a != 0};
}

void RasterState::ColorMaski(GLuint buf, GLboolean r, GLboolean g, GLboolean b,
                             GLboolean a) {
  if (buf >= static_cast<GLuint>(kMaxDrawBufferCount)) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  color_mask_[buf] = {r != 0, g != 0, b != 0, a != 0};
}

void RasterState::ClearColor(GLfloat r, GLfloat g, GLfloat b, GLfloat a) {
  clear_color_[0] = r;
  clear_color_[1] = g;
  clear_color_[2] = b;
  clear_color_[3] = a;
}

void RasterState::ClearDepthf(GLfloat depth) { clear_depth_ = depth; }
void RasterState::ClearStencil(GLint stencil) { clear_stencil_ = stencil; }

ViewportRect RasterState::GetViewport() const { return viewport_; }
BlendState RasterState::GetBlend(GLuint buf) const {
  return buf < blend_.size() ? blend_[buf] : BlendState();
}
GLenum RasterState::GetDepthFunc() const { return depth_func_; }
bool RasterState::GetDepthMask() const { return depth_mask_; }
GLfloat RasterState::GetLineWidth() const { return line_width_; }
GLfloat RasterState::GetClearDepth() const { return clear_depth_; }

}  // namespace tgles
