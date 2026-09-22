// Real host ABI implementations: every entry point here is served by a state
// manager from src/state|pipeline|facade through HostRuntime. Anything not
// implemented here is a declared gap in gl_gap.cpp (see abi_ledger.h).
//
// Layout of this file follows the spec chapters: context (ch. 2, 20), buffer
// (6), shader/program (7), texture/sampler (8), framebuffer (9), vertex array
// and draw (10), raster (13-15), queries/sync (4), transform feedback (12),
// compute (11), debug (18), framebuffer pixels (17).

#include "tgles/host/abi_gl.h"

#include <cstring>
#include <string>

#include "tgles/base/gl_types.h"
#include "tgles/facade/gles.h"
#include "tgles/host/host_runtime.h"

namespace {

// The single process-wide host runtime (see host_runtime.h). Every entry point
// below funnels through it, so EGL make-current state and GL state can never
// disagree about which context a call belongs to.
tgles::HostRuntime& Host() { return tgles::HostRuntime::Instance(); }

tgles::GlesContext& Gl() { return Host().gl(); }

// GL calls outside a current context must not touch state (ES 3.2 spec 2.3.1
// plus the EGL 1.5 rule that a context is required for GL commands).
bool RequireContext() {
  if (Host().HasCurrent()) return true;
  Host().FlagNoContext();
  return false;
}

// Info-log adapter (spec 7.3/7.13): std::string -> (bufSize, length, infoLog).
// length always reports the full size (even for a NULL infoLog); infoLog is
// NUL-terminated within bufSize. A NULL length is legal and skips the write.
void CopyInfoLogOut(const std::string& log, GLsizei bufSize, GLsizei* length,
                    GLchar* infoLog) {
  if (length != nullptr) *length = static_cast<GLsizei>(log.size());
  if (infoLog != nullptr && bufSize > 0) {
    std::size_t n = log.size();
    if (n > static_cast<std::size_t>(bufSize - 1)) {
      n = static_cast<std::size_t>(bufSize - 1);
    }
    std::memcpy(infoLog, log.data(), n);
    infoLog[n] = '\0';
  }
}

// Sync-handle conversions: SyncManager names syncs with GLuint ids while the
// C ABI carries GLsync (a Khronos opaque pointer). Same void*-as-integer
// convention as the EGL layer in host_c_api.cpp.
GLsync ToSync(tgles::GLuint id) {
  return reinterpret_cast<GLsync>(static_cast<std::uintptr_t>(id));
}

tgles::GLuint FromSync(GLsync sync) {
  return static_cast<tgles::GLuint>(reinterpret_cast<std::uintptr_t>(sync));
}

}  // namespace

