#ifndef TGLES_RASTER_H
#define TGLES_RASTER_H

// Rasterization and per-fragment state (spec chapters 13-15): viewport,
// culling, polygon offset, blending (per-buffer), depth/stencil, multisample
// and clear values. All defaults follow the spec.

#include <array>

#include "tgles/error.h"
#include "tgles/gl_types.h"

namespace tgles {

// Face / winding.
inline constexpr GLenum kGlFront = 0x0404;
inline constexpr GLenum kGlBackFace = 0x0405;
inline constexpr GLenum kGlFrontAndBack = 0x0408;
inline constexpr GLenum kGlCw = 0x0900;
inline constexpr GLenum kGlCcw = 0x0901;

// Blend equations and factors.
inline constexpr GLenum kGlFuncAdd = 0x8006;
inline constexpr GLenum kGlFuncSubtract = 0x800A;
inline constexpr GLenum kGlFuncReverseSubtract = 0x800B;
inline constexpr GLenum kGlMinBlend = 0x8007;
inline constexpr GLenum kGlMaxBlend = 0x8008;
inline constexpr GLenum kGlZero = 0;
inline constexpr GLenum kGlOne = 1;
inline constexpr GLenum kGlSrcColor = 0x0300;
inline constexpr GLenum kGlOneMinusSrcColor = 0x0301;
inline constexpr GLenum kGlSrcAlpha = 0x0302;
inline constexpr GLenum kGlOneMinusSrcAlpha = 0x0303;
inline constexpr GLenum kGlDstAlpha = 0x0304;
inline constexpr GLenum kGlOneMinusDstAlpha = 0x0305;
inline constexpr GLenum kGlDstColor = 0x0306;
inline constexpr GLenum kGlOneMinusDstColor = 0x0307;
inline constexpr GLenum kGlSrcAlphaSaturate = 0x0308;
inline constexpr GLenum kGlConstantColor = 0x8001;
inline constexpr GLenum kGlOneMinusConstantColor = 0x8002;
inline constexpr GLenum kGlConstantAlpha = 0x8003;
inline constexpr GLenum kGlOneMinusConstantAlpha = 0x8004;

// Depth / stencil functions and ops.
inline constexpr GLenum kGlNever = 0x0200;
inline constexpr GLenum kGlLess = 0x0201;
inline constexpr GLenum kGlEqual = 0x0202;
inline constexpr GLenum kGlLequal = 0x0203;
inline constexpr GLenum kGlGreater = 0x0204;
inline constexpr GLenum kGlNotequal = 0x0205;
inline constexpr GLenum kGlGequal = 0x0206;
inline constexpr GLenum kGlAlways = 0x0207;
inline constexpr GLenum kGlKeep = 0x1E00;
inline constexpr GLenum kGlReplace = 0x1E01;
inline constexpr GLenum kGlIncr = 0x1E02;
inline constexpr GLenum kGlDecr = 0x1E03;
inline constexpr GLenum kGlInvert = 0x150A;
inline constexpr GLenum kGlIncrWrap = 0x8507;
inline constexpr GLenum kGlDecrWrap = 0x8508;

inline constexpr GLint kMaxDrawBufferCount = 8;  // >= spec minimum 4.

struct ViewportRect {
  GLint x = 0;
  GLint y = 0;
  GLsizei width = 0;
  GLsizei height = 0;
};

struct BlendState {
  GLenum equation_rgb = kGlFuncAdd;
  GLenum equation_alpha = kGlFuncAdd;
  GLenum src_rgb = kGlOne;
  GLenum dst_rgb = kGlZero;
  GLenum src_alpha = kGlOne;
  GLenum dst_alpha = kGlZero;
};

class RasterState {
 public:
  RasterState();

  GLenum GetError();
  bool HasPending() const;

  void Viewport(GLint x, GLint y, GLsizei width, GLsizei height);
  void DepthRangef(GLfloat n, GLfloat f);
  void Scissor(GLint x, GLint y, GLsizei width, GLsizei height);
  void CullFace(GLenum mode);
  void FrontFace(GLenum mode);
  void PolygonOffset(GLfloat factor, GLfloat units);
  void LineWidth(GLfloat width);

