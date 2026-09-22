// OpenGL ES 3.2 C entry points (Phase A trial subset).
// Each wrapper converts the C ABI (opaque void* EGL handles) to the C++
// managers owned by HostRuntime. GL calls with no current context record
// INVALID_OPERATION in the host queue (drained first by glGetError).

#include "tgles/host/host_c_api.h"

#include <cstring>
#include <vector>

#include "tgles/host/host_runtime.h"

namespace {

tgles::HostRuntime& Rt() { return tgles::HostRuntime::Instance(); }

// TGL EGL handles are small integer ids; the C ABI carries them as void*.
EGLDisplay ToDisplay(tgles::EGLDisplay id) {
  return reinterpret_cast<EGLDisplay>(static_cast<uintptr_t>(id));
}
tgles::EGLDisplay FromDisplay(EGLDisplay dpy) {
  return static_cast<tgles::EGLDisplay>(reinterpret_cast<uintptr_t>(dpy));
}
EGLConfig ToConfig(tgles::EGLConfig id) {
  return reinterpret_cast<EGLConfig>(static_cast<uintptr_t>(id));
}
tgles::EGLConfig FromConfig(EGLConfig cfg) {
  return static_cast<tgles::EGLConfig>(reinterpret_cast<uintptr_t>(cfg));
}
EGLSurface ToSurface(tgles::EGLSurface id) {
  return reinterpret_cast<EGLSurface>(static_cast<uintptr_t>(id));
}
tgles::EGLSurface FromSurface(EGLSurface s) {
  return static_cast<tgles::EGLSurface>(reinterpret_cast<uintptr_t>(s));
}
EGLContext ToContext(tgles::EGLContext id) {
  return reinterpret_cast<EGLContext>(static_cast<uintptr_t>(id));
}
tgles::EGLContext FromContext(EGLContext c) {
  return static_cast<tgles::EGLContext>(reinterpret_cast<uintptr_t>(c));
}

bool HaveCurrent() {
  if (!Rt().HasCurrent()) {
    Rt().FlagNoContext();
    return false;
  }
  return true;
}

}  // namespace