extern "C" {

// --- ES 3.2 spec chapter 20: server strings and version queries -------------
// These are the first GL calls every host makes after eglMakeCurrent (CTS F5,
// MobileGL bring-up, launchers showing the version). Served by Context
// (src/state/context.cpp); unknown names record INVALID_ENUM inside Context
// and return nullptr / leave params untouched, per spec 20.2-20.3.

const GLubyte* glGetString(GLenum name) {
  if (!RequireContext()) return nullptr;
  return Gl().foundation().GetString(name);
}

const GLubyte* glGetStringi(GLenum name, GLuint index) {
  if (!RequireContext()) return nullptr;
  return Gl().foundation().GetStringi(name, index);
}

void glGetIntegerv(GLenum pname, GLint* params) {
  if (!RequireContext()) return;
  Gl().foundation().GetIntegerv(pname, params);
}

// --- ES 3.2 spec 15.1.7 + 18: capability toggles ----------------------------
// Served by Context (src/state/context.cpp). Unknown caps record INVALID_ENUM
// inside Context and change nothing — the fail-closed rule CTS checks first.

void glEnable(GLenum cap) {
  if (!RequireContext()) return;
  Gl().foundation().Enable(cap);
  // BLEND is indexed (ES 3.2 §14.1): non-indexed Enable sets ALL draw
  // buffers (refpages: "for all or one draw buffer"). Mirror into the
  // per-buffer store so PsoKey planning (which ORs both) stays exact after
  // mixed Enable/Enablei sequences.
  if (cap == 0x0BE2u) {
    for (GLuint i = 0; i < 8; ++i) Gl().raster().EnableIndexed(cap, i);
  }
}

void glDisable(GLenum cap) {
  if (!RequireContext()) return;
  Gl().foundation().Disable(cap);
  if (cap == 0x0BE2u) {
    for (GLuint i = 0; i < 8; ++i) Gl().raster().DisableIndexed(cap, i);
  }
}

GLboolean glIsEnabled(GLenum cap) {
  if (!RequireContext()) return GL_FALSE;
  return Gl().foundation().IsEnabled(cap);
}

// --- ES 3.2 spec 17.3: clear values ------------------------------------------
// Served by RasterState (src/pipeline/raster.cpp). These set the values a
// later glClear(mask) consumes; the bridge applies them at pass start.

void glClearColor(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha) {
  if (!RequireContext()) return;
  Gl().raster().ClearColor(red, green, blue, alpha);
}

void glClearDepthf(GLfloat depth) {
  if (!RequireContext()) return;
  Gl().raster().ClearDepthf(depth);
}

void glClearStencil(GLint stencil) {
  if (!RequireContext()) return;
  Gl().raster().ClearStencil(stencil);
}

// --- Rasterization state (spec ch.13-15) -------------------------------------
// Served by RasterState (src/pipeline/raster.cpp), which validates every enum
// (bad factor/mode/func/face -> INVALID_ENUM, negative size -> INVALID_VALUE)
// and changes nothing on error. Wrappers only add the no-context guard.

void glBlendFunc(GLenum sfactor, GLenum dfactor) {
  if (!RequireContext()) return;
  Gl().raster().BlendFunc(sfactor, dfactor);
}

void glBlendEquation(GLenum mode) {
  if (!RequireContext()) return;
  Gl().raster().BlendEquation(mode);
}

void glDepthFunc(GLenum func) {
  if (!RequireContext()) return;
  Gl().raster().DepthFunc(func);
}

void glDepthMask(GLboolean flag) {
  if (!RequireContext()) return;
  Gl().raster().DepthMask(flag);
}

void glCullFace(GLenum mode) {
  if (!RequireContext()) return;
  Gl().raster().CullFace(mode);
}

void glFrontFace(GLenum mode) {
  if (!RequireContext()) return;
  Gl().raster().FrontFace(mode);
}

void glColorMask(GLboolean red, GLboolean green, GLboolean blue,
                 GLboolean alpha) {
  if (!RequireContext()) return;
  Gl().raster().ColorMask(red, green, blue, alpha);
}

void glScissor(GLint x, GLint y, GLsizei width, GLsizei height) {
  if (!RequireContext()) return;
  Gl().raster().Scissor(x, y, width, height);
}

// --- ES 3.2 spec 10.5: indexed draws ------------------------------------------
// Served by GlesContext::RenderElements through HostRuntime (validator +
// index-store decode + the same bridge submission as RenderFrame).

void glDrawElements(GLenum mode, GLsizei count, GLenum type,
                    const void* indices) {
  if (!RequireContext()) return;
  Host().DrawElements(mode, count, type,
                      reinterpret_cast<std::uintptr_t>(indices));
}

// --- Object lifecycle: Is* -----------------------------------------------
// Served by the owning manager; a dead/never-created name is GL_FALSE with no
// error (spec: Is* only errors via the no-context guard above).

GLboolean glIsBuffer(GLuint buffer) {
  if (!RequireContext()) return GL_FALSE;
  return Gl().buffers().IsBuffer(buffer);
}

GLboolean glIsFramebuffer(GLuint framebuffer) {
  if (!RequireContext()) return GL_FALSE;
  return Gl().framebuffers().IsFramebuffer(framebuffer);
}

GLboolean glIsProgram(GLuint program) {
  if (!RequireContext()) return GL_FALSE;
  return Gl().programs().IsProgram(program);
}

GLboolean glIsRenderbuffer(GLuint renderbuffer) {
  if (!RequireContext()) return GL_FALSE;
  return Gl().renderbuffers().IsRenderbuffer(renderbuffer);
}

GLboolean glIsShader(GLuint shader) {
  if (!RequireContext()) return GL_FALSE;
  return Gl().shaders().IsShader(shader);
}

GLboolean glIsTexture(GLuint texture) {
  if (!RequireContext()) return GL_FALSE;
  return Gl().textures().IsTexture(texture);
}

GLboolean glIsVertexArray(GLuint array) {
  if (!RequireContext()) return GL_FALSE;
  return Gl().vertex_arrays().IsVertexArray(array);
}

GLboolean glIsQuery(GLuint id) {
  if (!RequireContext()) return GL_FALSE;
  return Gl().queries().IsQuery(id);
}

GLboolean glIsSampler(GLuint sampler) {
  if (!RequireContext()) return GL_FALSE;
  return Gl().samplers().IsSampler(sampler);
}

GLboolean glIsSync(GLsync sync) {
  if (!RequireContext()) return GL_FALSE;
  return Gl().sync().IsSync(FromSync(sync));
}

GLboolean glIsProgramPipeline(GLuint pipeline) {
  if (!RequireContext()) return GL_FALSE;
  return Gl().programs().IsProgramPipeline(pipeline);
}

// --- Object lifecycle: Gen*/Delete* ----------------------------------------

void glGenQueries(GLsizei n, GLuint* ids) {
  if (!RequireContext()) return;
  Gl().queries().GenQueries(n, ids);
}

void glDeleteQueries(GLsizei n, const GLuint* ids) {
  if (!RequireContext()) return;
  Gl().queries().DeleteQueries(n, ids);
}

void glGenSamplers(GLsizei n, GLuint* samplers) {
  if (!RequireContext()) return;
  Gl().samplers().GenSamplers(n, samplers);
}

void glDeleteSamplers(GLsizei n, const GLuint* samplers) {
  if (!RequireContext()) return;
  Gl().samplers().DeleteSamplers(n, samplers);
}

void glGenProgramPipelines(GLsizei n, GLuint* pipelines) {
  if (!RequireContext()) return;
  Gl().programs().GenProgramPipelines(n, pipelines);
}

void glDeleteProgramPipelines(GLsizei n, const GLuint* pipelines) {
  if (!RequireContext()) return;
  Gl().programs().DeleteProgramPipelines(n, pipelines);
}

void glDeleteSync(GLsync sync) {
  if (!RequireContext()) return;
  Gl().sync().DeleteSync(FromSync(sync));
}

void glGenRenderbuffers(GLsizei n, GLuint* renderbuffers) {
  if (!RequireContext()) return;
  Gl().renderbuffers().GenRenderbuffers(n, renderbuffers);
}

void glDeleteRenderbuffers(GLsizei n, const GLuint* renderbuffers) {
  if (!RequireContext()) return;
  Gl().renderbuffers().DeleteRenderbuffers(n, renderbuffers);
}

// Deleting a name that was never created is a silent no-op; deleting a live
// object kills it (Is* goes FALSE). All managers own this rule already.

void glDeleteBuffers(GLsizei n, const GLuint* buffers) {
  if (!RequireContext()) return;
  Gl().buffers().DeleteBuffers(n, buffers);
}

void glDeleteTextures(GLsizei n, const GLuint* textures) {
  if (!RequireContext()) return;
  Gl().textures().DeleteTextures(n, textures);
}

void glDeleteFramebuffers(GLsizei n, const GLuint* framebuffers) {
  if (!RequireContext()) return;
  Gl().framebuffers().DeleteFramebuffers(n, framebuffers);
}

void glDeleteProgram(GLuint program) {
  if (!RequireContext()) return;
  Gl().programs().DeleteProgram(program);
}

void glDeleteShader(GLuint shader) {
  if (!RequireContext()) return;
  Gl().shaders().DeleteShader(shader);
}

void glDeleteVertexArrays(GLsizei n, const GLuint* arrays) {
  if (!RequireContext()) return;
  Gl().vertex_arrays().DeleteVertexArrays(n, arrays);
}

// NOTE: transform-feedback and pixel-store entries stay gaps: GlesContext
// owns no TransformFeedbackManager/PixelState member yet, so there is no
// manager to route to. Adding the facade member (+ GetError order audit) is
// tracked work, not a one-line route — see limits.md.

// --- Queries: begin/end drives the object model (spec 4.2) -------------------
// A generated id becomes an object on first Begin (IsQuery goes TRUE).

void glBeginQuery(GLenum target, GLuint id) {
  if (!RequireContext()) return;
  Gl().queries().BeginQuery(target, id);
}

void glEndQuery(GLenum target) {
  if (!RequireContext()) return;
  Gl().queries().EndQuery(target);
}

// --- Object lifecycle: Bind* (spec ch.6-9) -----------------------------------

void glBindSampler(GLuint unit, GLuint sampler) {
  if (!RequireContext()) return;
  Gl().samplers().BindSampler(unit, sampler);
}

void glBindProgramPipeline(GLuint pipeline) {
  if (!RequireContext()) return;
  Gl().programs().BindProgramPipeline(pipeline);
}

void glBindBufferBase(GLenum target, GLuint index, GLuint buffer) {
  if (!RequireContext()) return;
  Gl().buffers().BindBufferBase(target, index, buffer);
}

void glBindBufferRange(GLenum target, GLuint index, GLuint buffer,
                       GLintptr offset, GLsizeiptr size) {
  if (!RequireContext()) return;
  Gl().buffers().BindBufferRange(target, index, buffer, offset, size);
}

void glBindImageTexture(GLuint unit, GLuint texture, GLint level,
                        GLboolean layered, GLint layer, GLenum access,
                        GLenum format) {
  if (!RequireContext()) return;
  Gl().images().BindImageTexture(unit, texture, level, layered, layer,
                                 access, format);
}

void glBindAttribLocation(GLuint program, GLuint index, const GLchar* name) {
  if (!RequireContext()) return;
  Gl().programs().BindAttribLocation(program, index, name);
}

void glBindRenderbuffer(GLenum target, GLuint renderbuffer) {
  if (!RequireContext()) return;
  Gl().renderbuffers().BindRenderbuffer(target, renderbuffer);
}

// --- Buffers: upload paths beyond BufferData (spec ch.6) ---------------------

void glBufferSubData(GLenum target, GLintptr offset, GLsizeiptr size,
                     const void* data) {
  if (!RequireContext()) return;
  Gl().buffers().BufferSubData(target, offset, size, data);
}

void glCopyBufferSubData(GLenum read_target, GLenum write_target,
                         GLintptr read_offset, GLintptr write_offset,
                         GLsizeiptr size) {
  if (!RequireContext()) return;
  Gl().buffers().CopyBufferSubData(read_target, write_target, read_offset,
                                   write_offset, size);
}

void glGetBufferParameteriv(GLenum target, GLenum pname, GLint* params) {
  if (!RequireContext()) return;
  Gl().buffers().GetBufferParameteriv(target, pname, params);
}

void* glMapBufferRange(GLenum target, GLintptr offset, GLsizeiptr length,
                       GLbitfield access) {
  if (!RequireContext()) return nullptr;
  return Gl().buffers().MapBufferRange(target, offset, length, access);
}

GLboolean glUnmapBuffer(GLenum target) {
  if (!RequireContext()) return GL_FALSE;
  return Gl().buffers().UnmapBuffer(target);
}

void glFlushMappedBufferRange(GLenum target, GLintptr offset,
                              GLsizeiptr length) {
  if (!RequireContext()) return;
  Gl().buffers().FlushMappedBufferRange(target, offset, length);
}

void glBufferStorageEXT(GLenum target, GLsizeiptr size, const void* data,
                        GLbitfield flags) {
  if (!RequireContext()) return;
  Gl().buffers().BufferStorageEXT(target, size, data, flags);
}

// --- Sync objects (spec 4.1): GLuint ids across a GLsync pointer -------------

GLsync glFenceSync(GLenum condition, GLbitfield flags) {
  if (!RequireContext()) return nullptr;
  const tgles::GLuint id = Gl().sync().FenceSync(condition, flags);
  if (id == 0) return nullptr;  // Manager recorded the error (bad enum/...).
  return ToSync(id);
}

GLenum glClientWaitSync(GLsync sync, GLbitfield flags, GLuint64 timeout) {
  if (!RequireContext()) return tgles::kGlWaitFailed;
  return Gl().sync().ClientWaitSync(FromSync(sync), flags, timeout);
}

void glWaitSync(GLsync sync, GLbitfield flags, GLuint64 timeout) {
  if (!RequireContext()) return;
  Gl().sync().WaitSync(FromSync(sync), flags, timeout);
}

void glGetSynciv(GLsync sync, GLenum pname, GLsizei count, GLsizei* length,
                 GLint* values) {
  if (!RequireContext()) return;
  Gl().sync().GetSynciv(FromSync(sync), pname, count, length, values);
}

// --- Textures: storage/parameters/mipmap/buffer paths (spec ch.8) ------------
// (TexImage2D/3D/SubImage uploads live in the trial subset; pixel unpacking
// itself is TextureManager::UnpackToRgba8 with its own test file.)

void glActiveTexture(GLenum texture) {
  if (!RequireContext()) return;
  Gl().textures().ActiveTexture(texture);
}

void glTexStorage2D(GLenum target, GLsizei levels, GLenum internalformat,
                    GLsizei width, GLsizei height) {
  if (!RequireContext()) return;
  Gl().textures().TexStorage2D(target, levels, internalformat, width, height);
}

void glTexStorage3D(GLenum target, GLsizei levels, GLenum internalformat,
                    GLsizei width, GLsizei height, GLsizei depth) {
  if (!RequireContext()) return;
  Gl().textures().TexStorage3D(target, levels, internalformat, width, height,
                               depth);
}

void glTexParameteri(GLenum target, GLenum pname, GLint param) {
  if (!RequireContext()) return;
  Gl().textures().TexParameteri(target, pname, param);
}

void glTexParameterf(GLenum target, GLenum pname, GLfloat param) {
  if (!RequireContext()) return;
  Gl().textures().TexParameterf(target, pname, param);
}

void glTexParameterfv(GLenum target, GLenum pname, const GLfloat* params) {
  if (!RequireContext()) return;
  Gl().textures().TexParameterfv(target, pname, params);
}

void glGetTexParameteriv(GLenum target, GLenum pname, GLint* params) {
  if (!RequireContext()) return;
  Gl().textures().GetTexParameteriv(target, pname, params);
}

void glGetTexParameterfv(GLenum target, GLenum pname, GLfloat* params) {
  if (!RequireContext()) return;
  Gl().textures().GetTexParameterfv(target, pname, params);
}

void glGenerateMipmap(GLenum target) {
  if (!RequireContext()) return;
  Gl().textures().GenerateMipmap(target);
}

void glTexBuffer(GLenum target, GLenum internalformat, GLuint buffer) {
  if (!RequireContext()) return;
  Gl().textures().TexBuffer(target, internalformat, buffer);
}

void glTexBufferRange(GLenum target, GLenum internalformat, GLuint buffer,
                      GLintptr offset, GLsizeiptr size) {
  if (!RequireContext()) return;
  Gl().textures().TexBufferRange(target, internalformat, buffer, offset,
                                 size);
}

// EXT/OES_texture_buffer spellings: same validation and storage as core
// (MobileGL prefers the suffixed spelling per tier, core as fallback).
void glTexBufferEXT(GLenum target, GLenum internalformat, GLuint buffer) {
  if (!RequireContext()) return;
  Gl().textures().TexBuffer(target, internalformat, buffer);
}

void glTexBufferOES(GLenum target, GLenum internalformat, GLuint buffer) {
  if (!RequireContext()) return;
  Gl().textures().TexBuffer(target, internalformat, buffer);
}

void glTexBufferRangeEXT(GLenum target, GLenum internalformat, GLuint buffer,
                         GLintptr offset, GLsizeiptr size) {
  if (!RequireContext()) return;
  Gl().textures().TexBufferRange(target, internalformat, buffer, offset,
                                 size);
}

void glTexBufferRangeOES(GLenum target, GLenum internalformat, GLuint buffer,
                         GLintptr offset, GLsizeiptr size) {
  if (!RequireContext()) return;
  Gl().textures().TexBufferRange(target, internalformat, buffer, offset,
                                 size);
}

// EXT/OES_texture_view: alias entry points sharing the manager method (the
// spellings differ only in suffix; validation is identical).
void glTextureViewEXT(GLuint texture, GLenum target, GLuint origtexture,
                      GLenum internalformat, GLuint minlevel, GLuint numlevels,
                      GLuint minlayer, GLuint numlayers) {
  if (!RequireContext()) return;
  Gl().textures().TextureView(texture, target, origtexture, internalformat,
                              minlevel, numlevels, minlayer, numlayers);
}

void glTextureViewOES(GLuint texture, GLenum target, GLuint origtexture,
                      GLenum internalformat, GLuint minlevel, GLuint numlevels,
                      GLuint minlayer, GLuint numlayers) {
  if (!RequireContext()) return;
  Gl().textures().TextureView(texture, target, origtexture, internalformat,
                              minlevel, numlevels, minlayer, numlayers);
}

void glRenderbufferStorage(GLenum target, GLenum internalformat,
                           GLsizei width, GLsizei height) {
  if (!RequireContext()) return;
  Gl().renderbuffers().RenderbufferStorage(target, internalformat, width,
                                           height);
}

void glRenderbufferStorageMultisample(GLenum target, GLsizei samples,
                                      GLenum internalformat, GLsizei width,
                                      GLsizei height) {
  if (!RequireContext()) return;
  Gl().renderbuffers().RenderbufferStorageMultisample(target, samples,
                                                      internalformat, width,
                                                      height);
}

// --- Programs: float/int uniform setters (spec 7.3.1) ----------------------------
// Served by ProgramManager::SetUniformFloat (type-checked, -1 ignored,
// INVALID_OPERATION without a linked current program). Only the setter
// shapes the manager implements are routed; the rest stay gaps.

void glUniform1f(GLint location, GLfloat v0) {
  if (!RequireContext()) return;
  Gl().programs().Uniform1f(location, v0);
}

void glUniform4f(GLint location, GLfloat v0, GLfloat v1, GLfloat v2,
                 GLfloat v3) {
  if (!RequireContext()) return;
  Gl().programs().Uniform4f(location, v0, v1, v2, v3);
}

void glUniform1i(GLint location, GLint v0) {
  if (!RequireContext()) return;
  Gl().programs().Uniform1i(location, v0);
}

void glUniform1fv(GLint location, GLsizei count, const GLfloat* value) {
  if (!RequireContext()) return;
  Gl().programs().Uniform1fv(location, count, value);
}

// --- Programs/shaders: reflection + logs + detach/validate (spec ch.7) --------

void glDetachShader(GLuint program, GLuint shader) {
  if (!RequireContext()) return;
  Gl().programs().DetachShader(program, shader);
}

void glValidateProgram(GLuint program) {
  if (!RequireContext()) return;
  Gl().programs().ValidateProgram(program);
}

GLint glGetAttribLocation(GLuint program, const GLchar* name) {
  if (!RequireContext()) return -1;
  return Gl().programs().GetAttribLocation(program, name);
}

void glGetActiveAttrib(GLuint program, GLuint index, GLsizei bufSize,
                       GLsizei* length, GLint* size, GLenum* type,
                       GLchar* name) {
  if (!RequireContext()) return;
  Gl().programs().GetActiveAttrib(program, index, bufSize, length, size,
                                  type, name);
}

GLuint glGetUniformBlockIndex(GLuint program, const GLchar* uniformBlockName) {
  if (!RequireContext()) return tgles::kGlInvalidIndex;
  return Gl().programs().GetUniformBlockIndex(program, uniformBlockName);
}

void glUniformBlockBinding(GLuint program, GLuint uniformBlockIndex,
                           GLuint uniformBlockBinding) {
  if (!RequireContext()) return;
  Gl().programs().UniformBlockBinding(program, uniformBlockIndex,
                                      uniformBlockBinding);
}

void glGetProgramInfoLog(GLuint program, GLsizei bufSize, GLsizei* length,
                         GLchar* infoLog) {
  if (!RequireContext()) return;
  CopyInfoLogOut(Gl().programs().GetProgramInfoLog(program), bufSize, length,
                 infoLog);
}

void glGetShaderInfoLog(GLuint shader, GLsizei bufSize, GLsizei* length,
                        GLchar* infoLog) {
  if (!RequireContext()) return;
  CopyInfoLogOut(Gl().shaders().GetShaderInfoLog(shader), bufSize, length,
                 infoLog);
}

void glGetShaderSource(GLuint shader, GLsizei bufSize, GLsizei* length,
                       GLchar* source) {
  if (!RequireContext()) return;
  CopyInfoLogOut(Gl().shaders().GetShaderSource(shader), bufSize, length,
                 source);
}

// --- Debug output control (spec ch.18; filtering in DebugManager) -------------

void glDebugMessageControl(GLenum source, GLenum type, GLenum severity,
                           GLsizei count, const GLuint* ids,
                           GLboolean enabled) {
  if (!RequireContext()) return;
  Gl().debug().DebugMessageControl(source, type, severity, count, ids,
                                   enabled);
}

void glDebugMessageInsert(GLenum source, GLenum type, GLuint id,
                          GLenum severity, GLsizei length, const GLchar* buf) {
  if (!RequireContext()) return;
  Gl().debug().DebugMessageInsert(source, type, id, severity, length, buf);
}

void glDebugMessageCallback(GLDEBUGPROC callback, const void* userParam) {
  if (!RequireContext()) return;
  Gl().debug().SetCallback(reinterpret_cast<void*>(callback), userParam);
}

// --- Uniform setters, full family (spec 7.3.1) -----------------------------------
// Only the shapes ProgramManager implements are routed (float/int/uint
// scalars, vectors and arrays, all matrix shapes); the rest stay gaps. Every
// shape funnels into SetUniform*For, so validation lives in one place.

void glUniform2f(GLint location, GLfloat v0, GLfloat v1) {
  if (!RequireContext()) return;
  Gl().programs().Uniform2f(location, v0, v1);
}

void glUniform3f(GLint location, GLfloat v0, GLfloat v1, GLfloat v2) {
  if (!RequireContext()) return;
  Gl().programs().Uniform3f(location, v0, v1, v2);
}

void glUniform2i(GLint location, GLint v0, GLint v1) {
  if (!RequireContext()) return;
  Gl().programs().Uniform2i(location, v0, v1);
}

void glUniform3i(GLint location, GLint v0, GLint v1, GLint v2) {
  if (!RequireContext()) return;
  Gl().programs().Uniform3i(location, v0, v1, v2);
}

void glUniform4i(GLint location, GLint v0, GLint v1, GLint v2, GLint v3) {
  if (!RequireContext()) return;
  Gl().programs().Uniform4i(location, v0, v1, v2, v3);
}

void glUniform1ui(GLint location, GLuint v0) {
  if (!RequireContext()) return;
  Gl().programs().Uniform1ui(location, v0);
}

void glUniform2ui(GLint location, GLuint v0, GLuint v1) {
  if (!RequireContext()) return;
  Gl().programs().Uniform2ui(location, v0, v1);
}

void glUniform3ui(GLint location, GLuint v0, GLuint v1, GLuint v2) {
  if (!RequireContext()) return;
  Gl().programs().Uniform3ui(location, v0, v1, v2);
}

void glUniform4ui(GLint location, GLuint v0, GLuint v1, GLuint v2, GLuint v3) {
  if (!RequireContext()) return;
  Gl().programs().Uniform4ui(location, v0, v1, v2, v3);
}

void glUniform2fv(GLint location, GLsizei count, const GLfloat* value) {
  if (!RequireContext()) return;
  Gl().programs().Uniform2fv(location, count, value);
}

void glUniform3fv(GLint location, GLsizei count, const GLfloat* value) {
  if (!RequireContext()) return;
  Gl().programs().Uniform3fv(location, count, value);
}

void glUniform2iv(GLint location, GLsizei count, const GLint* value) {
  if (!RequireContext()) return;
  Gl().programs().Uniform2iv(location, count, value);
}

void glUniform3iv(GLint location, GLsizei count, const GLint* value) {
  if (!RequireContext()) return;
  Gl().programs().Uniform3iv(location, count, value);
}

void glUniform4iv(GLint location, GLsizei count, const GLint* value) {
  if (!RequireContext()) return;
  Gl().programs().Uniform4iv(location, count, value);
}

void glUniform1uiv(GLint location, GLsizei count, const GLuint* value) {
  if (!RequireContext()) return;
  Gl().programs().Uniform1uiv(location, count, value);
}

void glUniform2uiv(GLint location, GLsizei count, const GLuint* value) {
  if (!RequireContext()) return;
  Gl().programs().Uniform2uiv(location, count, value);
}

void glUniform3uiv(GLint location, GLsizei count, const GLuint* value) {
  if (!RequireContext()) return;
  Gl().programs().Uniform3uiv(location, count, value);
}

void glUniform4uiv(GLint location, GLsizei count, const GLuint* value) {
  if (!RequireContext()) return;
  Gl().programs().Uniform4uiv(location, count, value);
}

void glUniformMatrix2fv(GLint location, GLsizei count, GLboolean transpose,
                        const GLfloat* value) {
  if (!RequireContext()) return;
  Gl().programs().UniformMatrix2fv(location, count, transpose, value);
}

void glUniformMatrix3fv(GLint location, GLsizei count, GLboolean transpose,
                        const GLfloat* value) {
  if (!RequireContext()) return;
  Gl().programs().UniformMatrix3fv(location, count, transpose, value);
}

void glUniformMatrix2x3fv(GLint location, GLsizei count, GLboolean transpose,
                          const GLfloat* value) {
  if (!RequireContext()) return;
  Gl().programs().UniformMatrix2x3fv(location, count, transpose, value);
}

void glUniformMatrix3x2fv(GLint location, GLsizei count, GLboolean transpose,
                          const GLfloat* value) {
  if (!RequireContext()) return;
  Gl().programs().UniformMatrix3x2fv(location, count, transpose, value);
}

void glUniformMatrix2x4fv(GLint location, GLsizei count, GLboolean transpose,
                          const GLfloat* value) {
  if (!RequireContext()) return;
  Gl().programs().UniformMatrix2x4fv(location, count, transpose, value);
}

void glUniformMatrix4x2fv(GLint location, GLsizei count, GLboolean transpose,
                          const GLfloat* value) {
  if (!RequireContext()) return;
  Gl().programs().UniformMatrix4x2fv(location, count, transpose, value);
}

void glUniformMatrix3x4fv(GLint location, GLsizei count, GLboolean transpose,
                          const GLfloat* value) {
  if (!RequireContext()) return;
  Gl().programs().UniformMatrix3x4fv(location, count, transpose, value);
}

void glUniformMatrix4x3fv(GLint location, GLsizei count, GLboolean transpose,
                          const GLfloat* value) {
  if (!RequireContext()) return;
  Gl().programs().UniformMatrix4x3fv(location, count, transpose, value);
}

// --- ProgramUniform*, full family (spec 7.3.1, explicit program) ----------------

void glProgramUniform1f(GLuint program, GLint location, GLfloat v0) {
  if (!RequireContext()) return;
  Gl().programs().ProgramUniform1f(program, location, v0);
}

void glProgramUniform2f(GLuint program, GLint location, GLfloat v0,
                        GLfloat v1) {
  if (!RequireContext()) return;
  Gl().programs().ProgramUniform2f(program, location, v0, v1);
}

void glProgramUniform3f(GLuint program, GLint location, GLfloat v0, GLfloat v1,
                        GLfloat v2) {
  if (!RequireContext()) return;
  Gl().programs().ProgramUniform3f(program, location, v0, v1, v2);
}

void glProgramUniform4f(GLuint program, GLint location, GLfloat v0, GLfloat v1,
                        GLfloat v2, GLfloat v3) {
  if (!RequireContext()) return;
  Gl().programs().ProgramUniform4f(program, location, v0, v1, v2, v3);
}

void glProgramUniform1i(GLuint program, GLint location, GLint v0) {
  if (!RequireContext()) return;
  Gl().programs().ProgramUniform1i(program, location, v0);
}

void glProgramUniform2i(GLuint program, GLint location, GLint v0, GLint v1) {
  if (!RequireContext()) return;
  Gl().programs().ProgramUniform2i(program, location, v0, v1);
}

void glProgramUniform3i(GLuint program, GLint location, GLint v0, GLint v1,
                        GLint v2) {
  if (!RequireContext()) return;
  Gl().programs().ProgramUniform3i(program, location, v0, v1, v2);
}

void glProgramUniform4i(GLuint program, GLint location, GLint v0, GLint v1,
                        GLint v2, GLint v3) {
  if (!RequireContext()) return;
  Gl().programs().ProgramUniform4i(program, location, v0, v1, v2, v3);
}

void glProgramUniform1ui(GLuint program, GLint location, GLuint v0) {
  if (!RequireContext()) return;
  Gl().programs().ProgramUniform1ui(program, location, v0);
}

void glProgramUniform2ui(GLuint program, GLint location, GLuint v0,
                         GLuint v1) {
  if (!RequireContext()) return;
  Gl().programs().ProgramUniform2ui(program, location, v0, v1);
}

void glProgramUniform3ui(GLuint program, GLint location, GLuint v0, GLuint v1,
                         GLuint v2) {
  if (!RequireContext()) return;
  Gl().programs().ProgramUniform3ui(program, location, v0, v1, v2);
}

void glProgramUniform4ui(GLuint program, GLint location, GLuint v0, GLuint v1,
                         GLuint v2, GLuint v3) {
  if (!RequireContext()) return;
  Gl().programs().ProgramUniform4ui(program, location, v0, v1, v2, v3);
}

void glProgramUniform1fv(GLuint program, GLint location, GLsizei count,
                         const GLfloat* value) {
  if (!RequireContext()) return;
  Gl().programs().ProgramUniform1fv(program, location, count, value);
}

void glProgramUniform2fv(GLuint program, GLint location, GLsizei count,
                         const GLfloat* value) {
  if (!RequireContext()) return;
  Gl().programs().ProgramUniform2fv(program, location, count, value);
}

void glProgramUniform3fv(GLuint program, GLint location, GLsizei count,
                         const GLfloat* value) {
  if (!RequireContext()) return;
  Gl().programs().ProgramUniform3fv(program, location, count, value);
}

void glProgramUniform4fv(GLuint program, GLint location, GLsizei count,
                         const GLfloat* value) {
  if (!RequireContext()) return;
  Gl().programs().ProgramUniform4fv(program, location, count, value);
}

void glProgramUniform1iv(GLuint program, GLint location, GLsizei count,
                         const GLint* value) {
  if (!RequireContext()) return;
  Gl().programs().ProgramUniform1iv(program, location, count, value);
}

void glProgramUniform2iv(GLuint program, GLint location, GLsizei count,
                         const GLint* value) {
  if (!RequireContext()) return;
  Gl().programs().ProgramUniform2iv(program, location, count, value);
}

void glProgramUniform3iv(GLuint program, GLint location, GLsizei count,
                         const GLint* value) {
  if (!RequireContext()) return;
  Gl().programs().ProgramUniform3iv(program, location, count, value);
}

void glProgramUniform4iv(GLuint program, GLint location, GLsizei count,
                         const GLint* value) {
  if (!RequireContext()) return;
  Gl().programs().ProgramUniform4iv(program, location, count, value);
}

void glProgramUniform1uiv(GLuint program, GLint location, GLsizei count,
                          const GLuint* value) {
  if (!RequireContext()) return;
  Gl().programs().ProgramUniform1uiv(program, location, count, value);
}

void glProgramUniform2uiv(GLuint program, GLint location, GLsizei count,
                          const GLuint* value) {
  if (!RequireContext()) return;
  Gl().programs().ProgramUniform2uiv(program, location, count, value);
}

void glProgramUniform3uiv(GLuint program, GLint location, GLsizei count,
                          const GLuint* value) {
  if (!RequireContext()) return;
  Gl().programs().ProgramUniform3uiv(program, location, count, value);
}

void glProgramUniform4uiv(GLuint program, GLint location, GLsizei count,
                          const GLuint* value) {
  if (!RequireContext()) return;
  Gl().programs().ProgramUniform4uiv(program, location, count, value);
}

void glProgramUniformMatrix2fv(GLuint program, GLint location, GLsizei count,
                               GLboolean transpose, const GLfloat* value) {
  if (!RequireContext()) return;
  Gl().programs().ProgramUniformMatrix2fv(program, location, count, transpose,
                                          value);
}

void glProgramUniformMatrix3fv(GLuint program, GLint location, GLsizei count,
                               GLboolean transpose, const GLfloat* value) {
  if (!RequireContext()) return;
  Gl().programs().ProgramUniformMatrix3fv(program, location, count, transpose,
                                          value);
}

void glProgramUniformMatrix4fv(GLuint program, GLint location, GLsizei count,
                               GLboolean transpose, const GLfloat* value) {
  if (!RequireContext()) return;
  Gl().programs().ProgramUniformMatrix4fv(program, location, count, transpose,
                                          value);
}

void glProgramUniformMatrix2x3fv(GLuint program, GLint location, GLsizei count,
                                 GLboolean transpose, const GLfloat* value) {
  if (!RequireContext()) return;
  Gl().programs().ProgramUniformMatrix2x3fv(program, location, count,
                                            transpose, value);
}

void glProgramUniformMatrix3x2fv(GLuint program, GLint location, GLsizei count,
                                 GLboolean transpose, const GLfloat* value) {
  if (!RequireContext()) return;
  Gl().programs().ProgramUniformMatrix3x2fv(program, location, count,
                                            transpose, value);
}

void glProgramUniformMatrix2x4fv(GLuint program, GLint location, GLsizei count,
                                 GLboolean transpose, const GLfloat* value) {
  if (!RequireContext()) return;
  Gl().programs().ProgramUniformMatrix2x4fv(program, location, count,
                                            transpose, value);
}

void glProgramUniformMatrix4x2fv(GLuint program, GLint location, GLsizei count,
                                 GLboolean transpose, const GLfloat* value) {
  if (!RequireContext()) return;
  Gl().programs().ProgramUniformMatrix4x2fv(program, location, count,
                                            transpose, value);
}

void glProgramUniformMatrix3x4fv(GLuint program, GLint location, GLsizei count,
                                 GLboolean transpose, const GLfloat* value) {
  if (!RequireContext()) return;
  Gl().programs().ProgramUniformMatrix3x4fv(program, location, count,
                                            transpose, value);
}

void glProgramUniformMatrix4x3fv(GLuint program, GLint location, GLsizei count,
                                 GLboolean transpose, const GLfloat* value) {
  if (!RequireContext()) return;
  Gl().programs().ProgramUniformMatrix4x3fv(program, location, count,
                                            transpose, value);
}

// --- Uniform reads (spec 7.12): free conversion, zeros when unset -------------

void glGetUniformfv(GLuint program, GLint location, GLfloat* params) {
  if (!RequireContext()) return;
  if (params == nullptr) {
    Host().FlagError(tgles::kGlInvalidValue);
    return;
  }
  std::vector<float> values;
  if (!Gl().programs().GetUniformFloats(program, location, &values)) return;
  for (std::size_t i = 0; i < values.size(); ++i) params[i] = values[i];
}

void glGetUniformiv(GLuint program, GLint location, GLint* params) {
  if (!RequireContext()) return;
  if (params == nullptr) {
    Host().FlagError(tgles::kGlInvalidValue);
    return;
  }
  std::vector<float> values;
  if (!Gl().programs().GetUniformFloats(program, location, &values)) return;
  for (std::size_t i = 0; i < values.size(); ++i) {
    params[i] = static_cast<GLint>(values[i]);
  }
}

void glGetUniformuiv(GLuint program, GLint location, GLuint* params) {
  if (!RequireContext()) return;
  if (params == nullptr) {
    Host().FlagError(tgles::kGlInvalidValue);
    return;
  }
  std::vector<float> values;
  if (!Gl().programs().GetUniformFloats(program, location, &values)) return;
  for (std::size_t i = 0; i < values.size(); ++i) {
    params[i] = static_cast<GLuint>(values[i]);
  }
}

void glGetnUniformfv(GLuint program, GLint location, GLsizei bufSize,
                     GLfloat* params) {
  if (!RequireContext()) return;
  if (bufSize < 0 || (bufSize > 0 && params == nullptr)) {
    Host().FlagError(tgles::kGlInvalidValue);
    return;
  }
  std::vector<float> values;
  if (!Gl().programs().GetUniformFloats(program, location, &values)) return;
  // bufSize counts ELEMENTS (floats), not bytes: safe under both caller
  // conventions (a byte-count is always >= the element count for these
  // types) — see the header note on GetUniformFloats.
  if (bufSize < static_cast<GLsizei>(values.size())) {
    Host().FlagError(tgles::kGlInvalidOperation);
    return;
  }
  for (std::size_t i = 0; i < values.size(); ++i) params[i] = values[i];
}

void glGetnUniformiv(GLuint program, GLint location, GLsizei bufSize,
                     GLint* params) {
  if (!RequireContext()) return;
  if (bufSize < 0 || (bufSize > 0 && params == nullptr)) {
    Host().FlagError(tgles::kGlInvalidValue);
    return;
  }
  std::vector<float> values;
  if (!Gl().programs().GetUniformFloats(program, location, &values)) return;
  if (bufSize < static_cast<GLsizei>(values.size())) {
    Host().FlagError(tgles::kGlInvalidOperation);
    return;
  }
  for (std::size_t i = 0; i < values.size(); ++i) {
    params[i] = static_cast<GLint>(values[i]);
  }
}

void glGetnUniformuiv(GLuint program, GLint location, GLsizei bufSize,
                      GLuint* params) {
  if (!RequireContext()) return;
  if (bufSize < 0 || (bufSize > 0 && params == nullptr)) {
    Host().FlagError(tgles::kGlInvalidValue);
    return;
  }
  std::vector<float> values;
  if (!Gl().programs().GetUniformFloats(program, location, &values)) return;
  if (bufSize < static_cast<GLsizei>(values.size())) {
    Host().FlagError(tgles::kGlInvalidOperation);
    return;
  }
  for (std::size_t i = 0; i < values.size(); ++i) {
    params[i] = static_cast<GLuint>(values[i]);
  }
}

// --- Reflection reads (spec 7.3.2/7.12, grounded in the link parse) ------------

void glGetUniformIndices(GLuint program, GLsizei uniformCount,
                         const GLchar* const* uniformNames, GLuint* indices) {
  if (!RequireContext()) return;
  Gl().programs().GetUniformIndices(program, uniformCount, uniformNames,
                                    indices);
}

void glGetActiveUniformsiv(GLuint program, GLsizei uniformCount,
                           const GLuint* uniformIndices, GLenum pname,
                           GLint* params) {
  if (!RequireContext()) return;
  Gl().programs().GetActiveUniformsiv(program, uniformCount, uniformIndices,
                                      pname, params);
}

void glGetActiveUniformBlockName(GLuint program, GLuint uniformBlockIndex,
                                 GLsizei bufSize, GLsizei* length,
                                 GLchar* uniformBlockName) {
  if (!RequireContext()) return;
  Gl().programs().GetActiveUniformBlockName(program, uniformBlockIndex,
                                            bufSize, length, uniformBlockName);
}

void glGetActiveUniformBlockiv(GLuint program, GLuint uniformBlockIndex,
                               GLenum pname, GLint* params) {
  if (!RequireContext()) return;
  Gl().programs().GetActiveUniformBlockiv(program, uniformBlockIndex, pname,
                                          params);
}

void glGetProgramInterfaceiv(GLuint program, GLenum programInterface,
                             GLenum pname, GLint* params) {
  if (!RequireContext()) return;
  Gl().programs().GetProgramInterfaceiv(program, programInterface, pname,
                                        params);
}

void glGetProgramResourceName(GLuint program, GLenum programInterface,
                              GLuint index, GLsizei bufSize, GLsizei* length,
                              GLchar* name) {
  if (!RequireContext()) return;
  Gl().programs().GetProgramResourceName(program, programInterface, index,
                                         bufSize, length, name);
}

GLint glGetProgramResourceLocation(GLuint program, GLenum programInterface,
                                   const GLchar* name) {
  if (!RequireContext()) return -1;
  return Gl().programs().GetProgramResourceLocation(program, programInterface,
                                                    name);
}

void glGetProgramResourceiv(GLuint program, GLenum programInterface,
                            GLuint index, GLsizei propCount,
                            const GLenum* props, GLsizei bufSize,
                            GLsizei* length, GLint* params) {
  if (!RequireContext()) return;
  Gl().programs().GetProgramResourceiv(program, programInterface, index,
                                       propCount, props, bufSize, length,
                                       params);
}

void glGetAttachedShaders(GLuint program, GLsizei maxCount, GLsizei* count,
                          GLuint* shaders) {
  if (!RequireContext()) return;
  Gl().programs().GetAttachedShaders(program, maxCount, count, shaders);
}

GLint glGetFragDataLocation(GLuint program, const GLchar* name) {
  if (!RequireContext()) return -1;
  return Gl().programs().GetFragDataLocation(program, name);
}

GLuint glCreateShaderProgramv(GLenum type, GLsizei count,
                              const GLchar* const* strings) {
  if (!RequireContext()) return 0;
  return Gl().programs().CreateShaderProgramv(type, count, strings);
}

// --- Flush/finish (spec 2.3.2): HostRuntime owns the fence ring --------------

void glFinish(void) { Host().Finish(); }

void glFlush(void) { Host().Flush(); }

// --- Framebuffers: attachments, completeness, buffers (spec ch.9) -------------
// Served by RenderbufferManager/FramebufferManager. Completeness follows spec
// 9.4 (the same rules tests/state/test_step05_framebuffers.cpp pins).

void glFramebufferTextureLayer(GLenum target, GLenum attachment,
                               GLuint texture, GLint level, GLint layer) {
  if (!RequireContext()) return;
  Gl().framebuffers().FramebufferTextureLayer(target, attachment, texture,
                                              level, layer);
}

void glFramebufferRenderbuffer(GLenum target, GLenum attachment,
                               GLenum renderbuffertarget, GLuint renderbuffer) {
  if (!RequireContext()) return;
  Gl().framebuffers().FramebufferRenderbuffer(target, attachment,
                                              renderbuffertarget, renderbuffer);
}

void glFramebufferParameteri(GLenum target, GLenum pname, GLint param) {
  if (!RequireContext()) return;
  Gl().framebuffers().FramebufferParameteri(target, pname, param);
}

GLenum glCheckFramebufferStatus(GLenum target) {
  if (!RequireContext()) return 0;
  return Gl().framebuffers().CheckFramebufferStatus(target);
}

void glDrawBuffers(GLsizei n, const GLenum* bufs) {
  if (!RequireContext()) return;
  Gl().framebuffers().DrawBuffers(n, bufs);
}

void glReadBuffer(GLenum src) {
  if (!RequireContext()) return;
  Gl().framebuffers().ReadBuffer(src);
}

void glBlitFramebuffer(GLint src_x0, GLint src_y0, GLint src_x1, GLint src_y1,
                       GLint dst_x0, GLint dst_y0, GLint dst_x1, GLint dst_y1,
                       GLbitfield mask, GLenum filter) {
  if (!RequireContext()) return;
  Gl().framebuffers().BlitFramebuffer(src_x0, src_y0, src_x1, src_y1, dst_x0,
                                      dst_y0, dst_x1, dst_y1, mask, filter);
}

void glInvalidateFramebuffer(GLenum target, GLsizei num_attachments,
                             const GLenum* attachments) {
  if (!RequireContext()) return;
  Gl().framebuffers().InvalidateFramebuffer(target, num_attachments,
                                            attachments);
}

void glGetFramebufferAttachmentParameteriv(GLenum target, GLenum attachment,
                                           GLenum pname, GLint* params) {
  if (!RequireContext()) return;
  Gl().framebuffers().GetFramebufferAttachmentParameteriv(target, attachment,
                                                          pname, params);
}

// --- Queries: active queries and results (spec ch.4 + Table 4.2) ---------------

void glGetQueryiv(GLenum target, GLenum pname, GLint* params) {
  if (!RequireContext()) return;
  Gl().queries().GetQueryiv(target, pname, params);
}

void glGetQueryObjectuiv(GLuint id, GLenum pname, GLuint* params) {
  if (!RequireContext()) return;
  Gl().queries().GetQueryObjectuiv(id, pname, params);
}

// --- Samplers: parameters (spec 8.2; binding lives in the section above) --------

void glSamplerParameteri(GLuint sampler, GLenum pname, GLint param) {
  if (!RequireContext()) return;
  Gl().samplers().SamplerParameteri(sampler, pname, param);
}

void glSamplerParameterf(GLuint sampler, GLenum pname, GLfloat param) {
  if (!RequireContext()) return;
  Gl().samplers().SamplerParameterf(sampler, pname, param);
}

void glSamplerParameterfv(GLuint sampler, GLenum pname,
                          const GLfloat* params) {
  if (!RequireContext()) return;
  Gl().samplers().SamplerParameterfv(sampler, pname, params);
}

// --- Sampler queries (spec 8.2) --------------------------------------------------

void glGetSamplerParameteriv(GLuint sampler, GLenum pname, GLint* params) {
  if (!RequireContext()) return;
  Gl().samplers().GetSamplerParameteriv(sampler, pname, params);
}

void glGetSamplerParameterfv(GLuint sampler, GLenum pname, GLfloat* params) {
  if (!RequireContext()) return;
  Gl().samplers().GetSamplerParameterfv(sampler, pname, params);
}

// --- Programs: binaries, parameters, stages, reflection (spec ch.7) ------------
// Reflection reads the link-time parse (uniforms/attribs/blocks); the
// RenderFrame MVP lookup already depends on it, so these are the same
// ground truth the draw path uses.

void glGetProgramBinary(GLuint program, GLsizei bufSize, GLsizei* length,
                        GLenum* binaryFormat, void* binary) {
  if (!RequireContext()) return;
  Gl().programs().GetProgramBinary(program, bufSize, length, binaryFormat,
                                   binary);
}

void glProgramBinary(GLuint program, GLenum binaryFormat, const void* binary,
                     GLsizei length) {
  if (!RequireContext()) return;
  Gl().programs().ProgramBinary(program, binaryFormat, binary, length);
}

void glProgramParameteri(GLuint program, GLenum pname, GLint value) {
  if (!RequireContext()) return;
  Gl().programs().ProgramParameteri(program, pname, value);
}

void glUseProgramStages(GLuint pipeline, GLbitfield stages, GLuint program) {
  if (!RequireContext()) return;
  Gl().programs().UseProgramStages(pipeline, stages, program);
}

void glActiveShaderProgram(GLuint pipeline, GLuint program) {
  if (!RequireContext()) return;
  Gl().programs().ActiveShaderProgram(pipeline, program);
}

GLuint glGetProgramResourceIndex(GLuint program, GLenum programInterface,
                                 const GLchar* name) {
  if (!RequireContext()) return tgles::kGlInvalidIndex;
  return Gl().programs().GetProgramResourceIndex(program, programInterface,
                                                 name);
}

void glGetActiveUniform(GLuint program, GLuint index, GLsizei bufSize,
                        GLsizei* length, GLint* size, GLenum* type,
                        GLchar* name) {
  if (!RequireContext()) return;
  Gl().programs().GetActiveUniform(program, index, bufSize, length, size,
                                   type, name);
}

// --- Textures: framebuffer-sourced uploads (spec ch.8) --------------------------

void glCopyTexSubImage2D(GLenum target, GLint level, GLint xoffset,
                         GLint yoffset, GLint x, GLint y, GLsizei width,
                         GLsizei height) {
  if (!RequireContext()) return;
  Gl().textures().CopyTexSubImage2D(target, level, xoffset, yoffset, x, y,
                                    width, height);
}

// Desktop-only source copy: 1D copy targets never existed in ES (same rule
// as glTexImage1D above).
void glCopyTexImage1D(GLenum target, GLint level, GLenum internalformat,
                      GLint x, GLint y, GLsizei width, GLint border) {
  if (!RequireContext()) return;
  (void)target;
  (void)level;
  (void)internalformat;
  (void)x;
  (void)y;
  (void)width;
  (void)border;
  Host().FlagError(tgles::kGlInvalidEnum);
}

// --- Raster: constant color + sample mask (spec ch.13-14) -----------------------

void glBlendColor(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha) {
  if (!RequireContext()) return;
  Gl().raster().BlendColor(red, green, blue, alpha);
}

void glSampleMaski(GLuint maskNumber, GLbitfield mask) {
  if (!RequireContext()) return;
  Gl().raster().SampleMaski(maskNumber, mask);
}

// --- Vertex-array integer/divisor attributes (spec 10.3) --------------------------
// Pointer-shaped calls split like the trial-subset VertexAttribPointer: the
// client pointer is an offset into the currently bound ARRAY_BUFFER.

void glVertexAttribIPointer(GLuint index, GLint size, GLenum type,
                            GLsizei stride, const void* pointer) {
  if (!RequireContext()) return;
  const GLuint bound =
      Gl().buffers().BoundBuffer(tgles::kGlArrayBuffer);
  Gl().vertex_arrays().VertexAttribIPointer(
      index, size, type, stride,
      reinterpret_cast<std::uintptr_t>(pointer), bound);
}

void glVertexAttrib4f(GLuint index, GLfloat x, GLfloat y, GLfloat z,
                      GLfloat w) {
  if (!RequireContext()) return;
  Gl().vertex_arrays().VertexAttrib4f(index, x, y, z, w);
}

void glVertexAttribDivisor(GLuint index, GLuint divisor) {
  if (!RequireContext()) return;
  Gl().vertex_arrays().VertexAttribDivisor(index, divisor);
}

// --- Vertex-array attribute toggles (spec 10.3; pointer setup is above) --------

void glDisableVertexAttribArray(GLuint index) {
  if (!RequireContext()) return;
  Gl().vertex_arrays().DisableVertexAttribArray(index);
}

// --- Debug: groups, log retrieval, labels (spec ch.18) --------------------------

void glPushDebugGroup(GLenum source, GLuint id, GLsizei length,
                      const GLchar* message) {
  if (!RequireContext()) return;
  Gl().debug().PushDebugGroup(source, id, length, message);
}

void glPopDebugGroup(void) {
  if (!RequireContext()) return;
  Gl().debug().PopDebugGroup();
}

GLuint glGetDebugMessageLog(GLuint count, GLsizei bufSize, GLenum* sources,
                            GLenum* types, GLuint* ids, GLenum* severities,
                            GLsizei* lengths, GLchar* messageLog) {
  if (!RequireContext()) return 0;
  return Gl().debug().GetDebugMessageLog(count, bufSize, sources, types, ids,
                                         severities, lengths, messageLog);
}

void glObjectLabel(GLenum identifier, GLuint name, GLsizei length,
                   const GLchar* label) {
  if (!RequireContext()) return;
  Gl().debug().ObjectLabel(identifier, name, length, label);
}

void glGetObjectLabel(GLenum identifier, GLuint name, GLsizei bufSize,
                      GLsizei* length, GLchar* label) {
  if (!RequireContext()) return;
  CopyInfoLogOut(Gl().debug().GetObjectLabel(identifier, name), bufSize,
                 length, label);
}

// --- Sync: 64-bit server query (spec 4.1; fences are above) ---------------------

void glGetInteger64v(GLenum pname, GLint64* params) {
  if (!RequireContext()) return;
  Gl().sync().GetInteger64v(pname, params);
}

// --- Foundation boolean/float mirrors (spec 20.2, same table as iv) -------------

void glGetBooleanv(GLenum pname, GLboolean* data) {
  if (!RequireContext()) return;
  Gl().foundation().GetBooleanv(pname, data);
}

void glGetFloatv(GLenum pname, GLfloat* data) {
  if (!RequireContext()) return;
  Gl().foundation().GetFloatv(pname, data);
}

void glGetBooleani_v(GLenum target, GLuint index, GLboolean* data) {
  if (!RequireContext()) return;
  Gl().raster().GetBooleani_v(target, index, data);
}

void glGetInteger64i_v(GLenum target, GLuint index, GLint64* data) {
  if (!RequireContext()) return;
  if (data == nullptr) {
    Host().FlagError(tgles::kGlInvalidValue);
    return;
  }
  // Only the compute work-group counts are modeled (spec 19 + the
  // ComputeState table); the adapter widens the 32-bit answer.
  GLint v = 0;
  Gl().compute().GetIntegeri_v(target, index, &v);
  if (Host().GetError() != tgles::kGlNoError) return;
  *data = static_cast<GLint64>(v);
}

void glGetBufferParameteri64v(GLenum target, GLenum pname, GLint64* params) {
  if (!RequireContext()) return;
  if (params == nullptr) {
    Host().FlagError(tgles::kGlInvalidValue);
    return;
  }
  GLint v = 0;
  Gl().buffers().GetBufferParameteriv(target, pname, &v);
  if (Host().GetError() != tgles::kGlNoError) return;
  *params = static_cast<GLint64>(v);
}

void* glMapBufferOES(GLenum target, GLenum access) {
  if (!RequireContext()) return nullptr;
  // Whole-buffer map expressed through MapBufferRange (spec OES_mapbuffer
  // semantics: READ_ONLY/WRITE_ONLY/READ_WRITE over [0, size)).
  GLbitfield bits = 0;
  if (access == tgles::kGlReadOnly) {
    bits = tgles::kGlMapReadBit;
  } else if (access == tgles::kGlWriteOnly) {
    bits = tgles::kGlMapWriteBit;
  } else if (access == tgles::kGlReadWrite) {
    bits = tgles::kGlMapReadBit | tgles::kGlMapWriteBit;
  } else {
    Host().FlagError(tgles::kGlInvalidEnum);
    return nullptr;
  }
  const tgles::GLuint bound = Gl().buffers().BoundBuffer(target);
  if (bound == 0) {
    Host().FlagError(tgles::kGlInvalidOperation);
    return nullptr;
  }
  return Gl().buffers().MapBufferRange(target, 0, Gl().buffers().BufferSize(bound), bits);
}

// --- Shader precision + reset status: static answers ----------------------------
// Precision table from the GLSL ES 3.20 spec section 4.7.1 (vendored PDF):
// TGL stores every scalar at full width (float32/int32, see gl_types.h), so
// every qualifier reports the IEEE single / int32 rows. shadertype is still
// validated (only VERTEX/FRAGMENT exist per spec 7.12).

void glGetShaderPrecisionFormat(GLenum shaderType, GLenum precisionType,
                                GLint* range, GLint* precision) {
  if (!RequireContext()) return;
  if (shaderType != tgles::kGlVertexShader &&
      shaderType != tgles::kGlFragmentShader) {
    Host().FlagError(tgles::kGlInvalidEnum);
    return;
  }
  switch (precisionType) {
    case 0x8DF0u:  // LOW_FLOAT
    case 0x8DF1u:  // MEDIUM_FLOAT
    case 0x8DF2u:  // HIGH_FLOAT
      if (range != nullptr) {
        range[0] = 127;
        range[1] = 127;
      }
      if (precision != nullptr) *precision = 23;
      return;
    case 0x8DF3u:  // LOW_INT
    case 0x8DF4u:  // MEDIUM_INT
    case 0x8DF5u:  // HIGH_INT
      if (range != nullptr) {
        range[0] = 31;
        range[1] = 30;
      }
      if (precision != nullptr) *precision = 0;
      return;
    default:
      break;
  }
  Host().FlagError(tgles::kGlInvalidEnum);
}

GLenum glGetGraphicsResetStatus(void) {
  if (!RequireContext()) return tgles::kGlNoError;
  // No reset machinery exists in the CPU model (nothing to lose); NO_ERROR
  // is the truthful answer, not a placeholder.
  return tgles::kGlNoError;
}

// --- Generic vertex attributes: setters + reads (spec 10.3.1) ---------------------

void glVertexAttrib1f(GLuint index, GLfloat x) {
  if (!RequireContext()) return;
  Gl().vertex_arrays().VertexAttrib1f(index, x);
}

void glVertexAttrib2f(GLuint index, GLfloat x, GLfloat y) {
  if (!RequireContext()) return;
  Gl().vertex_arrays().VertexAttrib2f(index, x, y);
}

void glVertexAttrib3f(GLuint index, GLfloat x, GLfloat y, GLfloat z) {
  if (!RequireContext()) return;
  Gl().vertex_arrays().VertexAttrib3f(index, x, y, z);
}

void glVertexAttrib1fv(GLuint index, const GLfloat* v) {
  if (!RequireContext()) return;
  Gl().vertex_arrays().VertexAttrib1fv(index, v);
}

void glVertexAttrib2fv(GLuint index, const GLfloat* v) {
  if (!RequireContext()) return;
  Gl().vertex_arrays().VertexAttrib2fv(index, v);
}

void glVertexAttrib3fv(GLuint index, const GLfloat* v) {
  if (!RequireContext()) return;
  Gl().vertex_arrays().VertexAttrib3fv(index, v);
}

void glVertexAttrib4fv(GLuint index, const GLfloat* v) {
  if (!RequireContext()) return;
  Gl().vertex_arrays().VertexAttrib4fv(index, v);
}

void glVertexAttribI4i(GLuint index, GLint x, GLint y, GLint z, GLint w) {
  if (!RequireContext()) return;
  Gl().vertex_arrays().VertexAttribI4i(index, x, y, z, w);
}

void glVertexAttribI4ui(GLuint index, GLuint x, GLuint y, GLuint z, GLuint w) {
  if (!RequireContext()) return;
  Gl().vertex_arrays().VertexAttribI4ui(index, x, y, z, w);
}

void glVertexAttribI4iv(GLuint index, const GLint* v) {
  if (!RequireContext()) return;
  Gl().vertex_arrays().VertexAttribI4iv(index, v);
}

void glVertexAttribI4uiv(GLuint index, const GLuint* v) {
  if (!RequireContext()) return;
  Gl().vertex_arrays().VertexAttribI4uiv(index, v);
}

void glGetVertexAttribfv(GLuint index, GLenum pname, GLfloat* params) {
  if (!RequireContext()) return;
  Gl().vertex_arrays().GetVertexAttribfv(index, pname, params);
}

void glGetVertexAttribiv(GLuint index, GLenum pname, GLint* params) {
  if (!RequireContext()) return;
  Gl().vertex_arrays().GetVertexAttribiv(index, pname, params);
}

void glGetVertexAttribIiv(GLuint index, GLenum pname, GLint* params) {
  if (!RequireContext()) return;
  Gl().vertex_arrays().GetVertexAttribIiv(index, pname, params);
}

void glGetVertexAttribIuiv(GLuint index, GLenum pname, GLuint* params) {
  if (!RequireContext()) return;
  Gl().vertex_arrays().GetVertexAttribIuiv(index, pname, params);
}

void glGetVertexAttribPointerv(GLuint index, GLenum pname, void** pointer) {
  if (!RequireContext()) return;
  Gl().vertex_arrays().GetVertexAttribPointerv(index, pname, pointer);
}

// --- Texture level queries + integer reads (spec 8.10/8.11) -----------------------

void glGetTexLevelParameteriv(GLenum target, GLint level, GLenum pname,
                              GLint* params) {
  if (!RequireContext()) return;
  Gl().textures().GetTexLevelParameteriv(target, level, pname, params);
}

void glGetTexLevelParameterfv(GLenum target, GLint level, GLenum pname,
                              GLfloat* params) {
  if (!RequireContext()) return;
  Gl().textures().GetTexLevelParameterfv(target, level, pname, params);
}

void glGetTexParameterIiv(GLenum target, GLenum pname, GLint* params) {
  if (!RequireContext()) return;
  Gl().textures().GetTexParameterIiv(target, pname, params);
}

void glGetTexParameterIuiv(GLenum target, GLenum pname, GLuint* params) {
  if (!RequireContext()) return;
  Gl().textures().GetTexParameterIuiv(target, pname, params);
}

// --- Sampler integer transfers (spec 8.2) ------------------------------------------

void glSamplerParameterIiv(GLuint sampler, GLenum pname, const GLint* param) {
  if (!RequireContext()) return;
  Gl().samplers().SamplerParameterIiv(sampler, pname, param);
}

void glSamplerParameterIuiv(GLuint sampler, GLenum pname, const GLuint* param) {
  if (!RequireContext()) return;
  Gl().samplers().SamplerParameterIuiv(sampler, pname, param);
}

void glGetSamplerParameterIiv(GLuint sampler, GLenum pname, GLint* params) {
  if (!RequireContext()) return;
  Gl().samplers().GetSamplerParameterIiv(sampler, pname, params);
}

void glGetSamplerParameterIuiv(GLuint sampler, GLenum pname, GLuint* params) {
  if (!RequireContext()) return;
  Gl().samplers().GetSamplerParameterIuiv(sampler, pname, params);
}

// --- Renderbuffer + default-framebuffer queries (spec ch.9) -------------------------

void glGetRenderbufferParameteriv(GLenum target, GLenum pname, GLint* params) {
  if (!RequireContext()) return;
  Gl().renderbuffers().GetRenderbufferParameteriv(target, pname, params);
}

void glGetFramebufferParameteriv(GLenum target, GLenum pname, GLint* params) {
  if (!RequireContext()) return;
  Gl().framebuffers().GetFramebufferParameteriv(target, pname, params);
}

// --- Textures: sub-uploads, layered images, desktop-only names (ch.8) ----------

void glTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset,
                     GLsizei width, GLsizei height, GLenum format, GLenum type,
                     const void* pixels) {
  if (!RequireContext()) return;
  Gl().textures().TexSubImage2D(target, level, xoffset, yoffset, width,
                                height, format, type, pixels);
}

