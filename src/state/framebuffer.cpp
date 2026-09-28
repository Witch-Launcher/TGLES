#include "tgles/state/framebuffer.h"

#include "tgles/base/debug_log.h"

namespace tgles {

// --- Renderbuffers ---

RenderbufferManager::RenderbufferManager() = default;

GLenum RenderbufferManager::GetError() { return errors_.Get(); }
bool RenderbufferManager::HasPending() const { return errors_.HasPending(); }

bool RenderbufferManager::IsStorageFormat(GLenum internalformat) {
  return GlIsColorRenderable(internalformat) ||
         GlIsDepthFormat(internalformat) ||
         GlIsDepthStencilFormat(internalformat);
}

void RenderbufferManager::GenRenderbuffers(GLsizei n, GLuint* renderbuffers) {
  if (n < 0 || (n > 0 && renderbuffers == nullptr)) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  for (GLsizei i = 0; i < n; ++i) {
    renderbuffers[i] = next_name_++;
    renderbuffers_[renderbuffers[i]] = Renderbuffer();
  }
}

void RenderbufferManager::DeleteRenderbuffers(GLsizei n,
                                              const GLuint* renderbuffers) {
  if (n < 0 || (n > 0 && renderbuffers == nullptr)) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  for (GLsizei i = 0; i < n; ++i) {
    if (renderbuffers[i] == 0) continue;
    renderbuffers_.erase(renderbuffers[i]);
    if (bound_ == renderbuffers[i]) bound_ = 0;
  }
}

GLboolean RenderbufferManager::IsRenderbuffer(GLuint renderbuffer) {
  if (renderbuffer == 0) return kGlFalse;
  auto it = renderbuffers_.find(renderbuffer);
  return (it != renderbuffers_.end() && it->second.alive) ? kGlTrue : kGlFalse;
}

void RenderbufferManager::BindRenderbuffer(GLenum target, GLuint renderbuffer) {
  if (target != kGlRenderbuffer) {
    errors_.Record(kGlInvalidEnum);
    return;
  }
  if (renderbuffer != 0) {
    auto it = renderbuffers_.find(renderbuffer);
    if (it == renderbuffers_.end()) {
      renderbuffers_[renderbuffer] = Renderbuffer();
      it = renderbuffers_.find(renderbuffer);
    }
    it->second.alive = true;
  }
  bound_ = renderbuffer;
}

void RenderbufferManager::RenderbufferStorage(GLenum target,
                                              GLenum internalformat,
                                              GLsizei width, GLsizei height) {
  RenderbufferStorageMultisample(target, 0, internalformat, width, height);
}

void RenderbufferManager::RenderbufferStorageMultisample(GLenum target,
                                                         GLsizei samples,
                                                         GLenum internalformat,
                                                         GLsizei width,
                                                         GLsizei height) {
  if (target != kGlRenderbuffer) {
    errors_.Record(kGlInvalidEnum);
    return;
  }
  if (!IsStorageFormat(internalformat)) {
    errors_.Record(kGlInvalidEnum);
    return;
  }
  if (width < 0 || height < 0 || width > kMaxRenderbufferSizeValue ||
      height > kMaxRenderbufferSizeValue) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  if (samples < 0 || samples > kMaxSamplesValue) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  if (bound_ == 0) {
    errors_.Record(kGlInvalidOperation);
    return;
  }
  Renderbuffer& rb = renderbuffers_[bound_];
  rb.alive = true;
  rb.defined = true;
  rb.internalformat = internalformat;
  rb.width = width;
  rb.height = height;
  rb.samples = samples;
}

void RenderbufferManager::GetRenderbufferParameteriv(GLenum target, GLenum pname,
                                                     GLint* params) {
  if (params == nullptr) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  if (target != kGlRenderbuffer) {
    errors_.Record(kGlInvalidEnum);
    return;
  }
  if (bound_ == 0) {
    errors_.Record(kGlInvalidOperation);
    return;
  }
  const Renderbuffer& rb = renderbuffers_[bound_];
  switch (pname) {
    case kGlRenderbufferWidth:
      *params = rb.width;
      return;
    case kGlRenderbufferHeight:
      *params = rb.height;
      return;
    case kGlRenderbufferInternalFormat:
      *params = static_cast<GLint>(rb.internalformat);
      return;
    case kGlRenderbufferSamples:
      *params = rb.samples;
      return;
    default:
      break;
  }
  errors_.Record(kGlInvalidEnum);
}

void RenderbufferManager::GetInternalformativ(GLenum target,
                                             GLenum internalformat,
                                             GLenum pname, GLsizei bufSize,
                                             GLint* params) {
  // Values from docs/reference/gl32.h: verified, not TGL-invented.
  static constexpr GLenum kTexture2DMultisample = 0x9100u;
  static constexpr GLenum kSamplesPname = 0x80A9u;
  static constexpr GLenum kNumSampleCounts = 0x9380u;
  if (bufSize < 0 || (bufSize > 0 && params == nullptr)) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  if (target != kGlRenderbuffer && target != kTexture2DMultisample) {
    errors_.Record(kGlInvalidEnum);
    return;
  }
  if (pname != kSamplesPname && pname != kNumSampleCounts) {
    errors_.Record(kGlInvalidEnum);
    return;
  }
  if (!IsStorageFormat(internalformat)) {
    errors_.Record(kGlInvalidEnum);
    return;
  }
  // TGL supports 0/1/4 samples for color/depth (kMaxSamplesValue=4). Report
  // descending counts; NUM_SAMPLE_COUNTS is the list length.
  static constexpr GLint kCounts[] = {4, 1};
  if (pname == kNumSampleCounts) {
    if (bufSize >= 1 && params != nullptr) params[0] = 2;
    return;
  }
  const GLsizei n = bufSize < 2 ? bufSize : 2;
  for (GLsizei i = 0; i < n; ++i) params[i] = kCounts[i];
}

bool RenderbufferManager::GetInfo(GLuint renderbuffer, GLenum* internalformat,
                                  GLsizei* width, GLsizei* height,
                                  GLsizei* samples) const {
  auto it = renderbuffers_.find(renderbuffer);
  if (it == renderbuffers_.end() || !it->second.alive ||
      !it->second.defined) {
    return false;
  }
  if (internalformat != nullptr) *internalformat = it->second.internalformat;
  if (width != nullptr) *width = it->second.width;
  if (height != nullptr) *height = it->second.height;
  if (samples != nullptr) *samples = it->second.samples;
  return true;
}

// --- Framebuffers ---

FramebufferManager::FramebufferManager(TextureManager* textures,
                                       RenderbufferManager* rbos)
    : textures_(textures), rbos_(rbos) {}

GLenum FramebufferManager::GetError() { return errors_.Get(); }
bool FramebufferManager::HasPending() const { return errors_.HasPending(); }

bool FramebufferManager::IsFramebufferTarget(GLenum target) {
  return target == kGlFramebuffer || target == kGlReadFramebuffer ||
         target == kGlDrawFramebuffer;
}

bool FramebufferManager::IsColorAttachment(GLenum attachment) {
  return attachment >= kGlColorAttachment0 &&
         attachment <
             kGlColorAttachment0 + static_cast<GLenum>(kMaxColorAttachments);
}

bool FramebufferManager::IsFramebufferAttachment(GLenum attachment) {
  return IsColorAttachment(attachment) ||
         attachment == kGlDepthAttachment ||
         attachment == kGlStencilAttachment ||
         attachment == kGlDepthStencilAttachment;
}

FramebufferManager::Framebuffer* FramebufferManager::BoundForWrite(
    GLenum target) {
  if (!IsFramebufferTarget(target)) {
    errors_.Record(kGlInvalidEnum);
    return nullptr;
  }
  const GLuint name =
      (target == kGlReadFramebuffer) ? bound_read_ : bound_draw_;
  if (name == 0) {
    errors_.Record(kGlInvalidOperation);  // Default FBO has no attachments.
    return nullptr;
  }
  auto it = framebuffers_.find(name);
  if (it == framebuffers_.end() || !it->second.alive) {
    errors_.Record(kGlInvalidOperation);
    return nullptr;
  }
  return &it->second;
}

const FramebufferManager::Framebuffer* FramebufferManager::BoundForRead(
    GLenum target) const {
  return const_cast<FramebufferManager*>(this)->BoundForWrite(target);
}

void FramebufferManager::GenFramebuffers(GLsizei n, GLuint* framebuffers) {
  if (n < 0 || (n > 0 && framebuffers == nullptr)) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  for (GLsizei i = 0; i < n; ++i) {
    framebuffers[i] = next_name_++;
    framebuffers_[framebuffers[i]] = Framebuffer();
  }
}

void FramebufferManager::DeleteFramebuffers(GLsizei n,
                                            const GLuint* framebuffers) {
  if (n < 0 || (n > 0 && framebuffers == nullptr)) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  for (GLsizei i = 0; i < n; ++i) {
    if (framebuffers[i] == 0) continue;
    framebuffers_.erase(framebuffers[i]);
    if (bound_draw_ == framebuffers[i]) bound_draw_ = 0;
    if (bound_read_ == framebuffers[i]) bound_read_ = 0;
  }
}

GLboolean FramebufferManager::IsFramebuffer(GLuint framebuffer) {
  if (framebuffer == 0) return kGlFalse;
  auto it = framebuffers_.find(framebuffer);
  return (it != framebuffers_.end() && it->second.alive) ? kGlTrue : kGlFalse;
}

void FramebufferManager::BindFramebuffer(GLenum target, GLuint framebuffer) {
  if (!IsFramebufferTarget(target)) {
    errors_.Record(kGlInvalidEnum);
    return;
  }
  if (framebuffer != 0) {
    auto it = framebuffers_.find(framebuffer);
    if (it == framebuffers_.end()) {
      framebuffers_[framebuffer] = Framebuffer();
      it = framebuffers_.find(framebuffer);
    }
    it->second.alive = true;
  }
  if (target == kGlReadFramebuffer) {
    bound_read_ = framebuffer;
  } else {
    bound_draw_ = framebuffer;  // FRAMEBUFFER and DRAW_FRAMEBUFFER.
    if (target == kGlFramebuffer) bound_read_ = framebuffer;
  }
}

bool FramebufferManager::ResolveTexture(GLuint texture, GLenum textarget,
                                        GLint level, GLint layer,
                                        Attachment* out) {
  if (textures_ == nullptr) return false;
  if (textures_->IsTexture(texture) == kGlFalse) return false;
  GLenum face = textarget;
  if (textarget == kGlTextureCubeMap) face = kGlTextureCubeMapPositiveX;
  TextureLevel lvl = textures_->LevelState(texture, face, level);
  if (!lvl.defined) {
    // Unsized cube faces bound via BindTexture without storage land here.
    if (textarget == kGlTextureCubeMap) {
      for (int f = 0; f < 6; ++f) {
        lvl = textures_->LevelState(
            texture, kGlTextureCubeMapPositiveX + f, level);
        if (lvl.defined) break;
      }
    }
    if (!lvl.defined) return false;
  }
  out->present = true;
  out->is_texture = true;
  out->name = texture;
  out->textarget = textarget;
  out->level = level;
  out->layer = layer;
  out->internalformat = lvl.internalformat;
  out->width = lvl.width;
  out->height = lvl.height;
  out->samples = lvl.samples;
  return true;
}

bool FramebufferManager::ResolveRenderbuffer(GLuint renderbuffer,
                                             Attachment* out) {
  if (rbos_ == nullptr) return false;
  GLenum format = 0;
  GLsizei w = 0, h = 0, samples = 0;
  if (!rbos_->GetInfo(renderbuffer, &format, &w, &h, &samples)) return false;
  out->present = true;
  out->is_texture = false;
  out->name = renderbuffer;
  out->internalformat = format;
  out->width = w;
  out->height = h;
  out->samples = samples;
  return true;
}

void FramebufferManager::FramebufferTexture2D(GLenum target, GLenum attachment,
                                              GLenum textarget, GLuint texture,
                                              GLint level) {
  Framebuffer* fb = BoundForWrite(target);
  if (fb == nullptr) return;
  if (!IsFramebufferAttachment(attachment)) {
    errors_.Record(kGlInvalidEnum);
    return;
  }
  const bool valid_textarget =
      textarget == kGlTexture2d || textarget == kGlTextureCubeMap ||
      (textarget >= kGlTextureCubeMapPositiveX &&
       textarget < kGlTextureCubeMapPositiveX + 6) ||
      textarget == kGlTexture2dMultisample;
  if (!valid_textarget) {
    errors_.Record(kGlInvalidEnum);
    return;
  }
  if (level < 0) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  if (texture == 0) {
    fb->attachments.erase(attachment);  // Detach.
    return;
  }
  Attachment a;
  if (!ResolveTexture(texture, textarget, level, 0, &a)) {
    errors_.Record(kGlInvalidOperation);
    return;
  }
  fb->attachments[attachment] = a;
}

void FramebufferManager::FramebufferTextureLayer(GLenum target,
                                                 GLenum attachment,
                                                 GLuint texture, GLint level,
                                                 GLint layer) {
  Framebuffer* fb = BoundForWrite(target);
  if (fb == nullptr) return;
  if (!IsFramebufferAttachment(attachment)) {
    errors_.Record(kGlInvalidEnum);
    return;
  }
  if (level < 0 || layer < 0) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  if (texture == 0) {
    fb->attachments.erase(attachment);
    return;
  }
  Attachment a;
  // Layered attach accepts 3D/array/cube-array textures.
  if (!ResolveTexture(texture, kGlTexture3d, level, layer, &a) &&
      !ResolveTexture(texture, kGlTexture2dArray, level, layer, &a) &&
      !ResolveTexture(texture, kGlTextureCubeMapArray, level, layer, &a)) {
    errors_.Record(kGlInvalidOperation);
    return;
  }
  fb->attachments[attachment] = a;
}

void FramebufferManager::FramebufferRenderbuffer(GLenum target,
                                                 GLenum attachment,
                                                 GLenum renderbuffertarget,
                                                 GLuint renderbuffer) {
  Framebuffer* fb = BoundForWrite(target);
  if (fb == nullptr) return;
  if (!IsFramebufferAttachment(attachment)) {
    errors_.Record(kGlInvalidEnum);
    return;
  }
  if (renderbuffertarget != kGlRenderbuffer) {
    errors_.Record(kGlInvalidEnum);
    return;
  }
  if (renderbuffer == 0) {
    fb->attachments.erase(attachment);
    return;
  }
  Attachment a;
  if (!ResolveRenderbuffer(renderbuffer, &a)) {
    errors_.Record(kGlInvalidOperation);
    return;
  }
  fb->attachments[attachment] = a;
}

void FramebufferManager::FramebufferParameteri(GLenum target, GLenum pname,
                                               GLint param) {
  Framebuffer* fb = BoundForWrite(target);
  if (fb == nullptr) return;
  switch (pname) {
    case kGlFramebufferDefaultWidth:
    case kGlFramebufferDefaultHeight:
    case kGlFramebufferDefaultSamples:
      if (param < 0) {
        errors_.Record(kGlInvalidValue);
        return;
      }
      if (pname == kGlFramebufferDefaultWidth) fb->default_width = param;
      if (pname == kGlFramebufferDefaultHeight) fb->default_height = param;
      if (pname == kGlFramebufferDefaultSamples) fb->default_samples = param;
      return;
    default:
      break;
  }
  errors_.Record(kGlInvalidEnum);
}

void FramebufferManager::GetFramebufferParameteriv(GLenum target, GLenum pname,
                                                   GLint* params) {
  if (params == nullptr) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  if (!IsFramebufferTarget(target)) {
    errors_.Record(kGlInvalidEnum);
    return;
  }
  const GLuint name =
      (target == kGlReadFramebuffer) ? bound_read_ : bound_draw_;
  if (name == 0) {
    errors_.Record(kGlInvalidOperation);  // No stored defaults on 0.
    return;
  }
  auto it = framebuffers_.find(name);
  if (it == framebuffers_.end() || !it->second.alive) {
    errors_.Record(kGlInvalidOperation);
    return;
  }
  const Framebuffer& fb = it->second;
  switch (pname) {
    case kGlFramebufferDefaultWidth:
      *params = fb.default_width;
      return;
    case kGlFramebufferDefaultHeight:
      *params = fb.default_height;
      return;
    case kGlFramebufferDefaultSamples:
      *params = fb.default_samples;
      return;
    default:
      break;
  }
  errors_.Record(kGlInvalidEnum);
}

GLenum FramebufferManager::CheckFramebufferStatus(GLenum target) {
  if (!IsFramebufferTarget(target)) {
    errors_.Record(kGlInvalidEnum);
    return 0;
  }
  const GLuint name =
      (target == kGlReadFramebuffer) ? bound_read_ : bound_draw_;
  if (name == 0) return kGlFramebufferComplete;  // Window-system FBO.
  auto it = framebuffers_.find(name);
  if (it == framebuffers_.end() || !it->second.alive) {
    errors_.Record(kGlInvalidOperation);
    return 0;
  }
  const Framebuffer& fb = it->second;
  if (fb.attachments.empty()) {
    return kGlFramebufferIncompleteMissingAttachment;
  }
  GLsizei width = -1, height = -1, samples = -1;
  for (const auto& kv : fb.attachments) {
    const GLenum attachment = kv.first;
    const Attachment& a = kv.second;
    if (!a.present || a.width <= 0 || a.height <= 0) {
      return kGlFramebufferIncompleteAttachment;
    }
    // Format/class compatibility (spec 9.4).
    const bool is_color = IsColorAttachment(attachment);
    const bool is_depth =
        attachment == kGlDepthAttachment || attachment == kGlDepthStencilAttachment;
    const bool is_stencil =
        attachment == kGlStencilAttachment || attachment == kGlDepthStencilAttachment;
    if (is_color && !GlIsColorRenderable(a.internalformat)) {
      return kGlFramebufferIncompleteAttachment;
    }
    if (attachment == kGlDepthAttachment && !GlIsDepthFormat(a.internalformat)) {
      return kGlFramebufferIncompleteAttachment;
    }
    if (attachment == kGlStencilAttachment) {
      return kGlFramebufferIncompleteAttachment;  // No stencil-only formats.
    }
    if (attachment == kGlDepthStencilAttachment &&
        !GlIsDepthStencilFormat(a.internalformat)) {
      return kGlFramebufferIncompleteAttachment;
    }
    if (is_depth && GlIsColorRenderable(a.internalformat)) {
      return kGlFramebufferIncompleteAttachment;
    }
    if (is_stencil && GlIsColorRenderable(a.internalformat)) {
      return kGlFramebufferIncompleteAttachment;
    }
    if (width < 0) {
      width = a.width;
      height = a.height;
      samples = a.samples;
    } else if (a.width != width || a.height != height) {
      return kGlFramebufferIncompleteDimensions;
    } else if (a.samples != samples) {
      return kGlFramebufferIncompleteMultisample;
    }
  }
  return kGlFramebufferComplete;
}

void FramebufferManager::DrawBuffers(GLsizei n, const GLenum* bufs) {
  if (n < 0 || n > kMaxDrawBuffers) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  if (n > 0 && bufs == nullptr) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  const bool is_default = (bound_draw_ == 0);
  for (GLsizei i = 0; i < n; ++i) {
    if (is_default) {
      if (bufs[i] != kGlNoneDraw && bufs[i] != kGlBack) {
        errors_.Record(kGlInvalidOperation);
        return;
      }
    } else {
      if (bufs[i] != kGlNoneDraw && !IsColorAttachment(bufs[i])) {
        errors_.Record(kGlInvalidEnum);
        return;
      }
      if (IsColorAttachment(bufs[i]) &&
          bufs[i] >=
              kGlColorAttachment0 + static_cast<GLenum>(kMaxDrawBuffers)) {
        errors_.Record(kGlInvalidValue);
        return;
      }
    }
  }
  if (bound_draw_ != 0) {
    auto it = framebuffers_.find(bound_draw_);
    if (it != framebuffers_.end()) {
      it->second.draw_buffers.assign(bufs, bufs + n);
    }
  }
}

void FramebufferManager::ReadBuffer(GLenum src) {
  const bool is_default =
      (bound_read_ == 0);  // READ binding follows FRAMEBUFFER binds.
  if (is_default) {
    if (src != kGlNoneDraw && src != kGlBack) {
      errors_.Record(kGlInvalidOperation);
    }
    return;
  }
  if (src != kGlNoneDraw && !IsColorAttachment(src)) {
    errors_.Record(kGlInvalidEnum);
  }
}

void FramebufferManager::BlitFramebuffer(GLint src_x0, GLint src_y0,
                                         GLint src_x1, GLint src_y1,
                                         GLint dst_x0, GLint dst_y0,
                                         GLint dst_x1, GLint dst_y1,
                                         GLbitfield mask, GLenum filter) {
  (void)src_x0;
  (void)src_y0;
  (void)src_x1;
  (void)src_y1;
  (void)dst_x0;
  (void)dst_y0;
  (void)dst_x1;
  (void)dst_y1;
  constexpr GLbitfield kAllBits =
      kGlColorBufferBit | kGlDepthBufferBit | kGlStencilBufferBit;
  if ((mask & ~kAllBits) != 0) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  if (filter != kGlNearest && filter != kGlLinear) {
    errors_.Record(kGlInvalidEnum);
    return;
  }
  if (filter == kGlLinear &&
      (mask & (kGlDepthBufferBit | kGlStencilBufferBit)) != 0) {
    errors_.Record(kGlInvalidOperation);  // LINEAR only for color.
    return;
  }
  // Pixel movement itself is backend business (step 9 blit encoder) — the
  // bridge target already holds every app draw. But a COLOR blit INTO the
  // window (bound draw FBO 0) is how an app says "this FBO is the screen":
  // remember it so its glClear marks a window clear (MC clears its main
  // target every frame, then blits it to 0; the GUI target's clear must not
  // wipe the window).
  if (bound_draw_ == 0 && (mask & kGlColorBufferBit) != 0 &&
      bound_read_ != 0) {
    window_source_ = bound_read_;
  }
  static int s_blit_diag = 0;
  const int n = ++s_blit_diag;
  if (n <= 16) {
    TglDebugf(
        "diag blit#%d read=%u draw=%u mask=0x%x wsrc=%u (stuck: window "
        "source)",
        n, bound_read_, bound_draw_, mask, window_source_);
  }
}

void FramebufferManager::InvalidateFramebuffer(GLenum target,
                                               GLsizei num_attachments,
                                               const GLenum* attachments) {
  if (!IsFramebufferTarget(target)) {
    errors_.Record(kGlInvalidEnum);
    return;
  }
  if (num_attachments < 0 || (num_attachments > 0 && attachments == nullptr)) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  for (GLsizei i = 0; i < num_attachments; ++i) {
    const GLenum a = attachments[i];
    if (a != kGlDepthAttachment && a != kGlStencilAttachment &&
        a != kGlDepthStencilAttachment && !IsColorAttachment(a)) {
      errors_.Record(kGlInvalidEnum);
      return;
    }
  }
}

void FramebufferManager::InvalidateSubFramebuffer(
    GLenum target, GLsizei num_attachments, const GLenum* attachments, GLint x,
    GLint y, GLsizei width, GLsizei height) {
  if (!IsFramebufferTarget(target)) {
    errors_.Record(kGlInvalidEnum);
    return;
  }
  if (num_attachments < 0 || (num_attachments > 0 && attachments == nullptr)) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  if (x < 0 || y < 0 || width < 0 || height < 0) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  for (GLsizei i = 0; i < num_attachments; ++i) {
    const GLenum a = attachments[i];
    if (a != kGlDepthAttachment && a != kGlStencilAttachment &&
        a != kGlDepthStencilAttachment && !IsColorAttachment(a)) {
      errors_.Record(kGlInvalidEnum);
      return;
    }
  }
  // Sub-region is a hint; no store is discarded in the CPU model.
}

void FramebufferManager::FramebufferTexture(GLenum target, GLenum attachment,
                                            GLuint texture, GLint level) {
  Framebuffer* fb = BoundForWrite(target);
  if (fb == nullptr) return;
  if (!IsFramebufferAttachment(attachment)) {
    errors_.Record(kGlInvalidEnum);
    return;
  }
  if (level < 0) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  if (texture == 0) {
    fb->attachments.erase(attachment);
    return;
  }
  Attachment a;
  // Whole-level attach: try 2D, then 3D/array layers (layer 0 representative).
  if (!ResolveTexture(texture, kGlTexture2d, level, 0, &a) &&
      !ResolveTexture(texture, kGlTexture3d, level, 0, &a) &&
      !ResolveTexture(texture, kGlTexture2dArray, level, 0, &a) &&
      !ResolveTexture(texture, kGlTextureCubeMap, level, 0, &a)) {
    errors_.Record(kGlInvalidOperation);
    return;
  }
  fb->attachments[attachment] = a;
}

void FramebufferManager::Clear(GLbitfield mask,
                               const GLfloat clear_color[4]) {
  constexpr GLbitfield kAllBuffers =
      kGlColorBufferBit | kGlDepthBufferBit | kGlStencilBufferBit;
  if ((mask & ~kAllBuffers) != 0) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  if (mask == 0) return;  // Clearing nothing is a no-op (spec 17.3).
  // Capture the color now: the facade must clear the bridge target with
  // THIS clear's color, not with whatever glClearColor happens to be by the
  // time the next draw submits (other FBOs get cleared in between).
  for (int i = 0; i < 4; ++i) window_clear_color_[i] = clear_color[i];
  if (bound_draw_ == 0) {
    // Window-system framebuffer: the CPU model has no window pixels to
    // fill. Do NOT fail closed — the facade marks a pending window clear
    // and the next bridge pass clears on device with the current
    // glClearColor/glClearDepth/glClearStencil state (spec 17.3: the clear
    // is ordered before subsequent draws; Metal loadActionClear at pass
    // start is the device equivalent for the first pass of the frame).
    window_clear_pending_ = true;
    return;
  }
  if (CheckFramebufferStatus(kGlDrawFramebuffer) != kGlFramebufferComplete) {
    errors_.Record(kGlInvalidFramebufferOperation);
    return;
  }
  if ((mask & kGlColorBufferBit) != 0) {
    const Attachment att = AttachmentState(bound_draw_, kGlColorAttachment0);
    if (!att.present || !att.is_texture) {
      errors_.Record(kGlInvalidOperation);
      return;
    }
    std::uint8_t rgba[4];
    for (int i = 0; i < 4; ++i) {
      const GLfloat clamped =
          clear_color[i] < 0.0f ? 0.0f
                                : (clear_color[i] > 1.0f ? 1.0f
                                                         : clear_color[i]);
      rgba[i] = static_cast<std::uint8_t>(clamped * 255.0f + 0.5f);
    }
    if (textures_ == nullptr ||
        !textures_->FillLevel(att.name, att.textarget, att.level, rgba)) {
      errors_.Record(kGlInvalidOperation);
      return;
    }
    // The bound FBO is the one presented to the window (learned from a
    // blit into FBO 0): its clear IS the window clear, so the facade must
    // clear the bridge target with this color at the next pass start.
    // Clears of any other FBO stay in the CPU model only — they must not
    // decide what the window shows (spec 17.3: clearing an offscreen target
    // never touches the default framebuffer).
    if (bound_draw_ == window_source_ && window_source_ != 0) {
      window_clear_pending_ = true;
    }
  }
  if ((mask & kGlDepthBufferBit) != 0) {
    const Attachment att = AttachmentState(bound_draw_, kGlDepthAttachment);
    const Attachment packed =
        AttachmentState(bound_draw_, kGlDepthStencilAttachment);
    if (att.present || packed.present) {
      errors_.Record(kGlInvalidOperation);  // No depth store in CPU model.
      return;
    }
  }
  if ((mask & kGlStencilBufferBit) != 0) {
    const Attachment att = AttachmentState(bound_draw_, kGlStencilAttachment);
    const Attachment packed =
        AttachmentState(bound_draw_, kGlDepthStencilAttachment);
    if (att.present || packed.present) {
      errors_.Record(kGlInvalidOperation);  // No stencil store in CPU model.
      return;
    }
  }
}

void FramebufferManager::ClearBufferiv(GLenum buffer, GLint drawbuffer,
                                       const GLint* value) {
  static constexpr GLenum kColor = 0x1800u;
  static constexpr GLenum kStencil = 0x1802u;
  if (buffer != kColor && buffer != kStencil) {
    errors_.Record(kGlInvalidEnum);
    return;
  }
  if (value == nullptr) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  if (buffer == kColor) {
    if (drawbuffer < 0 || drawbuffer >= kMaxDrawBuffers) {
      errors_.Record(kGlInvalidValue);
      return;
    }
    if (drawbuffer != 0) {
      errors_.Record(kGlInvalidEnum);  // MRT not modeled (same as fv path).
      return;
    }
    if (bound_draw_ == 0) {
      errors_.Record(kGlInvalidOperation);
      return;
    }
    if (CheckFramebufferStatus(kGlDrawFramebuffer) != kGlFramebufferComplete) {
      errors_.Record(kGlInvalidFramebufferOperation);
      return;
    }
    const Attachment att = AttachmentState(bound_draw_, kGlColorAttachment0);
    if (!att.present || !att.is_texture) {
      errors_.Record(kGlInvalidOperation);
      return;
    }
    std::uint8_t rgba[4];
    for (int i = 0; i < 4; ++i) {
      const GLint v = value[i];
      rgba[i] = static_cast<std::uint8_t>(v < 0 ? 0 : (v > 255 ? 255 : v));
    }
    if (textures_ == nullptr ||
        !textures_->FillLevel(att.name, att.textarget, att.level, rgba)) {
      errors_.Record(kGlInvalidOperation);
      return;
    }
    return;
  }
  // STENCIL drawbuffer must be 0; no stencil store: no-op when absent.
  if (drawbuffer != 0) {
    errors_.Record(kGlInvalidValue);
    return;
  }
}

void FramebufferManager::ClearBufferuiv(GLenum buffer, GLint drawbuffer,
                                        const GLuint* value) {
  static constexpr GLenum kColor = 0x1800u;
  if (buffer != kColor) {
    errors_.Record(kGlInvalidEnum);
    return;
  }
  if (value == nullptr) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  if (drawbuffer < 0 || drawbuffer >= kMaxDrawBuffers) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  if (drawbuffer != 0) {
    errors_.Record(kGlInvalidEnum);
    return;
  }
  if (bound_draw_ == 0) {
    errors_.Record(kGlInvalidOperation);
    return;
  }
  if (CheckFramebufferStatus(kGlDrawFramebuffer) != kGlFramebufferComplete) {
    errors_.Record(kGlInvalidFramebufferOperation);
    return;
  }
  const Attachment att = AttachmentState(bound_draw_, kGlColorAttachment0);
  if (!att.present || !att.is_texture) {
    errors_.Record(kGlInvalidOperation);
    return;
  }
  std::uint8_t rgba[4];
  for (int i = 0; i < 4; ++i) {
    rgba[i] = static_cast<std::uint8_t>(value[i] > 255u ? 255u : value[i]);
  }
  if (textures_ == nullptr ||
      !textures_->FillLevel(att.name, att.textarget, att.level, rgba)) {
    errors_.Record(kGlInvalidOperation);
  }
}

void FramebufferManager::GetFramebufferAttachmentParameteriv(GLenum target,
                                                             GLenum attachment,
                                                             GLenum pname,
                                                             GLint* params) {
  if (params == nullptr) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  const Framebuffer* fb = BoundForRead(target);
  if (fb == nullptr) return;
  if (!IsFramebufferAttachment(attachment)) {
    errors_.Record(kGlInvalidEnum);
    return;
  }
  auto it = fb->attachments.find(attachment);
  const bool present = it != fb->attachments.end() && it->second.present;
  switch (pname) {
    case kGlFramebufferAttachmentObjectType:
      if (!present) {
        *params = static_cast<GLint>(kGlNoneDraw);
        return;
      }
      // GL_TEXTURE (0x1702) or GL_RENDERBUFFER.
      *params = it->second.is_texture ? 0x1702
                                      : static_cast<GLint>(kGlRenderbuffer);
      return;
    case kGlFramebufferAttachmentObjectName:
      *params = present ? static_cast<GLint>(it->second.name) : 0;
      return;
    case kGlFramebufferAttachmentTextureLevel:
      *params = present ? it->second.level : 0;
      return;
    case kGlFramebufferAttachmentTextureLayer:
      *params = present ? it->second.layer : 0;
      return;
    default:
      break;
  }
  errors_.Record(kGlInvalidEnum);
}

Attachment FramebufferManager::AttachmentState(GLuint framebuffer,
                                               GLenum attachment) const {
  auto it = framebuffers_.find(framebuffer);
  if (it == framebuffers_.end()) return Attachment();
  auto ait = it->second.attachments.find(attachment);
  return ait == it->second.attachments.end() ? Attachment() : ait->second;
}

GLuint FramebufferManager::BoundFramebuffer(GLenum target) const {
  if (target == kGlReadFramebuffer) return bound_read_;
  return bound_draw_;
}

void FramebufferManager::SetDefaultFramebufferSize(GLsizei width,
                                                   GLsizei height) {
  if (width <= 0 || height <= 0) return;
  default_fb_width_ = width;
  default_fb_height_ = height;
}

void FramebufferManager::DefaultFramebufferSize(GLsizei* width,
                                                GLsizei* height) const {
  if (width != nullptr) *width = default_fb_width_;
  if (height != nullptr) *height = default_fb_height_;
}

bool FramebufferManager::ConsumeWindowClear() {
  const bool was = window_clear_pending_;
  window_clear_pending_ = false;
  return was;
}

void FramebufferManager::MarkWindowClear() { window_clear_pending_ = true; }

void FramebufferManager::GetWindowClearColor(GLfloat out[4]) const {
  if (out == nullptr) return;
  for (int i = 0; i < 4; ++i) out[i] = window_clear_color_[i];
}

std::vector<GLenum> FramebufferManager::DrawBufferList(
    GLuint framebuffer) const {
  auto it = framebuffers_.find(framebuffer);
  if (it == framebuffers_.end() || !it->second.alive)
    return {kGlColorAttachment0};
  return it->second.draw_buffers;
}

}  // namespace tgles
