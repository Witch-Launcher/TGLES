#ifndef TGLES_HOST_C_API_H
#define TGLES_HOST_C_API_H

// OpenGL ES 3.2 + EGL 1.5 C entry points served by TGL (Phase A subset).
// Includable from C and C++. Types match Khronos egl.h / GLES3/gl3platform.h
// (EGLDisplay/Config/Surface/Context are void*; scalars match gl_types.h),
// so app code written against this header looks like real ES 3.2 code.
// Values match docs/reference/egl.h and docs/reference/gl32.h exactly.
//
// Coverage: the trial subset (EGL setup + buffers/VAO/shaders/programs/
// textures/FBO/viewport/arrays + eglGetProcAddress). eglGetProcAddress
// returns REAL pointers for implemented entries and NULL otherwise.

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void* EGLDisplay;
typedef void* EGLConfig;
typedef void* EGLSurface;
typedef void* EGLContext;
typedef void* EGLClientBuffer;
typedef int EGLint;
typedef unsigned int EGLenum;
typedef unsigned int EGLBoolean;
typedef intptr_t EGLAttrib;

typedef unsigned int GLenum;
typedef unsigned char GLboolean;
typedef unsigned int GLbitfield;
typedef int GLint;
typedef int GLsizei;
typedef unsigned int GLuint;
typedef float GLfloat;
typedef float GLclampf;
typedef intptr_t GLintptr;
typedef intptr_t GLsizeiptr;
typedef char GLchar;

// EGL errors and attributes used by the subset.
#ifndef EGL_SUCCESS
#define EGL_SUCCESS 0x3000
#define EGL_BAD_DISPLAY 0x3008
#define EGL_BAD_PARAMETER 0x300C
#define EGL_NONE 0x3038
#define EGL_RED_SIZE 0x3024
#define EGL_WIDTH 0x3057
#define EGL_HEIGHT 0x3056
#define EGL_CONTEXT_MAJOR_VERSION 0x3098
#define EGL_CONTEXT_MINOR_VERSION 0x30FB
#endif

// GLES errors and enums used by the subset.
#ifndef GL_NO_ERROR
#define GL_NO_ERROR 0
#define GL_INVALID_ENUM 0x0500
#define GL_INVALID_VALUE 0x0501
#define GL_INVALID_OPERATION 0x0502
#define GL_TRIANGLES 0x0004
#define GL_ARRAY_BUFFER 0x8892
#define GL_STATIC_DRAW 0x88E4
#define GL_FLOAT 0x1406
#define GL_FALSE 0
#define GL_TRUE 1
#endif

// --- EGL 1.5 subset ---
EGLDisplay eglGetDisplay(void* native_display);
EGLBoolean eglInitialize(EGLDisplay dpy, EGLint* major, EGLint* minor);
EGLBoolean eglTerminate(EGLDisplay dpy);
EGLBoolean eglChooseConfig(EGLDisplay dpy, const EGLint* attribs,
                           EGLConfig* configs, EGLint config_size,
                           EGLint* num_config);
EGLContext eglCreateContext(EGLDisplay dpy, EGLConfig config,
                            EGLContext share_context, const EGLint* attribs);
EGLSurface eglCreatePbufferSurface(EGLDisplay dpy, EGLConfig config,
                                   const EGLint* attribs);
EGLBoolean eglDestroySurface(EGLDisplay dpy, EGLSurface surface);
EGLBoolean eglMakeCurrent(EGLDisplay dpy, EGLSurface draw, EGLSurface read,
                          EGLContext ctx);
EGLBoolean eglSwapBuffers(EGLDisplay dpy, EGLSurface surface);
EGLint eglGetError(void);
const char* eglQueryString(EGLDisplay dpy, EGLint name);
void* eglGetProcAddress(const char* procname);

// --- GLES 3.2 subset (trial surface) ---
void glGenBuffers(GLsizei n, GLuint* buffers);
void glBindBuffer(GLenum target, GLuint buffer);
void glBufferData(GLenum target, GLsizeiptr size, const void* data,
                  GLenum usage);
void glGenVertexArrays(GLsizei n, GLuint* arrays);
void glBindVertexArray(GLuint array);
void glEnableVertexAttribArray(GLuint index);
void glVertexAttribPointer(GLuint index, GLint size, GLenum type,
                           GLboolean normalized, GLsizei stride,
                           const void* pointer);
GLuint glCreateShader(GLenum type);
void glShaderSource(GLuint shader, GLsizei count, const char* const* string,
                    const GLint* length);
void glCompileShader(GLuint shader);
void glGetShaderiv(GLuint shader, GLenum pname, GLint* params);
GLuint glCreateProgram(void);
void glAttachShader(GLuint program, GLuint shader);
void glLinkProgram(GLuint program);
void glGetProgramiv(GLuint program, GLenum pname, GLint* params);
void glUseProgram(GLuint program);
GLint glGetUniformLocation(GLuint program, const char* name);
void glUniformMatrix4fv(GLint location, GLsizei count, GLboolean transpose,
                        const GLfloat* value);
void glGenTextures(GLsizei n, GLuint* textures);
void glBindTexture(GLenum target, GLuint texture);
void glTexImage2D(GLenum target, GLint level, GLint internalformat,
                  GLsizei width, GLsizei height, GLint border, GLenum format,
                  GLenum type, const void* pixels);
void glGenFramebuffers(GLsizei n, GLuint* framebuffers);
void glBindFramebuffer(GLenum target, GLuint framebuffer);
void glFramebufferTexture2D(GLenum target, GLenum attachment,
                            GLenum textarget, GLuint texture, GLint level);
void glViewport(GLint x, GLint y, GLsizei width, GLsizei height);
void glDrawArrays(GLenum mode, GLint first, GLsizei count);
GLenum glGetError(void);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // TGLES_HOST_C_API_H
