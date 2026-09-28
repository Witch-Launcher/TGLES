#ifndef TGLES_FRAMEBUFFER_H
#define TGLES_FRAMEBUFFER_H

// Framebuffers and renderbuffers (spec chapter 9): attachment, completeness
// (9.4), draw/read buffers, blitting, invalidation and renderbuffer storage.

#include <map>
#include <vector>

#include "tgles/base/error.h"
#include "tgles/base/gl_types.h"
#include "tgles/state/texture.h"  // TextureManager + format classifiers.

namespace tgles {

// FBO targets.
inline constexpr GLenum kGlReadFramebuffer = 0x8CA8;
inline constexpr GLenum kGlDrawFramebuffer = 0x8CA9;
// GL_FRAMEBUFFER (gl.xml, both bindings) — NOT 0x8CA9: aliasing it to
// GL_DRAW_FRAMEBUFFER made every draw-bind also clobber the read binding,
// so a READ-then-DRAW bind sequence (what an app does before glBlitFramebuffer
// into the window) left the read FBO at 0.
inline constexpr GLenum kGlFramebuffer = 0x8D40;
inline constexpr GLenum kGlRenderbuffer = 0x8D41;

// Attachments.
inline constexpr GLenum kGlColorAttachment0 = 0x8CE0;
inline constexpr GLenum kGlDepthAttachment = 0x8D00;
inline constexpr GLenum kGlStencilAttachment = 0x8D20;
inline constexpr GLenum kGlDepthStencilAttachment = 0x821A;

// Completeness codes.
inline constexpr GLenum kGlFramebufferComplete = 0x8CD5;
inline constexpr GLenum kGlFramebufferIncompleteAttachment = 0x8CD6;
inline constexpr GLenum kGlFramebufferIncompleteMissingAttachment = 0x8CD7;
inline constexpr GLenum kGlFramebufferIncompleteDimensions = 0x8CD9;
inline constexpr GLenum kGlFramebufferUnsupported = 0x8CDD;
inline constexpr GLenum kGlFramebufferIncompleteMultisample = 0x8D56;

// Draw/read selectors.
inline constexpr GLenum kGlNoneDraw = 0;
inline constexpr GLenum kGlBack = 0x0405;

// Framebuffer parameter names.
inline constexpr GLenum kGlFramebufferDefaultWidth = 0x9310;
inline constexpr GLenum kGlFramebufferDefaultHeight = 0x9311;
inline constexpr GLenum kGlFramebufferDefaultSamples = 0x9313;

// Attachment queries.
inline constexpr GLenum kGlFramebufferAttachmentObjectType = 0x8CD0;
inline constexpr GLenum kGlFramebufferAttachmentObjectName = 0x8CD1;
inline constexpr GLenum kGlFramebufferAttachmentTextureLevel = 0x8CD2;
inline constexpr GLenum kGlFramebufferAttachmentTextureLayer = 0x8CD4;

// Renderbuffer queries.
inline constexpr GLenum kGlRenderbufferWidth = 0x8D42;
inline constexpr GLenum kGlRenderbufferHeight = 0x8D43;
inline constexpr GLenum kGlRenderbufferInternalFormat = 0x8D44;
inline constexpr GLenum kGlRenderbufferSamples = 0x8CAB;

// Blit bits.
inline constexpr GLbitfield kGlColorBufferBit = 0x00004000;
inline constexpr GLbitfield kGlDepthBufferBit = 0x00000100;
inline constexpr GLbitfield kGlStencilBufferBit = 0x00000400;

// Limits (>= spec minimums: 4/4/4/2048).
inline constexpr GLint kMaxDrawBuffers = 8;
inline constexpr GLint kMaxColorAttachments = 8;
inline constexpr GLint kMaxRenderbufferSizeValue = 8192;

class RenderbufferManager {
 public:
  RenderbufferManager();

  GLenum GetError();
  bool HasPending() const;