void glTexSubImage3D(GLenum target, GLint level, GLint xoffset, GLint yoffset,
                     GLint zoffset, GLsizei width, GLsizei height,
                     GLsizei depth, GLenum format, GLenum type,
                     const void* pixels) {
  if (!RequireContext()) return;
  Gl().textures().TexSubImage3D(target, level, xoffset, yoffset, zoffset,
                                width, height, depth, format, type, pixels);
}

void glTexImage3D(GLenum target, GLint level, GLint internalformat,
                  GLsizei width, GLsizei height, GLsizei depth, GLint border,
                  GLenum format, GLenum type, const void* pixels) {
  if (!RequireContext()) return;
  Gl().textures().TexImage3D(target, level, internalformat, width, height,
                             depth, border, format, type, pixels);
}

void glCopyImageSubData(GLuint srcName, GLenum srcTarget, GLint srcLevel,
                        GLint srcX, GLint srcY, GLint srcZ, GLuint dstName,
                        GLenum dstTarget, GLint dstLevel, GLint dstX,
                        GLint dstY, GLint dstZ, GLsizei srcWidth,
                        GLsizei srcHeight, GLsizei srcDepth) {
  if (!RequireContext()) return;
  Gl().textures().CopyImageSubData(srcName, srcTarget, srcLevel, srcX, srcY,
                                   srcZ, dstName, dstTarget, dstLevel, dstX,
                                   dstY, dstZ, srcWidth, srcHeight, srcDepth);
}

