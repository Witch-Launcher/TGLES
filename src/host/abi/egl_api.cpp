// EGL 1.5 host ABI: the 38 entry points not covered by the trial subset in
// src/host/host_c_api.cpp (which owns GetDisplay/Initialize/Terminate/
// ChooseConfig/CreateContext/CreatePbufferSurface/DestroySurface/MakeCurrent/
// SwapBuffers/GetError/QueryString).
//
// Each wrapper converts the C ABI (opaque void* handles) to the C++ EglState
// owned by HostRuntime. No logic lives here: validation and the CPU model are
// EglState's job (see include/tgles/egl/egl.h). EXT/KHR aliases forward to the
// same core implementation; unknown platform tokens record an EGL error via
// EglState::RecordError instead of crashing the host.

#include "tgles/host/abi.h"

#include <vector>

#include "tgles/host/host_runtime.h"

namespace {

tgles::HostRuntime& Rt() { return tgles::HostRuntime::Instance(); }

tgles::EGLDisplay FromDisplay(EGLDisplay dpy) {
  return static_cast<tgles::EGLDisplay>(reinterpret_cast<uintptr_t>(dpy));
}
EGLDisplay ToDisplay(tgles::EGLDisplay id) {
  return reinterpret_cast<EGLDisplay>(static_cast<uintptr_t>(id));
}
tgles::EGLConfig FromConfig(EGLConfig cfg) {
  return static_cast<tgles::EGLConfig>(reinterpret_cast<uintptr_t>(cfg));
}
EGLConfig ToConfig(tgles::EGLConfig id) {
  return reinterpret_cast<EGLConfig>(static_cast<uintptr_t>(id));
}
tgles::EGLSurface FromSurface(EGLSurface s) {
  return static_cast<tgles::EGLSurface>(reinterpret_cast<uintptr_t>(s));
}
EGLSurface ToSurface(tgles::EGLSurface id) {
  return reinterpret_cast<EGLSurface>(static_cast<uintptr_t>(id));
}
tgles::EGLContext FromContext(EGLContext c) {
  return static_cast<tgles::EGLContext>(reinterpret_cast<uintptr_t>(c));
}
EGLContext ToContext(tgles::EGLContext id) {
  return reinterpret_cast<EGLContext>(static_cast<uintptr_t>(id));
}
tgles::EGLImage FromImage(EGLImage img) {
  return static_cast<tgles::EGLImage>(reinterpret_cast<uintptr_t>(img));
}
EGLImage ToImage(tgles::EGLImage id) {
  return reinterpret_cast<EGLImage>(static_cast<uintptr_t>(id));
}
tgles::EGLSync FromSync(EGLSync s) {
  return static_cast<tgles::EGLSync>(reinterpret_cast<uintptr_t>(s));
}
EGLSync ToSync(tgles::EGLSync id) {
  return reinterpret_cast<EGLSync>(static_cast<uintptr_t>(id));
}

}  // namespace

