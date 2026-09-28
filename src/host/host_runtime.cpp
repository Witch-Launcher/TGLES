#include "tgles/host/host_runtime.h"

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

void HostRuntime::FlagError(GLenum code) { host_errors_.Record(code); }

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

void HostRuntime::DrawElements(GLenum mode, GLsizei count, GLenum type,
                               std::uintptr_t indices) {
  if (!has_current_) {
    host_errors_.Record(kGlInvalidOperation);
    return;
  }
  if (bridge_ == nullptr) {
    // Validation-only headless mode: sync VAO EAB presence first, or a
    // missing index buffer would wrongly validate (spec 10.5).
    gl_.SyncElementState();
    gl_.draw().DrawElements(mode, count, type, indices);
    return;
  }
  gl_.RenderElements(*bridge_, mode, count, type, indices);
}

void HostRuntime::DrawArraysInstanced(GLenum mode, GLint first, GLsizei count,
                                      GLsizei instanceCount) {
  if (!has_current_) {
    host_errors_.Record(kGlInvalidOperation);
    return;
  }
  if (bridge_ == nullptr) {
    gl_.draw().DrawArraysInstanced(mode, first, count, instanceCount);
    return;
  }
  gl_.RenderInstanced(bridge_, mode, first, count, instanceCount);
}

void HostRuntime::DrawElementsInstanced(GLenum mode, GLsizei count,
                                        GLenum type, std::uintptr_t indices,
                                        GLsizei instanceCount) {
  if (!has_current_) {
    host_errors_.Record(kGlInvalidOperation);
    return;
  }
  if (bridge_ == nullptr) {
    gl_.SyncElementState();
    gl_.draw().DrawElementsInstanced(mode, count, type, indices,
                                     instanceCount);
    return;
  }
  gl_.RenderElementsInstanced(bridge_, mode, count, type, indices,
                              instanceCount);
}

void HostRuntime::DrawArraysIndirect(GLenum mode, std::uintptr_t indirect) {
  if (!has_current_) {
    host_errors_.Record(kGlInvalidOperation);
    return;
  }
  if (bridge_ == nullptr) {
    gl_.SyncIndirectState();
    gl_.draw().DrawArraysIndirect(mode, indirect);
    return;
  }
  gl_.RenderArraysIndirect(bridge_, mode, indirect);
}

void HostRuntime::DrawElementsIndirect(GLenum mode, GLenum type,
                                       std::uintptr_t indirect) {
  if (!has_current_) {
    host_errors_.Record(kGlInvalidOperation);
    return;
  }
  if (bridge_ == nullptr) {
    gl_.SyncIndirectState();
    gl_.draw().DrawElementsIndirect(mode, type, indirect);
    return;
  }
  gl_.RenderElementsIndirect(bridge_, mode, type, indirect);
}

void HostRuntime::DrawRangeElements(GLenum mode, GLuint start, GLuint end,
                                    GLsizei count, GLenum type,
                                    std::uintptr_t indices) {
  if (!has_current_) {
    host_errors_.Record(kGlInvalidOperation);
    return;
  }
  if (bridge_ == nullptr) {
    gl_.SyncElementState();
    gl_.draw().DrawRangeElements(mode, start, end, count, type, indices);
    return;
  }
  // Range is a validation refinement; execution matches DrawElements.
  gl_.SyncElementState();
  if (!gl_.draw().DrawRangeElements(mode, start, end, count, type, indices))
    return;
  gl_.RenderElements(*bridge_, mode, count, type, indices);
}

void HostRuntime::DrawElementsBaseVertex(GLenum mode, GLsizei count,
                                         GLenum type, std::uintptr_t indices,
                                         GLint basevertex) {
  if (!has_current_) {
    host_errors_.Record(kGlInvalidOperation);
    return;
  }
  gl_.SyncElementState();
  if (!gl_.draw().DrawElementsBaseVertex(mode, count, type, indices, basevertex))
    return;
  if (bridge_ == nullptr) return;
  // Executed: CPU-side index rebasing in the facade, then the shared submit
  // path (same as RenderElementsIndirect's baseVertex handling).
  gl_.RenderElementsBaseVertex(*bridge_, mode, count, type, indices,
                               basevertex);
}

void HostRuntime::DrawRangeElementsBaseVertex(
    GLenum mode, GLuint start, GLuint end, GLsizei count, GLenum type,
    std::uintptr_t indices, GLint basevertex) {
  if (!has_current_) {
    host_errors_.Record(kGlInvalidOperation);
    return;
  }
  gl_.SyncElementState();
  if (!gl_.draw().DrawRangeElementsBaseVertex(mode, start, end, count, type,
                                              indices, basevertex))
    return;
  if (bridge_ == nullptr) return;
  gl_.RenderRangeElementsBaseVertex(*bridge_, mode, start, end, count, type,
                                    indices, basevertex);
}