// Desktop-only leftovers the loader still requires by name: 1D textures never
// existed in ES, so the target is always INVALID_ENUM here (the loader itself
// never issues them; see tools/mobilegl/host_contract.json provenance).
void glTexImage1D(GLenum target, GLint level, GLint internalformat,
                  GLsizei width, GLint border, GLenum format, GLenum type,
                  const void* pixels) {
  if (!RequireContext()) return;
  (void)target;
  (void)level;
  (void)internalformat;
  (void)width;
  (void)border;
  (void)format;
  (void)type;
  (void)pixels;
  Host().FlagError(tgles::kGlInvalidEnum);
}

void glTexStorage1D(GLenum target, GLsizei levels, GLenum internalformat,
                    GLsizei width) {
  if (!RequireContext()) return;
  (void)target;
  (void)levels;
  (void)internalformat;
  (void)width;
  Host().FlagError(tgles::kGlInvalidEnum);
}

// --- Blend/stencil separate + indexed state (spec ch.13-15) --------------------
// Same RasterState validators as the batch-2 setters, per-buffer variants
// included (spec 14.1 indexed state).

void glBlendFuncSeparate(GLenum srcRGB, GLenum dstRGB, GLenum srcAlpha,
                         GLenum dstAlpha) {
  if (!RequireContext()) return;
  Gl().raster().BlendFuncSeparate(srcRGB, dstRGB, srcAlpha, dstAlpha);
}