extern "C" {

// --- Displays, configs, surfaces -------------------------------------------

EGLDisplay eglGetPlatformDisplay(EGLenum platform, void* native_display,
                                 const EGLAttrib* /*attrib_list*/) {
  // EglState models one display; the platform token selects errors only.
  if (platform != static_cast<EGLenum>(0x31DD) &&  // SURFACELESS_MESA
      platform != static_cast<EGLenum>(0x3202) &&  // ANGLE
      platform != static_cast<EGLenum>(0x313F) &&  // DEVICE_EXT
      platform != static_cast<EGLenum>(0x31D7) &&  // GBM_KHR
      platform != static_cast<EGLenum>(0x31D8) &&  // WAYLAND_KHR
      platform != static_cast<EGLenum>(0x31D5)) {  // X11_KHR
    // Still hand out a display (CTS probes unknown platforms), but record the
    // misuse where eglGetError can report it.
    Rt().egl().RecordError(static_cast<EGLint>(0x300C));  // BAD_PARAMETER
  }
  return ToDisplay(Rt().egl().GetPlatformDisplay(platform, native_display));
}

EGLDisplay eglGetPlatformDisplayEXT(EGLenum platform, void* native_display,
                                    const EGLint* /*attrib_list*/) {
  return eglGetPlatformDisplay(platform, native_display, nullptr);
}

EGLBoolean eglGetConfigs(EGLDisplay dpy, EGLConfig* configs,
                         EGLint config_size, EGLint* num_config) {
  if (configs == nullptr) {
    return Rt().egl().GetConfigs(FromDisplay(dpy), nullptr, config_size,
                                 num_config);
  }
  std::vector<tgles::EGLConfig> ids(
      config_size > 0 ? static_cast<std::size_t>(config_size) : 0);
  const EGLBoolean ok = Rt().egl().GetConfigs(
      FromDisplay(dpy), config_size > 0 ? ids.data() : nullptr, config_size,
      num_config);
  for (EGLint i = 0; i < config_size && i < (num_config ? *num_config : 0);
       ++i) {
    configs[i] = ToConfig(ids[static_cast<std::size_t>(i)]);
  }
  return ok;
}

EGLBoolean eglGetConfigAttrib(EGLDisplay dpy, EGLConfig config,
                              EGLint attribute, EGLint* value) {
  return Rt().egl().GetConfigAttrib(FromDisplay(dpy), FromConfig(config),
                                    attribute, value);
}

EGLSurface eglCreateWindowSurface(EGLDisplay dpy, EGLConfig config,
                                  EGLNativeWindowType win,
                                  const EGLint* attribs) {
  return ToSurface(Rt().egl().CreateWindowSurface(
      FromDisplay(dpy), FromConfig(config), win, attribs));
}

EGLSurface eglCreatePlatformWindowSurface(EGLDisplay dpy, EGLConfig config,
                                          void* native_window,
                                          const EGLAttrib* attrib_list) {
  (void)attrib_list;
  return ToSurface(Rt().egl().CreatePlatformWindowSurface(
      FromDisplay(dpy), FromConfig(config), native_window, nullptr));
}

EGLSurface eglCreatePlatformWindowSurfaceEXT(EGLDisplay dpy, EGLConfig config,
                                             void* native_window,
                                             const EGLint* attrib_list) {
  (void)attrib_list;
  return ToSurface(Rt().egl().CreatePlatformWindowSurface(
      FromDisplay(dpy), FromConfig(config), native_window, nullptr));
}

EGLSurface eglCreatePixmapSurface(EGLDisplay dpy, EGLConfig config,
                                  EGLNativePixmapType pixmap,
                                  const EGLint* attribs) {
  return ToSurface(Rt().egl().CreatePixmapSurface(
      FromDisplay(dpy), FromConfig(config), pixmap, attribs));
}

EGLSurface eglCreatePlatformPixmapSurface(EGLDisplay dpy, EGLConfig config,
                                          void* native_pixmap,
                                          const EGLAttrib* attrib_list) {
  (void)attrib_list;
  return ToSurface(Rt().egl().CreatePlatformPixmapSurface(
      FromDisplay(dpy), FromConfig(config), native_pixmap, nullptr));
}

EGLSurface eglCreatePlatformPixmapSurfaceEXT(EGLDisplay dpy, EGLConfig config,
                                             void* native_pixmap,
                                             const EGLint* attrib_list) {
  (void)attrib_list;
  return ToSurface(Rt().egl().CreatePlatformPixmapSurface(
      FromDisplay(dpy), FromConfig(config), native_pixmap, nullptr));
}

EGLSurface eglCreatePbufferFromClientBuffer(EGLDisplay dpy, EGLenum buftype,
                                            EGLClientBuffer buffer,
                                            EGLConfig config,
                                            const EGLint* attribs) {
  return ToSurface(Rt().egl().CreatePbufferFromClientBuffer(
      FromDisplay(dpy), buftype, buffer, FromConfig(config), attribs));
}

EGLBoolean eglQuerySurface(EGLDisplay dpy, EGLSurface surface,
                           EGLint attribute, EGLint* value) {
  return Rt().egl().QuerySurface(FromDisplay(dpy), FromSurface(surface),
                                 attribute, value);
}

EGLBoolean eglSurfaceAttrib(EGLDisplay dpy, EGLSurface surface,
                            EGLint attribute, EGLint value) {
  return Rt().egl().SurfaceAttrib(FromDisplay(dpy), FromSurface(surface),
                                  attribute, value);
}

EGLBoolean eglBindTexImage(EGLDisplay dpy, EGLSurface surface, EGLint buffer) {
  return Rt().egl().BindTexImage(FromDisplay(dpy), FromSurface(surface),
                                 buffer);
}

EGLBoolean eglReleaseTexImage(EGLDisplay dpy, EGLSurface surface,
                              EGLint buffer) {
  return Rt().egl().ReleaseTexImage(FromDisplay(dpy), FromSurface(surface),
                                    buffer);
}

EGLBoolean eglSwapInterval(EGLDisplay dpy, EGLint interval) {
  return Rt().egl().SwapInterval(FromDisplay(dpy), interval);
}

EGLBoolean eglSwapBuffersWithDamageEXT(EGLDisplay dpy, EGLSurface surface,
                                       const EGLint* rects, EGLint n_rects) {
  return Rt().egl().SwapBuffersWithDamageEXT(FromDisplay(dpy),
                                             FromSurface(surface), rects,
                                             n_rects);
}

EGLBoolean eglCopyBuffers(EGLDisplay dpy, EGLSurface surface,
                          EGLNativePixmapType target) {
  return Rt().egl().CopyBuffers(FromDisplay(dpy), FromSurface(surface),
                                target);
}

EGLBoolean eglUnlockSurfaceKHR(EGLDisplay dpy, EGLSurface surface) {
  return Rt().egl().UnlockSurfaceKHR(FromDisplay(dpy), FromSurface(surface));
}

// --- Contexts ---------------------------------------------------------------

EGLBoolean eglDestroyContext(EGLDisplay dpy, EGLContext ctx) {
  return Rt().egl().DestroyContext(FromDisplay(dpy), FromContext(ctx));
}

EGLBoolean eglQueryContext(EGLDisplay dpy, EGLContext ctx, EGLint attribute,
                           EGLint* value) {
  return Rt().egl().QueryContext(FromDisplay(dpy), FromContext(ctx), attribute,
                                 value);
}

EGLContext eglGetCurrentContext(void) {
  return ToContext(Rt().egl().GetCurrentContext());
}

EGLSurface eglGetCurrentSurface(EGLint readdraw) {
  return ToSurface(Rt().egl().GetCurrentSurface(readdraw));
}

EGLDisplay eglGetCurrentDisplay(void) {
  return ToDisplay(Rt().egl().GetCurrentDisplay());
}

// --- API binding, threads, waits --------------------------------------------

EGLBoolean eglBindAPI(EGLenum api) { return Rt().egl().BindApi(api); }

EGLenum eglQueryAPI(void) { return Rt().egl().QueryApi(); }

EGLBoolean eglReleaseThread(void) { return Rt().egl().ReleaseThread(); }

EGLBoolean eglWaitClient(void) { return Rt().egl().WaitClient(); }

EGLBoolean eglWaitGL(void) { return Rt().egl().WaitGL(); }

EGLBoolean eglWaitNative(EGLint engine) {
  return Rt().egl().WaitNative(engine);
}

// --- Images and syncs --------------------------------------------------------

EGLImage eglCreateImage(EGLDisplay dpy, EGLContext ctx, EGLenum target,
                        EGLClientBuffer buffer,
                        const EGLAttrib* attrib_list) {
  return ToImage(Rt().egl().CreateImage(FromDisplay(dpy), FromContext(ctx),
                                        target, buffer, attrib_list));
}

EGLBoolean eglDestroyImage(EGLDisplay dpy, EGLImage image) {
  return Rt().egl().DestroyImage(FromDisplay(dpy), FromImage(image));
}

EGLSync eglCreateSync(EGLDisplay dpy, EGLenum type,
                      const EGLAttrib* attrib_list) {
  return ToSync(Rt().egl().CreateSync(FromDisplay(dpy), type, attrib_list));
}

EGLBoolean eglDestroySync(EGLDisplay dpy, EGLSync sync) {
  return Rt().egl().DestroySync(FromDisplay(dpy), FromSync(sync));
}

EGLint eglClientWaitSync(EGLDisplay dpy, EGLSync sync, EGLint flags,
                         EGLTime timeout) {
  return Rt().egl().ClientWaitSync(FromDisplay(dpy), FromSync(sync), flags,
                                   timeout);
}

EGLBoolean eglGetSyncAttrib(EGLDisplay dpy, EGLSync sync, EGLint attribute,
                            EGLAttrib* value) {
  return Rt().egl().GetSyncAttrib(FromDisplay(dpy), FromSync(sync), attribute,
                                  value);
}

EGLBoolean eglWaitSync(EGLDisplay dpy, EGLSync sync, EGLint flags) {
  return Rt().egl().WaitSync(FromDisplay(dpy), FromSync(sync), flags);
}

EGLBoolean eglWaitSyncKHR(EGLDisplay dpy, EGLSync sync, EGLint flags) {
  return eglWaitSync(dpy, sync, flags);
}

}  // extern "C"
