// OpenGL ES 3.2 C entry points (Phase A trial subset).
// Each wrapper converts the C ABI (opaque void* EGL handles) to the C++
// managers owned by HostRuntime. GL calls with no current context record
// INVALID_OPERATION in the host queue (drained first by glGetError).

#include "tgles/host/host_c_api.h"

#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "tgles/base/debug_log.h"
#include "tgles/host/host_runtime.h"

namespace {

tgles::HostRuntime& Rt() { return tgles::HostRuntime::Instance(); }

// First-N program-lifecycle diagnostics (black-screen: prog always 0).
int DiagP(const char* tag) {
  static int n_create = 0, n_link = 0, n_use = 0, n_compile = 0;
  if (tag[0] == 'c' && tag[1] == 'r') return ++n_create;
  if (tag[0] == 'l') return ++n_link;
  if (tag[0] == 'u') return ++n_use;
  return ++n_compile;
}

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
  // Multi-draw before swap: seal any open frame so Present shows the
  // draws from this frame (reopen-at-swap path; SubmitVertices may leave
  // the frame open across several glDraw* calls).
  if (bridge->FrameOpen() && !bridge->CommitFrame()) {
    // Present/commit failures latch INVALID_OPERATION on the bridge; drain it
    // so the next draw does not false-fail on a stale first-error-wins latch.
    (void)bridge->GetError();
    Rt().egl().RecordError(tgles::kEglBadAlloc);
    return 0;
  }
  if (!bridge->Present()) {
    (void)bridge->GetError();
    Rt().egl().RecordError(tgles::kEglBadAlloc);
    return 0;
  }
  if (!bridge->WaitForCompletion(bridge->FrameSerial())) {
    (void)bridge->GetError();
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
  Rt().gl().BindVertexArray(array);
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
  const int n = DiagP("compile");
  if (n <= 32) {
    GLint ok = 0;
    Rt().gl().shaders().GetShaderiv(shader, tgles::kGlCompileStatus, &ok);
    tgles::TglDebugf("diag CompileShader#%d sh=%u ok=%d", n, shader, ok);
  }
}

void glGetShaderiv(GLuint shader, GLenum pname, GLint* params) {
  if (!HaveCurrent()) return;
  Rt().gl().shaders().GetShaderiv(shader, pname, params);
}

GLuint glCreateProgram(void) {
  if (!HaveCurrent()) {
    const int n = DiagP("use");
    if (n <= 8) tgles::TglDebugf("diag CreateProgram no_current#%d", n);
    return 0;
  }
  const GLuint id = Rt().gl().programs().CreateProgram();
  const int n = DiagP("create");
  if (n <= 32) tgles::TglDebugf("diag CreateProgram#%d id=%u", n, id);
  return id;
}

void glAttachShader(GLuint program, GLuint shader) {
  if (!HaveCurrent()) return;
  Rt().gl().programs().AttachShader(program, shader);
}

void glLinkProgram(GLuint program) {
  if (!HaveCurrent()) {
    const int n = DiagP("use");
    if (n <= 8) tgles::TglDebugf("diag LinkProgram no_current#%d prog=%u", n, program);
    return;
  }
  Rt().gl().programs().LinkProgram(program);
  const int n = DiagP("link");
  if (n <= 32) {
    GLint ok = 0;
    Rt().gl().programs().GetProgramiv(program, tgles::kGlLinkStatus, &ok);
    if (ok) {
      tgles::TglDebugf("diag LinkProgram#%d prog=%u ok=1", n, program);
    } else {
      const std::string log = Rt().gl().programs().GetProgramInfoLog(program);
      GLint attached = 0;
      Rt().gl().programs().GetProgramiv(program, tgles::kGlAttachedShaders, &attached);
      tgles::TglDebugf("diag LinkProgram#%d prog=%u ok=0 attached=%d log=%.240s", n,
                       program, attached, log.c_str());
    }
  }
}

void glGetProgramiv(GLuint program, GLenum pname, GLint* params) {
  if (!HaveCurrent()) return;
  Rt().gl().programs().GetProgramiv(program, pname, params);
}

void glUseProgram(GLuint program) {
  if (!HaveCurrent()) {
    const int n = DiagP("use");
    if (n <= 8) tgles::TglDebugf("diag UseProgram no_current#%d prog=%u", n, program);
    return;
  }
  const int n = DiagP("use");
  if (n <= 48) {
    tgles::TglDebugf("diag UseProgram#%d prog=%u cur_before=%u", n, program,
              Rt().gl().programs().CurrentProgram());
  }
  // Uncapped: first use of each program, forever (bounded by program count).
  {
    static std::map<GLuint, bool> first_use_logged;
    if (!first_use_logged[program]) {
      first_use_logged[program] = true;
      tgles::TglDebugf("diag first_use prog=%u use_count=%d", program, n);
    }
  }
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
  const void* src = nullptr;
  if (!Rt().gl().ResolveTexelSource(pixels, width, height, 1, format, type,
                                    &src))
    return;
  Rt().gl().textures().TexImage2D(target, level, internalformat, width,
                                  height, border, format, type, src);
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
  // 0x0 (observed for the first ~70 frames of a session) turns every window
  // draw into a no-op while the clear still shows: log the value, on change,
  // so a screenshot can be matched against the viewport that produced it.
  static int vp_logged = 0;
  static GLint lx = 1, ly = 1, lw = 1, lh = 1;
  if (x != lx || y != ly || width != lw || height != lh) {
    lx = x;
    ly = y;
    lw = width;
    lh = height;
    if (vp_logged < 32) {
      ++vp_logged;
      tgles::TglDebugf("diag viewport#%d x=%d y=%d w=%d h=%d", vp_logged, x,
                       y, width, height);
    }
  }
}

void glDrawArrays(GLenum mode, GLint first, GLsizei count) {
  if (!HaveCurrent()) return;
  Rt().DrawArrays(mode, first, count);
}

GLenum glGetError(void) { return Rt().GetError(); }

}  // extern "C"