void HostRuntime::DrawElementsInstancedBaseVertex(
    GLenum mode, GLsizei count, GLenum type, std::uintptr_t indices,
    GLsizei instanceCount, GLint basevertex) {
  if (!has_current_) {
    host_errors_.Record(kGlInvalidOperation);
    return;
  }
  gl_.SyncElementState();
  if (!gl_.draw().DrawElementsInstancedBaseVertex(mode, count, type, indices,
                                                  instanceCount, basevertex))
    return;
  if (bridge_ == nullptr) return;
  gl_.RenderElementsInstancedBaseVertex(bridge_, mode, count, type, indices,
                                        instanceCount, basevertex);
}

void HostRuntime::DrawArraysInstancedBaseInstance(GLenum mode, GLint first,
                                                  GLsizei count,
                                                  GLsizei instanceCount,
                                                  GLuint baseInstance) {
  if (!has_current_) {
    host_errors_.Record(kGlInvalidOperation);
    return;
  }
  if (!gl_.draw().DrawArraysInstanced(mode, first, count, instanceCount))
    return;
  if (bridge_ == nullptr) return;
  gl_.RenderArraysInstancedBaseInstance(bridge_, mode, first, count,
                                        instanceCount, baseInstance);
}

void HostRuntime::DrawElementsInstancedBaseInstance(
    GLenum mode, GLsizei count, GLenum type, std::uintptr_t indices,
    GLsizei instanceCount, GLuint baseInstance) {
  if (!has_current_) {
    host_errors_.Record(kGlInvalidOperation);
    return;
  }
  gl_.SyncElementState();
  if (!gl_.draw().DrawElementsInstanced(mode, count, type, indices,
                                        instanceCount))
    return;
  if (bridge_ == nullptr) return;
  gl_.RenderElementsInstancedBaseInstance(bridge_, mode, count, type, indices,
                                          instanceCount, baseInstance);
}

void HostRuntime::DrawElementsInstancedBaseVertexBaseInstance(
    GLenum mode, GLsizei count, GLenum type, std::uintptr_t indices,
    GLsizei instanceCount, GLint basevertex, GLuint baseInstance) {
  if (!has_current_) {
    host_errors_.Record(kGlInvalidOperation);
    return;
  }
  gl_.SyncElementState();
  if (!gl_.draw().DrawElementsInstancedBaseVertex(mode, count, type, indices,
                                                  instanceCount, basevertex))
    return;
  if (bridge_ == nullptr) return;
  gl_.RenderElementsInstancedBaseVertexBaseInstance(
      bridge_, mode, count, type, indices, instanceCount, basevertex,
      baseInstance);
}

void HostRuntime::MultiDrawArraysIndirect(GLenum mode, std::uintptr_t indirect,
                                          GLsizei drawcount, GLsizei stride) {
  if (!has_current_) {
    host_errors_.Record(kGlInvalidOperation);
    return;
  }
  gl_.SyncIndirectState();
  if (drawcount < 0 || stride < 0) {
    gl_.draw().FlagBridgeError();
    return;
  }
  if (bridge_ == nullptr) {
    // Validation-only: each command still validates through the facade.
    gl_.MultiDrawArraysIndirect(nullptr, mode, indirect, drawcount, stride);
    return;
  }
  gl_.MultiDrawArraysIndirect(bridge_, mode, indirect, drawcount, stride);
}

void HostRuntime::MultiDrawElementsIndirect(GLenum mode, GLenum type,
                                            std::uintptr_t indirect,
                                            GLsizei drawcount,
                                            GLsizei stride) {
  if (!has_current_) {
    host_errors_.Record(kGlInvalidOperation);
    return;
  }
  gl_.SyncIndirectState();
  if (drawcount < 0 || stride < 0) {
    gl_.draw().FlagBridgeError();
    return;
  }
  if (bridge_ == nullptr) {
    gl_.MultiDrawElementsIndirect(nullptr, mode, type, indirect, drawcount,
                                  stride);
    return;
  }
  gl_.MultiDrawElementsIndirect(bridge_, mode, type, indirect, drawcount,
                                stride);
}

void HostRuntime::MultiDrawElementsBaseVertex(
    GLenum mode, const GLsizei* count, GLenum type,
    const void* const* indices, GLsizei drawcount, const GLint* basevertex) {
  if (!has_current_) {
    host_errors_.Record(kGlInvalidOperation);
    return;
  }
  gl_.SyncElementState();
  if (drawcount < 0) {
    gl_.draw().FlagBridgeError();
    return;
  }
  gl_.MultiDrawElementsBaseVertex(bridge_, mode, count, type, indices,
                                  drawcount, basevertex);
}

void HostRuntime::Finish() {
  if (!has_current_) {
    host_errors_.Record(kGlInvalidOperation);
    return;
  }
  if (bridge_ == nullptr) return;  // Nothing outstanding anywhere.
  if (!bridge_->WaitForCompletion(bridge_->FrameSerial())) {
    host_errors_.Record(kGlInvalidOperation);
  }
}

void HostRuntime::Flush() {
  if (!has_current_) {
    host_errors_.Record(kGlInvalidOperation);
  }
  // No-op otherwise: the bridge owns its command queue already.
}

GLenum HostRuntime::GetError() {
  if (host_errors_.HasPending()) return host_errors_.Get();
  return gl_.GetError();
}

}  // namespace tgles