  void GenRenderbuffers(GLsizei n, GLuint* renderbuffers);
  void DeleteRenderbuffers(GLsizei n, const GLuint* renderbuffers);
  GLboolean IsRenderbuffer(GLuint renderbuffer);
  void BindRenderbuffer(GLenum target, GLuint renderbuffer);
  void RenderbufferStorage(GLenum target, GLenum internalformat, GLsizei width,
                           GLsizei height);
  void RenderbufferStorageMultisample(GLenum target, GLsizei samples,
                                      GLenum internalformat, GLsizei width,
                                      GLsizei height);
  void GetRenderbufferParameteriv(GLenum target, GLenum pname, GLint* params);
  // Internalformat query (spec 9.x, docs.gl/es3): target RENDERBUFFER or
  // TEXTURE_2D_MULTISAMPLE; pname SAMPLES or NUM_SAMPLE_COUNTS. Reports TGL's
  // multisample capability (kMaxSamplesValue=4). bufSize negative ->
  // INVALID_VALUE; unknown target/pname/format -> INVALID_ENUM.
  void GetInternalformativ(GLenum target, GLenum internalformat, GLenum pname,
                           GLsizei bufSize, GLint* params);

  // Introspection for the FBO completeness check.
  bool GetInfo(GLuint renderbuffer, GLenum* internalformat, GLsizei* width,
               GLsizei* height, GLsizei* samples) const;

 private:
  struct Renderbuffer {
    bool alive = false;
    bool defined = false;
    GLenum internalformat = 0;
    GLsizei width = 0;
    GLsizei height = 0;
    GLsizei samples = 0;
  };

  static bool IsStorageFormat(GLenum internalformat);

  ErrorQueue errors_;
  GLuint next_name_ = 1;
  std::map<GLuint, Renderbuffer> renderbuffers_;
  GLuint bound_ = 0;
};

struct Attachment {
  bool present = false;
  bool is_texture = false;
  GLuint name = 0;
  GLenum textarget = 0;
  GLint level = 0;
  GLint layer = 0;
  GLenum internalformat = 0;
  GLsizei width = 0;
  GLsizei height = 0;
  GLsizei samples = 0;
};

class FramebufferManager {
 public:
  FramebufferManager(TextureManager* textures, RenderbufferManager* rbos);

  GLenum GetError();
  bool HasPending() const;

  void GenFramebuffers(GLsizei n, GLuint* framebuffers);
  void DeleteFramebuffers(GLsizei n, const GLuint* framebuffers);
  GLboolean IsFramebuffer(GLuint framebuffer);
  void BindFramebuffer(GLenum target, GLuint framebuffer);

  void FramebufferTexture2D(GLenum target, GLenum attachment, GLenum textarget,
                            GLuint texture, GLint level);
  void FramebufferTextureLayer(GLenum target, GLenum attachment, GLuint texture,
                               GLint level, GLint layer);
  // Layered attach (ES 3.2 core): attaches a whole 3D/array/cube-array texture
  // level (all layers). texture 0 detaches. Bad attachment -> INVALID_ENUM,
  // bad level -> INVALID_VALUE, missing level -> INVALID_OPERATION.
  void FramebufferTexture(GLenum target, GLenum attachment, GLuint texture,
                          GLint level);
  void FramebufferRenderbuffer(GLenum target, GLenum attachment,
                               GLenum renderbuffertarget, GLuint renderbuffer);
  void FramebufferParameteri(GLenum target, GLenum pname, GLint param);
  GLenum CheckFramebufferStatus(GLenum target);

  void DrawBuffers(GLsizei n, const GLenum* bufs);
  void ReadBuffer(GLenum src);
  void BlitFramebuffer(GLint src_x0, GLint src_y0, GLint src_x1, GLint src_y1,
                       GLint dst_x0, GLint dst_y0, GLint dst_x1, GLint dst_y1,
                       GLbitfield mask, GLenum filter);
  void InvalidateFramebuffer(GLenum target, GLsizei num_attachments,
                             const GLenum* attachments);
  // Sub-region invalidate (ES 3.2 core): same attachment rules as
  // InvalidateFramebuffer plus non-negative region, else INVALID_VALUE.
  void InvalidateSubFramebuffer(GLenum target, GLsizei num_attachments,
                                const GLenum* attachments, GLint x, GLint y,
                                GLsizei width, GLsizei height);
  void GetFramebufferAttachmentParameteriv(GLenum target, GLenum attachment,
                                           GLenum pname, GLint* params);
  // Default-framebuffer parameters (spec 9.2 FramebufferParameteri): stored
  // per-FBO (the old code accepted and dropped them). Unknown pname is
  // INVALID_ENUM; the window-system FBO (name 0) has no stored defaults, so
  // reading it is INVALID_OPERATION.
  void GetFramebufferParameteriv(GLenum target, GLenum pname, GLint* params);
  // glClear on the CPU model (spec 17.3): fills the bound draw-FBO's
  // COLOR_ATTACHMENT0 texture store with the current clear color. Rules a
  // reader can check against the spec: unknown mask bits -> INVALID_VALUE;
  // default framebuffer (name 0, window size unknown) -> INVALID_OPERATION;
  // incomplete FBO -> INVALID_FRAMEBUFFER_OPERATION; non-RGBA8/non-texture
  // color target -> INVALID_OPERATION; DEPTH/STENCIL bits with a present
  // attachment -> INVALID_OPERATION (no depth/stencil store exists yet) and
  // are ignored when no such attachment exists. MRT clears (attachments
  // 1..7) are tracked work, not silent success.
  void Clear(GLbitfield mask, const GLfloat clear_color[4]);
  // Integer clears (spec 17.3, docs.gl/gl4 verified): COLOR drawbuffer i with
  // 4 ints/uints. Type mismatch with the attachment is UNDEFINED (not an
  // error) per spec, so RGBA8 accepts all three forms via conversion.
  // STENCIL (iv only) with no stencil attachment is a no-op success.
  void ClearBufferiv(GLenum buffer, GLint drawbuffer, const GLint* value);
  void ClearBufferuiv(GLenum buffer, GLint drawbuffer, const GLuint* value);