  void BlendEquation(GLenum mode);
  void BlendEquationSeparate(GLenum mode_rgb, GLenum mode_alpha);
  void BlendEquationi(GLuint buf, GLenum mode);
  void BlendFunc(GLenum sfactor, GLenum dfactor);
  void BlendFuncSeparate(GLenum src_rgb, GLenum dst_rgb, GLenum src_alpha,
                         GLenum dst_alpha);
  void BlendFunci(GLuint buf, GLenum sfactor, GLenum dfactor);
  void BlendColor(GLfloat r, GLfloat g, GLfloat b, GLfloat a);

  void DepthFunc(GLenum func);
  void DepthMask(GLboolean flag);
  void StencilFunc(GLenum func, GLint ref, GLuint mask);
  void StencilOp(GLenum sfail, GLenum dpfail, GLenum dppass);
  void StencilFuncSeparate(GLenum face, GLenum func, GLint ref, GLuint mask);
  void StencilOpSeparate(GLenum face, GLenum sfail, GLenum dpfail,
                         GLenum dppass);
  void StencilMask(GLuint mask);
  void StencilMaskSeparate(GLenum face, GLuint mask);

  void SampleCoverage(GLfloat value, GLboolean invert);
  void SampleMaski(GLuint mask_number, GLbitfield mask);
  void ColorMask(GLboolean r, GLboolean g, GLboolean b, GLboolean a);
  void ColorMaski(GLuint buf, GLboolean r, GLboolean g, GLboolean b,
                  GLboolean a);

  void ClearColor(GLfloat r, GLfloat g, GLfloat b, GLfloat a);
  void ClearDepthf(GLfloat depth);
  void ClearStencil(GLint stencil);

  // Test getters.
  ViewportRect GetViewport() const;
  BlendState GetBlend(GLuint buf) const;
  GLenum GetDepthFunc() const;
  bool GetDepthMask() const;
  GLfloat GetLineWidth() const;
  GLfloat GetClearDepth() const;

 private:
  static bool IsBlendEquation(GLenum mode);
  static bool IsBlendFactor(GLenum factor);
  static bool IsCompareFunc(GLenum func);
  static bool IsStencilOp(GLenum op);
  static bool IsFace(GLenum face);

  ErrorQueue errors_;
  ViewportRect viewport_;
  GLfloat depth_near_ = 0.0f;
  GLfloat depth_far_ = 1.0f;
  ViewportRect scissor_;
  GLenum cull_mode_ = kGlBackFace;
  GLenum front_face_ = kGlCcw;
  GLfloat polygon_factor_ = 0.0f;
  GLfloat polygon_units_ = 0.0f;
  GLfloat line_width_ = 1.0f;
  std::array<BlendState, kMaxDrawBufferCount> blend_;
  GLfloat blend_color_[4] = {0, 0, 0, 0};
  GLenum depth_func_ = kGlLess;
  bool depth_mask_ = true;
  GLenum stencil_func_front_ = kGlAlways;
  GLenum stencil_func_back_ = kGlAlways;
  GLint stencil_ref_front_ = 0;
  GLint stencil_ref_back_ = 0;
  GLuint stencil_mask_front_ = 0xFFFFFFFFu;
  GLuint stencil_mask_back_ = 0xFFFFFFFFu;
  GLenum stencil_sfail_front_ = kGlKeep;
  GLenum stencil_sfail_back_ = kGlKeep;
  GLenum stencil_dpfail_front_ = kGlKeep;
  GLenum stencil_dpfail_back_ = kGlKeep;
  GLenum stencil_dppass_front_ = kGlKeep;
  GLenum stencil_dppass_back_ = kGlKeep;
  GLuint stencil_writemask_front_ = 0xFFFFFFFFu;
  GLuint stencil_writemask_back_ = 0xFFFFFFFFu;
  GLfloat sample_coverage_ = 1.0f;
  bool sample_invert_ = false;
  std::array<GLbitfield, 1> sample_mask_ = {0xFFFFFFFFu};
  std::array<std::array<bool, 4>, kMaxDrawBufferCount> color_mask_;
  GLfloat clear_color_[4] = {0, 0, 0, 0};
  GLfloat clear_depth_ = 1.0f;
  GLint clear_stencil_ = 0;
};

}  // namespace tgles

#endif  // TGLES_RASTER_H