extern "C" {

EGLDisplay eglGetDisplay(void* native_display) {
  return ToDisplay(Rt().egl().GetDisplay(native_display));
}

EGLBoolean eglInitialize(EGLDisplay dpy, EGLint* major, EGLint* minor) {
  return Rt().egl().Initialize(FromDisplay(dpy), major, minor);
}

EGLBoolean eglTerminate(EGLDisplay dpy) {
  return Rt().egl().Terminate(FromDisplay(dpy));
}

EGLBoolean eglChooseConfig(EGLDisplay dpy, const EGLint* attribs,
                           EGLConfig* configs, EGLint config_size,
                           EGLint* num_config) {
  if (configs == nullptr) {
    return Rt().egl().ChooseConfig(FromDisplay(dpy), attribs, nullptr,
                                   config_size, num_config);
  }
  std::vector<tgles::EGLConfig> ids(
      config_size > 0 ? static_cast<std::size_t>(config_size) : 0);
  const EGLBoolean ok = Rt().egl().ChooseConfig(
      FromDisplay(dpy), attribs,
      config_size > 0 ? ids.data() : nullptr, config_size, num_config);
  for (EGLint i = 0; i < config_size && i < (num_config ? *num_config : 0);
       ++i) {
    configs[i] = ToConfig(ids[static_cast<std::size_t>(i)]);
  }
  return ok;
}

EGLContext eglCreateContext(EGLDisplay dpy, EGLConfig config,
                            EGLContext share_context, const EGLint* attribs) {
  return ToContext(Rt().egl().CreateContext(
      FromDisplay(dpy), FromConfig(config), FromContext(share_context),
      attribs));
}

EGLSurface eglCreatePbufferSurface(EGLDisplay dpy, EGLConfig config,
                                   const EGLint* attribs) {
  return ToSurface(
      Rt().egl().CreatePbufferSurface(FromDisplay(dpy), FromConfig(config),
                                      attribs));
}

EGLBoolean eglDestroySurface(EGLDisplay dpy, EGLSurface surface) {
  return Rt().egl().DestroySurface(FromDisplay(dpy), FromSurface(surface));
}

EGLBoolean eglMakeCurrent(EGLDisplay dpy, EGLSurface draw, EGLSurface read,
                          EGLContext ctx) {
  const EGLBoolean ok = Rt().egl().MakeCurrent(
      FromDisplay(dpy), FromSurface(draw), FromSurface(read),
      FromContext(ctx));
  Rt().SetCurrent(ok != 0);
  return ok;
}

EGLBoolean eglSwapBuffers(EGLDisplay dpy, EGLSurface surface) {
  const EGLBoolean ok =
      Rt().egl().SwapBuffers(FromDisplay(dpy), FromSurface(surface));
  if (ok == 0) return ok;
  // Window surfaces present through the attached bridge (the launcher path:
  // EGL window + swap is how every GLES game presents). Pbuffers stay a
  // validation-only no-op per spec. No bridge (headless tests) also succeeds
  // without presenting. A failed present maps to EGL_FALSE with BAD_ALLOC
  // (resource failure, verified against docs/reference/egl.h 0x3003).
  if (!Rt().egl().IsWindowSurface(FromSurface(surface))) return ok;
  tgles::metal_bridge::MetalBridge* bridge = Rt().Bridge();
  if (bridge == nullptr) return ok;
  if (!bridge->Present()) {
    Rt().egl().RecordError(tgles::kEglBadAlloc);
    return 0;
  }
  if (!bridge->WaitForCompletion(bridge->FrameSerial())) {
    Rt().egl().RecordError(tgles::kEglBadAlloc);
    return 0;
  }
  return ok;
}

EGLint eglGetError(void) { return Rt().egl().GetError(); }

const char* eglQueryString(EGLDisplay dpy, EGLint name) {
  return Rt().egl().QueryString(FromDisplay(dpy), name);
}

// eglGetProcAddress lives in src/host/abi/dispatch.cpp: it is the single
// name -> address table for the whole 371 GLES + 46 EGL contract, so dlsym and
// eglGetProcAddress can never diverge (plan-02 Block A). Kept out of this file
// to avoid a duplicate symbol at link time.

void glGenBuffers(GLsizei n, GLuint* buffers) {
  if (!HaveCurrent()) return;
  Rt().gl().buffers().GenBuffers(n, buffers);
}

void glBindBuffer(GLenum target, GLuint buffer) {
  if (!HaveCurrent()) return;
  Rt().gl().BindBuffer(target, buffer);
}

void glBufferData(GLenum target, GLsizeiptr size, const void* data,
                  GLenum usage) {
  if (!HaveCurrent()) return;
  Rt().gl().buffers().BufferData(target, size, data, usage);
}

void glGenVertexArrays(GLsizei n, GLuint* arrays) {
  if (!HaveCurrent()) return;
  Rt().gl().vertex_arrays().GenVertexArrays(n, arrays);
}

void glBindVertexArray(GLuint array) {
  if (!HaveCurrent()) return;
  Rt().gl().vertex_arrays().BindVertexArray(array);
}

void glEnableVertexAttribArray(GLuint index) {
  if (!HaveCurrent()) return;
  Rt().gl().vertex_arrays().EnableVertexAttribArray(index);
}

void glVertexAttribPointer(GLuint index, GLint size, GLenum type,
                           GLboolean normalized, GLsizei stride,
                           const void* pointer) {
  if (!HaveCurrent()) return;
  const GLuint bound =
      Rt().gl().buffers().BoundBuffer(tgles::kGlArrayBuffer);
  Rt().gl().vertex_arrays().VertexAttribPointer(
      index, size, type, normalized, stride,
      reinterpret_cast<std::uintptr_t>(pointer), bound);
}

GLuint glCreateShader(GLenum type) {
  if (!HaveCurrent()) return 0;
  return Rt().gl().shaders().CreateShader(type);
}

void glShaderSource(GLuint shader, GLsizei count, const char* const* string,
                    const GLint* length) {
  if (!HaveCurrent()) return;
  Rt().gl().shaders().ShaderSource(shader, count,
                                   const_cast<const char**>(string), length);
}

void glCompileShader(GLuint shader) {
  if (!HaveCurrent()) return;
  Rt().gl().shaders().CompileShader(shader);
}

void glGetShaderiv(GLuint shader, GLenum pname, GLint* params) {
  if (!HaveCurrent()) return;
  Rt().gl().shaders().GetShaderiv(shader, pname, params);
}

GLuint glCreateProgram(void) {
  if (!HaveCurrent()) return 0;
  return Rt().gl().programs().CreateProgram();
}

void glAttachShader(GLuint program, GLuint shader) {
  if (!HaveCurrent()) return;
  Rt().gl().programs().AttachShader(program, shader);
}

void glLinkProgram(GLuint program) {
  if (!HaveCurrent()) return;
  Rt().gl().programs().LinkProgram(program);
}

void glGetProgramiv(GLuint program, GLenum pname, GLint* params) {
  if (!HaveCurrent()) return;
  Rt().gl().programs().GetProgramiv(program, pname, params);
}

void glUseProgram(GLuint program) {
  if (!HaveCurrent()) return;
  Rt().gl().programs().UseProgram(program);
}

GLint glGetUniformLocation(GLuint program, const char* name) {
  if (!HaveCurrent()) return -1;
  return Rt().gl().programs().GetUniformLocation(program, name);
}

void glUniformMatrix4fv(GLint location, GLsizei count, GLboolean transpose,
                        const GLfloat* value) {
  if (!HaveCurrent()) return;
  Rt().gl().programs().UniformMatrix4fv(location, count, transpose, value);
}

void glGenTextures(GLsizei n, GLuint* textures) {
  if (!HaveCurrent()) return;
  Rt().gl().textures().GenTextures(n, textures);
}

void glBindTexture(GLenum target, GLuint texture) {
  if (!HaveCurrent()) return;
  Rt().gl().textures().BindTexture(target, texture);
}

void glTexImage2D(GLenum target, GLint level, GLint internalformat,
                  GLsizei width, GLsizei height, GLint border, GLenum format,
                  GLenum type, const void* pixels) {
  if (!HaveCurrent()) return;
  Rt().gl().textures().TexImage2D(target, level, internalformat, width,
                                  height, border, format, type, pixels);
}

void glGenFramebuffers(GLsizei n, GLuint* framebuffers) {
  if (!HaveCurrent()) return;
  Rt().gl().framebuffers().GenFramebuffers(n, framebuffers);
}

void glBindFramebuffer(GLenum target, GLuint framebuffer) {
  if (!HaveCurrent()) return;
  Rt().gl().framebuffers().BindFramebuffer(target, framebuffer);
}

void glFramebufferTexture2D(GLenum target, GLenum attachment,
                            GLenum textarget, GLuint texture, GLint level) {
  if (!HaveCurrent()) return;
  Rt().gl().framebuffers().FramebufferTexture2D(target, attachment,
                                                textarget, texture, level);
}

void glViewport(GLint x, GLint y, GLsizei width, GLsizei height) {
  if (!HaveCurrent()) return;
  Rt().gl().raster().Viewport(x, y, width, height);
}

void glDrawArrays(GLenum mode, GLint first, GLsizei count) {
  if (!HaveCurrent()) return;
  Rt().DrawArrays(mode, first, count);
}

GLenum glGetError(void) { return Rt().GetError(); }

}  // extern "C"
