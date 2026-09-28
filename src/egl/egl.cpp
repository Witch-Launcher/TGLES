#include "tgles/egl/egl.h"

#include <chrono>
#include <cstring>
#include <pthread.h>

#include "tgles/base/debug_log.h"
#include "tgles/host/entry_points.h"

namespace tgles {

namespace {

// Vendor/name strings reported by eglQueryString. Kept here (not in the
// header) so the ABI file and the tests agree on one source of truth.
constexpr const char* kEglVendorName = "TGLES";

// Extensions TGL actually implements, advertised honestly:
//  - EGL_EXT_platform_base / EGL_EXT_platform_device: eglGetPlatformDisplay
//    plus the platform window/pixmap surface entry points.
//  - EGL_KHR_surfaceless_context: eglMakeCurrent accepts EGL_NO_SURFACE with a
//    real context. This is the path the dEQP surfaceless platform and
//    MobileGL's headless mode rely on.
//  - EGL_KHR_fence_sync: eglCreateSync/ClientWaitSync/GetSyncAttrib, backed by
//    the same frame serial the Metal bridge reports on eglSwapBuffers.
//  - EGL_EXT_surface_swap_buffers_with_damage: eglSwapBuffersWithDamageEXT
//    (rects are advisory; the present path is unchanged).
// Deliberately absent: EGL_KHR_image / _base (no glEGLImageTargetTexture2DOES
// in the host contract, so an image could never reach a texture),
// EGL_KHR_lock_surface, EGL_KHR_swap_buffers_with_damage (KHR spelling),
// EGL_ANDROID_* (no Android native window interop yet).
constexpr const char* kClientExtensions =
    "EGL_EXT_platform_base "
    "EGL_EXT_platform_device "
    "EGL_KHR_surfaceless_context "
    "EGL_KHR_fence_sync "
    "EGL_EXT_surface_swap_buffers_with_damage";

constexpr const char* kDisplayExtensions =
    "EGL_EXT_platform_base "
    "EGL_EXT_platform_device "
    "EGL_KHR_surfaceless_context "
    "EGL_KHR_fence_sync "
    "EGL_EXT_surface_swap_buffers_with_damage";

}  // namespace

EglState::EglState() = default;

void EglState::RecordError(EGLint code) {
  errors_.Record(static_cast<GLenum>(code));
}

EGLint EglState::GetError() {
  // EGL_SUCCESS is 0x3000 (not zero): an empty latch reads as success.
  if (!errors_.HasPending()) return kEglSuccess;
  return static_cast<EGLint>(errors_.Get());
}

bool EglState::ValidDisplay(EGLDisplay dpy) const {
  auto it = displays_.find(dpy);
  return dpy != kEglNoDisplay && it != displays_.end() && it->second.alive;
}

bool EglState::InitializedDisplay(EGLDisplay dpy) {
  if (!ValidDisplay(dpy)) {
    errors_.Record(static_cast<GLenum>(kEglBadDisplay));
    return false;
  }
  if (!displays_[dpy].initialized) {
    errors_.Record(static_cast<GLenum>(kEglNotInitialized));
    return false;
  }
  return true;
}

bool EglState::ValidConfig(EGLDisplay dpy, EGLConfig config) const {
  if (!ValidDisplay(dpy)) return false;
  auto it = configs_.find(dpy);
  if (it == configs_.end()) return false;
  for (const Config& c : it->second) {
    if (c.id == config) return true;
  }
  return false;
}

bool EglState::ParseAttribs(const EGLint* attribs,
                            std::map<EGLint, EGLint>* out) {
  if (attribs == nullptr) return true;
  for (int i = 0; attribs[i] != kEglNone; ++i) {
    const EGLint name = attribs[i];
    ++i;
    (*out)[name] = attribs[i];
    if (i > 64) return false;  // Unterminated list guard.
  }
  return true;
}

EGLDisplay EglState::GetDisplay(void* /*native_display*/) {
  // One display per call site model is overkill on the host; every distinct
  // call gets a fresh uninitialized display (callers Initialize what they
  // use). All displays share the same config table shape.
  const EGLDisplay dpy = next_display_++;
  displays_[dpy] = Display();
  displays_[dpy].alive = true;
  Config full;
  full.id = next_config_++;
  Config no_msaa = full;
  no_msaa.id = next_config_++;
  no_msaa.samples = 0;
  Config rgb565 = full;
  rgb565.id = next_config_++;
  rgb565.red = 5;
  rgb565.green = 6;
  rgb565.blue = 5;
  rgb565.alpha = 0;
  rgb565.samples = 0;
  configs_[dpy] = {full, no_msaa, rgb565};
  return dpy;
}

EGLBoolean EglState::Initialize(EGLDisplay dpy, EGLint* major, EGLint* minor) {
  if (!ValidDisplay(dpy)) {
    errors_.Record(static_cast<GLenum>(kEglBadDisplay));
    return kEglFalse;
  }
  displays_[dpy].initialized = true;
  if (major != nullptr) *major = 1;
  if (minor != nullptr) *minor = 5;
  return kEglTrue;
}

EGLBoolean EglState::Terminate(EGLDisplay dpy) {
  if (!ValidDisplay(dpy)) {
    errors_.Record(static_cast<GLenum>(kEglBadDisplay));
    return kEglFalse;
  }
  displays_[dpy].initialized = false;
  if (current_display_ == dpy) {
    current_display_ = kEglNoDisplay;
    current_context_ = kEglNoContext;
    current_draw_ = kEglNoSurface;
    current_read_ = kEglNoSurface;
  }
  return kEglTrue;
}

const char* EglState::QueryString(EGLDisplay dpy, EGLint name) {
  // EGL 1.5 spec 3.7.1: with EGL_NO_DISPLAY only EGL_EXTENSIONS is legal and
  // returns the *client* extension string (Mesa and ANGLE behave the same).
  // Apps and CTS probe this before any display exists, so returning
  // BAD_DISPLAY here would misreport the driver.
  if (dpy == kEglNoDisplay) {
    if (name != kEglExtensions) {
      errors_.Record(static_cast<GLenum>(kEglBadDisplay));
      return nullptr;
    }
    strings_ = kClientExtensions;
    return strings_.c_str();
  }
  if (!ValidDisplay(dpy)) {
    errors_.Record(static_cast<GLenum>(kEglBadDisplay));
    return nullptr;
  }
  if (!displays_[dpy].initialized) {
    errors_.Record(static_cast<GLenum>(kEglNotInitialized));
    return nullptr;
  }
  switch (name) {
    case kEglVersion:
      strings_ = "1.5 TGL (OpenGL ES 3.2 host)";
      return strings_.c_str();
    case kEglVendor:
      strings_ = kEglVendorName;
      return strings_.c_str();
    case kEglClientApis:
      // TGL hosts only the OpenGL ES client API (BindApi rejects the rest).
      strings_ = "OpenGL_ES";
      return strings_.c_str();
    case kEglExtensions:
      strings_ = kDisplayExtensions;
      return strings_.c_str();
    default:
      break;
  }
  errors_.Record(static_cast<GLenum>(kEglBadParameter));
  return nullptr;
}

EGLBoolean EglState::BindApi(EGLenum api) {
  if (api != kEglOpenGlEsApi) {
    // Desktop GL and VG are not hosted (spec: BAD_PARAMETER).
    errors_.Record(static_cast<GLenum>(kEglBadParameter));
    return kEglFalse;
  }
  bound_api_ = api;
  return kEglTrue;
}

EGLenum EglState::QueryApi() const { return bound_api_; }

EGLBoolean EglState::ChooseConfig(EGLDisplay dpy, const EGLint* attribs,
                                  EGLConfig* configs, EGLint config_size,
                                  EGLint* num_config) {
  if (!InitializedDisplay(dpy)) {
    // BAD_DISPLAY or NOT_INITIALIZED already recorded above.
    return kEglFalse;
  }
  std::map<EGLint, EGLint> wanted;
  if (!ParseAttribs(attribs, &wanted)) {
    errors_.Record(static_cast<GLenum>(kEglBadAttribute));
    return kEglFalse;
  }
  for (const auto& kv : wanted) {
    switch (kv.first) {
      case kEglRedSize:
      case kEglGreenSize:
      case kEglBlueSize:
      case kEglAlphaSize:
      case kEglDepthSize:
      case kEglStencilSize:
      case kEglSamples:
      case kEglSampleBuffers:
      case kEglSurfaceType:
      case kEglRenderableType:
      case kEglConfigId:
        break;
      default:
        errors_.Record(static_cast<GLenum>(kEglBadAttribute));
        return kEglFalse;
    }
  }
  std::vector<EGLConfig> matched;
  for (const Config& c : configs_[dpy]) {
    bool ok = true;
    for (const auto& kv : wanted) {
      switch (kv.first) {
        case kEglRedSize:
          ok = ok && c.red >= kv.second;
          break;
        case kEglGreenSize:
          ok = ok && c.green >= kv.second;
          break;
        case kEglBlueSize:
          ok = ok && c.blue >= kv.second;
          break;
        case kEglAlphaSize:
          ok = ok && c.alpha >= kv.second;
          break;
        case kEglDepthSize:
          ok = ok && c.depth >= kv.second;
          break;
        case kEglStencilSize:
          ok = ok && c.stencil >= kv.second;
          break;
        case kEglSamples:
          ok = ok && c.samples >= kv.second;
          break;
        case kEglSampleBuffers:
          ok = ok && (kv.second == 0 || c.samples > 0);
          break;
        case kEglSurfaceType:
          ok = ok && ((c.surface_type & kv.second) == kv.second);
          break;
        case kEglRenderableType:
          ok = ok && ((c.renderable & kv.second) == kv.second);
          break;
        case kEglConfigId:
          ok = ok && (c.id == static_cast<EGLConfig>(kv.second));
          break;
        default:
          break;
      }
      if (!ok) break;
    }
    if (ok) matched.push_back(c.id);
  }
  if (num_config != nullptr) {
    *num_config = static_cast<EGLint>(matched.size());
  }
  if (configs != nullptr && config_size > 0) {
    for (EGLint i = 0; i < config_size && i < (EGLint)matched.size(); ++i) {
      configs[i] = matched[(std::size_t)i];
    }
  }
  return kEglTrue;
}

EGLBoolean EglState::GetConfigAttrib(EGLDisplay dpy, EGLConfig config,
                                     EGLint attribute, EGLint* value) {
  if (!InitializedDisplay(dpy)) return kEglFalse;
  if (value == nullptr) {
    errors_.Record(static_cast<GLenum>(kEglBadParameter));
    return kEglFalse;
  }
  for (const Config& c : configs_[dpy]) {
    if (c.id != config) continue;
    switch (attribute) {
      case kEglRedSize:
        *value = c.red;
        return kEglTrue;
      case kEglGreenSize:
        *value = c.green;
        return kEglTrue;
      case kEglBlueSize:
        *value = c.blue;
        return kEglTrue;
      case kEglAlphaSize:
        *value = c.alpha;
        return kEglTrue;
      case kEglDepthSize:
        *value = c.depth;
        return kEglTrue;
      case kEglStencilSize:
        *value = c.stencil;
        return kEglTrue;
      case kEglSamples:
        *value = c.samples;
        return kEglTrue;
      case kEglSurfaceType:
        *value = c.surface_type;
        return kEglTrue;
      case kEglRenderableType:
        *value = c.renderable;
        return kEglTrue;
      case kEglConfigId:
        *value = static_cast<EGLint>(c.id);
        return kEglTrue;
      default:
        break;
    }
    errors_.Record(static_cast<GLenum>(kEglBadAttribute));
    return kEglFalse;
  }
  errors_.Record(static_cast<GLenum>(kEglBadConfig));
  return kEglFalse;
}

EGLContext EglState::CreateContext(EGLDisplay dpy, EGLConfig config,
                                   EGLContext share_context,
                                   const EGLint* attribs) {
  if (!InitializedDisplay(dpy)) return kEglNoContext;
  if (!ValidConfig(dpy, config)) {
    errors_.Record(static_cast<GLenum>(kEglBadConfig));
    return kEglNoContext;
  }
  if (share_context != kEglNoContext &&
      contexts_.find(share_context) == contexts_.end()) {
    errors_.Record(static_cast<GLenum>(kEglBadContext));
    return kEglNoContext;
  }
  std::map<EGLint, EGLint> want;
  if (!ParseAttribs(attribs, &want)) {
    errors_.Record(static_cast<GLenum>(kEglBadAttribute));
    return kEglNoContext;
  }
  EGLint major = 1, minor = 0;  // Spec 3.7.1 defaults.
  for (const auto& kv : want) {
    if (kv.first == kEglContextMajorVersion ||
        kv.first == kEglContextClientVersion) {
      major = kv.second;
    } else if (kv.first == kEglContextMinorVersion) {
      minor = kv.second;
    } else {
      errors_.Record(static_cast<GLenum>(kEglBadAttribute));
      return kEglNoContext;
    }
  }
  // TGL hosts ES 3.2 only: 2.0-3.1 ride along (backwards compatible),
  // anything else cannot be satisfied.
  const bool ok = (major == 2 && minor == 0) ||
                  (major == 3 && (minor == 0 || minor == 1 || minor == 2));
  if (!ok) {
    errors_.Record(static_cast<GLenum>(kEglBadMatch));
    return kEglNoContext;
  }
  const EGLContext ctx = next_context_++;
  ContextRec rec;
  rec.alive = true;
  rec.config = config;
  rec.major = 3;
  rec.minor = 2;
  contexts_[ctx] = rec;
  return ctx;
}

EGLBoolean EglState::DestroyContext(EGLDisplay dpy, EGLContext ctx) {
  if (!ValidDisplay(dpy)) {
    errors_.Record(static_cast<GLenum>(kEglBadDisplay));
    return kEglFalse;
  }
  if (ctx == kEglNoContext) return kEglTrue;  // Silently ignored.
  auto it = contexts_.find(ctx);
  if (it == contexts_.end()) {
    errors_.Record(static_cast<GLenum>(kEglBadContext));
    return kEglFalse;
  }
  contexts_.erase(it);
  if (current_context_ == ctx) {
    current_context_ = kEglNoContext;
    current_draw_ = kEglNoSurface;
    current_read_ = kEglNoSurface;
  }
  return kEglTrue;
}

EGLBoolean EglState::QueryContext(EGLDisplay dpy, EGLContext ctx,
                                  EGLint attribute, EGLint* value) {
  if (!InitializedDisplay(dpy)) return kEglFalse;
  if (value == nullptr) {
    errors_.Record(static_cast<GLenum>(kEglBadParameter));
    return kEglFalse;
  }
  auto it = contexts_.find(ctx);
  if (it == contexts_.end()) {
    errors_.Record(static_cast<GLenum>(kEglBadContext));
    return kEglFalse;
  }
  switch (attribute) {
    case kEglContextMajorVersion:  // Includes CLIENT_VERSION alias (0x3098).
      *value = it->second.major;
      return kEglTrue;
    case kEglContextMinorVersion:
      *value = it->second.minor;
      return kEglTrue;
    case kEglConfigId: {
      for (const Config& c : configs_[dpy]) {
        if (c.id == it->second.config) {
          *value = static_cast<EGLint>(c.id);
          return kEglTrue;
        }
      }
      errors_.Record(static_cast<GLenum>(kEglBadContext));
      return kEglFalse;
    }
    default:
      break;
  }
  errors_.Record(static_cast<GLenum>(kEglBadAttribute));
  return kEglFalse;
}

EGLSurface EglState::CreateWindowSurface(EGLDisplay dpy, EGLConfig config,
                                         void* native_window,
                                         const EGLint* attribs) {
  if (!InitializedDisplay(dpy)) return kEglNoSurface;
  if (!ValidConfig(dpy, config)) {
    errors_.Record(static_cast<GLenum>(kEglBadConfig));
    return kEglNoSurface;
  }
  if (native_window == nullptr) {
    errors_.Record(static_cast<GLenum>(kEglBadNativeWindow));
    return kEglNoSurface;
  }
  std::map<EGLint, EGLint> want;
  if (!ParseAttribs(attribs, &want)) {
    errors_.Record(static_cast<GLenum>(kEglBadAttribute));
    return kEglNoSurface;
  }
  if (!want.empty()) {
    // TGL window surfaces take no attributes; anything present is foreign.
    errors_.Record(static_cast<GLenum>(kEglBadAttribute));
    return kEglNoSurface;
  }
  const EGLSurface s = next_surface_++;
  Surface surf;
  surf.alive = true;
  surf.is_window = true;
  surf.config = config;
  // If the host already attached/resized the Metal layer, stamp that size
  // onto surfaces created later (attach-before-create order).
  surf.width = last_window_width_;
  surf.height = last_window_height_;
  surfaces_[s] = surf;
  return s;
}

EGLSurface EglState::CreatePbufferSurface(EGLDisplay dpy, EGLConfig config,
                                          const EGLint* attribs) {
  if (!InitializedDisplay(dpy)) return kEglNoSurface;
  if (!ValidConfig(dpy, config)) {
    errors_.Record(static_cast<GLenum>(kEglBadConfig));
    return kEglNoSurface;
  }
  std::map<EGLint, EGLint> want;
  if (!ParseAttribs(attribs, &want)) {
    errors_.Record(static_cast<GLenum>(kEglBadAttribute));
    return kEglNoSurface;
  }
  EGLint width = 0, height = 0;
  for (const auto& kv : want) {
    if (kv.first == kEglWidth) {
      width = kv.second;
    } else if (kv.first == kEglHeight) {
      height = kv.second;
    } else {
      errors_.Record(static_cast<GLenum>(kEglBadAttribute));
      return kEglNoSurface;
    }
  }
  if (width <= 0 || height <= 0) {
    errors_.Record(static_cast<GLenum>(kEglBadMatch));  // Zero-area drawable.
    return kEglNoSurface;
  }
  const EGLSurface s = next_surface_++;
  Surface surf;
  surf.alive = true;
  surf.is_window = false;
  surf.config = config;
  surf.width = width;
  surf.height = height;
  surfaces_[s] = surf;
  return s;
}

EGLBoolean EglState::DestroySurface(EGLDisplay dpy, EGLSurface surface) {
  if (!ValidDisplay(dpy)) {
    errors_.Record(static_cast<GLenum>(kEglBadDisplay));
    return kEglFalse;
  }
  if (surface == kEglNoSurface) return kEglTrue;
  auto it = surfaces_.find(surface);
  if (it == surfaces_.end()) {
    errors_.Record(static_cast<GLenum>(kEglBadSurface));
    return kEglFalse;
  }
  surfaces_.erase(it);
  return kEglTrue;
}

EGLBoolean EglState::QuerySurface(EGLDisplay dpy, EGLSurface surface,
                                  EGLint attribute, EGLint* value) {
  if (!InitializedDisplay(dpy)) return kEglFalse;
  if (value == nullptr) {
    errors_.Record(static_cast<GLenum>(kEglBadParameter));
    return kEglFalse;
  }
  auto it = surfaces_.find(surface);
  if (it == surfaces_.end()) {
    errors_.Record(static_cast<GLenum>(kEglBadSurface));
    return kEglFalse;
  }
  switch (attribute) {
    case kEglWidth:
    case kEglHeight: {
      // MC sizes its window from eglQuerySurface (and mirrors it through
      // tglHostResizeMetalLayer); a stale 0x0 here propagates into the
      // viewport. Log the pair on change so the EGL-visible size can be
      // compared with the Metal drawable size in the same log.
      static int size_logged = 0;
      static EGLint lw = -1, lh = -1;
      const EGLint w = it->second.width;
      const EGLint h = it->second.height;
      if (w != lw || h != lh) {
        lw = w;
        lh = h;
        if (size_logged < 12) {
          ++size_logged;
          TglDebugf("diag eglsize#%d %dx%d attr=0x%x t=%lld", size_logged, w,
                    h, attribute, TglNowMs());
        }
      }
      *value = (attribute == kEglWidth) ? w : h;
      return kEglTrue;
    }
    case kEglConfigId:
      *value = static_cast<EGLint>(it->second.config);
      return kEglTrue;
    default:
      break;
  }
  errors_.Record(static_cast<GLenum>(kEglBadAttribute));
  return kEglFalse;
}

void EglState::SetWindowSurfaceSize(EGLint width, EGLint height) {
  if (width <= 0 || height <= 0) return;
  last_window_width_ = width;
  last_window_height_ = height;
  for (auto& kv : surfaces_) {
    if (kv.second.alive && kv.second.is_window) {
      kv.second.width = width;
      kv.second.height = height;
    }
  }
}

EGLBoolean EglState::MakeCurrent(EGLDisplay dpy, EGLSurface draw,
                                 EGLSurface read, EGLContext ctx) {
  if (!InitializedDisplay(dpy)) return kEglFalse;
  if (ctx != kEglNoContext && contexts_.find(ctx) == contexts_.end()) {
    errors_.Record(static_cast<GLenum>(kEglBadContext));
    return kEglFalse;
  }
  const bool draw_no = (draw == kEglNoSurface);
  const bool read_no = (read == kEglNoSurface);
  if (!draw_no && surfaces_.find(draw) == surfaces_.end()) {
    errors_.Record(static_cast<GLenum>(kEglBadSurface));
    return kEglFalse;
  }
  if (!read_no && surfaces_.find(read) == surfaces_.end()) {
    errors_.Record(static_cast<GLenum>(kEglBadSurface));
    return kEglFalse;
  }
  if (ctx == kEglNoContext && (!draw_no || !read_no)) {
    errors_.Record(static_cast<GLenum>(kEglBadMatch));
    return kEglFalse;
  }
  if (draw_no != read_no) {
    errors_.Record(static_cast<GLenum>(kEglBadMatch));
    return kEglFalse;
  }
  current_display_ = (ctx == kEglNoContext) ? kEglNoDisplay : dpy;
  current_context_ = ctx;
  current_draw_ = draw;
  current_read_ = read;
  return kEglTrue;
}

EGLContext EglState::GetCurrentContext() const { return current_context_; }
EGLDisplay EglState::GetCurrentDisplay() const { return current_display_; }

EGLSurface EglState::GetCurrentSurface(EGLint readdraw) {
  if (readdraw != kEglDraw && readdraw != kEglRead) {
    errors_.Record(static_cast<GLenum>(kEglBadParameter));
    return kEglNoSurface;
  }
  if (current_display_ == kEglNoDisplay) return kEglNoSurface;
  return (readdraw == kEglDraw) ? current_draw_ : current_read_;
}

EGLBoolean EglState::GetConfigs(EGLDisplay dpy, EGLConfig* configs,
                                EGLint config_size, EGLint* num_config) {
  if (!InitializedDisplay(dpy)) return kEglFalse;
  const std::vector<Config>& list = configs_[dpy];
  if (num_config != nullptr) {
    *num_config = static_cast<EGLint>(list.size());
  }
  if (configs != nullptr && config_size > 0) {
    for (EGLint i = 0; i < config_size && i < (EGLint)list.size(); ++i) {
      configs[i] = list[(std::size_t)i].id;
    }
  }
  return kEglTrue;
}

EGLBoolean EglState::SurfaceAttrib(EGLDisplay dpy, EGLSurface surface,
                                   EGLint attribute, EGLint value) {
  if (!InitializedDisplay(dpy)) return kEglFalse;
  auto it = surfaces_.find(surface);
  if (it == surfaces_.end()) {
    errors_.Record(static_cast<GLenum>(kEglBadSurface));
    return kEglFalse;
  }
  switch (attribute) {
    case kEglMipmapLevel:
    case kEglMultisampleResolve:
    case kEglSwapBehavior:
      (void)value;
      return kEglTrue;
    default:
      break;
  }
  errors_.Record(static_cast<GLenum>(kEglBadAttribute));
  return kEglFalse;
}

EGLBoolean EglState::WaitClient() { return kEglTrue; }
EGLBoolean EglState::WaitGL() { return kEglTrue; }
EGLBoolean EglState::WaitNative(EGLint engine) {
  (void)engine;
  return kEglTrue;
}

EGLBoolean EglState::ReleaseThread() {
  current_display_ = kEglNoDisplay;
  current_context_ = kEglNoContext;
  current_draw_ = kEglNoSurface;
  current_read_ = kEglNoSurface;
  return kEglTrue;
}

EGLImage EglState::CreateImage(EGLDisplay dpy, EGLContext ctx, EGLenum target,
                               void* buffer, const EGLAttrib* attribs) {
  (void)attribs;
  if (!InitializedDisplay(dpy)) return kEglNoImage;
  if (ctx != kEglNoContext && contexts_.find(ctx) == contexts_.end()) {
    errors_.Record(static_cast<GLenum>(kEglBadContext));
    return kEglNoImage;
  }
  switch (target) {
    case kEglGlTexture2D:
    case kEglGlTexture3D:
    case kEglGlRenderbuffer:
      break;
    default:
      errors_.Record(static_cast<GLenum>(kEglBadParameter));
      return kEglNoImage;
  }
  const EGLImage img = next_image_++;
  ImageRec rec;
  rec.alive = true;
  rec.display = dpy;
  rec.context = ctx;
  rec.target = target;
  rec.buffer = buffer;
  images_[img] = rec;
  return img;
}

EGLBoolean EglState::DestroyImage(EGLDisplay dpy, EGLImage image) {
  if (!ValidDisplay(dpy)) {
    errors_.Record(static_cast<GLenum>(kEglBadDisplay));
    return kEglFalse;
  }
  auto it = images_.find(image);
  if (it == images_.end() || !it->second.alive) {
    errors_.Record(static_cast<GLenum>(kEglBadParameter));
    return kEglFalse;
  }
  if (it->second.display != dpy) {
    errors_.Record(static_cast<GLenum>(kEglBadMatch));
    return kEglFalse;
  }
  images_.erase(it);
  return kEglTrue;
}

EGLBoolean EglState::SwapBuffers(EGLDisplay dpy, EGLSurface surface) {
  if (!InitializedDisplay(dpy)) return kEglFalse;
  auto it = surfaces_.find(surface);
  if (it == surfaces_.end()) {
    errors_.Record(static_cast<GLenum>(kEglBadSurface));
    return kEglFalse;
  }
  if (it->second.is_window) {
    // Real present happens in the iOS bridge; the host records the swap.
    ++it->second.swaps;
    // Swap cadence: a frozen screen with a live render thread shows as
    // draws continuing while these stop (or the reverse). Bounded so a long
    // session cannot flood the 64KB diag ring.
    const int sw_now = it->second.swaps;
    if (sw_now <= 300 && (sw_now <= 5 || (sw_now % 10) == 0)) {
      TglDebugf("diag swap#%d tid=%p t=%lld", sw_now, (void*)pthread_self(),
                TglNowMs());
    }
    // Frame-gap timing: log slow frames (>=250ms) for the fps diagnosis.
    // Bounded: first 96 slow frames only, so a slow session cannot flood
    // the 64KB diag ring.
    using Clock = std::chrono::steady_clock;
    static Clock::time_point last_swap;
    static bool have_last = false;
    static int slow_seen = 0;
    const Clock::time_point now = Clock::now();
    if (have_last) {
      const long long ms =
          std::chrono::duration_cast<std::chrono::milliseconds>(now -
                                                                last_swap)
              .count();
      if (ms >= 250) {
        const int n = ++slow_seen;
        if (n <= 96) {
          TglDebugf("diag frame_gap#%d ms=%lld swap=%d t=%lld", n, ms,
                    it->second.swaps, TglNowMs());
        }
      }
    }
    last_swap = now;
    have_last = true;
  }
  return kEglTrue;  // Pbuffer/single-buffered: spec no-op, still success.
}

EGLBoolean EglState::SwapInterval(EGLDisplay dpy, EGLint interval) {
  if (!InitializedDisplay(dpy)) return kEglFalse;
  // Silently clamped into the implementation range (spec 3.9).
  if (interval < 0) interval = 0;
  if (interval > kEglMaxSwapInterval) interval = kEglMaxSwapInterval;
  swap_interval_ = interval;
  return kEglTrue;
}

EGLDisplay EglState::GetPlatformDisplay(EGLenum platform, void* native_display) {
  (void)platform;
  return GetDisplay(native_display);
}

EGLSurface EglState::CreatePixmapSurface(EGLDisplay dpy, EGLConfig config,
                                         void* native_pixmap,
                                         const EGLint* attribs) {
  if (!InitializedDisplay(dpy)) return kEglNoSurface;
  if (!ValidConfig(dpy, config)) {
    errors_.Record(static_cast<GLenum>(kEglBadConfig));
    return kEglNoSurface;
  }
  if (native_pixmap == nullptr) {
    errors_.Record(static_cast<GLenum>(kEglBadNativePixmap));
    return kEglNoSurface;
  }
  std::map<EGLint, EGLint> want;
  if (!ParseAttribs(attribs, &want)) {
    errors_.Record(static_cast<GLenum>(kEglBadAttribute));
    return kEglNoSurface;
  }
  if (!want.empty()) {
    errors_.Record(static_cast<GLenum>(kEglBadAttribute));
    return kEglNoSurface;
  }
  const EGLSurface s = next_surface_++;
  Surface surf;
  surf.alive = true;
  surf.is_window = false;
  surf.config = config;
  surf.width = 64;
  surf.height = 64;
  surfaces_[s] = surf;
  return s;
}

EGLSurface EglState::CreatePlatformWindowSurface(EGLDisplay dpy,
                                                 EGLConfig config,
                                                 void* native_window,
                                                 const EGLAttrib* attribs) {
  if (attribs != nullptr && attribs[0] != kEglNone) {
    if (!InitializedDisplay(dpy)) return kEglNoSurface;
    errors_.Record(static_cast<GLenum>(kEglBadAttribute));
    return kEglNoSurface;
  }
  return CreateWindowSurface(dpy, config, native_window, nullptr);
}

EGLSurface EglState::CreatePlatformPixmapSurface(EGLDisplay dpy,
                                                 EGLConfig config,
                                                 void* native_pixmap,
                                                 const EGLAttrib* attribs) {
  if (attribs != nullptr && attribs[0] != kEglNone) {
    if (!InitializedDisplay(dpy)) return kEglNoSurface;
    errors_.Record(static_cast<GLenum>(kEglBadAttribute));
    return kEglNoSurface;
  }
  return CreatePixmapSurface(dpy, config, native_pixmap, nullptr);
}

EGLSurface EglState::CreatePbufferFromClientBuffer(EGLDisplay dpy,
                                                   EGLenum buftype,
                                                   void* buffer,
                                                   EGLConfig config,
                                                   const EGLint* attribs) {
  (void)buftype;
  if (!InitializedDisplay(dpy)) return kEglNoSurface;
  if (!ValidConfig(dpy, config)) {
    errors_.Record(static_cast<GLenum>(kEglBadConfig));
    return kEglNoSurface;
  }
  if (buffer == nullptr) {
    errors_.Record(static_cast<GLenum>(kEglBadParameter));
    return kEglNoSurface;
  }
  return CreatePbufferSurface(dpy, config, attribs);
}

EGLBoolean EglState::BindTexImage(EGLDisplay dpy, EGLSurface surface,
                                  EGLint buffer) {
  (void)buffer;
  if (!InitializedDisplay(dpy)) return kEglFalse;
  if (surfaces_.find(surface) == surfaces_.end()) {
    errors_.Record(static_cast<GLenum>(kEglBadSurface));
    return kEglFalse;
  }
  return kEglTrue;
}

EGLBoolean EglState::ReleaseTexImage(EGLDisplay dpy, EGLSurface surface,
                                     EGLint buffer) {
  (void)buffer;
  if (!InitializedDisplay(dpy)) return kEglFalse;
  if (surfaces_.find(surface) == surfaces_.end()) {
    errors_.Record(static_cast<GLenum>(kEglBadSurface));
    return kEglFalse;
  }
  return kEglTrue;
}

EGLBoolean EglState::CopyBuffers(EGLDisplay dpy, EGLSurface surface,
                                 void* native_pixmap) {
  if (!InitializedDisplay(dpy)) return kEglFalse;
  if (surfaces_.find(surface) == surfaces_.end()) {
    errors_.Record(static_cast<GLenum>(kEglBadSurface));
    return kEglFalse;
  }
  if (native_pixmap == nullptr) {
    errors_.Record(static_cast<GLenum>(kEglBadNativePixmap));
    return kEglFalse;
  }
  return kEglTrue;
}

EGLBoolean EglState::SwapBuffersWithDamageEXT(EGLDisplay dpy,
                                              EGLSurface surface,
                                              const EGLint* rects,
                                              EGLint n_rects) {
  (void)rects;
  (void)n_rects;
  return SwapBuffers(dpy, surface);
}

EGLBoolean EglState::UnlockSurfaceKHR(EGLDisplay dpy, EGLSurface surface) {
  if (!InitializedDisplay(dpy)) return kEglFalse;
  if (surfaces_.find(surface) == surfaces_.end()) {
    errors_.Record(static_cast<GLenum>(kEglBadSurface));
    return kEglFalse;
  }
  return kEglTrue;
}

EGLSync EglState::CreateSync(EGLDisplay dpy, EGLenum type,
                               const EGLAttrib* attribs) {
  if (!InitializedDisplay(dpy)) return kEglNoSync;
  if (type != kEglSyncFence) {
    errors_.Record(static_cast<GLenum>(kEglBadAttribute));
    return kEglNoSync;
  }
  if (attribs != nullptr && attribs[0] != kEglNone) {
    errors_.Record(static_cast<GLenum>(kEglBadAttribute));
    return kEglNoSync;
  }
  const EGLSync sync = next_sync_++;
  syncs_[sync] = SyncRec();
  syncs_[sync].alive = true;
  return sync;
}

EGLBoolean EglState::DestroySync(EGLDisplay dpy, EGLSync sync) {
  if (!ValidDisplay(dpy)) {
    errors_.Record(static_cast<GLenum>(kEglBadDisplay));
    return kEglFalse;
  }
  if (sync == kEglNoSync) return kEglTrue;
  auto it = syncs_.find(sync);
  if (it == syncs_.end()) {
    errors_.Record(static_cast<GLenum>(kEglBadParameter));
    return kEglFalse;
  }
  syncs_.erase(it);
  return kEglTrue;
}

EGLint EglState::ClientWaitSync(EGLDisplay dpy, EGLSync sync, EGLint flags,
                                EGLTime timeout) {
  if (!InitializedDisplay(dpy)) return kEglFalse;
  auto it = syncs_.find(sync);
  if (it == syncs_.end()) {
    // Spec-verified: invalid EGL sync -> EGL_BAD_PARAMETER (unlike GL).
    errors_.Record(static_cast<GLenum>(kEglBadParameter));
    return kEglFalse;
  }
  if ((flags & ~kEglSyncFlushCommandsBit) != 0) {
    errors_.Record(static_cast<GLenum>(kEglBadParameter));
    return kEglFalse;
  }
  if (it->second.signaled) return kEglConditionSatisfied;
  if (timeout == 0) return kEglTimeoutExpired;
  // No GPU on the host; a finite wait expires. The bridge signals first.
  return kEglTimeoutExpired;
}

EGLBoolean EglState::WaitSync(EGLDisplay dpy, EGLSync sync, EGLint flags) {
  if (!InitializedDisplay(dpy)) return kEglFalse;
  if (syncs_.find(sync) == syncs_.end()) {
    errors_.Record(static_cast<GLenum>(kEglBadParameter));
    return kEglFalse;
  }
  if (flags != 0) {
    errors_.Record(static_cast<GLenum>(kEglBadParameter));
    return kEglFalse;
  }
  return kEglTrue;
}

EGLBoolean EglState::GetSyncAttrib(EGLDisplay dpy, EGLSync sync,
                                   EGLint attribute, EGLAttrib* value) {
  if (!InitializedDisplay(dpy)) return kEglFalse;
  auto it = syncs_.find(sync);
  if (it == syncs_.end()) {
    errors_.Record(static_cast<GLenum>(kEglBadParameter));
    return kEglFalse;
  }
  if (value == nullptr) {
    errors_.Record(static_cast<GLenum>(kEglBadParameter));
    return kEglFalse;
  }
  switch (attribute) {
    case kEglSyncType:
      *value = kEglSyncFence;
      return kEglTrue;
    case kEglSyncStatus:
      *value = it->second.signaled ? kEglSignaled : kEglUnsignaled;
      return kEglTrue;
    case kEglSyncCondition:
      *value = kEglSyncPriorCommandsComplete;
      return kEglTrue;
    default:
      break;
  }
  errors_.Record(static_cast<GLenum>(kEglBadAttribute));
  return kEglFalse;
}

void EglState::SignalSync(EGLSync sync) {
  auto it = syncs_.find(sync);
  if (it != syncs_.end()) it->second.signaled = true;
}

EGLint EglState::SwapCount(EGLSurface surface) const {
  auto it = surfaces_.find(surface);
  return it == surfaces_.end() ? -1 : it->second.swaps;
}

bool EglState::IsWindowSurface(EGLSurface surface) const {
  auto it = surfaces_.find(surface);
  return it != surfaces_.end() && it->second.alive && it->second.is_window;
}

EGLint EglState::SwapIntervalValue() const { return swap_interval_; }

}  // namespace tgles