void glBlendEquationSeparate(GLenum modeRGB, GLenum modeAlpha) {
  if (!RequireContext()) return;
  Gl().raster().BlendEquationSeparate(modeRGB, modeAlpha);
}

void glBlendEquationi(GLuint buf, GLenum mode) {
  if (!RequireContext()) return;
  Gl().raster().BlendEquationi(buf, mode);
}

void glBlendFunci(GLuint buf, GLenum src, GLenum dst) {
  if (!RequireContext()) return;
  Gl().raster().BlendFunci(buf, src, dst);
}

void glBlendFuncSeparatei(GLuint buf, GLenum srcRGB, GLenum dstRGB,
                          GLenum srcAlpha, GLenum dstAlpha) {
  if (!RequireContext()) return;
  Gl().raster().BlendFuncSeparatei(buf, srcRGB, dstRGB, srcAlpha, dstAlpha);
}

void glBlendEquationSeparatei(GLuint buf, GLenum modeRGB, GLenum modeAlpha) {
  if (!RequireContext()) return;
  Gl().raster().BlendEquationSeparatei(buf, modeRGB, modeAlpha);
}

void glColorMaski(GLuint index, GLboolean r, GLboolean g, GLboolean b,
                  GLboolean a) {
  if (!RequireContext()) return;
  Gl().raster().ColorMaski(index, r, g, b, a);
}

