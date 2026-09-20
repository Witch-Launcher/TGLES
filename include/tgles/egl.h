#ifndef TGLES_EGL_H
#define TGLES_EGL_H

// EGL 1.5 platform layer (Khronos "Native Platform Graphics Interface",
// EGL 1.5 Aug 27 2014): displays, configs, contexts, surfaces, current
// state, swap control and procedure addresses. Pure C++ state machine —
// the CAMetalLayer present path belongs to the iOS bridge (documented gap).

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "tgles/error.h"
#include "tgles/gl_types.h"

namespace tgles {

// Opaque EGL handles (integer ids in the CPU model).
using EGLDisplay = std::uint64_t;
using EGLConfig = std::uint64_t;
using EGLSurface = std::uint64_t;
using EGLContext = std::uint64_t;
using EGLint = int;
using EGLenum = unsigned int;
using EGLBoolean = unsigned int;
using EGLNativeWindowType = void*;
using EGLSync = std::uint64_t;
using EGLTime = std::uint64_t;
using EGLAttrib = std::intptr_t;

inline constexpr EGLBoolean kEglTrue = 1;
inline constexpr EGLBoolean kEglFalse = 0;
inline constexpr EGLDisplay kEglNoDisplay = 0;
inline constexpr EGLContext kEglNoContext = 0;
inline constexpr EGLSurface kEglNoSurface = 0;

// Error codes (docs/reference/egl.h, spec 3.1).
inline constexpr EGLint kEglSuccess = 0x3000;
inline constexpr EGLint kEglNotInitialized = 0x3001;
inline constexpr EGLint kEglBadAccess = 0x3002;
inline constexpr EGLint kEglBadAlloc = 0x3003;
inline constexpr EGLint kEglBadAttribute = 0x3004;
inline constexpr EGLint kEglBadConfig = 0x3005;
inline constexpr EGLint kEglBadContext = 0x3006;
inline constexpr EGLint kEglBadCurrentSurface = 0x3007;
inline constexpr EGLint kEglBadDisplay = 0x3008;
inline constexpr EGLint kEglBadMatch = 0x3009;
inline constexpr EGLint kEglBadNativePixmap = 0x300A;
inline constexpr EGLint kEglBadNativeWindow = 0x300B;
inline constexpr EGLint kEglBadParameter = 0x300C;
inline constexpr EGLint kEglBadSurface = 0x300D;
inline constexpr EGLint kEglContextLost = 0x300E;

// Common attributes.
inline constexpr EGLint kEglNone = 0x3038;
inline constexpr EGLint kEglRedSize = 0x3024;
inline constexpr EGLint kEglGreenSize = 0x3023;
inline constexpr EGLint kEglBlueSize = 0x3022;
inline constexpr EGLint kEglAlphaSize = 0x3021;
inline constexpr EGLint kEglDepthSize = 0x3025;
inline constexpr EGLint kEglStencilSize = 0x3026;
inline constexpr EGLint kEglSamples = 0x3031;
inline constexpr EGLint kEglSampleBuffers = 0x3032;
inline constexpr EGLint kEglSurfaceType = 0x3033;
inline constexpr EGLint kEglRenderableType = 0x3040;
inline constexpr EGLint kEglConfigId = 0x3028;
inline constexpr EGLint kEglWidth = 0x3057;
inline constexpr EGLint kEglHeight = 0x3056;

// Bit masks.
inline constexpr EGLint kEglWindowBit = 0x0004;
inline constexpr EGLint kEglPbufferBit = 0x0001;
inline constexpr EGLint kEglOpenGlEsBit = 0x0001;
inline constexpr EGLint kEglOpenGlEs2Bit = 0x0004;
inline constexpr EGLint kEglOpenGlEs3Bit = 0x00000040;

// String names.
inline constexpr EGLint kEglVersion = 0x3054;
inline constexpr EGLint kEglVendor = 0x3053;
inline constexpr EGLint kEglClientApis = 0x308D;
inline constexpr EGLint kEglExtensions = 0x3055;

// Client APIs (TGL hosts EGL_OPENGL_ES_API only).
inline constexpr EGLenum kEglOpenGlEsApi = 0x30A0;
inline constexpr EGLenum kEglOpenGlApi = 0x30A2;
inline constexpr EGLenum kEglOpenVgApi = 0x30A1;

// Context attributes (CLIENT_VERSION is an alias of MAJOR, spec fn.18).
inline constexpr EGLint kEglContextMajorVersion = 0x3098;
inline constexpr EGLint kEglContextMinorVersion = 0x30FB;
inline constexpr EGLint kEglContextClientVersion = 0x3098;

// Swap interval clamp.
inline constexpr EGLint kEglMaxSwapInterval = 4;
inline constexpr EGLSync kEglNoSync = 0;
using EGLImage = std::uint64_t;
inline constexpr EGLImage kEglNoImage = 0;

// EGLImage targets (docs/reference/egl.h, EGL 1.5 core via KHR_image).
inline constexpr EGLenum kEglGlTexture2D = 0x30B1;
inline constexpr EGLenum kEglGlTexture3D = 0x30B2;
inline constexpr EGLenum kEglGlRenderbuffer = 0x30B9;
// Current-surface query names (EGL 1.5 spec 3.7.3).
inline constexpr EGLint kEglDraw = 0x3059;
inline constexpr EGLint kEglRead = 0x305A;
// SurfaceAttrib names (EGL 1.5 spec 3.5.x, CPU-model subset).
inline constexpr EGLint kEglMipmapLevel = 0x3083;
inline constexpr EGLint kEglMultisampleResolve = 0x3099;
inline constexpr EGLint kEglSwapBehavior = 0x3093;

// Fence-sync objects (EGL 1.5 core, values from docs/reference/egl.h).
inline constexpr EGLenum kEglSyncFence = 0x30F9;
inline constexpr EGLenum kEglSyncPriorCommandsComplete = 0x30F0;
inline constexpr EGLint kEglSyncType = 0x30F7;
inline constexpr EGLint kEglSyncStatus = 0x30F1;
inline constexpr EGLint kEglSyncCondition = 0x30F8;
inline constexpr EGLint kEglSignaled = 0x30F2;
inline constexpr EGLint kEglUnsignaled = 0x30F3;
inline constexpr EGLint kEglSyncFlushCommandsBit = 0x0001;
inline constexpr EGLTime kEglForever = 0xFFFFFFFFFFFFFFFFull;
inline constexpr EGLint kEglTimeoutExpired = 0x30F5;
inline constexpr EGLint kEglConditionSatisfied = 0x30F6;

class EglState {
 public:
  EglState();

