// Present ABI: lets an out-of-process host (game window, MobileGL-style
// loader) drive Metal presentation through the BUILT shared library.
//
// Why this file exists: HostRuntime::SetMetalBridge is a C++ API, so a
// dlopen()ed libtgles.dylib renders validation-only until something inside
// the library owns a Metal bridge. These C entry points create the Apple
// bridge INSIDE the dylib (same HostRuntime instance as the gl* calls),
// attach a caller-provided CAMetalLayer, and present on demand. The game
// window (apps/tgl_game_window.mm) uses ONLY dlsym/eglGetProcAddress to
// reach them — never links tgles_core directly — proving the built file is
// the real driver, not the test harness.
//
// Apple-only bodies: on non-Apple hosts every call fails closed (0) with
// the host INVALID_OPERATION queue untouched (no context needed for the
// failure itself; callers check glGetError/eglGetError as usual).

#include "tgles/host/abi.h"

#include <cstddef>
#include <cstdint>
#include <memory>

#include "tgles/base/debug_log.h"

#include "tgles/host/host_runtime.h"

#if defined(__APPLE__)
#include "tgles/gpu/metal_bridge_apple.h"
#endif

namespace {

#if defined(__APPLE__)
std::unique_ptr<tgles::metal_bridge::MetalBridge>& PresentBridge() {
  static std::unique_ptr<tgles::metal_bridge::MetalBridge> bridge;
  return bridge;
}
#endif

}  // namespace

extern "C" {

// Attaches a CAMetalLayer (passed as void* so this header stays pure C) and
// sizes it. Creates + initializes the in-dylib Apple bridge on first use
// (family Apple3 baseline: every Apple GPU since A9/M1 supports the TGL MSL).
// Returns 1 on success, 0 on failure (no device, bad size, non-Apple host).
int tglHostAttachMetalLayer(void* ca_metal_layer, int width, int height) {
#if defined(__APPLE__)
  if (ca_metal_layer == nullptr || width <= 0 || height <= 0) return 0;
  auto& bridge = PresentBridge();
  if (bridge == nullptr) {
    bridge = tgles::metal_bridge::CreateAppleBridge();
    if (bridge == nullptr) return 0;
    if (!bridge->Initialize("Apple3")) {
      bridge.reset();
      return 0;
    }
    tgles::HostRuntime::Instance().SetMetalBridge(bridge.get());
  }
  bridge->SetLayer(ca_metal_layer, static_cast<tgles::GLsizei>(width),
                   static_cast<tgles::GLsizei>(height));
  if (bridge->GetError() != tgles::kGlNoError) return 0;
  // Mirror the drawable size into EGL window surfaces so eglQuerySurface
  // reports the real size instead of 0x0 (hosts probe this for readbacks).
  tgles::HostRuntime::Instance().egl().SetWindowSurfaceSize(width, height);
  // Also size the default FBO (name 0) so window draws are not fail-closed
  // on Attachment() width=0 (the black-screen SubmitVertices gap).
  tgles::HostRuntime::Instance().gl().framebuffers().SetDefaultFramebufferSize(
      width, height);
  return 1;
#else
  (void)ca_metal_layer;
  (void)width;
  (void)height;
  return 0;
#endif
}

int tglHostResizeMetalLayer(int width, int height) {
#if defined(__APPLE__)
  auto& bridge = PresentBridge();
  if (bridge == nullptr || width <= 0 || height <= 0) return 0;
  bridge->Resize(static_cast<tgles::GLsizei>(width),
                 static_cast<tgles::GLsizei>(height));
  if (bridge->GetError() != tgles::kGlNoError) return 0;
  tgles::HostRuntime::Instance().egl().SetWindowSurfaceSize(width, height);
  tgles::HostRuntime::Instance().gl().framebuffers().SetDefaultFramebufferSize(
      width, height);
  return 1;
#else
  (void)width;
  (void)height;
  return 0;
#endif
}

// Presents the current frame (commit + presentDrawable when a layer is
// attached) and blocks until the GPU finishes it, mirroring the
// fence-before-swap ordering MobileGL DirectGLES relies on. Returns 1 on
// success, 0 when no bridge/layer is attached or the commit fails.
int tglHostPresent(void) {
#if defined(__APPLE__)
  auto& bridge = PresentBridge();
  if (bridge == nullptr) return 0;
  // Multi-draw before swap: seal any open frame so Present shows this
  // frame's draws (eglSwapBuffers does the same).
  if (bridge->FrameOpen() && !bridge->CommitFrame()) {
    (void)bridge->GetError();
    return 0;
  }
  if (!bridge->Present()) {
    (void)bridge->GetError();
    return 0;
  }
  if (!bridge->WaitForCompletion(bridge->FrameSerial())) {
    (void)bridge->GetError();
    return 0;
  }
  return 1;
#else
  return 0;
#endif
}

int tglHostReadbackPixel(int x, int y, unsigned char out_rgba[4]) {
#if defined(__APPLE__)
  auto& bridge = PresentBridge();
  if (bridge == nullptr || out_rgba == nullptr) return 0;
  std::uint8_t px[4] = {0};
  if (!bridge->ReadbackPixel(static_cast<tgles::GLint>(x),
                             static_cast<tgles::GLint>(y), px)) {
    return 0;
  }
  out_rgba[0] = px[0];
  out_rgba[1] = px[1];
  out_rgba[2] = px[2];
  out_rgba[3] = px[3];
  return 1;
#else
  (void)x;
  (void)y;
  (void)out_rgba;
  return 0;
#endif
}

unsigned long long tglHostFrameSerial(void) {
#if defined(__APPLE__)
  auto& bridge = PresentBridge();
  return bridge == nullptr ? 0u : bridge->FrameSerial();
#else
  return 0u;
#endif
}

void tglHostSetDebugLog(int mode) { tgles::TglSetDebugLogMode(mode); }

int tglHostGetDebugLog(char* out, int capacity) {
  return tgles::TglDebugLogDrain(out, capacity);
}

}  // extern "C"