  // Test helpers.
  Attachment AttachmentState(GLuint framebuffer, GLenum attachment) const;
  GLuint BoundFramebuffer(GLenum target) const;
  // Draw-buffer list for the MRT path (the stored vector; defaults to
  // {COLOR_ATTACHMENT0} when DrawBuffers was never called).
  std::vector<GLenum> DrawBufferList(GLuint framebuffer) const;
  // Window-system size for the default framebuffer (name 0). Set from the
  // host present path (tglHostAttachMetalLayer / tglHostResizeMetalLayer)
  // via EglState::SetWindowSurfaceSize → HostRuntime. Until set, FBO 0
  // draws fail closed (size unknown) — same as the old Attachment() gap,
  // but once set the default FB is drawable at the layer size.
  void SetDefaultFramebufferSize(GLsizei width, GLsizei height);
  void DefaultFramebufferSize(GLsizei* width, GLsizei* height) const;
  // True when a glClear hit the default framebuffer (window) OR the
  // framebuffer that is presented to the window (learned from a
  // glBlitFramebuffer into FBO 0). The CPU model cannot fill window pixels;
  // the facade consumes this before the next bridge pass so the pass clear
  // (glClearColor state) applies on device.
  bool ConsumeWindowClear();
  void MarkWindowClear();
  // Color captured by the last window (or window-source) clear. The facade
  // uses it for the device-side pass clear instead of the live glClearColor
  // state, which by then may have been overwritten by a clear of some other
  // FBO (MC clears its GUI target after the main target every frame).
  void GetWindowClearColor(GLfloat out[4]) const;
  // FBO last blitted into the window (0 = none learned yet).
  GLuint WindowSource() const { return window_source_; }
  bool HasWindowSource() const { return window_source_ != 0; }

 private:
  struct Framebuffer {
    bool alive = false;
    std::map<GLenum, Attachment> attachments;  // Keyed by attachment enum.
    std::vector<GLenum> draw_buffers = {kGlColorAttachment0};
    GLenum read_buffer = kGlColorAttachment0;
    GLint default_width = 0;
    GLint default_height = 0;
    GLint default_samples = 0;
  };

  static bool IsFramebufferTarget(GLenum target);
  static bool IsColorAttachment(GLenum attachment);
  static bool IsFramebufferAttachment(GLenum attachment);
  Framebuffer* BoundForWrite(GLenum target);
  const Framebuffer* BoundForRead(GLenum target) const;
  bool ResolveTexture(GLuint texture, GLenum textarget, GLint level,
                      GLint layer, Attachment* out);
  bool ResolveRenderbuffer(GLuint renderbuffer, Attachment* out);

  ErrorQueue errors_;
  TextureManager* textures_;
  RenderbufferManager* rbos_;
  GLuint next_name_ = 1;
  std::map<GLuint, Framebuffer> framebuffers_;
  GLuint bound_draw_ = 0;
  GLuint bound_read_ = 0;
  GLsizei default_fb_width_ = 0;
  GLsizei default_fb_height_ = 0;
  bool window_clear_pending_ = false;
  GLuint window_source_ = 0;
  GLfloat window_clear_color_[4] = {0.f, 0.f, 0.f, 0.f};
};

}  // namespace tgles

#endif  // TGLES_FRAMEBUFFER_H