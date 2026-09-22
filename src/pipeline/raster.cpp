#include "tgles/pipeline/raster.h"

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
    case kGlSrc1Color:
    case kGlSrc1Alpha:
    case kGlOneMinusSrc1Color:
    case kGlOneMinusSrc1Alpha:
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

void RasterState::BlendEquationSeparatei(GLuint buf, GLenum mode_rgb,
                                         GLenum mode_alpha) {
  if (!IsBlendEquation(mode_rgb) || !IsBlendEquation(mode_alpha)) {
    errors_.Record(kGlInvalidEnum);
    return;
  }
  if (buf >= static_cast<GLuint>(kMaxDrawBufferCount)) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  blend_[buf].equation_rgb = mode_rgb;
  blend_[buf].equation_alpha = mode_alpha;
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

void RasterState::BlendFuncSeparatei(GLuint buf, GLenum src_rgb,
                                     GLenum dst_rgb, GLenum src_alpha,
                                     GLenum dst_alpha) {
  if (!IsBlendFactor(src_rgb) || !IsBlendFactor(dst_rgb) ||
      !IsBlendFactor(src_alpha) || !IsBlendFactor(dst_alpha)) {
    errors_.Record(kGlInvalidEnum);
    return;
  }
  if (buf >= static_cast<GLuint>(kMaxDrawBufferCount)) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  blend_[buf].src_rgb = src_rgb;
  blend_[buf].dst_rgb = dst_rgb;
  blend_[buf].src_alpha = src_alpha;
  blend_[buf].dst_alpha = dst_alpha;
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
ViewportRect RasterState::GetScissor() const { return scissor_; }
GLfloat RasterState::GetPolygonFactor() const { return polygon_factor_; }
GLfloat RasterState::GetPolygonUnits() const { return polygon_units_; }
BlendState RasterState::GetBlend(GLuint buf) const {
  return buf < blend_.size() ? blend_[buf] : BlendState();
}

bool RasterState::IsBlendEnabledForBuffer(GLuint buf) const {
  return buf < blend_enabled_.size() ? blend_enabled_[buf] : false;
}

void RasterState::GetColorMask(GLuint buf, bool out_rgba[4]) const {
  const auto& m = (buf < color_mask_.size())
                      ? color_mask_[buf]
                      : std::array<bool, 4>{true, true, true, true};
  for (int i = 0; i < 4; ++i) out_rgba[i] = m[static_cast<std::size_t>(i)];
}

void RasterState::GetBooleani_v(GLenum target, GLuint index,
                                GLboolean* data) {
  if (data == nullptr) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  if (target != kGlColorWriteMask) {
    errors_.Record(kGlInvalidEnum);
    return;
  }
  if (index >= static_cast<GLuint>(kMaxDrawBufferCount)) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  bool mask[4] = {true, true, true, true};
  GetColorMask(index, mask);
  for (int i = 0; i < 4; ++i) {
    data[i] = mask[i] ? kGlTrue : kGlFalse;
  }
}

void RasterState::GetMultisamplefv(GLenum pname, GLuint index, GLfloat* val) {
  static constexpr GLenum kSamplePosition = 0x8E50u;
  static constexpr GLuint kMaxSamples = 4u;
  if (val == nullptr) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  if (pname != kSamplePosition) {
    errors_.Record(kGlInvalidEnum);
    return;
  }
  if (index >= kMaxSamples) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  // Fixed 4x MSAA pattern in [0,1]^2 (matches Metal MTLSamplePosition style).
  static constexpr GLfloat kPos[4][2] = {
      {0.375f, 0.125f}, {0.875f, 0.375f}, {0.125f, 0.625f}, {0.625f, 0.875f}};
  val[0] = kPos[index][0];
  val[1] = kPos[index][1];
}

namespace {
// Indexed caps: only BLEND is truly per-buffer in ES (MAX_DRAW_BUFFERS=8).
// Other global caps accept only index 0 (gl4 refpage note). Values from
// Context's core cap list (13 caps) + BLEND.
bool IsIndexedCap(GLenum cap) { return cap == 0x0BE2u; }  // BLEND
bool IsGlobalCap(GLenum cap) {
  switch (cap) {
    case 0x0BE2u:  // BLEND
    case 0x0B71u:  // DEPTH_TEST
    case 0x0B44u:  // CULL_FACE
    case 0x0B90u:  // STENCIL_TEST
    case 0x0C11u:  // SCISSOR_TEST
    case 0x8037u:  // POLYGON_OFFSET_FILL
    case 0x809Du:  // SAMPLE_ALPHA_TO_COVERAGE
    case 0x80A0u:  // SAMPLE_COVERAGE
    case 0x0BD0u:  // DITHER
    case 0x8DBBu:  // PRIMITIVE_RESTART_FIXED_INDEX
    case 0x8038u:  // SAMPLE_MASK (ES 3.2)
    case 0x8DD9u:  // RASTERIZER_DISCARD
    case 0x8246u:  // DEBUG_OUTPUT (context cap, indexed only 0)
      return true;
    default:
      return false;
  }
}
bool IsLogicOpcode(GLenum op) {
  switch (op) {
    case 0x1500u:  // CLEAR
    case 0x1501u:  // AND
    case 0x1502u:  // AND_REVERSE
    case 0x1503u:  // COPY
    case 0x1504u:  // AND_INVERTED
    case 0x1505u:  // NOOP
    case 0x1506u:  // XOR
    case 0x1507u:  // OR
    case 0x1508u:  // NOR
    case 0x1509u:  // EQUIV
    case 0x150Au:  // INVERT
    case 0x150Bu:  // OR_REVERSE
    case 0x150Cu:  // COPY_INVERTED
    case 0x150Du:  // OR_INVERTED
    case 0x150Eu:  // NAND
    case 0x150Fu:  // SET
      return true;
    default:
      return false;
  }
}
}  // namespace

void RasterState::EnableIndexed(GLenum cap, GLuint index) {
  if (!IsIndexedCap(cap) && !IsGlobalCap(cap)) {
    errors_.Record(kGlInvalidEnum);
    return;
  }
  if (IsIndexedCap(cap)) {
    if (index >= static_cast<GLuint>(kMaxDrawBufferCount)) {
      errors_.Record(kGlInvalidValue);
      return;
    }
    blend_enabled_[index] = true;
    return;
  }
  if (index != 0) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  // Non-indexed caps via indexed entry: store BLEND index0 only; others are
  // owned by Context (facade syncs global Enable there). Record success.
}

void RasterState::DisableIndexed(GLenum cap, GLuint index) {
  if (!IsIndexedCap(cap) && !IsGlobalCap(cap)) {
    errors_.Record(kGlInvalidEnum);
    return;
  }
  if (IsIndexedCap(cap)) {
    if (index >= static_cast<GLuint>(kMaxDrawBufferCount)) {
      errors_.Record(kGlInvalidValue);
      return;
    }
    blend_enabled_[index] = false;
    return;
  }
  if (index != 0) {
    errors_.Record(kGlInvalidValue);
    return;
  }
}

GLboolean RasterState::IsEnabledIndexed(GLenum cap, GLuint index) {
  if (!IsIndexedCap(cap) && !IsGlobalCap(cap)) {
    errors_.Record(kGlInvalidEnum);
    return kGlFalse;
  }
  if (IsIndexedCap(cap)) {
    if (index >= static_cast<GLuint>(kMaxDrawBufferCount)) {
      errors_.Record(kGlInvalidValue);
      return kGlFalse;
    }
    return blend_enabled_[index] ? kGlTrue : kGlFalse;
  }
  if (index != 0) {
    errors_.Record(kGlInvalidValue);
    return kGlFalse;
  }
  return kGlFalse;  // Global caps owned by Context; indexed query of them is 0.
}

void RasterState::LogicOp(GLenum opcode) {
  if (!IsLogicOpcode(opcode)) {
    errors_.Record(kGlInvalidEnum);
    return;
  }
  logic_op_ = opcode;
}

void RasterState::PointSize(GLfloat size) {
  if (!(size > 0.0f)) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  point_size_ = size;
}

void RasterState::BlendBarrier() {}

void RasterState::MinSampleShading(GLfloat value) {
  if (!(value >= 0.0f && value <= 1.0f)) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  min_sample_shading_ = value;
}

void RasterState::PrimitiveBoundingBox(GLfloat minX, GLfloat minY, GLfloat minZ,
                                       GLfloat minW, GLfloat maxX, GLfloat maxY,
                                       GLfloat maxZ, GLfloat maxW) {
  bbox_[0] = minX;
  bbox_[1] = minY;
  bbox_[2] = minZ;
  bbox_[3] = minW;
  bbox_[4] = maxX;
  bbox_[5] = maxY;
  bbox_[6] = maxZ;
  bbox_[7] = maxW;
}
GLenum RasterState::GetDepthFunc() const { return depth_func_; }
bool RasterState::GetDepthMask() const { return depth_mask_; }
GLfloat RasterState::GetLineWidth() const { return line_width_; }
GLfloat RasterState::GetClearDepth() const { return clear_depth_; }

GLint RasterState::GetClearStencil() const { return clear_stencil_; }

GLenum RasterState::GetStencilFuncFront() const { return stencil_func_front_; }
GLenum RasterState::GetStencilFuncBack() const { return stencil_func_back_; }
GLint RasterState::GetStencilRefFront() const { return stencil_ref_front_; }
GLint RasterState::GetStencilRefBack() const { return stencil_ref_back_; }
GLuint RasterState::GetStencilValueMaskFront() const {
  return stencil_mask_front_;
}
GLuint RasterState::GetStencilValueMaskBack() const {
  return stencil_mask_back_;
}
GLuint RasterState::GetStencilWriteMaskFront() const {
  return stencil_writemask_front_;
}
GLuint RasterState::GetStencilWriteMaskBack() const {
  return stencil_writemask_back_;
}
GLenum RasterState::GetStencilSfailFront() const { return stencil_sfail_front_; }
GLenum RasterState::GetStencilSfailBack() const { return stencil_sfail_back_; }
GLenum RasterState::GetStencilDpfailFront() const {
  return stencil_dpfail_front_;
}
GLenum RasterState::GetStencilDpfailBack() const { return stencil_dpfail_back_; }
GLenum RasterState::GetStencilDppassFront() const {
  return stencil_dppass_front_;
}
GLenum RasterState::GetStencilDppassBack() const { return stencil_dppass_back_; }

GLenum RasterState::GetCullMode() const { return cull_mode_; }
GLenum RasterState::GetFrontFace() const { return front_face_; }

void RasterState::GetClearColor(GLfloat out_rgba[4]) const {
  for (int i = 0; i < 4; ++i) out_rgba[i] = clear_color_[i];
}

void RasterState::GetBlendColor(GLfloat out_rgba[4]) const {
  for (int i = 0; i < 4; ++i) out_rgba[i] = blend_color_[i];
}

}  // namespace tgles