  EGLint GetError();

  EGLDisplay GetDisplay(void* native_display);
  EGLBoolean Initialize(EGLDisplay dpy, EGLint* major, EGLint* minor);
  EGLBoolean Terminate(EGLDisplay dpy);
  const char* QueryString(EGLDisplay dpy, EGLint name);

  EGLBoolean BindApi(EGLenum api);
  EGLenum QueryApi() const;

  EGLBoolean ChooseConfig(EGLDisplay dpy, const EGLint* attribs,
                          EGLConfig* configs, EGLint config_size,
                          EGLint* num_config);
  EGLBoolean GetConfigAttrib(EGLDisplay dpy, EGLConfig config,
                             EGLint attribute, EGLint* value);

  EGLContext CreateContext(EGLDisplay dpy, EGLConfig config,
                           EGLContext share_context,
                           const EGLint* attribs);
  EGLBoolean DestroyContext(EGLDisplay dpy, EGLContext ctx);
  EGLBoolean QueryContext(EGLDisplay dpy, EGLContext ctx, EGLint attribute,
                          EGLint* value);

  EGLSurface CreateWindowSurface(EGLDisplay dpy, EGLConfig config,
                                 void* native_window,
                                 const EGLint* attribs);
  EGLSurface CreatePbufferSurface(EGLDisplay dpy, EGLConfig config,
                                  const EGLint* attribs);
  EGLBoolean DestroySurface(EGLDisplay dpy, EGLSurface surface);
  EGLBoolean QuerySurface(EGLDisplay dpy, EGLSurface surface,
                          EGLint attribute, EGLint* value);

  EGLBoolean MakeCurrent(EGLDisplay dpy, EGLSurface draw, EGLSurface read,
                         EGLContext ctx);
  EGLContext GetCurrentContext() const;
  EGLDisplay GetCurrentDisplay() const;
  EGLSurface GetCurrentSurface(EGLint readdraw);

  EGLBoolean SwapBuffers(EGLDisplay dpy, EGLSurface surface);
  EGLBoolean SwapInterval(EGLDisplay dpy, EGLint interval);
  EGLBoolean SurfaceAttrib(EGLDisplay dpy, EGLSurface surface,
                           EGLint attribute, EGLint value);

  // Display-wide queries and thread coordination (EGL 1.5 core, all REQUIRED
  // by MobileGL loader INIT_EGL_FUNC; CPU model returns success no-ops).
  EGLBoolean GetConfigs(EGLDisplay dpy, EGLConfig* configs,
                        EGLint config_size, EGLint* num_config);
  EGLBoolean WaitClient();
  EGLBoolean WaitGL();
  EGLBoolean WaitNative(EGLint engine);
  EGLBoolean ReleaseThread();

  EGLImage CreateImage(EGLDisplay dpy, EGLContext ctx, EGLenum target,
                       void* buffer, const EGLAttrib* attribs);
  EGLBoolean DestroyImage(EGLDisplay dpy, EGLImage image);

