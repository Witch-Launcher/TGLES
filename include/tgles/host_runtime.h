#ifndef TGLES_HOST_RUNTIME_H
#define TGLES_HOST_RUNTIME_H

// Process-wide host runtime backing the C API (Phase A subset).
// Owns one EglState and one GlesContext; EGL calls set the current tuple,
// GL calls require it (otherwise INVALID_OPERATION in the host queue, which
// glGetError drains before the context queue). An attached MetalBridge
// turns glDrawArrays(TRIANGLES) into real frames via GlesContext::
// RenderFrame; with no bridge, draws validate only (deterministic headless
// mode for unit tests). Single-threaded, like the underlying EglState.

#include "tgles/error.h"
#include "tgles/egl.h"
#include "tgles/gles.h"
#include "tgles/metal_bridge.h"

namespace tgles {

class HostRuntime {
 public:
  static HostRuntime& Instance();

  EglState& egl() { return egl_; }
  GlesContext& gl() { return gl_; }

  void SetMetalBridge(metal_bridge::MetalBridge* bridge);
  bool HasCurrent() const { return has_current_; }
  void SetCurrent(bool current);
  // Records INVALID_OPERATION for a GL call with no current context.
  void FlagNoContext();

  // Validating draw; executes through the bridge when attached and the
  // mode is TRIANGLES (see GlesContext::RenderFrame for the exact rules).
  void DrawArrays(GLenum mode, GLint first, GLsizei count);

  // Host-queue error first, then the context queue (spec 2.3.1 pairing).
  GLenum GetError();

 private:
  HostRuntime();
  ErrorQueue host_errors_;

  EglState egl_;
  GlesContext gl_;
  metal_bridge::MetalBridge* bridge_ = nullptr;
  bool has_current_ = false;
};

}  // namespace tgles

#endif  // TGLES_HOST_RUNTIME_H