// EXT/OES_color_buffer_indexed spellings: same per-buffer masks as core.
void glColorMaskiEXT(GLuint index, GLboolean r, GLboolean g, GLboolean b,
                     GLboolean a) {
  if (!RequireContext()) return;
  Gl().raster().ColorMaski(index, r, g, b, a);
}

void glColorMaskiOES(GLuint index, GLboolean r, GLboolean g, GLboolean b,
                     GLboolean a) {
  if (!RequireContext()) return;
  Gl().raster().ColorMaski(index, r, g, b, a);
}

// NV/ANGLE_polygon_mode: TGL rasterizes filled only, so FILL is an honest
// no-op while LINE/POINT fail closed (INVALID_OPERATION, like other
// valid-enum-but-unexecutable state). Bad face/mode enums stay INVALID_ENUM
// via the host queue, matching the desktop rule MobileGL validates.
void glPolygonModeNV(GLenum face, GLenum mode) {
  if (!RequireContext()) return;
  if (face != 0x0404u && face != 0x0405u && face != 0x0408u) {
    Host().FlagError(tgles::kGlInvalidEnum);
    return;
  }
  if (mode != 0x1B00u && mode != 0x1B01u && mode != 0x1B02u) {
    Host().FlagError(tgles::kGlInvalidEnum);
    return;
  }
  if (mode != 0x1B02u) {  // FILL executes; LINE/POINT do not.
    Host().FlagError(tgles::kGlInvalidOperation);
  }
}

void glPolygonModeANGLE(GLenum face, GLenum mode) {
  if (!RequireContext()) return;
  glPolygonModeNV(face, mode);
}

void glStencilFunc(GLenum func, GLint ref, GLuint mask) {
  if (!RequireContext()) return;
  Gl().raster().StencilFunc(func, ref, mask);
}

void glStencilFuncSeparate(GLenum face, GLenum func, GLint ref, GLuint mask) {
  if (!RequireContext()) return;
  Gl().raster().StencilFuncSeparate(face, func, ref, mask);
}

void glStencilOp(GLenum sfail, GLenum dpfail, GLenum dppass) {
  if (!RequireContext()) return;
  Gl().raster().StencilOp(sfail, dpfail, dppass);
}

void glStencilOpSeparate(GLenum face, GLenum sfail, GLenum dpfail,
                         GLenum dppass) {
  if (!RequireContext()) return;
  Gl().raster().StencilOpSeparate(face, sfail, dpfail, dppass);
}

void glStencilMask(GLuint mask) {
  if (!RequireContext()) return;
  Gl().raster().StencilMask(mask);
}

void glStencilMaskSeparate(GLenum face, GLuint mask) {
  if (!RequireContext()) return;
  Gl().raster().StencilMaskSeparate(face, mask);
}

void glSampleCoverage(GLfloat value, GLboolean invert) {
  if (!RequireContext()) return;
  Gl().raster().SampleCoverage(value, invert);
}

void glDepthRangef(GLfloat n, GLfloat f) {
  if (!RequireContext()) return;
  Gl().raster().DepthRangef(n, f);
}

void glLineWidth(GLfloat width) {
  if (!RequireContext()) return;
  Gl().raster().LineWidth(width);
}

void glPolygonOffset(GLfloat factor, GLfloat units) {
  if (!RequireContext()) return;
  Gl().raster().PolygonOffset(factor, units);
}

// --- Tessellation patch size (spec 11.1) ----------------------------------------

void glPatchParameteri(GLenum pname, GLint value) {
  if (!RequireContext()) return;
  Gl().tessellation().PatchParameteri(pname, value);
}

// --- Compute dispatches (spec ch.19; state in ComputeState) ---------------------

void glDispatchCompute(GLuint num_groups_x, GLuint num_groups_y,
                       GLuint num_groups_z) {
  if (!RequireContext()) return;
  Gl().SyncComputeProgram();
  Gl().compute().DispatchCompute(num_groups_x, num_groups_y, num_groups_z);
}

