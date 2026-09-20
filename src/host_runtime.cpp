#include "tgles/host_runtime.h"

namespace tgles {

HostRuntime::HostRuntime() : gl_(GlesContext::Create(false)) {}

HostRuntime& HostRuntime::Instance() {
  static HostRuntime instance;
  return instance;
}

void HostRuntime::SetMetalBridge(metal_bridge::MetalBridge* bridge) {
  bridge_ = bridge;
}

void HostRuntime::SetCurrent(bool current) { has_current_ = current; }

void HostRuntime::FlagNoContext() {
  host_errors_.Record(kGlInvalidOperation);
}

void HostRuntime::DrawArrays(GLenum mode, GLint first, GLsizei count) {
  if (!has_current_) {
    host_errors_.Record(kGlInvalidOperation);
    return;
  }
  if (bridge_ == nullptr) {
    // Validation-only headless mode: run the validator, execute nothing.
    gl_.draw().DrawArrays(mode, first, count);
    return;
  }
  gl_.RenderFrame(*bridge_, mode, first, count);
}

GLenum HostRuntime::GetError() {
  if (host_errors_.HasPending()) return host_errors_.Get();
  return gl_.GetError();
}

}  // namespace tgles
