#ifndef TGLES_HOST_RUNTIME_H
#define TGLES_HOST_RUNTIME_H

// Process-wide host runtime backing the C API (Phase A subset).
// Owns one EglState and one GlesContext; EGL calls set the current tuple,
// GL calls require it (otherwise INVALID_OPERATION in the host queue, which
// glGetError drains before the context queue). An attached MetalBridge
// turns glDrawArrays(TRIANGLES) into real frames via GlesContext::
// RenderFrame; with no bridge, draws validate only (deterministic headless
// mode for unit tests). Single-threaded, like the underlying EglState.

#include "tgles/base/error.h"
#include "tgles/egl/egl.h"
#include "tgles/facade/gles.h"
#include "tgles/gpu/metal_bridge.h"

namespace tgles {

class HostRuntime {
 public:
  static HostRuntime& Instance();

  EglState& egl() { return egl_; }
  GlesContext& gl() { return gl_; }

  void SetMetalBridge(metal_bridge::MetalBridge* bridge);
  metal_bridge::MetalBridge* Bridge() { return bridge_; }
  bool HasCurrent() const { return has_current_; }
  void SetCurrent(bool current);
  // Records INVALID_OPERATION for a GL call with no current context.
  void FlagNoContext();
  // Records an explicit host-detected error (desktop-only entry points,
  // static enum tables). Keeps one error queue for the whole host surface so
  // GetError drains host errors before context errors (spec 2.3.1 pairing).
  void FlagError(GLenum code);

  // Validating draw; executes through the bridge when attached and the
  // mode is TRIANGLES (see GlesContext::RenderFrame for the exact rules).
  void DrawArrays(GLenum mode, GLint first, GLsizei count);
  // Indexed sibling: `indices` is a byte offset into the VAO's
  // ELEMENT_ARRAY_BUFFER (see GlesContext::RenderElements).
  void DrawElements(GLenum mode, GLsizei count, GLenum type,
                    std::uintptr_t indices);
  void DrawArraysInstanced(GLenum mode, GLint first, GLsizei count,
                           GLsizei instanceCount);
  void DrawElementsInstanced(GLenum mode, GLsizei count, GLenum type,
                             std::uintptr_t indices, GLsizei instanceCount);
  void DrawArraysIndirect(GLenum mode, std::uintptr_t indirect);
  void DrawElementsIndirect(GLenum mode, GLenum type, std::uintptr_t indirect);
  void DrawRangeElements(GLenum mode, GLuint start, GLuint end, GLsizei count,
                         GLenum type, std::uintptr_t indices);
  void DrawElementsBaseVertex(GLenum mode, GLsizei count, GLenum type,
                              std::uintptr_t indices, GLint basevertex);
  void DrawRangeElementsBaseVertex(GLenum mode, GLuint start, GLuint end,
                                   GLsizei count, GLenum type,
                                   std::uintptr_t indices, GLint basevertex);
  void DrawElementsInstancedBaseVertex(GLenum mode, GLsizei count, GLenum type,
                                       std::uintptr_t indices,
                                       GLsizei instanceCount, GLint basevertex);
  // EXT_base_instance / EXT_multi_draw_indirect families: same context and
  // bridge rules as the core instanced/indirect draws above (validation-only
  // without a bridge, executed with one).
  void DrawArraysInstancedBaseInstance(GLenum mode, GLint first, GLsizei count,
                                       GLsizei instanceCount,
                                       GLuint baseInstance);
  void DrawElementsInstancedBaseInstance(GLenum mode, GLsizei count,
                                         GLenum type, std::uintptr_t indices,
                                         GLsizei instanceCount,
                                         GLuint baseInstance);
  void DrawElementsInstancedBaseVertexBaseInstance(
      GLenum mode, GLsizei count, GLenum type, std::uintptr_t indices,
      GLsizei instanceCount, GLint basevertex, GLuint baseInstance);
  void MultiDrawArraysIndirect(GLenum mode, std::uintptr_t indirect,
                               GLsizei drawcount, GLsizei stride);
  void MultiDrawElementsIndirect(GLenum mode, GLenum type,
                                 std::uintptr_t indirect, GLsizei drawcount,
                                 GLsizei stride);
  void MultiDrawElementsBaseVertex(GLenum mode, const GLsizei* count,
                                   GLenum type, const void* const* indices,
                                   GLsizei drawcount,
                                   const GLint* basevertex);
  // glFinish: block until submitted work completes. With no bridge there is
  // nothing outstanding (success); otherwise wait on the bridge fence ring.
  void Finish();
  // glFlush: submitted work is already visible to the bridge command queue;
  // a CPU model has nothing to push, so this only requires a context.
  void Flush();

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