void glDispatchComputeIndirect(GLintptr indirect) {
  if (!RequireContext()) return;
  Gl().DispatchComputeIndirect(static_cast<std::uintptr_t>(indirect));
}

void glMemoryBarrier(GLbitfield barriers) {
  if (!RequireContext()) return;
  Gl().compute().MemoryBarrier(barriers);
}

void glMemoryBarrierByRegion(GLbitfield barriers) {
  if (!RequireContext()) return;
  Gl().compute().MemoryBarrierByRegion(barriers);
}

// --- Indexed integer queries with a manager today -------------------------------
// ComputeState answers its two GetIntegeri_v pnames; anything else is
// INVALID_ENUM there (full indexed-query tables are tracked work).

void glGetIntegeri_v(GLenum target, GLuint index, GLint* data) {
  if (!RequireContext()) return;
  Gl().compute().GetIntegeri_v(target, index, data);
}

// --- Hints: a hint may be ignored, but never misspelled (spec 21.5) -------------
// TGL keeps no hint state (nothing changes behaviour), so valid pairs are a
// checked no-op and anything else is INVALID_ENUM.

void glHint(GLenum target, GLenum mode) {
  if (!RequireContext()) return;
  switch (target) {
    case 0x0C50:  // PERSPECTIVE_CORRECTION_HINT
    case 0x0C51:  // POINT_SMOOTH_HINT
    case 0x0C52:  // LINE_SMOOTH_HINT
    case 0x0C53:  // POLYGON_SMOOTH_HINT
    case 0x84EF:  // TEXTURE_COMPRESSION_HINT
    case 0x8BFB:  // FRAGMENT_SHADER_DERIVATIVE_HINT
      break;
    default:
      Host().FlagError(tgles::kGlInvalidEnum);
      return;
  }
  switch (mode) {
    case 0x1100:  // DONT_CARE
    case 0x1101:  // FASTEST
    case 0x1102:  // NICEST
      break;
    default:
      Host().FlagError(tgles::kGlInvalidEnum);
      return;
  }
}

// --- Shader compiler release: a resource hint with no state ---------------------
// Spec 7.2: "The implementation may ... free resources"; freeing nothing is
// a legal response, so this is a checked no-op (never an error).

void glReleaseShaderCompiler(void) {
  if (!RequireContext()) return;
}

// --- Clear values into the framebuffer store (spec 17.3) ------------------------
// GlesContext::Clear owns the mask/attachment rules (see its doc); the two
// ClearBuffer* float/int entry points below share the same store fill.

void glClear(GLbitfield mask) {
  if (!RequireContext()) return;
  Gl().Clear(mask);
}

void glClearBufferfv(GLenum buffer, GLint drawbuffer, const GLfloat* value) {
  if (!RequireContext()) return;
  // COLOR drawbuffer 0 with 4 floats is the implemented path (spec 17.3);
  // depth/stencil buffers and other drawbuffers are tracked work.
  if (buffer != 0x1800 /*COLOR*/ || drawbuffer != 0) {
    Host().FlagError(tgles::kGlInvalidEnum);
    return;
  }
  if (value == nullptr) {
    Host().FlagError(tgles::kGlInvalidValue);
    return;
  }
  Gl().raster().ClearColor(value[0], value[1], value[2], value[3]);
  Gl().Clear(0x00004000u /*COLOR_BUFFER_BIT*/);
}

void glClearBufferfi(GLenum buffer, GLint drawbuffer, GLfloat depth,
                     GLint stencil) {
  if (!RequireContext()) return;
  if (buffer != 0x84F9u /*DEPTH_STENCIL*/ || drawbuffer != 0) {
    Host().FlagError(tgles::kGlInvalidEnum);
    return;
  }
  Gl().raster().ClearDepthf(depth);
  Gl().raster().ClearStencil(stencil);
  Gl().Clear(0x00000100u /*DEPTH_BUFFER_BIT*/ |
             0x00000400u /*STENCIL_BUFFER_BIT*/);
}

// NOTE: glClearBufferiv/uiv stay gaps: integer clears need integer-attachment
// conversion rules the CPU store does not model yet (tracked work, not a
// silent no-op — the ledger still measures every call).

// --- Sot batch: Uniform1iv + Uniform4fv (manager already had the logic) -----
// ProgramManager::Uniform1iv was declared but never defined/routed; Uniform4fv
// was defined but never routed. Both validated against docs.gl/es3 uniform
// rules via SetUniform*For (same as the rest of the family).

void glUniform1iv(GLint location, GLsizei count, const GLint* value) {
  if (!RequireContext()) return;
  Gl().programs().Uniform1iv(location, count, value);
}

void glUniform4fv(GLint location, GLsizei count, const GLfloat* value) {
  if (!RequireContext()) return;
  Gl().programs().Uniform4fv(location, count, value);
}

// --- Sot batch: TexParameteriv/Iiv/Iuiv + SamplerParameteriv (spec 8.2/8.10) -

void glTexParameteriv(GLenum target, GLenum pname, const GLint* params) {
  if (!RequireContext()) return;
  Gl().textures().TexParameteriv(target, pname, params);
}

void glTexParameterIiv(GLenum target, GLenum pname, const GLint* params) {
  if (!RequireContext()) return;
  Gl().textures().TexParameterIiv(target, pname, params);
}

void glTexParameterIuiv(GLenum target, GLenum pname, const GLuint* params) {
  if (!RequireContext()) return;
  Gl().textures().TexParameterIuiv(target, pname, params);
}

void glSamplerParameteriv(GLuint sampler, GLenum pname, const GLint* param) {
  if (!RequireContext()) return;
  Gl().samplers().SamplerParameteriv(sampler, pname, param);
}

// --- Get batch: buffer pointer + internalformat + multisample (docs.gl/es3) --

void glGetBufferPointerv(GLenum target, GLenum pname, void** params) {
  if (!RequireContext()) return;
  Gl().buffers().GetBufferPointerv(target, pname, params);
}

void glGetInternalformativ(GLenum target, GLenum internalformat, GLenum pname,
                            GLsizei bufSize, GLint* params) {
  if (!RequireContext()) return;
  Gl().renderbuffers().GetInternalformativ(target, internalformat, pname,
                                           bufSize, params);
}

void glGetMultisamplefv(GLenum pname, GLuint index, GLfloat* val) {
  if (!RequireContext()) return;
  Gl().raster().GetMultisamplefv(pname, index, val);
}

// --- Get batch: debug pointer queries (spec 18, gl32.h 1525-1526) ------------

void glGetPointerv(GLenum pname, void** params) {
  if (!RequireContext()) return;
  Gl().debug().GetPointerv(pname, params);
}

void glObjectPtrLabel(const void* ptr, GLsizei length, const GLchar* label) {
  if (!RequireContext()) return;
  Gl().debug().ObjectPtrLabel(ptr, length, label);
}

void glGetObjectPtrLabel(const void* ptr, GLsizei bufSize, GLsizei* length,
                         GLchar* label) {
  if (!RequireContext()) return;
  CopyInfoLogOut(Gl().debug().GetObjectPtrLabel(ptr), bufSize, length, label);
}

// --- Get batch: program pipeline queries (spec 7.4) --------------------------

void glGetProgramPipelineiv(GLuint pipeline, GLenum pname, GLint* params) {
  if (!RequireContext()) return;
  Gl().programs().GetProgramPipelineiv(pipeline, pname, params);
}

void glGetProgramPipelineInfoLog(GLuint pipeline, GLsizei bufSize,
                                 GLsizei* length, GLchar* infoLog) {
  if (!RequireContext()) return;
  CopyInfoLogOut(Gl().programs().GetProgramPipelineInfoLog(pipeline), bufSize,
                 length, infoLog);
}

void glValidateProgramPipeline(GLuint pipeline) {
  if (!RequireContext()) return;
  Gl().programs().ValidateProgramPipeline(pipeline);
}

// --- Get batch: EXT timer queries (glext.h, EXT_disjoint_timer_query) --------

void glQueryCounterEXT(GLuint id, GLenum target) {
  if (!RequireContext()) return;
  Gl().queries().QueryCounterEXT(id, target);
}

void glGetQueryObjectivEXT(GLuint id, GLenum pname, GLint* params) {
  if (!RequireContext()) return;
  Gl().queries().GetQueryObjectivEXT(id, pname, params);
}

void glGetQueryObjecti64vEXT(GLuint id, GLenum pname, GLint64* params) {
  if (!RequireContext()) return;
  Gl().queries().GetQueryObjecti64vEXT(id, pname, params);
}

void glGetQueryObjectui64vEXT(GLuint id, GLenum pname, GLuint64* params) {
  if (!RequireContext()) return;
  Gl().queries().GetQueryObjectui64vEXT(id, pname, params);
}

// --- Program batch: transform-feedback varyings + SSBO + frag bindings ------

void glTransformFeedbackVaryings(GLuint program, GLsizei count,
                                 const GLchar* const* varyings,
                                 GLenum bufferMode) {
  if (!RequireContext()) return;
  Gl().programs().TransformFeedbackVaryings(program, count, varyings,
                                            bufferMode);
}

void glGetTransformFeedbackVarying(GLuint program, GLuint index,
                                   GLsizei bufSize, GLsizei* length,
                                   GLsizei* size, GLenum* type, GLchar* name) {
  if (!RequireContext()) return;
  Gl().programs().GetTransformFeedbackVarying(program, index, bufSize, length,
                                              size, type, name);
}

void glShaderStorageBlockBinding(GLuint program, GLuint storageBlockIndex,
                                 GLuint storageBlockBinding) {
  if (!RequireContext()) return;
  Gl().programs().ShaderStorageBlockBinding(program, storageBlockIndex,
                                            storageBlockBinding);
}

void glBindFragDataLocationEXT(GLuint program, GLuint color,
                               const GLchar* name) {
  if (!RequireContext()) return;
  Gl().programs().BindFragDataLocationEXT(program, color, name);
}

// --- VertexAttrib separate-attribute batch (ES 3.1+, docs.gl/es3) ------------

void glBindVertexBuffer(GLuint bindingindex, GLuint buffer, GLintptr offset,
                        GLsizei stride) {
  if (!RequireContext()) return;
  if (offset < 0) {
    Host().FlagError(tgles::kGlInvalidValue);
    return;
  }
  Gl().vertex_arrays().BindVertexBuffer(
      bindingindex, buffer, static_cast<std::uintptr_t>(offset), stride);
}

void glVertexAttribFormat(GLuint attribindex, GLint size, GLenum type,
                          GLboolean normalized, GLuint relativeoffset) {
  if (!RequireContext()) return;
  Gl().vertex_arrays().VertexAttribFormat(attribindex, size, type, normalized,
                                          relativeoffset);
}

void glVertexAttribIFormat(GLuint attribindex, GLint size, GLenum type,
                           GLuint relativeoffset) {
  if (!RequireContext()) return;
  Gl().vertex_arrays().VertexAttribIFormat(attribindex, size, type,
                                           relativeoffset);
}

void glVertexAttribBinding(GLuint attribindex, GLuint bindingindex) {
  if (!RequireContext()) return;
  Gl().vertex_arrays().VertexAttribBinding(attribindex, bindingindex);
}

void glVertexBindingDivisor(GLuint bindingindex, GLuint divisor) {
  if (!RequireContext()) return;
  Gl().vertex_arrays().VertexBindingDivisor(bindingindex, divisor);
}

// --- Draw batch: instanced/indirect/range/base-vertex (spec 10.5) ------------
// Validation lives in DrawValidator (already unit-tested); execution goes
// through HostRuntime so headless (no bridge) validates only and Metal
// executes TRIANGLES via GlesContext expansion.

void glDrawArraysInstanced(GLenum mode, GLint first, GLsizei count,
                           GLsizei instancecount) {
  if (!RequireContext()) return;
  Host().DrawArraysInstanced(mode, first, count, instancecount);
}

void glDrawElementsInstanced(GLenum mode, GLsizei count, GLenum type,
                             const void* indices, GLsizei instancecount) {
  if (!RequireContext()) return;
  Host().DrawElementsInstanced(mode, count, type,
                               reinterpret_cast<std::uintptr_t>(indices),
                               instancecount);
}

void glDrawArraysIndirect(GLenum mode, const void* indirect) {
  if (!RequireContext()) return;
  Host().DrawArraysIndirect(mode, reinterpret_cast<std::uintptr_t>(indirect));
}