  // Remaining EGL 1.5 + platform/EXT/KHR surface paths (all REQUIRED by
  // MobileGL loader; CPU-model stubs with spec-correct errors).
  EGLDisplay GetPlatformDisplay(EGLenum platform, void* native_display);
  EGLSurface CreatePixmapSurface(EGLDisplay dpy, EGLConfig config,
                                 void* native_pixmap, const EGLint* attribs);
  EGLSurface CreatePlatformWindowSurface(EGLDisplay dpy, EGLConfig config,
                                         void* native_window,
                                         const EGLAttrib* attribs);
  EGLSurface CreatePlatformPixmapSurface(EGLDisplay dpy, EGLConfig config,
                                         void* native_pixmap,
                                         const EGLAttrib* attribs);
  EGLSurface CreatePbufferFromClientBuffer(EGLDisplay dpy, EGLenum buftype,
                                           void* buffer, EGLConfig config,
                                           const EGLint* attribs);
  EGLBoolean BindTexImage(EGLDisplay dpy, EGLSurface surface, EGLint buffer);
  EGLBoolean ReleaseTexImage(EGLDisplay dpy, EGLSurface surface, EGLint buffer);
  EGLBoolean CopyBuffers(EGLDisplay dpy, EGLSurface surface,
                         void* native_pixmap);
  EGLBoolean SwapBuffersWithDamageEXT(EGLDisplay dpy, EGLSurface surface,
                                      const EGLint* rects, EGLint n_rects);
  EGLBoolean UnlockSurfaceKHR(EGLDisplay dpy, EGLSurface surface);

  EGLSync CreateSync(EGLDisplay dpy, EGLenum type, const EGLAttrib* attribs);
  EGLBoolean DestroySync(EGLDisplay dpy, EGLSync sync);
  EGLint ClientWaitSync(EGLDisplay dpy, EGLSync sync, EGLint flags,
                        EGLTime timeout);
  EGLBoolean WaitSync(EGLDisplay dpy, EGLSync sync, EGLint flags);
  EGLBoolean GetSyncAttrib(EGLDisplay dpy, EGLSync sync, EGLint attribute,
                           EGLAttrib* value);

  void* GetProcAddress(const char* procname);

  // Test helpers.
  EGLint SwapCount(EGLSurface surface) const;
  EGLint SwapIntervalValue() const;
  void SignalSync(EGLSync sync);

 private:
  struct Config {
    EGLConfig id = 0;
    EGLint red = 8, green = 8, blue = 8, alpha = 8;
    EGLint depth = 24, stencil = 8, samples = 4;
    EGLint surface_type = kEglWindowBit | kEglPbufferBit;
    EGLint renderable = kEglOpenGlEsBit | kEglOpenGlEs2Bit | kEglOpenGlEs3Bit;
  };
  struct Surface {
    bool alive = false;
    bool is_window = false;
    EGLConfig config = 0;
    EGLint width = 0, height = 0;
    EGLint swaps = 0;
  };
  struct ContextRec {
    bool alive = false;
    EGLConfig config = 0;
    EGLint major = 3, minor = 2;
  };
  struct SyncRec {
    bool alive = false;
    bool signaled = false;
  };
  struct ImageRec {
    bool alive = false;
    EGLDisplay display = kEglNoDisplay;
    EGLContext context = kEglNoContext;
    EGLenum target = 0;
    void* buffer = nullptr;
  };
  struct Display {
    bool alive = false;
    bool initialized = false;
  };

  bool ValidDisplay(EGLDisplay dpy) const;
  bool InitializedDisplay(EGLDisplay dpy);
  bool ValidConfig(EGLDisplay dpy, EGLConfig config) const;
  static bool ParseAttribs(const EGLint* attribs,
                           std::map<EGLint, EGLint>* out);

  ErrorQueue errors_;
  EGLenum bound_api_ = kEglOpenGlEsApi;  // Spec initial value.
  EGLDisplay next_display_ = 1;
  EGLConfig next_config_ = 1;
  EGLSurface next_surface_ = 1;
  EGLContext next_context_ = 1;
  EGLSync next_sync_ = 1;
  EGLImage next_image_ = 1;
  std::map<EGLDisplay, Display> displays_;
  std::map<EGLDisplay, std::vector<Config>> configs_;
  std::map<EGLSurface, Surface> surfaces_;
  std::map<EGLContext, ContextRec> contexts_;
  std::map<EGLSync, SyncRec> syncs_;
  std::map<EGLImage, ImageRec> images_;
  EGLDisplay current_display_ = kEglNoDisplay;
  EGLContext current_context_ = kEglNoContext;
  EGLSurface current_draw_ = kEglNoSurface;
  EGLSurface current_read_ = kEglNoSurface;
  EGLint swap_interval_ = 1;
  std::string strings_;
};

}  // namespace tgles

#endif  // TGLES_EGL_H