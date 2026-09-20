#ifndef TGLES_FRAMEBUFFER_H
#define TGLES_FRAMEBUFFER_H

// Framebuffers and renderbuffers (spec chapter 9): attachment, completeness
// (9.4), draw/read buffers, blitting, invalidation and renderbuffer storage.

#include <map>
#include <vector>

#include "tgles/error.h"
#include "tgles/gl_types.h"
#include "tgles/texture.h"  // TextureManager + format classifiers.

namespace tgles {

// FBO targets.
inline constexpr GLenum kGlReadFramebuffer = 0x8CA8;
inline constexpr GLenum kGlDrawFramebuffer = 0x8CA9;
inline constexpr GLenum kGlFramebuffer = 0x8CA9;
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
  void GetFramebufferAttachmentParameteriv(GLenum target, GLenum attachment,
                                           GLenum pname, GLint* params);

  // Test helpers.
  Attachment AttachmentState(GLuint framebuffer, GLenum attachment) const;
  GLuint BoundFramebuffer(GLenum target) const;

 private:
  struct Framebuffer {
    bool alive = false;
    std::map<GLenum, Attachment> attachments;  // Keyed by attachment enum.
    std::vector<GLenum> draw_buffers = {kGlColorAttachment0};
    GLenum read_buffer = kGlColorAttachment0;
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
};

}  // namespace tgles

#endif  // TGLES_FRAMEBUFFER_H