void glDrawElementsIndirect(GLenum mode, GLenum type, const void* indirect) {
  if (!RequireContext()) return;
  Host().DrawElementsIndirect(mode, type,
                              reinterpret_cast<std::uintptr_t>(indirect));
}

void glDrawRangeElements(GLenum mode, GLuint start, GLuint end, GLsizei count,
                         GLenum type, const void* indices) {
  if (!RequireContext()) return;
  Host().DrawRangeElements(mode, start, end, count, type,
                           reinterpret_cast<std::uintptr_t>(indices));
}

void glDrawElementsBaseVertex(GLenum mode, GLsizei count, GLenum type,
                              const void* indices, GLint basevertex) {
  if (!RequireContext()) return;
  Host().DrawElementsBaseVertex(mode, count, type,
                                reinterpret_cast<std::uintptr_t>(indices),
                                basevertex);
}

void glDrawRangeElementsBaseVertex(GLenum mode, GLuint start, GLuint end,
                                   GLsizei count, GLenum type,
                                   const void* indices, GLint basevertex) {
  if (!RequireContext()) return;
  Host().DrawRangeElementsBaseVertex(mode, start, end, count, type,
                                     reinterpret_cast<std::uintptr_t>(indices),
                                     basevertex);
}

void glDrawElementsInstancedBaseVertex(GLenum mode, GLsizei count, GLenum type,
                                       const void* indices,
                                       GLsizei instancecount,
                                       GLint basevertex) {
  if (!RequireContext()) return;
  Host().DrawElementsInstancedBaseVertex(
      mode, count, type, reinterpret_cast<std::uintptr_t>(indices),
      instancecount, basevertex);
}

// --- EXT_base_instance / EXT_multi_draw_indirect (optional in the MobileGL
// loader, implemented for real): base-instance shifts instance ids for
// divisor stepping; multidraw loops validated single draws (draws before a
// mid-list failure stand, per spec).
void glDrawArraysInstancedBaseInstanceEXT(GLenum mode, GLint first,
                                          GLsizei count, GLsizei instancecount,
                                          GLuint baseinstance) {
  if (!RequireContext()) return;
  Host().DrawArraysInstancedBaseInstance(mode, first, count, instancecount,
                                         baseinstance);
}

void glDrawElementsInstancedBaseInstanceEXT(GLenum mode, GLsizei count,
                                            GLenum type, const void* indices,
                                            GLsizei instancecount,
                                            GLuint baseinstance) {
  if (!RequireContext()) return;
  Host().DrawElementsInstancedBaseInstance(
      mode, count, type, reinterpret_cast<std::uintptr_t>(indices),
      instancecount, baseinstance);
}

void glDrawElementsInstancedBaseVertexBaseInstanceEXT(
    GLenum mode, GLsizei count, GLenum type, const void* indices,
    GLsizei instancecount, GLint basevertex, GLuint baseinstance) {
  if (!RequireContext()) return;
  Host().DrawElementsInstancedBaseVertexBaseInstance(
      mode, count, type, reinterpret_cast<std::uintptr_t>(indices),
      instancecount, basevertex, baseinstance);
}

void glMultiDrawArraysIndirectEXT(GLenum mode, const void* indirect,
                                  GLsizei drawcount, GLsizei stride) {
  if (!RequireContext()) return;
  Host().MultiDrawArraysIndirect(mode,
                                 reinterpret_cast<std::uintptr_t>(indirect),
                                 drawcount, stride);
}

void glMultiDrawElementsIndirectEXT(GLenum mode, GLenum type,
                                    const void* indirect, GLsizei drawcount,
                                    GLsizei stride) {
  if (!RequireContext()) return;
  Host().MultiDrawElementsIndirect(
      mode, type, reinterpret_cast<std::uintptr_t>(indirect), drawcount,
      stride);
}

void glMultiDrawElementsBaseVertexEXT(GLenum mode, const GLsizei* count,
                                      GLenum type, const void* const* indices,
                                      GLsizei drawcount,
                                      const GLint* basevertex) {
  if (!RequireContext()) return;
  Host().MultiDrawElementsBaseVertex(mode, count, type, indices, drawcount,
                                     basevertex);
}

// --- Pixel store + TransformFeedback object batch (managers pre-existed) -----
// PixelState::PixelStorei and TransformFeedbackManager::* were implemented
// with full validation but had no facade member / ABI route (sot found by
// auditing gl_gap.cpp against src/state/*.cpp). Routing is one line each.

void glPixelStorei(GLenum pname, GLint param) {
  if (!RequireContext()) return;
  Gl().pixels().PixelStorei(pname, param);
}

void glGenTransformFeedbacks(GLsizei n, GLuint* ids) {
  if (!RequireContext()) return;
  Gl().transform_feedback().GenTransformFeedbacks(n, ids);
}

void glDeleteTransformFeedbacks(GLsizei n, const GLuint* ids) {
  if (!RequireContext()) return;
  Gl().transform_feedback().DeleteTransformFeedbacks(n, ids);
}

GLboolean glIsTransformFeedback(GLuint id) {
  if (!RequireContext()) return GL_FALSE;
  return Gl().transform_feedback().IsTransformFeedback(id);
}

void glBindTransformFeedback(GLenum target, GLuint id) {
  if (!RequireContext()) return;
  Gl().transform_feedback().BindTransformFeedback(target, id);
}

void glBeginTransformFeedback(GLenum primitiveMode) {
  if (!RequireContext()) return;
  Gl().transform_feedback().BeginTransformFeedback(primitiveMode);
}

void glEndTransformFeedback(void) {
  if (!RequireContext()) return;
  Gl().transform_feedback().EndTransformFeedback();
}

void glPauseTransformFeedback(void) {
  if (!RequireContext()) return;
  Gl().transform_feedback().PauseTransformFeedback();
}

void glResumeTransformFeedback(void) {
  if (!RequireContext()) return;
  Gl().transform_feedback().ResumeTransformFeedback();
}


// --- Final23: compressed/copy tex (spec 8.7/8.10, docs.gl/es3 verified) ------

void glCompressedTexImage2D(GLenum target, GLint level, GLenum internalformat,
                            GLsizei width, GLsizei height, GLint border,
                            GLsizei imageSize, const void *data) {
  if (!RequireContext()) return;
  Gl().textures().CompressedTexImage2D(target, level, internalformat, width,
                                       height, border, imageSize, data);
}

void glCompressedTexImage3D(GLenum target, GLint level, GLenum internalformat,
                            GLsizei width, GLsizei height, GLsizei depth,
                            GLint border, GLsizei imageSize, const void *data) {
  if (!RequireContext()) return;
  Gl().textures().CompressedTexImage3D(target, level, internalformat, width,
                                       height, depth, border, imageSize, data);
}

void glCompressedTexSubImage2D(GLenum target, GLint level, GLint xoffset,
                               GLint yoffset, GLsizei width, GLsizei height,
                               GLenum format, GLsizei imageSize,
                               const void *data) {
  if (!RequireContext()) return;
  Gl().textures().CompressedTexSubImage2D(target, level, xoffset, yoffset,
                                          width, height, format, imageSize,
                                          data);
}

void glCompressedTexSubImage3D(GLenum target, GLint level, GLint xoffset,
                               GLint yoffset, GLint zoffset, GLsizei width,
                               GLsizei height, GLsizei depth, GLenum format,
                               GLsizei imageSize, const void *data) {
  if (!RequireContext()) return;
  Gl().textures().CompressedTexSubImage3D(target, level, xoffset, yoffset,
                                          zoffset, width, height, depth,
                                          format, imageSize, data);
}

void glCopyTexImage2D(GLenum target, GLint level, GLenum internalformat,
                      GLint x, GLint y, GLsizei width, GLsizei height,
                      GLint border) {
  if (!RequireContext()) return;
  (void)x;
  (void)y;
  Gl().CopyTexImage2D(target, level, internalformat, width, height, border);
}

void glCopyTexSubImage3D(GLenum target, GLint level, GLint xoffset,
                         GLint yoffset, GLint zoffset, GLint x, GLint y,
                         GLsizei width, GLsizei height) {
  if (!RequireContext()) return;
  (void)x;
  (void)y;
  Gl().textures().CopyTexSubImage3D(target, level, xoffset, yoffset, zoffset,
                                    width, height);
}

// --- Final23: integer clears + readback (spec 17.3, docs.gl verified) --------

void glClearBufferiv(GLenum buffer, GLint drawbuffer, const GLint *value) {
  if (!RequireContext()) return;
  Gl().framebuffers().ClearBufferiv(buffer, drawbuffer, value);
}

void glClearBufferuiv(GLenum buffer, GLint drawbuffer, const GLuint *value) {
  if (!RequireContext()) return;
  Gl().framebuffers().ClearBufferuiv(buffer, drawbuffer, value);
}

void glReadPixels(GLint x, GLint y, GLsizei width, GLsizei height,
                  GLenum format, GLenum type, void *pixels) {
  if (!RequireContext()) return;
  Gl().ReadPixels(x, y, width, height, format, type, pixels);
}

void glReadnPixels(GLint x, GLint y, GLsizei width, GLsizei height,
                   GLenum format, GLenum type, GLsizei bufSize, void *data) {
  if (!RequireContext()) return;
  Gl().ReadnPixels(x, y, width, height, format, type, bufSize, data);
}

// --- Final23: indexed enables (spec 14.1, gl4 refpage) -----------------------

void glEnablei(GLenum target, GLuint index) {
  if (!RequireContext()) return;
  Gl().raster().EnableIndexed(target, index);
}

void glDisablei(GLenum target, GLuint index) {
  if (!RequireContext()) return;
  Gl().raster().DisableIndexed(target, index);
}

GLboolean glIsEnabledi(GLenum target, GLuint index) {
  if (!RequireContext()) return GL_FALSE;
  return Gl().raster().IsEnabledIndexed(target, index);
}

// --- Final23: raster misc (desktop names validated, ES core stored) ----------

void glLogicOp(GLenum opcode) {
  if (!RequireContext()) return;
  Gl().raster().LogicOp(opcode);
}

void glPointSize(GLfloat size) {
  if (!RequireContext()) return;
  Gl().raster().PointSize(size);
}

void glBlendBarrier(void) {
  if (!RequireContext()) return;
  Gl().raster().BlendBarrier();
}

void glMinSampleShading(GLfloat value) {
  if (!RequireContext()) return;
  Gl().raster().MinSampleShading(value);
}

void glPrimitiveBoundingBox(GLfloat minX, GLfloat minY, GLfloat minZ,
                            GLfloat minW, GLfloat maxX, GLfloat maxY,
                            GLfloat maxZ, GLfloat maxW) {
  if (!RequireContext()) return;
  Gl().raster().PrimitiveBoundingBox(minX, minY, minZ, minW, maxX, maxY, maxZ,
                                     maxW);
}

// --- Final23: FBO + multisample storage + shader binary ----------------------

void glFramebufferTexture(GLenum target, GLenum attachment, GLuint texture,
                          GLint level) {
  if (!RequireContext()) return;
  Gl().framebuffers().FramebufferTexture(target, attachment, texture, level);
}

void glInvalidateSubFramebuffer(GLenum target, GLsizei numAttachments,
                                const GLenum *attachments, GLint x, GLint y,
                                GLsizei width, GLsizei height) {
  if (!RequireContext()) return;
  Gl().framebuffers().InvalidateSubFramebuffer(target, numAttachments,
                                               attachments, x, y, width,
                                               height);
}

void glTexStorage2DMultisample(GLenum target, GLsizei samples,
                               GLenum internalformat, GLsizei width,
                               GLsizei height, GLboolean fixedsamplelocations) {
  if (!RequireContext()) return;
  Gl().textures().TexStorage2DMultisample(target, samples, internalformat,
                                          width, height, fixedsamplelocations);
}

void glTexStorage3DMultisample(GLenum target, GLsizei samples,
                               GLenum internalformat, GLsizei width,
                               GLsizei height, GLsizei depth,
                               GLboolean fixedsamplelocations) {
  if (!RequireContext()) return;
  Gl().textures().TexStorage3DMultisample(target, samples, internalformat,
                                          width, height, depth,
                                          fixedsamplelocations);
}

void glShaderBinary(GLsizei count, const GLuint *shaders, GLenum binaryFormat,
                    const void *binary, GLsizei length) {
  if (!RequireContext()) return;
  Gl().shaders().ShaderBinary(count, shaders, binaryFormat, binary, length);
}

}  // extern "C"